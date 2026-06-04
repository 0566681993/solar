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
#include <PubSubClient.h>
#include <NimBLEDevice.h>

static NimBLEAddress targetAddress("A4:C1:38:07:43:80", BLE_ADDR_PUBLIC);
NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* ffe1 = nullptr;

static uint8_t seqCounter = 0;
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

const char* MQTT_SERVER="272917f2a9904b409d89a9f5a3d8d058.s1.eu.hivemq.cloud";
const int MQTT_PORT=8883;

const char* MQTT_USER="0566681993";
const char* MQTT_PASS="Anhtan1993";
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

Preferences prefs;
WebServer server(80);
ModbusIP mb;
// ================= BLE BMS =================


String bmsRaw = "";

// MAC BMS

// ================= VERSION =================
String currentVersion = "1.0.2";

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
uint16_t r5[34];

// ================= TIMER =================
unsigned long lastRun = 0;
const unsigned long interval = 3000;

unsigned long lastOTA = 0;
const unsigned long otaInterval = 100000;

// ================= OTA FLAG =================
bool otaRunning = false;

// ===== BMS DATA =====
float bms_pack_voltage = 0;
float bms_pack_power = 0;
float bms_pack_current = 0;
float bms_maxcharge=0;
float bms_maxdischarge=0;
float bms_charge =0;
float bms_discharge=0;
float bms_balance=0;
float bms_volt_dif=0;
float bms_soc=0;
float bms_temp=0;
float bms_capacity=0;
float bms_cycle=0;
float bms_oncharge=0;
float bms_ondischarge=0;
float bms_onbalance=0;
float bms_uvp = 0;
float bms_ovp = 0;
float bms_uvpr = 0;
float bms_ovpr = 0;
float bms_rfv=0;
float bms_rcv=0;
float bms_soc100 =0;
float bms_soc0=0;
float bms_celloff=0;
    
//======== MQTT
bool readBmsSettingReq = false;
bool readIvtSettingReq = false;
 
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
int32_t toS32(uint16_t h,uint16_t l) {

  uint32_t v =
    ((uint32_t)h << 16) | l;

  return (int32_t)v;
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

float prm_mode = 0; //cômode
float prm_bat_type = 0; //sử dụng hôm nay
float prm_dischanger = 0; //tổng sử dụng
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
  // READ R5
  // =====================================================

  if (!mb.readHreg(remote, 0x2100, r5, 34))
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
  battery_current = -toS32(r3[7], r3[8]) / 100.0;
  battery_changer_today      = toU32(r3[11], r3[12]);
  battery_total_changer      = toU32(r3[13], r3[14]);
  battery_dischanger_today   = toU32(r3[15], r3[16]);
  battery_total_dischanger   = toU32(r3[17], r3[18]);
  //=====================================================
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
  // parameter
  prm_mode = r5[0];
  prm_bat_type = r5[6];
  prm_dischanger = r5[9];
  return true;
}

//MQTT/////////////////
void connectMQTT() {

  while (!mqtt.connected()) {

    Serial.println("MQTT CONNECT...");

    String cid =
      "SOLAR-" +
      String(random(0xffff), HEX);

    bool ok =
      mqtt.connect(
        cid.c_str(),
        MQTT_USER,
        MQTT_PASS
      );

    if (ok) {

      Serial.println("MQTT CONNECTED");
      mqtt.subscribe("solar/bms/cmd");

      mqtt.subscribe("solar/inverter/cmd"); 
      Serial.println("SUBSCRIBE OK");          

    }
    else {

      Serial.print("MQTT FAIL:");

      Serial.println(
        mqtt.state()
      );

      delay(3000);
    }
  }
}
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    String msg;

    for(unsigned int i=0;i<length;i++)
        msg += (char)payload[i];

    Serial.print("MQTT RX ");
    Serial.print(topic);
    Serial.print(" = ");
    Serial.println(msg);

    if(String(topic) == "solar/bms/cmd")
    {
        if(msg == "read_setting")
            readBmsSettingReq = true;
    }

    if(String(topic) == "solar/inverter/cmd")
    {
        if(msg == "read_setting")
            readIvtSettingReq = true;
    }
}

