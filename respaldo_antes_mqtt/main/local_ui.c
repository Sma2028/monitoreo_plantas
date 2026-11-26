/*
 * local_ui.c — LED RGB, zumbador, pulsadores (ACK y BOOT) y sensor PIR.
 *
 * Código de colores (hoja 4 del esquemático):
 *   Blanco        inicialización
 *   Verde         monitoreo normal
 *   Rojo parpad.  alerta activa (+ zumbador intermitente)
 *   Amarillo      alerta reconocida (zumbador silenciado)
 *   Cian          falla de sensor
 *   Magenta       movimiento detectado (destello)
 *   Azul          modo configuración Wi-Fi (punto de acceso)
 *   Verde + destello azul cada 3 s: sin conexión Wi-Fi (sigue monitoreando)
 *
 * Pulsadores:
 *   ACK (SW3)            pulsación corta: reconocer alerta (F-10)
 *   BOOT (SW2) ≥ 3 s     reiniciar rangos a valores por defecto (F-12)
 *                        (mientras lo mantienes, el LED parpadea en blanco)
 *   BOOT (SW2) ≥ 10 s    borrar la red Wi-Fi guardada (LED azul rápido)
 */
#include "local_ui.h"
#include "board.h"
#include "app_state.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ui_local";

typedef struct {
    gpio_num_t pin;
    bool       raw, stable;      /* true = presionado */
    int64_t    last_change_ms;
    int64_t    press_start_ms;
} button_t;

static button_t s_ack  = { .pin = PIN_BTN_ACK };
static button_t s_boot = { .pin = PIN_BTN_BOOT };

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void set_rgb(bool r, bool g, bool b)
{
    /* Con LED de ánodo común el GPIO enciende en BAJO (LED_ACTIVE_LOW = 1) */
    gpio_set_level(PIN_LED_R, r ^ LED_ACTIVE_LOW);
    gpio_set_level(PIN_LED_G, g ^ LED_ACTIVE_LOW);
    gpio_set_level(PIN_LED_B, b ^ LED_ACTIVE_LOW);
}

/* Antirrebote por software (SR-17). Devuelve +1 al presionar, -1 al soltar, 0 sin cambio */
static int button_poll(button_t *b, int64_t t)
{
    bool raw = gpio_get_level(b->pin) == 0;         /* activo en bajo */
    if (raw != b->raw) { b->raw = raw; b->last_change_ms = t; }
    if (raw != b->stable && (t - b->last_change_ms) >= BTN_DEBOUNCE_MS) {
        b->stable = raw;
        if (raw) { b->press_start_ms = t; return 1; }
        return -1;
    }
    return 0;
}

static void ui_task(void *arg)
{
    bool pir_last = false;
    uint32_t last_motion_logged = 0;
    bool boot_long_fired = false, boot_vlong_fired = false;

    for (;;) {
        const int64_t t = now_ms();
        const uint32_t up = app_uptime_s();

        /* ---------- Pulsador ACK ---------- */
        if (button_poll(&s_ack, t) == 1) {
            if (!app_request_ack("pulsador")) ESP_LOGI(TAG, "ACK: no hay alerta pendiente");
        }

        /* ---------- Pulsador BOOT (pulsación larga) ---------- */
        int ev = button_poll(&s_boot, t);
        int64_t held = s_boot.stable ? (t - s_boot.press_start_ms) : 0;
        if (ev == 1) { boot_long_fired = boot_vlong_fired = false; }
        if (s_boot.stable && held >= BTN_LONG_MS) boot_long_fired = true;
        if (s_boot.stable && held >= BTN_VERY_LONG_MS) boot_vlong_fired = true;
        if (ev == -1) {
            if (boot_vlong_fired) {
                app_add_event(EV_WIFI, "Red Wi-Fi borrada con el pulsador BOOT");
                wifi_mgr_forget();
            } else if (boot_long_fired) {
                settings_reset_ranges();
                app_add_event(EV_INFO, "Rangos reiniciados con el pulsador BOOT");
            }
            boot_long_fired = boot_vlong_fired = false;
        }

        /* ---------- PIR (F-05, SR-05) ---------- */
        bool warming = up < PIR_WARMUP_S;
        bool pir = gpio_get_level(PIN_PIR) == 1;
        if (pir && !pir_last && !warming) {
            bool log_it = (last_motion_logged == 0) || (up - last_motion_logged >= 30);
            app_lock();
            app_status_t *st = app_status();
            st->motion_count++;
            st->last_motion_uptime = up ? up : 1;
            st->last_motion_epoch = app_epoch_now();
            app_unlock();
            if (log_it) {                       /* evita llenar el registro */
                last_motion_logged = up;
                app_add_event(EV_MOTION, "Movimiento detectado cerca de la planta");
            }
        }
        pir_last = pir;

        /* ---------- Leer estado para LED y zumbador ---------- */
        app_lock();
        app_status_t *st = app_status();
        st->pir_warming = warming;
        st->pir_now = pir;
        sys_state_t state = st->eng.state;
        bool fault = false;
        for (int i = 0; i < VAR_COUNT; i++) fault |= st->r[i].fault;
        bool motion = st->last_motion_uptime && (up - st->last_motion_uptime) < 2;
        bool ap = st->net_mode != NET_STA;
        bool connected = st->sta_connected;
        app_unlock();

        /* ---------- Patrones ---------- */
        bool blink_slow = (t / 500) % 2;          /* 1 Hz       */
        bool blink_fast = (t / 125) % 2;          /* 4 Hz       */
        bool buzzer = false;

        if (s_boot.stable && held >= BTN_VERY_LONG_MS) {
            set_rgb(0, 0, blink_fast);
        } else if (s_boot.stable && held >= BTN_LONG_MS) {
            set_rgb(blink_fast, blink_fast, blink_fast);
        } else if (state == SYS_INIT) {
            set_rgb(1, 1, 1);
        } else if (state == SYS_ALERT) {
            set_rgb(blink_slow, 0, 0);
            buzzer = (t % 1000) < 200;            /* pitido 200 ms cada 1 s (SR-15) */
        } else if (state == SYS_ACKED) {
            set_rgb(1, 1, 0);                     /* amarillo fijo */
        } else if (fault) {
            set_rgb(0, 1, 1);
        } else if (motion) {
            set_rgb(1, 0, 1);
        } else if (ap && !connected) {
            set_rgb(0, 0, 1);
        } else if (!connected) {
            set_rgb(0, 1, (t % 3000) < 150);
        } else {
            set_rgb(0, 1, 0);
        }
        gpio_set_level(PIN_BUZZER, buzzer);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t local_ui_start(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B) |
                        (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    set_rgb(1, 1, 1);                           /* blanco: inicialización */
    gpio_set_level(PIN_BUZZER, 0);

    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << PIN_BTN_ACK) | (1ULL << PIN_BTN_BOOT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       /* refuerza las pull-up externas */
    };
    gpio_config(&btn);

    gpio_config_t pir = {
        .pin_bit_mask = (1ULL << PIN_PIR),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = PIR_INTERNAL_PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&pir);

    BaseType_t ok = xTaskCreate(ui_task, "ui_local", 4096, NULL, 6, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
