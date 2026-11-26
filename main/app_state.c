#include "app_state.h"
#include "mqtt_planta.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "estado";
static app_status_t s_st;
static SemaphoreHandle_t s_mtx;

void app_state_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    alerts_init(&s_st.eng);
    s_st.pir_warming = true;
    s_mtx = xSemaphoreCreateMutex();
}

void app_lock(void)   { xSemaphoreTake(s_mtx, portMAX_DELAY); }
void app_unlock(void) { xSemaphoreGive(s_mtx); }
app_status_t *app_status(void) { return &s_st; }

void app_snapshot(app_status_t *out)
{
    app_lock();
    *out = s_st;
    app_unlock();
}

uint32_t app_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

time_t app_epoch_now(void)
{
    time_t now = time(NULL);
    return (now > 1700000000) ? now : 0;   /* antes de NTP el reloj marca 1970 */
}

void app_add_event(event_kind_t kind, const char *fmt, ...)
{
    event_t ev = { .epoch = app_epoch_now(), .uptime_s = app_uptime_s(), .kind = (uint8_t)kind };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev.text, sizeof(ev.text), fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "Evento: %s", ev.text);
    app_lock();
    s_st.events[s_st.ev_head] = ev;
    s_st.ev_head = (s_st.ev_head + 1) % EVENT_LOG_LEN;
    if (s_st.ev_count < EVENT_LOG_LEN) s_st.ev_count++;
    app_unlock();
    mqtt_planta_evento(ev.text);        /* también se envía a la página por MQTT */
}

bool app_request_ack(const char *origin)
{
    app_lock();
    bool changed = alerts_ack(&s_st.eng);
    app_unlock();
    if (changed) app_add_event(EV_INFO, "Alerta reconocida (%s)", origin);
    return changed;
}
