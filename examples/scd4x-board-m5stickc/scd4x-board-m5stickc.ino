/******************************************************************************/
//#define M5STICKC_PULS_EN      // Enable M5StickC Plus
//#define UPLOAD_DATA_EN        // Enable upload data to Ambient
//#define ALERT_LED_BUZZER_EN   // Enable LED & Buzzer alert

#if defined(UPLOAD_DATA_EN)
// WiFi Access Point
const char* WIFI_SSID   = "********";
const char* WIFI_PASS   = "********";
unsigned int AMB_CH_ID  = 0; // Channel ID for Ambient
const char* AMB_WR_KEY  = "****************"; // Write Key to Channel
#endif /* defined(UPLOAD_DATA_EN) */

const int UPDATE_INTERVAL_S   = 60;  // Update interval seconds (default:60, range:30-3600)
const int LCD_BRIGHTNESS_LV   = 10;   // LCD brightness (default: 10, range:7-12)

const uint16_t ALERT_LV1_CO2_MIN = 0;
const uint16_t ALERT_LV1_CO2_MAX = 1000;
const uint16_t ALERT_LV2_CO2_MIN = 1000;
const uint16_t ALERT_LV2_CO2_MAX = 2000;
const float ALERT_LV1_TEMP_MIN = 22.0;
const float ALERT_LV1_TEMP_MAX = 26.0;
const float ALERT_LV2_TEMP_MIN = 16.0;
const float ALERT_LV2_TEMP_MAX = 30.0;
const float ALERT_LV1_HUMI_MIN = 40.0;
const float ALERT_LV1_HUMI_MAX = 50.0;
const float ALERT_LV2_HUMI_MIN = 20.0;
const float ALERT_LV2_HUMI_MAX = 65.0;

const int ALERT_BUZZER_REPEAT_NUM = 3;  // (default: 3, range:1-10)
/******************************************************************************/

#if defined(M5STICKC_PULS_EN)
#include "M5StickCPlus.h"
#else
#include "M5StickC.h"
#endif

#include <Wire.h>
#if defined(UPLOAD_DATA_EN)
#include <WiFi.h>
#include "Ambient.h"
#endif

#include <SensirionI2CScd4x.h>
SensirionI2CScd4x scd4x;

#if defined(UPLOAD_DATA_EN)
// for Ambient
WiFiClient client;
Ambient ambient;

const int WIFI_CON_WAIT_INTERVAL_MS  = 500;
const int WIFI_CON_WAIT_TIMEOUT_MS   = 180000;
#endif /* defined(UPLOAD_DATA_EN) */

const int SCD4X_READ_MIN_INTERVAL_S  = 30; // wait for measurement
const int SCD4X_FRC_INTERVAL_S       = 300;  // wait for FRC

const int I2C_SCL_PIN = 26;
const int I2C_SDA_PIN = 0;

#if defined(UPLOAD_DATA_EN)
const int CPU_FREQ_MHZ = 80;
#else
const int CPU_FREQ_MHZ = 10;
#endif /* defined(UPLOAD_DATA_EN) */
bool setupCalibMode = false;
uint16_t ascEn = 1;
const int SCD4X_FRC_CO2_PPM = 400;

enum D_TYPE {
     D_TYPE_CO2
    ,D_TYPE_TEMP
    ,D_TYPE_HUMI
    ,D_TYPE_MAX
};
char D_TYPE_LABEL_STR[D_TYPE_MAX][8] = {
     "CO2 :"
    ,"TEMP:"
    ,"HUMD:"
};
char D_TYPE_UNIT_STR[D_TYPE_MAX][8] = {
     "ppm"
    ,"`C"
    ,"%RH"
};

enum D_STS {
     D_STS_NO_ERR
    ,D_STS_INITIALIZING
    ,D_STS_READ_DATA_ERR
    ,D_STS_MEASURING
    ,D_STS_PERFORMING_FRC
    ,D_STS_MAX
};
char D_STS_MSG[D_STS_MAX][32] = {
     "                      "
    ,"Initializing...       "
    ,"Can not read data.    "
    ,"Measuring...          "
    ,"Performing FRC(5 min.)"
};
D_STS current_sts = D_STS_NO_ERR;

