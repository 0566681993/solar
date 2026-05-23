#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ModbusIP_ESP8266.h>

Preferences prefs;
WebServer server(80);
ModbusIP mb;

// ================= VERSION =================
String currentVersion = "1.0.0";

// ================= OTA =================
const char* firmwareURL =
  "https://github.com/0566681993/solar/releases/download/solar/solar.bin";

const char* versionURL =
  "https://raw.githubusercontent.com/0566681993/solar/main/version.txt";

// ================= SUPABASE =================
const char* SUPABASE_URL =
  "https://rtxdvcvzkepogytpnvsi.supabase.co";

const char* SUPABASE_KEY =
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJ0eGR2Y3Z6a2Vwb2d5dHBudnNpIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTMyNzI1NzksImV4cCI6MjA2ODg0ODU3OX0.SG_q8rnfAA7LAFoYYQyPsUFZPqsh1C13GbkGln4Z3JU";

const char* TABLE_NAME =
  "inverter_logs";

// ================= MODBUS =================
IPAddress remote(192, 168, 1, 84);

// ================= REGISTER =================
uint16_t r1[60];
uint16_t r2[130];
uint16_t r3[60];

// ================= TIMER =================
unsigned long lastRun = 0;
const unsigned long interval = 1000;

unsigned long lastOTA = 0;
const unsigned long otaInterval = 180000;

// ================= OTA FLAG =================
bool otaRunning = false;

// =====================================================
// WEB CONFIG
// =====================================================
void handleRoot() {

  String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;text-align:center;padding-top:40px;}"
    "input{width:250px;height:40px;font-size:18px;margin:10px;}"
    "button{width:260px;height:45px;font-size:18px;}"
    "</style>"
    "</head>"
    "<body>"
    "<h2>ESP32 WiFi Setup</h2>"
    "<form action='/save'>"
    "<input name='s' placeholder='WiFi Name'><br>"
    "<input name='p' type='password' placeholder='Password'><br>"
    "<button type='submit'>SAVE</button>"
    "</form>"
    "</body>"
    "</html>";
  server.send(200, "text/html", html);
}

void handleSave() {

  String ssid = server.arg("s");
  String pass = server.arg("p");

  prefs.begin("wifi", false);

  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);

  prefs.end();

  server.send(
    200,
    "text/html",
    "<h2>WiFi Saved<br>Restarting...</h2>"
  );

  delay(2000);

  ESP.restart();
}

// =====================================================
// WIFI CONNECT
// =====================================================
bool connectWiFi() {

  prefs.begin("wifi", true);

  String ssid =
    prefs.getString("ssid", "");

  String pass =
    prefs.getString("pass", "");

  prefs.end();

  if (ssid == "") {

    Serial.println("NO WIFI SAVED");

    return false;
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid.c_str(),
    pass.c_str()
  );

  Serial.print("CONNECTING WIFI");

  int t = 30;

  while (
    WiFi.status() != WL_CONNECTED &&
    t--
  ) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WIFI CONNECTED");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    return true;
  }

  Serial.println("WIFI FAIL");

  return false;
}

// =====================================================
// START AP
// =====================================================
void startAP() {

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    "Solar-SETUP",
    "12345678"
  );

  Serial.println("AP STARTED");

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);

  server.on("/save", handleSave);

  server.begin();
}

// =====================================================
// OTA
// =====================================================
void checkUpdate() {

  if (otaRunning) return;

  if (WiFi.status() != WL_CONNECTED)
    return;

  Serial.println("CHECK OTA");

  HTTPClient http;

  http.begin(versionURL);

  int code = http.GET();

  if (code == 200) {

    String latest =
      http.getString();

    latest.trim();

    if (latest != currentVersion) {

      Serial.println("NEW UPDATE");

      otaRunning = true;

      mb.disconnect(remote);

      delay(1000);

      WiFiClient client;

      t_httpUpdate_return ret =
        httpUpdate.update(
          client,
          firmwareURL
        );

      switch (ret) {

        case HTTP_UPDATE_FAILED:

          Serial.printf(
            "OTA FAIL %d\n",
            httpUpdate.getLastError()
          );

          break;

        case HTTP_UPDATE_NO_UPDATES:

          Serial.println("NO UPDATE");

          break;

        case HTTP_UPDATE_OK:

          Serial.println("OTA SUCCESS");

          break;
      }

      otaRunning = false;
    }
  }

  http.end();
}

// =====================================================
// CONVERT
// =====================================================
int32_t toS32(
  uint16_t h,
  uint16_t l
) {

  uint32_t v =
    ((uint32_t)h << 16) | l;

  return
    (v > 0x7FFFFFFF)
    ? v - 0x100000000
    : v;
}

uint32_t toU32(
  uint16_t h,
  uint16_t l
) {

  return
    ((uint32_t)h << 16) | l;
}

// =====================================================
// DATA VARIABLES
// =====================================================
float grid_voltage = 0;
float grid_current = 0;
float grid_frequency = 0;
float grid_power = 0;

float pv1_voltage = 0;
float pv1_current = 0;
float pv1_power = 0;

float battery_voltage = 0;
float battery_power = 0;
int battery_soc = 0;

float load_voltage = 0;
float load_frequency = 0;
float load_current = 0;
float load_power = 0;

float ivt_temp = 0;

