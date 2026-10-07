#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

//====================【用户配置区 - 可直接修改】====================
const char* WIFI_SSID = "CMCC-eujw";
const char* WIFI_PASS = "gmw18479639832";
const char* MQTT_HOST = "i11884f9.ala.cn-hangzhou.emqxsl.cn";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "esp-rgb01";
const char* MQTT_PASS = "rgb666888";

#define CMD_TOPIC "light/rgb/cmd"
#define DATA_PIN 15
#define LED_NUM 30
#define KEY_PIN 12

// 系统参数配置
#define WIFI_CONNECT_TIMEOUT 20000UL
#define WIFI_RECONNECT_INTERVAL 5000UL
#define MQTT_RECONNECT_INTERVAL 3000UL
#define KEY_DEBOUNCE_TIME 20UL
#define KEY_LOGO_TIMEOUT 5000UL
#define IP_SHOW_DURATION 1000UL
#define OTA_ANIM_INTERVAL 400UL
#define LED_MAX_MA 1800
#define LED_RESET_DELAY_US 300

// 呼吸/渐变参数
#define BREATH_PERIOD_MS 3000UL
#define BREATH_MIN_BRIGHT 20
#define RAINBOW_STEP_MS 30UL

// OLED配置
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
//=================================================================

// 全局硬件实例
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClientSecure espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

// 灯光变量
CRGB leds[LED_NUM];
uint8_t targetR = 0, targetG = 0, targetB = 0;
bool refreshFlag = false;
String currentColor = "OFF";
uint8_t lastStaticR = 0, lastStaticG = 0, lastStaticB = 0;
bool hasValidColor = false;
bool screenUpdateFlag = false;

// ==================== 工作模式 ====================
enum LightMode {
  MODE_STATIC = 0,
  MODE_BREATH,
  MODE_RAINBOW
};

LightMode lightMode = MODE_STATIC;
uint8_t breathBaseR = 255, breathBaseG = 0, breathBaseB = 0;
unsigned long breathStartTimer = 0;
unsigned long rainbowTimer = 0;
uint8_t rainbowHue = 0;

// 状态标志
bool otaInProgress = false;   // 同时用于 ArduinoOTA 和 Web OTA
bool showIPFlag = true;

// 定时器变量
unsigned long ipShowTimer = 0;
unsigned long wifiReconTimer = 0;
unsigned long mqttReconTimer = 0;
unsigned long lastOtaAnim = 0;

// 预缓存OLED文字尺寸
uint16_t logoW, logoH;
int16_t logoX, logoY;

// 动态客户端ID
String getClientID() {
  return "esp01_rgb_" + String(ESP.getChipId());
}

// ==================== LED 安全刷新（重载） ====================
void showLedsSafe(CRGB color) {
  fill_solid(leds, LED_NUM, CRGB::Black);
  FastLED.show();
  delayMicroseconds(LED_RESET_DELAY_US);

  fill_solid(leds, LED_NUM, color);
  FastLED.show();
}

void showLedsSafe(CRGB color, bool animate) {
  if (animate) {
    fill_solid(leds, LED_NUM, color);
    FastLED.show();
  } else {
    showLedsSafe(color);
  }
}
// ============================================================

// 颜色名称匹配
String getColorName(uint8_t g, uint8_t r, uint8_t b) {
  if (r == 0 && g == 0 && b == 0) return "OFF";
  if (r == 255 && g == 0 && b == 0) return "RED";
  if (r == 255 && g == 50 && b == 0) return "ORANGE";
  if (r == 255 && g == 255 && b == 0) return "YELLOW";
  if (r == 0 && g == 255 && b == 0) return "GREEN";
  if (r == 0 && g == 255 && b == 250) return "CYAN";
  if (r == 0 && g == 0 && b == 255) return "BLUE";
  if (r == 255 && g == 0 && b == 255) return "PURPLE";
  if (r == 166 && g == 166 && b == 166) return "WHITE";
  if (r == 255 && g == 35 && b == 30) return "PINK";
  if (r == 40 && g == 180 && b == 220) return "AZURE";
  if (r == 0 && g == 255 && b == 55) return "SPRING";
  if (r == 110 && g == 120 && b == 127) return "COOLWHITE";
  return "UNKNOWN";
}

