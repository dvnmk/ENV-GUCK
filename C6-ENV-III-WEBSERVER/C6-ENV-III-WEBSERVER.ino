#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WebServer.h>
#include "M5UnitENV.h"

const char* WIFI_SSID = "XXXX";
const char* WIFI_PASSWORD = "XXXX";

SHT3X sht3x;
QMP6988 qmp;
WebServer server(80);

// ==================================================
// Current readings
// ==================================================

float temperature = NAN;
float humidity = NAN;
float pressure = NAN;   // Pa
float altitude = NAN;   // m

// ==================================================
// 24-hour min / max
// ==================================================

float tempMin = NAN, tempMax = NAN;
float humMin = NAN, humMax = NAN;
float pressMin = NAN, pressMax = NAN;
float altMin = NAN, altMax = NAN;

// ==================================================
// Timing
// ==================================================

unsigned long lastSensorUpdate = 0;
unsigned long lastHistoryUpdate = 0;
unsigned long statsStart = 0;

const unsigned long SENSOR_INTERVAL = 10000;                 // 10 sec
const unsigned long HISTORY_INTERVAL = 60UL * 1000UL; // 1 min
const unsigned long STATS_INTERVAL = 24UL * 60UL * 60UL * 1000UL;

// ==================================================
// 24-hour history
// 1440 points = 24 hours / 1 minutes
// ==================================================

const int HISTORY_SIZE = 1440;

float tempHistory[HISTORY_SIZE];
float humHistory[HISTORY_SIZE];
float pressHistory[HISTORY_SIZE];
float altHistory[HISTORY_SIZE];

int historyIndex = 0;
int historyCount = 0;

// ==================================================
// Reset 24-hour statistics
// ==================================================

void resetStats() {
    tempMin = tempMax = NAN;
    humMin = humMax = NAN;
    pressMin = pressMax = NAN;
    altMin = altMax = NAN;

    statsStart = millis();
}

// ==================================================
// Update min/max
// ==================================================

void updateStats() {

    if (!isnan(temperature)) {
        if (isnan(tempMin) || temperature < tempMin)
            tempMin = temperature;

        if (isnan(tempMax) || temperature > tempMax)
            tempMax = temperature;
    }

    if (!isnan(humidity)) {
        if (isnan(humMin) || humidity < humMin)
            humMin = humidity;

        if (isnan(humMax) || humidity > humMax)
            humMax = humidity;
    }

    if (!isnan(pressure)) {
        if (isnan(pressMin) || pressure < pressMin)
            pressMin = pressure;

        if (isnan(pressMax) || pressure > pressMax)
            pressMax = pressure;
    }

    if (!isnan(altitude)) {
        if (isnan(altMin) || altitude < altMin)
            altMin = altitude;

        if (isnan(altMax) || altitude > altMax)
            altMax = altitude;
    }
}

// ==================================================
// Store one 5-minute history point
// ==================================================

void storeHistory() {

    if (isnan(temperature) ||
        isnan(humidity) ||
        isnan(pressure) ||
        isnan(altitude)) {
        return;
    }

    tempHistory[historyIndex] = temperature;
    humHistory[historyIndex] = humidity;

    // Pa -> hPa for history/display
    pressHistory[historyIndex] = pressure / 100.0;

    altHistory[historyIndex] = altitude;

    historyIndex++;

    if (historyIndex >= HISTORY_SIZE)
        historyIndex = 0;

    if (historyCount < HISTORY_SIZE)
        historyCount++;
}

// ==================================================
// History JSON
// ==================================================

void handleHistory() {

    String json = "{";

    // Temperature
    json += "\"temperature\":[";
    
    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex - historyCount + i + HISTORY_SIZE)
            % HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(tempHistory[index], 2);
    }

    json += "],";

    // Humidity
    json += "\"humidity\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex - historyCount + i + HISTORY_SIZE)
            % HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(humHistory[index], 2);
    }

    json += "],";

    // Pressure
    json += "\"pressure\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex - historyCount + i + HISTORY_SIZE)
            % HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(pressHistory[index], 2);
    }

    json += "],";

    // Altitude
    json += "\"altitude\":[";

    for (int i = 0; i < historyCount; i++) {

        int index =
            (historyIndex - historyCount + i + HISTORY_SIZE)
            % HISTORY_SIZE;

        if (i > 0)
            json += ",";

        json += String(altHistory[index], 2);
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

    String page = R"rawliteral(
<!doctype html>
<html>

<head>

<meta charset="utf-8">

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<meta http-equiv="refresh" content="60">

<title>ENV III</title>

<style>

body {
    font-family: sans-serif;
    margin: 2rem;
    color: #222;
}

table {
    border-collapse: collapse;
    width: 100%;
    max-width: 700px;
}

th, td {
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

</style>

</head>

<body>

<h1>ENV III</h1>

<table>

<tr>
    <th></th>
    <th>Current</th>
    <th>24h Min</th>
    <th>24h Max</th>
</tr>

<!-- HUMIDITY -->

<tr>

    <td>Humidity</td>

    <td>)rawliteral";

    page += String(humidity, 2);

    page += R"rawliteral( %</td>

    <td>)rawliteral";

    page += String(humMin, 2);

    page += R"rawliteral( %</td>

    <td>)rawliteral";

    page += String(humMax, 2);

    page += R"rawliteral( %</td>

