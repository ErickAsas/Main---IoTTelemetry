//Disable Bluetooth completely (BIGGEST win)
#define CONFIG_BT_ENABLED 0
#define CONFIG_BLUEDROID_ENABLED 0
//Reduce logging (very safe)
#define CORE_DEBUG_LEVEL 0
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
//Tune ArduinoJson (if used)
#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0

// ESP32 WiFi and Networking Libraries
#include <WiFiManager.h>         // WiFi Manager for easy configuration
#include <HTTPClient.h>          // HTTP communication (depends on WiFi.h)
#include <Preferences.h>         // For storing values
Preferences prefs;               // for storing lastEqID persistently

// JSON and Parsing Libraries
#include <ArduinoJson.h>         // JSON parsing

// Sensor Libraries
#include <DHT.h>                 // DHT temperature and humidity sensor
#include <math.h>		             // for log10, pow

// Other Utilities
#include <FastBot.h>             // Telegram bot Already includes Http Client Library 
#include <Update.h>              // Firmware update handling

// TFT and LCD
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Fonts/GFXFF/gfxfont.h>

//IMU AND I2C Sensors
#include <Wire.h>
#include <BMI160Gen.h>          //BMI160 GYROMETER
#include <MPU6050.h>            //MPU6050 GYROMETER
#include <I2Cdev.h>             //MPU6050 GYROMETER
#include <Adafruit_AHTX0.h>     //AHT10/20 HUMIDITY SENSOR 
#include <Adafruit_BMP280.h>    //BMP280 PRESSURE SENSOR

//esp task watchdog and System Control Functions:
#include <esp_task_wdt.h>
#include "esp_system.h"
#include <nvs_flash.h>
#include <esp_wifi.h>

/**
 * IoTTelemetry
 * 
 * Sketch to read values from DHT11/AHT10/AHT20, BMP280, BMI160/MPU6050 and MQ135 Sensors using ESP32
 * - Send readings to Sharepoint cloud storage using Power Automate
 * - Send readings to PowerBI Streaming Dataset
 * - OTA Capabilities
 * - Telegram integration for realtime time readings, commands and notifications
 * - Seismic monitoring using local gyroscope and USGS earthquake data
 * @author Erick L. Asas, MinFM Northmin
 * @version See Below
 */
// ============================================================================
// 1. System / Device / Time / Reset
// ============================================================================
#define FW_VERSION "4.1.6"

//Fetches from the main branch — not from Releases.
const char* VERSION_JSON_URL = "https://raw.githubusercontent.com/ErickAsas/Main---IoTTelemetry/main/version.json";

String currentVersion = FW_VERSION; // your current firmware version

unsigned long currentMillis = 0;
String localWebhost = "EZFMC";

bool isLoggedIn = false;
unsigned long authExpiry = 0; // for auto-expire

bool forceConfigPortal = false;
bool forceOTACheck = false;

unsigned long oRefreshInterval = 300000; // default 5 min (ms)
unsigned long lastSendTime = 0;

unsigned long oPrintInterval = 10000; // default 10 sec (ms)
unsigned long lastPrintTime = 0;

const unsigned long profileCheckInterval = 5000; // default 5 sec (ms)
unsigned long lastprofileCheck = 0;

const unsigned long warningInterval = 900000; // 15 minutes
unsigned long lastWarningTime = 0;

unsigned long oEQInterval = 60000; // default 1 min (ms)
unsigned long lastEQSendTime = 0;

const unsigned long otaInterval = 3600000;  // 1 hour

unsigned long lastPeriodicReportTime = 0;  // Track last periodic report send
unsigned long lastOtaCheckTime = 0;

RTC_DATA_ATTR bool firstPowerOnFlag = true;

volatile bool restartRequested = false;

RTC_DATA_ATTR uint8_t wifiFailBootCount = 0;

#define PREF_WIFI_FAILS "wifi_fails"

// Global or file-scope variable
String bootResetCause;

// ============================================================================
// 2. Network / WiFi / Internet / OTA
// ============================================================================
uint8_t wifiConnectFailCount = 0;
unsigned long wifiFailWindowStart = 0;
bool wifiEscalationInProgress = false;
bool pendingPortalClose = false;
bool wifiResumePending = false;

// Access Point name used during device setup or recovery mode.
const char* DEVICE_SETUP_AP_SSID = "DEVICE_SETUP_AP_NAME";

// Access Point password used during device setup or recovery mode.
// Must be at least 8 characters (WiFi requirement).
const char* DEVICE_SETUP_AP_PASSWORD = "DEVICE_SETUP_AP_PASSWORD";

WiFiManager wm;

const unsigned long internetFailTimeout = 900000; // 15 minutes
unsigned long internetFailStart = 0;
bool internetDown = false;

unsigned long lastInternetCheck = 0;
const unsigned long internetCheckInterval = 15000; // 15 seconds

bool otaPending = false;
bool otawakeupMessageSent = false;
bool otaMessageSent = false;

// ============================================================================
// 3. Device Profile / Settings / Config Portal
// ============================================================================
bool sendToServer = false;
bool sendToalternativeServer = true;
bool profileLoaded = false;
bool configMode = false;

float dhttemperature, dhthumidity, dhtheatindex, dhtpressure;
float CO, Alcohol, CO2, Toluen, NH3, Aceton;
float AcetonPPM, AlcoholPPM, COPPM, CO2PPM, NH3PPM, ToluenPPM;

String oDivision = "";
String oDistrict = "";
String oLocation = "";
String oContext = "";
float oCO2Base = 422.8;
float oTempThreshold = 28;
String deviceID = "";
String oDataset;
String timestamp, LocaltimeStamp;
String jsonPayload;
float oValue;
String lastUpdated = "";

float oTempOffset = -1.4;      // calibration
float oHumidOffset  = -4.0;

unsigned long wifiConnectedAt = 0;

bool otaInProgress = false;

#define PREF_PERIODIC_ENABLED "periodic_enabled"
#define PREF_PERIODIC_HOURS   "periodic_hours"

// ============================================================================
// 4. TFT Display
// ============================================================================
bool WithDisplay = false;   
bool IsDisplayInitialized = false;
String DisplaySize = "NA";

TFT_eSPI tft = TFT_eSPI();
int lineHeight = 25;
int labelX = 5;
int valueX = 55;
int fadeDir = 1;
int fadeValue = 100;
bool blink = false;

// 5. Web / REST / HTTP / JSON / TELEGRAM (VERY IMPORTANT)
// ============================================================================

// Primary cloud endpoint used to send IoTTelemetry sensor data to Microsoft Power Automate.
char IOTDeviceTelemetry[600] = "https://your-dev-endpoint";

// Cloud endpoint used to retrieve or synchronize IoTTelemetry device configuration profile.
const char* IOTDeviceProfile = "https://your-profile-endpoint";

// Secondary cloud endpoint used as fallback when primary telemetry delivery fails.
const char* IOTDeviceTelemetryBackup = "https://your-backup-endpoint";

// External earthquake data endpoint used to retrieve latest global seismic events (GeoJSON feed).
const char* usgsAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson";


// ============================================================================
// 6. Telegram Bot Integration
// ============================================================================

// Telegram Bot authentication token used for sending alerts and notifications.
String BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";

// Primary Telegram chat ID used for system owner / developer monitoring.
String CHAT_ID = "PRIMARY_MONITORING_CHAT_ID";

// Secondary Telegram chat ID used for standard operational viewers.
String CHAT_ID1 = "GENERAL_VIEWERS_CHAT_ID";

// Tertiary Telegram chat ID used for administrative or elevated-level viewers.
String CHAT_ID2 = "ADMIN_VIEWERS_CHAT_ID";


bool CHATIDADMIN_ENABLED = false;
bool CHAT_ID_ENABLED = true;
bool CHAT_ID_ADMIN_ENABLED = false;

FastBot bot(BOT_TOKEN.c_str());

bool telegramStarted = false;

// =========================
// TELEGRAM STABILITY LAYER
// =========================
#define TG_QUEUE_SIZE 8

struct TgMessage {
  char text[768];
  bool markdown;
  char chatID[32];   // empty = broadcast
};

TgMessage tgQueue[TG_QUEUE_SIZE];
volatile int tgHead = 0;
volatile int tgTail = 0;

// ============================================================================
// 7. Earthquake Monitoring (USGS)
// ============================================================================
bool sendEQAlert = true;
float alertMagnitude = 5.0;
float deviceLat = 8.48247530642442;
float deviceLon = 124.63515620933033;
float quakeRadiusKm = 250.0;
float minMagnitude = 4.0;

bool isUSGSFetching = false;
TaskHandle_t usgsTaskHandle = NULL;
SemaphoreHandle_t httpMutex;

// ============================================================================
// 8. Gyroscope / Local Seismic Monitoring / I2C Devices
// ============================================================================
const int GYRO_ADDR = 0x68;

//I2C1 Default pins
const int SDA_PIN = 21;
const int SCL_PIN = 22;
//I2C2 custom pins
const int SDA_PIN2 = 32;
const int SCL_PIN2 = 33;

enum GyroType { BMI160_TYPE, MPU6050_TYPE };
GyroType gyroModel;

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

BMI160GenClass bmi;
TwoWire I2C2 = TwoWire(1);
MPU6050 mpu(GYRO_ADDR, &I2C2);

bool UsesDHTSensor = true;
bool WithGyrometer = false;
bool IsGyrometerInitialized = false;
bool IsTempSensorInitialized = false;

// Tuned for ±2g, 25 Hz, gravity-removed acceleration
const float G_THRESHOLD_BMI160 = 0.020f;  // seismic-sensitive
const float G_THRESHOLD_MPU6050 = 0.030f; // noise-safe

const float ALPHA = 0.80;

const int MAX_SHAKING_EVENTS = 100;
struct ShakeEvent { float peak; unsigned long ts; uint8_t intensity; };
ShakeEvent shakeLog[MAX_SHAKING_EVENTS];
int shakeIndex = 0;

float gx=0, gy=0, gz=0;
const float GMPE_A = -1.68f;
const float GMPE_B = 0.90f;
const float GMPE_C = 1.10f;
const float GMPE_D = 0.0007f;

struct QuakeEstimate { float expectedPGA_g; float R; uint8_t intensity; uint8_t usgsMMI; };

bool imuReady  = true;
bool eqFirstCorrelation = true;
bool shakingNow = false;
bool warningSent = false; // Flag to track if warning has been sent
bool restoredSent = false; // Flag to track if the restoration message has been sent
unsigned long fastModeEndTime = 0;
unsigned long lastGyroReadTime = 0;
unsigned long gyroInterval = 1000;
unsigned long fastInterval = 250;
unsigned long normalInterval = 1000;
bool shakeNotificationSent = false;
unsigned long imuWarmupStart = 0;
int imuWarmupCount = 0;

unsigned long firstShakeTime = 0;                 // When shaking first detected
const unsigned long SHAKE_MATCH_WINDOW = 300000;  // 5 minutes to find matching earthquake
const unsigned long SHAKE_TIMEOUT = 300000;       // 5 minutes before assume "just movement"
bool shakeAwaitingEQ = false;                     // waiting for EQ to match shake
bool shakeTimeoutNotified = false;                // NEW: whether timeout notification was sent

bool sendPeriodicReport = false;                  // NEW: whether timeout notification was sent
float PeriodicReportInterval = 2;                 // NEW: whether timeout notification was sent

// ============================================================================
// 9. MQ135 / DHTXX / AHTXX / BMP280 / Air Quality
// ============================================================================
#define BMP280_ID_REG 0xD0
#define BMP280_CHIP_ID 0x58

const int GPIO_MQ135 = 35;
const int GPIO_DHT = 4;

struct GasCalibration { const char* name; float slope; float intercept; };
GasCalibration gasMap[] = { {"CO",-0.36,1.45},{"NH3",-0.47,1.55},{"Alcohol",-0.38,1.73},{"Aceton",-0.48,1.60},{"Toluen",-0.42,1.50},{"CO2",-0.42,1.92} };
const int gasCount = sizeof(gasMap)/sizeof(gasMap[0]);

#define MQ135_PIN GPIO_MQ135
#define DHTPIN GPIO_DHT
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define ADC_RESOLUTION 4095.0
#define VREF 3.3
#define RL 10.0
#define RATIO_CLEAN_AIR 3.6
#define VCC   5.0

#define DIV_TOP      47.0
#define DIV_BOTTOM   10.0
#define DIV_RATIO    ((DIV_TOP + DIV_BOTTOM) / DIV_BOTTOM)  // 5.7

#define CORA 0.00035
#define CORB 0.02718
#define CORC 1.39538
#define CORD 0.0018

float R0 = 10.3;      // kΩ (clean-air calibrated)
float R0Fixed = 10.3;
float RS = 0;

// Clean-air reference (from your stable runs)
#define BASE_CO2      187.0
#define BASE_CO       67.0
#define BASE_ALCOHOL  134.0
#define BASE_ACETONE  126.0
#define BASE_NH3      110.0
#define IAQ_ALPHA 0.15

#define MQ_ADC_SAMPLES 10
#define MQ_ADC_DELAY   5   // ms between samples

bool mq135NeedsCalibration = false;
unsigned long mq135BootTime = 0;

// ================== Filtered outputs ==================
float tempFilt = NAN, humFilt = NAN, pressFilt = NAN, dewFilt = NAN;
float tempOut  = NAN, humOut  = NAN;

float bmpTempFilt = NAN;
float bmpTempOut  = NAN;

// ================== EMA filtering ==================
const float TEMP_ALPHA  = 0.1;
const float HUM_ALPHA   = 0.1;
const float PRESS_ALPHA = 0.05;
const float DEW_ALPHA   = 0.1;

const float BMP_TEMP_ALPHA    = 0.1;

// ================== Deadband ==================
const float TEMP_DEADBAND = 0.2; // °C
const float HUM_DEADBAND  = 0.5; // %
const float BMP_TEMP_DEADBAND = 0.2;

// ============================================================================
// 10. IR / AC Control
// ============================================================================
const uint8_t IR_LED_PIN=23, IR_LED_PINL=19, IR_LED_PINR=18;
#define MAX_VALUES 400
struct IRCommand { const char* name; uint16_t* data; size_t* length; };

uint16_t arr_ACL_ON[MAX_VALUES]; size_t len_ACL_ON=0;
uint16_t arr_ACL_OFF[MAX_VALUES]; size_t len_ACL_OFF=0;
uint16_t arr_ACL_UP[MAX_VALUES]; size_t len_ACL_UP=0;
uint16_t arr_ACL_DOWN[MAX_VALUES]; size_t len_ACL_DOWN=0;
uint16_t arr_ACL_18[MAX_VALUES]; size_t len_ACL_18=0;
uint16_t arr_ACL_20[MAX_VALUES]; size_t len_ACL_20=0;
uint16_t arr_ACL_22[MAX_VALUES]; size_t len_ACL_22=0;
uint16_t arr_ACL_24[MAX_VALUES]; size_t len_ACL_24=0;

uint16_t arr_ACR_ON[MAX_VALUES]; size_t len_ACR_ON=0;
uint16_t arr_ACR_OFF[MAX_VALUES]; size_t len_ACR_OFF=0;
uint16_t arr_ACR_UP[MAX_VALUES]; size_t len_ACR_UP=0;
uint16_t arr_ACR_DOWN[MAX_VALUES]; size_t len_ACR_DOWN=0;
uint16_t arr_ACR_18[MAX_VALUES]; size_t len_ACR_18=0;
uint16_t arr_ACR_20[MAX_VALUES]; size_t len_ACR_20=0;
uint16_t arr_ACR_22[MAX_VALUES]; size_t len_ACR_22=0;
uint16_t arr_ACR_24[MAX_VALUES]; size_t len_ACR_24=0;

