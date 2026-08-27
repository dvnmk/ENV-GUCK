#include <M5Cardputer.h>
#include <Wire.h>
#include <bsec2.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

/* ============================================================
 * Brightness
 * ============================================================ */

uint8_t brightnessLevels[5] = {0, 30, 100, 200, 255};
uint8_t currentBrightnessIndex = 1;

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
 * BSEC2
 * ============================================================ */

Bsec2 envSensor;


/**
 * @brief Check BSEC status
 */
void checkBsecStatus(Bsec2 bsec);


/**
 * @brief BSEC output callback
 */
void newDataCallback(
    const bme68xData data,
    const bsecOutputs outputs,
    Bsec2 bsec
);


/* ============================================================
 * Display data
 *
 * These variables mirror the BSEC output for the UI.
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
 * 24H MIN / MAX
 * ============================================================ */

float temperatureMin = 999.0;
float temperatureMax = -999.0;

float humidityMin = 999.0;
float humidityMax = -999.0;

float pressureMin = 999999.0;
float pressureMax = -999999.0;

float gasMin = 999999999.0;
float gasMax = -999999999.0;

float iaqMin = 999999.0;
float iaqMax = -999999.0;


/*
 * Start time of current 24-hour period.
 *
 * This is a 24-hour MIN/MAX period since startup.
 * It is not a rolling 24-hour history.
 */

unsigned long minMaxStart = 0;

const unsigned long MINMAX_PERIOD =
    24UL * 60UL * 60UL * 1000UL;


/* ============================================================
 * History
 *
 * 1440 points = 24 hours at 1 sample/minute
 * ============================================================ */

#define HISTORY_POINTS 1440

float historyTemperature[HISTORY_POINTS];
float historyHumidity[HISTORY_POINTS];
float historyPressure[HISTORY_POINTS];
float historyGas[HISTORY_POINTS];
float historyIAQ[HISTORY_POINTS];

uint16_t historyIndex = 0;
bool historyFilled = false;

unsigned long lastHistorySample = 0;

const unsigned long HISTORY_INTERVAL = 60000UL;

char historyMode = 'T';


/* ============================================================
 * Page
 * ============================================================ */

uint8_t page = 1;


/* ============================================================
 * New data flag
 * ============================================================ */

volatile bool newData = false;


/* ============================================================
 * Battery
 * ============================================================ */

int getBatteryPercent()
{
    return M5Cardputer.Power.getBatteryLevel();
}


/* ============================================================
 * Update 24H MIN / MAX
 * ============================================================ */

void updateMinMax()
{
    if (displayTemperature < temperatureMin)
        temperatureMin = displayTemperature;

    if (displayTemperature > temperatureMax)
        temperatureMax = displayTemperature;


    if (displayHumidity < humidityMin)
        humidityMin = displayHumidity;

    if (displayHumidity > humidityMax)
        humidityMax = displayHumidity;


    if (displayPressure < pressureMin)
        pressureMin = displayPressure;

    if (displayPressure > pressureMax)
        pressureMax = displayPressure;


    if (displayGasResistance < gasMin)
        gasMin = displayGasResistance;

    if (displayGasResistance > gasMax)
        gasMax = displayGasResistance;


    if (displayIAQ < iaqMin)
        iaqMin = displayIAQ;

    if (displayIAQ > iaqMax)
        iaqMax = displayIAQ;
}


/* ============================================================
 * Reset MIN / MAX
 * ============================================================ */

void resetMinMax()
{
    temperatureMin = 999.0;
    temperatureMax = -999.0;

    humidityMin = 999.0;
    humidityMax = -999.0;

    pressureMin = 999999.0;
    pressureMax = -999999.0;

    gasMin = 999999999.0;
    gasMax = -999999999.0;

    iaqMin = 999999.0;
    iaqMax = -999999.0;

    minMaxStart = millis();
}


/* ============================================================
 * History sampling
 * ============================================================ */

