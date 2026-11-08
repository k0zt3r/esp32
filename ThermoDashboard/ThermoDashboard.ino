#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "dashboard_page.h"
#include "mqtt_private.h"
#include "mqtt_setup.h"
#include "neon_frames.h"

const char *AP_NAME = "ESP32-DevKit";
const char *AP_PASSWORD = "esp32test";
String i2cDiagnostics = "{}";

void scanDisplayBus() {
  Wire.begin(21, 22, 100000);
  Wire.setTimeOut(20);
  String addresses = "[";
  unsigned int found = 0, errors = 0;
  for (uint8_t address = 8; address < 120; ++address) {
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    if (result == 0) {
      if (found++) addresses += ',';
      addresses += String(address);
      Serial.printf("I2C FOUND | address=0x%02X | SDA=21 | SCL=22\n", address);
    } else if (result != 2) ++errors;
  }
  i2cDiagnostics = "{\"sda\":21,\"scl\":22,\"addresses\":" + addresses + "],\"errors\":" + String(errors) + "}";
  Serial.printf("I2C SCAN | devices=%u | bus_errors=%u\n", found, errors);
}
constexpr uint8_t LED_PIN = 2;
constexpr uint32_t PWM_MAX = 4095;
WebServer server(80);
String lightMode = "off";
uint8_t brightness = 70;
bool apReady = false;
unsigned long breathStart = 0, lastFrame = 0, lastHeartbeat = 0;
bool scanRunning = false;
String scanStatus = "idle";
String scanResults = "[]";
int scanTotal = 0;


constexpr uint8_t SENSOR_PIN = 4;
constexpr uint16_t HISTORY_CAPACITY = 300;
constexpr unsigned long SAMPLE_INTERVAL_MS = 2000;
struct TemperaturePoint { uint32_t uptime; float value; };
TemperaturePoint temperatureHistory[HISTORY_CAPACITY];
uint16_t historyHead = 0, historyCount = 0;
OneWire sensorWire(SENSOR_PIN);
DallasTemperature sensors(&sensorWire);
DeviceAddress sensorAddress;
bool sensorPresent = false, conversionPending = false, temperatureReady = false;
float currentTemperature = NAN, minimumTemperature = NAN, maximumTemperature = NAN;
double temperatureSum = 0;
unsigned long validSamples = 0, telemetrySequence = 0, lastConversionStart = 0, lastDiscovery = 0, lastValidSample = 0;

Preferences networkPreferences;
WiFiClient mqttSocket;
PubSubClient mqtt(mqttSocket);
String homeSsid, homePassword, mqttHost;
bool networkPending = false;
unsigned long lastWifiAttempt = 0, lastMqttAttempt = 0, publishedSequence = ULONG_MAX;
unsigned long mqttPublished = 0, mqttLastPublish = 0;
bool mqttWasConnected = false;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledReady = false;
unsigned long lastOledFrame = 0, oledFrames = 0;

void initOled() {
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED MISSING | no response at 0x3C");
    return;
  }
  // Wire was already configured explicitly for SDA21/SCL22 by the bus scan.
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  if (!oledReady) {
    Serial.println("OLED ERROR | framebuffer allocation failed");
    return;
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.ssd1306_command(SSD1306_SETCONTRAST);
  oled.ssd1306_command(0x7F);
  Serial.println("OLED INIT | SSD1306 candidate | 128x64 | 0x3C | image needs visual confirmation");
}