IRCommand commands[] = {
  {"ACL_ON", arr_ACL_ON,&len_ACL_ON},{"ACL_OFF", arr_ACL_OFF,&len_ACL_OFF},{"ACL_UP", arr_ACL_UP,&len_ACL_UP},
  {"ACL_DOWN", arr_ACL_DOWN,&len_ACL_DOWN},{"ACL_18", arr_ACL_18,&len_ACL_18},{"ACL_20", arr_ACL_20,&len_ACL_20},
  {"ACL_22", arr_ACL_22,&len_ACL_22},{"ACL_24", arr_ACL_24,&len_ACL_24},{"ACR_ON", arr_ACR_ON,&len_ACR_ON},
  {"ACR_OFF", arr_ACR_OFF,&len_ACR_OFF},{"ACR_UP", arr_ACR_UP,&len_ACR_UP},{"ACR_DOWN", arr_ACR_DOWN,&len_ACR_DOWN},
  {"ACR_18", arr_ACR_18,&len_ACR_18},{"ACR_20", arr_ACR_20,&len_ACR_20},{"ACR_22", arr_ACR_22,&len_ACR_22},
  {"ACR_24", arr_ACR_24,&len_ACR_24}
};

bool AC_Control=false;
bool Wireless_Control=false;
String ACL_ON,ACL_OFF,ACL_UP,ACL_DOWN,ACL_18,ACL_20,ACL_22,ACL_24;
String ACR_ON,ACR_OFF,ACR_UP,ACR_DOWN,ACR_18,ACR_20,ACR_22,ACR_24;

/****************************************************
 * 1. FORWARD DECLARATIONS (function prototypes)
 ****************************************************/
// ============================================================================
// System / Device / Time / Reset
// ============================================================================
String getLocaltimeStamp(time_t epochSec = 0);
String decodeResetReason(esp_reset_reason_t reason);
String getTimestamp();

// ============================================================================
// Network / WiFi / Internet / OTA
// ============================================================================
void EstablishWifiConnection();
bool isInternetAvailable();
void checkInternet();
void checkWiFi();
void checkRemoteOta();
void PerformTasksWithInternetConnectivity();
void checkPeriodicReport();
String readVersion();
void sendOtaSuccessMessage();
void sendWakeupMessage();
void sendLowHeapRestartNotice(uint32_t freeHeap);
void updateVersion(String ver);

// ============================================================================
// Device Profile / Settings / Config Portal
// ============================================================================
void EvaluateforceConfigPortal();
void getDeviceProfile(String mac);
void loadSettings();
void stopConfigPortal();
void saveConfigCallback();
void saveInterval(unsigned long interval);

// ============================================================================
// Web Server / UI / TFT Display
// ============================================================================
uint16_t getTFTColor(float value, float warn, float danger);
void drawHeader();
void drawLabelValue(const char* label, String value, uint16_t color, int y);
void drawRoom();
String formatValue(float val, const char* unit);

// ============================================================================
// JSON / Payload / HTTP / REST
// ============================================================================
String buildJsonPayload(String timestamp, String localTimestamp);
void sendToClassicASP(const char* url, const String& payload, const String& dataset);
void sendToPowerAutomate(const char* url, const String& payload, const String& dataset);

// ============================================================================
// Telegram Bot
// ============================================================================
void initTelegram();
void EvaluateTemperatureBreach();
String getDeviceReadingsForTelegram();
void handleBotMessage(FB_msg& msg);
bool isACLCommand(const String& cmd);
bool enqueueTelegram(const char* msg, const char* targetChatID = nullptr);
void processTelegramQueue();
String getDeviceReadingsForTelegramSpecific(String cmd);

// ============================================================================
// Earthquake Monitoring (USGS)
// ============================================================================
void checkEQAlerts(String source);
void checkEQAlertsPageSafe(String source);
void clearShakeLog();
float haversine(float lat1, float lon1, float lat2, float lon2);
QuakeEstimate estimateQuakeIntensity(float magnitude, float depth_km, float distance_km);
void startUSGSTask();
void sendShakingAlert();
String getShakingLevel(float intensity);
uint8_t mapIntensity(float g);
uint8_t pgaToIntensity(float pga);
uint8_t pgaToUSGS_MMI(float pga);
void usgsTask(void* param);

// ============================================================================
// Gyroscope / Local Seismic Monitoring
// ============================================================================
bool GYRO_init();
void ExecuteSeismicMonitoring();
void readAccelerometer();

// ============================================================================
// MQ135/ DHTXX / AHTXX / BMP280 / Air Quality / Gas Calculations
// ============================================================================
float LoadMQ135_R0();
void CalibrateMQ135();
float heatIndex(float t, float h);
float getCO2CorrectionFactor(float t, float h);
float getCorrectionFactor(const char* gas, float tempC, float humidity);
void GetAirQuality();

const char* Get_Level_TemperatureEmoji(float val, bool emojionly);
const char* Get_Level_HumidityEmoji(float val, bool emojionly);
const char* Get_Level_HeatIndexEmoji(float val, bool emojionly);
const char* Get_Level_AcetonEmoji(float val, bool emojionly);
const char* Get_Level_AlcoholEmoji(float val, bool emojionly);
const char* Get_Level_COEmoji(float val, bool emojionly);
const char* Get_Level_CO2Emoji(float val, bool emojionly);
const char* Get_Level_NH3Emoji(float val, bool emojionly);
const char* Get_Level_TolueneEmoji(float val, bool emojionly);

void PrintAirQuality();
GasCalibration* findGasCalibration(const char* name);
float getCorrectedPPM(const char* gas, float ppm_raw, float tempC, float humidity);
float getGasPPM(const char* gas, float Rs_R0);
float getSensorResistance();
int readMQ135ADC();
void updateValues(float tmp, float hum, float hi, float ace, float alc, float co, float co2, float nh3, float tol);

// ============================================================================
// Infrared (IR) Control
// ============================================================================
int parseStringToArray(String data, uint16_t *arr, int maxLen);
void sendIR(const String& command);
void sendRaw(const String& command, uint16_t *arr, size_t length, uint32_t carrierFreq = 38000);

bool i2cDevicePresent(uint8_t addr);
uint8_t readRegister(uint8_t addr, uint8_t reg);
int detectBMP280Address();
bool detectBMI160();

void handleSerialCommands();
void printHelp();
// END OF FORWARD DECLARATIONS
// ============================================================================

/****************************************************
 * 2. setup() and loop() — the “high-level logic”
 ****************************************************/
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);  // Disable all debug output over serial

  //setup MQ135 and DHT11 pins and attenuation AND CONFIGURE I2C PINS
  pinMode(MQ135_PIN, INPUT);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);
  pinMode(GPIO_DHT, INPUT);   // High-Z, no ADC, no pullups
  Wire.begin(SDA_PIN, SCL_PIN);  // ESP32 I2C pins - Wire 1

  // Create HTTP mutex
  httpMutex = xSemaphoreCreateMutex();
  if (!httpMutex) {
    Serial.println("❌ Failed to create HTTP mutex");
  }


  //Create Preferences instance
  prefs.begin("IoTTelemetry", false);  // ONE namespace, RW, lifetime = firmware

  // ==================================================
  // 🔁 MONITOR RESET COUNT (once per boot)
  // ==================================================
  esp_reset_reason_t reason = esp_reset_reason();

  // Reset counter on real power events
  uint32_t resetCount;
  if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT || reason == ESP_RST_SW) {
    resetCount = 1;
  } else {
    resetCount = prefs.getUInt("reset.count", 0) + 1;
  }

  prefs.putUInt("reset.count", resetCount);
  prefs.putUInt("reset.last", reason);

  // Decode once
  bootResetCause = decodeResetReason(reason);

  Serial.printf("🔁 Reset #%u | Cause: %s\n",
                resetCount,
                bootResetCause.c_str());


  // ==================================================
  // 🔒 Let ESP32 restore credentials naturally
  // ==================================================
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);

  // ==================================================
  // ==================================================
  EstablishWifiConnection();
  // ==================================================
  // ==================================================

  // ==================================================
  // ⏳ WAIT FOR WIFI TO STABILIZE (CRITICAL)
  // ==================================================
  bool wifiReady = false;

  if (WiFi.status() == WL_CONNECTED) {
    delay(500); // let DHCP + TCP settle
    Serial.println("📶 WiFi stabilized: " + WiFi.localIP().toString());
    wifiReady = true;
    wifiConnectFailCount = 0;
    wifiFailWindowStart = 0;    
  }

    // Display Device ID via MAC (safe even if WiFi OFF)
  deviceID = WiFi.macAddress();
  Serial.println("🔑 Device MAC: " + deviceID);

  // ==================================================
  // 🌫 MQ135 INIT
  // ================================================== 
  mq135BootTime = millis();

  R0 = LoadMQ135_R0();

  if (R0 <= 0 || isnan(R0)) {
    Serial.println("⚠️ No stored R0 — using fallback and scheduling calibration");
    R0 = R0Fixed;                 // SAFE TEMP VALUE
    mq135NeedsCalibration = true; // schedule later
  }

  // ==================================================
  // ⚙ LOAD SETTINGS & RUNTIME CONFIG
  // ==================================================
  loadSettings();

  sendPeriodicReport     = prefs.getBool(PREF_PERIODIC_ENABLED, false);
  PeriodicReportInterval = prefs.getFloat(PREF_PERIODIC_HOURS, 2.0);
  lastPeriodicReportTime = millis();

  // ==================================================
  // 🔖 FIRMWARE VERSION HANDLING
  // ==================================================
  String storedVersion = readVersion();
  storedVersion.trim();

  if (storedVersion == "") {
    updateVersion(FW_VERSION);
    currentVersion = FW_VERSION;
  }
  else if (storedVersion == FW_VERSION) {
    currentVersion = FW_VERSION;
  }
  else {
    Serial.println("⚠️ Version mismatch detected.");
    Serial.println("📦 Compiled FW : " + String(FW_VERSION));
    Serial.println("💾 Stored FW   : " + storedVersion);
    updateVersion(FW_VERSION);
    currentVersion = FW_VERSION;
  }
}

void loop() {
  currentMillis = millis();

   // ==================================================
  // 🛠 CONFIG MODE — HARD ISOLATION
  // ==================================================
  EvaluateforceConfigPortal();

  if (configMode) {

    wm.process();

    if (WiFi.status() == WL_CONNECTED && !pendingPortalClose) {
      Serial.println("✅ WiFi connected — scheduling portal close");
      pendingPortalClose = true;

      wifiConnectFailCount = 0;
      wifiFailWindowStart  = 0;
    }

    return;   // 🚫 NOTHING ELSE RUNS
  }

  // ==================================================
  // 🔒 Deferred config portal close (SAFE)
  // ==================================================
  if (pendingPortalClose) {
    pendingPortalClose = false;
    stopConfigPortal();
    wifiResumePending = true;   // ⭐ IMPORTANT
    return;                     // let WM fully unwind
  }

  // ==================================================
  // 🌐 Resume STA AFTER portal is fully gone
  // ==================================================
  if (wifiResumePending) {
    Serial.println("📡 Resuming WiFi STA after portal");
    wifiResumePending = false;

    WiFi.mode(WIFI_STA);
    WiFi.begin();
    return;   // give WiFi stack one full loop
  }
  
  // ==================================================
  // 🤖 Telegram (NORMAL MODE ONLY)
  // ==================================================
  if (!telegramStarted && WiFi.status() == WL_CONNECTED) {
    initTelegram();
    telegramStarted = true;
  }

  if (telegramStarted && WiFi.status() == WL_CONNECTED) {
    bot.tick();
    processTelegramQueue();
  }

  // ==================================================
  // 📶 WiFi health
  // ==================================================
  checkWiFi();

  // ==================================================
  // 🌍 Internet check
  // ==================================================
  if (currentMillis - lastInternetCheck >= internetCheckInterval) {
    lastInternetCheck = currentMillis;
    checkInternet();
  }

  // ==================================================
  // 🌫 Air quality readings
  // ==================================================
  if (currentMillis - lastPrintTime >= oPrintInterval) {
    lastPrintTime = currentMillis;

    if (profileLoaded) {
      GetAirQuality();
      PrintAirQuality();

      if (IsDisplayInitialized) {
        updateValues(dhttemperature,dhthumidity,dhtheatindex,AcetonPPM,AlcoholPPM,COPPM,CO2PPM,NH3PPM,ToluenPPM);
      }
    }
  }

  // ==================================================
  // ☁ Internet-dependent tasks
  // ==================================================
  if (WiFi.status() == WL_CONNECTED && isInternetAvailable()) {
    //Device profile fetching, periodic reporting, earthquake alerts, OTA checks
    PerformTasksWithInternetConnectivity();
  }

  // ==================================================
  // 🧪 wakeup / OTA messages
  // ==================================================
  static bool justUpdated = false;
  static bool bootFlagsRead = false;

  if (!bootFlagsRead) {
    justUpdated = prefs.getBool("ota.justUpdated", false);
    bootFlagsRead = true;
  }

  if (profileLoaded && !otawakeupMessageSent) {
    if (justUpdated) {
      sendOtaSuccessMessage();
      otaMessageSent = true;
    } else {
      sendWakeupMessage();
    }
    otawakeupMessageSent = true;
  }

  // ==================================================
  // 🌡 Temperature sensor init (one-shot)
  // ==================================================
  if (profileLoaded && !IsTempSensorInitialized) {

    if (UsesDHTSensor) {
      if (!otaInProgress) {
        pinMode(GPIO_DHT, INPUT);
        dht.begin();
        IsTempSensorInitialized = true;
        Serial.println("🌡️ DHT11 initialized");
      }
    } else {
      aht.begin();
      Serial.println("✅ AHT20 Initialized");

      int bmpAddr = detectBMP280Address();
      if (bmpAddr >= 0 && bmp.begin(bmpAddr)) {
        Serial.println("✅ BMP280 initialized");
      } else {
        Serial.println("❌ BMP280 init failed");
      }

      IsTempSensorInitialized = true;
    }
  }

  // ==================================================
  // 🖥 Display init (one-shot)
  // ==================================================
  if (WithDisplay && !IsDisplayInitialized) {
    ledcSetup(0, 38000, 8);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    drawHeader();
    drawRoom();

    IsDisplayInitialized = true;
  }

  // ==================================================
  // 🌎 Seismic monitoring
  // ==================================================
  ExecuteSeismicMonitoring();

  // ==================================================
  // 💾 Memory check
  // ==================================================
  static unsigned long lastHeapLog = 0;
  static bool lowHeapNotified = false;

  uint32_t freeHeap = ESP.getFreeHeap();

  if (millis() - lastHeapLog >= 60000) {
    lastHeapLog = millis();
    Serial.printf("💾 Free Heap: %u bytes\n", freeHeap);
  }

  if (!otaInProgress && freeHeap < 30000) {

    if (!lowHeapNotified) {
      lowHeapNotified = true;
      Serial.printf("🚨 Heap critically low: %u bytes\n", freeHeap);
      sendLowHeapRestartNotice(freeHeap);
      delay(1000);
    }

    ESP.restart();
  }

  // ==================================================
  // 🧪 MQ135 delayed calibration
  // ==================================================
  const unsigned long MQ135_WARMUP_MS = 120000;

  if (mq135NeedsCalibration &&
      millis() - mq135BootTime > MQ135_WARMUP_MS) {

    Serial.println("🧪 Running delayed MQ135 calibration");
    CalibrateMQ135();

    if (R0 > 0 && R0 != R0Fixed) {
      mq135NeedsCalibration = false;
    }
  }

  // ==================================================
  // 🧪 Force OTA check
  // ==================================================
  if (forceOTACheck) {
    forceOTACheck = false;
    checkRemoteOta();
  }

  if (otaMessageSent) {
    prefs.putBool("ota.pending", false);
    prefs.putBool("ota.justUpdated", false);
    prefs.putString("version.previous", currentVersion);
    otaMessageSent = false;
  }

  // ==================================================
  // 🧪 Serial commands
  // ==================================================
  handleSerialCommands();

  // ==================================================
  // 🔄 Deferred restart
  // ==================================================
  if (restartRequested) {
    Serial.println("🔄 Restarting device...");
    delay(1000);
    ESP.restart();
  }
}

/****************************************************
 * FOR DEBUGGING AND SIMULATION
 ****************************************************/
/****************************************************
 * SERIAL COMMANDS — DEBUG / MAINTENANCE / SIMULATION
 ****************************************************/