void updateHistory()
{
    unsigned long now = millis();

    if (now - lastHistorySample < HISTORY_INTERVAL)
        return;

    lastHistorySample = now;


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


/* ============================================================
 * Header
 * ============================================================ */

void drawHeader(const char *title)
{
    M5Cardputer.Display.fillScreen(BLACK);

    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(5, 4);

    M5Cardputer.Display.print(title);

    /* Page indicator */

    /* M5Cardputer.Display.setCursor(170, 4); */
		/* M5Cardputer.Display.printf("#%d", page); */
char pageText[4];
sprintf(pageText, "#%d", page);

int16_t textWidth = M5Cardputer.Display.textWidth(pageText);

M5Cardputer.Display.setCursor(
    235 - textWidth,
    4
);

M5Cardputer.Display.print(pageText);
 
		
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
 * Page 1
 *
 * ENV PRO
 * ============================================================ */

void drawEnvironment()
{
    drawHeader("ENV");

    M5Cardputer.Display.setTextSize(2);

		M5Cardputer.Display.setCursor(55, 4);
    M5Cardputer.Display.printf(        "BAT %3d%%",        getBatteryPercent()    );

    /* Temperature */

    M5Cardputer.Display.setCursor(5, 34);

    M5Cardputer.Display.printf(
        "TMP %6.1f C",
        displayTemperature
    );


    /* Humidity */

    M5Cardputer.Display.setCursor(5, 54);

    M5Cardputer.Display.printf(
        "HMD %6.1f %%",
        displayHumidity
    );


    /* Pressure */

    M5Cardputer.Display.setCursor(5, 74);

    M5Cardputer.Display.printf(
        "PRS %6.1f hPa",
        displayPressure
    );


    /* Gas */

    M5Cardputer.Display.setCursor(5, 94);

    if (displayGasResistance >= 1000.0) {

        M5Cardputer.Display.printf(
            "GAS %6.1f kOhm",
            displayGasResistance / 1000.0
        );

    } else {

        M5Cardputer.Display.printf(
            "GAS  %6.0f",
            displayGasResistance
        );
    }


    /* IAQ */

    M5Cardputer.Display.setCursor(5, 114);

    M5Cardputer.Display.printf(
        "IAQ %6.1f ASR %d%d%d",
        displayIAQ,
        displayIAQAccuracy,
        displayStabilization,
        displayRunIn
    );
}


/* ============================================================
 * Page 2
 *
 * 24H MIN / MAX
 * ============================================================ */

void drawMinMax()
{
    drawHeader("24HR MIN/MAX");

    M5Cardputer.Display.setTextSize(2);


    /* Temperature */

    M5Cardputer.Display.setCursor(5, 34);

    M5Cardputer.Display.printf(
        "TMP %6.1f  %6.1f",
        temperatureMin,
        temperatureMax
    );


    /* Humidity */

    M5Cardputer.Display.setCursor(5, 54);

    M5Cardputer.Display.printf(
        "HMD %6.1f  %6.1f",
        humidityMin,
        humidityMax
    );


    /* Pressure */
    M5Cardputer.Display.setCursor(5, 74);
    M5Cardputer.Display.printf(
        "PRS %6.1f  %6.1f",
        pressureMin,
        pressureMax
    );


    /* Gas */
    M5Cardputer.Display.setCursor(5, 94);
    M5Cardputer.Display.printf(
        "GAS %6.1f  %6.1f",
        gasMin / 1000.0,
        gasMax / 1000.0
    );


    /* IAQ */
    M5Cardputer.Display.setCursor(5, 114);
    M5Cardputer.Display.printf(
        "IAQ %6.1f  %6.1f",
        iaqMin,
        iaqMax
    );
}


/* ============================================================
 * Page 3
 * HISTORY GRAPH
 * ============================================================ */

void drawHistory()
{
    // ---------------------------------------------------------
    // Header
    // ---------------------------------------------------------

    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(5, 4);

    switch (historyMode) {

        case 'T':
            M5Cardputer.Display.printf(
                "TEMP %.1f C",
                displayTemperature
            );
            break;

        case 'H':
            M5Cardputer.Display.printf(
                "HUM %.1f %%",
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
                "IAQ %.1f Ohm",
                displayIAQ
            );
            break;
    }

    // Page indicator
		char pageText[4];
		sprintf(pageText, "#%d", page);

		int16_t textWidth = M5Cardputer.Display.textWidth(pageText);

		M5Cardputer.Display.setCursor(
																	235 - textWidth,
																	4
																 );

M5Cardputer.Display.print(pageText);
    M5Cardputer.Display.drawLine(
        5, 27, 235, 27, DARKGREY
    );


    // ---------------------------------------------------------
    // Graph
    // ---------------------------------------------------------

    M5Cardputer.Display.setTextSize(1);

    const int graphX = 28;
    const int graphY = 35;
    const int graphW = 207;
    const int graphH = 70;

    float *history = nullptr;


    // Select history array

    switch (historyMode) {

        case 'T':
            history = historyTemperature;
            break;

        case 'H':
            history = historyHumidity;
            break;

        case 'P':
            history = historyPressure;
            break;

        case 'G':
            history = historyGas;
            break;

        case 'I':
            history = historyIAQ;
            break;
    }


    // ---------------------------------------------------------
    // Current value
    // ---------------------------------------------------------

    float currentValue = 0;

    switch (historyMode) {

        case 'T':
            currentValue = displayTemperature;
            break;

        case 'H':
            currentValue = displayHumidity;
            break;

        case 'P':
            currentValue = displayPressure;
            break;

        case 'G':
            currentValue = displayGasResistance;
            break;

        case 'I':
            currentValue = displayIAQ;
            break;
    }


    // ---------------------------------------------------------
    // Number of available samples
    // ---------------------------------------------------------

    uint16_t count;

    if (historyFilled)
        count = HISTORY_POINTS;
    else
        count = historyIndex;


    // ---------------------------------------------------------
    // Find min / max
    // ---------------------------------------------------------

    float minValue = currentValue;
    float maxValue = currentValue;

    for (uint16_t i = 0; i < count; i++) {

        float v = history[i];

        if (v < minValue)
            minValue = v;

        if (v > maxValue)
            maxValue = v;
    }


    // ---------------------------------------------------------
    // Avoid zero-height graph
    // ---------------------------------------------------------

    if (maxValue - minValue < 0.01) {

        maxValue += 1.0;
        minValue -= 1.0;
    }


    // ---------------------------------------------------------
    // Add 10% margin
    // ---------------------------------------------------------

    float margin =
        (maxValue - minValue) * 0.10;

    maxValue += margin;
    minValue -= margin;


    // ---------------------------------------------------------
    // Graph border
    // ---------------------------------------------------------

    M5Cardputer.Display.drawRect(
        graphX,
        graphY,
        graphW,
        graphH,
        DARKGREY
    );


    // ---------------------------------------------------------
    // Center grid line
    // ---------------------------------------------------------

    M5Cardputer.Display.drawLine(
        graphX,
        graphY + graphH / 2,
        graphX + graphW,
        graphY + graphH / 2,
        DARKGREY
    );


    // ---------------------------------------------------------
    // Y-axis MAX
    // ---------------------------------------------------------

    M5Cardputer.Display.setCursor(
        1,
        graphY - 2
    );

    if (historyMode == 'G') {

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


    // ---------------------------------------------------------
    // Y-axis MIN
    // ---------------------------------------------------------

    M5Cardputer.Display.setCursor(
        1,
        graphY + graphH - 8
    );

    if (historyMode == 'G') {

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


    // ---------------------------------------------------------
    // Draw graph
    // ---------------------------------------------------------

    if (count > 1) {

        int previousX =
            graphX + 1;

        int previousY =
            graphY + graphH / 2;


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
                graphX + 1 + x;


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


    // ---------------------------------------------------------
    // X-axis time
    //
    // 1440 samples = 24 hours
    // 1 sample = 1 minute
    // ---------------------------------------------------------

    M5Cardputer.Display.setCursor(
        graphX,
        108
    );

    M5Cardputer.Display.print("24h");


    M5Cardputer.Display.setCursor(
        graphX + graphW / 4 - 8,
        108
    );

    M5Cardputer.Display.print("18h");


    M5Cardputer.Display.setCursor(
        graphX + graphW / 2 - 8,
        108
    );

    M5Cardputer.Display.print("12h");


    M5Cardputer.Display.setCursor(
        graphX + (graphW * 3) / 4 - 8,
        108
    );

    M5Cardputer.Display.print("6h");


    M5Cardputer.Display.setCursor(
        graphX + graphW - 12,
        108
    );

    M5Cardputer.Display.print("0");


    // ---------------------------------------------------------
    // Bottom controls
    // ---------------------------------------------------------

    M5Cardputer.Display.setCursor(
        5,
        118
    );

    M5Cardputer.Display.print(
        "T TMP H HUM P PRS G GAS I IAQ"
    );
}



/* ============================================================
 * Page 4
 *
 * CONTROL
 * ============================================================ */

void drawControl()
{
    drawHeader("CTR");

    M5Cardputer.Display.setTextSize(2);

    M5Cardputer.Display.setCursor(5, 34);
    M5Cardputer.Display.print("1 ENV PRO");

    M5Cardputer.Display.setCursor(5, 58);
    M5Cardputer.Display.print("2 MIN/MAX");

    M5Cardputer.Display.setCursor(5, 82);
    M5Cardputer.Display.print("3 HISTORY");

    M5Cardputer.Display.setCursor(5, 106);
    M5Cardputer.Display.print("N NEXT  B BRT");
}

/* ============================================================
 * Draw current page
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
 * Keyboard
 * ============================================================ */

void handleKeyboard()
{
    M5Cardputer.update();


    if (!M5Cardputer.Keyboard.isChange())
        return;


    if (!M5Cardputer.Keyboard.isPressed())
        return;


    auto keys =
        M5Cardputer.Keyboard.keysState();


    for (auto key : keys.word) {

        switch (key) {


            /* Page 1 */

            case '1':

                page = 1;

                drawPage();

                break;


            /* Page 2 */

            case '2':

                page = 2;

                drawPage();

                break;


            /* Page 3 */
case '3':

    if (page == 3) {

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

        drawPage();

    } else {

        page = 3;
        drawPage();
    }

    break;

            /* Page 4 */

            case '4':

                page = 4;

                drawPage();

                break;


            /* Next */

            case 'n':
            case 'N':

                page++;

                if (page > 4)
                    page = 1;

                drawPage();

                break;


            /* Brightness */

            case 'b':
            case 'B':

                cycleBrightness();

                break;


            /* History temperature */

				case 't':
				case 'T':

					historyMode = 'T';
					page = 3;
					drawPage();

					break;

            /* History humidity */

				case 'h':
				case 'H':

					historyMode = 'H';
					page = 3;
					drawPage();

					break;

            /* History pressure */

            case 'p':
            case 'P':
					historyMode = 'P';
					page = 3;
					drawPage();
                break;


            /* History gas */

            case 'g':
            case 'G':
					historyMode = 'G';
					page = 3;
					drawPage();
                break;


            /* History IAQ */

            case 'i':
            case 'I':
					historyMode = 'I';
					page = 3;
					drawPage();
                break;
        }
    }
}


/* ============================================================
 * Setup
 * ============================================================ */

void setup(void)
{
    /* Cardputer */

    M5Cardputer.begin();

    M5Cardputer.Display.setRotation(1);


    /* Initial brightness */

    M5Cardputer.Display.setBrightness(
        brightnessLevels[currentBrightnessIndex]
    );


    /* Serial */

    Serial.begin(115200);


    /* --------------------------------------------------------
     * Desired subscription list of BSEC2 outputs
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    bsecSensor sensorList[] = {

        BSEC_OUTPUT_IAQ,

        BSEC_OUTPUT_RAW_TEMPERATURE,

        BSEC_OUTPUT_RAW_PRESSURE,

        BSEC_OUTPUT_RAW_HUMIDITY,

        BSEC_OUTPUT_RAW_GAS,

        BSEC_OUTPUT_STABILIZATION_STATUS,

        BSEC_OUTPUT_RUN_IN_STATUS
    };


    /* --------------------------------------------------------
     * Initialize communication
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    Wire.begin(2, 1);


    /* --------------------------------------------------------
     * Initialize BSEC
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    if (!envSensor.begin(
        BME68X_I2C_ADDR_HIGH,
        Wire
    )) {

        checkBsecStatus(envSensor);
    }


    /* --------------------------------------------------------
     * Subscribe to BSEC outputs
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    if (!envSensor.updateSubscription(
        sensorList,
        ARRAY_LEN(sensorList),
        BSEC_SAMPLE_RATE_LP
    )) {

        checkBsecStatus(envSensor);
    }


    /* --------------------------------------------------------
     * Callback
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    envSensor.attachCallback(
        newDataCallback
    );


    Serial.println(
        "BSEC library version " +
        String(envSensor.version.major) +
        "." +
        String(envSensor.version.minor) +
        "." +
        String(envSensor.version.major_bugfix) +
        "." +
        String(envSensor.version.minor_bugfix)
    );


    /* Start MIN/MAX period */

    resetMinMax();


    /* Start history timer */

    lastHistorySample = millis();


    /* --------------------------------------------------------
     * Initial screen
     * -------------------------------------------------------- */

    M5Cardputer.Display.fillScreen(
        BLACK
    );

    M5Cardputer.Display.setTextColor(
        WHITE
    );

    M5Cardputer.Display.setTextSize(2);


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
        "Starting BME688..."
    );


    M5Cardputer.Display.setCursor(
        5,
        65
    );

    M5Cardputer.Display.println(
        "BSEC2 ready"
    );
}


/* ============================================================
 * Loop
 * ============================================================ */

void loop(void)
{
    /* --------------------------------------------------------
     * BSEC run
     *
     * UNCHANGED
     * -------------------------------------------------------- */

    if (!envSensor.run()) {

        checkBsecStatus(
            envSensor
        );
    }


    /* History */

    updateHistory();


    /* Keyboard */

    handleKeyboard();


    /* --------------------------------------------------------
     * New sensor data
     * -------------------------------------------------------- */

    if (newData) {

        newData = false;


        updateMinMax();


        drawPage();


        Serial.printf(
            "ENV: %.1f C | %.1f %% | %.1f hPa | IAQ %.1f | Gas %.0f Ohm\n",

            displayTemperature,

            displayHumidity,

            displayPressure,

            displayIAQ,

            displayGasResistance
        );
    }


    /* --------------------------------------------------------
     * Automatic 24-hour reset
     * -------------------------------------------------------- */

    if (
        millis() - minMaxStart >=
        MINMAX_PERIOD
    ) {

        resetMinMax();


        if (page == 2)
            drawPage();
    }


    delay(10);
}


/* ============================================================
 * BSEC callback
 *
 * Original BSEC processing preserved.
 * Only UI mirror variables are updated.
 * ============================================================ */

void newDataCallback(
    const bme68xData data,
    const bsecOutputs outputs,
    Bsec2 bsec
)
{
    if (!outputs.nOutputs) {

        return;
    }


    Serial.println(
        "BSEC outputs:\n\ttimestamp = " +
        String(
            (int)(
                outputs.output[0].time_stamp /
                INT64_C(1000000)
            )
        )
    );


    for (
        uint8_t i = 0;
        i < outputs.nOutputs;
        i++
    ) {

        const bsecData output =
            outputs.output[i];


        switch (output.sensor_id) {


            case BSEC_OUTPUT_IAQ:

                Serial.println(
                    "\tiaq = " +
                    String(output.signal)
                );


                Serial.println(
                    "\tiaq accuracy = " +
                    String(
                        (int)output.accuracy
                    )
                );


                /*
                 * UI copy only.
                 */

                displayIAQ =
                    output.signal;


                displayIAQAccuracy =
                    (uint8_t)output.accuracy;

                break;


            case BSEC_OUTPUT_RAW_TEMPERATURE:

                Serial.println(
                    "\ttemperature = " +
                    String(output.signal)
                );


                displayTemperature =
                    output.signal;

                break;


            case BSEC_OUTPUT_RAW_PRESSURE:

                Serial.println(
                    "\tpressure = " +
                    String(output.signal)
                );


                /*
                 * BSEC gives pressure in Pa.
                 *
                 * UI uses hPa.
                 */

                displayPressure =
                    output.signal;

                break;


            case BSEC_OUTPUT_RAW_HUMIDITY:

                Serial.println(
                    "\thumidity = " +
                    String(output.signal)
                );


                displayHumidity =
                    output.signal;

                break;


            case BSEC_OUTPUT_RAW_GAS:

                Serial.println(
                    "\tgas resistance = " +
                    String(output.signal)
                );


                displayGasResistance =
                    output.signal;

                break;


            case BSEC_OUTPUT_STABILIZATION_STATUS:

                Serial.println(
                    "\tstabilization status = " +
                    String(output.signal)
                );


                displayStabilization =
                    (uint8_t)output.signal;

                break;


            case BSEC_OUTPUT_RUN_IN_STATUS:

                Serial.println(
                    "\trun in status = " +
                    String(output.signal)
                );


                displayRunIn =
                    (uint8_t)output.signal;

                break;


            default:

                break;
        }
    }


    newData = true;
}


/* ============================================================
 * BSEC status
 *
 * ORIGINAL
 * ============================================================ */

void checkBsecStatus(Bsec2 bsec)
{
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
