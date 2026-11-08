"""Verify device listener authentication and topic access without exposing credentials."""
import json
from pathlib import Path
import subprocess
import urllib.request
import time

root = Path(__file__).resolve().parent
credentials = json.loads((root / "device-credentials.json").read_text())
with urllib.request.urlopen("http://127.0.0.1:8770/api/connection", timeout=5) as response:
    connection = json.load(response)
command = [str(root / "runtime/mosquitto_pub.exe"), "-h", connection["host"], "-p", "1884",
           "-t", "lab/esp32/connection-test", "-m", "DEMO: authenticated connectivity check", "-q", "1"]
anonymous = subprocess.run(command, capture_output=True, timeout=12, creationflags=0x08000000)
if anonymous.returncode == 0:
    raise SystemExit("FAIL: anonymous access was accepted")
authenticated = subprocess.run(command + ["-u", credentials["username"], "-P", credentials["password"]],
                               capture_output=True, timeout=12, creationflags=0x08000000)
if authenticated.returncode:
    raise SystemExit("FAIL: authenticated device publication failed: " + authenticated.stderr.decode(errors="replace"))
for _ in range(30):
    with urllib.request.urlopen("http://127.0.0.1:8770/api/messages", timeout=5) as response:
        rows = json.load(response)["messages"]
    if any(row["topic"] == "lab/esp32/connection-test" for row in rows):
        print("PASS: device listener rejects anonymous access; authenticated publication reaches lab subscriber")
        break
    time.sleep(.1)
else:
    raise SystemExit("FAIL: subscriber did not receive the authenticated message")
