---
name: esp32-lab
description: Build, flash and extend this ESP32 DevKit V1 electronics lab, including DS18B20 temperature sensing, I2C OLED with Neon animation, local dashboard, MQTT and documented breadboard experiments. Use for changes, hardware diagnostics and reproduction of these projects.
---

# ESP32 lab

Locate the repository root by `tools/board.py` and `ThermoDashboard/`. Read its README and the relevant subproject README before changing hardware or running firmware tools. Paths below are relative to that root, not to this skill folder. Use existing local `.esp32/config.json`; on a new machine copy `config.example.json` and determine the actual port.

## Working hardware

- ESP32 DevKit V1, classic ESP32-WROOM, 4 MiB; FQBN `esp32:esp32:esp32doit-devkit-v1`.
- DS18B20 DQ → GPIO4, VDD → 3V3, GND common, 4.7 kOhm pull-up DQ → 3V3. Use signal labels, not assumed package pin order.
- OLED: GND, VCC → 3V3, SDA → GPIO21, SCL → GPIO22. Observed address 0x3C; SSD1306 128×64 driver produced a user-confirmed image. Address alone is not a controller identification.
- GPIO2 drives the built-in LED. Keep external loads off GPIO without a suitable driver.
- Each numbered A–E breadboard group is connected internally. Put OLED pins in distinct numbered groups; never short the four pins into one group.

Wiring assets: `docs/wiring-ds18b20.svg`, `docs/wiring-oled.svg`, `docs/wiring-usb.svg`. Preserve the current sensor/display arrangement when adding independent experiments. Modify wiring with power removed.

## Firmware and assets

`python tools/board.py build|flash <serial|wifi|temperature|sleep|dashboard>` builds and uploads. Display the Arduino CLI command before executing it and inspect its result. Preserve the active sketch after temporary diagnostics unless the user chooses otherwise. Do not erase NVS as a routine fix: it stores Wi-Fi settings.

ThermoDashboard combines an asynchronous DS18B20 conversion, HTTP server, LED PWM, Wi-Fi scan, MQTT and OLED. Avoid long blocking delays in its loop. OLED Neon frames refresh about every 180 ms, sensor readings every 2 seconds. Missing sensors must produce an explicit invalid state rather than a plausible stale value.

- Edit `ThermoDashboard/ui.html`; `tools/pack_dashboard.py` generates the compressed header during dashboard builds.
- Neon sources: `ThermoDashboard/assets/neon/idle/`. `tools/pack_neon.py` generates `neon_frames.h` and local previews. Regeneration needs Pillow; ordinary builds use the checked-in header.
- For missing dependencies, consult the project README; install the required libraries within the user's requested setup scope.
- `arduino-cli monitor` needs an interactive terminal. In Windows agent execution use a PTY where supported, capture startup briefly, then close the monitor so COM is available for the next upload.
- A successful framebuffer allocation or send does not prove the physical image is correct. Ask for visual confirmation after changing display driver/layout.

## MQTT reproduction and diagnosis

Run `python mqtt/lab.py --lan-ip <current-computer-ip>` to start the local broker and generate device credentials/private firmware header. Local clients use 127.0.0.1:1883; ESP32 uses the LAN IP:1884 with authentication. Web lab: 127.0.0.1:8770. Configure board Wi-Fi at `/setup`, without putting passwords into chat or logs.

`lab/esp32/temperature`: real readings have demo=false, valid, temperature, sequence and sample age. ESP32 publishes QoS 0 with retain; a missed sample is replaced by a later one, not queued. The web panel rejects stale readings. `lab/esp32/status` uses an offline last will.

For `MQTT state=-2`, first inspect `/api/mqtt` to distinguish Wi-Fi association from TCP broker access. Check the bound IP, running listener and Windows firewall before changing router bands. App-specific Block rules can override an Allow rule: `mqtt/diagnose-firewall.ps1` inspects them; `mqtt/allow-device.ps1` backs up and disables only conflicting Mosquitto inbound blocks and adds a scoped port rule. These firewall actions require Windows administrator access; do not disable the whole firewall.

Use `mqtt/check_lan.py` to test authentication and delivery. Claims of recovery require an actual disconnect followed by new valid messages; firmware retry code alone is not a passed test.

## Experiments, results and Git

Each experiment needs a purpose, exact wiring diagram, setup instructions and measured results separated from calculations. The OLED does not measure current; datasheet values are not measured DevKit consumption. Confirm meter mode and jacks before current measurement. Current measurement breaks a circuit in series; never place a current-range meter across a supply.

Keep firmware, experiments and supporting files in this repository. Retain private configuration locally. Before publishing inspect `git ls-files` including staged files: ignore rules do not protect files already tracked. Exclude `.esp32`, Wi-Fi/MQTT credentials, private headers, binary builds, broker runtime, installers, browser profiles and logs. Documentation should state what was actually tested; do not close task-tracker work based only on generated code.