// ==================== MQTT 回调 ====================
void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0) return;
  if (otaInProgress) return;  // OTA 期间忽略

  char msgBuf[64] = { 0 };
  uint16_t copyLen = min(length, (unsigned int)(sizeof(msgBuf) - 1));
  memcpy(msgBuf, payload, copyLen);

  String msg = String(msgBuf);
  msg.trim();

  // ---- OFF ----
  if (msg.equalsIgnoreCase("OFF")) {
    lightMode = MODE_STATIC;
    targetR = targetG = targetB = 0;
    refreshFlag = true;
    hasValidColor = false;
    return;
  }

  // ---- RAINBOW ----
  if (msg.equalsIgnoreCase("RAINBOW")) {
    lightMode = MODE_RAINBOW;
    rainbowHue = 0;
    rainbowTimer = millis();
    currentColor = "RAINBOW";
    screenUpdateFlag = true;
    return;
  }

  // ---- BREATH,R,G,B ----
  if (msg.startsWith("BREATH,")) {
    if (!hasValidColor || (lastStaticR == 0 && lastStaticG == 0 && lastStaticB == 0)) {
      Serial.println("[MQTT] BREATH ignored: no color set (OFF state)");
      return;
    }
    int r = 0, g = 0, b = 0;
    if (sscanf(msg.c_str(), "BREATH,%d,%d,%d", &r, &g, &b) == 3) {
      if (r == 0 && g == 0 && b == 0) {
        Serial.println("[MQTT] BREATH ignored: base color is black");
        return;
      }
      lightMode = MODE_BREATH;
      breathBaseR = constrain(r, 0, 255);
      breathBaseG = constrain(g, 0, 255);
      breathBaseB = constrain(b, 0, 255);
      breathStartTimer = millis();
      currentColor = "BREATH";
      screenUpdateFlag = true;
      Serial.printf("[MQTT] -> BREATH base=%d,%d,%d\n", breathBaseR, breathBaseG, breathBaseB);
    }
    return;
  }

  // ---- R,G,B 静态 ----
  int r = 0, g = 0, b = 0;
  if (sscanf(msg.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
    lightMode = MODE_STATIC;
    targetR = constrain(r, 0, 255);
    targetG = constrain(g, 0, 255);
    targetB = constrain(b, 0, 255);
    refreshFlag = true;

    if (targetR == 0 && targetG == 0 && targetB == 0) {
      hasValidColor = false;
    } else {
      lastStaticR = targetR;
      lastStaticG = targetG;
      lastStaticB = targetB;
      hasValidColor = true;
    }
  }
}
// ============================================================

// ==================== WiFi ====================
void connectWifi() {
  Serial.print("WiFi connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connect timeout, will retry in loop()");
  }
}

void wifiAutoReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - wifiReconTimer < WIFI_RECONNECT_INTERVAL) return;
  wifiReconTimer = millis();

  wl_status_t st = WiFi.status();
  if (st == WL_DISCONNECTED || st == WL_IDLE_STATUS || st == WL_CONNECT_FAILED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

void mqttAutoReconnect() {
  if (client.connected()) return;
  if (millis() - mqttReconTimer < MQTT_RECONNECT_INTERVAL) return;
  mqttReconTimer = millis();
  Serial.println("MQTT reconnecting...");
  espClient.stop();

  if (client.connect(getClientID().c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("MQTT connected success!");
    client.subscribe(CMD_TOPIC);
  } else {
    Serial.print("MQTT connect fail, state: ");
    Serial.println(client.state());
  }
}

// ==================== 屏幕 ====================
void updateScreenColor() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("COLOR:");
  display.setCursor(0, 28);
  display.print(currentColor);
  display.display();
}

void updateScreenLogo() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - logoW) / 2, (SCREEN_HEIGHT - logoH) / 2);
  display.print("LightMQTT");
  display.display();
}

void drawOTAScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds("OTA", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.print("OTA");

  display.getTextBounds("UPDATING", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 24);
  display.print("UPDATING");

  display.getTextBounds("......", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 48);

  static uint8_t dots = 0;
  dots = (dots % 6) + 1;
  for (uint8_t i = 0; i < dots; i++) display.print(".");

  display.display();
}