void sendMQTT() {

  if (!mqtt.connected())
      return;

  String json="{";

  // ===== DEVICE =====
  json += "\"device_id\":\"SEN ECO 6KW\",";

  // ===== GRID =====
  json += "\"grid_voltage\":"+String(grid_voltage,1)+",";
  json += "\"grid_current\":"+String(grid_current,1)+",";
  json += "\"grid_frequency\":"+String(grid_frequency,0)+",";
  json += "\"grid_power\":"+String(grid_power,0)+",";
  json += "\"grid_buy_today\":"+String(grid_buy_today,0)+",";
  json += "\"grid_sel_today\":"+String(grid_sel_today,0)+",";

  // ===== PV =====
  json += "\"pv1_voltage\":"+String(pv1_voltage,1)+",";
  json += "\"pv1_current\":"+String(pv1_current,1)+",";
  json += "\"pv1_power\":"+String(pv1_power,0)+",";

  json += "\"pv2_voltage\":"+String(pv2_voltage,1)+",";
  json += "\"pv2_current\":"+String(pv2_current,1)+",";
  json += "\"pv2_power\":"+String(pv2_power,0)+",";

  json += "\"pv_power\":"+String(pv_power,0)+",";
  json += "\"pv_power_peak\":"+String(pv_power_peak,0)+",";
  json += "\"pv_today\":"+String(pv_today,0)+",";
  json += "\"pv_total\":"+String(pv_total,0)+",";

  // ===== BATTERY =====
  json += "\"battery_soc\":"+String(battery_soc,1)+",";
  json += "\"battery_voltage\":"+String(battery_voltage,1)+",";
  json += "\"battery_current\":"+String(battery_current,1)+",";
  json += "\"battery_power\":"+String(battery_power,0)+",";
  json += "\"battery_temp\":"+String(battery_temp,0)+",";

  json += "\"battery_changer_today\":"+String(battery_changer_today,0)+",";
  json += "\"battery_total_changer\":"+String(battery_total_changer,0)+",";
  json += "\"battery_dischanger_today\":"+String(battery_dischanger_today,0)+",";
  json += "\"battery_total_dischanger\":"+String(battery_total_dischanger,0)+",";

  // ===== LOAD =====
  json += "\"load_voltage\":"+String(load_voltage,1)+",";
  json += "\"load_current\":"+String(load_current,1)+",";
  json += "\"load_power\":"+String(load_power,0)+",";
  json += "\"load_today\":"+String(load_today,0)+",";
  json += "\"load_total\":"+String(load_total,0)+",";

  // ===== EPS =====
  json += "\"eps_voltage\":"+String(eps_voltage,1)+",";
  json += "\"eps_frequency\":"+String(eps_frequency,0)+",";
  json += "\"eps_current\":"+String(eps_current,1)+",";
  json += "\"eps_power\":"+String(eps_power,0)+",";
  json += "\"eps_today\":"+String(eps_today,0)+",";
  json += "\"eps_total\":"+String(eps_total,0)+",";

  // ===== INFO =====
  json += "\"ivt_sn\":\""+String(ivt_sn)+"\",";
  json += "\"ivt_temp\":"+String(ivt_temp,0)+",";

  // ===== PARAM =====
  json += "\"prm_mode\":"+String(prm_mode,0)+",";
  json += "\"prm_bat_type\":"+String(prm_bat_type,0)+",";
  json += "\"prm_dischanger\":"+String(prm_dischanger,0);

  json += "}";

  mqtt.publish(
      "solar/inverter/data",
      json.c_str(),
      true
  );

}
//
//BMS/////////////////////
// ===== GLOBAL =====
// send Mqtt setting
void sendBMSSettingMQTT()
{
    if(!mqtt.connected()) return;

    String json = "{";
    json += "\"bms_uvpr\":" + String(bms_uvpr,3) + ",";
    json += "\"bms_ovpr\":" + String(bms_ovpr,3) + ",";
    json += "\"bms_rcv\":" + String(bms_rcv,3) + ",";
    json += "\"bms_rfv\":" + String(bms_rfv,3) + ",";
    json += "\"bms_soc100\":" + String(bms_soc100,3) + ",";
    json += "\"bms_soc0\":" + String(bms_soc0,3) + ",";
    json += "\"bms_celloff\":" + String(bms_celloff,3) + ",";
    json += "\"bms_maxcharge\":" + String(bms_maxcharge,0) + ",";
    json += "\"bms_maxdischarge\":" + String(bms_maxdischarge,0) + ",";
    json += "\"bms_oncharge\":" + String(bms_oncharge,0) + ",";
    json += "\"bms_ondischarge\":" + String(bms_ondischarge,0) + ",";
    json += "\"bms_onbalance\":" + String(bms_onbalance,0) + ",";
                        
    json += "\"bms_uvp\":" + String(bms_uvp,3) + ",";
    json += "\"bms_ovp\":" + String(bms_ovp,3);

    json += "}";

    mqtt.publish(
        "solar/bms/setting",
        json.c_str(),
        true
    );
 
    Serial.println(json);
}

