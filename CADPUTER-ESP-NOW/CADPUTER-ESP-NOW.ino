#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SD.h>
#include <SPI.h>

/* ============================================================
 * ESP-NOW SENSOR PAYLOAD
 *
 * MUST EXACTLY MATCH THE NanoC6 SENDER
 * ============================================================ */

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

    /* NanoC6 24H MIN/MAX */

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


/* ============================================================
 * SENSOR DATA
 * ============================================================ */

sensorPayload sensor = {};

volatile bool newData = false;
volatile unsigned long lastReceive = 0;

bool receivedOnce = false;


/* ============================================================
 * DISPLAY VALUES
 *
 * These are copied from the latest ESP-NOW packet.
 * ============================================================ */

float displayTemperature = 0.0;
float displayPressure = 0.0;
float displayHumidity = 0.0;
float displayGasResistance = 0.0;
float displayIAQ = 0.0;

uint8_t displayIAQAccuracy = 0;
uint8_t displayStabilization = 0;
uint8_t displayRunIn = 0;


/* ============================================================
 * MIN / MAX
 *
 * These come DIRECTLY from NanoC6.
 *
 * Cardputer does NOT calculate these.
 * ============================================================ */

float temperatureMin = 0.0;
float temperatureMax = 0.0;

float humidityMin = 0.0;
float humidityMax = 0.0;

float pressureMin = 0.0;
float pressureMax = 0.0;

float gasMin = 0.0;
float gasMax = 0.0;

float iaqMin = 0.0;
float iaqMax = 0.0;


/* ============================================================
 * HISTORY
 *
 * Cardputer-local history.
 *
 * 1440 points = 24 hours
 * 1 point = 1 minute
 * ============================================================ */

#define HISTORY_POINTS 1440

float historyTemperature[HISTORY_POINTS];
float historyHumidity[HISTORY_POINTS];
float historyPressure[HISTORY_POINTS];
float historyGas[HISTORY_POINTS];
float historyIAQ[HISTORY_POINTS];

uint16_t historyIndex = 0;

bool historyFilled = false;
bool historyStarted = false;

unsigned long lastHistorySample = 0;

//const unsigned long HISTORY_INTERVAL = 10000UL;
//const unsigned long HISTORY_INTERVAL = 30000UL;
//const unsigned long HISTORY_INTERVAL = 60000UL;

const unsigned long historyIntervals[] = {
    10000UL,  // 10 seconds: about 4 hours
    30000UL,  // 30 seconds: about 12 hours
    60000UL   // 60 seconds: about 24 hours
};

uint8_t historyIntervalIndex = 0;
unsigned long historyInterval =
    historyIntervals[historyIntervalIndex];

void clearHistory()
{
    historyIndex = 0;
    historyFilled = false;
    historyStarted = false;
    lastHistorySample = 0;
}



/* ============================================================
 * HISTORY MODE
 * ============================================================ */

char historyMode = 'T';


/* ============================================================
 * PAGE
 *
 * 1 = ENV
 * 2 = MIN/MAX
 * 3 = HISTORY
 * 4 = CONTROL
 * ============================================================ */

uint8_t page = 1;

void drawPage();

void cycleHistoryInterval()
{
    historyIntervalIndex++;

    if (historyIntervalIndex >= 3) {
        historyIntervalIndex = 0;
    }

    historyInterval =
        historyIntervals[historyIntervalIndex];

    clearHistory();

    page = 3;
    drawPage();
}


/* ============================================================
 * BRIGHTNESS
 * ============================================================ */

uint8_t brightnessLevels[5] = {
    0,
    30,
    100,
    200,
    255
};

uint8_t currentBrightnessIndex = 2;


void cycleBrightness()
{
    currentBrightnessIndex++;

    if (currentBrightnessIndex >= 5) {
        currentBrightnessIndex = 0;
    }

    M5Cardputer.Display.setBrightness(
        brightnessLevels[currentBrightnessIndex]
    );
}


/* ============================================================
 * BATTERY
 * ============================================================ */

int getBatteryPercent()
{
    return M5Cardputer.Power.getBatteryLevel();
}


/* ============================================================
 * COPY ESP-NOW DATA TO DISPLAY VARIABLES
 * ============================================================ */