void handleSerialCommands() {

  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  // ==================================================
  // 🔄 SYSTEM / FIRMWARE
  // ==================================================
  if (cmd == "ver") {
    Serial.println("🔑 Firmware version: " + String(FW_VERSION));
    return;
  }

  if (cmd == "restart") {
    Serial.println("🔄 Restarting device...");
    delay(1000);
    ESP.restart();
    return;
  }

  if (cmd == "ota") {
    Serial.println("🧪 Manual OTA trigger received");
    checkRemoteOta();
    return;
  }

  // ==================================================
  // 📡 WIFI / CONNECTIVITY (DEBUG & SIMULATION)
  // ==================================================
  if (cmd == "forceportal") {
    Serial.println("🧪 Forcing config portal (test mode)");
    forceConfigPortal = true;
    return;
  }

  if (cmd == "wififail") {
    Serial.println("🧪 Simulating WiFi failure");
    WiFi.disconnect(false, false);
    wifiConnectFailCount = 99;
    wifiFailWindowStart = millis() - 60000;
    return;
  }

  if (cmd == "wifireset") {
    Serial.println("🔥 Erasing WiFi credentials + rebooting");

    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_OFF);

    nvs_flash_erase();
    nvs_flash_init();
    delay(500);

    ESP.restart();
    return;
  }

  // ==================================================
  // 🌋 SEISMIC / ALERT TUNING
  // ==================================================
  if (cmd.startsWith("magnitude ")) {
    float newMagnitude = cmd.substring(10).toFloat();

    if (newMagnitude > 0) {
      alertMagnitude = newMagnitude;
      Serial.println("✅ Magnitude alert limit updated to: " +
                     String(alertMagnitude, 1));
    } else {
      Serial.println("❌ Invalid magnitude value.");
    }
    return;
  }

  // ==================================================
  // 🌫 MQ135 / SENSOR MAINTENANCE
  // ==================================================
  if (cmd == "calibrate mq135") {
    prefs.remove("mq135.r0");
    R0 = R0Fixed;
    mq135NeedsCalibration = true;
    mq135BootTime = millis();
    Serial.println("🧪 MQ135 calibration scheduled");
    return;
  }

  if (cmd == "force calibrate mq135") {
    prefs.remove("mq135.r0");
    CalibrateMQ135();
    Serial.println("🧪 MQ135 calibration forced");
    return;
  }

  // ==================================================
  // 🤖 REPORTING / TELEGRAM
  // ==================================================
  if (cmd == "wakeup report") {
    sendWakeupMessage();
    return;
  }

  if (cmd == "ota report") {
    sendOtaSuccessMessage();
    return;
  }

  if (cmd == "online report") {
    bot.sendMessage("I am online", CHAT_ID1.c_str());
    return;
  }

  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;   // ⭐ THIS IS THE KEY
  }

  // ==================================================
  // ❓ UNKNOWN COMMAND
  // ==================================================
  Serial.println("❓ Unknown command: " + cmd);
  printHelp();
}

void printHelp() {
  Serial.println("ℹ️ Available commands:");
  Serial.println("  ver");
  Serial.println("  restart");
  Serial.println("  ota");
  Serial.println("  forceportal");
  Serial.println("  wififail");
  Serial.println("  wifireset");
  Serial.println("  magnitude <value>");
  Serial.println("  calibrate mq135");
  Serial.println("  force calibrate mq135");
  Serial.println("  wakeup report");
  Serial.println("  ota report");
  Serial.println("  online report");
}

/****************************************************
 * 3. FUNCTION DEFINITIONS
 ****************************************************/

 // ============================================================================
// System / Device / Time / Reset
// ============================================================================
String getLocaltimeStamp(time_t epochSec) {
  // If no argument, use current time
  if (epochSec == 0) epochSec = time(NULL);

  // Convert to Philippine local time (UTC+8)
  epochSec += 8 * 3600;

  struct tm timeinfo;
  gmtime_r(&epochSec, &timeinfo);

  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  return String(timeString); // e.g., "2025-10-30T21:12:45"
}

String decodeResetReason(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "🔌 POWERON (Cold boot)";
    case ESP_RST_BROWNOUT:
      return "⚡ BROWNOUT (Low voltage)";
    case ESP_RST_SW:
      return "🔄 SOFTWARE (Controlled restart)";
    case ESP_RST_WDT:
      return "⏱️ WDT (Watchdog)";
    case ESP_RST_TASK_WDT:
      return "🧵 TASK_WDT (Task blocked)";
    case ESP_RST_INT_WDT:
      return "🚦 INT_WDT (Interrupt watchdog)";
    case ESP_RST_PANIC:
      return "💥 PANIC (Guru Meditation)";
    case ESP_RST_EXT:
      return "🔘 EXTERNAL (Reset pin)";
    case ESP_RST_DEEPSLEEP:
      return "😴 DEEPSLEEP Wake";
    default:
      return "❓ UNKNOWN";
  }
}

String getTimestamp() {
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);  // Convert to UTC
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(timeString) + "Z";  // Add Z for UTC
}

// ============================================================================
// Network / WiFi / Internet / OTA
// ============================================================================
void EstablishWifiConnection() {

  Serial.println("🔁 WiFi fast connect (persistent)");
  WiFi.begin();

  unsigned long start = millis();
  const unsigned long TIMEOUT = 8000;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < TIMEOUT) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected: " + WiFi.localIP().toString());
    prefs.putUChar(PREF_WIFI_FAILS, 0);
    wifiConnectFailCount = 0;
    wifiFailWindowStart = 0;    
    return;
  }

  Serial.println("\n⚠️ WiFi not connected — checking driver");

  wifi_mode_t mode;
  esp_wifi_get_mode(&mode);

  esp_err_t err = (mode == WIFI_MODE_NULL)
                    ? esp_wifi_start()
                    : ESP_OK;

  if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STOPPED) {
    Serial.println("🟡 WiFi driver OK — tracking failures");

    if (wifiFailWindowStart == 0) {
      wifiFailWindowStart = millis();
    }

    wifiConnectFailCount++;

    Serial.printf("📉 WiFi connect failures: %u\n", wifiConnectFailCount);

    // If failed continuously for 30 seconds, assume creds wrong
    if (wifiConnectFailCount >= 5 &&
        millis() - wifiFailWindowStart > 30000) {

      Serial.println("🛠 WiFi credentials likely invalid — opening config portal");

      wifiConnectFailCount = 0;
      wifiFailWindowStart = 0;

      forceConfigPortal = true;
    }

    return;

  }

  Serial.printf("🧨 WiFi driver failure (err=%d)\n", err);

  uint8_t fails = prefs.getUChar(PREF_WIFI_FAILS, 0) + 1;
  prefs.putUChar(PREF_WIFI_FAILS, fails);

  if (fails >= 3) {
    Serial.println("🔥 NVS erase justified (driver corrupted)");
    prefs.putUChar(PREF_WIFI_FAILS, 0);

    nvs_flash_erase();
    nvs_flash_init();
    delay(500);
    ESP.restart();
  }
}

bool isInternetAvailable() {
    HTTPClient http;
    http.setTimeout(5000); // 5 seconds max per request
    http.begin("http://clients3.google.com/generate_204");
    int code = http.GET();
    http.end();
    return (code == 204);
}

void checkInternet() {
    if (WiFi.status() == WL_CONNECTED) {
        if (isInternetAvailable()) {
            // Internet is reachable
            internetDown = false;
            internetFailStart = 0;
        } else {
            // Wi-Fi connected but no internet
            if (!internetDown) {
                internetDown = true;
                internetFailStart = millis();
            } else {
                // Check if failure exceeded timeout
                if (millis() - internetFailStart >= internetFailTimeout) {
                    Serial.println("⚠️ Internet unreachable for too long, restarting ESP32...");
                    ESP.restart();
                }
            }
        }
    } else {
        // Wi-Fi disconnected, handled by checkWiFi()
        internetDown = false;
        internetFailStart = 0;
    }
}

void checkWiFi() {
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long failWindowStart = 0;
  static uint8_t failCount = 0;

  const unsigned long reconnectInterval = 10000; // 10s
  const unsigned long FAIL_WINDOW_MS = 30000;     // 30s
  const uint8_t FAIL_LIMIT = 3;

  if (configMode) return;

  if (WiFi.status() == WL_CONNECTED) {
    // Reset on success
    failCount = 0;
    failWindowStart = 0;
    return;
  }

  unsigned long now = millis();

  // Start failure window
  if (failWindowStart == 0) {
    failWindowStart = now;
  }

  // Periodic reconnect attempt
  if (now - lastReconnectAttempt >= reconnectInterval) {
    Serial.println("⚠️ Wi-Fi disconnected. Attempting reconnect...");
    lastReconnectAttempt = now;
    WiFi.reconnect();
    failCount++;
    Serial.printf("📉 WiFi reconnect failures: %u\n", failCount);
  }

  // Escalation decision
  if (failCount >= FAIL_LIMIT &&
      now - failWindowStart >= FAIL_WINDOW_MS) {

    Serial.println("🛠 WiFi credentials likely invalid — opening config portal");

    failCount = 0;
    failWindowStart = 0;
    forceConfigPortal = true;
  }
}

bool isNewerVersion(const String& server, const String& current) {
  int sMaj, sMin, sPat;
  int cMaj, cMin, cPat;

  if (sscanf(server.c_str(), "%d.%d.%d", &sMaj, &sMin, &sPat) != 3) return false;
  if (sscanf(current.c_str(), "%d.%d.%d", &cMaj, &cMin, &cPat) != 3) return false;

  if (sMaj != cMaj) return sMaj > cMaj;
  if (sMin != cMin) return sMin > cMin;
  return sPat > cPat;
}

String buildFirmwareURL(const String& tag, const String& bin) {
  return "https://github.com/ErickAsas/Main---IoTTelemetry/releases/download/" +
         tag + "/" + bin;
}

void checkRemoteOta() {

  Serial.println("⏳ Remote OTA Check...");

  // === OTA SAFEGUARD ===
  pinMode(GPIO_DHT, INPUT);      // Force GPIO4 High-Z
  otaInProgress = true;   // Optional global flag

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(VERSION_JSON_URL);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.println("❌ Failed to fetch version.json");
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  DynamicJsonDocument doc(256);
  #pragma GCC diagnostic pop

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("❌ JSON parse error");
    return;
  }

  String remoteVersion = doc["version"].as<String>();
  String tag           = doc["tag"].as<String>();
  String bin           = doc["bin"].as<String>();

  Serial.println("📦 Current FW : " FW_VERSION);
  Serial.println("🌐 Remote FW  : " + remoteVersion);

  if (!isNewerVersion(remoteVersion, FW_VERSION)) {
    Serial.println("✅ ESP32 firmware is up to date.");
    return;
  }

  Serial.println("⬆️ New firmware available!");

  String firmwareUrl = buildFirmwareURL(tag, bin);
  Serial.println("⬇️ Downloading:");
  Serial.println(firmwareUrl);

  // ==== OTA DOWNLOAD ====
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(firmwareUrl);
  int firmwareCode = http.GET();
  if (firmwareCode != HTTP_CODE_OK) {
    Serial.println("❌ Firmware download failed. Code: " + String(firmwareCode));
    http.end();
    return;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (contentLength <= 0) {
    Serial.println("Invalid firmware size.");
    http.end();
    return;
  }

  if (!Update.begin(contentLength)) {
    Update.printError(Serial);
    http.end();
    return;
  }

  size_t written = Update.writeStream(*stream);
  if (written != (size_t)contentLength) {
    Serial.println("❌ Incomplete OTA write");
    Update.abort();
    http.end();
    return;
  }

  if (Update.end(true)) {
    Serial.println("✅ OTA successful. Rebooting...");
    prefs.putBool("otaPending", true);
    updateVersion(remoteVersion);
    ESP.restart();
  } else {
    Update.printError(Serial);
    otaInProgress = false;   // OTA failed, allow sensors again
  }

  http.end();
}

void PerformTasksWithInternetConnectivity(){

    if (!profileLoaded) {
      if (currentMillis - lastprofileCheck >= profileCheckInterval) {
        lastprofileCheck = currentMillis;
        getDeviceProfile(deviceID);
      }
      if (!profileLoaded) {
        return; 
      }
    }
  
    if (currentMillis - lastSendTime >= oRefreshInterval) {
      lastSendTime = currentMillis;
      timestamp = getTimestamp();
      LocaltimeStamp = getLocaltimeStamp();
      oDataset = "IOTDeviceTelemetry";
      jsonPayload = buildJsonPayload(timestamp, LocaltimeStamp);
      sendToPowerAutomate(IOTDeviceTelemetry, jsonPayload, oDataset);
      if(sendToalternativeServer){
        sendToClassicASP(IOTDeviceTelemetryBackup, jsonPayload, oDataset);
      }
    }

    //Evaluate temperature threshold breach
    EvaluateTemperatureBreach();

    //check new EQ
    if (currentMillis - lastEQSendTime >= oEQInterval) {
        lastEQSendTime = currentMillis;
        // Start USGS tasks
        startUSGSTask();
    } 

    // Check if 1 hour has passed since the last OTA check
    if (currentMillis - lastOtaCheckTime >= otaInterval) {
      // It's time to check for OTA updates
      checkRemoteOta();
      // Update the last check time
      lastOtaCheckTime = currentMillis;
    }

    // Check periodic report based on sendPeriodicReport and interval
    checkPeriodicReport();
}

void checkPeriodicReport() {
  // Only proceed if periodic reports are enabled
  if (!sendPeriodicReport) {
    return;
  }

  // Convert PeriodicReportInterval from hours to milliseconds
  unsigned long reportIntervalMs = (unsigned long)(PeriodicReportInterval * 3600000UL);

  // Check if enough time has passed since last report
  if (currentMillis - lastPeriodicReportTime >= reportIntervalMs) {
    lastPeriodicReportTime = currentMillis;

    // Build the report message
    char message[768];   // adjust size if needed

    String tmp = getDeviceReadingsForTelegram();
    strncpy(message, tmp.c_str(), sizeof(message));
    message[sizeof(message) - 1] = '\0';   // safety null-termination

    // Send to all enabled chat IDs
    if (xSemaphoreTake(httpMutex, portMAX_DELAY)) {
      Serial.println("📊 Sending periodic report to Telegram...");
      enqueueTelegram(message);      
      Serial.println("✅ Periodic report sent successfully");
    } else {
      Serial.println("⚠️ Failed to acquire mutex for periodic report");
    }
  }
}

void updateVersion(String ver) {
  prefs.putString("version.current", ver);
  Serial.println("Saved version: " + ver);
}

void sendOtaSuccessMessage() {

  String prevVersion = prefs.getString("version.previous", "unknown");

  char message[1024];

  snprintf(message, sizeof(message),
        "💡 Device Is Now Online!\n"  
        "💡 OTA Update Completed!\n"
        "📍 Location: %s\n"
        "🛰️ Context: %s\n"
        "🆕 Version: %s\n"
        "📦 Previous: %s \n"
        "🔗 MAC ID: %s\n"
        "✅ Status: Success \n",
        oLocation.c_str(),
        oContext.c_str(),
        currentVersion,
        prevVersion,
        WiFi.macAddress().c_str()
  );

  // --- Send Telegram message ---
  Serial.println("🚨 Sending OTA success to Telegram...");
  //enqueueTelegram(message);
  bot.sendMessage(message, CHAT_ID1.c_str()); //IoTTelemetry  

}

void sendWakeupMessage() {

    // ============================================
    // 🔁 READ persisted reset diagnostics
    // ============================================
    uint32_t resetCount = prefs.getUInt("reset.count", 0);
    uint32_t lastReset  = prefs.getUInt("reset.last", 0);

    Serial.printf("🔁 Reset #%u, Reason=%u\n", resetCount, lastReset);

    char message[1024];

    snprintf(message, sizeof(message),
        "💡 Device Is Now Online!\n"
        "📍 Location: %s\n"
        "🛰️ Context: %s\n"
        "🆕 Version: %s\n"
        "🔗 MAC ID: %s\n"
        "🔄 Reset Reason: %s\n"
        "🔢 Reset Count: %u\n",
        oLocation.c_str(),
        oContext.c_str(),
        currentVersion,
        WiFi.macAddress().c_str(),
        bootResetCause.c_str(),
        resetCount
    );

    // --- Send Telegram message ---    
    Serial.println("🚨 Sending wakeup notification to Telegram...");
    //enqueueTelegram(message);
    bot.sendMessage(message, CHAT_ID1.c_str()); //IoTTelemetry

}

