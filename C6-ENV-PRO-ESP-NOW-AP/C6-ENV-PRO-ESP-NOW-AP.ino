#include <M5Unified.h>
#include <Wire.h>
#include <bsec2.h>
#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;

Bsec2 envSensor;
WebServer server(80);

// ==================================================
// Sensor payload
// Must match Cardputer exactly
// ==================================================

struct sensorPayload {

    float temperature;
    float pressure;       // hPa
    float humidity;
    float gasResistance;  // Ohm
    float iaq;

    uint8_t iaqAccuracy;
    uint8_t stabilization;
    uint8_t runIn;

    uint32_t timestamp;

    // 24h min/max
    float temperatureMin;
    float temperatureMax;

    float humidityMin;
    float humidityMax;

    float pressureMin;
    float pressureMax;

    float gasMin;
    float gasMax;

    float iaqMin;
    float iaqMax;
};

sensorPayload latest = {};

bool dataReady = false;

// ==================================================
// ESP-NOW broadcast
// ==================================================

uint8_t peerMac[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

// ==================================================
// 24-hour min/max
// ==================================================

float tempMin = NAN;
float tempMax = NAN;

float humMin = NAN;
float humMax = NAN;

float pressMin = NAN;
float pressMax = NAN;

float gasMin = NAN;
float gasMax = NAN;

float iaqMin = NAN;
float iaqMax = NAN;

// ==================================================
// Timing
// ==================================================

unsigned long statsStart = 0;
unsigned long lastHistoryUpdate = 0;

const unsigned long STATS_INTERVAL =
    24UL * 60UL * 60UL * 1000UL;

const unsigned long HISTORY_INTERVAL =
    60UL * 1000UL;

// ==================================================
// 24-hour history
// ==================================================

const int HISTORY_SIZE = 1440;

float tempHistory[HISTORY_SIZE];
float humHistory[HISTORY_SIZE];
float pressHistory[HISTORY_SIZE];
float gasHistory[HISTORY_SIZE];
float iaqHistory[HISTORY_SIZE];

int historyIndex = 0;
int historyCount = 0;

// ==================================================
// Forward declarations
// ==================================================

void checkBsecStatus(Bsec2 bsec);

void newDataCallback(
    const bme68xData data,
    const bsecOutputs outputs,
    Bsec2 bsec
);

// ==================================================
// Reset statistics
// ==================================================

void resetStats() {

    tempMin = tempMax = NAN;
    humMin = humMax = NAN;
    pressMin = pressMax = NAN;
    gasMin = gasMax = NAN;
    iaqMin = iaqMax = NAN;

    statsStart = millis();
}

// ==================================================
// Update min/max
// ==================================================

void updateStats() {

    if (!isnan(latest.temperature)) {

        if (isnan(tempMin) ||
            latest.temperature < tempMin)
            tempMin = latest.temperature;

        if (isnan(tempMax) ||
            latest.temperature > tempMax)
            tempMax = latest.temperature;
    }

    if (!isnan(latest.humidity)) {

        if (isnan(humMin) ||
            latest.humidity < humMin)
            humMin = latest.humidity;

        if (isnan(humMax) ||
            latest.humidity > humMax)
            humMax = latest.humidity;
    }

    if (!isnan(latest.pressure)) {

        if (isnan(pressMin) ||
            latest.pressure < pressMin)
            pressMin = latest.pressure;

        if (isnan(pressMax) ||
            latest.pressure > pressMax)
            pressMax = latest.pressure;
    }

    if (!isnan(latest.gasResistance)) {

        if (isnan(gasMin) ||
            latest.gasResistance < gasMin)
            gasMin = latest.gasResistance;

        if (isnan(gasMax) ||
            latest.gasResistance > gasMax)
            gasMax = latest.gasResistance;
    }

    if (!isnan(latest.iaq)) {

        if (isnan(iaqMin) ||
            latest.iaq < iaqMin)
            iaqMin = latest.iaq;

        if (isnan(iaqMax) ||
            latest.iaq > iaqMax)
            iaqMax = latest.iaq;
    }
}

// ==================================================
// Copy statistics into payload
// ==================================================

void updatePayloadStats() {

    latest.temperatureMin = tempMin;
    latest.temperatureMax = tempMax;

    latest.humidityMin = humMin;
    latest.humidityMax = humMax;

    latest.pressureMin = pressMin;
    latest.pressureMax = pressMax;

    latest.gasMin = gasMin;
    latest.gasMax = gasMax;

    latest.iaqMin = iaqMin;
    latest.iaqMax = iaqMax;
}

// ==================================================
// Store history
// ==================================================

void storeHistory() {

    tempHistory[historyIndex] =
        latest.temperature;

    humHistory[historyIndex] =
        latest.humidity;

    pressHistory[historyIndex] =
        latest.pressure;

    gasHistory[historyIndex] =
        latest.gasResistance;

    iaqHistory[historyIndex] =
        latest.iaq;

    historyIndex++;

    if (historyIndex >= HISTORY_SIZE)
        historyIndex = 0;

    if (historyCount < HISTORY_SIZE)
        historyCount++;
}

// ==================================================
// ESP-NOW send
// ==================================================

void sendESPNow() {

    // Make sure the latest min/max values
    // are included in the packet.

    updatePayloadStats();

    esp_err_t result =
        esp_now_send(
            peerMac,
            (uint8_t*)&latest,
            sizeof(latest)
        );

    if (result != ESP_OK) {

        Serial.println(
            "ESP-NOW send error"
        );
    }
}

// ==================================================
// History JSON
// ==================================================

void handleHistory() {

    String json = "{";

    json += "\"temperature\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex -
             historyCount +
             i +
             HISTORY_SIZE) %
            HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(
            tempHistory[index],
            2
        );
    }

    json += "],";

    json += "\"humidity\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex -
             historyCount +
             i +
             HISTORY_SIZE) %
            HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(
            humHistory[index],
            2
        );
    }

    json += "],";

    json += "\"pressure\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex -
             historyCount +
             i +
             HISTORY_SIZE) %
            HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(
            pressHistory[index],
            2
        );
    }

    json += "],";

    json += "\"gas\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex -
             historyCount +
             i +
             HISTORY_SIZE) %
            HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(
            gasHistory[index],
            0
        );
    }

    json += "],";

    json += "\"iaq\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex -
             historyCount +
             i +
             HISTORY_SIZE) %
            HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(
            iaqHistory[index],
            1
        );
    }

    json += "]";

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ==================================================
// Main webpage
// ==================================================

