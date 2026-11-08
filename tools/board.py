"""Build/flash the ESP32 sketches with the installed Arduino CLI."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SKETCHES = {"wifi": "WiFiLed", "serial": "SerialTest", "sleep": "experiments/sleep/SleepLab", "temperature": "DS18B20Test", "dashboard": "ThermoDashboard"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "flash", "monitor"])
    parser.add_argument("sketch", nargs="?", choices=SKETCHES)
    args = parser.parse_args()
    if args.action != "monitor" and not args.sketch:
        parser.error("choose wifi, serial, sleep, temperature or dashboard")
    cli = os.environ.get("ARDUINO_CLI") or shutil.which("arduino-cli")
    if not cli:
        cli = str(Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe")
    if not Path(cli).is_file():
        parser.error("Arduino CLI not found; set ARDUINO_CLI to its executable path")
    config_path = ROOT / ".esp32/config.json"
    if not config_path.is_file():
        parser.error("Copy config.example.json to .esp32/config.json and set your serial port first; see README.md")
    config = json.loads(config_path.read_text(encoding="utf-8-sig"))

    def run(*parts):
        command = [cli, *map(str, parts)]
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, check=True)

    if args.action == "monitor":
        run("monitor", "-p", config["port"], "-c", f"baudrate={config['monitorBaud']}", "--timestamp")
        return
    sketch = ROOT / SKETCHES[args.sketch]
    if args.sketch == "dashboard":
        subprocess.run([sys.executable, str(ROOT / "tools/pack_dashboard.py")], check=True)
    build = ROOT / ".build" / args.sketch
    run("compile", "--fqbn", config["fqbn"], "--build-path", build, sketch)
    if args.action == "flash":
        run("upload", "-p", config["port"], "--fqbn", config["fqbn"], "--input-dir", build, sketch)
        config["activeSketch"] = f"{SKETCHES[args.sketch]}/{sketch.name}.ino"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