void sendLowHeapRestartNotice(uint32_t freeHeap) {

  char msg[256];

  snprintf(msg, sizeof(msg),
    "🚨 LOW MEMORY WARNING\n\n"
    "💾 Free Heap: %lu bytes\n"
    "🔄 Action: Controlled restart\n"
    "🧠 Reason: Heap below safe threshold",
    freeHeap
  );

  if (WiFi.status() == WL_CONNECTED){
    enqueueTelegram(msg);   // Markdown OFF
  }
}

String readVersion() {
  String ver = prefs.getString("version.current", "");
  return ver;
}

// ============================================================================
// Device Profile / Settings / Config Portal
// ============================================================================

void EvaluateforceConfigPortal() {

  if (!forceConfigPortal) return;

  forceConfigPortal = false;
  configMode = true;

  Serial.println("🛠 Launching config portal");

  wm.setSaveConnect(true);
  wm.setConfigPortalTimeout(300);
  wm.setConfigPortalBlocking(false);
  wm.setCaptivePortalEnable(true);
  wm.setSaveConfigCallback(saveConfigCallback);

  // Launches WiFi configuration access point used for initial device setup
  // or recovery mode when no saved WiFi credentials are available.
  wm.startConfigPortal(DEVICE_SETUP_AP_SSID, DEVICE_SETUP_AP_PASSWORD);

}

void stopConfigPortal() {

   if (!configMode) return;

  Serial.println("✅ Closing config portal");

  configMode = false;          // stop portal logic immediately
  wm.stopConfigPortal();       // teardown portal ONLY

   // 🚫 DO NOT touch WiFi here
}

void getDeviceProfile(String mac) {
  if (profileLoaded) return;
  Serial.println("Retrieving profile: " + mac);
  HTTPClient http;
  http.begin(IOTDeviceProfile);
  http.addHeader("Content-Type", "application/json");
  // Add this line to increase the timeout to 15 seconds
  http.setTimeout(15000);

  String requestBody = "{\"mac\":\"" + mac + "\"}";
  int httpCode = http.POST(requestBody);

  if (httpCode == 200) {
    String payload = http.getString();
    
    Serial.println(payload);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<512> doc;    
    #pragma GCC diagnostic pop    
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      oDivision = doc["Division"].as<String>();
      oDistrict = doc["District"].as<String>();
      oLocation = doc["Location"].as<String>();
      oContext = doc["Context"].as<String>();
      oCO2Base = doc["CO2Base"].as<float>();
      oTempThreshold = doc["TempThreshold"].as<float>();
      ACL_ON = doc["ACL_ON"].as<String>();
      ACL_OFF = doc["ACL_OFF"].as<String>();
      ACL_UP = doc["ACL_UP"].as<String>();
      ACL_DOWN = doc["ACL_DOWN"].as<String>();
      ACL_18 = doc["ACL_18"].as<String>();
      ACL_20 = doc["ACL_20"].as<String>();
      ACL_22 = doc["ACL_22"].as<String>();
      ACL_24 = doc["ACL_24"].as<String>();
      ACR_ON = doc["ACR_ON"].as<String>();
      ACR_OFF = doc["ACR_OFF"].as<String>();
      ACR_UP = doc["ACR_UP"].as<String>();
      ACR_DOWN = doc["ACR_DOWN"].as<String>();
      ACR_18 = doc["ACR_18"].as<String>();
      ACR_20 = doc["ACR_20"].as<String>();
      ACR_22 = doc["ACR_22"].as<String>();
      ACR_24 = doc["ACR_24"].as<String>();      

      AC_Control = doc["ACControl"].as<bool>();
      Wireless_Control = doc["WirelessControl"].as<bool>();

      WithDisplay = doc["WithDisplay"].as<bool>();
      DisplaySize = doc["DisplaySize"].as<String>();      

      oTempOffset = doc["TempOffset"].as<float>();
      oHumidOffset  = doc["HumidOffset"].as<float>();

      sendEQAlert = doc["EQ_Alert"].as<bool>();      
      quakeRadiusKm = doc["EQ_Distance"].as<float>();
      minMagnitude = doc["EQ_Magnitude"].as<float>();
      deviceLat = doc["Latitude"].as<float>();
      deviceLon = doc["Longitude"].as<float>();

      WithGyrometer = doc["WithGyrometer"].as<bool>();    

      //Commented since will now be autodetected
      //String gyroTypeTemp = doc["GyroModel"].as<String>();

      UsesDHTSensor = doc["UsesDHTSensor"].as<bool>();    

      CHATIDADMIN_ENABLED = doc["CHATIDADMIN_ENABLED"].as<bool>();    
      CHAT_ID_ENABLED = doc["CHAT_ID_ENABLED"].as<bool>();    
      CHAT_ID_ADMIN_ENABLED = doc["CHAT_ID_ADMIN_ENABLED"].as<bool>();  

      strncpy(IOTDeviceTelemetry, doc["PAFlowLink"] | "", sizeof(IOTDeviceTelemetry));
      IOTDeviceTelemetry[sizeof(IOTDeviceTelemetry) - 1] = '\0';   // Safety null-terminate

      sendToalternativeServer = doc["SendToAlternativeServer"].as<bool>();

      //Commented since will now be based on user preference via Telegram command
      //sendPeriodicReport= doc["SendPeriodicReport"].as<bool>();
      //PeriodicReportInterval = doc["PeriodicReportInterval"].as<float>();      

      // Parse all strings into arrays
      len_ACL_ON   = parseStringToArray(ACL_ON, arr_ACL_ON, MAX_VALUES);
      len_ACL_OFF  = parseStringToArray(ACL_OFF, arr_ACL_OFF, MAX_VALUES);
      len_ACL_UP   = parseStringToArray(ACL_UP, arr_ACL_UP, MAX_VALUES);
      len_ACL_DOWN = parseStringToArray(ACL_DOWN, arr_ACL_DOWN, MAX_VALUES);
      len_ACL_18 = parseStringToArray(ACL_18, arr_ACL_18, MAX_VALUES);
      len_ACL_20 = parseStringToArray(ACL_20, arr_ACL_20, MAX_VALUES);
      len_ACL_22 = parseStringToArray(ACL_22, arr_ACL_22, MAX_VALUES);
      len_ACL_24 = parseStringToArray(ACL_24, arr_ACL_24, MAX_VALUES);
      len_ACR_ON   = parseStringToArray(ACR_ON, arr_ACR_ON, MAX_VALUES);
      len_ACR_OFF  = parseStringToArray(ACR_OFF, arr_ACR_OFF, MAX_VALUES);
      len_ACR_UP   = parseStringToArray(ACR_UP, arr_ACR_UP, MAX_VALUES);
      len_ACR_DOWN = parseStringToArray(ACR_DOWN, arr_ACR_DOWN, MAX_VALUES);
      len_ACR_18 = parseStringToArray(ACR_18, arr_ACR_18, MAX_VALUES);
      len_ACR_20 = parseStringToArray(ACR_20, arr_ACR_20, MAX_VALUES);
      len_ACR_22 = parseStringToArray(ACR_22, arr_ACR_22, MAX_VALUES);
      len_ACR_24 = parseStringToArray(ACR_24, arr_ACR_24, MAX_VALUES);      
      //free up memory
      ACL_ON = "";
      ACL_OFF = "";
      ACL_UP = "";
      ACL_DOWN = "";
      ACL_18 = "";
      ACL_20 = "";
      ACL_22 = "";
      ACL_24 = "";

      ACR_ON = "";
      ACR_OFF = "";
      ACR_UP = "";
      ACR_DOWN = "";
      ACR_18 = "";
      ACR_20 = "";
      ACR_22 = "";
      ACR_24 = "";

      String CHATID = doc["CHAT_ID"].as<String>();
      if (CHATID!="") {
        CHAT_ID1 = CHATID;
      }  

      String CHATID_ADMIN = doc["CHAT_ID_ADMIN"].as<String>();
      if (CHATID_ADMIN!="") {
        CHAT_ID2 = CHATID_ADMIN;
      }  

      if (oDivision.length() > 0 && oDistrict.length() > 0 && oLocation.length() > 0 && oContext.length() > 0) {
        sendToServer = true;
        profileLoaded = true;
        Serial.println("✅ Profile loaded successfully!");
        Serial.println("✅ Device profile: " + oDivision + ", " + oDistrict + ", " + oLocation + ", " + oContext);
        localWebhost = oContext;
        localWebhost.replace("Room", "");
        localWebhost.replace("room", "");
        localWebhost.replace("Office", "");
        localWebhost.replace("office", "");
        localWebhost.replace(" ", "");
        localWebhost.toLowerCase();
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      } else {
        Serial.println("⚠️ Incomplete device profile data!");
      }
    } else {
      Serial.println("❌ JSON Parse Error!");
    }
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(httpCode);
  }
  http.end();

}

void loadSettings() {
  // --- Read stored refresh interval ---
  unsigned long storedInterval = prefs.getULong("refresh.interval", 0);

  // Sanity check: < 1 hour
  if (storedInterval > 0 && storedInterval < 3600000) {
    oRefreshInterval = storedInterval;
  }

  Serial.println("Refresh Interval (ms): " + String(oRefreshInterval));
}

void saveConfigCallback() {
  Serial.println("📡 Config saved, exiting portal...");
  configMode = false;
}

void saveInterval(unsigned long interval) {
  prefs.putULong("refresh.interval", interval);
  Serial.println("Saved new interval: " + String(interval));
}

// ============================================================================
// TFT Display
// ============================================================================
// --- Draw centered IoTTelemetry header ---
void drawHeader() {
  //tft.setTextSize(2);
  tft.setFreeFont(&FreeMonoOblique9pt7b);  
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(tft.color565(fadeValue, 255, 255 - fadeValue), TFT_BLACK);
  tft.drawString("IoTTelemetry", tft.width() / 2, 20);
}

// --- Draw centered room name ---
void drawRoom() {
  //tft.setTextSize(2);
  tft.setFreeFont(&FreeMonoOblique9pt7b);    
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("FasMan Room", tft.width() / 2, 50);
}

// --- Draw label + value pair ---
void drawLabelValue(const char* label, String value, uint16_t labelColor, uint16_t valueColor, int y) {

  tft.setFreeFont(&FreeMonoOblique9pt7b);  

  tft.setTextDatum(TL_DATUM);
  // Draw label (fixed color)
  tft.setTextColor(labelColor, TFT_BLACK);
  tft.drawString(label, labelX, y);

  // Clear only the value area before updating
  tft.fillRect(valueX, y, tft.width() - valueX - 5, lineHeight, TFT_BLACK);
  // Draw value (dynamic color)
  tft.setTextColor(valueColor, TFT_BLACK);
  tft.drawString(value, valueX, y);
}

// --- Helper: format temperature values with proper degree symbol ---
String formatValue(float val, const char* unit) {
  return String(val, 2) + "°" + unit;  // Use the actual degree symbol directly
}

uint16_t getTFTColor(float value, float warn, float danger) {
  if (value >= danger) return TFT_RED;     // danger
  if (value >= warn)   return TFT_ORANGE;  // warning
  return TFT_GREEN;                        // normal
}

// --- Update values dynamically ---
void updateValues(float tmp, float hum, float hi, float ace, float alc, float co, float co2, float nh3, float tol) {
  int y = 90;

  drawLabelValue("TMP:", formatValue(tmp, "C"), TFT_RED,     getTFTColor(tmp, 30, 40), y);   y += lineHeight;
  drawLabelValue("HUM:", String(hum, 2) + "%",  TFT_BLUE,    getTFTColor(hum, 70, 90), y);   y += lineHeight;
  drawLabelValue("HI :", formatValue(hi, "C"),  TFT_MAGENTA, getTFTColor(hi, 30, 40), y);    y += lineHeight;
  drawLabelValue("ACE:", String(ace, 2) + "ppm", TFT_GREEN,  getTFTColor(ace, 200, 400), y); y += lineHeight;
  drawLabelValue("ALC:", String(alc, 2) + "ppm", TFT_ORANGE, getTFTColor(alc, 200, 400), y); y += lineHeight;
  drawLabelValue("CO :", String(co, 2) + "ppm",  TFT_YELLOW, getTFTColor(co, 20, 50), y);    y += lineHeight;
  drawLabelValue("CO2:", String(co2, 2) + "ppm", TFT_CYAN,   getTFTColor(co2, 800, 1500), y); y += lineHeight;
  drawLabelValue("NH3:", String(nh3, 2) + "ppm", TFT_PINK,   getTFTColor(nh3, 25, 50), y);   y += lineHeight;
  drawLabelValue("TOL:", String(tol, 2) + "ppm", TFT_WHITE,  getTFTColor(tol, 200, 400), y);
}

// ============================================================================
// JSON / Payload / HTTP / REST
// ============================================================================
String buildJsonPayload(String timestamp, String localTimestamp) {

  String titles[] = {"RoomAceton", "RoomAlcohol", "RoomCO", "RoomCO2", "RoomHeatIndex",
                     "RoomHumidity", "RoomNH3", "RoomTemperature", "RoomToluen"};
  float values[] = {AcetonPPM, AlcoholPPM, COPPM, CO2PPM, dhtheatindex, dhthumidity, NH3PPM, dhttemperature, ToluenPPM};

  int count = sizeof(titles) / sizeof(titles[0]);  // 9

  String jsonPayload = "{";
  jsonPayload += "\"DeviceId\":\"" + deviceID + "\",";
  jsonPayload += "\"Readings\":[";

  for (int i = 0; i < count; i++) {
    jsonPayload += "{";
    jsonPayload += "\"TimeStamp\":\"" + timestamp + "\",";
    jsonPayload += "\"LocalTimeStamp\":\"" + localTimestamp + "\",";
    jsonPayload += "\"Title\":\"" + titles[i] + "\",";
    jsonPayload += "\"Division\":\"" + oDivision + "\",";
    jsonPayload += "\"District\":\"" + oDistrict + "\",";
    jsonPayload += "\"Location\":\"" + oLocation + "\",";
    jsonPayload += "\"Context\":\"" + oContext + "\",";
    jsonPayload += "\"Value\":" + String(values[i], 2);
    jsonPayload += "}";

    if (i < count - 1) jsonPayload += ","; // Add comma except after last object
  }

  jsonPayload += "]}";
  return jsonPayload;
}

void sendToPowerAutomate(const char* url, const String& payload, const String& dataset) {
  
  Serial.println("⏳Sending JSON:" + dataset);

  HTTPClient http;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    Serial.print(dataset + " data sent to Power BI! Response: ");
  } else {
    Serial.print(dataset + " error sending data: ");
  }

  Serial.println(httpResponseCode);
  http.end();
}

void sendToClassicASP(const char* url, const String& payload, const String& dataset) {
  
  Serial.println("⏳Sending JSON to Classic ASP: " + dataset);

  HTTPClient http;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");  // ASP will parse JSON from body

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    Serial.print(dataset + " data sent to Classic ASP! Response: ");
  } else {
    Serial.print(dataset + " error sending data: ");
  }

  Serial.println(httpResponseCode);
  http.end();
}

// ============================================================================
// Telegram Bot
// ============================================================================
void initTelegram() {
  bot.setTextMode(FB_TEXT);
  bot.attach(handleBotMessage);
  Serial.println("🤖 Telegram bot initialized");
}

