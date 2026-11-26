# Monitoreo de plantas — Firmware ESP-IDF (ESP32-C3)

Firmware para la PCB del proyecto **DR-PS-02** (esquemático Revisión A, ESP32-C3-WROOM-02-N4).
Lee AHT10, BH1750, sensor capacitivo de suelo y PIR HC-SR501; maneja alertas con persistencia
e histéresis; LED RGB, zumbador y pulsadores; pantalla OLED opcional; y sirve una página web
para ver mediciones, reconocer alertas, configurar rangos, calibrar el suelo y cambiar el Wi-Fi.

---

## 1. Qué necesitas

| Cosa | Detalle |
|---|---|
| ESP-IDF | **v5.3 o superior** (v5.4 / v5.5 recomendadas). Se instala con el instalador de Espressif o con la extensión "ESP-IDF" de VS Code. |
| Cable | USB-C **de datos** (algunos cables solo cargan). |
| Internet | Solo la primera compilación: el gestor de componentes descarga `espressif/mdns`. |

> ¿Usas ESP-IDF v6? En v6 el componente `json` (cJSON) pasó al gestor de componentes. Añade en
> `main/idf_component.yml` la línea `espressif/cjson: "*"` debajo de `dependencies:`.

## 2. Estructura del proyecto

```
monitoreo_plantas/
├── CMakeLists.txt          proyecto
├── sdkconfig.defaults      ESP32-C3, consola USB, watchdog, partición grande
├── README.md               este archivo
├── docs/                   explicación de resistencias y revisión del esquemático
└── main/
    ├── board.h             ← TODOS los pines de la PCB (cámbialos solo aquí)
    ├── main.c              arranque + tarea de sensores (muestreo, filtrado, alertas, OLED)
    ├── alerts.c/.h         máquina de estados: persistencia, histéresis, reconocimiento
    ├── settings.c/.h       rangos y calibración en NVS, con validación
    ├── app_state.c/.h      estado compartido entre tareas + registro de eventos
    ├── i2c_bus.c/.h        bus I2C a 100 kHz (pull-ups externas de 4,7 kΩ)
    ├── aht10.c             temperatura y humedad (0x38)
    ├── bh1750.c            luz (0x23 / 0x5C)
    ├── soil.c              suelo por ADC1 (IO0) con calibración de fábrica del ADC
    ├── oled.c/.h           pantalla SSD1306/SH1106 128x64 (opcional, 0x3C)
    ├── local_ui.c/.h       LED RGB, zumbador, pulsadores ACK y BOOT, PIR
    ├── wifi_mgr.c/.h       Wi-Fi + reconexión + punto de acceso de configuración
    ├── dns_server.c/.h     portal cautivo (abre la página sola en el celular)
    ├── web_server.c/.h     servidor HTTP + API REST
    └── web/index.html      la página web (va embebida dentro del firmware)
```

## 3. Compilar y grabar (paso a paso)

### Opción A — Terminal de ESP-IDF

```bash
cd monitoreo_plantas
idf.py set-target esp32c3        # solo la primera vez (crea sdkconfig desde sdkconfig.defaults)
idf.py build
idf.py -p PUERTO flash monitor   # PUERTO: COM5 en Windows, /dev/ttyACM0 en Linux, /dev/cu.usbmodem* en Mac
```

Para salir del monitor: `Ctrl + ]`.

### Opción B — VS Code con la extensión ESP-IDF

1. *File → Open Folder* → `monitoreo_plantas`.
2. Abajo en la barra: elige **esp32c3** como target y el puerto COM.
3. Botón 🔥 **Build, Flash and Monitor**.

### Si no aparece el puerto o falla la grabación

1. Mantén presionado **BOOT**.
2. Pulsa y suelta **RESET** (o conecta el USB) sin soltar BOOT.
3. Suelta BOOT. Ahora el C3 está en modo descarga: vuelve a ejecutar `flash`.
4. Después de grabar, pulsa **RESET** una vez.