void handleRoot() {

    String html = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<meta http-equiv="refresh"
      content="10">

<title>ENV PRO</title>

<style>

body {
    font-family: sans-serif;
    margin: 2rem;
    color: #222;
}

table {
    border-collapse: collapse;
    width: 100%;
    max-width: 750px;
}

th,
td {
    padding: .65rem 1rem;
    border-bottom: 1px solid #ddd;
    text-align: right;
}

th:first-child,
td:first-child {
    text-align: left;
}

th {
    font-weight: 600;
}

.graph {
    width: 100%;
    max-width: 800px;
    margin-top: 2rem;
}

.graph h2 {
    margin-bottom: .5rem;
}

canvas {
    width: 100%;
    height: 220px;
    border: 1px solid #ddd;
    display: block;
}

.status {
    margin-top: 1rem;
    color: #555;
}

</style>

</head>

<body>

<h1>ENV PRO</h1>

<table>

<tr>
    <th></th>
    <th>Current</th>
    <th>24h Min</th>
    <th>24h Max</th>
</tr>

<tr>

<td>Humidity</td>

<td>)rawliteral";

    html += String(
        latest.humidity,
        1
    );

    html += R"rawliteral( %</td>

<td>)rawliteral";

    html += String(
        humMin,
        1
    );

    html += R"rawliteral( %</td>

<td>)rawliteral";

    html += String(
        humMax,
        1
    );

    html += R"rawliteral( %</td>

</tr>

<tr>

<td>Temperature</td>

<td>)rawliteral";

    html += String(
        latest.temperature,
        1
    );

    html += R"rawliteral( &deg;C</td>

<td>)rawliteral";

    html += String(
        tempMin,
        1
    );

    html += R"rawliteral( &deg;C</td>

<td>)rawliteral";

    html += String(
        tempMax,
        1
    );

    html += R"rawliteral( &deg;C</td>

</tr>

<tr>

<td>Pressure</td>

<td>)rawliteral";

    html += String(
        latest.pressure,
        1
    );

    html += R"rawliteral( hPa</td>

<td>)rawliteral";

    html += String(
        pressMin,
        1
    );

    html += R"rawliteral( hPa</td>

<td>)rawliteral";

    html += String(
        pressMax,
        1
    );

    html += R"rawliteral( hPa</td>