enum ALERT_LV {
     ALERT_LV_1
    ,ALERT_LV_2
    ,ALERT_LV_3
    ,ALERT_LV_MAX
};
uint16_t ALERT_LV_COLOR[ALERT_LV_MAX] {
     GREEN
    ,ORANGE
    ,RED
};

const float BAT_VOL_MAX = 4.10;
const float BAT_VOL_MIN = 3.60;

float convBattVoltageToPercent(float batVol) {
    float battVolPer = (batVol-BAT_VOL_MIN)/(BAT_VOL_MAX-BAT_VOL_MIN)*100.0;
    if (battVolPer > 100.0) {
        battVolPer = 100.0;
    } else if (battVolPer < 0) {
        battVolPer = 0.0;
    }
    return battVolPer;
}

void drawStatus(D_STS sts) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
#if defined(M5STICKC_PULS_EN)
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.fillRect(0, 20, 135, 20, BLACK);
#else
    M5.Lcd.setCursor(0, 10);
    M5.Lcd.fillRect(0, 10, 160, 20, BLACK);
#endif
    M5.Lcd.print(D_STS_MSG[sts]);
}

#if defined(M5STICKC_PULS_EN)
void buzzerAck() {
    M5.Beep.tone(2700,200);
    delay(100);
    M5.Beep.mute();
}
#endif

void initM5() {
    current_sts = D_STS_NO_ERR;
    
    M5.begin();
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    M5.Axp.ScreenBreath(LCD_BRIGHTNESS_LV);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setRotation(0);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(0, 0);
#if defined(M5STICKC_PULS_EN)
    M5.Lcd.setTextSize(2);
#else
    M5.Lcd.setTextSize(1);
#endif
    M5.Lcd.print("CO2 Monitor");
    drawStatus(D_STS_INITIALIZING);

    // set LED off
    pinMode(GPIO_NUM_10, OUTPUT);
    digitalWrite(GPIO_NUM_10, HIGH);
    
#if defined(M5STICKC_PULS_EN) && defined(ALERT_LED_BUZZER_EN)
    M5.Beep.setVolume(10);
#endif
    
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    
    
    M5.update();
    if (M5.BtnA.isPressed()) {
        setupCalibMode = true;
    }
}

#if defined(UPLOAD_DATA_EN)
void setupNetwork() {
    int wifi_wait_time = 0;
  
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        wifi_wait_time += WIFI_CON_WAIT_INTERVAL_MS;
        if (WIFI_CON_WAIT_TIMEOUT_MS <= wifi_wait_time) {
            esp_restart();
        }
        delay(WIFI_CON_WAIT_INTERVAL_MS);
        Serial.print(".");
    }
    Serial.print("\r\nWiFi connected\r\nIP address: ");
    Serial.println(WiFi.localIP());
    ambient.begin(AMB_CH_ID, AMB_WR_KEY, &client); // Initialize Ambient channel
}
#endif /* defined(UPLOAD_DATA_EN) */

