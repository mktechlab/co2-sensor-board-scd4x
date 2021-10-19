/******************************************************************************/
//#define UPLOAD_DATA_EN      // Enable upload data to Ambient
//#define ALERT_BUZZER_EN // Enable buzzer alert

#if defined(UPLOAD_DATA_EN)
// WiFi Access Point
const char* WIFI_SSID   = "********";
const char* WIFI_PASS   = "********";
unsigned int AMB_CH_ID  = 0; // Channel ID for Ambient
const char* AMB_WR_KEY  = "****************"; // Write Key to Channel
#endif /* defined(UPLOAD_DATA_EN) */

const int UPDATE_INTERVAL_S   = 600; // Update interval seconds (default:600, range:60-3600)

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

#include "M5CoreInk.h"
#include <Wire.h>
#if defined(UPLOAD_DATA_EN)
#include <WiFi.h>
#include "Ambient.h"
#endif
#include "esp_adc_cal.h"

#include <SensirionI2CScd4x.h>
SensirionI2CScd4x scd4x;

#if defined(UPLOAD_DATA_EN)
// for Ambient
WiFiClient client;
Ambient ambient;

const int WIFI_CON_WAIT_INT            = 500;
const int WIFI_CON_WAIT_TIMEOUT        = 30000;
#endif

const int SCD4X_READ_MIN_INTERVAL_S     = 5;    // wait for measurement
const int SCD4X_FRC_INTERVAL_S          = 300;  // wait for FRC
const int SHUTDOWN_WIFI_ERR_INTERVAL_S  = 60;   // reboot time for wifi error
const int I2C_SCL_PIN = 26;
const int I2C_SDA_PIN = 25;

#if defined(UPLOAD_DATA_EN)
const int CPU_FREQ_MHZ = 80;
#else
const int CPU_FREQ_MHZ = 10;
#endif

// for eInk
Ink_Sprite MainPageSprite(&M5.M5Ink);

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
    ,D_STS_READ_DATA_ERR
    ,D_STS_MEASURING
    ,D_STS_PERFORMING_FRC
    ,D_STS_MAX
};
char D_STS_MSG[D_STS_MAX][32] = {
     "                      "
    ,"Can not read data!    "
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

const float BAT_VOL_MAX = 4.10;
const float BAT_VOL_MIN = 3.55;

int t_begin_ms = 0;
int t_dur_ms = 0;
int t_sleep_s = 0;

float getBattVoltage() {
    analogSetPinAttenuation(35,ADC_11db);
    esp_adc_cal_characteristics_t *adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 3600, adc_chars);
    uint16_t ADCValue = analogRead(35);
    
    uint32_t BattVolmV  = esp_adc_cal_raw_to_voltage(ADCValue,adc_chars);
    float battVol = float(BattVolmV) * 25.1 / 5.1 / 1000;
    return battVol;
}

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
    MainPageSprite.drawString(4,16,D_STS_MSG[sts]);
}

void buzzerAck() {
    M5.Speaker.tone(2700,200);
    delay(100);
    M5.Speaker.mute();
}

void initM5() {
    current_sts = D_STS_NO_ERR;
    
    M5.begin();
    digitalWrite(LED_EXT_PIN,LOW);
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    
    M5.update();
    if (M5.BtnMID.isPressed()) {
        setupCalibMode = true;
    }
    
    if (!M5.M5Ink.isInit()) {
        Serial.printf("Ink Init faild");
        while (1) delay(100);   
    }
    M5.M5Ink.clear();
    delay(1000);
    //creat ink refresh Sprite
    current_sts = D_STS_MEASURING;
    if (MainPageSprite.creatSprite(0,0,200,200,true) != 0) {
        Serial.printf("Create main page sprite faild");
    }
    drawStatus(current_sts);
    MainPageSprite.drawString(4,2,"CO2 Monitor (SCD4x)");
    MainPageSprite.pushSprite();
}

#if defined(UPLOAD_DATA_EN)
void setupNetwork() {
    int wifi_wait_time = 0;
  
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        wifi_wait_time += WIFI_CON_WAIT_INT;
        if (WIFI_CON_WAIT_TIMEOUT <= wifi_wait_time) {
            M5.shutdown(SHUTDOWN_WIFI_ERR_INTERVAL_S);
        }
        delay(WIFI_CON_WAIT_INT);
        Serial.print(".");
    }
    Serial.print("\r\nWiFi connected\r\nIP address: ");
    Serial.println(WiFi.localIP());
    ambient.begin(AMB_CH_ID, AMB_WR_KEY, &client); // Initialize Ambient channel
}
#endif