</tr>

<tr>

<td>Gas resistance</td>

<td>)rawliteral";

    html += String(
        latest.gasResistance,
        0
    );

    html += R"rawliteral( &Omega;</td>

<td>)rawliteral";

    html += String(
        gasMin,
        0
    );

    html += R"rawliteral( &Omega;</td>

<td>)rawliteral";

    html += String(
        gasMax,
        0
    );

    html += R"rawliteral( &Omega;</td>

</tr>

<tr>

<td>IAQ</td>

<td>)rawliteral";

    html += String(
        latest.iaq,
        1
    );

    html += R"rawliteral(</td>

<td>)rawliteral";

    html += String(
        iaqMin,
        1
    );

    html += R"rawliteral(</td>

<td>)rawliteral";

    html += String(
        iaqMax,
        1
    );

    html += R"rawliteral(</td>

</tr>

</table>

<div class="status">

IAQ accuracy:
)rawliteral";

    html += String(
        latest.iaqAccuracy
    );

    html += R"rawliteral(

&nbsp;&nbsp;

Stabilization:
)rawliteral";

    html += String(
        latest.stabilization
    );

    html += R"rawliteral(

&nbsp;&nbsp;

Run-in:
)rawliteral";

    html += String(
        latest.runIn
    );

    html += R"rawliteral(

</div>


<div class="graph">

<h2>Humidity &mdash; 24h</h2>

<canvas id="humidityGraph"></canvas>

</div>


<div class="graph">

<h2>Temperature &mdash; 24h</h2>

<canvas id="temperatureGraph"></canvas>

</div>


<div class="graph">

<h2>Pressure &mdash; 24h</h2>

<canvas id="pressureGraph"></canvas>

</div>


<div class="graph">

<h2>Gas resistance &mdash; 24h</h2>

<canvas id="gasGraph"></canvas>

</div>


<div class="graph">

<h2>IAQ &mdash; 24h</h2>

<canvas id="iaqGraph"></canvas>

</div>


<script>

async function loadHistory() {

    try {

        const response =
            await fetch('/api/history');

        const data =
            await response.json();

        drawGraph(
            'humidityGraph',
            data.humidity,
            '%'
        );

        drawGraph(
            'temperatureGraph',
            data.temperature,
            '°C'
        );

        drawGraph(
            'pressureGraph',
            data.pressure,
            'hPa'
        );

        drawGraph(
            'gasGraph',
            data.gas,
            'Ω'
        );

        drawGraph(
            'iaqGraph',
            data.iaq,
            ''
        );

    } catch (error) {

        console.log(
            'History error:',
            error
        );
    }
}


function drawGraph(
    canvasId,
    values,
    unit
) {

    const canvas =
        document.getElementById(canvasId);

    const ctx =
        canvas.getContext('2d');

    const rect =
        canvas.getBoundingClientRect();

    const dpr =
        window.devicePixelRatio || 1;

    canvas.width =
        rect.width * dpr;

    canvas.height =
        rect.height * dpr;

    ctx.scale(dpr, dpr);

    const width =
        rect.width;

    const height =
        rect.height;

    ctx.clearRect(
        0,
        0,
        width,
        height
    );

    if (!values ||
        values.length === 0) {

        ctx.fillStyle = '#777';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';

        ctx.fillText(
            'Waiting for data...',
            width / 2,
            height / 2
        );

        return;
    }

    let min =
        Math.min(...values);

    let max =
        Math.max(...values);

    if (min === max) {

        min -= 1;
        max += 1;
    }

    const range =
        max - min;

    min -= range * 0.08;
    max += range * 0.08;

    const left = 60;
    const right = 15;
    const top = 20;
    const bottom = 30;

    const graphWidth =
        width - left - right;

    const graphHeight =
        height - top - bottom;

    ctx.strokeStyle =
        '#dddddd';

    ctx.lineWidth = 1;

    for (let i = 0; i <= 4; i++) {

        const y =
            top +
            graphHeight * i / 4;

        ctx.beginPath();

        ctx.moveTo(
            left,
            y
        );

        ctx.lineTo(
            width - right,
            y
        );

        ctx.stroke();

        const value =
            max -
            (max - min) *
            i / 4;

        ctx.fillStyle =
            '#666';

        ctx.font =
            '11px sans-serif';

        ctx.textAlign =
            'right';

        ctx.fillText(
            value.toFixed(1),
            left - 7,
            y + 4
        );
    }

    ctx.fillStyle =
        '#666';

    ctx.font =
        '11px sans-serif';

    ctx.textAlign =
        'center';

    ctx.fillText(
        '-24h',
        left,
        height - 8
    );

    ctx.fillText(
        '-12h',
        left + graphWidth / 2,
        height - 8
    );

    ctx.fillText(
        'now',
        width - right,
        height - 8
    );

    if (values.length > 1) {

        ctx.beginPath();

        for (
            let i = 0;
            i < values.length;
            i++
        ) {

            const x =
                left +
                graphWidth *
                i /
                (values.length - 1);

            const y =
                top +
                (max - values[i]) /
                (max - min) *
                graphHeight;

            if (i === 0)
                ctx.moveTo(x, y);
            else
                ctx.lineTo(x, y);
        }

        ctx.strokeStyle =
            '#333';

        ctx.lineWidth =
            1.5;

        ctx.stroke();
    }

    ctx.fillStyle =
        '#222';

    for (
        let i = 0;
        i < values.length;
        i++
    ) {

        const x =
            left +
            graphWidth *
            i /
            Math.max(
                1,
                values.length - 1
            );

        const y =
            top +
            (max - values[i]) /
            (max - min) *
            graphHeight;

        ctx.beginPath();

        ctx.arc(
            x,
            y,
            2,
            0,
            Math.PI * 2
        );

        ctx.fill();
    }
}