void updateDisplayValues()
{
    displayTemperature =
        sensor.temperature;

    displayPressure =
        sensor.pressure;

    displayHumidity =
        sensor.humidity;

    displayGasResistance =
        sensor.gasResistance;

    displayIAQ =
        sensor.iaq;

    displayIAQAccuracy =
        sensor.iaqAccuracy;

    displayStabilization =
        sensor.stabilization;

    displayRunIn =
        sensor.runIn;


    /* --------------------------------------------------------
     * MIN/MAX come from NanoC6
     * -------------------------------------------------------- */

    temperatureMin =
        sensor.temperatureMin;

    temperatureMax =
        sensor.temperatureMax;

    humidityMin =
        sensor.humidityMin;

    humidityMax =
        sensor.humidityMax;

    pressureMin =
        sensor.pressureMin;

    pressureMax =
        sensor.pressureMax;

    gasMin =
        sensor.gasMin;

    gasMax =
        sensor.gasMax;

    iaqMin =
        sensor.iaqMin;

    iaqMax =
        sensor.iaqMax;
}


/* ============================================================
 * ADD ONE HISTORY SAMPLE
 *
 * This uses the latest ESP-NOW values.
 * ============================================================ */

void addHistorySample()
{
    historyTemperature[historyIndex] =
        displayTemperature;

    historyHumidity[historyIndex] =
        displayHumidity;

    historyPressure[historyIndex] =
        displayPressure;

    historyGas[historyIndex] =
        displayGasResistance;

    historyIAQ[historyIndex] =
        displayIAQ;


    historyIndex++;


    if (historyIndex >= HISTORY_POINTS) {

        historyIndex = 0;

        historyFilled = true;
    }
}

void updateHistoryOnReceive()
{
    unsigned long now = millis();

    if (!historyStarted) {
        addHistorySample();
        historyStarted = true;
        lastHistorySample = now;
        return;
    }

    if (now - lastHistorySample >= historyInterval) {
        addHistorySample();
        lastHistorySample = now;
    }
}

/* ============================================================
 * UPDATE HISTORY
 *
 * IMPORTANT:
 *
 * First ESP-NOW packet:
 *     immediately creates first history point.
 *
 * After that:
 *     one point every 60 seconds.
 *
 * History is therefore based ONLY on received ESP-NOW data.
 * ============================================================ */

/* void updateHistory() */
/* { */
/*     if (!receivedOnce) { */
/*         return; */
/*     } */


/*     unsigned long now = millis(); */


/*     /\* -------------------------------------------------------- */
/*      * First sample */
/*      * -------------------------------------------------------- *\/ */

/*     if (!historyStarted) { */

/*         addHistorySample(); */

/*         historyStarted = true; */

/*         lastHistorySample = now; */

/*         Serial.println( */
/*             "HISTORY: first sample added" */
/*         ); */

/*         return; */
/*     } */


/*     /\* -------------------------------------------------------- */
/*      * Normal 1-minute sampling */
/*      * -------------------------------------------------------- *\/ */

/*     if ( */
/*         now - lastHistorySample >= */
/*         HISTORY_INTERVAL */
/*     ) { */

/*         /\* */
/*          * Use the current latest ESP-NOW value. */
/*          *\/ */

/*         addHistorySample(); */

/*         lastHistorySample = now; */

/*         Serial.printf( */
/*             "HISTORY: sample %u added\n", */
/*             historyIndex */
/*         ); */


/*         /\* */
/*          * If currently viewing history, */
/*          * update the graph. */
/*          *\/ */

/*         if (page == 3) { */
/*             drawHistory(); */
/*         } */
/*     } */
/* } */


/* ============================================================
 * HEADER
 * ============================================================ */

void drawHeader(const char *title)
{
    M5Cardputer.Display.fillScreen(BLACK);

    M5Cardputer.Display.setTextColor(WHITE);

    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(
        5,
        4
    );

    M5Cardputer.Display.print(title);


    /* Page number */

    char pageText[4];

    sprintf(
        pageText,
        "#%d",
        page
    );

    int16_t textWidth =
        M5Cardputer.Display.textWidth(
            pageText
        );

    M5Cardputer.Display.setCursor(
        235 - textWidth,
        4
    );

    M5Cardputer.Display.print(
        pageText
    );


    /* Header line */

    M5Cardputer.Display.drawLine(
        5,
        27,
        235,
        27,
        DARKGREY
    );
}


/* ============================================================
 * PAGE 1
 *
 * ENV
 * ============================================================ */