> Respaldo: también puedes grabar por **J2** con un adaptador USB-serie (solo TX, RX y GND; **no** conectes su 3V3).

## 4. Primera puesta en marcha

1. Al encender, el LED se pone **blanco** (inicialización). En la consola verás el escaneo I2C:
   ```
   Dispositivo en 0x23   ← BH1750
   Dispositivo en 0x38   ← AHT10
   Dispositivo en 0x3C   ← OLED (si la tienes)
   ```
   Si falta alguno, revisa el orden de pines del módulo en su header.
2. Sin red guardada el LED pasa a **azul**: el ESP32 crea la red **`Planta-XXXX`**, clave **`planta1234`**.
3. Conéctate desde el celular. La página se abre sola (si no, entra a `http://192.168.4.1`).
4. Pestaña **Wi-Fi y ajustes** → *Buscar redes* → elige la tuya → clave → *Conectar*.
   La contraseña de la página es **`planta123`** (cámbiala ahí mismo).
5. Cuando conecte, el panel muestra la IP. Vuelve a tu Wi-Fi normal y entra a esa IP o a
   **`http://planta.local`**.
6. **Calibra el suelo** (pestaña *Calibrar suelo*): punto seco en el aire, punto húmedo en agua.
7. Ajusta los **Rangos** de tu planta y guarda.

## 5. Funcionamiento

| Requisito | Cómo está implementado |
|---|---|
| SR-06 Muestreo | Cada 5 s (configurable 2–60 s). Suelo: 16 muestras, se descartan extremos y se promedia. |
| SR-07 Falla de sensor | 3 lecturas seguidas sin respuesta o fuera de rango físico → "Falla" (LED cian). Las demás variables siguen. |
| SR-12 Persistencia | Alerta solo si la variable sigue fuera de rango 3 lecturas seguidas (configurable). |
| SR-13 Histéresis | Para volver a normal debe entrar a `[mín + h, máx − h]`. |
| SR-15/16 | Alerta: LED rojo parpadeante + pitido 200 ms cada 1 s. Reconocida: LED amarillo fijo, sin pitido. |
| SR-17/18 | ACK silencia sin borrar la condición. Si otra variable sale de rango, vuelve a sonar. |
| F-12 | BOOT ≥ 3 s y soltar: rangos de ejemplo. BOOT ≥ 10 s: borra el Wi-Fi. |
| SR-25 | Reintenta conectar cada 5 s; si falla 60 s abre además el punto de acceso. Las alertas locales nunca dependen del Wi-Fi. |
| SR-26 | NVS se escribe solo si un parámetro cambió. |
| NF-11 | Cambios protegidos con contraseña (cabecera `X-Auth`); la clave Wi-Fi nunca se devuelve. |
| NF-12 | Watchdog de 15 s sobre la tarea de sensores: si se cuelga, el ESP32 se reinicia solo. |

## 6. API (por si quieres probar con el navegador o con Postman)

| Método | Ruta | Contraseña |
|---|---|---|
| GET | `/api/status` | no |
| GET | `/api/config` | no |
| POST | `/api/config` `{"ranges":{"soil":{"min":30,"max":70,"hyst":3}}}` | sí |
| POST | `/api/ack` | sí |
| POST | `/api/calibrate` `{"point":"dry"}` o `"wet"` | sí |
| POST | `/api/reset_ranges` | sí |
| POST | `/api/password` `{"new":"..."}` | sí |
| GET | `/api/scan` | no |
| POST | `/api/wifi` `{"ssid":"..","pass":".."}` | sí |

Ejemplo: `curl -X POST http://planta.local/api/ack -H "X-Auth: planta123"`

## 7. Cosas que puedes cambiar fácilmente

