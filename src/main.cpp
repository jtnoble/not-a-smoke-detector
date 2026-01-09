/*
  ESP32 Adafruit IO Beeper (WiFi provisioning + MQTT over TLS + light power save)

  Features:
   - On first boot / on reset: opens AP "BEEPER-SETUP" for provisioning (simple web form).
   - Saves SSID and PASS into Preferences (NVS).
   - Connects to WiFi, then to Adafruit IO using MQTT/TLS (io.adafruit.com:8883).
   - Subscribes to topic:  "<ADA_USERNAME>/feeds/<FEED_KEY>"
   - When a message payload contains "true" -> beep() and publish "false" to clear.
   - Uses WiFi modem power save mode to reduce average power draw.

  Placeholders to replace:
    - Nothing for SSID/PASS initially (user will provision).
    - ADA_USERNAME and ADA_KEY must be set in the provisioning web page (or edited below).
      For convenience the sketch allows entering Adafruit username/key at provisioning time.
*/

#include <WiFi.h>
#include "esp_wifi.h"
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// -------------------- Hardware / user config --------------------
const int BUZZER_PIN = 25;          // GPIO that drives the active buzzer
const int LED_PIN = 26;             // Pin for the LED
const int RESET_BTN = 27;           // Pin for the reset button
const char* AP_SSID = "BEEPER-SETUP";
const char* AP_PASS = "beeper1234"; // optional, keep >=8 chars for phones that require it

// Power-saving config
// Use LIGHT_SLEEP via WiFi power-save (modem PS). This keeps connection but reduces average draw.
const bool ENABLE_WIFI_POWERSAVE = false;

// If MQTT cannot connect repeatedly, open provisioning AP again after N attempts
const int MQTT_MAX_CONNECT_ATTEMPTS = 6;

// Optional: if you want to fall back to periodic deep-sleep when idle (very low power but slower)
// set FALLBACK_TO_DEEPSLEEP_SECONDS > 0 (e.g., 300) to deep-sleep for that many seconds after prolonged idle.
// Default 0 (disabled).
const uint32_t FALLBACK_TO_DEEPSLEEP_SECONDS = 0;

// -------------------- Globals --------------------
Preferences prefs;
WebServer server(80);

String savedSSID = "";
String savedPASS = "";

String adaUsername = "";
String adaKey = "";
String feedKey = "beeper"; // default feed key; can be changed in provisioning page

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

unsigned long lastMqttMsgTime = 0;
int mqttConnectAttempts = 0;

// ---------- HTML provisioning page ----------
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Beeper Setup</title>
<style> body{font-family:Arial;padding:10px} input{width:100%;padding:8px;margin:6px 0} button{padding:10px;width:100%;background:#007bff;color:#fff;border:none} </style>
</head>
<body>
<h3>"Not A Smoke Detector™" Adafruit IO Setup</h3>
<form action="/save" method="POST">
<label>WiFi SSID</label><input name="ssid" required>
<label>WiFi Password</label><input name="pass">
<hr>
<label>Adafruit IO Username</label><input name="ada_user" value="" required>
<label>Adafruit IO AIO Key</label><input name="ada_key" value="" required>
<label>Feed Key (e.g. beeper)</label><input name="feed_key" value="beeper" required>
<button type="submit">Save & Reboot</button>
</form>
<p>A single beep means you have turned on the device and it has connected to the network!</p>
<p>To reset for a new WiFi network, press the RESET button.</p>
<p>2 beeps means you have clicked the reset button!</p>
<p>3 beeps means you have entered setup mode, which is this one!</p>
<p>If you saved your settings, and after a reboot you re-enter setup mode, this likely means you entered your SSID or Password incorrectly!</p>
</body>
</html>
)rawliteral";

// ---------- Helpers ----------
void beep() {
  // Enable buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  // quick beep pattern
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  // Disable buzzer pin (for high pitched ring)
  pinMode(BUZZER_PIN, OUTPUT);
}

void beepNeedsConfig() {
  // beep to signify setup required
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(200);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(200);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, INPUT);
}

