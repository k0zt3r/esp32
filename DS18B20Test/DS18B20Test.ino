#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// User-confirmed wiring: DQ -> D4, with 4.7 kOhm from DQ to 3.3 V.
constexpr uint8_t DATA_PIN = 4;
OneWire oneWire(DATA_PIN);
DallasTemperature sensors(&oneWire);
unsigned long sampleNumber = 0;

void printAddress(const uint8_t *address) {
  for (uint8_t i = 0; i < 8; ++i) Serial.printf("%02X", address[i]);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  sensors.begin();
  // Wait for conversion before reading: do not mistake a power-up value
  // for a completed temperature measurement.
  sensors.setWaitForConversion(true);
  Serial.println("DS18B20 TEST | GPIO4 | external 3.3 V supply expected");
}

void loop() {
  sensors.begin();
  const uint8_t count = sensors.getDeviceCount();
  Serial.printf("BUS | GPIO4 | devices=%u\n", count);
  if (count == 0) {
    Serial.println("NO SENSOR | check DQ, 3.3 V, GND, pull-up and breadboard rows");
    delay(3000);
    return;
  }

  sensors.requestTemperatures();
  for (uint8_t i = 0; i < count; ++i) {
    DeviceAddress address;
    if (!sensors.getAddress(address, i)) continue;
    Serial.print("DEVICE | ROM=");
    printAddress(address);
    if (OneWire::crc8(address, 7) != address[7]) {
      Serial.println(" | ERROR: ROM CRC");
      continue;
    }
    if (address[0] != 0x28) {
      Serial.println(" | not a DS18B20 family device");
      continue;
    }
    // DallasTemperature validates scratchpad data; -127 means a read failure.
    const float temperature = sensors.getTempC(address);
    if (temperature == DEVICE_DISCONNECTED_C) {
      Serial.println(" | ERROR: temperature read failed");
      continue;
    }
    Serial.printf(" | ROM_CRC=OK | sample=%lu | temperature_C=%.4f | resolution_bits=%u\n",
                  ++sampleNumber, temperature, sensors.getResolution(address));
  }
  delay(1500);
}