loadHistory();

</script>

</body>

</html>
)rawliteral";

    server.send(
        200,
        "text/html",
        html
    );
}

// ==================================================
// JSON
// ==================================================

void handleJson() {

    updatePayloadStats();

    String json = "{";

    json += "\"iaq\":";
    json += String(latest.iaq, 2);

    json += ",\"accuracy\":";
    json += String(latest.iaqAccuracy);

    json += ",\"temperature\":";
    json += String(latest.temperature, 2);

    json += ",\"humidity\":";
    json += String(latest.humidity, 2);

    json += ",\"pressure\":";
    json += String(latest.pressure, 2);

    json += ",\"gas\":";
    json += String(latest.gasResistance, 0);

    json += ",\"stab\":";
    json += String(latest.stabilization);

    json += ",\"runIn\":";
    json += String(latest.runIn);

    json += ",\"24h\":{";

    json += "\"temperature_min\":";
    json += String(tempMin, 2);

    json += ",\"temperature_max\":";
    json += String(tempMax, 2);

    json += ",\"humidity_min\":";
    json += String(humMin, 2);

    json += ",\"humidity_max\":";
    json += String(humMax, 2);

    json += ",\"pressure_min\":";
    json += String(pressMin, 2);

    json += ",\"pressure_max\":";
    json += String(pressMax, 2);

    json += ",\"gas_min\":";
    json += String(gasMin, 0);

    json += ",\"gas_max\":";
    json += String(gasMax, 0);

    json += ",\"iaq_min\":";
    json += String(iaqMin, 2);

    json += ",\"iaq_max\":";
    json += String(iaqMax, 2);

    json += "}";

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ==================================================
// BSEC callback
// ==================================================

void newDataCallback(
    const bme68xData data,
    const bsecOutputs outputs,
    Bsec2 bsec
) {

    if (!outputs.nOutputs)
        return;

    for (
        uint8_t i = 0;
        i < outputs.nOutputs;
        i++
    ) {

        const bsecData output =
            outputs.output[i];

        switch (output.sensor_id) {

            case BSEC_OUTPUT_IAQ:

                latest.iaq =
                    output.signal;

                latest.iaqAccuracy =
                    output.accuracy;

                break;

            case BSEC_OUTPUT_RAW_TEMPERATURE:

                latest.temperature =
                    output.signal;

                break;

            case BSEC_OUTPUT_RAW_PRESSURE:

                // BSEC output is hPa

                latest.pressure =
                    output.signal;

                break;

            case BSEC_OUTPUT_RAW_HUMIDITY:

                latest.humidity =
                    output.signal;

                break;

            case BSEC_OUTPUT_RAW_GAS:

                latest.gasResistance =
                    output.signal;

                break;

            case BSEC_OUTPUT_STABILIZATION_STATUS:

                latest.stabilization =
                    (uint8_t)output.signal;

                break;

            case BSEC_OUTPUT_RUN_IN_STATUS:

                latest.runIn =
                    (uint8_t)output.signal;

                break;
        }
    }

    latest.timestamp =
        outputs.output[0].time_stamp /
        1000000ULL;

    updateStats();

    dataReady = true;
}

// ==================================================
// BSEC status
// ==================================================

void checkBsecStatus(Bsec2 bsec) {

    if (bsec.status < BSEC_OK) {

        Serial.println(
            "BSEC error code : " +
            String(bsec.status)
        );

    } else if (bsec.status > BSEC_OK) {

        Serial.println(
            "BSEC warning code : " +
            String(bsec.status)
        );
    }

    if (bsec.sensor.status < BME68X_OK) {

        Serial.println(
            "BME68X error code : " +
            String(bsec.sensor.status)
        );

    } else if (bsec.sensor.status > BME68X_OK) {

        Serial.println(
            "BME68X warning code : " +
            String(bsec.sensor.status)
        );
    }
}

// ==================================================
// SETUP
// ==================================================

void setup() {

    M5.begin();

    Serial.begin(115200);

    Wire.begin(2, 1);

    // ----------------------------------------------
    // BSEC outputs
    // ----------------------------------------------

    bsecSensor sensorList[] = {

        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_STABILIZATION_STATUS,
        BSEC_OUTPUT_RUN_IN_STATUS
    };

    // ----------------------------------------------
    // BME688
    // ----------------------------------------------

    if (
        !envSensor.begin(
            BME68X_I2C_ADDR_HIGH,
            Wire
        )
    ) {

        checkBsecStatus(
            envSensor
        );
    }

    if (
        !envSensor.updateSubscription(
            sensorList,
            ARRAY_LEN(sensorList),
            BSEC_SAMPLE_RATE_LP
        )
    ) {

        checkBsecStatus(
            envSensor
        );
    }

    envSensor.attachCallback(
        newDataCallback
    );

    // ----------------------------------------------
    // Wi-Fi AP
    // ----------------------------------------------

    WiFi.mode(WIFI_STA);

    WiFi.softAP(
        "ENV_PRO_AP",
        "12345678"
    );

    Serial.print(
        "AP IP: "
    );

    Serial.println(
        WiFi.softAPIP()
    );

    // ----------------------------------------------
    // ESP-NOW
    // ----------------------------------------------

    if (
        esp_now_init() != ESP_OK
    ) {

        Serial.println(
            "ESP-NOW init failed"
        );
    }

    esp_now_peer_info_t peerInfo{};

    memcpy(
        peerInfo.peer_addr,
        peerMac,
        6
    );

    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (
        esp_now_add_peer(
            &peerInfo
        ) != ESP_OK
    ) {

        Serial.println(
            "ESP-NOW peer add failed"
        );
    }

    // ----------------------------------------------
    // Web server
    // ----------------------------------------------

    server.on(
        "/",
        handleRoot
    );

    server.on(
        "/data",
        handleJson
    );

    server.on(
        "/api/history",
        handleHistory
    );

    server.begin();

    // ----------------------------------------------
    // Statistics
    // ----------------------------------------------

    resetStats();

    lastHistoryUpdate =
        millis();

    Serial.println(
        "ENV PRO ready"
    );
}

// ==================================================
// LOOP
// ==================================================

void loop() {

    if (
        !envSensor.run()
    ) {

        checkBsecStatus(
            envSensor
        );
    }

    server.handleClient();

		if (dataReady) {

    dataReady = false;

    if (millis() - lastSend >= SEND_INTERVAL) {

        lastSend = millis();

        sendESPNow();

        Serial.printf(
            "ENV: %.1f C | %.1f %% | %.1f hPa | IAQ %.1f | Gas %.0f Ohm | "
            "24h T %.1f-%.1f | H %.1f-%.1f\n",
            latest.temperature,
            latest.humidity,
            latest.pressure,
            latest.iaq,
            latest.gasResistance,
            tempMin,
            tempMax,
            humMin,
            humMax
        );
    }
}
		
    // ----------------------------------------------
    // Store history every minute
    // ----------------------------------------------

    if (
        millis() -
        lastHistoryUpdate >=
        HISTORY_INTERVAL
    ) {

        lastHistoryUpdate =
            millis();

        storeHistory();
    }

    // ----------------------------------------------
    // Reset 24h statistics
    // ----------------------------------------------

    if (
        millis() -
        statsStart >=
        STATS_INTERVAL
    ) {

        resetStats();
    }
}