void EvaluateTemperatureBreach(){
  char message[512];
  char safeLocation[128];
  char safeContext[128];    

  if (dhttemperature > oTempThreshold && !warningSent) {
    // Send a warning message if the threshold is exceeded

    snprintf(message, sizeof(message),
        "⚠️ Warning:\n"
        "%s\n"
        "%s\n"
        "🌡️ Temperature exceeded the threshold of %.1f°C!\n"
        "🌡️ Current temperature: %.1f°C",
        oLocation.c_str(),
        oContext.c_str(),
        oTempThreshold,
        dhttemperature
    );
    enqueueTelegram(message);      
    // Mark that the warning has been sent
    warningSent = true;
    // Reset restoredSent flag when a breach occurs
    restoredSent = false;    
    lastWarningTime = currentMillis; // Log the time when the warning was sent

    // Log the sending of the warning
    Serial.println("Warning message sent to Telegram.");
  }

  // Periodically check every 15 minutes (900,000 milliseconds) if the temperature is still high
  if (warningSent && currentMillis - lastWarningTime >= warningInterval) {
    if (dhttemperature > oTempThreshold) {
      // Send another warning if the temperature is still above the threshold
      snprintf(message, sizeof(message),
          "⚠️ Reminder:\n"
          "%s\n"
          "%s\n"
          "🌡️ Temperature is still above the threshold!\n"
          "🌡️ Current temperature: %.1f°C",
          safeLocation,
          safeContext,
          oTempThreshold,
          dhttemperature
      );
      enqueueTelegram(message);            
      // Log the sending of the periodic warning
      lastWarningTime = currentMillis;
      Serial.println("Warning message sent to Telegram.");
    } else {
     // If the temperature is back to normal, send a confirmation message once
      if (!restoredSent) { 
        snprintf(message, sizeof(message),
            "✅ Normal:\n"
            "%s\n"
            "%s\n"
            "🌡️ Temperature is back to normal. Warning reset.\\n"
            "🌡️ Current temperature: %.1f°C",
            safeLocation,
            safeContext,
            oTempThreshold,
            dhttemperature
        );
        enqueueTelegram(message);                                 
        restoredSent = true; // Prevent sending this message again

        // Log the restoration message
        Serial.println("Temperature is back to normal. Warning reset. Current temperature: " + String(dhttemperature) + "°C");
      }      
      // If the temperature is back to normal, reset the warning system
      warningSent = false;
      Serial.println("Temperature is back to normal. Warning reset. Current temperature: " + String(dhttemperature) + "°C");
    }
  }  
}

String getDeviceReadingsForTelegram() {
    int minutes = oRefreshInterval / 60000;

    String sendReportStatus = "Disabled";
    if (sendPeriodicReport) {
        sendReportStatus = String(PeriodicReportInterval, 1) + " Hr";
    }

    char message[2048];

    snprintf(message, sizeof(message),
        "%s\n%s\n"
        "\n"
        "📟 Sensor Readings:\n"
        "\n"
        "🌡️ Temperature: %.2f°C %s\n"
        "💧 Humidity: %.2f%% %s\n"
        "🥵 Heat Index: %.2f°C %s\n"
        "🧪 Acetone: %.2f ppm %s\n"
        "🍷 Alcohol: %.2f ppm %s\n"
        "🚗 CO: %.2f ppm %s\n"
        "🌍 CO₂: %.2f ppm %s\n"
        "⚗️ NH₃: %.2f ppm %s\n"
        "🧪 Toluene: %.2f ppm %s\n"
        "\n"
        "📟 Sensor Levels:\n"
        "\n"
        "🌡️ Temperature: %s\n"
        "💧 Humidity: %s\n"
        "🥵 Heat Index: %s\n"
        "🧪 Acetone: %s\n"
        "🍷 Alcohol: %s\n"
        "🚗 CO: %s\n"
        "🌍 CO₂: %s\n"
        "⚗️ NH₃: %s\n"
        "🧪 Toluene: %s\n"
        "\n"
        "🛰️ Sensor Info:\n"
        "\n"
        "🛠️ Firmware Version: %s\n"
        "🔗 MAC ID: %s\n"
        "⏲️ Upload Interval: %d min\n"
        "⏲️ Auto Reporting: %s\n",

        oLocation.c_str(),
        oContext.c_str(),

        dhttemperature, Get_Level_TemperatureEmoji(dhttemperature, true),
        dhthumidity,    Get_Level_HumidityEmoji(dhthumidity, true),
        dhtheatindex,   Get_Level_HeatIndexEmoji(dhtheatindex, true),

        AcetonPPM,  Get_Level_AcetonEmoji(AcetonPPM, true),
        AlcoholPPM, Get_Level_AlcoholEmoji(AlcoholPPM, true),
        COPPM,      Get_Level_COEmoji(COPPM, true),
        CO2PPM,     Get_Level_CO2Emoji(CO2PPM, true),
        NH3PPM,     Get_Level_NH3Emoji(NH3PPM, true),
        ToluenPPM,  Get_Level_TolueneEmoji(ToluenPPM, true),

        Get_Level_TemperatureEmoji(dhttemperature, false),
        Get_Level_HumidityEmoji(dhthumidity, false),
        Get_Level_HeatIndexEmoji(dhtheatindex, false),
        Get_Level_AcetonEmoji(AcetonPPM, false),
        Get_Level_AlcoholEmoji(AlcoholPPM, false),
        Get_Level_COEmoji(COPPM, false),
        Get_Level_CO2Emoji(CO2PPM, false),
        Get_Level_NH3Emoji(NH3PPM, false),
        Get_Level_TolueneEmoji(ToluenPPM, false),

        currentVersion,
        WiFi.macAddress().c_str(),
        minutes,
        sendReportStatus.c_str()
    );

    return String(message);
}

String getDeviceReadingsForTelegramSpecific(String cmd) {

    String header = String(oLocation.c_str()) + "\n" + String(oContext.c_str()) + "\n\n";
    String body;

    // Use startsWith to allow trailing chat IDs
    if (cmd.startsWith("BotReport - T")) {
        body = "🌡️ *Temperature:* " + String(dhttemperature, 2) + "°C\n";
    }
    else if (cmd.startsWith("BotReport - HI")) {  // check HI before H
        body = "🥵 *Heat Index:* " + String(dhtheatindex, 2) + "°C\n";
    }
    else if (cmd.startsWith("BotReport - H")) {
        body = "💧 *Humidity:* " + String(dhthumidity, 2) + "%\n";
    }
    else if (cmd.startsWith("BotReport - ACE")) {
        body = "🧪 *Acetone:* " + String(AcetonPPM, 2) + " ppm\n";
    }
    else if (cmd.startsWith("BotReport - ALC")) {
        body = "🍷 *Alcohol:* " + String(AlcoholPPM, 2) + " ppm\n";
    }
    else if (cmd.startsWith("BotReport - CO1")) {
        body = "🚗 *CO:* " + String(COPPM, 2) + " ppm\n";
    }
    else if (cmd.startsWith("BotReport - NH3")) {
        body = "⚗️ *NH₃:* " + String(NH3PPM, 2) + " ppm\n";
    }
    else if (cmd.startsWith("BotReport - TOL")) {
        body = "🛢️ *Toluene:* " + String(ToluenPPM, 2) + " ppm\n";
    }
    else if (cmd.startsWith("BotReport - VERSION")) {
        body = "🛠️ *Firmware Version:* " + String(currentVersion) + "\n";
    }
    else {
        body = "❓ Unknown command: " + cmd + "\n";
    }

    return header + body;
}

