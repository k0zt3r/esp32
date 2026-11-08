#include <Arduino.h>

unsigned long lastHeartbeat = 0;
unsigned long heartbeatCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 serial test started");
  Serial.printf("Chip: %s | Cores: %d | Flash: %lu bytes\n",
                ESP.getChipModel(), ESP.getChipCores(),
                static_cast<unsigned long>(ESP.getFlashChipSize()));
}

void loop() {
  // millis() keeps the loop available for other work between messages.
  const unsigned long now = millis();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    Serial.printf("ESP32 OK | count=%lu | uptime_ms=%lu | free_heap=%lu\n",
                  ++heartbeatCount, now,
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
  // Yield to the ESP32 scheduler while waiting for the next heartbeat.
  delay(1);
}
