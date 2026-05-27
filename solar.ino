#define LED_PIN 48      // ESP32-S3 DevKit thường là GPIO48 
#define LED_COUNT 1
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ModbusIP_ESP8266.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

Preferences prefs;
WebServer server(80);
ModbusIP mb;
// ================= BLE BMS =================


String bmsRaw = "";

// MAC BMS

// ================= VERSION =================
String currentVersion = "1.0.3";

// ================= OTA =================
String firmwareURL =
"https://raw.githubusercontent.com/0566681993/solar/main/solar.bin";

const char* versionURL =
  "https://raw.githubusercontent.com/0566681993/solar/main/version.txt";

// ================= SUPABASE =================
const char* SUPABASE_URL =
  "https://rtxdvcvzkepogytpnvsi.supabase.co";

const char* SUPABASE_KEY =
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJ0eGR2Y3Z6a2Vwb2d5dHBudnNpIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTMyNzI1NzksImV4cCI6MjA2ODg0ODU3OX0.SG_q8rnfAA7LAFoYYQyPsUFZPqsh1C13GbkGln4Z3JU";

const char* TABLE_NAME =
  "inverter_logs";

// LED WIFI

void setLED(int r, int g, int b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ================= MODBUS =================
IPAddress remote(192,168,1,84);

// ================= REGISTER =================
uint16_t r1[60];
uint16_t r2[100];
uint16_t r3[20];
uint16_t r4[8];

// ================= TIMER =================
unsigned long lastRun = 0;
const unsigned long interval = 3000;

unsigned long lastOTA = 0;
const unsigned long otaInterval = 100000;

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
    
    setLED(0, 255, 0);   // 🔵 XANH LÁ

    return true;
  }

  Serial.println("WIFI FAIL");
  setLED(255, 0, 0); // đỏ
  return false;
}
// ================= WIFI CHECK =================
void checkWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WIFI LOST");

  setLED(255, 0, 0);

  WiFi.disconnect(true);  // 🔥 xóa state cũ (quan trọng)

  delay(100);

  // 🔥 QUAN TRỌNG: dùng begin lại từ dữ liệu đã lưu
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid == "") return;

  WiFi.begin(ssid.c_str(), pass.c_str());

  int t = 10;
  while (WiFi.status() != WL_CONNECTED && t--) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWIFI RECONNECTED");

    setLED(0, 255, 0);
  }
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
  http.setTimeout(3000);
  int code = http.GET();

  if (code == 200) {

    String latest =
      http.getString();

    latest.trim();
    Serial.println(currentVersion);
    Serial.println(latest);
    if (latest != currentVersion) {

      Serial.println("NEW UPDATE");

      otaRunning = true;

      mb.disconnect(remote);

      delay(1000);
      WiFiClientSecure client;

      client.setInsecure();

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
  else {
  Serial.print("VERSION FAIL: ");
}

  http.end();
}

// =====================================================
// CONVERT
// =====================================================
uint32_t toS32(uint16_t h,uint16_t l) {

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

// CONVERT SN
String decodeU16ASCII(uint16_t *buf, int len) {
  String s = "";

  for (int i = 0; i < len; i++) {
    char c1 = buf[i] >> 8;
    char c2 = buf[i] & 0xFF;

    if (c1) s += c1;
    if (c2) s += c2;
  }

  return s;
}
String readU16ASCII(uint16_t *data, int len) {

  String s = "";

  for (int i = 0; i < len; i++) {

    char high = data[i] >> 8;
    char low  = data[i] & 0xFF;

    if (high != 0) s += high;
    if (low != 0) s += low;
  }

  return s;
}
// =====================================================
// DATA VARIABLES
// =====================================================
float grid_voltage = 0; // điện áp
float grid_current = 0; // dòng
float grid_frequency = 0; // tần số
float grid_power = 0; // công suất
float grid_buy_today =0; // lấy lưới hôm nay
float grid_sel_today = 0;

float pv1_voltage = 0; // điện áp
float pv1_current = 0; //dòng
float pv1_power = 0; // công suất
float pv2_voltage = 0; // điện áp
float pv2_current = 0; //dòng
float pv2_power = 0; // công suất
float pv_power = 0; // tổng công suất
float pv_power_peak = 0; //công suất đỉnh
float pv_today = 0; // sản lượng hôm nay
float pv_total = 0; // tổng sản lượng

float battery_voltage = 0; // điện áp
float battery_power = 0; // công suất
float battery_soc = 0; // phần trăm pin
float battery_temp = 0; // nhiệt độ
float battery_current =0;
float battery_changer_today = 0; // sạc hôm nay
float battery_dischanger_today = 0; // xả hôm nay
float battery_total_changer = 0; // tổng sạc
float battery_total_dischanger = 0; // tổng xả

float load_voltage = 0; //điện áp
float load_frequency = 0; // tần số
float load_current = 0; // dòng
float load_power = 0; //công suất
float load_today = 0; //sử dụng hôm nay
float load_total = 0; //tổng sử dụng

float eps_voltage = 0; //điện áp
float eps_frequency = 0; // tần số
float eps_current = 0; // dòng
float eps_power = 0; //công suất
float eps_today = 0; //sử dụng hôm nay
float eps_total = 0; //tổng sử dụng

String ivt_sn = ""; //serial
float ivt_temp = 0; //nhiệt độ biến tần

// =====================================================
// MODBUS READ
// =====================================================
bool waitModbus(unsigned long timeout = 200) {

  unsigned long start = millis();

  while (millis() - start < timeout) {

    mb.task();

    delay(1);
  }

  return true;
}
bool readInverter() {

  // =====================================================
  // CONNECT
  // =====================================================
  if (!mb.isConnected(remote)) {

    Serial.println("MODBUS CONNECT...");

    mb.connect(remote);

    unsigned long start = millis();

    while (!mb.isConnected(remote)) {

      mb.task();
      delay(10);

      if (millis() - start > 5000) {

        Serial.println("MODBUS CONNECT FAIL");

        mb.disconnect(remote);

        return false;
      }
    }

    Serial.println("MODBUS CONNECTED");
  }

  // =====================================================
  // READ R1
  // =====================================================
  if (!mb.readHreg(remote, 0x1001, r1, 30))
    return false;

  if (!waitModbus())
    return false;

  if (!mb.readHreg(remote, 0x101F, &r1[30], 30))
    return false;

  if (!waitModbus())
    return false;

  // =====================================================
  // READ R2
  // =====================================================

  if (!mb.readHreg(remote, 0x1300, r2, 30))
    return false;

  if (!waitModbus())
    return false;

  if (!mb.readHreg(remote, 0x131E, &r2[30], 30))
    return false;

  if (!waitModbus())
    return false;

  // block 0x133C (split 3 lần 10 reg)
  for (int i = 0; i < 30; i += 10) {

    if (!mb.readHreg(remote, 0x133C + i, &r2[60 + i], 10))
      return false;

    if (!waitModbus())
      return false;
  }

  // block cuối
  if (!mb.readHreg(remote, 0x135A, &r2[90], 10))
    return false;

  if (!waitModbus())
    return false;

  // =====================================================
  // READ R3
  // =====================================================

  if (!mb.readHreg(remote, 0x2000, r3, 10))
    return false;

  if (!waitModbus())
    return false;

  if (!mb.readHreg(remote, 0x200A, &r3[10], 10))
    return false;

  if (!waitModbus())
    return false;

  // =====================================================
  // READ R4
  // =====================================================

  if (!mb.readHreg(remote, 0x1A10, r4, 8))
    return false;

  if (!waitModbus())
    return false;

  // =====================================================
  // GRID
  // =====================================================

  grid_frequency = r2[56] / 100;
  grid_current   = toS32(r2[29], r2[30]) / 100;
  grid_voltage   = r2[26] / 10;
  grid_power     = toS32(r2[0], r2[1]) / 10;
  grid_buy_today = toU32(r2[50], r2[51]);
  grid_sel_today = toU32(r2[52], r2[53]);

  // =====================================================
  // PV
  // =====================================================

  pv1_voltage = r1[15] / 10.0;
  pv1_current = r1[16] / 100.0;
  pv1_power   = toU32(r1[17], r1[18]) / 10.0;

  pv2_voltage = r1[19] / 10.0;
  pv2_current = r1[20] / 100.0;
  pv2_power   = toU32(r1[21], r1[22]) / 10.0;
  pv_power      = pv1_power + pv2_power;
  pv_power_peak = toS32(r1[58], r1[59]) ;
  pv_today      = toU32(r1[38], r1[39]);
  pv_total      = toU32(r1[32], r1[33]);

  // =====================================================
  // BATTERY
  // =====================================================

  battery_soc     = r3[0];
  battery_voltage = r3[6] / 10.0;
  battery_power   = -toS32(r3[9], r3[10]) / 10.0;
  battery_temp    = (int16_t)r3[1];
  battery_current      = -toS32(r3[8], r3[7]);
  battery_changer_today      = toU32(r3[11], r3[12]);
  battery_total_changer      = toU32(r3[13], r3[14]);
  battery_dischanger_today   = toU32(r3[15], r3[16]);
  battery_total_dischanger   = toU32(r3[17], r3[18]);

  // =====================================================
  // LOAD
  // =====================================================

  load_voltage = r2[35] / 10;
  load_current = toS32(r2[38], r2[39]) / 100;
  load_power   = toU32(r2[10], r2[11]) / 10;
  load_today   = toU32(r2[54], r2[55]);
  load_total   = toU32(r2[16], r2[17]);

  // =====================================================
  // EPS
  // =====================================================

  eps_voltage   = r2[80] / 10;
  eps_frequency = r2[85] / 100;
  eps_current   = toS32(r2[81], r2[82]) / 10;
  eps_power     = toS32(r2[83], r2[84]) / 10;
  eps_today     = toU32(r2[96], r2[97]);
  eps_total     = toU32(r2[98], r2[99]);

  // =====================================================
  // INVERTER
  // =====================================================

  ivt_sn   = readU16ASCII(r4, 8);
  ivt_temp = r1[27];

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
  http.setTimeout(3000);

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

  // ================= SAFE LAMBDA =================
  auto f = [](float v) -> String {
    if (isnan(v) || isinf(v)) return "0";
    return String(v, 2);
  };

  auto i = [](int v) -> String {
    if (isnan(v) || isinf(v)) return "0";
    return String(v);
  };

  auto s = [](String v) -> String {
    if (v.length() == 0) return "UNKNOWN";
    return v;
  };

  String json =
    "{"
    "\"device_id\":\"SEN ECO 6KW\",";

  // ================= GRID =================
  json += "\"grid_voltage\":" + f(grid_voltage) + ",";
  json += "\"grid_current\":" + f(grid_current) + ",";
  json += "\"grid_frequency\":" + f(grid_frequency) + ",";
  json += "\"grid_power\":" + f(grid_power) + ",";
  json += "\"grid_buy_today\":" + f(grid_buy_today) + ",";
  json += "\"grid_sel_today\":" + f(grid_sel_today) + ",";

  // ================= PV =================
  json += "\"pv1_voltage\":" + f(pv1_voltage) + ",";
  json += "\"pv1_current\":" + f(pv1_current) + ",";
  json += "\"pv1_power\":" + f(pv1_power) + ",";
  json += "\"pv2_voltage\":" + f(pv2_voltage) + ",";
  json += "\"pv2_current\":" + f(pv2_current) + ",";
  json += "\"pv2_power\":" + f(pv2_power) + ",";
  json += "\"pv_power\":" + f(pv_power) + ",";
  json += "\"pv_power_peak\":" + f(pv_power_peak) + ",";
  json += "\"pv_today\":" + f(pv_today) + ",";
  json += "\"pv_total\":" + f(pv_total) + ",";

  // ================= BATTERY =================
  json += "\"battery_voltage\":" + f(battery_voltage) + ",";
  json += "\"battery_power\":" + f(battery_power) + ",";
  json += "\"battery_soc\":" + i(battery_soc) + ",";
  json += "\"battery_temp\":" + f(battery_temp) + ",";
  json += "\"battery_current\":" + f(battery_current) + ",";
  json += "\"battery_changer_today\":" + f(battery_changer_today) + ",";
  json += "\"battery_total_changer\":" + f(battery_total_changer) + ",";
  json += "\"battery_dischanger_today\":" + f(battery_dischanger_today) + ",";
  json += "\"battery_total_dischanger\":" + f(battery_total_dischanger) + ",";

  // ================= LOAD =================
  json += "\"load_voltage\":" + f(load_voltage) + ",";
  json += "\"load_current\":" + f(load_current) + ",";
  json += "\"load_power\":" + f(load_power) + ",";
  json += "\"load_today\":" + f(load_today) + ",";
  json += "\"load_total\":" + f(load_total) + ",";

  // ================= EPS =================
  json += "\"eps_voltage\":" + f(eps_voltage) + ",";
  json += "\"eps_frequency\":" + f(eps_frequency) + ",";
  json += "\"eps_power\":" + f(eps_power) + ",";
  json += "\"eps_current\":" + f(eps_current) + ",";
  json += "\"eps_today\":" + f(eps_today) + ",";
  json += "\"eps_total\":" + f(eps_total) + ",";

  // ================= SN + TEMP =================
  json += "\"ivt_sn\":\"" + s(ivt_sn) + "\",";
  json += "\"ivt_temp\":" + f(ivt_temp);

  json += "}";

  // ================= DEBUG ================

  int code = http.POST(json);

  Serial.println(code);
  http.end();
}


// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);
  led.begin();
  led.setBrightness(10);
  led.show(); // tắt ban đầu
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  if (connectWiFi()) {
    setLED(0, 255, 0);   // xanh lá
    checkUpdate();
    server.begin();
  }
  else {
    setLED(255, 0, 0);   // đỏ
    startAP();
  }

  mb.client();

}

// =====================================================
// LOOP
// =====================================================
void loop() {

  // ================= BACKGROUND TASK =================
  mb.task();

  server.handleClient();

  delay(1);

  // ================= WIFI CHECK =================
  static unsigned long lastWiFiCheck = 0;

  if (millis() - lastWiFiCheck >= 10000) {

    lastWiFiCheck = millis();

    checkWiFi();
  }

  // ================= OTA TIMER =================
  if (millis() - lastOTA >= otaInterval) {

    lastOTA = millis();

    checkUpdate();
  }

  // ================= OTA RUNNING =================
  if (otaRunning) {

    delay(100);

    return;
  }

  // ================= MAIN LOOP =================
  if (millis() - lastRun >= interval) {

    lastRun = millis();

    unsigned long cycleStart = millis();

    // ================= MODBUS =================
    unsigned long modbusStart = millis();
    uint16_t test;

    bool ok = readInverter();

    unsigned long modbusTime =
      millis() - modbusStart;

    Serial.print("MODBUS TIME: ");
    Serial.println(modbusTime);

    // ================= SUPABASE =================
    if (ok) {

      unsigned long supabaseStart = millis();

      sendSupabase();

      unsigned long supabaseTime =
        millis() - supabaseStart;

      Serial.print("SUPABASE TIME: ");
      Serial.println(supabaseTime);
    }

  }
}