// ===== PARSE FULL FRAME =====
void parseFrame(uint8_t* data, uint16_t len)
{
    
    if(len < 6) return;

    uint8_t type = data[4];

    // ===== 0x01 SETTINGS =====
    if(type == 0x01)
    {
        if(len < 22) return;

        bms_uvp =
            ((uint32_t)data[10] |
            ((uint32_t)data[11] << 8) |
            ((uint32_t)data[12] << 16) |
            ((uint32_t)data[13] << 24))
            /1000.0f;
        bms_uvpr =
            ((uint32_t)data[14] |
            ((uint32_t)data[15] << 8) |
            ((uint32_t)data[16] << 16) |
            ((uint32_t)data[17] << 24))
            /1000.0f;
        
        bms_ovp =
            ((uint32_t)data[18] |
            ((uint32_t)data[19] << 8) |
            ((uint32_t)data[20] << 16) |
            ((uint32_t)data[21] << 24))
            /1000.0f;
        bms_ovpr =
            ((uint32_t)data[22] |
            ((uint32_t)data[23] << 8) |
            ((uint32_t)data[24] << 16) |
            ((uint32_t)data[25] << 24))
            /1000.0f;
        bms_soc100 =
            ((uint32_t)data[30] |
            ((uint32_t)data[31] << 8) |
            ((uint32_t)data[32] << 16) |
            ((uint32_t)data[33] << 24))
            /1000.0f;
        bms_soc0 =
            ((uint32_t)data[34] |
            ((uint32_t)data[35] << 8) |
            ((uint32_t)data[36] << 16) |
            ((uint32_t)data[37] << 24))
            /1000.0f;
        bms_rcv =
            ((uint32_t)data[38] |
            ((uint32_t)data[39] << 8) |
            ((uint32_t)data[40] << 16) |
            ((uint32_t)data[41] << 24))
            /1000.0f;
        bms_rfv =
            ((uint32_t)data[42] |
            ((uint32_t)data[43] << 8) |
            ((uint32_t)data[44] << 16) |
            ((uint32_t)data[45] << 24))
            /1000.0f;
        bms_celloff =
            ((uint32_t)data[46] |
            ((uint32_t)data[47] << 8) |
            ((uint32_t)data[48] << 16) |
            ((uint32_t)data[49] << 24))
            /1000.0f;
        bms_maxcharge =
            ((uint32_t)data[50] |
            ((uint32_t)data[51] << 8) |
            ((uint32_t)data[52] << 16) |
            ((uint32_t)data[53] << 24))
            /1000.0f;
        bms_maxdischarge =
            ((uint32_t)data[62] |
            ((uint32_t)data[63] << 8) |
            ((uint32_t)data[64] << 16) |
            ((uint32_t)data[65] << 24))
            /1000.0f;
        bms_oncharge =
            ((uint32_t)data[118] |
            ((uint32_t)data[119] << 8) |
            ((uint32_t)data[120] << 16) |
            ((uint32_t)data[121] << 24))
            /1000.0f;
        bms_ondischarge =
            ((uint32_t)data[122] |
            ((uint32_t)data[123] << 8) |
            ((uint32_t)data[124] << 16) |
            ((uint32_t)data[125] << 24))
            /1000.0f;
        bms_onbalance =
            ((uint32_t)data[126] |
            ((uint32_t)data[127] << 8) |
            ((uint32_t)data[128] << 16) |
            ((uint32_t)data[129] << 24))
            /1000.0f;
                                                                                        
        sendBMSSettingMQTT();
    }
    // ===== 0x02 CELL INFO =====
    else if(type == 0x02)
    {
        if(len < 300) return;
                
        bms_pack_voltage =
            ((uint32_t)data[150] |
            ((uint32_t)data[151] << 8) |
            ((uint32_t)data[152] << 16) |
            ((uint32_t)data[153] << 24))
            *0.001f;
        bms_pack_power =
              ((uint32_t)data[154] |
              ((uint32_t)data[155] << 8) |
              ((uint32_t)data[156] << 16) |
              ((uint32_t)data[157] << 24))
              *0.001f;
        bms_volt_dif =
                ((uint32_t)data[76] |
                ((uint32_t)data[77] << 8))
                /1000.0f;
        bms_soc =(uint8_t)data[173] ;
        
        bms_capacity =
              ((uint32_t)data[174] |
              ((uint32_t)data[175] << 8) |
              ((uint32_t)data[176] << 16) |
              ((uint32_t)data[177] << 24))
              *0.001f;

        bms_cycle =
              ((uint32_t)data[182] |
              ((uint32_t)data[183] << 8) |
              ((uint32_t)data[184] << 16) |
              ((uint32_t)data[185] << 24))
              ;

        bms_charge    = data[198];
        bms_discharge = data[199];
        bms_balance     = data[201];                                                
        bms_temp =
            ((uint16_t)data[144] |
            ((uint16_t)data[145] << 8))*0.1f
            ;
        
        int32_t rawCurrent =
            (int32_t)(
                ((uint32_t)data[158]) |
                ((uint32_t)data[159] << 8) |
                ((uint32_t)data[160] << 16) |
                ((uint32_t)data[161] << 24)
            );
        bms_pack_current =rawCurrent * 0.001f;

    }

}
        
