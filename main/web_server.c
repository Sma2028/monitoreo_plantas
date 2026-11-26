#include "web_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "app_state.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "mqtt_planta.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* ------------------------------------------------------------------ */
/*                            Utilidades                               */
/* ------------------------------------------------------------------ */
static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(req, txt);
    free(txt);
    return e;
}

static esp_err_t send_msg(httpd_req_t *req, const char *status, bool ok, const char *msg)
{
    httpd_resp_set_status(req, status);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", ok);
    cJSON_AddStringToObject(r, ok ? "msg" : "error", msg);
    return send_json(req, r);
}

static bool check_auth(httpd_req_t *req)
{
    char given[SETTINGS_PASS_LEN + 1] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "X-Auth", given, sizeof(given)) != ESP_OK) return false;
    settings_t s;
    settings_get(&s);
    return strcmp(given, s.web_pass) == 0;
}

#define REQUIRE_AUTH(req) \
    do { if (!check_auth(req)) return send_msg(req, "401 Unauthorized", false, "Contraseña incorrecta"); } while (0)

static cJSON *read_json_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 2048) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[len] = 0;
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j;
}

static bool json_num(cJSON *obj, const char *key, double *out)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) return false;
    *out = it->valuedouble;
    return true;
}

/* ------------------------------------------------------------------ */
/*                              Página                                 */
/* ------------------------------------------------------------------ */
static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

/* Cualquier otra ruta -> página principal (necesario para el portal cautivo) */
static esp_err_t h_catch_all(httpd_req_t *req)
{
    if (strncmp(req->uri, "/api/", 5) == 0)
        return send_msg(req, "404 Not Found", false, "Ruta no encontrada");
    app_lock();
    bool sta_only = app_status()->net_mode == NET_STA;
    app_unlock();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", sta_only ? "/" : "http://" WIFI_AP_IP "/");
    return httpd_resp_send(req, NULL, 0);
}

/* ------------------------------------------------------------------ */
/*                               API                                   */
/* ------------------------------------------------------------------ */
static void add_range(cJSON *o, const range_t *r)
{
    cJSON_AddNumberToObject(o, "min", r->min);
    cJSON_AddNumberToObject(o, "max", r->max);
    cJSON_AddNumberToObject(o, "hyst", r->hyst);
}

