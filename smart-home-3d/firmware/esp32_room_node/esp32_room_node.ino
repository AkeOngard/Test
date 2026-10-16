// ESP32 room node for บ้านอัจฉริยะ 3D
// - controls the bedroom ceiling light (LED/MOSFET on PWM pin, or a relay)
// - reports temperature and humidity from a DHT22
//
// Topics (BASE must match "Topic หลัก" in the web app):
//   BASE/bedroom/light/set     <- {"on":true,"level":60}
//   BASE/bedroom/light/state   -> {"on":true,"level":60}   (retained)
//   BASE/bedroom/sensor/state  -> {"temp":26.4,"hum":58}   (retained, every 5 s)
//
// Libraries: PubSubClient (Nick O'Leary), ArduinoJson 7, DHT sensor library (Adafruit)

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

const char* WIFI_SSID   = "your-wifi";
const char* WIFI_PASS   = "your-password";
const char* MQTT_HOST   = "broker.emqx.io";   // same broker as the web app, TCP port
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER   = "";
const char* MQTT_PASS   = "";
const char* BASE        = "house3d-xxxx";     // copy from the web app

const char* ROOM = "bedroom";
const int LIGHT_PIN = 25;   // PWM output to LED driver / MOSFET
const int DHT_PIN   = 4;

WiFiClient net;
PubSubClient mqtt(net);
DHT dht(DHT_PIN, DHT22);

bool lightOn = false;
int lightLevel = 40;        // 0-100 %
unsigned long lastSensor = 0;

String topic(const char* device, const char* kind) {
  return String(BASE) + "/" + ROOM + "/" + device + "/" + kind;
}

void applyLight() {
  int duty = lightOn ? map(lightLevel, 0, 100, 0, 255) : 0;
  analogWrite(LIGHT_PIN, duty);
}

void publishLight() {
  JsonDocument doc;
  doc["on"] = lightOn;
  doc["level"] = lightLevel;
  char buf[64];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(topic("light", "state").c_str(), (const uint8_t*)buf, n, true);
}

void publishSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return;   // skip a bad read instead of sending garbage
  JsonDocument doc;
  doc["temp"] = round(t * 10) / 10.0;
  doc["hum"] = (int)round(h);
  char buf[64];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(topic("sensor", "state").c_str(), (const uint8_t*)buf, n, true);
}

void onMessage(char* t, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  if (String(t) == topic("light", "set")) {
    if (doc["on"].is<bool>()) lightOn = doc["on"];
    if (doc["level"].is<int>()) lightLevel = constrain((int)doc["level"], 0, 100);
    applyLight();
    publishLight();   // confirm the real state back to the dashboard
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
}

void connectMqtt() {
  while (!mqtt.connected()) {
    String id = "esp32-" + String(ROOM) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS)) {
      mqtt.subscribe(topic("light", "set").c_str(), 1);
      publishLight();
    } else {
      delay(2000);
    }
  }
}

void setup() {
  pinMode(LIGHT_PIN, OUTPUT);
  applyLight();
  dht.begin();
  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
  if (millis() - lastSensor > 5000) {
    lastSensor = millis();
    publishSensor();
  }
}