// CRC SUM8
uint8_t calcCRC(const uint8_t* data, uint16_t len)
{
    uint8_t crc = 0;

    for(uint16_t i=0;i<len;i++)
    {
        crc += data[i];
    }

    return crc;
}

static uint8_t frameBuf[400];
static uint16_t frameLen=0;

void notifyCallback(
    NimBLERemoteCharacteristic* chr,
    uint8_t* data,
    size_t len,
    bool isNotify)
{
    if(len==0) return;

    bool header =
        len>=4 &&
        data[0]==0x55 &&
        data[1]==0xAA &&
        data[2]==0xEB &&
        data[3]==0x90;

    //Serial.printf(
   //     "\nNOTIFY len=%u header=%d\n",len,header);

    // ignore AT\r\n or random packets
    if(!header &&
       len<=8 &&
       frameLen==0)
    {
       // Serial.println(
       // "IGNORE stray packet" );
        return;
    }

    // new frame arrived
    if(header)
    {
        if(frameLen>=300 &&
           frameLen<=320)
        {
            uint8_t type =
                frameBuf[4];

            uint8_t crcCalc =
                calcCRC(
                    frameBuf,
                    frameLen-1
                );

            uint8_t crcRecv =
                frameBuf[frameLen-1];

            if(crcCalc==crcRecv)
            {

                parseFrame(
                    frameBuf,
                    frameLen
                );
            }
            else
            {
               // Serial.println(
                //    "CRC FAIL DROP"
               // );
            }
        }

        frameLen=0;
    }

    // ignore tiny junk while collecting
    if(!header &&
       len<=8 &&
       frameLen>0)
    {

        return;
    }

    if(frameLen+len >
       sizeof(frameBuf))
    {

        frameLen=0;
        return;
    }

    memcpy(
        frameBuf+frameLen,
        data,
        len
    );

    frameLen += len;
    if(frameLen>320)
    {

        frameLen=0;
    }
}