static esp_err_t h_status(httpd_req_t *req)
{
    app_status_t *st = malloc(sizeof(app_status_t));
    if (!st) return httpd_resp_send_500(req);
    app_snapshot(st);
    settings_t cfg;
    settings_get(&cfg);
    const uint32_t up = app_uptime_s();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", cfg.plant_name);
    cJSON_AddStringToObject(root, "state", alerts_state_name(st->eng.state));
    cJSON_AddNumberToObject(root, "uptime", up);
    cJSON_AddNumberToObject(root, "time", (double)app_epoch_now());
    cJSON_AddNumberToObject(root, "period", cfg.sample_period_s);
    cJSON_AddNumberToObject(root, "cycles", st->cycles);

    cJSON *vars = cJSON_AddObjectToObject(root, "vars");
    for (int i = 0; i < VAR_COUNT; i++) {
        cJSON *v = cJSON_AddObjectToObject(vars, alerts_var_key((var_id_t)i));
        if (st->r[i].valid) cJSON_AddNumberToObject(v, "v", st->r[i].value);
        else cJSON_AddNullToObject(v, "v");
        cJSON_AddBoolToObject(v, "fault", st->r[i].fault);
        cJSON_AddBoolToObject(v, "alarm", st->eng.alarm[i]);
        cJSON_AddNumberToObject(v, "dir", st->eng.dir[i]);
        add_range(v, &cfg.range[i]);
    }
    cJSON *soil = cJSON_AddObjectToObject(root, "soil");
    cJSON_AddNumberToObject(soil, "mv", st->soil_mv);
    cJSON_AddBoolToObject(soil, "saturated", st->soil_saturated);
    cJSON_AddNumberToObject(soil, "dry", cfg.soil_dry_mv);
    cJSON_AddNumberToObject(soil, "wet", cfg.soil_wet_mv);

    /* Datos para la pestaña Calibrar */
    cJSON *raw = cJSON_AddObjectToObject(root, "raw");
    static const char *rk[3] = { "t", "hr", "lux" };
    for (int k = 0; k < 3; k++) {
        if (st->raw_valid[k]) cJSON_AddNumberToObject(raw, rk[k], st->raw_avg[k]);
        else cJSON_AddNullToObject(raw, rk[k]);
    }
    if (st->boot_t_valid) cJSON_AddNumberToObject(raw, "boot_t", st->boot_t);
    else cJSON_AddNullToObject(raw, "boot_t");
    cJSON *cal = cJSON_AddObjectToObject(root, "cal");
    cJSON_AddNumberToObject(cal, "t_offset", cfg.t_offset);
    cJSON_AddNumberToObject(cal, "hr_gain", cfg.hr_gain);
    cJSON_AddNumberToObject(cal, "hr_offset", cfg.hr_offset);
    cJSON_AddNumberToObject(cal, "hr_p1_ref", cfg.hr_p1_ref);
    cJSON_AddNumberToObject(cal, "hr_p1_raw", cfg.hr_p1_raw);
    cJSON_AddNumberToObject(cal, "lux_k", cfg.lux_k);

    cJSON *pir = cJSON_AddObjectToObject(root, "pir");
    cJSON_AddBoolToObject(pir, "now", st->pir_now);
    cJSON_AddBoolToObject(pir, "warming", st->pir_warming);
    cJSON_AddNumberToObject(pir, "count", st->motion_count);
    if (st->last_motion_uptime) cJSON_AddNumberToObject(pir, "ago", up - st->last_motion_uptime);
    else cJSON_AddNullToObject(pir, "ago");

    cJSON *w = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(w, "connected", st->sta_connected);
    cJSON_AddBoolToObject(w, "ap", st->net_mode != NET_STA);
    cJSON_AddBoolToObject(w, "configured", st->has_credentials);
    cJSON_AddStringToObject(w, "ssid", st->sta_ssid);
    cJSON_AddStringToObject(w, "ip", st->sta_ip);
    cJSON_AddNumberToObject(w, "rssi", st->rssi);
    cJSON_AddStringToObject(w, "ap_ssid", st->ap_ssid);
    cJSON_AddBoolToObject(w, "mqtt", mqtt_planta_conectado());

    cJSON_AddBoolToObject(root, "oled", st->oled_present);

    cJSON *evs = cJSON_AddArrayToObject(root, "events");
    for (int i = 0; i < st->ev_count; i++) {           /* más reciente primero */
        const event_t *e = &st->events[(st->ev_head + EVENT_LOG_LEN - 1 - i) % EVENT_LOG_LEN];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "t", (double)e->epoch);
        cJSON_AddNumberToObject(o, "ago", up - e->uptime_s);
        cJSON_AddNumberToObject(o, "k", e->kind);
        cJSON_AddStringToObject(o, "msg", e->text);
        cJSON_AddItemToArray(evs, o);
    }
    free(st);
    return send_json(req, root);
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    settings_t s;
    settings_get(&s);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", s.plant_name);
    cJSON_AddNumberToObject(root, "persistence", s.persistence);
    cJSON_AddNumberToObject(root, "period", s.sample_period_s);
    cJSON_AddNumberToObject(root, "soil_dry", s.soil_dry_mv);
    cJSON_AddNumberToObject(root, "soil_wet", s.soil_wet_mv);
    cJSON *rg = cJSON_AddObjectToObject(root, "ranges");
    for (int i = 0; i < VAR_COUNT; i++)
        add_range(cJSON_AddObjectToObject(rg, alerts_var_key((var_id_t)i)), &s.range[i]);
    return send_json(req, root);
}

