#include <WiFi.h>
#include <PubSubClient.h>
#include "wifi_config.h"

constexpr char MQTT_HOST[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char TOPIC_POT[] = "instrumentacion/grupo1/potenciometro";
constexpr char TOPIC_LED[] = "instrumentacion/grupo1/control";
constexpr int PIN_LED = 2;
constexpr int PIN_POT = 34;

WiFiClient tcp;
PubSubClient mqtt(tcp);
uint32_t lastRetry = 0;
uint32_t lastPublish = 0;
bool tried = false;
bool wifiWasConnected = false;

void onMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_LED) != 0 || length != 1) return;
  if (payload[0] != '0' && payload[0] != '1') return;
  digitalWrite(PIN_LED, payload[0] == '1' ? HIGH : LOW);
  Serial.println(payload[0] == '1' ? "LED encendido" : "LED apagado");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  analogReadResolution(12);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setSocketTimeout(3);
  Serial.println("Conectando al Wi-Fi...");
}

void loop() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      tcp.stop();
      Serial.println("Wi-Fi desconectado, esperando reconexion...");
      wifiWasConnected = false;
      tried = false;
    }
    delay(20);
    return;
  }
  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.print("Wi-Fi conectado. IP: ");
    Serial.println(WiFi.localIP());
  }
  if (!mqtt.connected()) {
    if (tried && now - lastRetry < 5000) {
      delay(20);
      return;
    }
    tried = true;
    lastRetry = now;
    const String id = "careyours-" + WiFi.macAddress();
    if (!mqtt.connect(id.c_str())) {
      Serial.printf("MQTT fallo, rc=%d. Reintento en 5 s.\n", mqtt.state());
      return;
    }
    if (!mqtt.subscribe(TOPIC_LED)) {
      Serial.println("Fallo al suscribirse al control.");
      mqtt.disconnect();
      return;
    }
    Serial.println("MQTT conectado y suscrito.");
  }
  if (mqtt.loop() && now - lastPublish >= 2000) {
    lastPublish = now;
    char value[8];
    snprintf(value, sizeof(value), "%d", analogRead(PIN_POT));
    if (mqtt.publish(TOPIC_POT, value)) Serial.printf("ADC: %s\n", value);
  }
}
