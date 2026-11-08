"""Headless desktop/mobile QA of the real HTML with explicitly marked test data."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import subprocess
import threading
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".preview/dashboard"
OUTPUT.mkdir(parents=True, exist_ok=True)
BROWSER = Path("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")
if not BROWSER.is_file():
    raise SystemExit("Browser QA unavailable: a headless Edge executable was not found")

history = [[2000 * (i + 1), round(23.65 + .35 * math.sin(i / 38) + .001 * i, 4)] for i in range(300)]
history[-1][1] = 24.125
telemetry = {"demo": True, "ready": True, "temperature": 24.125, "minimum": min(row[1] for row in history),
             "maximum": max(row[1] for row in history), "average": sum(row[1] for row in history) / len(history),
             "samples": 300, "sequence": 300, "uptime_ms": 600000, "sample_interval_ms": 2000,
             "sensor_status": "ok", "history": history, "light": {"mode": "off", "brightness": 70}}
networks = [{"ssid": "Home_Network", "rssi": -42, "channel": 6, "secure": True},
            {"ssid": "Atelier_2G", "rssi": -67, "channel": 11, "secure": True}]
state = {"scan": "idle", "light": telemetry["light"]}
qa = r"""
<script>
(async()=>{
 const wait=async(test)=>{for(let i=0;i<60;i++){if(test())return;await new Promise(r=>setTimeout(r,80))}throw Error('wait timeout')};
 const assert=(test,message)=>{if(!test)throw Error(message)};
 try{
 await wait(()=>document.getElementById('whole').textContent==='24');
 assert(document.getElementById('fraction').textContent===',13','temperature formatting');
 assert(document.getElementById('plotLine').getAttribute('d').startsWith('M'),'chart renders');
 assert(!document.getElementById('plotLine').getAttribute('d').includes('NaN'),'finite chart points');
 document.querySelector('[data-period="60"]').click();
 assert(document.querySelector('[data-period="60"]').getAttribute('aria-pressed')==='true','chart period');
 document.querySelector('[data-period="300"]').click();
 renderNetworks([{ssid:'<img src=x onerror=alert(1)>',rssi:-50,channel:1,secure:false}]);
 assert(!document.getElementById('networks').querySelector('img'),'SSID must be text');
 const slider=document.getElementById('brightness');slider.value=35;slider.dispatchEvent(new Event('input'));slider.dispatchEvent(new Event('change'));
 await wait(()=>document.getElementById('lightStatus').textContent.includes('35%'));
 document.querySelector('[data-mode="breathe"]').click();
 await wait(()=>document.getElementById('lightStatus').textContent.includes('Дыхание'));
 document.getElementById('scanButton').click();
 await wait(()=>document.getElementById('networks').textContent.includes('Home_Network'));
 const healthyTelemetry=telemetry;
 render({...healthyTelemetry,ready:false,temperature:null,sensor_status:'unavailable'});
 assert(document.getElementById('whole').textContent==='—','disconnect hides current value');
 render(healthyTelemetry);
 assert(document.documentElement.scrollWidth<=window.innerWidth+1,'no horizontal overflow');
 document.body.dataset.qa='passed';
 document.body.dataset.viewport=window.innerWidth+'x'+window.innerHeight;
 }catch(error){document.body.dataset.qa='failed';document.body.dataset.qaError=error.message;}
})();
</script>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, data, content_type="application/json"):
        body = data.encode("utf-8") if isinstance(data, str) else json.dumps(data).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", content_type + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/api/telemetry"):
            self.reply({**telemetry, "light": state["light"]})
        elif self.path.startswith("/scan"):
            self.reply({"status": state["scan"], "total": len(networks), "networks": networks if state["scan"] == "ready" else []})
        elif self.path.startswith("/favicon"):
            self.send_response(204)
            self.end_headers()
        else:
            html = (ROOT / "ThermoDashboard/ui.html").read_text(encoding="utf-8")
            if state.get("viewport") == "mobile":
                html = html.replace("</head>", "<style>html{width:412px}body{width:100%}</style></head>")
            self.reply(html.replace("</body>", qa + "</body>"), "text/html")

    def do_POST(self):
        payload = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        if self.path == "/control":
            fields = parse_qs(payload)
            state["light"] = {"mode": fields["mode"][0], "brightness": int(fields["brightness"][0])}
            self.reply(state["light"])
        elif self.path == "/scan":
            state["scan"] = "ready"
            self.reply({"status": "ready", "total": len(networks), "networks": networks})
        else:
            self.send_response(404)
            self.end_headers()


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    for name, width, height in [("desktop", 1440, 1200), ("mobile", 412, 1450)]:
        state["viewport"] = name
        state["scan"] = "idle"
        state["light"] = {"mode": "off", "brightness": 70}
        command = [str(BROWSER), "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
                   "--disable-extensions", "--disable-background-networking", "--hide-scrollbars",
                   f"--user-data-dir={OUTPUT / (name + '-profile')}", f"--window-size={width},{height}",
                   "--virtual-time-budget=9000", "--dump-dom", f"--screenshot={OUTPUT / (name + '.png')}",
                   f"http://127.0.0.1:{server.server_address[1]}/"]
        result = subprocess.run(command, capture_output=True, timeout=45, creationflags=0x08000000)
        html = result.stdout.decode("utf-8", errors="replace")
        (OUTPUT / (name + ".html")).write_text(html, encoding="utf-8")
        if result.returncode or 'data-qa="passed"' not in html:
            error = html[html.find("data-qa="):html.find("data-qa=") + 250]
            raise RuntimeError(f"{name} browser QA failed: {error or result.stderr.decode(errors='replace')[:500]}")
        print(f"{name}: live rendering, chart range, LED controls, scanner, disconnect state, SSID escaping and overflow checks passed", flush=True)
finally:
    server.shutdown()
    server.server_close()