static esp_err_t h_config_post(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    if (!j) return send_msg(req, "400 Bad Request", false, "JSON inválido");

    settings_t s;
    settings_get(&s);                 /* se parte de lo actual: se aceptan cambios parciales */
    double d;
    cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
    if (cJSON_IsString(name)) strlcpy(s.plant_name, name->valuestring, sizeof(s.plant_name));
    if (json_num(j, "persistence", &d)) s.persistence = (uint8_t)d;
    if (json_num(j, "period", &d)) s.sample_period_s = (uint16_t)d;
    cJSON *rg = cJSON_GetObjectItemCaseSensitive(j, "ranges");
    for (int i = 0; i < VAR_COUNT && rg; i++) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(rg, alerts_var_key((var_id_t)i));
        if (!o) continue;
        if (json_num(o, "min", &d)) s.range[i].min = (float)d;
        if (json_num(o, "max", &d)) s.range[i].max = (float)d;
        if (json_num(o, "hyst", &d)) s.range[i].hyst = (float)d;
    }
    cJSON_Delete(j);

    char err[128];
    if (settings_apply(&s, err, sizeof(err)) != ESP_OK)       /* F-07 / SR-23 */
        return send_msg(req, "400 Bad Request", false, err);
    app_add_event(EV_INFO, "Rangos actualizados desde la web");
    return send_msg(req, "200 OK", true, "Rangos guardados");
}

static esp_err_t h_ack(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    bool changed = app_request_ack("web");
    return send_msg(req, "200 OK", true, changed ? "Alerta reconocida" : "No había alerta pendiente");
}

static esp_err_t h_calibrate(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    cJSON *p = j ? cJSON_GetObjectItemCaseSensitive(j, "point") : NULL;
    if (!cJSON_IsString(p)) { cJSON_Delete(j); return send_msg(req, "400 Bad Request", false, "Falta 'point'"); }
    bool dry = strcmp(p->valuestring, "dry") == 0;
    bool wet = strcmp(p->valuestring, "wet") == 0;
    cJSON_Delete(j);
    if (!dry && !wet) return send_msg(req, "400 Bad Request", false, "point debe ser 'dry' o 'wet'");

    app_lock();
    int mv = app_status()->soil_mv;
    bool fault = app_status()->r[VAR_SOIL].fault || !app_status()->r[VAR_SOIL].valid;
    app_unlock();
    if (fault || mv <= 0) return send_msg(req, "409 Conflict", false, "El sensor de suelo no tiene lectura válida");

    settings_t s;
    settings_get(&s);
    if (dry) s.soil_dry_mv = mv; else s.soil_wet_mv = mv;
    char err[128];
    if (settings_apply(&s, err, sizeof(err)) != ESP_OK)
        return send_msg(req, "400 Bad Request", false, err);
    app_add_event(EV_INFO, "Calibración de suelo %s = %d mV", dry ? "seco" : "húmedo", mv);
    char msg[64];
    snprintf(msg, sizeof(msg), "Punto %s guardado: %d mV", dry ? "seco" : "húmedo", mv);
    return send_msg(req, "200 OK", true, msg);
}

/* POST /api/cal/set  {"t_offset":-1.4,"hr_gain":1,"hr_offset":3,"hr_p1_ref":75,"hr_p1_raw":72,"lux_k":1.15}
 * Cambios parciales: solo se modifican los campos enviados. */
static esp_err_t h_cal_set(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    if (!j) return send_msg(req, "400 Bad Request", false, "JSON inválido");
    settings_t s;
    settings_get(&s);
    double d;
    if (json_num(j, "t_offset", &d))  s.t_offset = (float)d;
    if (json_num(j, "hr_gain", &d))   s.hr_gain = (float)d;
    if (json_num(j, "hr_offset", &d)) s.hr_offset = (float)d;
    if (json_num(j, "hr_p1_ref", &d)) s.hr_p1_ref = (float)d;
    if (json_num(j, "hr_p1_raw", &d)) s.hr_p1_raw = (float)d;
    if (json_num(j, "lux_k", &d))     s.lux_k = (float)d;
    cJSON_Delete(j);
    char err[128];
    if (settings_apply(&s, err, sizeof(err)) != ESP_OK)
        return send_msg(req, "400 Bad Request", false, err);
    app_add_event(EV_INFO, "Calibración actualizada");
    return send_msg(req, "200 OK", true, "Calibración guardada");
}