void drawEnvironment()
{
    drawHeader("ENV PRO");

    M5Cardputer.Display.setTextSize(2);


    /* Battery */

    M5Cardputer.Display.setCursor(
        55,
        4
    );

    M5Cardputer.Display.printf(
        "       %3d%%",
        getBatteryPercent()
    );


    /* Temperature */

    M5Cardputer.Display.setCursor(
        5,
        34
    );

    M5Cardputer.Display.printf(
        "TMP %6.1f C",
        displayTemperature
    );


    /* Humidity */

    M5Cardputer.Display.setCursor(
        5,
        54
    );

    M5Cardputer.Display.printf(
        "HMD %6.1f %%",
        displayHumidity
    );


    /* Pressure */

    M5Cardputer.Display.setCursor(
        5,
        74
    );

    M5Cardputer.Display.printf(
        "PRS %6.1f hPa",
        displayPressure
    );


    /* Gas */

    M5Cardputer.Display.setCursor(
        5,
        94
    );

    if (
        displayGasResistance >=
        1000.0
    ) {

        M5Cardputer.Display.printf(
            "GAS %6.1f kOhm",
            displayGasResistance / 1000.0
        );

    } else {

        M5Cardputer.Display.printf(
            "GAS  %6.0f Ohm",
            displayGasResistance
        );
    }


    /* IAQ */

    M5Cardputer.Display.setCursor(
        5,
        114
    );

    M5Cardputer.Display.printf(
        "IAQ %6.1f ASR %d%d%d",
        displayIAQ,
        displayIAQAccuracy,
        displayStabilization,
        displayRunIn
    );
}


/* ============================================================
 * PAGE 2
 *
 * MIN / MAX
 *
 * Values originate from NanoC6.
 * ============================================================ */

void drawMinMax()
{
    drawHeader("24HR MIN/MAX");

    M5Cardputer.Display.setTextSize(2);


    /* Temperature */

    M5Cardputer.Display.setCursor(
        5,
        34
    );

    M5Cardputer.Display.printf(
        "TMP %6.1f  %6.1f",
        temperatureMin,
        temperatureMax
    );


    /* Humidity */

    M5Cardputer.Display.setCursor(
        5,
        54
    );

    M5Cardputer.Display.printf(
        "HMD %6.1f  %6.1f",
        humidityMin,
        humidityMax
    );


    /* Pressure */

    M5Cardputer.Display.setCursor(
        5,
        74
    );

    M5Cardputer.Display.printf(
        "PRS %6.1f  %6.1f",
        pressureMin,
        pressureMax
    );


    /* Gas */

    M5Cardputer.Display.setCursor(
        5,
        94
    );

    M5Cardputer.Display.printf(
        "GAS %6.1f  %6.1f",
        gasMin / 1000.0,
        gasMax / 1000.0
    );


    /* IAQ */

    M5Cardputer.Display.setCursor(
        5,
        114
    );

    M5Cardputer.Display.printf(
        "IAQ %6.1f  %6.1f",
        iaqMin,
        iaqMax
    );
}


/* ============================================================
 * PAGE 3
 *
 * HISTORY GRAPH
 * ============================================================ */
void formatHistoryAge(char *buffer, size_t bufferSize, uint32_t seconds)
{
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;

    if (hours == 0) {
        snprintf(buffer, bufferSize, "%lum", minutes);
    } else if (minutes == 0) {
        snprintf(buffer, bufferSize, "%luh", hours);
    } else {
        snprintf(buffer, bufferSize, "%luh%02lum", hours, minutes);
    }
}