void updateOled() {
  const unsigned long now = millis();
  if (!oledReady || (oledFrames && now - lastOledFrame < 180)) return;
  lastOledFrame = now;
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
    oledReady = false;
    Serial.println("OLED LOST | display disconnected; thermometer continues");
    return;
  }
  const bool fresh = temperatureReady && now - lastValidSample < 6000;
  oled.clearDisplay();
  const uint8_t neonFrame = (now / 180) % NEON_FRAME_COUNT;
  oled.drawBitmap(0, 0, NEON_FRAMES[neonFrame], 64, 64, SSD1306_WHITE);
  oled.drawFastVLine(64, 0, 64, SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(76, 0);
  oled.print("NEON");
  oled.setCursor(72, 12);
  oled.print(WiFi.status() == WL_CONNECTED ? "W+" : "W-");
  oled.setCursor(108, 12);
  oled.print(mqtt.connected() ? "M+" : "M-");
  if (fresh) {
    const String reading = String(currentTemperature, 1);
    const int16_t left = 67 + (60 - reading.length() * 12) / 2;
    oled.setTextSize(2);
    oled.setCursor(left, 26);
    oled.print(reading);
    oled.setTextSize(1);
    oled.setCursor(90, 44);
    oled.print("deg C");
    const uint16_t shown = std::min<uint16_t>(historyCount, 60);
    const uint16_t first = (historyHead + HISTORY_CAPACITY - shown) % HISTORY_CAPACITY;
    float low = currentTemperature - 0.125f, high = currentTemperature + 0.125f;
    for (uint16_t i = 0; i < shown; ++i) {
      const float value = temperatureHistory[(first + i) % HISTORY_CAPACITY].value;
      low = fminf(low, value); high = fmaxf(high, value);
    }
    int16_t previousY = 0;
    for (uint16_t i = 0; i < shown; ++i) {
      const TemperaturePoint &point = temperatureHistory[(first + i) % HISTORY_CAPACITY];
      const int16_t y = 63 - static_cast<int16_t>((point.value - low) / (high - low) * 10);
      if (i && point.uptime - temperatureHistory[(first + i - 1) % HISTORY_CAPACITY].uptime <= 6000)
        oled.drawLine(67 + i - 1, previousY, 67 + i, y, SSD1306_WHITE);
      else oled.drawPixel(67 + i, y, SSD1306_WHITE);
      previousY = y;
    }
  } else {
    oled.setTextSize(1);
    oled.setCursor(70, 28);
    oled.print(validSamples ? "NO SENSOR" : "WAIT...");
    oled.setCursor(76, 44);
    oled.print("GPIO4");
  }
  oled.display();
  if (++oledFrames == 1) Serial.println("OLED FRAME | NEON animated | 6 frames | 64x64 | check physical screen");
}

void loadNetwork() {
  networkPreferences.begin("thermo-net", false);
  homeSsid = networkPreferences.getString("ssid", "");
  homePassword = networkPreferences.getString("wifi-pass", "");
  mqttHost = networkPreferences.getString("host", MQTT_DEFAULT_HOST);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(1);
  mqttSocket.setConnectionTimeout(500);
  networkPending = !homeSsid.isEmpty();
}

void networkStatus() {
  String json = "{\"configured\":" + String(homeSsid.isEmpty() ? "false" : "true");
  json += ",\"ssid\":" + jsonString(homeSsid) + ",\"host\":" + jsonString(mqttHost);
  json += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"ip\":" + jsonString(WiFi.localIP().toString());
  json += ",\"connected\":" + String(mqtt.connected() ? "true" : "false");
  json += ",\"published\":" + String(mqttPublished) + ",\"mqtt_state\":" + String(mqtt.state()) + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

void saveNetwork() {
  const String ssid = server.arg("ssid"), password = server.arg("password"), host = server.arg("host");
  IPAddress brokerAddress;
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64 || !brokerAddress.fromString(host) || brokerAddress[0] == 0) {
    server.send(400, "text/plain; charset=utf-8", "Check SSID, password length and broker IPv4 address");
    return;
  }
  // Blank password preserves the existing password only for the same SSID.
  if (!password.isEmpty() || homeSsid != ssid) homePassword = password;
  homeSsid = ssid;
  mqttHost = host;
  networkPreferences.putString("ssid", homeSsid);
  networkPreferences.putString("wifi-pass", homePassword);
  networkPreferences.putString("host", mqttHost);
  server.send(200, "application/json", "{\"saved\":true}");
  // Defer network changes until after the HTTP response is sent.
  networkPending = true;
}