</tr>

<!-- TEMPERATURE -->

<tr>

    <td>Temperature</td>

    <td>)rawliteral";

    page += String(temperature, 2);

    page += R"rawliteral( &deg;C</td>

    <td>)rawliteral";

    page += String(tempMin, 2);

    page += R"rawliteral( &deg;C</td>

    <td>)rawliteral";

    page += String(tempMax, 2);

    page += R"rawliteral( &deg;C</td>

</tr>

<!-- PRESSURE -->

<tr>

    <td>Pressure</td>

    <td>)rawliteral";

    page += String(pressure / 100.0, 2);

    page += R"rawliteral( hPa</td>

    <td>)rawliteral";

    page += String(pressMin / 100.0, 2);

    page += R"rawliteral( hPa</td>

    <td>)rawliteral";

    page += String(pressMax / 100.0, 2);

    page += R"rawliteral( hPa</td>

</tr>

<!-- ALTITUDE -->

<tr>

    <td>Altitude</td>

    <td>)rawliteral";

    page += String(altitude, 2);

    page += R"rawliteral( m</td>

    <td>)rawliteral";

    page += String(altMin, 2);

    page += R"rawliteral( m</td>

    <td>)rawliteral";

    page += String(altMax, 2);

    page += R"rawliteral( m</td>

</tr>

</table>


<!-- HUMIDITY GRAPH -->

<div class="graph">

<h2>Humidity — 24h</h2>

<canvas id="humidityGraph"></canvas>

</div>


<!-- TEMPERATURE GRAPH -->

<div class="graph">

<h2>Temperature — 24h</h2>

<canvas id="temperatureGraph"></canvas>

</div>


<!-- PRESSURE GRAPH -->

<div class="graph">

<h2>Pressure — 24h</h2>

<canvas id="pressureGraph"></canvas>

</div>


<!-- ALTITUDE GRAPH -->

<div class="graph">

<h2>Altitude — 24h</h2>

<canvas id="altitudeGraph"></canvas>

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
            'altitudeGraph',
            data.altitude,
            'm'
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


    // ----------------------------------------------
    // Find range
    // ----------------------------------------------

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

    // Add 8% vertical padding

    min -= range * 0.08;
    max += range * 0.08;


    // ----------------------------------------------
    // Graph dimensions
    // ----------------------------------------------

    const left = 55;
    const right = 15;
    const top = 20;
    const bottom = 30;

    const graphWidth =
        width - left - right;

    const graphHeight =
        height - top - bottom;


    // ----------------------------------------------
    // Grid
    // ----------------------------------------------

    ctx.strokeStyle = '#dddddd';
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


        // Y axis label

        const value =
            max -
            (max - min) *
            i / 4;

        ctx.fillStyle = '#666';

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


    // ----------------------------------------------
    // X axis labels
    // ----------------------------------------------

    ctx.fillStyle = '#666';

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


    // ----------------------------------------------
    // Draw connecting line
    // ----------------------------------------------

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

            if (i === 0) {

                ctx.moveTo(
                    x,
                    y
                );

            } else {

                ctx.lineTo(
                    x,
                    y
                );
            }
        }

        ctx.strokeStyle =
            '#333';

        ctx.lineWidth = 1.5;

        ctx.stroke();
    }


    // ----------------------------------------------
    // Draw dots
    // ----------------------------------------------

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
        page
    );
}

// ==================================================
// Current readings JSON
// ==================================================

