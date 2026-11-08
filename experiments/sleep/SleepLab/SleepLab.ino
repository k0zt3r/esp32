#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_timer.h>

// RTC memory survives timer wake from deep sleep; normal RAM does not.
RTC_DATA_ATTR unsigned long deepWakeCount = 0;
unsigned long ordinaryCounter = 0;
constexpr uint64_t LIGHT_US = 4000000ULL;
constexpr uint64_t DEEP_US = 6000000ULL;

void awakeWindow(unsigned long durationMs) {
  const unsigned long started = millis();
  while (millis() - started < durationMs) {
    Serial.printf("ACTIVE | uptime_ms=%lu | ordinary=%lu\n", millis(), ordinaryCounter);
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  // Do not start a radio: compare the same offline workload between modes.
  WiFi.mode(WIFI_OFF);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) ++deepWakeCount;
  else deepWakeCount = 0;
  Serial.printf("BOOT | wake=%s | rtc_deep_wakes=%lu | ordinary=%lu\n",
                cause == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "RESET", deepWakeCount, ordinaryCounter);
  Serial.println("PHASE ACTIVE | radios off | LED off | 5 seconds");
  awakeWindow(5000);

  ordinaryCounter = 42;
  esp_err_t result = esp_sleep_enable_timer_wakeup(LIGHT_US);
  if (result != ESP_OK) {
    Serial.printf("ERROR | light timer configuration=%d\n", result);
    return;
  }
  Serial.println("LIGHT ENTER | timer_us=4000000 | ordinary=42");
  Serial.flush();
  const int64_t beforeLight = esp_timer_get_time();
  result = esp_light_sleep_start();
  const int64_t elapsed = esp_timer_get_time() - beforeLight;
  Serial.printf("LIGHT EXIT | result=%d | wake=%s | elapsed_us=%lld | ordinary=%lu\n",
                result, esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "OTHER",
                static_cast<long long>(elapsed), ordinaryCounter);
  if (result != ESP_OK || ordinaryCounter != 42) {
    Serial.println("ERROR | light sleep verification failed");
    return;
  }
  awakeWindow(2000);

  result = esp_sleep_enable_timer_wakeup(DEEP_US);
  if (result != ESP_OK) {
    Serial.printf("ERROR | deep timer configuration=%d\n", result);
    return;
  }
  Serial.printf("DEEP ENTER | timer_us=6000000 | next_rtc_deep_wakes=%lu\n", deepWakeCount + 1);
  Serial.flush();
  // This never returns. Timer wake starts setup() again with ordinaryCounter=0.
  esp_deep_sleep_start();
}

void loop() {
  // Reaching loop means setup stopped after an error.
  delay(1000);
}