void handleBotMessage(FB_msg& msg) {

  String text = msg.text;
  text.trim();

  //Serial.println("📩 Command Received: " + text + " | From: " + msg.chatID + " | ChatID1: " + CHAT_ID1 + " | ChatID2: " + CHAT_ID2 + " | ChatID: " + CHAT_ID );
  //Serial.println(IOTDeviceTelemetry);

  String thisMAC = WiFi.macAddress();

  /*****************************************************************
   * 🟠 USE CASE 3 — INLINE MENUS (IMMEDIATE, USER-TRIGGERED ONLY)
   *****************************************************************/
  if (text == "/BotReport") {

    String message = getDeviceReadingsForTelegram();
    String thisMAC = WiFi.macAddress();

    /* =========================================================
    * ROLE 1 — FULL CONTROL (INLINE MENU)
    * ========================================================= */
    if (msg.chatID == CHAT_ID || msg.chatID == CHAT_ID2) {

      String menu = "Cfg \t Rst \t Upd \t Rpt";
      String callbacks = "config_" + thisMAC +
                        ",restart_" + thisMAC +
                        ",update_" + thisMAC +
                        ",report_" + thisMAC;

      if (AC_Control) {
        menu += "\n On-L \t Off-L \t Up-L \t Dwn-L";
        menu += "\n 18-L \t 20-L \t 22-L \t 24-L";
        menu += "\n On-R \t Off-R \t Up-R \t Dwn-R";
        menu += "\n 18-R \t 20-R \t 22-R \t 24-R";

        callbacks += ",ONL_" + thisMAC + ",OFFL_" + thisMAC + ",UPL_" + thisMAC + ",DOWNL_" + thisMAC;
        callbacks += ",18L_" + thisMAC + ",20L_" + thisMAC + ",22L_" + thisMAC + ",24L_" + thisMAC;
        callbacks += ",ONR_" + thisMAC + ",OFFR_" + thisMAC + ",UPR_" + thisMAC + ",DOWNR_" + thisMAC;
        callbacks += ",18R_" + thisMAC + ",20R_" + thisMAC + ",22R_" + thisMAC + ",24R_" + thisMAC;
      }

      // UI response (immediate)
      bot.inlineMenuCallback(message, menu, callbacks, msg.chatID);
      return;
    }

    /* =========================================================
    * ROLE 2 — READ-ONLY (TEXT ONLY)
    * ========================================================= */
    if (msg.chatID == CHAT_ID1) {

      // Simple message, no controls
      bot.sendMessage(message, msg.chatID);
      return;
    }

    /* =========================================================
    * ROLE 3 — UNAUTHORIZED (SILENT IGNORE)
    * ========================================================= */
    return;
  }


  /*****************************************************************
   * 🔵 USE CASE 2 — DIRECT USER COMMAND REPLIES (QUEUED)
   *****************************************************************/
  if (text.startsWith("/BotReport -") && (msg.chatID == CHAT_ID1 || msg.chatID == CHAT_ID2)) {

    if (text.startsWith("/"))
      text = text.substring(1);

    char reply[512];
    String tmp = getDeviceReadingsForTelegramSpecific(text);
    strncpy(reply, tmp.c_str(), sizeof(reply));
    reply[sizeof(reply) - 1] = '\0';

    enqueueTelegram(reply, msg.chatID.c_str());
    return;
  }

  if (text.startsWith("/BotReport Periodic") && (msg.chatID == CHAT_ID1 || msg.chatID == CHAT_ID2)) {

    char response[512];

    snprintf(response, sizeof(response),
            "%s\n%s\n\n", oLocation.c_str(), oContext.c_str());

    // ----------------------------
    // PERIODIC ON
    // ----------------------------
    if (text.indexOf("ON") != -1) {

      // Default interval (hours)
      float hours = 2.0;

      // Try to parse custom interval
      int onIndex = text.indexOf("ON");
      if (onIndex != -1) {
        String tail = text.substring(onIndex + 2);
        tail.trim();
        if (tail.length() > 0) {
          float parsed = tail.toFloat();
          if (parsed > 0.0) {
            hours = parsed;
          }
        }
      }

      // Apply runtime state
      sendPeriodicReport = true;
      PeriodicReportInterval = hours;
      lastPeriodicReportTime = currentMillis;

      // Persist intent
      prefs.putBool(PREF_PERIODIC_ENABLED, true);
      prefs.putFloat(PREF_PERIODIC_HOURS, hours);

      snprintf(response + strlen(response),
              sizeof(response) - strlen(response),
              "✅ Periodic Report Enabled\n⏱️ Interval: %.1f hours",
              hours);
    }

    // ----------------------------
    // PERIODIC OFF
    // ----------------------------
    else if (text.indexOf("OFF") != -1) {

      sendPeriodicReport = false;

      // Persist intent
      prefs.putBool(PREF_PERIODIC_ENABLED, false);

      strcat(response, "❌ Periodic Report Disabled");
    }

    // ----------------------------
    // INVALID USAGE
    // ----------------------------
    else {
      strcat(response,
            "❓ Invalid command\n\n"
            "/BotReport Periodic ON [hours]\n"
            "/BotReport Periodic OFF");
    }

    enqueueTelegram(response, msg.chatID.c_str());
    return;
  }

  if (text == "/Force Update C@tchmeifyoucan@10000" && msg.chatID == CHAT_ID) {
    otaPending = true;
    enqueueTelegram("⬆️ OTA update scheduled", msg.chatID.c_str());
    return;
  }

  if (text.startsWith("/SetRefresh") && (msg.chatID == CHAT_ID1 || msg.chatID == CHAT_ID2)) {

    int spaceIndex = text.indexOf(' ');
    if (spaceIndex > 0) {
      int minutes = text.substring(spaceIndex + 1).toInt();

      if (minutes >= 1 && minutes <= 60) {
        oRefreshInterval = minutes * 60000UL;
        saveInterval(oRefreshInterval);

        char msgBuf[128];
        snprintf(msgBuf, sizeof(msgBuf),
                 "✅ Refresh interval set to %d min.", minutes);

        enqueueTelegram(msgBuf, msg.chatID.c_str());
      }
      else {
        enqueueTelegram("⚠️ Invalid value. Use: /SetRefresh <1–60>",
                        msg.chatID.c_str());
      }
    }
    return;
  }

  /*****************************************************************
   * 🟢 USE CASE 1 — CALLBACK BUTTONS (IMMEDIATE + FLAGGED ACTIONS)
   *****************************************************************/
  if (msg.query) {

    if (msg.data.endsWith(thisMAC)) {

      if (msg.data.startsWith("config_")) {
        bot.answer("Entering WiFi Config Mode", FB_ALERT);
        forceConfigPortal = true;
      }
      else if (msg.data.startsWith("restart_")) {
        bot.answer("Restart scheduled", FB_ALERT);
        restartRequested = true;
      }
      else if (msg.data.startsWith("update_")) {
        bot.answer("OTA scheduled", FB_ALERT);
        forceOTACheck = true;
      }
      else if (msg.data.startsWith("report_")) {
        char buf[768];
        String tmp = getDeviceReadingsForTelegram();
        strncpy(buf, tmp.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        enqueueTelegram(buf, msg.chatID.c_str());
      }

      // AC IR controls (local + safe)
      else if (AC_Control) {
        if (msg.data.startsWith("ONL_")) sendIR("ACL_ON");
        else if (msg.data.startsWith("OFFL_")) sendIR("ACL_OFF");
        else if (msg.data.startsWith("UPL_")) sendIR("ACL_UP");
        else if (msg.data.startsWith("DOWNL_")) sendIR("ACL_DOWN");
        else if (msg.data.startsWith("18L_")) sendIR("ACL_18");
        else if (msg.data.startsWith("20L_")) sendIR("ACL_20");
        else if (msg.data.startsWith("22L_")) sendIR("ACL_22");
        else if (msg.data.startsWith("24L_")) sendIR("ACL_24");

        else if (msg.data.startsWith("ONR_")) sendIR("ACR_ON");
        else if (msg.data.startsWith("OFFR_")) sendIR("ACR_OFF");
        else if (msg.data.startsWith("UPR_")) sendIR("ACR_UP");
        else if (msg.data.startsWith("DOWNR_")) sendIR("ACR_DOWN");
        else if (msg.data.startsWith("18R_")) sendIR("ACR_18");
        else if (msg.data.startsWith("20R_")) sendIR("ACR_20");
        else if (msg.data.startsWith("22R_")) sendIR("ACR_22");
        else if (msg.data.startsWith("24R_")) sendIR("ACR_24");

        bot.answer("✅ Command executed", FB_ALERT);
      }
    }
  }
}

// Helper to check if a command is one of the ACL group
bool isACLCommand(const String& cmd) {
  const char* aclCmds[] = {"ACL_ON", "ACL_OFF", "ACL_UP", "ACL_DOWN", "ACL_18", "ACL_20", "ACL_22", "ACL_24"};
  for (auto& acl : aclCmds) {
    if (cmd.equals(acl)) return true;
  }
  return false;
}

bool enqueueTelegram(const char* msg, const char* targetChatID) {
  int next = (tgHead + 1) % TG_QUEUE_SIZE;
  if (next == tgTail) {
    Serial.println("⚠️ Telegram queue full, dropping message");
    return false;
  }

  strncpy(tgQueue[tgHead].text, msg, sizeof(tgQueue[tgHead].text));
  tgQueue[tgHead].text[sizeof(tgQueue[tgHead].text) - 1] = '\0';

  if (targetChatID) {
    strncpy(tgQueue[tgHead].chatID, targetChatID, sizeof(tgQueue[tgHead].chatID));
    tgQueue[tgHead].chatID[sizeof(tgQueue[tgHead].chatID) - 1] = '\0';
  } else {
    tgQueue[tgHead].chatID[0] = '\0';  // broadcast
  }

  tgHead = next;
  return true;
}

void processTelegramQueue() {
  static unsigned long lastSend = 0;
  if (millis() - lastSend < 1500) return;
  if (tgTail == tgHead) return;

  if (!xSemaphoreTake(httpMutex, 0)) return;

  TgMessage &m = tgQueue[tgTail];

  // 🔵 Direct reply
  if (m.chatID[0] != '\0') {
    bot.sendMessage(m.text, m.chatID);
  }
  // 🟢 Broadcast
  else {
    if (CHAT_ID_ENABLED) {
      bot.sendMessage(m.text, CHAT_ID1.c_str());
      delay(250);
    }
    if (CHAT_ID_ADMIN_ENABLED) {
      bot.sendMessage(m.text, CHAT_ID2.c_str());
      delay(250);
    }
    if (CHATIDADMIN_ENABLED) {
      bot.sendMessage(m.text, CHAT_ID.c_str());
    }
  }

  tgTail = (tgTail + 1) % TG_QUEUE_SIZE;
  lastSend = millis();

  xSemaphoreGive(httpMutex);
}

// ============================================================================
// Earthquake Monitoring (USGS)
// ============================================================================

const char* wifiStatusStr() {
    switch (WiFi.status()) {
        case WL_CONNECTED: return "CONNECTED";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

void logHttpError(int code) {
    if (code == -1) {
        Serial.println("🌐 Network error: connection/TLS/DNS failure");
    } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
        Serial.println("⏱️ Network timeout waiting for response");
    } else {
        Serial.printf("🌐 HTTP error code: %d\n", code);
    }
}

// --- Main EQ alert function ---
void checkEQAlerts(String source) {
    Serial.println("🌐 Checking " + source + " alerts...");

    HTTPClient http;
    isUSGSFetching = true;

    http.begin(usgsAPI);

    int httpCode = http.GET();
    if (httpCode != 200) {
        logHttpError(httpCode);
        Serial.printf("📶 RSSI: %d dBm | WiFi: %s\n",
                      WiFi.RSSI(), wifiStatusStr());
        http.end();
        isUSGSFetching = false;
        shakeNotificationSent = false;
        return;
    }

    String payload = http.getString();
    http.end();
    delay(50);  // allow lwIP to free buffers

    if (payload.length() < 200) {
      Serial.println("⚠️ USGS payload too small, skipping parse");
      isUSGSFetching = false;
      shakeNotificationSent = false;
      return;
    }

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    StaticJsonDocument<512> filter;
    #pragma GCC diagnostic pop       
    

    filter["features"][0]["properties"]["mag"] = true;
    filter["features"][0]["properties"]["place"] = true;
    filter["features"][0]["properties"]["time"] = true;
    filter["features"][0]["properties"]["url"] = true;
    filter["features"][0]["geometry"]["coordinates"][0] = true;
    filter["features"][0]["geometry"]["coordinates"][1] = true;
    filter["features"][0]["geometry"]["coordinates"][2] = true;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    DynamicJsonDocument doc(8 * 1024);   // 8 KB is enough WITH filter
    #pragma GCC diagnostic pop    

    DeserializationError error =
    deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    if (error) {
        Serial.print("❌ JSON parse failed: ");
        Serial.println(error.f_str());
        isUSGSFetching = false;
        shakeNotificationSent = false;
        return;
    }

    JsonArray features = (doc)["features"].as<JsonArray>();
    if (features.size() == 0) {
        Serial.println("No earthquakes in feed");
        isUSGSFetching = false;
        shakeNotificationSent = false;
        return;
    }

    JsonObject latest = features[0];

    String place = latest["properties"]["place"] | "Unknown Location";

    float mag = latest["properties"]["mag"] | NAN;
    float depth = latest["geometry"]["coordinates"][2] | NAN;
    long time_ms = latest["properties"]["time"] | 0;

    float lon = latest["geometry"]["coordinates"][0] | NAN;
    float lat = latest["geometry"]["coordinates"][1] | NAN;

    bool hasValidCoords =
        !latest["geometry"]["coordinates"][0].isNull() &&
        !latest["geometry"]["coordinates"][1].isNull();

    // --- ✅ PH BOUNDARY CHECK FIRST ---
    const float PH_LAT_MIN = 4.0f;
    const float PH_LAT_MAX = 21.5f;
    const float PH_LON_MIN = 116.0f;
    const float PH_LON_MAX = 127.5f;

    bool inPhilippines = hasValidCoords &&
                         !isnan(lat) && !isnan(lon) &&
                         lat >= PH_LAT_MIN && lat <= PH_LAT_MAX &&
                         lon >= PH_LON_MIN && lon <= PH_LON_MAX;


    //for debug purposes only
    //###########################################
    //inPhilippines = true;
    //###########################################

    if (!inPhilippines) {
        Serial.println("⏭️ Earthquake outside Philippine boundaries — ignored.");
        Serial.println("   Shaking logs preserved for future matching.");
        isUSGSFetching = false;
        shakeNotificationSent = false;  // ✅ Keep waiting for PH earthquake
        return;  // ✅ DO NOT clear shake logs
    }

    // --- Earthquake is in Philippines, continue processing ---
    char mapsLink[128];
    snprintf(mapsLink, sizeof(mapsLink),
            "https://www.google.com/maps/place/%.5f,%.5f/@%.5f,%.5f,8z",
            lat, lon, lat, lon);


    String sourceLink = latest["properties"]["url"] | "";

    String sLocation = oLocation;

    String sContext = oContext;

    time_t t = time_ms / 1000;
    t += 8 * 3600;

    struct tm* tm_info = gmtime(&t);
    char timeStr[32];
    if (tm_info)
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S PHT", tm_info);
    else
        strcpy(timeStr, "Unknown Local Time");

    String eqTime = timeStr;

    String eqID;
    if (hasValidCoords)
        eqID = String(lat, 2) + "," + String(lon, 2) + "," + String(mag, 1);
    else
        eqID = place + "," + String(mag, 1) + "," + eqTime;

    char buf[128];
    prefs.getString("eq.lastUSGSID", buf, sizeof(buf));
    String lastID = buf;

    // ======================================================
    // 🟢 FEED-ONLY DEVICE (NO IMU / NO GYRO)
    // ======================================================
    if (!WithGyrometer) {

        if (eqID != lastID && mag >= alertMagnitude) {

            prefs.putString("eq.lastUSGSID", eqID.c_str());

            Serial.println("🚨 Feed-only EQ device: Magnitude threshold met.");
            Serial.println("🚨 Sending earthquake alert to Telegram...");

            float distance = NAN;
            if (hasValidCoords && !isnan(lat) && !isnan(lon)) {
                distance = haversine(deviceLat, deviceLon, lat, lon);
            }

            String distanceStr = isnan(distance) ?
                                "Unknown" :
                                String(distance, 1) + " km";

            // --- Build Telegram Alert Message ---
            const size_t MSG_SZ = 2500;
            static char message[MSG_SZ];
            memset(message, 0, MSG_SZ);

            snprintf(message, MSG_SZ,
                "⚠️ Earthquake Alert (Feed-Based)\n\n"
                "🌋 Source: %s\n"
                "📍 Location: %s\n"
                "💥 Magnitude: %.1f\n"
                "⬇️ Depth: %.1f km\n"
                "📏 Distance: %s from Device\n"
                "🕓 Time: %s\n"
                "📡 Device Location: %s\n"
                "💬 Device Context: %s\n"
                "🗺️ [View Epicenter](%s)\n"
                "🔗 [Open %s Page](%s)",
                source.c_str(),
                place.c_str(),
                mag,
                depth,
                distanceStr.c_str(),
                eqTime.c_str(),
                oLocation.c_str(),
                sContext.c_str(),
                mapsLink,
                source.c_str(),
                sourceLink.c_str()
            );

            enqueueTelegram(message);
        } else {
            Serial.printf(
                "[FEED] Skipped | mag=%.1f (limit=%.1f) | eqID=%s | lastID=%s\n",
                mag,
                alertMagnitude,
                eqID.c_str(),
                lastID.c_str()
            );
        }

        // 🚫 EXIT EARLY — skip all IMU/shake logic
        isUSGSFetching = false;
        shakeNotificationSent = false;
        return;
    }

    // ======================================================
    // 🟢 IF WITH GYRO, EXECUTE THIS
    // ======================================================
    if (eqID != lastID || shakeIndex > 0) {
        prefs.putString("eq.lastUSGSID", eqID.c_str());

        float distance = NAN;
        if (hasValidCoords && !isnan(lat) && !isnan(lon)) {
            distance = haversine(deviceLat, deviceLon, lat, lon);
        }

        String distanceStr = isnan(distance) ?
                             "Unknown" :
                             String(distance, 1) + " km";

        QuakeEstimate q = estimateQuakeIntensity(mag, depth, distance);

        if (imuReady ) {
            if (mag < 5.0) {
                clearShakeLog();
            }
            imuReady  = false;
            shakeIndex = 0;
        }

        // ✅ NEW: TIME & MAGNITUDE CORRELATION
        bool isMatchingEQ = false;
        if (shakeAwaitingEQ && shakeIndex > 0) {
            // Check if earthquake timestamp is within shake window
            long eqAge_ms = currentMillis - firstShakeTime;  // ms since first shake
            long eqEventAge_ms = currentMillis - (time_ms / 1000) * 1000;  // ms since EQ occurred
            
            Serial.printf("🔍 Correlation Check:\n");
            Serial.printf("   Shake age: %ld ms, EQ event age: %ld ms\n", eqAge_ms, eqEventAge_ms);
            Serial.printf("   Earthquake magnitude: %.1f\n", mag);
            Serial.printf("   Distance from device: %s\n", distanceStr.c_str());

            // ✅ Match criteria:
            // 1. EQ happened within ±2 minutes of shaking
            // 2. Magnitude >= 4.5 (strong enough to cause shaking)
            // 3. Distance <= 500 km (reasonable detection range)
            if (abs(eqEventAge_ms) <= 120000 &&  // ±2 minutes
                mag >= 4.5 &&
                distance <= 500.0) {
                isMatchingEQ = true;
                Serial.println("✅ MATCHED: Shaking correlated with earthquake!");
            } else {
                Serial.println("❌ NO MATCH: Shaking doesn't correlate with this earthquake.");
                if (abs(eqEventAge_ms) > 120000) {
                    Serial.println("   Reason: Time difference too large");
                }
                if (mag < 4.5) {
                    Serial.println("   Reason: Magnitude too low to cause shaking");
                }
                if (distance > 500.0) {
                    Serial.println("   Reason: Earthquake too far away");
                }
            }
        }

        if (mag >= alertMagnitude ||
            shakeIndex > 0 ||
            (mag >= minMagnitude && distance <= quakeRadiusKm)) 
        {
            // ✅ Only send full details if:
            // - No shake awaiting (automatic alert), OR
            // - Shake found matching EQ
            if (!shakeAwaitingEQ || isMatchingEQ) {
                
                // 🚨 Build Telegram Alert Message (STATIC to avoid heap fragmentation)
                const size_t MSG_SZ = 3000;
                static char message[3000];
                memset(message, 0, MSG_SZ);  // Clear buffer

                size_t len = 0;
                size_t rem = MSG_SZ;
                int n = 0;

                // --- Build header ---
                // ✅ Different header if matched shake vs auto-alert
                if (isMatchingEQ) {
                    n = snprintf(message + len, rem, "⚠️ Earthquake Confirmed! \n");
                } else if (mag >= minMagnitude && distance <= quakeRadiusKm) {
                    n = snprintf(message + len, rem, "⚠️ Nearby Earthquake Alert! \n");
                } else if (mag >= 5.0) {
                    n = snprintf(message + len, rem, "⚠️ Magnitude 5+ Earthquake Update! \n");
                } else {
                    n = snprintf(message + len, rem, "⚠️ Earthquake Information Update! \n");
                }
                
                if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }

                n = snprintf(message + len, rem, "\n ");
                if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }

                // --- Main earthquake info ---
                n = snprintf(message + len, rem,
                    "🌋 Source: %s \n "
                    "📍 Location: %s \n "
                    "💥 Magnitude: %.1f \n "
                    "⬇️ Depth: %.1f km \n "
                    "📏 Distance: %s from Device \n "
                    "🕓 Time: %s \n "
                    "📡 Device Location: %s \n "
                    "💬 Device Context: %s \n "
                    "🗺️ [View Epicenter](%s) \n "
                    "🔗 [Open %s Page](%s) \n ",
                    source.c_str(),
                    place.c_str(),
                    mag,
                    depth,
                    distanceStr.c_str(),
                    eqTime.c_str(),
                    oLocation.c_str(),
                    sContext.c_str(),
                    mapsLink,
                    source.c_str(),
                    sourceLink.c_str()
                );
                if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }

                // --- Estimates ---
                float mmi = q.usgsMMI;
                if (isnan(mmi) || mmi < 0 || mmi > 12) mmi = 0;

                float lvl = q.intensity;
                if (isnan(lvl) || lvl < 0 || lvl > 12) lvl = 0;

                n = snprintf(message + len, rem,
                    "\n📊 Earthquake Estimates\n\n"
                    "📈 Expected Intensity: %.1f \n "
                    "📐 Effective HD: %.1f km \n "
                    "📉 Expected PGA: %.4f g \n "
                    "🔢 Expected Level: %.1f \n ",
                    mmi, q.R, q.expectedPGA_g, lvl
                );
                if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }

                // --- Device Log (if gyrometer enabled) ---
                if (WithGyrometer && shakeIndex > 0 && isMatchingEQ) {
                    n = snprintf(message + len, rem, "\n📟 *Local Shaking Log*\n\n");
                    if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }

                    float localPeak = 0.0f;
                    for (int i = 0; i < shakeIndex; ++i) {
                        if (!isnan(shakeLog[i].peak) &&
                            shakeLog[i].peak > localPeak)
                            localPeak = shakeLog[i].peak;
                    }

                    if (isnan(localPeak) || localPeak < 0 || localPeak > 5)
                        localPeak = 0.0f;

                    float localIntensity = pgaToIntensity(localPeak);
                    if (isnan(localIntensity) ||
                        localIntensity < 0 ||
                        localIntensity > 12)
                        localIntensity = 0;

                    String shakingLvl = getShakingLevel(localIntensity);
                    n = snprintf(message + len, rem,
                        "📈 Local Max Peak: %.4f g \n "
                        "📊 Local Intensity: %.1f\n "
                        "💡 Shaking Level: %s \n ",
                        localPeak,
                        localIntensity,
                        shakingLvl.c_str()
                    );
                    if (n > 0 && (size_t)n < rem) { len += n; rem -= n; }
                }

                // --- Send to Telegram ---
                Serial.println("🚨 Sending earthquake alert to Telegram...");
                enqueueTelegram(message);
                
                // Note: message is a static buffer, no free needed
                
                // ✅ Only clear shake log if matched EQ or high magnitude
                if (isMatchingEQ || mag >= 5.0) {
                    clearShakeLog();
                    shakeAwaitingEQ = false;  // ✅ Reset correlation flag
                }
            }

        } else {
            Serial.println("📊 Ignored (below threshold or too far). - " + source);
        }
    } else {
        Serial.println("✅ No new earthquake detected. - " + source);
    }
    isUSGSFetching = false;
    shakeNotificationSent = false;
}

void checkEQAlertsPageSafe(String source) {
    if (xSemaphoreTake(httpMutex, portMAX_DELAY)) { // wait until mutex available
        checkEQAlerts(source);  // only USGS URL
        xSemaphoreGive(httpMutex); // release mutex
    } else {
        Serial.println("⚠️ Failed to take HTTP mutex");
    }
}

void clearShakeLog() {
    shakeIndex = 0;  // reset circular buffer head
    // Optional: clear actual entries for clarity (not strictly necessary)
    for (int i = 0; i < MAX_SHAKING_EVENTS; i++) {
        shakeLog[i].peak = 0.0f;
        shakeLog[i].ts = 0;
        shakeLog[i].intensity = 0;
    }
    Serial.println(F("Shake log cleared after quake comparison."));
}

