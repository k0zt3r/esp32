#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <algorithm>
#include <vector>

const char *AP_NAME = "ESP32-DevKit";
const char *AP_PASSWORD = "esp32test";
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

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 · Свет</title><style>
*{box-sizing:border-box}body{margin:0;min-height:100svh;display:grid;place-items:center;background:#0e1726;color:#eef4ff;font:18px system-ui,sans-serif;padding:24px}
main{width:100%;max-width:440px;padding:30px;border:1px solid #33435b;border-radius:24px;background:#18243a}small,footer{color:#a5b5cc}h1{font-size:32px;margin:14px 0 24px}
.lamp{width:64px;height:64px;margin:12px 0 24px;border-radius:50%;background:#6ee7b7;box-shadow:0 0 35px #6ee7b7;opacity:.08}.lamp.solid{opacity:var(--level)}.lamp.breathe{animation:breath 4s ease-in-out infinite}@keyframes breath{0%,100%{opacity:.04}50%{opacity:var(--level)}}
p{line-height:1.5}label{display:flex;justify-content:space-between;margin:24px 0 12px}input{width:100%;height:36px;accent-color:#6ee7b7;cursor:pointer}button{width:100%;padding:16px;margin-top:12px;border:1px solid #435670;border-radius:12px;font:700 18px system-ui;cursor:pointer;background:#263650;color:#eef4ff}button[aria-pressed=true]{background:#6ee7b7;color:#10251e;border-color:#6ee7b7}footer{margin-top:24px;font-size:14px;line-height:1.5}#status{min-height:27px}
section{border-top:1px solid #33435b;margin-top:30px;padding-top:12px}h2{font-size:25px;margin-bottom:8px}.hint,#scanStatus{font-size:14px;color:#a5b5cc}.network{padding:16px 0;border-bottom:1px solid #33435b}.network strong{display:block;overflow-wrap:anywhere}.network small{display:block;margin:7px 0}.signal{height:5px;border-radius:5px;background:#33435b;overflow:hidden}.signal span{display:block;height:100%;background:#6ee7b7}button:disabled{opacity:.55;cursor:wait}
</style></head><body><main><small>ESP32 DEVKIT · WI-FI</small><h1>Свет под контролем.</h1><div id="lamp" class="lamp" aria-hidden="true"></div><p id="status" role="status" aria-live="polite">Подключаемся…</p>
<label for="brightness">Яркость <output id="value" for="brightness">%BRIGHTNESS%%</output></label><input id="brightness" type="range" min="0" max="100" value="%BRIGHTNESS%">
<button data-mode="solid" aria-pressed="false">Постоянный свет</button><button data-mode="breathe" aria-pressed="false">Дыхание</button><button data-mode="off" aria-pressed="false">Выключить</button>
<footer>В режиме дыхания ползунок задаёт максимальную яркость.<br>Полный вдох и выдох — 4 секунды.</footer>
<section><h2>Wi-Fi радар</h2><p class="hint">Сети 2,4 ГГц рядом с платой. Чем ближе dBm к нулю, тем сильнее сигнал.</p><button id="scan">Сканировать сети</button><p id="scanStatus" role="status" aria-live="polite">Загружаем результаты…</p><div id="networks"></div><p class="hint">Во время поиска связь может ненадолго замедляться.</p></section>
<noscript>Для управления включи JavaScript в браузере.</noscript></main>
<script>
let mode='%MODE%',pending=null,busy=false,timer;
const slider=document.getElementById('brightness'),value=document.getElementById('value'),status=document.getElementById('status'),lamp=document.getElementById('lamp');
function paint(){value.textContent=slider.value+'%';lamp.className='lamp '+mode;lamp.style.setProperty('--level',Math.max(.04,Number(slider.value)/100));document.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===mode)))}
function describe(s){return s.mode==='off'?'Свет выключен':s.mode==='breathe'?'Дыхание · до '+s.brightness+'%':'Свет включён · '+s.brightness+'%'}
async function send(){
 clearTimeout(timer);pending={mode,brightness:slider.value};if(busy)return;busy=true;
 try{while(pending){const command=pending;pending=null;const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);let r;
 try{r=await fetch('/control',{method:'POST',body:new URLSearchParams(command),signal:controller.signal});if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();status.textContent=describe(s)}finally{clearTimeout(timeout)}}}
 catch(e){pending=null;status.textContent='Нет связи. Подключись к ESP32-DevKit и попробуй ещё раз.'}finally{busy=false}
}
slider.addEventListener('input',()=>{if(mode==='off')mode='solid';paint();clearTimeout(timer);timer=setTimeout(send,120)});slider.addEventListener('change',send);
document.querySelectorAll('[data-mode]').forEach(b=>b.addEventListener('click',()=>{mode=b.dataset.mode;paint();send()}));paint();status.textContent=describe({mode,brightness:slider.value});
const scanButton=document.getElementById('scan'),scanStatus=document.getElementById('scanStatus'),networks=document.getElementById('networks');
let pollTimer,scanErrors=0;
async function scanRequest(method){
 const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),8000);
 try{const r=await fetch('/scan',{method,cache:'no-store',signal:controller.signal});if(!r.ok)throw Error('HTTP '+r.status);return await r.json()}finally{clearTimeout(timeout)}
}
function renderNetworks(list){
 networks.replaceChildren();
 list.forEach(n=>{const row=document.createElement('div'),name=document.createElement('strong'),meta=document.createElement('small'),bar=document.createElement('div'),fill=document.createElement('span');
 row.className='network';name.textContent=n.ssid||'Скрытая сеть';meta.textContent=n.rssi+' dBm · канал '+n.channel+' · '+(n.secure?'Защищена':'Открытая');
 bar.className='signal';fill.style.width=Math.min(100,Math.max(0,2*(n.rssi+100)))+'%';bar.append(fill);row.append(name,meta,bar);networks.append(row)});
}
function showScan(s){
 scanButton.disabled=s.status==='scanning';
 if(s.status==='scanning'){scanStatus.textContent='Ищем сети… Обычно это занимает несколько секунд.';pollTimer=setTimeout(pollScan,900);return}
 if(s.status==='ready'){renderNetworks(s.networks);scanStatus.textContent=s.total?'Найдено: '+s.total+(s.total>s.networks.length?' · показаны '+s.networks.length+' сильнейших':''):'Сети не найдены. Попробуй ещё раз.'}
 else scanStatus.textContent=s.status==='error'?'Поиск не удался. Попробуй ещё раз.':'Нажми «Сканировать сети».';
}
async function pollScan(){
 try{const s=await scanRequest('GET');scanErrors=0;showScan(s)}
 catch(e){if(++scanErrors<5){scanStatus.textContent='Ждём связь с платой…';pollTimer=setTimeout(pollScan,1500)}else{scanButton.disabled=false;scanStatus.textContent='Нет связи. Проверь сеть ESP32-DevKit и повтори поиск.'}}
}
scanButton.addEventListener('click',async()=>{clearTimeout(pollTimer);scanErrors=0;scanButton.disabled=true;scanStatus.textContent='Запускаем поиск…';try{showScan(await scanRequest('POST'))}catch(e){pollScan()}});
pollScan();
</script></body></html>)HTML";

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
  // Arduino-ESP32 3.x attaches PWM directly to the GPIO pin.
  if (!ledcAttach(LED_PIN, 5000, 12)) {
    Serial.println("ERROR: PWM initialization failed");
    return;
  }
  ledcWrite(LED_PIN, 0);
  Serial.println("PWM READY | GPIO2 | 5000 Hz | 12 bit");
  // STA is needed for scanning; AP keeps our control network available.
  WiFi.mode(WIFI_AP_STA);
  const IPAddress ip(192, 168, 4, 1);
  apReady = WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)) && WiFi.softAP(AP_NAME, AP_PASSWORD, 1, false, 4);
  if (!apReady) { Serial.println("ERROR: Wi-Fi access point failed to start"); return; }
  server.on("/", HTTP_GET, []() {
    String html = FPSTR(PAGE);
    html.replace("%BRIGHTNESS%", String(brightness));
    html.replace("%MODE%", lightMode);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", html);
  });
  server.on("/control", HTTP_POST, controlLight);
  server.on("/state", HTTP_GET, sendState);
  server.on("/scan", HTTP_GET, sendScan);
  server.on("/scan", HTTP_POST, []() { startScan(); sendScan(); });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.printf("AP READY | SSID=%s | IP=%s\n", AP_NAME, WiFi.softAPIP().toString().c_str());
  Serial.println("WEB READY | http://192.168.4.1 | brightness + breathing + scanner");
  startScan();
}

void loop() {
  if (apReady) {
    server.handleClient();
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
