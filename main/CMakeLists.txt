# Sin REQUIRES: el componente "main" puede usar todos los componentes de ESP-IDF.
idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "settings.c"
        "alerts.c"
        "i2c_bus.c"
        "aht10.c"
        "bh1750.c"
        "soil.c"
        "oled.c"
        "local_ui.c"
        "wifi_mgr.c"
        "dns_server.c"
        "web_server.c"
        "cfg_json.c"
        "mqtt_planta.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
)

# Evita que avisos de texto truncado detengan la compilacion
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation -Wno-format-truncation)