void drawHistory()
{
    /* --------------------------------------------------------
     * Header
     * -------------------------------------------------------- */

    M5Cardputer.Display.fillScreen(BLACK);

    M5Cardputer.Display.setTextColor(WHITE);

    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(
        5,
        4
    );


    switch (historyMode) {

        case 'T':

            M5Cardputer.Display.printf(
                "TEMP %.1f C",
                displayTemperature
            );

            break;


        case 'H':

            M5Cardputer.Display.printf(
                "HMD %.1f %%",
                displayHumidity
            );

            break;


        case 'P':

            M5Cardputer.Display.printf(
                "PRS %.1f hPa",
                displayPressure
            );

            break;


        case 'G':

            M5Cardputer.Display.printf(
                "GAS %.1f kOhm",
                displayGasResistance / 1000.0
            );

            break;


        case 'I':

            M5Cardputer.Display.printf(
                "IAQ %.1f",
                displayIAQ
            );

            break;
    }


    /* Page indicator */

    char pageText[4];

    sprintf(
        pageText,
        "#%d",
        page
    );

    int16_t textWidth =
        M5Cardputer.Display.textWidth(
            pageText
        );

    M5Cardputer.Display.setCursor(
        235 - textWidth,
        4
    );

    M5Cardputer.Display.print(
        pageText
    );


    /* Header line */

    M5Cardputer.Display.drawLine(
        5,
        27,
        235,
        27,
        DARKGREY
    );


    /* --------------------------------------------------------
     * No ESP-NOW data yet
     * -------------------------------------------------------- */

    if (!receivedOnce) {

        M5Cardputer.Display.setTextSize(2);

        M5Cardputer.Display.setCursor(
            25,
            60
        );

        M5Cardputer.Display.print(
            "WAITING FOR DATA"
        );

        return;
    }


    /* --------------------------------------------------------
     * Graph
     * -------------------------------------------------------- */

    M5Cardputer.Display.setTextSize(1);

    const int graphX = 28;
    const int graphY = 35;
    const int graphW = 207;
    const int graphH = 70;


    float *history = nullptr;


    /* Select history array */

    switch (historyMode) {

        case 'T':
            history =
                historyTemperature;
            break;

        case 'H':
            history =
                historyHumidity;
            break;

        case 'P':
            history =
                historyPressure;
            break;

        case 'G':
            history =
                historyGas;
            break;

        case 'I':
            history =
                historyIAQ;
            break;
    }


    /* --------------------------------------------------------
     * Current value
     * -------------------------------------------------------- */

    float currentValue = 0.0;


    switch (historyMode) {

        case 'T':
            currentValue =
                displayTemperature;
            break;

        case 'H':
            currentValue =
                displayHumidity;
            break;

        case 'P':
            currentValue =
                displayPressure;
            break;

        case 'G':
            currentValue =
                displayGasResistance;
            break;

        case 'I':
            currentValue =
                displayIAQ;
            break;
    }


    /* --------------------------------------------------------
     * Number of samples
     * -------------------------------------------------------- */

    uint16_t count;


    if (historyFilled) {

        count =
            HISTORY_POINTS;

    } else {

        count =
            historyIndex;
    }


    /* --------------------------------------------------------
     * First sample should exist
     * -------------------------------------------------------- */

    if (count == 0) {

        M5Cardputer.Display.setCursor(
            25,
            60
        );

        M5Cardputer.Display.setTextSize(2);

        M5Cardputer.Display.print(
            "WAITING FOR DATA"
        );

        return;
    }


    /* --------------------------------------------------------
     * Find min / max
     * -------------------------------------------------------- */

    float minValue =
        currentValue;

    float maxValue =
        currentValue;


    for (
        uint16_t i = 0;
        i < count;
        i++
    ) {

        float v =
            history[i];


        if (v < minValue)
            minValue = v;


        if (v > maxValue)
            maxValue = v;
    }


    /* --------------------------------------------------------
     * Avoid zero-height graph
     * -------------------------------------------------------- */

    if (
        maxValue - minValue <
        0.01
    ) {

        maxValue += 1.0;

        minValue -= 1.0;
    }


    /* --------------------------------------------------------
     * Add 10% margin
     * -------------------------------------------------------- */

    float margin =
        (maxValue - minValue) *
        0.10;


    maxValue += margin;

    minValue -= margin;


    /* --------------------------------------------------------
     * Graph border
     * -------------------------------------------------------- */

    M5Cardputer.Display.drawRect(
        graphX,
        graphY,
        graphW,
        graphH,
        DARKGREY
    );


    /* --------------------------------------------------------
     * Center grid line
     * -------------------------------------------------------- */

    M5Cardputer.Display.drawLine(
        graphX,
        graphY + graphH / 2,
        graphX + graphW,
        graphY + graphH / 2,
        DARKGREY
    );


    /* --------------------------------------------------------
     * Y-axis MAX
     * -------------------------------------------------------- */

    M5Cardputer.Display.setCursor(
        1,
        graphY - 2
    );


    if (
        historyMode == 'G'
    ) {

        M5Cardputer.Display.printf(
            "%.1fk",
            maxValue / 1000.0
        );

    } else {

        M5Cardputer.Display.printf(
            "%.1f",
            maxValue
        );
    }


    /* --------------------------------------------------------
     * Y-axis MIN
     * -------------------------------------------------------- */

    M5Cardputer.Display.setCursor(
        1,
        graphY + graphH - 8
    );


    if (
        historyMode == 'G'
    ) {

        M5Cardputer.Display.printf(
            "%.1fk",
            minValue / 1000.0
        );

    } else {

        M5Cardputer.Display.printf(
            "%.1f",
            minValue
        );
    }


    /* --------------------------------------------------------
     * Draw graph
     * -------------------------------------------------------- */

    if (count > 1) {

        int previousX =
            graphX + 1;

        int previousY =
            graphY +
            graphH / 2;


        for (
            uint16_t x = 0;
            x < graphW - 2;
            x++
        ) {

            uint16_t sample;


            if (historyFilled) {

                uint16_t start =
                    historyIndex;


                sample =
                    (
                        start +
                        (
                            (uint32_t)x *
                            (HISTORY_POINTS - 1) /
                            (graphW - 2)
                        )
                    )
                    % HISTORY_POINTS;

            } else {

                sample =
                    (
                        (uint32_t)x *
                        (count - 1) /
                        (graphW - 2)
                    );
            }


            float value =
                history[sample];


            int y =
                graphY +
                graphH -
                2 -
                (int)(
                    (value - minValue) /
                    (maxValue - minValue) *
                    (graphH - 4)
                );


            int px =
                graphX +
                1 +
                x;


            if (x > 0) {

                M5Cardputer.Display.drawLine(
                    previousX,
                    previousY,
                    px,
                    y,
                    WHITE
                );
            }


            previousX = px;

            previousY = y;
        }
    }

/* --------------------------------------------------------
 * X-axis: elapsed time from oldest point to now
 * -------------------------------------------------------- */

/* uint32_t spanSeconds = */
/*     (uint32_t)(count - 1) * */
/*     (HISTORY_INTERVAL / 1000UL); */

		uint32_t spanSeconds =
			(uint32_t)(count - 1) *
			(historyInterval / 1000UL);
		
uint32_t ages[5] = {
    spanSeconds,
    spanSeconds * 3 / 4,
    spanSeconds / 2,
    spanSeconds / 4,
    0
};

int xPositions[5] = {
    graphX,
    graphX + graphW / 4,
    graphX + graphW / 2,
    graphX + (graphW * 3) / 4,
    graphX + graphW
};

char label[8];

for (uint8_t i = 0; i < 5; i++) {
    formatHistoryAge(label, sizeof(label), ages[i]);

    int16_t labelWidth =
        M5Cardputer.Display.textWidth(label);

    /* M5Cardputer.Display.setCursor( */
    /*     xPositions[i] - labelWidth / 2, */
    /*     108 */
    /* ); */

		int labelX;

		if (i == 0) {
			labelX = graphX;                         // Left-align first label
		} else if (i == 4) {
			labelX = graphX + graphW - labelWidth;  // Right-align "0m"
		} else {
			labelX = xPositions[i] - labelWidth / 2;
		}

		M5Cardputer.Display.setCursor(labelX, 108);
 
    M5Cardputer.Display.print(label);
}
 
    /* /\* -------------------------------------------------------- */
    /*  * X-axis */
    /*  * -------------------------------------------------------- *\/ */

    /* M5Cardputer.Display.setCursor( */
    /*     graphX, */
    /*     108 */
    /* ); */

    /* M5Cardputer.Display.print( */
    /*     "24h" */
    /* ); */


    /* M5Cardputer.Display.setCursor( */
    /*     graphX + graphW / 4 - 8, */
    /*     108 */
    /* ); */

    /* M5Cardputer.Display.print( */
    /*     "18h" */
    /* ); */


    /* M5Cardputer.Display.setCursor( */
    /*     graphX + graphW / 2 - 8, */
    /*     108 */
    /* ); */

    /* M5Cardputer.Display.print( */
    /*     "12h" */
    /* ); */


    /* M5Cardputer.Display.setCursor( */
    /*     graphX + (graphW * 3) / 4 - 8, */
    /*     108 */
    /* ); */

    /* M5Cardputer.Display.print( */
    /*     "6h" */
    /* ); */


    /* M5Cardputer.Display.setCursor( */
    /*     graphX + graphW - 12, */
    /*     108 */
    /* ); */

    /* M5Cardputer.Display.print( */
    /*     "0h" */
    /* ); */


    /* --------------------------------------------------------
     * Bottom controls
     * -------------------------------------------------------- */

/*     M5Cardputer.Display.setCursor( */
/*         graphX, */
/*         122 */
/*     ); */

/* M5Cardputer.Display.printf( */
/*     "T-TMP H-HMD P-PRS G-GAS I-IAQ R-%lus",    historyInterval / 1000UL); */
M5Cardputer.Display.setTextSize(1);
 
char bottomText[48];

snprintf(
    bottomText,
    sizeof(bottomText),
    "T-TMP H-HMD P-PRS G-GAS I-IAQ R-%luS",
    historyInterval / 1000UL
);

int16_t bottomTextWidth =
    M5Cardputer.Display.textWidth(bottomText);

M5Cardputer.Display.setCursor(
    (M5Cardputer.Display.width() - bottomTextWidth) / 2,
    122
);

M5Cardputer.Display.print(bottomText);
 
}