// =====================================================
// MODBUS READ
// =====================================================
bool readInverter() {

  if (!mb.isConnected(remote)) {

    Serial.println("MODBUS CONNECT");

    if (!mb.connect(remote)) {

      Serial.println("MODBUS FAIL");

      return false;
    }

    delay(500);
  }

  bool ok1 =
    mb.readHreg(
      remote,
      0x1001,
      r1,
      60
    );

  mb.task();
  delay(100);

  bool ok2 =
    mb.readHreg(
      remote,
      0x1300,
      r2,
      99
    );

  mb.task();
  delay(100);

  bool ok3 =
    mb.readHreg(
      remote,
      0x2000,
      r3,
      20
    );

  mb.task();

  if (!(ok1 && ok2 && ok3)) {

    return false;
  }

  // =====================================================
  // GRID
  // =====================================================
  grid_frequency =
    r2[12] / 100.0;

  grid_current =
    r2[13] / 100.0;

  grid_voltage =
    r2[14] / 10.0;

  grid_power =
    toS32(
      r2[0],
      r2[1]
    ) / 10.0;

  // =====================================================
  // PV
  // =====================================================
  pv1_voltage =
    r1[1] / 10.0;

  pv1_current =
    r1[2] / 100.0;

  pv1_power =
    toU32(
      r1[17],
      r1[18]
    ) / 10.0;

  // =====================================================
  // BATTERY
  // =====================================================
  battery_soc =
    r3[0];

  battery_voltage =
    r3[5] / 10.0;

  battery_power =
    -toS32(
      r3[9],
      r3[10]
    ) / 10.0;

  // =====================================================
  // LOAD
  // =====================================================
  load_frequency =
    r2[36] / 100.0;

  load_voltage =
    r2[37] / 10.0;

  load_current =
    r2[38];

  load_power =
    toU32(
      r2[40],
      r2[41]
    ) / 10.0;

  // =====================================================
  // TEMP
  // =====================================================
  ivt_temp =
    r2[47] / 10.0;

  return true;
}

// =====================================================
// SUPABASE
// =====================================================
void sendSupabase() {

  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;

  String url =
    String(SUPABASE_URL) +
    "/rest/v1/" +
    TABLE_NAME +
    "?on_conflict=device_id";

  http.begin(url);

  http.addHeader("apikey", SUPABASE_KEY);

  http.addHeader(
    "Authorization",
    String("Bearer ") + SUPABASE_KEY
  );

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  http.addHeader(
    "Prefer",
    "resolution=merge-duplicates"
  );

  String json =
    "{"

    "\"device_id\":\"SEN ECO 6KW\","

    "\"grid_voltage_l1\":" +
    String(grid_voltage) + ","

    "\"grid_current_l1\":" +
    String(grid_current) + ","

    "\"grid_frequency\":" +
    String(grid_frequency) + ","

    "\"grid_power_r\":" +
    String(grid_power) + ","

    "\"pv1_voltage\":" +
    String(pv1_voltage) + ","

    "\"pv1_current\":" +
    String(pv1_current) + ","

    "\"pv1_power\":" +
    String(pv1_power) + ","

    "\"battery_voltage\":" +
    String(battery_voltage) + ","

    "\"battery_power\":" +
    String(battery_power) + ","

    "\"battery_soc\":" +
    String(battery_soc) + ","

    "\"load_voltage_l1\":" +
    String(load_voltage) + ","

    "\"load_frequency\":" +
    String(load_frequency) + ","

    "\"load_current\":" +
    String(load_current) + ","

    "\"load_power_r\":" +
    String(load_power) + ","

    "\"inverter_temp\":" +
    String(ivt_temp) +
    "}";

  int code = http.POST(json);
  Serial.print("SUPABASE: ");
  Serial.println(code);

  Serial.println(json);

  Serial.println(http.getString());

  http.end();
}

// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  if (connectWiFi()) {

    checkUpdate();
  }
  else {

    startAP();
  }

  mb.client();
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  mb.task();
  server.handleClient();
  // OTA TIMER
  if (
    millis() - lastOTA >= otaInterval
  ) {

    lastOTA = millis();

    checkUpdate();
  }

  // OTA RUNNING
  if (otaRunning) {

    delay(100);

    return;
  }

  // READ MODBUS
  if (
    millis() - lastRun >= interval
  ) {

    lastRun = millis();

    if (!readInverter()) {

      Serial.println();
      Serial.println("===== INVERTER =====");

      Serial.print("GRID POWER: ");
      Serial.println(grid_power);

      Serial.print("GRID VOLT: ");
      Serial.println(grid_voltage);

      Serial.print("GRID CURR: ");
      Serial.println(grid_current);

      Serial.print("GRID FREQ: ");
      Serial.println(grid_frequency);

      Serial.print("PV POWER: ");
      Serial.println(pv1_power);

      Serial.print("PV VOLT: ");
      Serial.println(pv1_voltage);

      Serial.print("PV CURR: ");
      Serial.println(pv1_current);

      Serial.print("BAT VOLT: ");
      Serial.println(battery_voltage);

      Serial.print("BAT POWER: ");
      Serial.println(battery_power);

      Serial.print("SOC: ");
      Serial.println(battery_soc);

      Serial.print("LOAD POWER: ");
      Serial.println(load_power);

      Serial.print("LOAD %: ");
      Serial.println(load_current);

      Serial.print("IVT TEMP: ");
      Serial.println(ivt_temp);

      sendSupabase();
    }
    else {

      Serial.println("MODBUS READ FAIL");
    }
  }
}