/* POST /api/cal/reset {"sensor":"t"|"hr"|"lux"|"soil"|"all"} */
static esp_err_t h_cal_reset(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    cJSON *w = j ? cJSON_GetObjectItemCaseSensitive(j, "sensor") : NULL;
    char which[8] = "all";
    if (cJSON_IsString(w)) strlcpy(which, w->valuestring, sizeof(which));
    cJSON_Delete(j);
    if (settings_reset_calibration(which) != ESP_OK)
        return send_msg(req, "400 Bad Request", false, "No se pudo restablecer");
    app_add_event(EV_INFO, "Calibración restablecida (%s)", which);
    return send_msg(req, "200 OK", true, "Calibración restablecida");
}

static esp_err_t h_reset_ranges(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    settings_reset_ranges();
    app_add_event(EV_INFO, "Rangos reiniciados desde la web");
    return send_msg(req, "200 OK", true, "Rangos restablecidos a los valores por defecto");
}

static esp_err_t h_password(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    cJSON *n = j ? cJSON_GetObjectItemCaseSensitive(j, "new") : NULL;
    if (!cJSON_IsString(n) || strlen(n->valuestring) >= SETTINGS_PASS_LEN) {
        cJSON_Delete(j);
        return send_msg(req, "400 Bad Request", false, "Contraseña nueva inválida (máx. 32 caracteres)");
    }
    settings_t s;
    settings_get(&s);
    strlcpy(s.web_pass, n->valuestring, sizeof(s.web_pass));
    cJSON_Delete(j);
    char err[128];
    if (settings_apply(&s, err, sizeof(err)) != ESP_OK)
        return send_msg(req, "400 Bad Request", false, err);
    return send_msg(req, "200 OK", true, "Contraseña cambiada");
}

static esp_err_t h_scan(httpd_req_t *req)
{
    wifi_scan_item_t *items = calloc(15, sizeof(wifi_scan_item_t));
    if (!items) return httpd_resp_send_500(req);
    int n = wifi_mgr_scan(items, 15);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", items[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", items[i].rssi);
        cJSON_AddBoolToObject(o, "secure", items[i].secure);
        cJSON_AddItemToArray(arr, o);
    }
    free(items);
    return send_json(req, arr);
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    cJSON *j = read_json_body(req);
    cJSON *ssid = j ? cJSON_GetObjectItemCaseSensitive(j, "ssid") : NULL;
    cJSON *pass = j ? cJSON_GetObjectItemCaseSensitive(j, "pass") : NULL;
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(ssid))
        e = wifi_mgr_set_credentials(ssid->valuestring, cJSON_IsString(pass) ? pass->valuestring : "");
    cJSON_Delete(j);
    if (e != ESP_OK)
        return send_msg(req, "400 Bad Request", false, "Red o contraseña inválida (la clave Wi-Fi debe tener 8 a 63 caracteres)");
    return send_msg(req, "200 OK", true, "Conectando… revisa en unos segundos la nueva IP en el panel");
}

/* ------------------------------------------------------------------ */
esp_err_t web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = 10;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t srv = NULL;
    esp_err_t e = httpd_start(&srv, &cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "No se pudo iniciar el servidor: %s", esp_err_to_name(e)); return e; }

    const httpd_uri_t routes[] = {
        { .uri = "/",                 .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status",       .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/config",       .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/config",       .method = HTTP_POST, .handler = h_config_post },
        { .uri = "/api/ack",          .method = HTTP_POST, .handler = h_ack },
        { .uri = "/api/calibrate",    .method = HTTP_POST, .handler = h_calibrate },
        { .uri = "/api/reset_ranges", .method = HTTP_POST, .handler = h_reset_ranges },
        { .uri = "/api/cal/set",      .method = HTTP_POST, .handler = h_cal_set },
        { .uri = "/api/cal/reset",    .method = HTTP_POST, .handler = h_cal_reset },
        { .uri = "/api/password",     .method = HTTP_POST, .handler = h_password },
        { .uri = "/api/scan",         .method = HTTP_GET,  .handler = h_scan },
        { .uri = "/api/wifi",         .method = HTTP_POST, .handler = h_wifi },
        { .uri = "/*",                .method = HTTP_GET,  .handler = h_catch_all },  /* siempre al final */
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        httpd_register_uri_handler(srv, &routes[i]);
    ESP_LOGI(TAG, "Servidor web iniciado en el puerto 80");
    return ESP_OK;
}