/* ============================================================
 * PAGE 4
 *
 * CONTROL
 * ============================================================ */

void drawControl()
{
    drawHeader("CTR");

    M5Cardputer.Display.setTextSize(2);


    M5Cardputer.Display.setCursor(
        5,
        34
    );

    M5Cardputer.Display.print(
        "1 ENV PRO"
    );


    M5Cardputer.Display.setCursor(
        5,
        58
    );

    M5Cardputer.Display.print(
        "2 MIN/MAX"
    );


    M5Cardputer.Display.setCursor(
        5,
        82
    );

    M5Cardputer.Display.print(
        "3 HISTORY"
    );


    M5Cardputer.Display.setCursor(
        5,
        106
    );

    M5Cardputer.Display.print(
        "N NXT  B BRT"
    );
}


/* ============================================================
 * DRAW PAGE
 * ============================================================ */

void drawPage()
{
    switch (page) {

        case 1:

            drawEnvironment();

            break;


        case 2:

            drawMinMax();

            break;


        case 3:

            drawHistory();

            break;


        case 4:

            drawControl();

            break;
    }
}


/* ============================================================
 * HISTORY MODE CYCLE
 * ============================================================ */

void cycleHistoryMode()
{
    switch (historyMode) {

        case 'T':

            historyMode = 'H';

            break;


        case 'H':

            historyMode = 'P';

            break;


        case 'P':

            historyMode = 'G';

            break;


        case 'G':

            historyMode = 'I';

            break;


        case 'I':

            historyMode = 'T';

            break;
    }


    page = 3;

    drawPage();
}