// ==================== Web 页面 ====================
const char INFO_HTML[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP8266 智能彩灯</title>
<style>
body{font-family:Arial;background:#222;color:#eee;text-align:center;padding:30px;}
h2{color:#4af;}
.info{margin-top:20px;color:#888;font-size:13px;}
a.btn{display:inline-block;margin-top:20px;padding:10px 20px;background:#4af;color:#fff;
      text-decoration:none;border-radius:6px;}
a.btn:hover{background:#29d;}
</style></head><body>
<h2>ESP8266 WS2812 MQTT 智能彩灯</h2>
<p>支持：静态颜色 / 呼吸灯 / 彩虹渐变</p>
<a class='btn' href='/update'>在线升级固件 (Web OTA)</a>
<div class='info'>版本：V5.1（含呼吸+渐变+Web OTA）</div>
</body></html>
)=====";

const char UPDATE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP8266 固件升级</title>
<style>
body{font-family:Arial;background:#222;color:#eee;text-align:center;padding:30px;}
h2{color:#4af;}
form{margin-top:20px;}
input[type=file]{padding:8px;background:#333;color:#eee;border:1px solid #555;border-radius:4px;}
input[type=submit]{padding:10px 20px;background:#4af;color:#fff;border:none;border-radius:6px;cursor:pointer;margin-left:8px;}
input[type=submit]:hover{background:#29d;}
.bar{width:80%;height:22px;background:#333;border-radius:11px;margin:20px auto;overflow:hidden;}
.bar>div{height:100%;width:0;background:#4af;transition:width .2s;}
#msg{margin-top:12px;color:#8f8;}
.info{margin-top:30px;color:#888;font-size:12px;}
a{color:#4af;}
</style></head><body>
<h2>ESP8266 固件在线升级</h2>
<form id='f' method='POST' action='/update' enctype='multipart/form-data'>
  <input type='file' name='firmware' accept='.bin' required>
  <input type='submit' value='上传并升级'>
</form>
<div class='bar'><div id='p'></div></div>
<div id='msg'></div>
<div class='info'>选择编译好的 <b>.bin</b> 固件文件后点击上传。<br>
上传完成后设备会自动重启，请勿断电。<br>
<a href='/'>返回首页</a></div>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var fd=new FormData(this);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(ev){
    if(ev.lengthComputable){
      var p=Math.round(ev.loaded/ev.total*100);
      document.getElementById('p').style.width=p+'%';
      document.getElementById('msg').textContent='上传中 '+p+'%';
    }
  };
  xhr.onload=function(){
    if(xhr.status===200){
      document.getElementById('msg').textContent='升级成功，设备正在重启…';
    }else{
      document.getElementById('msg').textContent='升级失败: '+xhr.status+' '+xhr.responseText;
    }
  };
  xhr.onerror=function(){document.getElementById('msg').textContent='网络错误';};
  xhr.send(fd);
});
</script>
</body></html>
)=====";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INFO_HTML);
}

void handleUpdatePage() {
  server.send_P(200, "text/html; charset=utf-8", UPDATE_HTML);
}

// 上传结束回调
void handleUpdateDone() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(500, "text/plain; charset=utf-8", "UPDATE FAILED");
    Serial.println("[WebOTA] Update failed");
    otaInProgress = false;
    updateScreenColor();
  } else {
    server.send(200, "text/plain; charset=utf-8", "UPDATE OK, REBOOTING...");
    Serial.println("[WebOTA] Update OK, rebooting...");
    delay(500);
    ESP.restart();
  }
}

// 上传中回调（写入 flash）
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    // 进入 OTA 模式：关灯 + 显示 OTA 界面 + 停止 MQTT
    otaInProgress = true;
    showLedsSafe(CRGB::Black);
    drawOTAScreen();

    Serial.printf("[WebOTA] Start: %s\n", upload.filename.c_str());

    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[WebOTA] Success: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    Serial.println("[WebOTA] Aborted");
    otaInProgress = false;
    updateScreenColor();
  }

  yield();
}
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed!");
    pinMode(LED_BUILTIN, OUTPUT);
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(200);
      yield();
    }
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setTextSize(2);
  display.getTextBounds("LightMQTT", 0, 0, &logoX, &logoY, &logoW, &logoH);

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, LED_NUM);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);

  showLedsSafe(CRGB::Black);

  connectWifi();

  espClient.setInsecure();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);

  // ---------- Web 服务器（含 Web OTA） ----------
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST,
            handleUpdateDone,   // 上传完成后的响应
            handleUpdateUpload  // 上传中的数据处理
  );
  server.begin();
  Serial.println("HTTP Server Started (Web OTA: /update)");

  // ---------- Arduino OTA ----------
  ArduinoOTA.setHostname("esp-rgb-01");
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    showLedsSafe(CRGB::Black);
    drawOTAScreen();
  });
  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 20);
    display.print("OTA Done!");
    display.display();
    delay(800);
    updateScreenColor();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (otaInProgress) drawOTAScreen();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    updateScreenColor();
    Serial.printf("OTA Error[%u]\n", error);
  });
  ArduinoOTA.setPassword("666666");
  ArduinoOTA.begin();
  Serial.println("Arduino OTA Started");

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 10);
  display.println("WIFI IP:");
  display.println(WiFi.localIP().toString());
  display.display();
  ipShowTimer = millis();
}