void setupSCD4x() {
    uint16_t error;
    char errorMessage[256];
    uint16_t frcCorrection;

    current_sts = D_STS_MEASURING;

    Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN);
    scd4x.begin(Wire);

    // stop potentially previously started measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        current_sts = D_STS_READ_DATA_ERR;
    }

    error = scd4x.getAutomaticSelfCalibration(ascEn);
    if (error) {
        Serial.print("Error trying to execute getAutomaticSelfCalibration(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        current_sts = D_STS_READ_DATA_ERR;
    }

    if (setupCalibMode) {
#if defined(M5STICKC_PULS_EN)
        buzzerAck();
#endif
        drawStatus(D_STS_PERFORMING_FRC);
        // Start Measurement
        error = scd4x.startPeriodicMeasurement();
        if (error) {
            Serial.print("Error trying to execute startPeriodicMeasurement(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
            current_sts = D_STS_READ_DATA_ERR;
        }
        Serial.print("Waiting for FRC... (");
        Serial.print(SCD4X_FRC_INTERVAL_S);
        Serial.println(" sec)");
        delay(SCD4X_FRC_INTERVAL_S * 1000);
        
        error = scd4x.stopPeriodicMeasurement();
        if (error) {
            Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
            current_sts = D_STS_READ_DATA_ERR;
        }
        delay(500);

        error = scd4x.performForcedRecalibration(SCD4X_FRC_CO2_PPM, frcCorrection);
        if (error) {
            Serial.print("Error trying to execute performForcedRecalibration(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
            current_sts = D_STS_READ_DATA_ERR;
        }
        
#if defined(M5STICKC_PULS_EN)
        buzzerAck();
#endif
    }
    
    // Start Measurement
    error = scd4x.startLowPowerPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute startLowPowerPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        current_sts = D_STS_READ_DATA_ERR;
    }

    drawStatus(current_sts);
    Serial.print("Waiting for first measurement... (");
    Serial.print(SCD4X_READ_MIN_INTERVAL_S);
    Serial.println(" sec)");
    delay(SCD4X_READ_MIN_INTERVAL_S * 1000);
}

void updateData() {
    int i;
    uint16_t error;
    char errorMessage[256];

    // Read Measurement SCD4x
    uint16_t co2;
    float temp;
    float humi;
    char valStr[D_TYPE_MAX][10];
    char drawValStr[D_TYPE_MAX][16];
    uint16_t valAlertLvColor[D_TYPE_MAX];

    for (i = 0; i < D_TYPE_MAX; i++) {
        sprintf(drawValStr[i],"-");
    }
    
    error = scd4x.readMeasurement(co2, temp, humi);
    if (error) {
        Serial.print("Error trying to execute readMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        current_sts = D_STS_READ_DATA_ERR;
    } else if (co2 == 0) {
        Serial.println("Invalid sample detected, skipping.");
        current_sts = D_STS_READ_DATA_ERR;
    } else {
        sprintf(valStr[D_TYPE_CO2],"%5d", co2);
        dtostrf(temp,5,1,valStr[D_TYPE_TEMP]);
        dtostrf(humi,5,1,valStr[D_TYPE_HUMI]);
        sprintf(drawValStr[D_TYPE_CO2],"%s",valStr[D_TYPE_CO2]);
        sprintf(drawValStr[D_TYPE_TEMP],"%s",valStr[D_TYPE_TEMP]);
        sprintf(drawValStr[D_TYPE_HUMI],"%s",valStr[D_TYPE_HUMI]);
        current_sts = D_STS_NO_ERR;
    }
    drawStatus(current_sts);

    // Read battery voltage
    float battVol = M5.Axp.GetBatVoltage();
    float battVolPer = convBattVoltageToPercent(battVol);
    char valBattVolStr[10];
    char valBattVolPerStr[10];
    char drawValBattStr[16];
    dtostrf(battVol,1,2,valBattVolStr);
    dtostrf(battVolPer,3,0,valBattVolPerStr);
    sprintf(drawValBattStr,"%s%%(%sV)",valBattVolPerStr,valBattVolStr);
    // serial print val
    for (i = 0;i < D_TYPE_MAX; i++) {
        Serial.print(D_TYPE_LABEL_STR[i]);
        Serial.print(drawValStr[i]);
        Serial.print(D_TYPE_UNIT_STR[i]);
        Serial.print(", ");
    }
    Serial.print(drawValBattStr);
    Serial.println();

    // draw string val
    uint16_t alert_on = 0;
    if (ALERT_LV1_CO2_MIN <= co2 && co2 < ALERT_LV1_CO2_MAX) {
        valAlertLvColor[D_TYPE_CO2] = ALERT_LV_COLOR[ALERT_LV_1];
    } else if (ALERT_LV2_CO2_MIN <= co2 && co2 < ALERT_LV2_CO2_MAX) {
        valAlertLvColor[D_TYPE_CO2] = ALERT_LV_COLOR[ALERT_LV_2];
    } else {
        valAlertLvColor[D_TYPE_CO2] = ALERT_LV_COLOR[ALERT_LV_3];
        alert_on = 1;
    }
    if (ALERT_LV1_TEMP_MIN <= temp && temp < ALERT_LV1_TEMP_MAX) {
        valAlertLvColor[D_TYPE_TEMP] = ALERT_LV_COLOR[ALERT_LV_1];
    } else if (ALERT_LV2_TEMP_MIN <= temp && temp < ALERT_LV2_TEMP_MAX) {
        valAlertLvColor[D_TYPE_TEMP] = ALERT_LV_COLOR[ALERT_LV_2];
    } else {
        valAlertLvColor[D_TYPE_TEMP] = ALERT_LV_COLOR[ALERT_LV_3];
        alert_on = 1;
    }
    if (ALERT_LV1_HUMI_MIN <= humi && humi < ALERT_LV1_HUMI_MAX) {
        valAlertLvColor[D_TYPE_HUMI] = ALERT_LV_COLOR[ALERT_LV_1];
    } else if (ALERT_LV2_HUMI_MIN <= humi && humi < ALERT_LV2_HUMI_MAX) {
        valAlertLvColor[D_TYPE_HUMI] = ALERT_LV_COLOR[ALERT_LV_2];
    } else {
        valAlertLvColor[D_TYPE_HUMI] = ALERT_LV_COLOR[ALERT_LV_3];
        alert_on = 1;
    }
#if defined(M5STICKC_PULS_EN)
    M5.Lcd.fillRect(0, 20, 135, 220, BLACK);
    for (i = 0; i < D_TYPE_MAX; i++) {
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setCursor(0, 50*i+40);
        M5.Lcd.print(D_TYPE_LABEL_STR[i]);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(117, 50*i+74);
        M5.Lcd.print(D_TYPE_UNIT_STR[i]);
        M5.Lcd.setTextSize(3);
        M5.Lcd.setTextColor(valAlertLvColor[i]);
        M5.Lcd.setCursor(18, 50*i+60);
        M5.Lcd.print(drawValStr[i]);
    }
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(2, 216);
    M5.Lcd.print(drawValBattStr);
#else
    M5.Lcd.fillRect(0, 10, 80, 150, BLACK);
    for (i = 0; i < D_TYPE_MAX; i++) {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setCursor(0, 40*i+30);
        M5.Lcd.print(D_TYPE_LABEL_STR[i]);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(62, 40*i+46);
        M5.Lcd.print(D_TYPE_UNIT_STR[i]);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(valAlertLvColor[i]);
        M5.Lcd.setCursor(0, 40*i+40);
        M5.Lcd.print(drawValStr[i]);
    }
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(12, 150);
    M5.Lcd.print(drawValBattStr);
#endif /* defined(M5STICKC_PULS_EN) */

#if defined(ALERT_LED_BUZZER_EN)
    // LED & Buzzer on if alert level 3
    if (alert_on) {
        digitalWrite(GPIO_NUM_10, LOW);
#if defined(M5STICKC_PULS_EN)
        // buzzer on
        for (i = 0; i < ALERT_BUZZER_REPEAT_NUM; i++) {
            M5.Beep.tone(220);
            delay(500);
            M5.Beep.mute();
            delay(200);
        }
#endif
    } else {
        digitalWrite(GPIO_NUM_10, HIGH);
    }
#endif

#if defined(UPLOAD_DATA_EN)
    // upload data to Ambient
    if (current_sts == D_STS_NO_ERR) {
        ambient.set(1, valStr[D_TYPE_CO2]);
        ambient.set(2, valStr[D_TYPE_TEMP]);
        ambient.set(3, valStr[D_TYPE_HUMI]);
        ambient.set(4, valBattVolStr);
        ambient.send();
    }
#endif /* UPLOAD_DATA_EN */
}

void setup() {
    // initialize M5
    initM5();

#if defined(UPLOAD_DATA_EN)
    // setup WiFi & connect to Ambient
    setupNetwork();
#endif /* defined(UPLOAD_DATA_EN) */

    // SCD4x - setup & measurement
    setupSCD4x();
}

void loop() {
    int t_begin = millis();
    int t_dur = 0;
    
    updateData();
    
    t_dur = millis() - t_begin;
    t_dur = (t_dur < UPDATE_INTERVAL_S * 1000) ? (UPDATE_INTERVAL_S * 1000 - t_dur) : 1000;
    Serial.print(t_dur);
    Serial.print(" ms delay");
    Serial.println();
   
    delay(t_dur);
    M5.update();
}