void updateMqtt() {
  const unsigned long now = millis();
  if (networkPending) {
    mqtt.disconnect();
    mqttSocket.stop();
    WiFi.disconnect(false, false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(homeSsid.c_str(), homePassword.c_str());
    mqtt.setServer(mqttHost.c_str(), 1884);
    lastWifiAttempt = now;
    lastMqttAttempt = 0;
    networkPending = false;
    mqttWasConnected = false;
    Serial.println("HOME WIFI | connecting (credentials hidden)");
  }
  if (homeSsid.isEmpty()) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected()) mqttSocket.stop();
    mqttWasConnected = false;
    if (now - lastWifiAttempt >= 20000) {
      lastWifiAttempt = now;
      WiFi.reconnect();
      Serial.println("HOME WIFI | retrying");
    }
    return;
  }
  if (!mqtt.connected()) {
    mqttWasConnected = false;
    if (lastMqttAttempt && now - lastMqttAttempt < 5000) return;
    lastMqttAttempt = now;
    String clientId = "thermo-" + WiFi.macAddress();
    // A retained last will makes an unexpected disconnect visible to subscribers.
    if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD,
                      "lab/esp32/status", 1, true, "offline")) {
      Serial.printf("MQTT WAIT | state=%d\n", mqtt.state());
      return;
    }
    Serial.printf("MQTT READY | broker=%s:1884 | board_ip=%s\n", mqttHost.c_str(), WiFi.localIP().toString().c_str());
  }
  mqtt.loop();
  if (!mqtt.connected()) return;
  if (!mqttWasConnected) {
    mqtt.publish("lab/esp32/status", "online", true);
    publishedSequence = ULONG_MAX;
    mqttWasConnected = true;
  }
  if (publishedSequence == telemetrySequence || (mqttLastPublish && now - mqttLastPublish < 1000)) return;
  const bool fresh = temperatureReady && now - lastValidSample < 6000;
  String payload = "{\"demo\":false,\"valid\":" + String(fresh ? "true" : "false");
  payload += ",\"temperature\":" + (fresh ? String(currentTemperature, 4) : String("null"));
  payload += ",\"unit\":\"C\",\"sequence\":" + String(telemetrySequence);
  payload += ",\"uptime_ms\":" + String(now) + ",\"sample_age_ms\":";
  payload += validSamples ? String(now - lastValidSample) : String("null");
  payload += "}";
  // PubSubClient publishes QoS 0: a new measurement supersedes a missed one.
  // Retain provides the latest reading, not a backlog of measurements.
  if (mqtt.publish("lab/esp32/temperature", payload.c_str(), true)) {
    publishedSequence = telemetrySequence;
    mqttLastPublish = now;
    ++mqttPublished;
    Serial.printf("MQTT SENT | sequence=%lu | valid=%s | total=%lu\n", telemetrySequence, fresh ? "YES" : "NO", mqttPublished);
  }
}

void discoverSensor() {
  lastDiscovery = millis();
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensorPresent = false;
  for (uint8_t i = 0; i < sensors.getDeviceCount(); ++i) {
    if (sensors.getAddress(sensorAddress, i) && sensorAddress[0] == 0x28 &&
        OneWire::crc8(sensorAddress, 7) == sensorAddress[7]) {
      sensorPresent = true;
      sensors.setResolution(sensorAddress, 12);
      Serial.println("SENSOR READY | DS18B20 | GPIO4 | 12 bit");
      break;
    }
  }
  if (!sensorPresent) Serial.println("SENSOR WAITING | check GPIO4, power and pull-up");
}

void updateTemperature() {
  const unsigned long now = millis();
  if (!sensorPresent) {
    if (now - lastDiscovery >= 5000) discoverSensor();
    return;
  }
  if (conversionPending) {
    // Conversion happens inside the sensor; keep HTTP, PWM and scanning responsive.
    if (now - lastConversionStart < 800) return;
    conversionPending = false;
    const float value = sensors.getTempC(sensorAddress);
    ++telemetrySequence;
    if (!isfinite(value) || value == DEVICE_DISCONNECTED_C || value < -55 || value > 125) {
      temperatureReady = false;
      sensorPresent = false;
      lastDiscovery = now;
      Serial.println("TEMP ERROR | no valid sensor reading; retrying in 5 seconds");
      return;
    }
    currentTemperature = value;
    temperatureReady = true;
    lastValidSample = now;
    minimumTemperature = validSamples ? fminf(minimumTemperature, value) : value;
    maximumTemperature = validSamples ? fmaxf(maximumTemperature, value) : value;
    temperatureSum += value;
    ++validSamples;
    temperatureHistory[historyHead] = {now, value};
    historyHead = (historyHead + 1) % HISTORY_CAPACITY;
    if (historyCount < HISTORY_CAPACITY) ++historyCount;
    Serial.printf("TEMP OK | sample=%lu | temperature_C=%.4f | history=%u | free_heap=%lu\n",
                  validSamples, value, historyCount, static_cast<unsigned long>(ESP.getFreeHeap()));
  }
  if (!lastConversionStart || now - lastConversionStart >= SAMPLE_INTERVAL_MS) {
    sensors.requestTemperatures();
    lastConversionStart = millis();
    conversionPending = true;
  }
}

