"""Local MQTT learning lab. Uses actual Mosquitto pub/sub, no Python dependencies."""
import argparse
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import secrets
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parent
RUNTIME = ROOT / "runtime"
HIDDEN = 0x08000000
messages = deque(maxlen=150)
lock = threading.Lock()
counter = 0
lan_address = None


def executable(name):
    return str(RUNTIME / (name + ".exe"))


def publish(topic, payload, retain=False):
    command = [executable("mosquitto_pub"), "-h", "127.0.0.1", "-p", "1883", "-t", topic,
               "-m", payload, "-q", "1"]
    if retain:
        command.append("-r")
    subprocess.run(command, check=True, timeout=25, capture_output=True, creationflags=HIDDEN)


def collect(subscriber):
    global counter
    for line in subscriber.stdout:
        topic, separator, payload = line.rstrip("\r\n").partition(" ")
        if separator:
            with lock:
                counter += 1
                messages.append({"id": counter, "time": datetime.now().strftime("%H:%M:%S"), "received_at": time.time(),
                                 "topic": topic, "payload": payload})


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, value, status=200):
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/connection":
            return self.reply({"host": lan_address, "port": 1884, "topic": "lab/esp32/temperature"})
        if self.path == "/api/messages":
            with lock:
                result = list(messages)
            return self.reply({"messages": result, "subscription": "lab/#"})
        if self.path != "/":
            return self.reply({"error": "Not found"}, 404)
        body = (ROOT / "index.html").read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.headers.get("Origin") not in (None, "http://127.0.0.1:8770", "http://localhost:8770"):
            return self.reply({"error": "Origin rejected"}, 403)
        if self.path != "/api/publish":
            return self.reply({"error": "Not found"}, 404)
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                return self.reply({"error": "Message too large"}, 400)
            data = json.loads(self.rfile.read(length))
            topic, payload = data.get("topic"), data.get("payload")
            if not isinstance(topic, str) or not topic.startswith("lab/") or any(c in topic for c in "#+\x00\r\n"):
                return self.reply({"error": "Topic must start with lab/ and contain no wildcards"}, 400)
            if not isinstance(payload, str) or len(payload.encode("utf-8")) > 2048 or any(c in payload for c in "\x00\r\n"):
                return self.reply({"error": "Use a single-line message up to 2048 bytes"}, 400)
            publish(topic, payload, data.get("retain") is True)
            self.reply({"accepted": True})
        except (ValueError, subprocess.SubprocessError, OSError):
            self.reply({"error": "Publish failed; check broker log"}, 502)


def main():
    global lan_address
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check pub/sub, retain and wildcard delivery; then exit")
    parser.add_argument("--lan-ip", help="Enable authenticated ESP32 listener on this local IPv4 address")
    args = parser.parse_args()
    for port in (1883, 8770):
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", port)) == 0:
                raise SystemExit(f"Port {port} is already in use; existing process was left untouched")
    config = ROOT / "mosquitto.conf"
    config_text = "per_listener_settings true\nlistener 1883 127.0.0.1\nallow_anonymous true\npersistence false\nlog_type all\n"
    if args.lan_ip:
        socket.inet_aton(args.lan_ip)
        lan_address = args.lan_ip
        credentials_path = ROOT / "device-credentials.json"
        credentials = json.loads(credentials_path.read_text()) if credentials_path.exists() else {"username": "esp32", "password": secrets.token_urlsafe(24)}
        credentials_path.write_text(json.dumps(credentials), encoding="ascii")
        subprocess.run([executable("mosquitto_passwd"), "-b", "-c", "device-passwords", credentials["username"], credentials["password"]],
                       cwd=ROOT, check=True, capture_output=True, creationflags=HIDDEN)
        (ROOT / "device-acl").write_text("user esp32\ntopic write lab/esp32/#\n", encoding="ascii")
        config_text += f"listener 1884 {lan_address}\nallow_anonymous false\npassword_file device-passwords\nacl_file device-acl\n"
        # The Wi-Fi password is entered on the board, never in this generated file.
        header = "#pragma once\n"
        for name, value in [("MQTT_DEFAULT_HOST", lan_address), ("MQTT_USER", credentials["username"]), ("MQTT_PASSWORD", credentials["password"])]:
            header += f"const char *{name} = {json.dumps(value)};\n"
        (ROOT.parent / "ThermoDashboard/mqtt_private.h").write_text(header, encoding="ascii")
    config.write_text(config_text, encoding="ascii")
    with (ROOT / "broker.log").open("a", encoding="utf-8") as log:
        broker = subprocess.Popen([executable("mosquitto"), "-c", "mosquitto.conf", "-v"], cwd=ROOT,
                                  stdout=log, stderr=log, creationflags=HIDDEN)
        subscriber = None
        try:
            for _ in range(50):
                if broker.poll() is not None:
                    raise RuntimeError("Broker exited; see broker.log")
                with socket.socket() as probe:
                    if probe.connect_ex(("127.0.0.1", 1883)) == 0:
                        break
                time.sleep(.1)
            else:
                raise RuntimeError("Broker startup timed out")
            subscriber = subprocess.Popen([executable("mosquitto_sub"), "-h", "127.0.0.1", "-t", "lab/#", "-q", "1", "-v"],
                                          stdout=subprocess.PIPE, stderr=log, text=True, encoding="utf-8", creationflags=HIDDEN)
            threading.Thread(target=collect, args=(subscriber,), daemon=True).start()
            # A retained greeting is delivered even if the subscriber starts a moment later.
            publish("lab/demo/status", "DEMO: broker is ready", retain=True)
            if args.check:
                publish("lab/demo/temperature", "DEMO: 23.6 C (not a sensor reading)", retain=True)
                late = subprocess.run([executable("mosquitto_sub"), "-h", "127.0.0.1", "-t", "lab/demo/temperature", "-C", "1", "-W", "5", "-v"],
                                      capture_output=True, text=True, timeout=8, creationflags=HIDDEN)
                if late.returncode or "DEMO: 23.6" not in late.stdout:
                    raise RuntimeError("Retained delivery check failed")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    with lock:
                        passed = any(m["topic"] == "lab/demo/temperature" for m in messages)
                    if passed:
                        break
                    time.sleep(.05)
                if not passed:
                    raise RuntimeError("Wildcard subscriber delivery check failed")
                print("PASS: actual MQTT publish/subscribe, lab/# wildcard, QoS 1 publish, retained delivery to a late subscriber", flush=True)
                return
            server = ThreadingHTTPServer(("127.0.0.1", 8770), Handler)
            print("MQTT broker: 127.0.0.1:1883 | lab: http://127.0.0.1:8770 | Ctrl+C to stop", flush=True)
            try:
                server.serve_forever()
            finally:
                server.server_close()
        finally:
            if subscriber:
                subscriber.terminate()
                subscriber.wait(timeout=5)
            broker.terminate()
            broker.wait(timeout=5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