- **Pines**: `main/board.h`.
- **Rangos de fábrica, contraseña por defecto**: `settings_defaults()` en `main/settings.c`.
- **Zona horaria** (Colombia, UTC−5): `start_sntp_once()` en `main/wifi_mgr.c`.
- **Clave del punto de acceso**: `WIFI_AP_PASSWORD` en `main/wifi_mgr.h`.
- **La página web**: `main/web/index.html`. Puedes abrirla directo en el navegador del PC: si no
  encuentra el ESP32 entra en *modo demostración* con datos simulados, ideal para diseñarla.
- **OLED de 1,3" (SH1106)**: si la imagen sale corrida, `OLED_COL_OFFSET` = 2 en `main/oled.c`.

## 8. Problemas frecuentes

| Síntoma | Causa probable |
|---|---|
| No compila: `i2c_master.h` no encontrado | ESP-IDF anterior a 5.2. Actualiza a 5.3+. |
| Error descargando `espressif/mdns` | Sin internet en la primera compilación. |
| Ningún dispositivo I2C | Falta GND/VCC, SDA y SCL invertidos, o faltan R7/R8. |
| El suelo marca siempre 0 % o 100 % | Falta calibrar, o el sensor satura por encima de ~2,5 V (ver `docs/`). |
| Temperatura 2–4 °C más alta | Calor de la placa: aleja el AHT10 de U1/U2. |
| LED azul se ve un instante al arrancar | Normal: IO8 tiene la pull-up de strapping R6. |
| Olvidé la contraseña de la página | `idf.py erase-flash` y volver a grabar (se pierden Wi-Fi y rangos). |

---

## Prueba de concepto en protoboard (ESP32 DevKit)

El mismo código compila para la DevKit con ESP32 clásico (ESP32-WROOM-32). Los pines están en
`main/board.h` (bloque `CONFIG_IDF_TARGET_ESP32`) y el diagrama en `docs/conexiones_prueba_concepto_esp32.png`.

```bash
idf.py set-target esp32        # importante: esp32, no esp32c3
idf.py build
idf.py -p PUERTO flash monitor # COMx en Windows (driver CP210x o CH340)
```

| Componente | Conexión |
|---|---|
| AHT10, BH1750, OLED | VCC→3V3, GND→GND, SDA→GPIO32, SCL→GPIO33 (BH1750: ADDR→GND) |
| Sensor de suelo | VCC→3V3, GND→GND, AOUT→1 kΩ→GPIO34 |
| PIR HC-SR501 | VCC→VIN (5 V), GND→GND, OUT→1 kΩ→GPIO35 |
| Zumbador | (+)→VIN, (−)→colector del NPN; emisor→GND; base←1 kΩ←GPIO12; base→10 kΩ→GND (obligatoria) |
| LED RGB | R←120/150 Ω←GPIO25, G←100 Ω←GPIO26, B←100 Ω←GPIO27; común a GND (cátodo) o a 3V3 (ánodo) |
| Pulsador ACK | GPIO13 ↔ GND |
| Pulsador BOOT | el de la DevKit (GPIO0) |

**LED de ánodo común:** cambia `#define LED_ACTIVE_LOW 0` a `1` en `main/board.h` y conecta el común a **3V3** (nunca a 5 V).

---

## Usar PlatformIO en VS Code

1. Instala **VS Code** y, en Extensiones (`Ctrl+Shift+X`), la extensión **PlatformIO IDE**.
2. *File » Open Folder…* y abre la carpeta `monitoreo_plantas` (la que tiene `platformio.ini`).
3. Abajo, en la barra azul, elige el entorno **env:esp32dev** (prueba de concepto) o **env:pcb_esp32c3** (PCB).
4. **✓ Build** compila. La primera vez descarga ESP-IDF y tarda varios minutos.
5. **→ Upload** graba. **🔌 Monitor** abre la consola.

Si sale un error de versión de ESP-IDF o de `i2c_master.h`: *PIO Home » Platforms » Espressif 32 » Update*.