void sendTelemetry() {
  String data;
  data.reserve(9000);
  data = "{\"ready\":";
  data += temperatureReady ? "true" : "false";
  data += ",\"temperature\":";
  data += temperatureReady ? String(currentTemperature, 4) : String("null");
  data += ",\"minimum\":" + (validSamples ? String(minimumTemperature, 4) : String("null"));
  data += ",\"maximum\":" + (validSamples ? String(maximumTemperature, 4) : String("null"));
  data += ",\"average\":" + (validSamples ? String(temperatureSum / validSamples, 4) : String("null"));
  data += ",\"samples\":" + String(validSamples) + ",\"sequence\":" + String(telemetrySequence);
  data += ",\"uptime_ms\":" + String(millis()) + ",\"sample_interval_ms\":2000";
  data += ",\"last_sample_age_ms\":" + (validSamples ? String(millis() - lastValidSample) : String("null"));
  data += ",\"sensor_status\":\"";
  data += temperatureReady ? "ok" : (!validSamples && sensorPresent ? "starting" : "unavailable");
  data += "\",\"light\":{\"mode\":\"" + lightMode + "\",\"brightness\":" + String(brightness) + "}";
  data += ",\"history\":[";
  const uint16_t oldest = (historyHead + HISTORY_CAPACITY - historyCount) % HISTORY_CAPACITY;
  for (uint16_t i = 0; i < historyCount; ++i) {
    const TemperaturePoint &point = temperatureHistory[(oldest + i) % HISTORY_CAPACITY];
    if (i) data += ',';
    data += "[" + String(point.uptime) + "," + String(point.value, 4) + "]";
  }
  data += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", data);
}

String jsonString(const String &value) {
  String result = "\"";
  for (unsigned int i = 0; i < value.length(); ++i) {
    const unsigned char c = value[i];
    if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else result += static_cast<char>(c);
  }
  return result + "\"";
}

void finishScan(int count) {
  if (count < 0) {
    scanStatus = "error";
    Serial.println("SCAN ERROR");
  } else {
    std::vector<int> order;
    for (int i = 0; i < count; ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
    // Bound the cached response; release the Wi-Fi driver's scan records below.
    const int shown = std::min(count, 40);
    String results;
    results.reserve(shown * 180 + 2);
    results = "[";
    for (int pos = 0; pos < shown; ++pos) {
      const int i = order[pos];
      if (pos) results += ',';
      results += "{\"ssid\":" + jsonString(WiFi.SSID(i)) +
                 ",\"rssi\":" + String(WiFi.RSSI(i)) +
                 ",\"channel\":" + String(WiFi.channel(i)) +
                 ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? String("false") : String("true")) + "}";
    }
    scanResults = results + "]";
    scanTotal = count;
    scanStatus = "ready";
    Serial.printf("SCAN READY | found=%d | shown=%d\n", count, shown);
  }
  WiFi.scanDelete();
  scanRunning = false;
}

void startScan() {
  if (scanRunning) return;
  scanStatus = "scanning";
  scanRunning = true;
  // Async scanning lets loop() keep serving HTTP and updating the LED.
  const int result = WiFi.scanNetworks(true, true, false, 120);
  Serial.println("SCAN STARTED");
  if (result != WIFI_SCAN_RUNNING) finishScan(result);
}

void sendScan() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(scanRunning ? 202 : 200, "application/json; charset=utf-8",
              "{\"status\":" + jsonString(scanStatus) + ",\"total\":" +
              String(scanTotal) + ",\"networks\":" + scanResults + "}");
}