void handleJson() {

    String json = "{";

    json += "\"temperature_c\":";
    json += String(
        temperature,
        2
    );

    json += ",";

    json += "\"humidity_rh\":";
    json += String(
        humidity,
        2
    );

    json += ",";

    json += "\"pressure_hpa\":";
    json += String(
        pressure / 100.0,
        2
    );

    json += ",";

    json += "\"altitude_m\":";
    json += String(
        altitude,
        2
    );

    json += ",";

    json += "\"24h\":{";

    json += "\"temperature_min\":";
    json += String(
        tempMin,
        2
    );

    json += ",";

    json += "\"temperature_max\":";
    json += String(
        tempMax,
        2
    );

    json += ",";

    json += "\"humidity_min\":";
    json += String(
        humMin,
        2
    );

    json += ",";

    json += "\"humidity_max\":";
    json += String(
        humMax,
        2
    );

    json += ",";

    json += "\"pressure_min\":";
    json += String(
        pressMin / 100.0,
        2
    );

    json += ",";

    json += "\"pressure_max\":";
    json += String(
        pressMax / 100.0,
        2
    );

    json += ",";

    json += "\"altitude_min\":";
    json += String(
        altMin,
        2
    );

    json += ",";

    json += "\"altitude_max\":";
    json += String(
        altMax,
        2
    );

    json += "}";

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ==================================================
// SETUP
// ==================================================

void setup() {

    Serial.begin(115200);


    // ----------------------------------------------
    // QMP6988
    // ----------------------------------------------

    if (!qmp.begin(
            &Wire,
            QMP6988_SLAVE_ADDRESS_L,
            2,
            1,
            400000U)) {

        Serial.println(
            "Couldn't find QMP6988"
        );

        while (true)
            delay(1);
    }


    // ----------------------------------------------
    // SHT3X
    // ----------------------------------------------

    if (!sht3x.begin(
            &Wire,
            SHT3X_I2C_ADDR,
            2,
            1,
            400000U)) {

        Serial.println(
            "Couldn't find SHT3X"
        );

        while (true)
            delay(1);
    }


    // ----------------------------------------------
    // Wi-Fi
    // ----------------------------------------------

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    Serial.print(
        "Connecting to Wi-Fi"
    );

    while (
        WiFi.status() != WL_CONNECTED
    ) {

        delay(500);

        Serial.print(".");
    }

    Serial.println();

    Serial.print(
        "Open: http://"
    );

    Serial.println(
        WiFi.localIP()
    );


    // Arduino OTA
    ArduinoOTA.setHostname("env3");

    ArduinoOTA.onStart([]() {

        Serial.println(
            "OTA Start"
        );
    });

    ArduinoOTA.onEnd([]() {

        Serial.println(
            "\nOTA End"
        );
    });

    ArduinoOTA.onProgress(
        [](unsigned int progress,
           unsigned int total) {

            Serial.printf(
                "OTA Progress: %u%%\r",
                progress /
                (total / 100)
            );
        }
    );

    ArduinoOTA.onError(
        [](ota_error_t error) {

            Serial.printf(
                "OTA Error[%u]: ",
                error
            );

            if (error == OTA_AUTH_ERROR)
                Serial.println(
                    "Auth Failed"
                );

            else if (error == OTA_BEGIN_ERROR)
                Serial.println(
                    "Begin Failed"
                );

            else if (error == OTA_CONNECT_ERROR)
                Serial.println(
                    "Connect Failed"
                );

            else if (error == OTA_RECEIVE_ERROR)
                Serial.println(
                    "Receive Failed"
                );

            else if (error == OTA_END_ERROR)
                Serial.println(
                    "End Failed"
                );
        }
    );

    ArduinoOTA.begin();

    Serial.println(
        "OTA ready"
    );

    // Webserver
    server.on(
        "/",
        handleRoot
    );

    server.on(
        "/api/readings",
        handleJson
    );

    server.on(
        "/api/history",
        handleHistory
    );

    server.begin();

    Serial.println(
        "Webserver ready"
    );


    // Start statistics
    resetStats();

    lastHistoryUpdate =
        millis();
}

// LOOP
void loop() {

    server.handleClient();

    ArduinoOTA.handle();


    // Sensor update every 10 seconds
    if (
        millis() -
        lastSensorUpdate >=
        SENSOR_INTERVAL
    ) {

        lastSensorUpdate =
            millis();


        if (sht3x.update()) {

            temperature =
                sht3x.cTemp;

            humidity =
                sht3x.humidity;
        }


        if (qmp.update()) {

            pressure =
                qmp.pressure;

            altitude =
                qmp.altitude;
        }


        updateStats();
    }


    // ----------------------------------------------
    // Save history every 5 minutes
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
    // Reset 24-hour statistics
    // ----------------------------------------------

    if (
        millis() -
        statsStart >=
        STATS_INTERVAL
    ) {

        resetStats();
    }
}
