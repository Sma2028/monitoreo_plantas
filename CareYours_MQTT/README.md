# CareYours MQTT · Prueba en ESP32 DevKit

Este sketch Arduino implementa la prueba solicitada de potenciómetro y LED.
El proyecto ESP-IDF con sensores, OLED, alertas y portal local se conserva por separado.

1. Abre CareYours_MQTT.ino en Arduino IDE. Selecciona ESP32 Dev Module y el puerto COM.
2. Instala PubSubClient de Nick O'Leary desde el gestor de bibliotecas. WiFi.h viene con el soporte de placas ESP32 de Espressif.
3. En el iPhone activa Compartir Internet y, si aparece, Maximizar compatibilidad.
4. Conecta el potenciómetro entre 3V3 y GND, con el cursor en GPIO34. Usa el LED de GPIO2 si tu DevKit lo tiene, o un LED externo con resistencia de 330 ohmios en serie hacia GND.
5. Carga el sketch y abre el monitor serie a 115200. Espera los mensajes de Wi-Fi y MQTT conectado.
6. Abre index.html en un computador con internet. Gira el potenciómetro y verifica el valor. Usa los botones para cambiar el LED.

ESP32: broker.hivemq.com:1883. Navegador: wss://broker.hivemq.com:8884/mqtt.
Los tópicos son instrumentacion/grupo1/potenciometro e instrumentacion/grupo1/control.
HiveMQ es público. Úsalo para pruebas y evita publicar datos privados.
La clave Wi-Fi queda en wifi_config.h. Mantén ese archivo privado al compartir el proyecto.