void updateLight(unsigned long now) {
  float level = lightMode == "off" ? 0.0f : brightness / 100.0f;
  if (lightMode == "breathe") {
    const float phase = ((now - breathStart) % 4000UL) / 4000.0f;
    level *= 0.5f - 0.5f * cosf(phase * TWO_PI);
  }
  // Gamma correction gives a smoother perceived brightness range.
  // Hardware PWM continues independently of HTTP requests.
  ledcWrite(LED_PIN, static_cast<uint32_t>(powf(level, 2.2f) * PWM_MAX + 0.5f));
}

void sendState() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"mode\":\"" + lightMode + "\",\"brightness\":" + String(brightness) + "}");
}

void controlLight() {
  const String nextMode = server.arg("mode"), raw = server.arg("brightness");
  bool valid = raw.length() > 0 && raw.length() <= 3;
  for (unsigned int i = 0; i < raw.length(); ++i) {
    if (raw[i] < '0' || raw[i] > '9') valid = false;
  }
  const int nextBrightness = raw.toInt();
  if (!valid || nextBrightness > 100 || (nextMode != "off" && nextMode != "solid" && nextMode != "breathe")) {
    server.send(400, "text/plain", "Invalid mode or brightness (0..100)");
    return;
  }
  if (nextMode == "breathe" && lightMode != "breathe") breathStart = millis();
  lightMode = nextMode;
  brightness = nextBrightness;
  updateLight(millis());
  Serial.printf("LIGHT | mode=%s | brightness=%u%%\n", lightMode.c_str(), brightness);
  sendState();
}

void setup() {
  Serial.begin(115200);
  scanDisplayBus();
  initOled();
  // Arduino-ESP32 3.x attaches PWM directly to the GPIO pin.
  if (!ledcAttach(LED_PIN, 5000, 12)) {
    Serial.println("ERROR: PWM initialization failed");
    return;
  }
  ledcWrite(LED_PIN, 0);
  Serial.println("PWM READY | GPIO2 | 5000 Hz | 12 bit");
  // STA is needed for scanning; AP keeps our control network available.
  discoverSensor();
  WiFi.mode(WIFI_AP_STA);
  loadNetwork();
  const IPAddress ip(192, 168, 4, 1);
  apReady = WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)) && WiFi.softAP(AP_NAME, AP_PASSWORD, 1, false, 4);
  if (!apReady) { Serial.println("ERROR: Wi-Fi access point failed to start"); return; }
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(DASHBOARD_GZIP), DASHBOARD_GZIP_LEN);
  });
  server.on("/api/telemetry", HTTP_GET, sendTelemetry);
  server.on("/api/i2c", HTTP_GET, []() { server.send(200, "application/json", i2cDiagnostics); });
  server.on("/setup", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", MQTT_SETUP_PAGE); });
  server.on("/api/mqtt", HTTP_GET, networkStatus);
  server.on("/api/mqtt", HTTP_POST, saveNetwork);
  server.on("/control", HTTP_POST, controlLight);
  server.on("/state", HTTP_GET, sendState);
  server.on("/scan", HTTP_GET, sendScan);
  server.on("/scan", HTTP_POST, []() { startScan(); sendScan(); });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.printf("AP READY | SSID=%s | IP=%s\n", AP_NAME, WiFi.softAPIP().toString().c_str());
  Serial.println("WEB READY | http://192.168.4.1 | THERMO DASHBOARD + light + scanner");
}

void loop() {
  if (apReady) {
    updateTemperature();
    server.handleClient();
    updateMqtt();
    updateOled();
    if (scanRunning) {
      const int result = WiFi.scanComplete();
      if (result != WIFI_SCAN_RUNNING) finishScan(result);
    }
    const unsigned long now = millis();
    // Animate without blocking the web server between frames.
    if (now - lastFrame >= 10) { lastFrame = now; updateLight(now); }
    if (now - lastHeartbeat >= 5000) {
      lastHeartbeat = now;
      Serial.printf("AP OK | clients=%u | mode=%s | brightness=%u%% | free_heap=%lu\n", WiFi.softAPgetStationNum(), lightMode.c_str(), brightness, static_cast<unsigned long>(ESP.getFreeHeap()));
    }
  }
  delay(1);
}