// ==================== 灯光效果处理 ====================
void handleBreath() {
  unsigned long elapsed = millis() - breathStartTimer;
  float phase = (float)(elapsed % BREATH_PERIOD_MS) / BREATH_PERIOD_MS;
  float brightness = (sin(phase * 2 * PI - PI / 2) + 1.0f) / 2.0f;
  uint8_t scale = BREATH_MIN_BRIGHT + brightness * (255 - BREATH_MIN_BRIGHT);

  CRGB c(
    (uint16_t)breathBaseR * scale / 255,
    (uint16_t)breathBaseG * scale / 255,
    (uint16_t)breathBaseB * scale / 255);
  showLedsSafe(c, true);
}

void handleRainbow() {
  if (millis() - rainbowTimer < RAINBOW_STEP_MS) return;
  rainbowTimer = millis();

  rainbowHue += 2;
  CRGB c = CHSV(rainbowHue, 255, 255);
  showLedsSafe(c, true);
}
// ============================================================

void loop() {
  // ★ Web 服务必须始终处理（含 OTA 上传）
  server.handleClient();
  ArduinoOTA.handle();

  if (otaInProgress) {
    if (millis() - lastOtaAnim > OTA_ANIM_INTERVAL) {
      lastOtaAnim = millis();
      drawOTAScreen();
    }
    yield();
    return;
  }

  wifiAutoReconnect();
  mqttAutoReconnect();
  client.loop();

  if (showIPFlag) {
    if (millis() - ipShowTimer > IP_SHOW_DURATION) {
      showIPFlag = false;
      updateScreenColor();
    }
    yield();
    return;
  }

  if (screenUpdateFlag && !showIPFlag && !otaInProgress) {
    screenUpdateFlag = false;
    updateScreenColor();
  }

  // ==================== 按键消抖 ====================
  static bool lastRawKey = false;
  static bool stableKey = false;
  static unsigned long debounceTimer = 0;
  static unsigned long keyDownTimer = 0;

  bool rawKey = (digitalRead(KEY_PIN) == LOW);

  if (rawKey != lastRawKey) {
    debounceTimer = millis();
    lastRawKey = rawKey;
  }

  if (millis() - debounceTimer > KEY_DEBOUNCE_TIME && stableKey != lastRawKey) {
    stableKey = lastRawKey;
    if (stableKey) {
      keyDownTimer = millis();
      updateScreenLogo();
    } else {
      updateScreenColor();
    }
  }

  if (stableKey && millis() - keyDownTimer > KEY_LOGO_TIMEOUT) {
    stableKey = false;
    updateScreenColor();
  }
  // =================================================

  // ==================== 灯光模式处理 ====================
  switch (lightMode) {
    case MODE_STATIC:
      if (refreshFlag) {
        showLedsSafe(CRGB(targetR, targetG, targetB));
        refreshFlag = false;
        currentColor = getColorName(targetG, targetR, targetB);
        if (!stableKey) updateScreenColor();
      }
      break;

    case MODE_BREATH:
      refreshFlag = false;
      handleBreath();
      break;

    case MODE_RAINBOW:
      refreshFlag = false;
      handleRainbow();
      break;
  }
  // ==================================================

  yield();
}
// V5.1：新增 Web OTA 在线升级（/update 页面），使用 ESP8266WebServer 内置 Update，无需额外库