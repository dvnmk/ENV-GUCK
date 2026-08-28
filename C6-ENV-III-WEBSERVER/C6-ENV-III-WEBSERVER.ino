#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WebServer.h>
#include "M5UnitENV.h"
#include "secrets.h"

SHT3X sht3x;
QMP6988 qmp;
WebServer server(80);

float temperature = NAN, humidity = NAN, pressure = NAN, altitude = NAN;
float tempMin = NAN, tempMax = NAN;
float humMin = NAN, humMax = NAN;
float pressMin = NAN, pressMax = NAN;
float altMin = NAN, altMax = NAN;

const uint32_t SENSOR_INTERVAL = 10UL * 1000UL;
const uint32_t HISTORY_INTERVAL = 60UL * 1000UL;
const uint32_t STATS_INTERVAL = 24UL * 60UL * 60UL * 1000UL;
const uint32_t WIFI_RETRY_INTERVAL = 30UL * 1000UL;
const uint32_t DIAGNOSTIC_INTERVAL = 5UL * 60UL * 1000UL;

uint32_t lastSensorUpdate = 0;
uint32_t lastHistoryUpdate = 0;
uint32_t lastWiFiRetry = 0;
uint32_t lastDiagnostic = 0;
uint32_t statsStart = 0;

// One sample per minute for 24 hours.  The four arrays use about 23 KB total.
const int HISTORY_SIZE = 1440;
float tempHistory[HISTORY_SIZE];
float humHistory[HISTORY_SIZE];
float pressHistory[HISTORY_SIZE];  // hPa
float altHistory[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

void resetStats() {
  tempMin = tempMax = NAN;
  humMin = humMax = NAN;
  pressMin = pressMax = NAN;
  altMin = altMax = NAN;
  statsStart = millis();
}

void updateRange(float value, float& minimum, float& maximum) {
  if (isnan(value)) return;
  if (isnan(minimum) || value < minimum) minimum = value;
  if (isnan(maximum) || value > maximum) maximum = value;
}

void updateStats() {
  updateRange(temperature, tempMin, tempMax);
  updateRange(humidity, humMin, humMax);
  updateRange(pressure, pressMin, pressMax);
  updateRange(altitude, altMin, altMax);
}

void storeHistory() {
  if (isnan(temperature) || isnan(humidity) || isnan(pressure) || isnan(altitude)) return;
  tempHistory[historyIndex] = temperature;
  humHistory[historyIndex] = humidity;
  pressHistory[historyIndex] = pressure / 100.0f;
  altHistory[historyIndex] = altitude;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

// Send a number without allocating a String.
void sendNumber(float value) {
  char buffer[24];
  if (isnan(value)) {
    server.sendContent("null");
  } else {
    snprintf(buffer, sizeof(buffer), "%.2f", value);
    server.sendContent(buffer);
  }
}

void numberToText(float value, char* buffer, size_t bufferSize) {
  if (isnan(value)) {
    snprintf(buffer, bufferSize, "null");
  } else {
    snprintf(buffer, bufferSize, "%.2f", value);
  }
}

void sendHistoryArray(const float* values) {
  for (int i = 0; i < historyCount; ++i) {
    const int index = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
    if (i) server.sendContent(",");
    sendNumber(values[index]);
  }
}

// Important: stream the response.  Do not build a 40+ KB String in RAM.
void handleHistory() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"temperature\":[");
  sendHistoryArray(tempHistory);
  server.sendContent("],\"humidity\":[");
  sendHistoryArray(humHistory);
  server.sendContent("],\"pressure\":[");
  sendHistoryArray(pressHistory);
  server.sendContent("],\"altitude\":[");
  sendHistoryArray(altHistory);
  server.sendContent("]}");
}