/* ============================================================
 * SCREENSHOT
 *
 * Saves the current Cardputer display as a BMP file
 * to the microSD card.
 *
 * File:
 *   /screenshot.bmp
 *
 * Each new screenshot overwrites the previous one.
 * ============================================================ */

void saveScreenshot()
{
    const int width  = M5Cardputer.Display.width();
    const int height = M5Cardputer.Display.height();

    Serial.println("SCREENSHOT: starting...");

    /* --------------------------------------------------------
     * Check SD card
     * -------------------------------------------------------- */

    if (!SD.begin()) {

        Serial.println("SCREENSHOT: SD init failed");

        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(10, 45);
        M5Cardputer.Display.println("SD ERROR");

        delay(1000);

        drawPage();

        return;
    }

    /* --------------------------------------------------------
     * Find next available filename
     * -------------------------------------------------------- */

    char filename[20];

    int shotNumber = 1;

    do {

        snprintf(
            filename,
            sizeof(filename),
            "/shot%03d.bmp",
            shotNumber
        );

        shotNumber++;

    } while (
        SD.exists(filename) &&
        shotNumber < 1000
    );

    /* --------------------------------------------------------
     * Open file
     * -------------------------------------------------------- */

    File file = SD.open(
        filename,
        FILE_WRITE
    );

    if (!file) {

        Serial.println(
            "SCREENSHOT: cannot open file"
        );

        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(10, 45);
        M5Cardputer.Display.println("FILE ERROR");

        delay(1000);

        drawPage();

        return;
    }

    /* --------------------------------------------------------
     * BMP information
     * -------------------------------------------------------- */

    const uint32_t rowSize =
        (width * 3 + 3) & ~3;

    const uint32_t imageSize =
        rowSize * height;

    const uint32_t fileSize =
        54 + imageSize;

    /* --------------------------------------------------------
     * BMP HEADER
     * -------------------------------------------------------- */

    uint8_t header[54] = {0};

    header[0] = 'B';
    header[1] = 'M';

    header[2] = fileSize;
    header[3] = fileSize >> 8;
    header[4] = fileSize >> 16;
    header[5] = fileSize >> 24;

    header[10] = 54;

    header[14] = 40;

    header[18] = width;
    header[19] = width >> 8;
    header[20] = width >> 16;
    header[21] = width >> 24;

    header[22] = height;
    header[23] = height >> 8;
    header[24] = height >> 16;
    header[25] = height >> 24;

    header[26] = 1;

    header[28] = 24;

    header[34] = imageSize;
    header[35] = imageSize >> 8;
    header[36] = imageSize >> 16;
    header[37] = imageSize >> 24;

    file.write(
        header,
        sizeof(header)
    );

    /* --------------------------------------------------------
     * Pixel buffer
     * -------------------------------------------------------- */

    uint16_t *lineBuffer =
        new uint16_t[width];

    if (!lineBuffer) {

        Serial.println(
            "SCREENSHOT: memory allocation failed"
        );

        file.close();

        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(10, 45);
        M5Cardputer.Display.println("MEM ERROR");

        delay(1000);

        drawPage();

        return;
    }

    uint8_t padding[3] = {
        0,
        0,
        0
    };

    uint8_t padBytes =
        rowSize - width * 3;

    /* --------------------------------------------------------
     * Write pixels
     * -------------------------------------------------------- */

    for (
        int y = height - 1;
        y >= 0;
        y--
    ) {

        M5Cardputer.Display.readRect(
            0,
            y,
            width,
            1,
            lineBuffer
        );

        for (
            int x = 0;
            x < width;
            x++
        ) {

            uint16_t pixel =
                lineBuffer[x];

            uint8_t r =
                ((pixel >> 11) & 0x1F)
                * 255 / 31;

            uint8_t g =
                ((pixel >> 5) & 0x3F)
                * 255 / 63;

            uint8_t b =
                (pixel & 0x1F)
                * 255 / 31;

            /* BMP uses BGR */

            file.write(b);
            file.write(g);
            file.write(r);
        }

        if (padBytes > 0) {

            file.write(
                padding,
                padBytes
            );
        }
    }

    delete[] lineBuffer;

    file.close();

    /* --------------------------------------------------------
     * Serial
     * -------------------------------------------------------- */

    Serial.printf(
        "SCREENSHOT: saved %s\n",
        filename
    );

    /* --------------------------------------------------------
     * Temporary Cardputer message
     * -------------------------------------------------------- */

    M5Cardputer.Display.fillScreen(BLACK);

    M5Cardputer.Display.setTextColor(WHITE);

    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(
        15,
        35
    );

    M5Cardputer.Display.println(
        "SCREENSHOT"
    );

    M5Cardputer.Display.setCursor(
        15,
        60
    );

    M5Cardputer.Display.println(
        "SAVED"
    );

    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(
        15,
        90
    );

    M5Cardputer.Display.println(
        filename
    );

    /* --------------------------------------------------------
     * Show for 1 second
     * -------------------------------------------------------- */

    delay(1000);

    /* --------------------------------------------------------
     * Return to previous page
     * -------------------------------------------------------- */

    drawPage();
}