// -----------------------
// Estimate PGA & intensity from M, depth (km), distance (km)
// -----------------------
QuakeEstimate estimateQuakeIntensity(float magnitude, float depth_km, float distance_km) {
  QuakeEstimate out;
  float R = sqrtf(distance_km * distance_km + depth_km * depth_km);
  if (R < 0.1f) R = 0.1f;

  float lnPGA = GMPE_A + GMPE_B * magnitude - GMPE_C * logf(R) - GMPE_D * R;
  float PGA_g = expf(lnPGA);

  out.R = R;
  out.expectedPGA_g = PGA_g;
  out.intensity = pgaToIntensity(PGA_g);
  out.usgsMMI = pgaToUSGS_MMI(PGA_g);

  return out;
}

// --- Map local intensity to descriptive shaking level ---
String getShakingLevel(float intensity) {
    if (intensity <= 2.0) return "😌 Light";
    else if (intensity <= 4.0) return "⚡ Moderate";
    else if (intensity <= 6.0) return "🚨 Strong";
    else if (intensity <= 8.0) return "🔥 Very Strong";
    else return "💥 Severe";
}

float haversine(float lat1, float lon1, float lat2, float lon2) {
  float R = 6371.0;
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);
  float a = sin(dLat/2)*sin(dLat/2) +
            cos(radians(lat1))*cos(radians(lat2))*
            sin(dLon/2)*sin(dLon/2);
  return R * 2 * atan2(sqrt(a), sqrt(1 - a));
}

// -----------------------
// Intensity mapping
// -----------------------
uint8_t mapIntensity(float g) {
  if (g < 0.03f) return 0;    // Not felt
  if (g < 0.05f) return 1;    // Weak
  if (g < 0.10f) return 2;    // Light
  if (g < 0.20f) return 3;    // Moderate
  if (g < 0.35f) return 4;    // Strong
  if (g < 0.60f) return 5;    // Very Strong
  return 6;                   // Violent / extreme
}

// -----------------------
// PGA -> Intensity (instrumental) mapping
// -----------------------
uint8_t pgaToIntensity(float pga) {
  if (pga < 0.003f) return 0;
  if (pga < 0.008f) return 1;
  if (pga < 0.025f) return 2;
  if (pga < 0.080f) return 3;
  if (pga < 0.180f) return 4;
  if (pga < 0.380f) return 5;
  return 6;
}

// -----------------------
// USGS MMI Intensity Mapping (PGA -> MMI)
// -----------------------
uint8_t pgaToUSGS_MMI(float pga) {
  if (pga < 0.0017f) return 1;       // MMI I
  if (pga < 0.0039f) return 2;       // MMI II
  if (pga < 0.0092f) return 3;       // MMI III
  if (pga < 0.022f)  return 4;       // MMI IV
  if (pga < 0.048f)  return 5;       // MMI V
  if (pga < 0.101f)  return 6;       // MMI VI
  if (pga < 0.215f)  return 7;       // MMI VII
  if (pga < 0.460f)  return 8;       // MMI VIII
  if (pga < 0.850f)  return 9;       // MMI IX
  return 10;                         // MMI X or greater
}

// Start USGS task if not already running
void startUSGSTask() {
    if (usgsTaskHandle == NULL) {
        xTaskCreate(
            usgsTask,         // task function
            "USGS Task",      // name
            16384,             // stack size
            NULL,             // parameter
            1,                // priority
            &usgsTaskHandle   // store handle
        );
    } else {
        Serial.println("ℹ️ USGS task already running.");
    }
}

// --- Sends a Telegram alert when shaking is detected locally ---
void sendShakingAlert() {
    char message[1024];

    // --- Compute local peak ---
    float localPeak = 0.0f;
    for (int i = 0; i < shakeIndex; ++i) {
        if (!isnan(shakeLog[i].peak) && shakeLog[i].peak > localPeak) {
            localPeak = shakeLog[i].peak;
        }
    }

    // Clamp peak to safe MEMS range
    if (isnan(localPeak) || localPeak < 0 || localPeak > 5) {
        localPeak = 0.0f;
    }

    // --- Convert to intensity ---
    float localIntensity = pgaToIntensity(localPeak);

    // Clamp invalid intensity values
    if (isnan(localIntensity) || localIntensity < 0 || localIntensity > 12) {
        localIntensity = 0;
    }

    // --- Build message ---
    snprintf(message, sizeof(message),
        "⚠️ Local Shaking Detected! \n\n"
        "📡 Device Information \n"
        "• Location: %s \n"
        "• Context: %s \n\n"
        "📟 Status \n"
        "• Device is fetching the latest earthquake data \n"
        "• Full earthquake details will be sent shortly \n"
        "\n "
        "🛰️ Device Log Estimates \n"
        "• 📈 Local Max Peak: %.4f g \n"
        "• 📊 Local Intensity Level: %.1f \n"
        "• 💡 Shaking Level: %s \n"
        "\n ",
        oLocation.c_str(),
        oContext.c_str(),
        localPeak,
        localIntensity,
        getShakingLevel(localIntensity)
    );
    Serial.println("🚨 Sending shaking alert to Telegram...");
    enqueueTelegram(message);

}

// USGS task function
void usgsTask(void* param) {
    Serial.println("🌐 USGS task started...");
    esp_task_wdt_init(15, true);  // ✅ 15 sec timeout instead of default 5
    checkEQAlertsPageSafe("USGS");
    Serial.println("✅ USGS task completed.");
    usgsTaskHandle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Gyroscope / Local Seismic Monitoring
// ============================================================================
bool GYRO_init() {

  if(detectBMI160()){
    gyroModel = BMI160_TYPE;
    Serial.println("BMI160 detected via CHIP_ID");
  } else {
    gyroModel = MPU6050_TYPE;
    Serial.println("BMI160 not found → assuming MPU6050 on secondary I2C bus.");
  }

  if (gyroModel == MPU6050_TYPE) {
    //MPU6050_init
    I2C2.begin(SDA_PIN2, SCL_PIN2);  // ESP32 I2C pins - Wire 2
    mpu.initialize();
    // Get device ID
    uint8_t devID = mpu.getDeviceID();
    Serial.print("MPU6050 Device ID: 0x"); Serial.println(devID, HEX);
    // Test connection (optional) or accept known clone IDs
    if (mpu.testConnection() || devID == GYRO_ADDR || devID == 0x38 || devID == 0x34 || devID == 0x36 || devID == 0x68 || devID == 0x69  || devID == 0x1F) {
        mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2); // Â±2 g
        mpu.setRate(9);                               // 1000 / (1+39) ≈ 25 Hz
        mpu.setDLPFMode(MPU6050_DLPF_BW_20);             // 5 Hz LPF      
        Serial.println("MPU6050 initialized successfully.");
        return true;
    } else {
        Serial.println("MPU6050 initialization failed!");
        return false;
    }
  } else {
    //BMI160_init
    //Wire.begin(SDA_PIN, SCL_PIN);  // ESP32 I2C pins - Wire 1 - already initialized in Setup
    if (bmi.begin(BMI160GenClass::I2C_MODE, GYRO_ADDR)) {
        bmi.setAccelerometerRange(2);     // Â±2g (best sensitivity)
        bmi.setAccelerometerRate(25);     // 25 Hz (low noise, seismic-friendly)      
        Serial.println("BMI160 initialized successfully!");        
        return true;
    } else {
        Serial.println("BMI160 initialization failed!");
        return false;
    }
  }
}

// read BMI160 or MPU6050
void readAccelerometer() {
  int ax_raw=0, ay_raw=0, az_raw=0;
  if (gyroModel == MPU6050_TYPE) {
    //MPU6050
    int16_t tmp_ax, tmp_ay, tmp_az;
    mpu.getAcceleration(&tmp_ax, &tmp_ay, &tmp_az);    
    ax_raw = tmp_ax;
    ay_raw = tmp_ay;
    az_raw = tmp_az;   
  } else {
    //BMI160
    bmi.readAccelerometer(ax_raw, ay_raw, az_raw); 
  } 

  // Apply correct scale - 0.000061 g per LSB for both BMI160 AND MPU6050
  float scale;
  if (gyroModel == BMI160_TYPE) {
      scale = 1.0f / 16384.0f;   // BMI160 ±2g
  } else {
      scale = 1.0f / 16384.0f;   // MPU6050 ±2g
  }
  
  float ax = ax_raw * scale;
  float ay = ay_raw * scale;
  float az = az_raw * scale;

  // Estimate gravity (low-pass)
  gx = ALPHA * gx + (1.0f - ALPHA) * ax;
  gy = ALPHA * gy + (1.0f - ALPHA) * ay;
  gz = ALPHA * gz + (1.0f - ALPHA) * az;

  // Remove gravity -> dynamic acceleration
  float dx = ax - gx;
  float dy = ay - gy;
  float dz = az - gz;

  // Peak vibration magnitude
  float peak = sqrtf(dx*dx + dy*dy + dz*dz);

  float horizontal = sqrtf(dx*dx + dy*dy);
  float effectivePeak = max(peak, horizontal * 1.6f);

  //shakingNow = peak > G_THRESHOLD;
  if (gyroModel == MPU6050_TYPE) {
    shakingNow = effectivePeak > G_THRESHOLD_MPU6050 ;
  } else {
    shakingNow = effectivePeak > G_THRESHOLD_BMI160 ;
  }

  if (shakingNow) {
    // log into shakeLog circularly
    shakeLog[shakeIndex].peak = effectivePeak;
    shakeLog[shakeIndex].ts = millis();
    shakeLog[shakeIndex].intensity = mapIntensity(effectivePeak);
    shakeIndex++;
    if (shakeIndex >= MAX_SHAKING_EVENTS) shakeIndex = 0;
  }

  // Determine mounting
  String mounting;
  if (fabs(ax) > 0.8f) mounting = "Wall Mounted (X-up)";
  else if (fabs(ay) > 0.8f) mounting = "Wall Mounted (Y-up)";
  else mounting = "Flat Mounted";

  //FOR DEBUGGING PURPOSES ONLY
  //=========================================
  //Serial.printf("Accel (g): X=%.4f Y=%.4f Z=%.4f | Peak=%.4f g | %s | Mounting: %s\n", ax, ay, az, effectivePeak, shakingNow ? "SHAKING" : "Calm", mounting.c_str());  
  //=========================================
}

void ExecuteSeismicMonitoring() {

    if (!profileLoaded) return;
    if (!WithGyrometer) return;

    // Initialize Gyrometer only once
    if (!IsGyrometerInitialized) {
        Serial.println("Initializing Gyro sensor ...");
        IsGyrometerInitialized = GYRO_init();
        imuWarmupStart = millis();
        imuWarmupCount = 0;
        return;
    }

    // Read only at interval
    if (currentMillis - lastGyroReadTime < gyroInterval) return;
    lastGyroReadTime = currentMillis;

    // Always read accelerometer
    readAccelerometer();

    // WARM-UP PROTECTION (time + sample count)
    if ((millis() - imuWarmupStart < 3000) || (imuWarmupCount < 5)) {

        imuWarmupCount++;        // count valid reads
        shakingNow = false;

        clearShakeLog();         // keep logs clean during warm-up
        return;
    }

    // ✅ IMU is now valid — allow shaking alerts
    if (imuReady) {
        Serial.println("🟢 IMU warm-up complete — enabling shake detection");
        imuReady = false;
    }    

    // --- NORMAL SEISMIC PROCESSING ---
    if (shakingNow) {
        gyroInterval = fastInterval;
        fastModeEndTime = currentMillis + 5000;

        // ✅ NEW: First time shaking detected
        //FOR DEBUGGING PURPOSES ONLY
        //=========================================        
        //Serial.println("shakeAwaitingEQ=" + String(shakeAwaitingEQ) + ",shakeNotificationSent=" + String(shakeNotificationSent) + ",isUSGSFetching=" + String(isUSGSFetching) + ",imuReady =" + String(imuReady ));
        //=========================================                
        if (!shakeAwaitingEQ && !shakeNotificationSent && !isUSGSFetching && !imuReady ) {
            firstShakeTime = currentMillis;  // ✅ Record shake start time
            shakeAwaitingEQ = true;          // ✅ Flag: waiting for matching EQ
            
            // Send initial alert (no details yet)
            Serial.println("🚨 Shaking detected! Awaiting earthquake confirmation...");
            sendShakingAlert();  // sends: "earthquake details to follow"
            shakeNotificationSent = true;
        }
    }
    else if (currentMillis < fastModeEndTime) {
        gyroInterval = fastInterval;
    }
    else {
        gyroInterval = normalInterval;
        
        // ✅ NEW: Timeout check — if shaking stopped and no EQ found yet
        if (shakeAwaitingEQ && (currentMillis - firstShakeTime) > SHAKE_TIMEOUT) {
            Serial.println("⏰ Shake timeout — no matching earthquake found. Assume device movement.");

            // Send a single Telegram notification that device likely moved
            if (!shakeTimeoutNotified) {
                char message[512];

                snprintf(message, sizeof(message),
                    "⚠️ Shake Timeout — Likely Device Movement \n\n"
                    "📍 Location: %s\n"
                    "💬 Context: %s\n\n"
                    "⏰ Detected shaking %lu minutes ago but no matching earthquake was found. The device may have been moved. Shake logs cleared.",
                    oLocation.c_str(),
                    oContext.c_str(),
                    (unsigned long)((currentMillis - firstShakeTime) / 60000UL)
                );
                Serial.println("🚨 Sending shake-timeout notification to Telegram...");
                enqueueTelegram(message);

                shakeTimeoutNotified = true;
            }

            shakeAwaitingEQ = false;
            shakeNotificationSent = false;
            clearShakeLog();
        }
    }
}

// ============================================================================
// MQ135 / Air Quality / Gas Calculations
// ============================================================================

void CalibrateMQ135() {

  Serial.print("Calibrating please wait");
  float calcR0 = 0;

  for (int i = 1; i <= 10; i++) {
    float rs = getSensorResistance();
    calcR0 += rs / RATIO_CLEAN_AIR;
    Serial.print(".");
    delay(500);
  }

  R0 = calcR0 / 10;
  Serial.println(" done!");

  // --- Safety checks ---
  if (isinf(R0)) {
    Serial.println("Warning: R0 is infinite (open circuit). Using fallback R0Fixed.");
    R0 = -1;
  }
  if (R0 == 0) {
    Serial.println("Warning: R0 is zero (short circuit). Using fallback R0Fixed.");
    R0 = -1;
  }

  // --- Save to Preferences ---
  prefs.putFloat("mq135.r0", R0);

  Serial.print("R0 saved: ");
  Serial.println(R0, 4);

  if (R0 < 0) {
    R0 = R0Fixed;
    Serial.print("Loaded R0 from R0Fixed: ");
    Serial.println(R0, 4);    
  }  else {
    Serial.print("Loaded R0 from Saved Prefs: ");
    Serial.println(R0, 4);        
  }
}

GasCalibration* findGasCalibration(const char* name) {
  for (int i = 0; i < gasCount; i++) {
    if (strcmp(gasMap[i].name, name) == 0) {
      return &gasMap[i];
    }
  }
  return nullptr;
}

float getCO2CorrectionFactor(float t, float h) {
  return CORA * t * t - CORB * t + CORC - (h - 33.0) * CORD;
}

float getCorrectionFactor(const char* gas, float tempC, float humidity) {
  float factor = 1.0;
  if (strcmp(gas, "CO") == 0)
    factor = 1.0 + (tempC - 25) * 0.005 - (humidity - 50) * 0.002;
  else if (strcmp(gas, "NH3") == 0)
    factor = 1.0 + (tempC - 25) * 0.002 - (humidity - 50) * 0.001;
  else if (strcmp(gas, "Alcohol") == 0)
    factor = 1.0 + (tempC - 25) * 0.004 - (humidity - 50) * 0.003;
  else if (strcmp(gas, "Aceton") == 0)
    factor = 1.0 + (tempC - 25) * 0.005 - (humidity - 50) * 0.004;
  else if (strcmp(gas, "Toluen") == 0)
    factor = 1.0 + (tempC - 25) * 0.003 - (humidity - 50) * 0.002;
  return constrain(factor, 0.8, 1.2);
}