// ===== SEND =====
void sendCommand(uint8_t cmd)
{
    if(!ffe1) return;

    uint8_t frame[20]={0};

    frame[0]=0xAA;
    frame[1]=0x55;
    frame[2]=0x90;
    frame[3]=0xEB;

    frame[4]=cmd;
    frame[5]=0x00;

    frame[16]=seqCounter++;

    frame[19]=calcCRC(frame,19);

    bool ok=false;

    if(ffe1->canWrite())
        ok=ffe1->writeValue(frame,20,true);

    else if(ffe1->canWriteNoResponse())
        ok=ffe1->writeValue(frame,20,false);

}

void sendBMSMQTT()
{
    if (!mqtt.connected()) return;

    String json = "{";
    json += "\"pack\":" + String(bms_pack_voltage, 2) + ",";
    json += "\"pack_power\":" + String(bms_pack_power,0) + ",";
    json += "\"pack_current\":" + String(bms_pack_current, 2) + ",";
    json += "\"bms_temp\":" + String(bms_temp, 1) + ",";
    json += "\"bms_capacity\":" + String(bms_capacity, 0) + ",";
    json += "\"bms_capacity2\":" + String(bms_charge,0) + ",";
    json += "\"bms_discharge\":" + String(bms_discharge,0) + ",";
    json += "\"bms_balance\":" + String(bms_balance,0) + ",";
    json += "\"bms_cycle\":" + String(bms_cycle,0) + ",";                    
    json += "\"bms_volt_dif\":" + String(bms_volt_dif, 4) + ",";
    json += "\"bms_soc\":" + String(bms_soc);
    json += "}";

    mqtt.publish("solar/bms/data", json.c_str(), true);
}
// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);
  led.begin();
  led.setBrightness(5);
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
  espClient.setInsecure();
  
  mqtt.setServer(
      MQTT_SERVER,
      MQTT_PORT
  );
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);
  mqtt.setBufferSize(4096);
  mb.client();
  NimBLEDevice::init("");
  client = NimBLEDevice::createClient();
  Serial.println("Connecting...");
  if (!client->connect(targetAddress))
  {
      Serial.println("Connect failed");
      return;
  }

  Serial.println("Connected");
  auto svc = client->getService("FFE0");
  ffe1 = svc->getCharacteristic("FFE1");
  if (ffe1->canNotify())
  {
      if (ffe1->subscribe(true, notifyCallback))
          Serial.println("FFE1 NOTIFY OK");
      else
          Serial.println("FFE1 NOTIFY FAIL");
  }

  delay(1000);
  sendCommand(0x97);
  delay(1000);
  sendCommand(0x96);

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
  if (!mqtt.connected()) {
        connectMQTT();
    }
    
    mqtt.loop();
    if(readBmsSettingReq)
{
    readBmsSettingReq = false;

    Serial.println("SEND 0x97");

    sendCommand(0x97);
    delay(1000);
    sendCommand(0x96);
}
  if (millis() - lastRun >= interval) {

    lastRun = millis();

    unsigned long cycleStart = millis();

    // ================= MODBUS =================
    unsigned long modbusStart = millis();
    

    bool ok = readInverter();

    unsigned long modbusTime =
      millis() - modbusStart;

    Serial.print("MODBUS TIME: ");
    Serial.println(modbusTime);

    // ================= SUPABASE =================
    if (ok) {

      unsigned long supabaseStart = millis();

      sendMQTT();

      unsigned long supabaseTime =
        millis() - supabaseStart;

      Serial.print("MQTT TIME: ");
      Serial.println(supabaseTime);
    }
    static unsigned long lastBMS = 0;
    
    if (millis() - lastBMS > 2000)
    {
        lastBMS = millis();
        sendBMSMQTT();
    } 
    
  }
}