/* ============================================================
 * KEYBOARD
 * ============================================================ */

void handleKeyboard()
{
    M5Cardputer.update();


    if (
        !M5Cardputer.Keyboard.isChange()
    ) {
        return;
    }


    if (
        !M5Cardputer.Keyboard.isPressed()
    ) {
        return;
    }


    auto keys =
        M5Cardputer.Keyboard.keysState();

    /* ------------------------------------------------
     * SCREENSHOT
     *
     * Fn + S
     * ------------------------------------------------ */

    if (keys.fn) {

        for (auto key : keys.word) {

            if (key == 's' || key == 'S') {

                saveScreenshot();

                return;
            }
        }
    }


    for (
        auto key : keys.word
    ) {

        switch (key) {


            /* ------------------------------------------------
             * PAGE 1
             * ------------------------------------------------ */

            case '1':

                page = 1;

                drawPage();

                break;


            /* ------------------------------------------------
             * PAGE 2
             * ------------------------------------------------ */

            case '2':

                page = 2;

                drawPage();

                break;


            /* ------------------------------------------------
             * PAGE 3
             *
             * Press 3 while already on page 3
             * to cycle history mode.
             * ------------------------------------------------ */

            case '3':

                if (page == 3) {

                    cycleHistoryMode();

                } else {

                    page = 3;

                    drawPage();
                }

                break;


            /* ------------------------------------------------
             * PAGE 4
             * ------------------------------------------------ */

            case '4':

                page = 4;

                drawPage();

                break;


            /* ------------------------------------------------
             * NEXT
             * ------------------------------------------------ */

            case 'n':
            case 'N':

                page++;

                if (page > 4) {
                    page = 1;
                }

                drawPage();

                break;


            /* ------------------------------------------------
             * BRIGHTNESS
             * ------------------------------------------------ */

            case 'b':
            case 'B':

                cycleBrightness();

                break;


            /* ------------------------------------------------
             * HISTORY TEMPERATURE
             * ------------------------------------------------ */

            case 't':
            case 'T':

                historyMode = 'T';

                page = 3;

                drawPage();

                break;


            /* ------------------------------------------------
             * HISTORY HUMIDITY
             * ------------------------------------------------ */

            case 'h':
            case 'H':

                historyMode = 'H';

                page = 3;

                drawPage();

                break;


            /* ------------------------------------------------
             * HISTORY PRESSURE
             * ------------------------------------------------ */

            case 'p':
            case 'P':

                historyMode = 'P';

                page = 3;

                drawPage();

                break;


            /* ------------------------------------------------
             * HISTORY GAS
             * ------------------------------------------------ */

            case 'g':
            case 'G':

                historyMode = 'G';

                page = 3;

                drawPage();

                break;


            /* ------------------------------------------------
             * HISTORY IAQ
             * ------------------------------------------------ */

            case 'i':
            case 'I':

                historyMode = 'I';

                page = 3;

                drawPage();

                break;

				case 'r':
				case 'R':
					cycleHistoryInterval();
					break;
		
        }
    }
}


/* ============================================================
 * ESP-NOW RECEIVE CALLBACK
 *
 * DO NOT CHANGE CALLBACK SIGNATURE
 * ============================================================ */

void onDataReceive(
    const esp_now_recv_info_t *info,
    const uint8_t *data,
    int len
)
{
    if (
        len != sizeof(sensorPayload)
    ) {

        Serial.printf(
            "ESP-NOW: wrong packet size %d / %u\n",
            len,
            sizeof(sensorPayload)
        );

        return;
    }


    memcpy(
        &sensor,
        data,
        sizeof(sensorPayload)
    );


    /*
     * Do not do display work here.
     *
     * Just mark that new data arrived.
     */

    newData = true;

    receivedOnce = true;

    lastReceive = millis();
}