int readMQ135ADC() {
  long sum = 0;
  int valid = 0;

  for (int i = 0; i < MQ_ADC_SAMPLES; i++) {
    int adc = analogRead(MQ135_PIN);

    //if (adc > 0 && adc < ADC_RESOLUTION) {
    if (adc >= 0 && adc < ADC_RESOLUTION) {
      sum += adc;
      valid++;
    }
    delay(MQ_ADC_DELAY);
  }

  if (valid == 0) return -1;
  return sum / valid;
}

float getSensorResistance() {

  int adcAvg = readMQ135ADC();
  if (adcAvg < 0) return NAN;

  // ADC voltage
  float vAdc = (adcAvg / ADC_RESOLUTION) * VREF;
  if (vAdc < 0.01) return NAN;

  // Reconstruct real Rs node voltage
  float vTap = vAdc * DIV_RATIO;
  vTap = constrain(vTap, 0.05, VCC - 0.05);

  // MQ135 Rs calculation (datasheet-correct)
  float rs = (VCC - vTap) * RL / vTap;   // kΩ
  return rs;
}

float LoadMQ135_R0() {
  float stored = prefs.getFloat("mq135.r0", -1.0);

  Serial.print("Loaded R0 from Prefs: ");
  Serial.println(stored, 4);

  if (stored <= 0 || isinf(stored) || isnan(stored)) {
    Serial.println("No valid R0 found in prefs. Need calibration.");
    return -1.0;
  }

  return stored;
}

void GetAirQuality() {

  if(UsesDHTSensor){
    dhttemperature = dht.readTemperature();
    dhthumidity = dht.readHumidity();

    if (!isnan(dhttemperature) || isnan(!dhthumidity)) {
      dhttemperature = dhttemperature + (oTempOffset);
      dhthumidity = dhthumidity + (oHumidOffset);
      dhthumidity = constrain(dhthumidity, 0, 100); 
      dhtheatindex = dht.computeHeatIndex(dhttemperature, dhthumidity, false);
      dhtpressure = 0;
    }
  } else {
    sensors_event_t h, t;
    aht.getEvent(&h, &t);

    float tRaw = t.temperature;
    float hRaw = h.relative_humidity;
    float pRaw = bmp.readPressure() / 100.0;
    float bmpTempRaw = bmp.readTemperature();  // °C (die temperature)    

    // ===== EMA filtering =====
    if (isnan(tempFilt)) {
      tempFilt = tempOut = tRaw;
      humFilt  = humOut  = hRaw;
      pressFilt = pRaw;
    } else {
      tempFilt  = TEMP_ALPHA  * tRaw + (1 - TEMP_ALPHA)  * tempFilt;
      humFilt   = HUM_ALPHA   * hRaw + (1 - HUM_ALPHA)   * humFilt;
      pressFilt = PRESS_ALPHA * pRaw + (1 - PRESS_ALPHA) * pressFilt;
    }

    // ===== Deadband =====
    if (fabs(tempFilt - tempOut) >= TEMP_DEADBAND) tempOut = tempFilt;
    if (fabs(humFilt  - humOut)  >= HUM_DEADBAND)  humOut  = humFilt;    

    tempOut += oTempOffset;     //Apply offsets
    humOut +=  oHumidOffset;   //Apply offsets

    dhttemperature = constrain(tempOut, -40, 85);
    dhthumidity    = constrain(humOut, 0, 100);

    dhtheatindex = heatIndex(dhttemperature, dhthumidity);
    dhtpressure = pressFilt;
  }

  // -----------------------------
  // 2. Read MQ135 sensor
  // -----------------------------
  RS = getSensorResistance();
  if (isnan(RS) || RS <= 0 || R0 <= 0) return;

  float ratio = RS / R0;

  // -----------------------------
  // 3. Raw gas estimates (MQ135-relative)
  // -----------------------------
  float co_raw      = getGasPPM("CO", ratio);
  float nh3_raw     = getGasPPM("NH3", ratio);
  float alcohol_raw = getGasPPM("Alcohol", ratio);
  float acetone_raw = getGasPPM("Aceton", ratio);
  float toluen_raw  = getGasPPM("Toluen", ratio);
  float co2_raw     = getGasPPM("CO2", ratio);

  // -----------------------------
  // 4. Apply temp/humidity correction
  // -----------------------------
  COPPM      = getCorrectedPPM("CO",      co_raw,      dhttemperature, dhthumidity);
  NH3PPM     = getCorrectedPPM("NH3",     nh3_raw,     dhttemperature, dhthumidity);
  AlcoholPPM = getCorrectedPPM("Alcohol", alcohol_raw, dhttemperature, dhthumidity);
  AcetonPPM  = getCorrectedPPM("Aceton",  acetone_raw, dhttemperature, dhthumidity);
  ToluenPPM  = getCorrectedPPM("Toluen",  toluen_raw,  dhttemperature, dhthumidity);

    // -----------------------------
  // 5. CO2 handling (special case)
  // -----------------------------
  float co2_corr = co2_raw * getCO2CorrectionFactor(dhttemperature, dhthumidity);

  CO2PPM = constrain(oCO2Base + co2_corr, 350, 5000);

  // -----------------------------
  // 6. Export raw values if needed
  // -----------------------------
  CO2      = CO2PPM;
  CO      = COPPM;
  NH3     = NH3PPM;
  Alcohol = AlcoholPPM;
  Aceton  = AcetonPPM;
  Toluen  = ToluenPPM;
}

float heatIndex(float t, float h) {
  float T = t * 9 / 5 + 32;
  float HI =
    -42.379 + 2.04901523*T + 10.14333127*h
    -0.22475541*T*h -0.00683783*T*T
    -0.05481717*h*h +0.00122874*T*T*h
    +0.00085282*T*h*h -0.00000199*T*T*h*h;
  return (HI - 32) * 5 / 9;
}

float getCorrectedPPM(const char* gas, float ppm_raw, float tempC, float humidity) {
  float cFactor = getCorrectionFactor(gas, tempC, humidity);
  if (cFactor < 0) cFactor = 1.0;
  return ppm_raw * cFactor;
}

float getGasPPM(const char* gas, float Rs_R0) {

  // Guard rails
  if (Rs_R0 <= 0 || isnan(Rs_R0) || isinf(Rs_R0)) return 0.0;

  GasCalibration* cal = findGasCalibration(gas);
  if (!cal) return 0.0;

  // Clamp ratio to sane MQ135 operating range
  // (prevents runaway ppm from noise)
  Rs_R0 = constrain(Rs_R0, 0.1, 10.0);

  float logPPM = cal->slope * log10(Rs_R0) + cal->intercept;
  float ppm = pow(10, logPPM);

  // Final sanity clamp
  if (isnan(ppm) || isinf(ppm) || ppm < 0) return 0.0;

  return ppm;
}

const char* Get_Level_TemperatureEmoji(float val, bool emojionly){
  return (val < 18.0) ? (emojionly ? "🧊" : "Cool") :
         (val <= 27.0) ? (emojionly ? "😌" : "Normal") :
         (val <= 30.0) ? (emojionly ? "⚡" : "Warm") :
         (val <= 35.0) ? (emojionly ? "🚨" : "High temperature") :
                         (emojionly ? "💥" : "Critical temperature");
}

const char* Get_Level_HumidityEmoji(float val, bool emojionly){
  return (val < 30.0) ? (emojionly ? "🏜️" : "Too dry") :
         (val <= 60.0) ? (emojionly ? "🟢😌" : "Normal") :
         (val <= 70.0) ? (emojionly ? "⚡" : "Humid") :
         (val <= 85.0) ? (emojionly ? "🚨" : "High humidity") :
                         (emojionly ? "💥" : "Critical humidity");
}

const char* Get_Level_HeatIndexEmoji(float val, bool emojionly){
  return (val < 27.0) ? (emojionly ? "😌" : "Comfortable") :
         (val <= 32.0) ? (emojionly ? "⚡" : "Caution") :
         (val <= 41.0) ? (emojionly ? "🚨" : "Extreme caution") :
         (val <= 54.0) ? (emojionly ? "🔥" : "Danger") :
                         (emojionly ? "💥" : "Extreme danger");
}


const char* Get_Level_AcetonEmoji(float val, bool emojionly){
  return (val <= 150) ? (emojionly ? "😌" : "Normal background") :
         (val <= 300) ? (emojionly ? "⚡" : "Elevated vapors") :
         (val <= 600) ? (emojionly ? "🚨" : "Strong presence") :
                        (emojionly ? "💥" : "High concentration");
}

const char* Get_Level_AlcoholEmoji(float val, bool emojionly){
  return (val <= 140) ? (emojionly ? "😌" : "Normal background") :
         (val <= 200) ? (emojionly ? "⚡" : "Light vapors") :
         (val <= 350) ? (emojionly ? "🚨" : "Strong vapors") :
                        (emojionly ? "💥" : "Heavy vapors");
}

const char* Get_Level_COEmoji(float val, bool emojionly){
  return (val <= 70)  ? (emojionly ? "😌" : "Normal background") :
         (val <= 120) ? (emojionly ? "⚡" : "Elevated levels") :
         (val <= 200) ? (emojionly ? "🚨" : "Unhealthy levels") :
                        (emojionly ? "💥" : "Dangerous levels");
}

const char* Get_Level_CO2Emoji(float val, bool emojionly){
  return (val <= 450)  ? (emojionly ? "😌" : "Excellent air") :
         (val <= 800)  ? (emojionly ? "⚡" : "Good ventilation") :
         (val <= 1200) ? (emojionly ? "🚨" : "Stale air") :
         (val <= 2500) ? (emojionly ? "🔥" : "Poor air") :
                         (emojionly ? "💥" : "Very poor air");
}

const char* Get_Level_NH3Emoji(float val, bool emojionly){
  return (val <= 120) ? (emojionly ? "😌" : "Normal background") :
         (val <= 200) ? (emojionly ? "⚡" : "Noticeable odor") :
         (val <= 400) ? (emojionly ? "🚨" : "Strong odor") :
                        (emojionly ? "💥" : "Hazardous levels");
}

const char* Get_Level_TolueneEmoji(float val, bool emojionly){
  return (val <= 100)  ? (emojionly ? "😌" : "Normal background") :
         (val <= 200)  ? (emojionly ? "⚡" : "Mild Headache") :
         (val <= 500)  ? (emojionly ? "🚨" : "Moderate Nausea") :
         (val <= 800)  ? (emojionly ? "🔥" : "Severe drowsiness") :
         (val <= 1000) ? (emojionly ? "😵" : "Extreme fatigue") :
                         (emojionly ? "☣️" : "Toxic exposure");
}

void PrintAirQuality() {
  // Non-allocating padded print to avoid using String
  auto padPrint = [](float val, int width){
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f", val);
    int len = (int)strlen(buf);
    int pad = width - len;
    for (int i = 0; i < pad; ++i) Serial.print(' ');
    Serial.print(buf);
  };

  Serial.println("*********************************************************************************************************************");
  Serial.println("** Values from MQ-135  **** Gas Concentrations  ****");
  Serial.println("*********************************************************************************************************************");

  // Header
  Serial.println("|    CO    |  Alcohol |   CO2      |  Toluen  |  NH3     |  Aceton  |  Temp     |  Humid    | Heat Index|");

  // Raw values row
  Serial.print("|"); 
  padPrint(COPPM, 8);       Serial.print(" | ");
  padPrint(AlcoholPPM, 8);  Serial.print(" | ");
  padPrint(CO2PPM, 10);     Serial.print(" | ");
  padPrint(ToluenPPM, 8);   Serial.print(" | ");
  padPrint(NH3PPM, 8);      Serial.print(" | ");
  padPrint(AcetonPPM, 8);   Serial.print(" | ");
  padPrint(dhttemperature, 8); Serial.print(" | ");
  padPrint(dhthumidity, 8); Serial.print(" | ");
  padPrint(dhtheatindex, 9); Serial.println(" |");

  Serial.println("---------------------------------------------------------------------------------------------------------------------");

  // Levels
  Serial.print("CO Level     :\t"); Serial.println(Get_Level_COEmoji(COPPM, false));    
  Serial.print("Alcohol Level:\t"); Serial.println(Get_Level_AlcoholEmoji(AlcoholPPM, false));    
  Serial.print("CO2 Level    :\t"); Serial.println(Get_Level_CO2Emoji(CO2PPM, false));
  Serial.print("Toluen Level :\t"); Serial.println(Get_Level_TolueneEmoji(ToluenPPM, false));    
  Serial.print("NH3 Level    :\t"); Serial.println(Get_Level_NH3Emoji(NH3PPM, false));    
  Serial.print("Aceton Level :\t"); Serial.println(Get_Level_AcetonEmoji(AcetonPPM, false));    

  Serial.println("---------------------------------------------------------------------------------------------------------------------");
}

// ============================================================================
// Infrared (IR) Control
// ============================================================================
// ==== IR Helpers ====
int parseStringToArray(String data, uint16_t *arr, int maxLen) {
  int idx = 0;
  char *token;
  char buf[data.length() + 1];
  data.toCharArray(buf, sizeof(buf));
  token = strtok(buf, ",");
  while (token != NULL && idx < maxLen) {
    arr[idx++] = atoi(token);
    token = strtok(NULL, ",");
  }
  return idx;
}

// ==== Send IR command with 3 repeats like a real remote ====
void sendIR(const String& command) {
  for (auto& cmd : commands) {
    if (command == cmd.name) {
      if (*(cmd.length) == 0) {
        Serial.println("⚠️ IR array is empty for command: " + command);
        return;
      }
      for (int r = 0; r < 3; r++) {         // send 3 repeats
        sendRaw(command, cmd.data, *(cmd.length)); // pass command too
        delay(45);                          // inter-frame gap
      }
      Serial.println("✅ Sent IR command: " + command);
      return;
    }
  }
  Serial.println("❌ Unknown IR command: " + command);
}

void sendRaw(const String& command, uint16_t *arr, size_t length, uint32_t carrierFreq) {
  int channel = 0;  // LEDC channel

  // attach once
  if (Wireless_Control) {
    ledcAttachPin(IR_LED_PIN, channel);
  } else {
    if (isACLCommand(command)) {
      ledcAttachPin(IR_LED_PINL, channel);
    } else {
      ledcAttachPin(IR_LED_PINR, channel);
    }
  }

  // send pulse codes
  for (size_t i = 0; i < length; i++) {
    if (i % 2 == 0) {
      // MARK: turn on carrier
      ledcWriteTone(channel, carrierFreq);
      delayMicroseconds(arr[i]);
      ledcWriteTone(channel, 0); // stop carrier
    } else {
      // SPACE: carrier off
      delayMicroseconds(arr[i]);
    }
  }

  // detach once
  if (Wireless_Control) {
    ledcDetachPin(IR_LED_PIN);
  } else {
    if (isACLCommand(command)) {
      ledcDetachPin(IR_LED_PINL);
    } else {
      ledcDetachPin(IR_LED_PINR);
    }
  }
}

//BMP280 Detection
bool i2cDevicePresent(TwoWire* bus, uint8_t addr) {
  bus->beginTransmission(addr);
  return (bus->endTransmission() == 0);
}

uint8_t readRegister(TwoWire* bus, uint8_t addr, uint8_t reg) {
  bus->beginTransmission(addr);
  bus->write(reg);
  bus->endTransmission(true);   // STOP (ESP32-safe)
  bus->requestFrom(addr, (uint8_t)1);
  return bus->available() ? bus->read() : 0xFF;
}

int detectBMP280Address() {

  const uint8_t candidates[] = { 0x76, 0x77 };

  for (uint8_t addr : candidates) {

    if (!i2cDevicePresent(&Wire, addr)) continue;
    uint8_t id = readRegister(&Wire, addr, BMP280_ID_REG);

    if (id == BMP280_CHIP_ID) {
      Serial.printf("✅ BMP280 detected at 0x%02X\n", addr);
      return addr;
    }

    // Optional: detect BME280 too
    if (id == 0x60) {
      Serial.printf("⚠️ BME280 detected at 0x%02X (not BMP280)\n", addr);
      return addr;
    }
  }

  Serial.println("❌ BMP280 not detected at 0x76 or 0x77");
  return -1;
}

bool detectBMI160() {
  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)GYRO_ADDR, (size_t)1, true) != 1) return false;

  uint8_t id = Wire.read();
  return (id == 0xD1);
}