void setupSCD4x() {
    uint16_t error;
    char errorMessage[256];
    uint16_t frcCorrection;

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
        buzzerAck();
        
        drawStatus(D_STS_PERFORMING_FRC);
        MainPageSprite.pushSprite();
        // Start Measurement
        error = scd4x.startPeriodicMeasurement();
        if (error) {
            Serial.print("Error trying to execute startPeriodicMeasurement(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
            current_sts = D_STS_READ_DATA_ERR;
        }
        Serial.print("Waiting for FRC... (");
        Serial.print(SCD4X_READ_MIN_INTERVAL_S);
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
        
        buzzerAck();
    }
    
    // Start Measurement
    error = scd4x.startPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute startPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        current_sts = D_STS_READ_DATA_ERR;
    }

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
    uint16_t valAlertLv[D_TYPE_MAX];

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
        sprintf(valStr[D_TYPE_CO2],"%4d", co2);
        sprintf(drawValStr[D_TYPE_CO2],"%s",valStr[D_TYPE_CO2]);
        dtostrf(temp,3,1,valStr[D_TYPE_TEMP]);
        sprintf(drawValStr[D_TYPE_TEMP],"%s",valStr[D_TYPE_TEMP]);
        dtostrf(humi,3,1,valStr[D_TYPE_HUMI]);
        sprintf(drawValStr[D_TYPE_HUMI],"%s",valStr[D_TYPE_HUMI]);
        current_sts = D_STS_NO_ERR;
    }

    // Read battery voltage
    float battVol = getBattVoltage();
    float battVolPer = convBattVoltageToPercent(battVol);
    char valBattVolStr[10];
    char valBattVolPerStr[10];
    char drawValBattStr[16];
    dtostrf(battVol,1,2,valBattVolStr);
    dtostrf(battVolPer,3,0,valBattVolPerStr);
    sprintf(drawValBattStr,"BATT: %s%%(%sV)",valBattVolPerStr,valBattVolStr);
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
        valAlertLv[D_TYPE_CO2] = ALERT_LV_1;
    } else if (ALERT_LV2_CO2_MIN <= co2 && co2 < ALERT_LV2_CO2_MAX) {
        valAlertLv[D_TYPE_CO2] = ALERT_LV_2;
    } else {
        valAlertLv[D_TYPE_CO2] = ALERT_LV_3;
        alert_on = 1;
    }
    if (ALERT_LV1_TEMP_MIN <= temp && temp < ALERT_LV1_TEMP_MAX) {
        valAlertLv[D_TYPE_TEMP] = ALERT_LV_1;
    } else if (ALERT_LV2_TEMP_MIN <= temp && temp < ALERT_LV2_TEMP_MAX) {
        valAlertLv[D_TYPE_TEMP] = ALERT_LV_2;
    } else {
        valAlertLv[D_TYPE_TEMP] = ALERT_LV_3;
        alert_on = 1;
    }
    if (ALERT_LV1_HUMI_MIN <= humi && humi < ALERT_LV1_HUMI_MAX) {
        valAlertLv[D_TYPE_HUMI] = ALERT_LV_1;
    } else if (ALERT_LV2_HUMI_MIN <= humi && humi < ALERT_LV2_HUMI_MAX) {
        valAlertLv[D_TYPE_HUMI] = ALERT_LV_2;
    } else {
        valAlertLv[D_TYPE_HUMI] = ALERT_LV_3;
        alert_on = 1;
    }
    drawStatus(current_sts);
    for (i = 0; i < D_TYPE_MAX; i++) {
        MainPageSprite.drawString(10,48*i+40,D_TYPE_LABEL_STR[i]);
        MainPageSprite.drawString(58,48*i+32,drawValStr[i],&AsciiFont24x48);
        MainPageSprite.drawString(158,48*i+58,D_TYPE_UNIT_STR[i]);
    }
    MainPageSprite.drawString(10,180,drawValBattStr);
    MainPageSprite.pushSprite();
    
#if defined(ALERT_BUZZER_EN)
    // Buzzer on if alert level 3
    if (alert_on) {
        // buzzer on
        for (i = 0; i < ALERT_BUZZER_REPEAT_NUM; i++) {
            M5.Speaker.tone(220);
            delay(500);
            M5.Speaker.mute();
            delay(200);
        }
    } else {
        // buzzer off
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
#endif
}

void setup() {
    t_begin_ms = millis();
    
    // initialize M5
    initM5();

#if defined(UPLOAD_DATA_EN)
    // setup WiFi & connect to Ambient
    setupNetwork();
#endif
    
    // SCD4x - setup & measurement
    setupSCD4x();
    updateData();

    if (current_sts == D_STS_NO_ERR) {
        t_dur_ms = millis() - t_begin_ms;
        t_dur_ms = (t_dur_ms < UPDATE_INTERVAL_S * 1000) ? (UPDATE_INTERVAL_S * 1000 - t_dur_ms) : 1000;
        t_sleep_s = t_dur_ms/1000;
        if (t_sleep_s > 255) {
            t_sleep_s += (60 - t_sleep_s%60);
        }
        Serial.print("Wake up after ");
        Serial.print(t_sleep_s);
        Serial.print(" s.");
        Serial.println();
        M5.shutdown(t_sleep_s);
    } else {
        M5.shutdown();
    }
}

void loop() {
    M5.update();
}