/* ============================================================
 * SETUP
 * ============================================================ */

void setup()
{
    /* --------------------------------------------------------
     * Cardputer
     * -------------------------------------------------------- */

    M5Cardputer.begin();

/* --------------------------------------------------------
 * microSD
 * -------------------------------------------------------- */

if (SD.begin()) {

    Serial.println(
        "SD card ready"
    );

} else {

    Serial.println(
        "SD card not detected"
    );
}
 
		
    M5Cardputer.Display.setRotation(1);


    /* --------------------------------------------------------
     * Brightness
     * -------------------------------------------------------- */

    M5Cardputer.Display.setBrightness(
        brightnessLevels[
            currentBrightnessIndex
        ]
    );


    /* --------------------------------------------------------
     * Serial
     * -------------------------------------------------------- */

    Serial.begin(115200);

    delay(100);


    /* --------------------------------------------------------
     * Wi-Fi / ESP-NOW
     * -------------------------------------------------------- */

    WiFi.mode(WIFI_STA);


    Serial.print(
        "Cardputer MAC: "
    );

    Serial.println(
        WiFi.macAddress()
    );


    /* --------------------------------------------------------
     * ESP-NOW
     * -------------------------------------------------------- */

    if (
        esp_now_init() != ESP_OK
    ) {

        Serial.println(
            "ESP-NOW init failed"
        );


        M5Cardputer.Display.fillScreen(
            BLACK
        );

        M5Cardputer.Display.setTextColor(
            RED
        );

        M5Cardputer.Display.setTextSize(
            2
        );

        M5Cardputer.Display.setCursor(
            5,
            30
        );

        M5Cardputer.Display.println(
            "ESP-NOW ERROR"
        );

        return;
    }


    esp_now_register_recv_cb(
        onDataReceive
    );


    /* --------------------------------------------------------
     * Clear history
     *
     * Not strictly necessary, but useful for clean startup.
     * -------------------------------------------------------- */

    historyIndex = 0;

    historyFilled = false;

    historyStarted = false;


    /* --------------------------------------------------------
     * Initial screen
     * -------------------------------------------------------- */

    M5Cardputer.Display.fillScreen(
        BLACK
    );

    M5Cardputer.Display.setTextColor(
        WHITE
    );

    M5Cardputer.Display.setTextSize(
        2
    );


    M5Cardputer.Display.setCursor(
        5,
        5
    );

    M5Cardputer.Display.println(
        "ENV PRO"
    );


    M5Cardputer.Display.setCursor(
        5,
        40
    );

    M5Cardputer.Display.println(
        "Waiting for NanoC6"
    );


    M5Cardputer.Display.setCursor(
        5,
        65
    );

    M5Cardputer.Display.println(
        "ESP-NOW ready"
    );


    Serial.println(
        "ESP-NOW receiver ready"
    );
}


/* ============================================================
 * LOOP
 * ============================================================ */

void loop()
{
    /* --------------------------------------------------------
     * Keyboard
     * -------------------------------------------------------- */

    handleKeyboard();


    /* --------------------------------------------------------
     * New ESP-NOW data
     * -------------------------------------------------------- */

    if (newData) {

        /*
         * Copy packet data to normal variables.
         */

        newData = false;


        updateDisplayValues();
				updateHistoryOnReceive();

        /*
         * Redraw current page with new data.
         */
        drawPage();


        Serial.printf(
            "ESP-NOW ENV: "
            "%.1f C | "
            "%.1f %% | "
            "%.1f hPa | "
            "IAQ %.1f | "
            "Gas %.0f Ohm\n",

            displayTemperature,

            displayHumidity,

            displayPressure,

            displayIAQ,

            displayGasResistance
        );


        Serial.printf(
            "NanoC6 MIN/MAX: "
            "T %.1f / %.1f | "
            "H %.1f / %.1f | "
            "P %.1f / %.1f | "
            "G %.0f / %.0f | "
            "IAQ %.1f / %.1f\n",

            temperatureMin,
            temperatureMax,

            humidityMin,
            humidityMax,

            pressureMin,
            pressureMax,

            gasMin,
            gasMax,

            iaqMin,
            iaqMax
        );
    }


    /* --------------------------------------------------------
     * History timer
     *
     * This does NOT create a sample until 60 seconds
     * have elapsed since the previous history point.
     *
     * It uses the latest received ESP-NOW values.
     * -------------------------------------------------------- */

		//    updateHistory();
    delay(10);
}