// Save credentials and aio settings into preferences
void saveCredentials(const String& ssid, const String& pass, const String& user, const String& aioKey, const String& fkey) {
  prefs.begin("config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("ada_user", user);
  prefs.putString("ada_key", aioKey);
  prefs.putString("feed", fkey);
  prefs.end();
}

// Load saved settings (if present)
void loadSavedSettings() {
  prefs.begin("config", true);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  adaUsername = prefs.getString("ada_user", "");
  adaKey = prefs.getString("ada_key", "");
  feedKey = prefs.getString("feed", "beeper");
  prefs.end();
}

// ---------- Web handlers ----------
void handleRoot() {
  server.send(200, "text/html", CONFIG_PAGE);
}

void handleSave() {
  if (!server.hasArg("ssid") || !server.hasArg("ada_user") || !server.hasArg("ada_key") || !server.hasArg("feed_key")) {
    server.send(400, "text/plain", "Missing fields");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String user = server.arg("ada_user");
  String aioKey = server.arg("ada_key");
  String fkey = server.arg("feed_key");

  saveCredentials(ssid, pass, user, aioKey, fkey);

  server.send(200, "text/html", "<html><body><h3>Saved! Rebooting...</h3></body></html>");
  delay(1500);
  ESP.restart();
}

void handleResetPrefs() {
  pinMode(BUZZER_PIN, OUTPUT);
  prefs.begin("config", false);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(250);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, INPUT);

  prefs.clear();
  prefs.end();
  delay(500);
  esp_restart();
}

// ---------- Provisioning portal ----------
void startConfigPortal() {
  Serial.println("Starting config portal (AP mode)...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(apIP);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  beepNeedsConfig();

  // Serve portal until reboot or configuration saved
  while (true) {
    server.handleClient();
    delay(2);
  }
}

// ---------- MQTT callback ----------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // convert payload to string
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.print("MQTT msg on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(msg);

  // look for true (case-insensitive)
  String lower = msg;
  lower.toLowerCase();
  if (lower.indexOf("true") >= 0 || lower == "1") {
    Serial.println("Ping received -> beep and clear feed");
    beep();

    // publish "false" to clear feed
    String pubTopic = adaUsername + "/feeds/" + feedKey;
    if (mqtt.publish(pubTopic.c_str(), "false")) {
      Serial.println("Cleared feed via publish.");
    } else {
      Serial.println("Failed to publish clear message.");
    }
  }

  lastMqttMsgTime = millis();
}

// ---------- MQTT connect helper ----------
bool connectToMqtt() {
  if (adaUsername.length() == 0 || adaKey.length() == 0) {
    Serial.println("Adafruit IO credentials missing.");
    return false;
  }

  String mqttHost = "io.adafruit.com";
  int mqttPort = 8883;

  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqtt.setCallback(mqttCallback);

  // WiFiClientSecure setup: insecure mode used here for easier usage.
  // For production, replace setInsecure() with setCACert() and load Adafruit IO root CA.
  secureClient.setInsecure();

  // clientID must be unique
  String clientId = "esp32-beeper-";
  clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.print("Connecting to MQTT as ");
  Serial.println(clientId);

  if (mqtt.connect(clientId.c_str(), adaUsername.c_str(), adaKey.c_str())) {
    Serial.println("MQTT connected.");
    String subTopic = adaUsername + "/feeds/" + feedKey;
    if (mqtt.subscribe(subTopic.c_str())) {
      Serial.print("Subscribed to: "); Serial.println(subTopic);
    } else {
      Serial.println("Subscribe failed.");
    }
    mqttConnectAttempts = 0;
    return true;
  } else {
    Serial.print("MQTT connect failed, rc=");
    Serial.println(mqtt.state());
    return false;
  }
}

// ---------- Try connect WiFi ----------
bool tryConnectWiFi(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi ");
  Serial.print(ssid);
  Serial.print(" ...");

  unsigned long start = millis();
  const unsigned long timeout = 20000; // 20s
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println("WiFi connect failed.");
    return false;
  }
}


// -------------------- setup & loop --------------------
void setup() {
  // Set CPU frequency to 80 MHz
  setCpuFrequencyMhz(80);
  
  Serial.begin(115200);
  delay(50);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, INPUT); // float when idle

  pinMode(RESET_BTN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);

  // Load any saved config
  loadSavedSettings();

  // If missing saved WiFi or missing Adafruit credentials -> open provisioning portal
  if (savedSSID == "") {
    startConfigPortal();
    // startConfigPortal only returns if server loop is broken, or restarted
  }

  // Try to connect using saved credentials
  if (!tryConnectWiFi(savedSSID.c_str(), savedPASS.c_str())) {
    // Can't connect -> open portal
    startConfigPortal();
    return;
  }

  // optionally enable WiFi power save mode (modem/light)
  if (ENABLE_WIFI_POWERSAVE) {
    // WIFI_PS_MIN_MODEM gives good balance for MQTT
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    Serial.println("WiFi power save enabled: WIFI_PS_MIN_MODEM");
  }

  // set Adafruit IO vars from saved settings
  // (we already loaded them in loadSavedSettings)
  // Connect to MQTT
  if (!connectToMqtt()) {
    // if fail, try a few times before falling back to portal
    mqttConnectAttempts++;
    for (int i = 0; i < MQTT_MAX_CONNECT_ATTEMPTS - 1 && mqttConnectAttempts < MQTT_MAX_CONNECT_ATTEMPTS; ++i) {
      delay(2000);
      if (connectToMqtt()) break;
      mqttConnectAttempts++;
    }
  }

  if (!mqtt.connected()) {
    Serial.println("Unable to connect to MQTT after attempts. Opening provisioning portal.");
    startConfigPortal();
    return;
  }

  // signal success
  beep();
  lastMqttMsgTime = millis();
}

void loop() {
  // keep MQTT alive
  if (!mqtt.connected()) {
    Serial.println("MQTT disconnected, reconnecting...");
    if (!connectToMqtt()) {
      // back-off a bit
      delay(2000);
    }
  } else {
    mqtt.loop();
  }

  // Optionally: if you want to deep-sleep after long idle to save power,
  // you can use FALLBACK_TO_DEEPSLEEP_SECONDS > 0. Deep-sleep disconnects MQTT
  if (FALLBACK_TO_DEEPSLEEP_SECONDS > 0) {
    unsigned long idleMs = millis() - lastMqttMsgTime;
    if (idleMs > (FALLBACK_TO_DEEPSLEEP_SECONDS * 1000UL)) {
      Serial.printf("Idle for %lu s. Going to deep sleep for %u s\n", idleMs/1000UL, FALLBACK_TO_DEEPSLEEP_SECONDS);
      // disable WiFi and deep sleep
      esp_sleep_enable_timer_wakeup((uint64_t)FALLBACK_TO_DEEPSLEEP_SECONDS * 1000000ULL);
      delay(100);
      esp_deep_sleep_start();
    }
  }

  if (digitalRead(RESET_BTN) == LOW) {
    handleResetPrefs();
  }

  delay(10); // allow background tasks to run
}
