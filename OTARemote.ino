#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ---------- CONFIG ----------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* VERSION_JSON_URL = "https://raw.githubusercontent.com/ErickAsas/Main---IoTTelemetry/main/version.json";
#define FW_VERSION "2.0.9"

Preferences prefs;

// ---------- HELPER FUNCTIONS ----------

bool isNewerVersion(const String& server, const String& current) {
  int sMaj, sMin, sPat;
  int cMaj, cMin, cPat;

  sscanf(server.c_str(), "%d.%d.%d", &sMaj, &sMin, &sPat);
  sscanf(current.c_str(), "%d.%d.%d", &cMaj, &cMin, &cPat);

  if (sMaj != cMaj) return sMaj > cMaj;
  if (sMin != cMin) return sMin > cMin;
  return sPat > cPat;
}

String buildFirmwareURL(const String& tag, const String& bin) {
  return "https://github.com/ErickAsas/Main---IoTTelemetry/releases/download/" +
         tag + "/" + bin;
}

void updateVersion(const String& newVersion) {
  prefs.begin("iotTelemetry", false);
  prefs.putString("firmwareVersion", newVersion);
  prefs.end();
}

// ---------- OTA FUNCTION ----------
void checkRemoteOta() {
  Serial.println("⏳ Remote OTA Check...");

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
  http.begin(firmwareUrl);
  int firmwareCode = http.GET();
  if (firmwareCode != HTTP_CODE_OK) {
    Serial.println("❌ Firmware download failed. Code: " + String(firmwareCode));
    http.end();
    return;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (!Update.begin(contentLength)) {
    Update.printError(Serial);
    http.end();
    return;
  }

  size_t written = Update.writeStream(*stream);
  if (written != contentLength) {
    Serial.println("❌ Incomplete OTA write");
    Update.abort();
    http.end();
    return;
  }

  if (Update.end(true)) {
    Serial.println("✅ OTA successful. Rebooting...");
    prefs.begin("iotTelemetry", false);
    prefs.putBool("otaPending", true);
    prefs.end();
    updateVersion(remoteVersion);
    ESP.restart();
  } else {
    Update.printError(Serial);
  }

  http.end();
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("🔌 Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to WiFi");

  // Optional: check for OTA immediately on boot
  checkRemoteOta();
}

// ---------- LOOP ----------
void loop() {
  // In real use, you could trigger OTA periodically or via a command
  // For demo purposes, check OTA every 30 minutes
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30UL * 60UL * 1000UL) {
    lastCheck = millis();
    checkRemoteOta();
  }

  delay(1000); // main loop delay
}