void handleReadings() {
  // Small response, so a fixed stack buffer is simpler and allocation-free.
  char json[512];
  char t[16], h[16], p[16], a[16], tmin[16], tmax[16], hmin[16], hmax[16];
  char pmin[16], pmax[16], amin[16], amax[16];
  numberToText(temperature, t, sizeof(t)); numberToText(humidity, h, sizeof(h));
  numberToText(pressure / 100.0f, p, sizeof(p)); numberToText(altitude, a, sizeof(a));
  numberToText(tempMin, tmin, sizeof(tmin)); numberToText(tempMax, tmax, sizeof(tmax));
  numberToText(humMin, hmin, sizeof(hmin)); numberToText(humMax, hmax, sizeof(hmax));
  numberToText(pressMin / 100.0f, pmin, sizeof(pmin)); numberToText(pressMax / 100.0f, pmax, sizeof(pmax));
  numberToText(altMin, amin, sizeof(amin)); numberToText(altMax, amax, sizeof(amax));
  snprintf(json, sizeof(json),
    "{\"temperature_c\":%s,\"humidity_rh\":%s,\"pressure_hpa\":%s,\"altitude_m\":%s,"
    "\"temperature_min\":%s,\"temperature_max\":%s,\"humidity_min\":%s,\"humidity_max\":%s,"
    "\"pressure_min\":%s,\"pressure_max\":%s,\"altitude_min\":%s,\"altitude_max\":%s}",
    t, h, p, a, tmin, tmax, hmin, hmax, pmin, pmax, amin, amax);
  server.send(200, "application/json", json);
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ENV III</title><style>
body{font-family:sans-serif;margin:2rem;color:#222}table{border-collapse:collapse;width:100%;max-width:700px}th,td{padding:.65rem 1rem;border-bottom:1px solid #ddd;text-align:right}th:first-child,td:first-child{text-align:left}.graph{width:100%;max-width:800px;margin-top:2rem}canvas{width:100%;height:220px;border:1px solid #ddd;display:block}
</style></head><body><h1>ENV III</h1><table><tr><th></th><th>Current</th><th>24h Min</th><th>24h Max</th></tr>
<tr><td>Humidity</td><td id="hum"></td><td id="humMin"></td><td id="humMax"></td></tr>
<tr><td>Temperature</td><td id="temp"></td><td id="tempMin"></td><td id="tempMax"></td></tr>
<tr><td>Pressure</td><td id="press"></td><td id="pressMin"></td><td id="pressMax"></td></tr>
<tr><td>Altitude</td><td id="alt"></td><td id="altMin"></td><td id="altMax"></td></tr></table>
<div class="graph"><h2>Humidity — 24h</h2><canvas id="humidityGraph"></canvas></div>
<div class="graph"><h2>Temperature — 24h</h2><canvas id="temperatureGraph"></canvas></div>
<div class="graph"><h2>Pressure — 24h</h2><canvas id="pressureGraph"></canvas></div>
<div class="graph"><h2>Altitude — 24h</h2><canvas id="altitudeGraph"></canvas></div>
<script>
const put=(id,v,u)=>document.getElementById(id).textContent=Number.isFinite(v)?v.toFixed(2)+' '+u:'--';
async function load(){try{const r=await fetch('/api/readings');const d=await r.json();put('temp',d.temperature_c,'°C');put('tempMin',d.temperature_min,'°C');put('tempMax',d.temperature_max,'°C');put('hum',d.humidity_rh,'%');put('humMin',d.humidity_min,'%');put('humMax',d.humidity_max,'%');put('press',d.pressure_hpa,'hPa');put('pressMin',d.pressure_min,'hPa');put('pressMax',d.pressure_max,'hPa');put('alt',d.altitude_m,'m');put('altMin',d.altitude_min,'m');put('altMax',d.altitude_max,'m');const h=await (await fetch('/api/history')).json();draw('humidityGraph',h.humidity,'%');draw('temperatureGraph',h.temperature,'°C');draw('pressureGraph',h.pressure,'hPa');draw('altitudeGraph',h.altitude,'m')}catch(e){console.log(e)}}
function draw(id,v,unit){const c=document.getElementById(id),x=c.getContext('2d'),r=c.getBoundingClientRect(),d=devicePixelRatio||1,w=r.width,h=r.height;c.width=w*d;c.height=h*d;x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);if(!v.length){x.fillText('Waiting for data…',w/2,h/2);return}let lo=Math.min(...v),hi=Math.max(...v);if(lo===hi){lo--;hi++}const p=(hi-lo)*.08;lo-=p;hi+=p;const L=55,R=15,T=20,B=30,gw=w-L-R,gh=h-T-B;x.font='11px sans-serif';for(let i=0;i<=4;i++){const y=T+gh*i/4,val=hi-(hi-lo)*i/4;x.strokeStyle='#ddd';x.beginPath();x.moveTo(L,y);x.lineTo(w-R,y);x.stroke();x.fillStyle='#666';x.textAlign='right';x.fillText(val.toFixed(1),L-7,y+4)}x.textAlign='center';x.fillText('-24h',L,h-8);x.fillText('-12h',L+gw/2,h-8);x.fillText('now',w-R,h-8);x.strokeStyle='#333';x.lineWidth=1.5;x.beginPath();v.forEach((n,i)=>{const px=L+gw*i/Math.max(1,v.length-1),py=T+(hi-n)/(hi-lo)*gh;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}
load();setInterval(load,60000);
</script></body></html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void keepWiFiAlive() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiRetry < WIFI_RETRY_INTERVAL) return;
  lastWiFiRetry = millis();
  Serial.println("Wi-Fi disconnected; reconnecting");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(2, 1);

  if (!qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 2, 1, 400000U)) {
    Serial.println("Couldn't find QMP6988");
    while (true) delay(1000);
  }
  if (!sht3x.begin(&Wire, SHT3X_I2C_ADDR, 2, 1, 400000U)) {
    Serial.println("Couldn't find SHT3X");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  ArduinoOTA.setHostname("env3");
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/api/readings", handleReadings);
  server.on("/api/history", handleHistory);
  server.begin();

  resetStats();
  lastHistoryUpdate = millis();
  Serial.println("Webserver ready!!!");
}

void loop() {
  keepWiFiAlive();
  server.handleClient();
  ArduinoOTA.handle();

  const uint32_t now = millis();
  if (now - lastSensorUpdate >= SENSOR_INTERVAL) {
    lastSensorUpdate = now;
    if (sht3x.update()) { temperature = sht3x.cTemp; humidity = sht3x.humidity; }
    if (qmp.update())   { pressure = qmp.pressure; altitude = qmp.altitude; }
    updateStats();
  }
  if (now - lastHistoryUpdate >= HISTORY_INTERVAL) {
    lastHistoryUpdate = now;
    storeHistory();
  }
  if (now - statsStart >= STATS_INTERVAL) resetStats();
  if (now - lastDiagnostic >= DIAGNOSTIC_INTERVAL) {
    lastDiagnostic = now;
    Serial.printf("WiFi=%d heap=%u minHeap=%u samples=%d\\n", WiFi.status(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), historyCount);
  }
}
