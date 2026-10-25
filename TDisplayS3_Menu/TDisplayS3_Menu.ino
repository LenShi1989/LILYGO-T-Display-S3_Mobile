/*
 * LILYGO T-Display-S3 控制面板
 * 軟體版本：1.3.0
 * Arduino IDE：2.3.10
 * Arduino CLI：1.5.1
 * ESP32 Arduino Core：3.3.12
 * 開發板：LilyGo T-Display-S3 (esp32:esp32:lilygo_t_display_s3)
 *
 * 需要安裝的函式庫：
 * - LovyanGFX 1.2.34
 * - ArduinoJson 7.4.2
 *
 * 注意：本專案不使用 TFT_eSPI，無需安裝或設定。
 *
 * 1.2.0 新增：
 * - 主選單「電子相片」，可全螢幕播放 SPIFFS 內的 JPG/PNG 相片。
 * - Web 控制台改為側邊欄，提供相片上傳/刪除與 OTA 更新。
 * - 後端新增相片上傳、清單、刪除與檔案讀取 API。
 *
 * 1.3.0 新增：
 * - AP SSID 改為 esp32-TTGO-S3。
 * - Web 控制台可上傳 spiffs.bin，直接更新 SPIFFS 映像。
 * - 提供 tools/build_spiffs.ps1 產生與燒錄 SPIFFS 映像。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_partition.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.freq_write = 20000000;
      cfg.freq_read = 8000000;
      cfg.pin_rd = 9;
      cfg.pin_wr = 8;
      cfg.pin_rs = 7;
      cfg.pin_d0 = 39;
      cfg.pin_d1 = 40;
      cfg.pin_d2 = 41;
      cfg.pin_d3 = 42;
      cfg.pin_d4 = 45;
      cfg.pin_d5 = 46;
      cfg.pin_d6 = 47;
      cfg.pin_d7 = 48;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 6;
      cfg.pin_rst = 5;
      cfg.pin_busy = -1;
      cfg.panel_width = 170;
      cfg.panel_height = 320;
      cfg.offset_x = 35;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 38;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 170;
static constexpr int HEADER_H = 24;
static constexpr int FOOTER_H = 17;
static constexpr int VISIBLE_ROWS = 5;
static constexpr int ROW_H = 23;
static constexpr int MAX_SCAN = 24;
static constexpr int MAX_POINTS = 120;

static constexpr const char* FW_VERSION = "1.3.0";
static constexpr const char* AP_SSID = "esp32-TTGO-S3";
static constexpr uint32_t OTA_REBOOT_DELAY_MS = 1200;

static constexpr const char* PHOTO_DIR = "/photos";
static constexpr int MAX_PHOTOS = 64;
static constexpr int MAX_UPLOAD_FILES = 8;
static constexpr uint32_t PHOTO_SLIDE_MS = 6000;
static constexpr size_t MAX_PHOTO_BYTES = 2 * 1024 * 1024;
static constexpr size_t PHOTO_MIN_FREE = 16384;

static constexpr uint8_t PIN_BTN_NEXT = 0;
static constexpr uint8_t PIN_BTN_OK = 14;
static constexpr uint8_t PIN_BAT = 4;
static constexpr uint8_t PIN_LCD_POWER = 15;

static constexpr float BAT_RATIO = 2.0f;
static constexpr float BAT_MIN_V = 3.30f;
static constexpr float BAT_MAX_V = 4.20f;

static constexpr uint32_t BAT_INTERVAL_MS = 15000;
static constexpr uint32_t ICON_INTERVAL_MS = 1000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_TRY_TIMEOUT_MS = 20000;
static constexpr uint32_t LONG_PRESS_MS = 700;

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static constexpr uint16_t C_BG = rgb565(8, 12, 24);
static constexpr uint16_t C_CARD = rgb565(20, 28, 48);
static constexpr uint16_t C_CARD2 = rgb565(30, 41, 64);
static constexpr uint16_t C_LINE = rgb565(45, 55, 75);
static constexpr uint16_t C_TEXT = rgb565(235, 240, 248);
static constexpr uint16_t C_MUTED = rgb565(148, 163, 184);
static constexpr uint16_t C_ACCENT = rgb565(45, 212, 191);
static constexpr uint16_t C_ACCENT2 = rgb565(56, 189, 248);
static constexpr uint16_t C_RED = rgb565(248, 113, 113);
static constexpr uint16_t C_GREEN = rgb565(74, 222, 128);
static constexpr uint16_t C_AMBER = rgb565(251, 191, 36);
static constexpr uint16_t C_DARK = rgb565(30, 41, 59);

enum Screen : uint8_t {
  SCR_SPLASH,
  SCR_MAIN,
  SCR_SYS_MENU,
  SCR_WIFI_MENU,
  SCR_STOCK_MENU,
  SCR_WIFI_INFO,
  SCR_SYS_INFO,
  SCR_DISPLAY,
  SCR_SCAN,
  SCR_KEYBOARD,
  SCR_WIFI_CLEAR,
  SCR_WEATHER,
  SCR_CLOCK,
  SCR_STOCK_QUOTE,
  SCR_STOCK_CHART,
  SCR_PHOTO_MENU,
  SCR_PHOTO_VIEW,
  SCR_PHOTO_INFO,
  SCR_OTA
};

enum Event : uint8_t {
  EV_NONE = 0,
  EV_SHORT = 1,
  EV_LONG = 2
};

enum KbMode : uint8_t {
  KB_SSID,
  KB_PASS,
  KB_SYMBOL
};

class Button {
public:
  explicit Button(uint8_t pin) : _pin(pin) {}

  void begin() {
    pinMode(_pin, INPUT_PULLUP);
    reset();
  }

  void reset() {
    _raw = (digitalRead(_pin) == LOW);
    _pressed = _raw;
    _longDone = _raw;
    _changeMs = millis();
    _pressMs = millis();
  }

  uint8_t poll() {
    bool cur = (digitalRead(_pin) == LOW);
    uint32_t now = millis();
    if (cur != _raw) {
      _raw = cur;
      _changeMs = now;
      return EV_NONE;
    }
    if (now - _changeMs < 20) return EV_NONE;
    if (_raw && !_pressed) {
      _pressed = true;
      _pressMs = now;
      _longDone = false;
    } else if (!_raw && _pressed) {
      _pressed = false;
      if (!_longDone) return EV_SHORT;
    }
    if (_pressed && !_longDone && now - _pressMs >= LONG_PRESS_MS) {
      _longDone = true;
      return EV_LONG;
    }
    return EV_NONE;
  }

private:
  uint8_t _pin;
  bool _raw = false;
  bool _pressed = false;
  bool _longDone = false;
  uint32_t _changeMs = 0;
  uint32_t _pressMs = 0;
};

Button btnNext(PIN_BTN_NEXT);
Button btnOk(PIN_BTN_OK);

struct MenuData {
  const char* const* items = nullptr;
  uint8_t count = 0;
  uint8_t sel = 0;
  uint8_t top = 0;
  String title;
};

MenuData menu;

static const char* const MAIN_ITEMS[] = {
  "系統狀態",
  "WiFi設定",
  "天氣",
  "時間",
  "股票",
  "電子相片",
  "OTA更新"
};

static const char* const SYS_ITEMS[] = {
  "WiFi資訊",
  "系統資訊",
  "顯示器"
};

static const char* const WIFI_ITEMS[] = {
  "掃描網路",
  "手動設定",
  "清除設定"
};

static const char* const STOCK_ITEMS[] = {
  "股票查詢",
  "折線圖",
  "更改代號"
};

static const char* const PHOTO_ITEMS[] = {
  "播放相片",
  "相片資訊"
};

static const char* const KB_KEYS[5][10] = {
  {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
  {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
  {"a", "s", "d", "f", "g", "h", "j", "k", "l", "-"},
  {"z", "x", "c", "v", "b", "n", "m", ".", "_", "@"},
  {"SFT", "SPC", ".com", "DEL", "OK", "/", ":", "^", "+", "="}
};

Preferences prefs;
String cfgSsid;
String cfgPass;
String cfgSymbol = "2330.TW";
String cfgLat = "25.0330";
String cfgLon = "121.5654";
String cfgCity = "台北";

Screen screen = SCR_SPLASH;
Screen returnScreen = SCR_MAIN;
Screen kbReturn = SCR_WIFI_MENU;

float batVolt = 0.0f;
int batPct = 0;
bool batValid = false;
uint32_t lastBatMs = 0;
uint32_t lastIconMs = 0;
bool iconBlink = false;

bool wifiTrying = false;
uint32_t wifiTryStart = 0;
bool ntpStarted = false;

bool spiffsReady = false;
bool otaRunning = false;
bool otaSuccess = false;
uint8_t otaProgress = 0;
size_t otaTotalSize = 0;
String otaStatus = "待機";

// SPIFFS 映像更新狀態（與韌體 OTA 各自獨立）
bool fsRunning = false;
bool fsSuccess = false;
bool fsStarted = false;
uint8_t fsProgress = 0;
size_t fsTotalSize = 0;
String fsStatus = "";

size_t spiffsImageSize();

String photoNames[MAX_PHOTOS];
size_t photoSizes[MAX_PHOTOS];
int photoCount = 0;
int photoIdx = 0;
uint32_t photoLastMs = 0;

File photoUploadFile;
String photoUploadCurrent;
bool photoUploadOpen = false;
bool photoUploadFailed = false;
bool photoUploadNewRequest = true;
size_t photoUploadBytes = 0;
size_t photoUploadFree = 0;
String photoUploadError;
String photoUploadSaved[MAX_UPLOAD_FILES];
int photoUploadSavedCount = 0;
uint32_t photoNameSeq = 0;

WebServer webServer(80);

KbMode kbMode = KB_SSID;
String kbTitle;
String kbValue;
String pendingSsid;
bool kbMask = false;
bool kbShift = false;
uint8_t kbCursor = 0;

String scanSsid[MAX_SCAN];
int scanRssi[MAX_SCAN];
uint8_t scanEnc[MAX_SCAN];
int scanN = 0;
int scanSel = 0;
int scanTop = 0;

bool clearChoice = false;

uint8_t brightIdx = 2;
static const uint8_t BRIGHT_LEVELS[4] = {64, 128, 192, 255};

struct WeatherData {
  bool valid = false;
  float temp = NAN;
  float feels = NAN;
  float tmax = NAN;
  float tmin = NAN;
  float wind = NAN;
  float precip = NAN;
  int humidity = 0;
  int code = -1;
  time_t updated = 0;
};

WeatherData weather;

struct StockData {
  bool valid = false;
  String symbol;
  String currency;
  float price = 0.0f;
  float prevClose = 0.0f;
  float high = 0.0f;
  float low = 0.0f;
  long volume = 0;
  float points[MAX_POINTS];
  time_t ts[MAX_POINTS];
  int n = 0;
  time_t updated = 0;
};

StockData stock;

int lastClockSec = -1;
int lastClockMin = -1;
bool clockMsgDrawn = false;
bool clockReady = false;

String fitText(const String& s, int maxW, const lgfx::IFont* font) {
  lcd.setFont(font);
  if (lcd.textWidth(s) <= maxW) return s;
  String t = s;
  while (t.length()) {
    int cut = t.length();
    do {
      cut--;
    } while (cut > 0 && ((uint8_t)t[cut] & 0xC0) == 0x80);
    t.remove(cut);
    if (lcd.textWidth(t + "..") <= maxW) return t + "..";
  }
  return "..";
}

String weekdayTW(int w) {
  static const char* const names[7] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};
  if (w < 0 || w > 6) return "";
  return String(names[w]);
}

String fmtSigned(float v, int digits) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%+.*f", digits, v);
  return String(buf);
}

String fmtVolume(long v) {
  if (v >= 1000000L) return String(v / 1000000.0f, 2) + "M";
  if (v >= 1000L) return String(v / 1000.0f, 1) + "K";
  return String(v);
}

String fmtUptime() {
  uint32_t s = millis() / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
           (unsigned long)(s / 86400),
           (unsigned long)((s % 86400) / 3600),
           (unsigned long)((s % 3600) / 60),
           (unsigned long)(s % 60));
  return String(buf);
}

String timeLabel(time_t ts) {
  if (ts < 100000) return "--:--";
  struct tm t;
  localtime_r(&ts, &t);
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

String weatherDesc(int code) {
  if (code == 0) return "晴朗";
  if (code == 1) return "晴時多雲";
  if (code == 2) return "多雲";
  if (code == 3) return "陰天";
  if (code == 45 || code == 48) return "有霧";
  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 67) return "下雨";
  if (code >= 71 && code <= 77) return "下雪";
  if (code >= 80 && code <= 82) return "陣雨";
  if (code >= 85 && code <= 86) return "陣雪";
  if (code >= 95) return "雷雨";
  return "未知";
}

String rssiQuality(int rssi) {
  if (rssi >= -60) return "極佳";
  if (rssi >= -70) return "良好";
  if (rssi >= -80) return "普通";
  return "微弱";
}

void loadConfig() {
  prefs.begin("tds3", true);
  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  cfgSymbol = prefs.getString("sym", "2330.TW");
  cfgLat = prefs.getString("lat", "24.9160");
  cfgLon = prefs.getString("lon", "121.2536");
  cfgCity = prefs.getString("city", "桃園平鎮區");
  prefs.end();
}

void saveWiFi(const String& ssid, const String& pass) {
  prefs.begin("tds3", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  cfgSsid = ssid;
  cfgPass = pass;
}

void saveSymbol(const String& symbol) {
  prefs.begin("tds3", false);
  prefs.putString("sym", symbol);
  prefs.end();
  cfgSymbol = symbol;
}

void clearWifiSettings() {
  WiFi.disconnect(false, true);
  prefs.begin("tds3", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  cfgSsid = "";
  cfgPass = "";
  wifiTrying = false;
  WiFi.mode(WIFI_AP_STA);
}

static const char OTA_FS_MISSING[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SPIFFS 未就緒</title>
<style>body{margin:0;padding:32px;background:#070b14;color:#eef4ff;font-family:system-ui,"Microsoft JhengHei",sans-serif;line-height:1.7}
code{background:#16223b;padding:2px 6px;border-radius:4px;color:#2dd4bf}h1{font-size:20px}</style></head>
<body><h1>SPIFFS 未就緒</h1>
<p>找不到 <code>/index.html</code>，請先將 <code>TDisplayS3_Menu/data</code> 打包並寫入 SPIFFS 分割區。</p>
<p>寫入後重新整理本頁即可使用 OTA 更新功能。</p>
</body></html>)HTML";

bool initFileSystem() {
  spiffsReady = SPIFFS.begin(true);
  if (spiffsReady) {
    Serial.printf("[SPIFFS] mounted, %u / %u bytes used\n",
                  (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  } else {
    Serial.println("[SPIFFS] mount failed");
  }
  return spiffsReady;
}

String fmtBytes(size_t bytes) {
  char buf[24];
  if (bytes >= 1048576UL) snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
  else if (bytes >= 1024UL) snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
  else snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
  return String(buf);
}

bool isPhotoFile(const String& name) {
  String low = name;
  low.toLowerCase();
  return low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".png");
}

String photoBaseName(const String& raw) {
  String base = raw;
  int cut = base.lastIndexOf('/');
  int cut2 = base.lastIndexOf('\\');
  if (cut2 > cut) cut = cut2;
  if (cut >= 0) base = base.substring(cut + 1);

  String clean;
  clean.reserve(base.length());
  for (size_t i = 0; i < base.length() && clean.length() < 40; i++) {
    char c = base[i];
    if (isalnum((uint8_t)c) || c == '.' || c == '-' || c == '_') {
      clean += (char)tolower((uint8_t)c);
    } else {
      clean += '_';
    }
  }
  while (clean.startsWith(".")) clean.remove(0, 1);
  if (clean.length() == 0) return "";
  if (!isPhotoFile(clean)) return "";
  return clean;
}

void refreshPhotoList() {
  photoCount = 0;
  if (!spiffsReady) return;

  File dir = SPIFFS.open(PHOTO_DIR, "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  File entry = dir.openNextFile();
  while (entry && photoCount < MAX_PHOTOS) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      int cut = name.lastIndexOf('/');
      if (cut >= 0) name = name.substring(cut + 1);
      if (isPhotoFile(name)) {
        photoNames[photoCount] = name;
        photoSizes[photoCount] = entry.size();
        photoCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  for (int i = 1; i < photoCount; i++) {
    String key = photoNames[i];
    size_t size = photoSizes[i];
    int j = i - 1;
    while (j >= 0 && photoNames[j] > key) {
      photoNames[j + 1] = photoNames[j];
      photoSizes[j + 1] = photoSizes[j];
      j--;
    }
    photoNames[j + 1] = key;
    photoSizes[j + 1] = size;
  }
  if (photoIdx >= photoCount) photoIdx = 0;
}

String makePhotoFileName(const String& original) {
  String base = photoBaseName(original);
  if (base.length() == 0) base = "photo.jpg";

  String stem = base;
  String ext = ".jpg";
  int dot = base.lastIndexOf('.');
  if (dot > 0) {
    stem = base.substring(0, dot);
    ext = base.substring(dot);
  }
  // SPIFFS 檔名（含 /photos/ 路徑）上限約 31 字元，主檔名需保持精簡。
  if (stem.length() > 10) stem.remove(10);
  if (stem.length() == 0) stem = "photo";

  String name;
  do {
    name = "p" + String(photoNameSeq++) + "_" + stem + ext;
  } while (photoNameSeq < 10000 && SPIFFS.exists(String(PHOTO_DIR) + "/" + name));
  return name;
}

// 底部保留 17px 給資訊列，相片以 contain 方式置中，不會被裁掉。
static constexpr int PHOTO_VIEW_H = SCREEN_H - 17;

bool drawPhotoFile(int idx) {
  if (!spiffsReady || idx < 0 || idx >= photoCount) return false;
  String name = photoNames[idx];
  String path = String(PHOTO_DIR) + "/" + name;
  String low = name;
  low.toLowerCase();
  if (low.endsWith(".png")) {
    return lcd.drawPngFile(SPIFFS, path, 0, 0, SCREEN_W, PHOTO_VIEW_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
  }
  return lcd.drawJpgFile(SPIFFS, path, 0, 0, SCREEN_W, PHOTO_VIEW_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
}

void dropPhotoUpload(bool removePartial) {
  if (photoUploadOpen) {
    photoUploadFile.close();
    photoUploadOpen = false;
  }
  if (removePartial && photoUploadCurrent.length()) {
    SPIFFS.remove(String(PHOTO_DIR) + "/" + photoUploadCurrent);
  }
  photoUploadCurrent = "";
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, NULL, 6, 0, 4);
  Serial.printf("[AP] %s open, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

String jsonQuote(const String& value) {
  String out;
  out.reserve(value.length() + 2);
  out += '"';
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char buf[7];
      snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
      out += buf;
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

void handleRoot() {
  webServer.sendHeader("Cache-Control", "no-store");
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    webServer.send(500, "text/html; charset=utf-8", String(OTA_FS_MISSING));
    return;
  }
  webServer.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void handleStatus() {
  bool staOnline = (WiFi.status() == WL_CONNECTED);
  String json;
  json.reserve(512);
  json += '{';
  json += "\"version\":" + jsonQuote(FW_VERSION) + ',';
  json += "\"apSsid\":" + jsonQuote(AP_SSID) + ',';
  json += "\"apIp\":" + jsonQuote(WiFi.softAPIP().toString()) + ',';
  json += "\"apClients\":" + String(WiFi.softAPgetStationNum()) + ',';
  json += "\"staConnected\":" + String(staOnline ? "true" : "false") + ',';
  json += "\"staSsid\":" + jsonQuote(staOnline ? WiFi.SSID() : String("")) + ',';
  json += "\"staIp\":" + jsonQuote(staOnline ? WiFi.localIP().toString() : String("--")) + ',';
  json += "\"heapFree\":" + String(ESP.getFreeHeap()) + ',';
  json += "\"uptime\":" + String(millis() / 1000UL) + ',';
  json += "\"fsReady\":" + String(spiffsReady ? "true" : "false") + ',';
  json += "\"fsTotal\":" + String(spiffsReady ? (unsigned)SPIFFS.totalBytes() : 0) + ',';
  json += "\"fsUsed\":" + String(spiffsReady ? (unsigned)SPIFFS.usedBytes() : 0) + ',';
  json += "\"photoCount\":" + String(photoCount) + ',';
  json += "\"fsImageSize\":" + String((unsigned)spiffsImageSize()) + ',';
  json += "\"fsUpdating\":" + String(fsRunning ? "true" : "false") + ',';
  json += "\"otaRunning\":" + String(otaRunning ? "true" : "false");
  json += '}';
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json; charset=utf-8", json);
}


void handlePhotoList() {
  refreshPhotoList();
  String json;
  json.reserve(160 + photoCount * 72);
  json += '{';
  json += "\"count\":";
  json += String(photoCount);
  json += ',';
  json += "\"fsTotal\":";
  json += String(spiffsReady ? (unsigned)SPIFFS.totalBytes() : 0);
  json += ',';
  json += "\"fsUsed\":";
  json += String(spiffsReady ? (unsigned)SPIFFS.usedBytes() : 0);
  json += ',';
  json += "\"files\":[";
  for (int i = 0; i < photoCount; i++) {
    if (i) json += ',';
    json += "{\"name\":";
    json += jsonQuote(photoNames[i]);
    json += ",\"size\":";
    json += String((unsigned)photoSizes[i]);
    json += '}';
  }
  json += "]}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json; charset=utf-8", json);
}

void handlePhotoUpload() {
  HTTPUpload& up = webServer.upload();

  if (up.status == UPLOAD_FILE_START) {
    // 只在每個請求的第一個檔案重置狀態，避免同一請求的多檔上傳遺失已存清單。
    if (photoUploadNewRequest) {
      photoUploadNewRequest = false;
      photoUploadFailed = false;
      photoUploadError = "";
      photoUploadSavedCount = 0;
    }
    photoUploadBytes = 0;
    photoUploadCurrent = "";
    if (photoUploadFailed) return;
    if (!spiffsReady) {
      photoUploadFailed = true;
      photoUploadError = "SPIFFS 未就緒";
      return;
    }
    if (photoUploadSavedCount >= MAX_UPLOAD_FILES) {
      photoUploadFailed = true;
      photoUploadError = "一次最多上傳 8 張相片";
      return;
    }

    String saved = makePhotoFileName(up.filename);
    if (saved.length() == 0) {
      photoUploadFailed = true;
      photoUploadError = "只接受 JPG 或 PNG 圖檔";
      return;
    }
    if (!SPIFFS.exists(PHOTO_DIR)) SPIFFS.mkdir(PHOTO_DIR);

    size_t total = SPIFFS.totalBytes();
    size_t used = SPIFFS.usedBytes();
    photoUploadFree = (total > used) ? (total - used) : 0;
    if (photoUploadFree < PHOTO_MIN_FREE) {
      photoUploadFailed = true;
      photoUploadError = "SPIFFS 空間不足";
      return;
    }

    photoUploadCurrent = saved;
    String path = String(PHOTO_DIR) + "/" + saved;
    photoUploadFile = SPIFFS.open(path, "w");
    if (!photoUploadFile) {
      photoUploadFailed = true;
      photoUploadError = "無法建立檔案";
      photoUploadCurrent = "";
      return;
    }
    photoUploadOpen = true;
    Serial.printf("[PHOTO] start %s\n", path.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (photoUploadFailed || !photoUploadOpen) return;

    size_t limit = MAX_PHOTO_BYTES;
    if (photoUploadFree > PHOTO_MIN_FREE && photoUploadFree - PHOTO_MIN_FREE < limit) {
      limit = photoUploadFree - PHOTO_MIN_FREE;
    }
    if (photoUploadBytes + up.currentSize > limit) {
      photoUploadFailed = true;
      photoUploadError = "圖檔過大或空間不足";
      dropPhotoUpload(true);
      return;
    }

    size_t written = photoUploadFile.write(up.buf, up.currentSize);
    if (written != up.currentSize) {
      photoUploadFailed = true;
      photoUploadError = "寫入 SPIFFS 失敗";
      dropPhotoUpload(true);
      return;
    }
    photoUploadBytes += written;
  } else if (up.status == UPLOAD_FILE_END) {
    if (photoUploadFailed) return;
    if (photoUploadOpen) {
      photoUploadFile.close();
      photoUploadOpen = false;
    }
    if (photoUploadBytes == 0) {
      if (photoUploadCurrent.length()) SPIFFS.remove(String(PHOTO_DIR) + "/" + photoUploadCurrent);
      photoUploadCurrent = "";
      photoUploadFailed = true;
      photoUploadError = "收到空檔案";
      return;
    }
    if (photoUploadSavedCount < MAX_UPLOAD_FILES) {
      photoUploadSaved[photoUploadSavedCount++] = photoUploadCurrent;
      Serial.printf("[PHOTO] saved %s (%u bytes)\n",
                    photoUploadCurrent.c_str(), (unsigned)photoUploadBytes);
    }
    photoUploadCurrent = "";
    photoUploadBytes = 0;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    photoUploadFailed = true;
    photoUploadError = "上傳中斷";
    photoUploadNewRequest = true;
    dropPhotoUpload(true);
  }
}

void handlePhotoUploadDone() {
  bool ok = (!photoUploadFailed && photoUploadSavedCount > 0);
  refreshPhotoList();

  webServer.sendHeader("Connection", "close");
  webServer.sendHeader("Cache-Control", "no-store");
  if (ok) {
    String json = "{\"ok\":true,\"saved\":" + String(photoUploadSavedCount);
    json += ",\"count\":" + String(photoCount) + ",\"files\":[";
    for (int i = 0; i < photoUploadSavedCount; i++) {
      if (i) json += ',';
      json += jsonQuote(photoUploadSaved[i]);
    }
    json += "]}";
    webServer.send(200, "application/json; charset=utf-8", json);
  } else {
    if (photoUploadError.length() == 0) photoUploadError = "沒有收到圖片資料";
    String json = "{\"ok\":false,\"message\":" + jsonQuote(photoUploadError) + "}";
    webServer.send(500, "application/json; charset=utf-8", json);
  }

  photoUploadSavedCount = 0;
  photoUploadFailed = false;
  photoUploadError = "";
  photoUploadBytes = 0;
  photoUploadFree = 0;
  photoUploadCurrent = "";
  photoUploadNewRequest = true;
}

void handlePhotoDelete() {
  String name = webServer.arg("name");
  int cut = name.lastIndexOf('/');
  if (cut >= 0) name = name.substring(cut + 1);

  bool ok = false;
  if (spiffsReady && name.length() && name.indexOf("..") < 0 && photoBaseName(name) == name) {
    String path = String(PHOTO_DIR) + "/" + name;
    if (SPIFFS.exists(path)) ok = SPIFFS.remove(path);
    if (ok) Serial.printf("[PHOTO] deleted %s\n", path.c_str());
  }
  refreshPhotoList();

  webServer.sendHeader("Cache-Control", "no-store");
  if (ok) {
    String json = "{\"ok\":true,\"count\":" + String(photoCount) + "}";
    webServer.send(200, "application/json; charset=utf-8", json);
  } else {
    webServer.send(404, "application/json; charset=utf-8",
                   "{\"ok\":false,\"message\":\"找不到相片\"}");
  }
}

void handlePhotoFile() {
  String uri = webServer.uri();
  String name = uri.substring(String("/photos/").length());
  if (name.indexOf('?') >= 0) name = name.substring(0, name.indexOf('?'));

  if (name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf("..") >= 0 ||
      photoBaseName(name) != name) {
    webServer.send(404, "text/plain; charset=utf-8", "Not Found");
    return;
  }

  String path = String(PHOTO_DIR) + "/" + name;
  if (!spiffsReady || !SPIFFS.exists(path)) {
    webServer.send(404, "text/plain; charset=utf-8", "Not Found");
    return;
  }
  File file = SPIFFS.open(path, "r");
  if (!file) {
    webServer.send(404, "text/plain; charset=utf-8", "Not Found");
    return;
  }
  String low = name;
  low.toLowerCase();
  const char* type = low.endsWith(".png") ? "image/png" : "image/jpeg";
  webServer.sendHeader("Cache-Control", "public, max-age=600");
  webServer.streamFile(file, type);
  file.close();
}

void handleNotFound() {
  if (webServer.uri().startsWith("/photos/")) {
    handlePhotoFile();
    return;
  }
  webServer.send(404, "text/plain; charset=utf-8", "Not Found");
}

void drawOtaIfVisible() {
  if (screen == SCR_OTA) drawOtaScreen();
}

void handleUpdateUpload() {
  HTTPUpload& up = webServer.upload();

  if (up.status == UPLOAD_FILE_START) {
    otaTotalSize = 0;
    String sizeHeader = webServer.header("X-File-Size");
    if (sizeHeader.length()) otaTotalSize = (size_t)strtoul(sizeHeader.c_str(), nullptr, 10);
    // 切換到韌體更新時，清掉先前的 SPIFFS 狀態顯示。
    fsStatus = "";
    fsProgress = 0;

    size_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    size_t beginSize = (otaTotalSize > 0 && otaTotalSize <= maxSize) ? otaTotalSize : maxSize;

    otaRunning = true;
    otaSuccess = false;
    otaProgress = 0;
    otaStatus = "接收韌體";
    drawOtaIfVisible();
    Serial.printf("[OTA] start %s, %u bytes\n", up.filename.c_str(), (unsigned)beginSize);

    if (!Update.begin(beginSize)) {
      Update.printError(Serial);
      otaRunning = false;
      otaStatus = String("無法開始: ") + Update.errorString();
      drawOtaIfVisible();
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() && !Update.hasError()) {
      size_t written = Update.write(up.buf, up.currentSize);
      if (written != up.currentSize) Update.printError(Serial);
    }
    if (otaTotalSize > 0) {
      int pct = (int)((uint64_t)Update.progress() * 100 / otaTotalSize);
      if (pct > 99) pct = 99;
      if (pct / 2 != otaProgress / 2) {
        otaProgress = (uint8_t)pct;
        otaStatus = "接收中 " + String(pct) + "%";
        drawOtaIfVisible();
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.isRunning() && !Update.hasError() && Update.end(true)) {
      otaSuccess = true;
      otaProgress = 100;
      otaStatus = "寫入完成";
    } else {
      Update.printError(Serial);
      otaStatus = Update.hasError() ? String("寫入失敗: ") + Update.errorString() : "寫入失敗";
    }
    otaRunning = false;
    drawOtaIfVisible();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    otaRunning = false;
    otaSuccess = false;
    otaStatus = "已取消";
    drawOtaIfVisible();
  }
}

void handleUpdateDone() {
  bool ok = otaSuccess && !Update.hasError();
  if (ok) {
    otaProgress = 100;
    otaStatus = "更新完成";
  } else {
    String reason = Update.hasError() ? String(Update.errorString()) : String("未收到韌體資料");
    otaStatus = "更新失敗: " + reason;
  }
  otaRunning = false;
  otaSuccess = false;
  drawOtaIfVisible();

  webServer.sendHeader("Connection", "close");
  webServer.sendHeader("Cache-Control", "no-store");
  if (ok) {
    webServer.send(200, "application/json; charset=utf-8",
                   "{\"ok\":true,\"message\":\"更新完成，裝置重新啟動\"}");
    Serial.println("[OTA] success, restarting");
    delay(OTA_REBOOT_DELAY_MS);
    ESP.restart();
  } else {
    String msg = "{\"ok\":false,\"message\":" + jsonQuote(otaStatus) + "}";
    webServer.send(500, "application/json; charset=utf-8", msg);
  }
}


size_t spiffsImageSize() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  return part ? part->size : 0;
}

void handleSpiffsUpload() {
  HTTPUpload& up = webServer.upload();

  if (up.status == UPLOAD_FILE_START) {
    fsTotalSize = 0;
    String sizeHeader = webServer.header("X-File-Size");
    if (sizeHeader.length()) fsTotalSize = (size_t)strtoul(sizeHeader.c_str(), nullptr, 10);

    fsRunning = true;
    fsSuccess = false;
    fsStarted = false;
    fsProgress = 0;
    fsStatus = "接收映像";

    size_t expected = spiffsImageSize();
    if (expected == 0) {
      fsRunning = false;
      fsStatus = "找不到 SPIFFS 分割區";
      drawOtaIfVisible();
      return;
    }
    if (fsTotalSize == 0) {
      fsRunning = false;
      fsStatus = "缺少檔案大小";
      drawOtaIfVisible();
      return;
    }
    if (fsTotalSize != expected) {
      fsRunning = false;
      fsStatus = "映像大小不符";
      drawOtaIfVisible();
      Serial.printf("[FS] size mismatch: %u != %u\n",
                    (unsigned)fsTotalSize, (unsigned)expected);
      return;
    }

    // 直接寫入分割區前先卸載 SPIFFS，避免檔案系統快取與寫入衝突。
    SPIFFS.end();
    spiffsReady = false;

    if (!Update.begin(fsTotalSize, U_SPIFFS)) {
      Update.printError(Serial);
      fsRunning = false;
      fsStatus = String("無法開始: ") + Update.errorString();
      spiffsReady = SPIFFS.begin(false);
      drawOtaIfVisible();
      return;
    }
    fsStarted = true;
    drawOtaIfVisible();
    Serial.printf("[FS] start %s, %u bytes\n", up.filename.c_str(), (unsigned)fsTotalSize);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!fsStarted || !fsRunning) return;
    if (Update.isRunning() && !Update.hasError()) {
      size_t written = Update.write(up.buf, up.currentSize);
      if (written != up.currentSize) Update.printError(Serial);
    }
    if (fsTotalSize > 0) {
      int pct = (int)((uint64_t)Update.progress() * 100 / fsTotalSize);
      if (pct > 99) pct = 99;
      if (pct / 2 != fsProgress / 2) {
        fsProgress = (uint8_t)pct;
        fsStatus = "寫入映像 " + String(pct) + "%";
        drawOtaIfVisible();
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!fsStarted) {
      // START 階段已被拒絕（例如大小不符），保留該訊息。
      fsRunning = false;
      drawOtaIfVisible();
      return;
    }
    fsStarted = false;
    if (Update.isRunning() && !Update.hasError() && Update.end(true)) {
      fsSuccess = true;
      fsProgress = 100;
      fsStatus = "映像寫入完成";
    } else {
      Update.printError(Serial);
      fsStatus = Update.hasError() ? String("寫入失敗: ") + Update.errorString()
                                   : "寫入未完成";
      // 失敗時不格式化，只嘗試重新掛載，避免清空既有內容。
      spiffsReady = SPIFFS.begin(false);
      refreshPhotoList();
    }
    fsRunning = false;
    drawOtaIfVisible();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    fsStarted = false;
    fsRunning = false;
    fsSuccess = false;
    fsStatus = "已中斷";
    spiffsReady = SPIFFS.begin(false);
    refreshPhotoList();
    drawOtaIfVisible();
  }
}

void handleSpiffsUploadDone() {
  bool ok = fsSuccess && !Update.hasError();
  fsRunning = false;
  fsSuccess = false;
  fsStarted = false;

  webServer.sendHeader("Connection", "close");
  webServer.sendHeader("Cache-Control", "no-store");
  if (ok) {
    fsProgress = 100;
    fsStatus = "映像更新完成";
    drawOtaIfVisible();
    webServer.send(200, "application/json; charset=utf-8",
                   "{\"ok\":true,\"message\":\"SPIFFS 映像已寫入，裝置重新啟動\"}");
    Serial.println("[FS] success, restarting");
    delay(OTA_REBOOT_DELAY_MS);
    ESP.restart();
  } else {
    if (fsStatus.length() == 0) fsStatus = "更新失敗";
    if (!spiffsReady) spiffsReady = SPIFFS.begin(false);
    refreshPhotoList();
    drawOtaIfVisible();
    String msg = "{\"ok\":false,\"message\":" + jsonQuote(fsStatus) + "}";
    webServer.send(500, "application/json; charset=utf-8", msg);
  }
}

void setupWebServer() {
  static const char* OTA_HEADERS[] = {"X-File-Size"};
  webServer.collectHeaders(OTA_HEADERS, 1);
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/index.html", HTTP_GET, handleRoot);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/photos", HTTP_GET, handlePhotoList);
  webServer.on("/api/photos/upload", HTTP_POST, handlePhotoUploadDone, handlePhotoUpload);
  webServer.on("/api/photos/delete", HTTP_POST, handlePhotoDelete);
  webServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  webServer.on("/update/spiffs", HTTP_POST, handleSpiffsUploadDone, handleSpiffsUpload);
  webServer.on("/favicon.ico", HTTP_GET, []() { webServer.send(204); });
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("[HTTP] server started on port 80");
}

void updateBattery() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogReadMilliVolts(PIN_BAT);
    delay(2);
  }
  batVolt = (sum / 16.0f) * BAT_RATIO / 1000.0f;
  batValid = (batVolt > 2.5f);
  if (batValid) {
    int pct = (int)((batVolt - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    batPct = pct;
  } else {
    batPct = 0;
  }
}

void drawStatusIcons() {
  lcd.fillRect(220, 1, 100, 21, C_CARD);

  int wx = 224;
  bool conn = (WiFi.status() == WL_CONNECTED);
  uint16_t wc = conn ? C_TEXT : (wifiTrying ? (iconBlink ? C_AMBER : C_DARK) : C_MUTED);
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    bool on = conn || (wifiTrying && i < 2);
    lcd.fillRect(wx + i * 5, 17 - h, 3, h, on ? wc : C_DARK);
  }

  lcd.setFont(&fonts::Font0);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.setTextColor(batValid ? C_TEXT : C_MUTED, C_CARD);
  char buf[8];
  if (batValid) snprintf(buf, sizeof(buf), "%d%%", batPct);
  else snprintf(buf, sizeof(buf), "--");
  lcd.drawString(buf, 282, 12);

  lcd.drawRect(288, 6, 22, 11, C_MUTED);
  lcd.fillRect(310, 9, 2, 5, C_MUTED);
  if (batValid) {
    int fw = (int)(18 * batPct / 100);
    if (fw < 1) fw = 1;
    uint16_t bc = (batPct > 50) ? C_GREEN : (batPct > 20 ? C_AMBER : C_RED);
    lcd.fillRect(290, 8, fw, 7, bc);
  }
}

void drawHeader(const String& title) {
  lcd.fillRect(0, 0, SCREEN_W, HEADER_H, C_CARD);
  lcd.drawFastHLine(0, HEADER_H - 1, SCREEN_W, C_LINE);
  String t = fitText(title, 208, &fonts::efontTW_16_b);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_TEXT, C_CARD);
  lcd.drawString(t, 8, 12);
  drawStatusIcons();
}

void drawFooter(const String& text) {
  lcd.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_BG);
  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(text, SCREEN_W / 2, SCREEN_H - 8);
}

void drawMenuScreen() {
  lcd.fillScreen(C_BG);
  drawHeader(menu.title);

  for (int i = 0; i < VISIBLE_ROWS; i++) {
    int idx = menu.top + i;
    if (idx >= menu.count) break;
    int y = 30 + i * ROW_H;
    bool sel = (idx == menu.sel);
    uint16_t bg = sel ? C_ACCENT : C_CARD;
    uint16_t fg = sel ? C_BG : C_TEXT;
    lcd.fillRoundRect(6, y, 306, 20, 4, bg);
    if (!sel) lcd.drawRoundRect(6, y, 306, 20, 4, C_LINE);

    const lgfx::IFont* font = sel ? &fonts::efontTW_16_b : &fonts::efontTW_14;
    String label = fitText(menu.items[idx], 265, font);
    lcd.setFont(font);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(fg, bg);
    lcd.drawString(label, 16, y + 10);

    if (sel) {
      lcd.fillTriangle(298, y + 6, 298, y + 14, 304, y + 10, C_BG);
    }
  }

  if (menu.count > VISIBLE_ROWS) {
    int trackTop = 30;
    int trackH = 118;
    lcd.fillRect(316, trackTop, 2, trackH, C_DARK);
    int thumbH = trackH * VISIBLE_ROWS / menu.count;
    if (thumbH < 16) thumbH = 16;
    int maxTop = menu.count - VISIBLE_ROWS;
    int thumbY = trackTop;
    if (maxTop > 0) thumbY += (trackH - thumbH) * menu.top / maxTop;
    lcd.fillRect(316, thumbY, 2, thumbH, C_ACCENT);
  }

  drawFooter("P0:下/長按上  P14:確認/長按返回");
}

void menuMove(int dir) {
  if (menu.count == 0) return;
  int n = menu.count;
  int s = (int)menu.sel + dir;
  if (s < 0) s = n - 1;
  if (s >= n) s = 0;
  menu.sel = (uint8_t)s;
  if (menu.sel < menu.top) menu.top = menu.sel;
  if (menu.sel >= menu.top + VISIBLE_ROWS) menu.top = menu.sel - VISIBLE_ROWS + 1;
  drawMenuScreen();
}

void showMainMenu() {
  screen = SCR_MAIN;
  menu.title = "主選單";
  menu.items = MAIN_ITEMS;
  menu.count = sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]);
  menu.sel = 0;
  menu.top = 0;
  drawMenuScreen();
}

void showSysMenu() {
  screen = SCR_SYS_MENU;
  menu.title = "系統狀態";
  menu.items = SYS_ITEMS;
  menu.count = sizeof(SYS_ITEMS) / sizeof(SYS_ITEMS[0]);
  menu.sel = 0;
  menu.top = 0;
  drawMenuScreen();
}

void showWifiMenu() {
  screen = SCR_WIFI_MENU;
  menu.title = "WiFi設定";
  menu.items = WIFI_ITEMS;
  menu.count = sizeof(WIFI_ITEMS) / sizeof(WIFI_ITEMS[0]);
  menu.sel = 0;
  menu.top = 0;
  drawMenuScreen();
}

void showStockMenu() {
  screen = SCR_STOCK_MENU;
  menu.title = "股票 " + cfgSymbol;
  menu.items = STOCK_ITEMS;
  menu.count = sizeof(STOCK_ITEMS) / sizeof(STOCK_ITEMS[0]);
  menu.sel = 0;
  menu.top = 0;
  drawMenuScreen();
}

void showPhotoMenu() {
  refreshPhotoList();
  screen = SCR_PHOTO_MENU;
  menu.title = "電子相片";
  menu.items = PHOTO_ITEMS;
  menu.count = sizeof(PHOTO_ITEMS) / sizeof(PHOTO_ITEMS[0]);
  menu.sel = 0;
  menu.top = 0;
  drawMenuScreen();
}

void goBack() {
  if (returnScreen == SCR_SYS_MENU) showSysMenu();
  else if (returnScreen == SCR_WIFI_MENU) showWifiMenu();
  else if (returnScreen == SCR_STOCK_MENU) showStockMenu();
  else if (returnScreen == SCR_PHOTO_MENU) showPhotoMenu();
  else showMainMenu();
}

void drawBusy(const String& title, const String& line1, const String& line2) {
  lcd.fillScreen(C_BG);
  drawHeader(title);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(fitText(line1, 300, &fonts::efontTW_16_b), 160, 80);
  if (line2.length()) {
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(fitText(line2, 300, &fonts::efontTW_14), 160, 106);
  }
  drawFooter("請稍候");
}

void showToast(const String& msg, uint16_t color) {
  int x = 24;
  int y = 62;
  int w = 272;
  int h = 46;
  lcd.fillRoundRect(x, y, w, h, 8, C_CARD);
  lcd.drawRoundRect(x, y, w, h, 8, color);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(color, C_CARD);
  lcd.drawString(fitText(msg, w - 16, &fonts::efontTW_16_b), 160, y + h / 2);
  delay(1300);
  resetButtons();
}

void resetButtons() {
  btnNext.reset();
  btnOk.reset();
}

bool connectWiFiBlocking(const String& ssid, const String& pass, uint32_t timeoutMs) {
  WiFi.disconnect(false, false);
  delay(150);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiTrying = true;
  wifiTryStart = millis();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(100);
  }
  bool ok = (WiFi.status() == WL_CONNECTED);
  wifiTrying = !ok;
  return ok;
}

bool ensureWiFi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (cfgSsid.length() == 0) return false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  wifiTrying = true;
  wifiTryStart = millis();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
  }
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) wifiTrying = false;
  return ok;
}

void ensureTimeService() {
  if (ntpStarted) return;
  if (WiFi.status() != WL_CONNECTED) return;
  configTzTime("CST-8", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  ntpStarted = true;
}

String urlEncode(const String& s) {
  String out;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

bool httpGet(const String& url, String& out, uint32_t timeoutMs) {
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);

  int code = 0;
  bool ok = false;
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    if (http.begin(client, url)) {
      http.addHeader("User-Agent", "Mozilla/5.0 (ESP32-S3)");
      http.addHeader("Accept", "application/json");
      code = http.GET();
      if (code == HTTP_CODE_OK) out = http.getString();
      ok = (code == HTTP_CODE_OK);
      http.end();
    }
  } else {
    WiFiClient client;
    if (http.begin(client, url)) {
      http.addHeader("User-Agent", "Mozilla/5.0 (ESP32-S3)");
      http.addHeader("Accept", "application/json");
      code = http.GET();
      if (code == HTTP_CODE_OK) out = http.getString();
      ok = (code == HTTP_CODE_OK);
      http.end();
    }
  }
  Serial.printf("GET %s -> %d (%u bytes)\n", url.c_str(), code, (unsigned)out.length());
  return ok;
}

bool fetchWeather() {
  weather.valid = false;
  if (!ensureWiFi(12000)) return false;

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + cfgLat +
               "&longitude=" + cfgLon +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,precipitation" +
               "&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=Asia%2FTaipei";
  String payload;
  if (!httpGet(url, payload, 12000)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;

  weather.temp = cur["temperature_2m"] | NAN;
  weather.feels = cur["apparent_temperature"] | NAN;
  weather.humidity = cur["relative_humidity_2m"] | 0;
  weather.code = cur["weather_code"] | -1;
  weather.wind = cur["wind_speed_10m"] | NAN;
  weather.precip = cur["precipitation"] | NAN;

  JsonArray tmax = doc["daily"]["temperature_2m_max"];
  JsonArray tmin = doc["daily"]["temperature_2m_min"];
  weather.tmax = tmax[0] | NAN;
  weather.tmin = tmin[0] | NAN;
  weather.updated = time(nullptr);
  weather.valid = !isnan(weather.temp);
  return weather.valid;
}

bool parseStockPayload(const String& payload, const String& sym) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonObject res = doc["chart"]["result"][0];
  if (res.isNull()) return false;
  JsonObject meta = res["meta"];

  stock.n = 0;
  stock.symbol = meta["symbol"].as<String>();
  if (stock.symbol.length() == 0) stock.symbol = sym;
  stock.currency = meta["currency"].as<String>();
  stock.price = meta["regularMarketPrice"] | 0.0f;
  stock.prevClose = meta["chartPreviousClose"] | 0.0f;
  if (stock.prevClose <= 0.0f) stock.prevClose = meta["previousClose"] | 0.0f;
  stock.high = meta["regularMarketDayHigh"] | 0.0f;
  stock.low = meta["regularMarketDayLow"] | 0.0f;
  stock.volume = meta["regularMarketVolume"] | 0L;
  int gmtoffset = meta["gmtoffset"] | 0;
  time_t marketTime = meta["regularMarketTime"] | 0;

  JsonArray closes = res["indicators"]["quote"][0]["close"];
  JsonArray times = res["timestamp"];
  int m = closes.size();
  for (int i = 0; i < m; i++) {
    if (closes[i].isNull()) continue;
    float p = closes[i].as<float>();
    if (p <= 0.0f) continue;
    long tt = 0;
    if (!times.isNull() && i < (int)times.size()) tt = times[i].as<long>();
    if (stock.n < MAX_POINTS) {
      stock.points[stock.n] = p;
      stock.ts[stock.n] = (time_t)tt;
      stock.n++;
    } else {
      memmove(stock.points, stock.points + 1, (MAX_POINTS - 1) * sizeof(float));
      memmove(stock.ts, stock.ts + 1, (MAX_POINTS - 1) * sizeof(time_t));
      stock.points[MAX_POINTS - 1] = p;
      stock.ts[MAX_POINTS - 1] = (time_t)tt;
    }
  }

  if (stock.n > 1 && stock.ts[stock.n - 1] > 100000) {
    time_t lastAdj = stock.ts[stock.n - 1] + gmtoffset;
    struct tm lastTm;
    gmtime_r(&lastAdj, &lastTm);
    int start = 0;
    for (int i = stock.n - 1; i >= 0; i--) {
      if (stock.ts[i] <= 100000) break;
      time_t adj = stock.ts[i] + gmtoffset;
      struct tm tm;
      gmtime_r(&adj, &tm);
      if (tm.tm_year != lastTm.tm_year || tm.tm_yday != lastTm.tm_yday) break;
      start = i;
    }
    if (start > 0) {
      stock.n -= start;
      memmove(stock.points, stock.points + start, stock.n * sizeof(float));
      memmove(stock.ts, stock.ts + start, stock.n * sizeof(time_t));
    }
  }

  if (stock.n > 0) {
    if (stock.price <= 0.0f) stock.price = stock.points[stock.n - 1];
    if (stock.prevClose <= 0.0f) stock.prevClose = stock.points[0];
    if (stock.high <= 0.0f) {
      float mx = stock.points[0];
      for (int i = 1; i < stock.n; i++) if (stock.points[i] > mx) mx = stock.points[i];
      stock.high = mx;
    }
    if (stock.low <= 0.0f) {
      float mn = stock.points[0];
      for (int i = 1; i < stock.n; i++) if (stock.points[i] < mn) mn = stock.points[i];
      stock.low = mn;
    }
    stock.updated = stock.ts[stock.n - 1];
  } else {
    stock.updated = marketTime > 0 ? marketTime : time(nullptr);
  }

  return true;
}

bool fetchStock(bool needChart = true) {
  stock.valid = false;
  stock.n = 0;
  if (!ensureWiFi(12000)) return false;

  String sym = cfgSymbol;
  sym.trim();
  sym.toUpperCase();
  if (sym.length() == 0) sym = "2330.TW";

  bool allDigits = (sym.length() > 0);
  for (size_t i = 0; i < sym.length(); i++) {
    if (!isdigit((uint8_t)sym[i])) allDigits = false;
  }
  if (allDigits) sym += ".TW";

  String base = "https://query1.finance.yahoo.com/v8/finance/chart/" + urlEncode(sym);
  String payload;
  bool got = false;

  if (httpGet(base + "?range=1d&interval=5m", payload, 15000)) {
    got = parseStockPayload(payload, sym);
  }

  if (needChart && stock.n < 2) {
    if (httpGet(base + "?range=5d&interval=5m", payload, 15000)) {
      got = parseStockPayload(payload, sym) || got;
    }
  }

  if (!got) return false;

  stock.valid = (stock.price > 0.0f || stock.n > 1);
  return stock.valid;
}

void drawWifiInfo() {
  lcd.fillScreen(C_BG);
  drawHeader("WiFi資訊");
  drawFooter("長按 P14 返回");

  String status;
  uint16_t sc = C_TEXT;
  if (WiFi.status() == WL_CONNECTED) status = "已連線";
  else if (wifiTrying) { status = "連線中"; sc = C_AMBER; }
  else { status = "未連線"; sc = C_MUTED; }

  String ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : cfgSsid;
  if (ssid.length() == 0) ssid = "---";
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "---";
  String gw = WiFi.status() == WL_CONNECTED ? WiFi.gatewayIP().toString() : "---";
  String rssi = "---";
  if (WiFi.status() == WL_CONNECTED) {
    int r = WiFi.RSSI();
    rssi = String(r) + " dBm (" + rssiQuality(r) + ")";
  }
  String host = String(WiFi.getHostname());

  const char* labels[8] = {"狀態", "SSID", "IP位址", "訊號", "閘道", "MAC", "主機名稱", "AP位址"};
  String values[8] = {status, ssid, ip, rssi, gw, WiFi.macAddress(), host, WiFi.softAPIP().toString()};

  for (int i = 0; i < 8; i++) {
    int y = 27 + i * 15;
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(labels[i], 12, y);
    lcd.setTextColor(i == 0 ? sc : C_TEXT, C_BG);
    lcd.drawString(fitText(values[i], 226, &fonts::efontTW_14), 84, y);
  }
}

void drawSystemInfo() {
  lcd.fillScreen(C_BG);
  drawHeader("系統資訊");
  drawFooter("長按 P14 返回");

  String psram = ESP.getPsramSize() ? String(ESP.getPsramSize() / 1048576.0f, 1) + " MB" : "無";
  String heap = String(ESP.getFreeHeap() / 1024) + " KB / " + String(ESP.getMinFreeHeap() / 1024) + " KB";
  String battery = batValid ? String(batVolt, 2) + " V (" + String(batPct) + "%)" : "無電池";

  const char* labels[8] = {"晶片", "核心", "快閃", "PSRAM", "記憶體", "運作時間", "電量", "SDK"};
  String values[8] = {
    String(ESP.getChipModel()) + " r" + String(ESP.getChipRevision()),
    String(ESP.getCpuFreqMHz()) + " MHz x2",
    String(ESP.getFlashChipSize() / 1048576.0f, 1) + " MB",
    psram,
    heap,
    fmtUptime(),
    battery,
    String(ESP.getSdkVersion())
  };

  for (int i = 0; i < 8; i++) {
    int y = 28 + i * 15;
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(labels[i], 12, y);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString(fitText(values[i], 226, &fonts::efontTW_14), 84, y);
  }
}

void drawDisplayInfo() {
  lcd.fillScreen(C_BG);
  drawHeader("顯示器");
  drawFooter("P0:亮度  P14長按:返回");

  const char* labels[5] = {"驅動", "面板", "畫布", "匯流排", "亮度"};
  String values[5] = {
    "ST7789",
    "170x320 IPS",
    "320x170",
    "8-bit parallel",
    String(BRIGHT_LEVELS[brightIdx] * 100 / 255) + "%"
  };

  for (int i = 0; i < 5; i++) {
    int y = 30 + i * 19;
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(labels[i], 12, y);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString(values[i], 84, y);
  }

  static const uint16_t bars[8] = {
    rgb565(255, 0, 0), rgb565(255, 128, 0), rgb565(255, 255, 0), rgb565(0, 255, 0),
    rgb565(0, 255, 255), rgb565(0, 0, 255), rgb565(255, 0, 255), rgb565(255, 255, 255)
  };
  for (int i = 0; i < 8; i++) {
    lcd.fillRect(5 + i * 39, 124, 38, 22, bars[i]);
  }
}

void drawOtaScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("OTA 更新");
  drawFooter("P14長按:返回");

  bool fsActive = fsRunning || fsStatus.length() > 0;
  bool busy = otaRunning || fsRunning;
  uint8_t percent = fsActive ? fsProgress : otaProgress;
  String statusText = fsActive ? fsStatus : otaStatus;

  const char* labels[5] = {"AP名稱", "密碼", "網址", "韌體", "狀態"};
  String values[5] = {
    AP_SSID,
    "無(開放)",
    "http://" + WiFi.softAPIP().toString(),
    FW_VERSION,
    statusText
  };

  for (int i = 0; i < 5; i++) {
    int y = 27 + i * 17;
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(labels[i], 12, y);
    lcd.setTextColor(i == 4 && busy ? C_ACCENT2 : C_TEXT, C_BG);
    lcd.drawString(fitText(values[i], 226, &fonts::efontTW_14), 84, y);
  }

  int barX = 12;
  int barY = 118;
  int barW = 296;
  int barH = 15;
  lcd.drawRoundRect(barX, barY, barW, barH, 4, C_LINE);
  int fillW = (barW - 4) * (int)percent / 100;
  if (fillW > 0) {
    lcd.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 3, busy ? C_ACCENT2 : C_ACCENT);
  }

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString("瀏覽器開啟網址即可更新", 160, 143);
}

void drawPhotoEmpty() {
  lcd.fillScreen(C_BG);
  drawHeader("電子相片");
  drawFooter("P14:重新整理 長按:返回");

  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString("尚無相片", 160, 58);

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString("開啟下列網址上傳圖檔", 160, 86);
  lcd.setTextColor(C_ACCENT2, C_BG);
  lcd.drawString("http://" + WiFi.softAPIP().toString(), 160, 110);
}

void drawPhotoView() {
  if (photoCount == 0) {
    drawPhotoEmpty();
    photoLastMs = millis();
    return;
  }

  lcd.fillScreen(C_BG);
  bool ok = drawPhotoFile(photoIdx);
  if (!ok) {
    lcd.setFont(&fonts::efontTW_16_b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(C_RED, C_BG);
    lcd.drawString("無法解碼此圖片", 160, 70);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(fitText(photoNames[photoIdx], 280, &fonts::efontTW_14), 160, 98);
  }

  int barY = SCREEN_H - 17;
  lcd.fillRect(0, barY, SCREEN_W, 17, C_CARD);
  lcd.drawFastHLine(0, barY, SCREEN_W, C_LINE);
  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_ACCENT, C_CARD);
  lcd.drawString(String(photoIdx + 1) + "/" + String(photoCount), 8, barY + 8);
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.setTextColor(C_MUTED, C_CARD);
  lcd.drawString("P0:切換  P14長按:返回", 312, barY + 8);

  photoLastMs = millis();
}

void photoStep(int dir) {
  if (photoCount == 0) return;
  int n = photoCount;
  int s = (photoIdx + dir) % n;
  if (s < 0) s += n;
  photoIdx = s;
  drawPhotoView();
}

void drawPhotoInfo() {
  lcd.fillScreen(C_BG);
  drawHeader("相片資訊");
  drawFooter("長按 P14 返回");

  size_t photoBytes = 0;
  for (int i = 0; i < photoCount; i++) photoBytes += photoSizes[i];
  size_t total = spiffsReady ? SPIFFS.totalBytes() : 0;
  size_t used = spiffsReady ? SPIFFS.usedBytes() : 0;

  const char* labels[5] = {"相片數量", "相片容量", "已用空間", "剩餘空間", "上傳網址"};
  String values[5] = {
    String(photoCount) + " 張",
    fmtBytes(photoBytes),
    fmtBytes(used) + " / " + fmtBytes(total),
    fmtBytes(total > used ? total - used : 0),
    "http://" + WiFi.softAPIP().toString()
  };

  for (int i = 0; i < 5; i++) {
    int y = 27 + i * 17;
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(labels[i], 12, y);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString(fitText(values[i], 226, &fonts::efontTW_14), 84, y);
  }

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString("至網頁「電子相片」即可上傳", 160, 128);
}


void drawWeatherScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("天氣");
  drawFooter("P14:更新 長按:返回");

  if (!weather.valid) {
    lcd.setFont(&fonts::efontTW_16_b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(C_RED, C_BG);
    lcd.drawString(cfgSsid.length() == 0 ? "請先設定 WiFi" : "無法取得天氣資料", 160, 72);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("P14 重新整理 / 長按返回", 160, 102);
    return;
  }

  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_ACCENT, C_BG);
  lcd.drawString(fitText(cfgCity, 120, &fonts::efontTW_16_b), 12, 27);

  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::top_right);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString("更新 " + timeLabel(weather.updated), 310, 30);

  String tempStr = String(weather.temp, 1) + " C";
  lcd.setFont(&fonts::FreeSansBold24pt7b);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(tempStr, 12, 44);
  int tempW = lcd.textWidth(tempStr);

  String desc = weatherDesc(weather.code);
  int descX = 12 + tempW + 16;
  if (descX > 230) descX = 230;
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_ACCENT2, C_BG);
  lcd.drawString(fitText(desc, 310 - descX, &fonts::efontTW_16_b), descX, 61);

  lcd.drawFastHLine(12, 86, 296, C_LINE);

  String r1a = "體感 " + String(weather.feels, 1) + " C";
  String r1b = "濕度 " + String(weather.humidity) + "%";
  String r2a = "風速 " + String(weather.wind, 1) + " km/h";
  String r2b = "雨量 " + String(weather.precip, 1) + " mm";
  String r3a = "最高 " + String(weather.tmax, 1) + " C";
  String r3b = "最低 " + String(weather.tmin, 1) + " C";

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(r1a, 12, 94);
  lcd.drawString(r1b, 170, 94);
  lcd.drawString(r2a, 12, 114);
  lcd.drawString(r2b, 170, 114);
  lcd.drawString(r3a, 12, 134);
  lcd.drawString(r3b, 170, 134);
}

void drawStockQuoteScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("股票查詢");
  drawFooter("P14:更新 長按:返回");

  if (!stock.valid) {
    lcd.setFont(&fonts::efontTW_16_b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(C_RED, C_BG);
    lcd.drawString(cfgSsid.length() == 0 ? "請先設定 WiFi" : "無法取得股票資料", 160, 72);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(fitText(cfgSymbol, 280, &fonts::efontTW_14), 160, 100);
    return;
  }

  float chg = stock.price - stock.prevClose;
  float pct = (stock.prevClose != 0.0f) ? (chg / stock.prevClose * 100.0f) : 0.0f;
  uint16_t cc = (chg >= 0.0f) ? C_RED : C_GREEN;

  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_ACCENT, C_BG);
  lcd.drawString(fitText(stock.symbol, 250, &fonts::efontTW_16_b), 12, 26);

  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::top_right);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(stock.currency, 310, 28);

  lcd.setFont(&fonts::FreeSansBold24pt7b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(String(stock.price, 2), 12, 41);

  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextColor(cc, C_BG);
  lcd.drawString(fmtSigned(chg, 2) + "  " + fmtSigned(pct, 2) + "%", 12, 80);

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString("昨收 " + String(stock.prevClose, 2), 12, 102);
  lcd.drawString("最高 " + String(stock.high, 2), 170, 102);
  lcd.drawString("最低 " + String(stock.low, 2), 12, 118);
  lcd.drawString("成交量 " + fmtVolume(stock.volume), 170, 118);
  lcd.drawString("更新 " + timeLabel(stock.updated), 12, 134);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(stock.currency, 170, 134);
}

float mapF(float v, float inMin, float inMax, int outMin, int outMax) {
  if (inMax == inMin) return (outMin + outMax) / 2.0f;
  return outMin + (v - inMin) * (outMax - outMin) / (inMax - inMin);
}

void drawStockChartScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("股票走勢");
  drawFooter("P14:更新 長按:返回");

  if (!stock.valid || stock.n < 2) {
    lcd.setFont(&fonts::efontTW_16_b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(C_RED, C_BG);
    lcd.drawString(cfgSsid.length() == 0 ? "請先設定 WiFi" : "沒有足夠的走勢資料", 160, 76);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(fitText(cfgSymbol, 280, &fonts::efontTW_14), 160, 104);
    return;
  }

  float chg = stock.price - stock.prevClose;
  float pct = (stock.prevClose != 0.0f) ? (chg / stock.prevClose * 100.0f) : 0.0f;
  uint16_t cc = (chg >= 0.0f) ? C_RED : C_GREEN;

  String summary = stock.symbol + "  " + String(stock.price, 2) + "  " + fmtSigned(chg, 2) + "  " + fmtSigned(pct, 2) + "%";
  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(cc, C_BG);
  lcd.drawString(fitText(summary, 304, &fonts::efontTW_14), 8, 25);

  const int x0 = 44;
  const int x1 = 312;
  const int y0 = 44;
  const int y1 = 136;

  float mn = stock.points[0];
  float mx = stock.points[0];
  for (int i = 1; i < stock.n; i++) {
    if (stock.points[i] < mn) mn = stock.points[i];
    if (stock.points[i] > mx) mx = stock.points[i];
  }
  if (mx - mn < 0.01f) {
    mx += 1.0f;
    mn -= 1.0f;
  }
  float pad = (mx - mn) * 0.08f;
  mn -= pad;
  mx += pad;

  lcd.drawRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, C_LINE);

  for (int i = 0; i <= 4; i++) {
    int y = y0 + (y1 - y0) * i / 4;
    if (i > 0 && i < 4) lcd.drawFastHLine(x0 + 1, y, x1 - x0 - 1, C_DARK);
    int x = x0 + (x1 - x0) * i / 4;
    if (i > 0 && i < 4) lcd.drawFastVLine(x, y0 + 1, y1 - y0 - 1, C_DARK);
  }

  lcd.setFont(&fonts::Font0);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(String(mx, 2), x0 - 3, y0);
  lcd.drawString(String((mx + mn) / 2.0f, 2), x0 - 3, (y0 + y1) / 2);
  lcd.drawString(String(mn, 2), x0 - 3, y1);

  if (stock.prevClose > mn && stock.prevClose < mx) {
    int yp = (int)mapF(stock.prevClose, mn, mx, y1, y0);
    for (int x = x0 + 2; x < x1 - 2; x += 7) {
      lcd.drawFastHLine(x, yp, 4, C_AMBER);
    }
    if (yp > y0 + 12) {
      lcd.setFont(&fonts::efontTW_14);
      lcd.setTextDatum(textdatum_t::top_left);
      lcd.setTextColor(C_AMBER, C_BG);
      lcd.drawString("昨收", x0 + 3, yp - 15);
    }
  }

  int px = x0;
  int py = (int)mapF(stock.points[0], mn, mx, y1, y0);
  for (int i = 1; i < stock.n; i++) {
    int x = x0 + (x1 - x0) * i / (stock.n - 1);
    int y = (int)mapF(stock.points[i], mn, mx, y1, y0);
    if (y < y0) y = y0;
    if (y > y1) y = y1;
    lcd.drawWideLine(px, py, x, y, 1.0f, cc);
    px = x;
    py = y;
  }
  lcd.fillCircle(px, py, 2, C_TEXT);

  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(timeLabel(stock.ts[0]), x0, 140);
  lcd.setTextDatum(textdatum_t::top_right);
  lcd.drawString(timeLabel(stock.ts[stock.n - 1]), x1, 140);
}

void drawClockMessage() {
  lcd.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H - FOOTER_H, C_BG);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(WiFi.status() == WL_CONNECTED ? "NTP 同步中..." : "請先連接 WiFi", 160, 80);
  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(WiFi.status() == WL_CONNECTED ? "請稍候" : "至 WiFi設定 連線", 160, 104);
}

void updateClock() {
  struct tm t;
  if (!getLocalTime(&t, 5)) {
    if (!clockMsgDrawn) {
      drawClockMessage();
      clockMsgDrawn = true;
      clockReady = false;
    }
    return;
  }

  if (!clockReady) {
    lcd.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H - FOOTER_H, C_BG);
    clockReady = true;
    clockMsgDrawn = false;
    lastClockMin = -1;
    lastClockSec = -1;
  }

  if (t.tm_min != lastClockMin) {
    char hm[8];
    strftime(hm, sizeof(hm), "%H:%M", &t);
    lcd.setFont(&fonts::AsciiFont24x48);
    lcd.setTextSize(1.6f, 1.6f);
    lcd.setTextDatum(textdatum_t::top_center);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString(hm, 150, 38);

    char date[16];
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
    String ds = String(date) + "  " + weekdayTW(t.tm_wday);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(ds, 160, 122);
    lastClockMin = t.tm_min;
  }

  if (t.tm_sec != lastClockSec) {
    char ss[4];
    strftime(ss, sizeof(ss), "%S", &t);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::top_center);
    lcd.setTextColor(C_ACCENT, C_BG);
    lcd.drawString(ss, 292, 58);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("秒", 292, 82);
    lastClockSec = t.tm_sec;
  }
}

void drawClockScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("時間  UTC+8");
  drawFooter("長按 P14 返回");
  clockReady = false;
  clockMsgDrawn = false;
  lastClockSec = -1;
  lastClockMin = -1;
  updateClock();
}

void openWeather() {
  returnScreen = SCR_MAIN;
  screen = SCR_WEATHER;
  drawBusy("天氣", "正在取得天氣資料...", "Open-Meteo");
  bool ok = fetchWeather();
  resetButtons();
  if (!ok) weather.valid = false;
  drawWeatherScreen();
}

void openClock() {
  returnScreen = SCR_MAIN;
  screen = SCR_CLOCK;
  ensureTimeService();
  drawClockScreen();
}

void openStockQuote() {
  returnScreen = SCR_STOCK_MENU;
  screen = SCR_STOCK_QUOTE;
  drawBusy("股票查詢", "正在取得報價...", cfgSymbol);
  bool ok = fetchStock(false);
  resetButtons();
  if (!ok) stock.valid = false;
  drawStockQuoteScreen();
}

void openStockChart() {
  returnScreen = SCR_STOCK_MENU;
  screen = SCR_STOCK_CHART;
  drawBusy("股票走勢", "正在取得走勢資料...", cfgSymbol);
  bool ok = fetchStock();
  resetButtons();
  if (!ok) stock.valid = false;
  drawStockChartScreen();
}

void openWifiInfo() {
  returnScreen = SCR_SYS_MENU;
  screen = SCR_WIFI_INFO;
  drawWifiInfo();
}

void openSystemInfo() {
  returnScreen = SCR_SYS_MENU;
  screen = SCR_SYS_INFO;
  drawSystemInfo();
}

void openDisplayInfo() {
  returnScreen = SCR_SYS_MENU;
  screen = SCR_DISPLAY;
  drawDisplayInfo();
}

void openOtaScreen() {
  returnScreen = SCR_MAIN;
  screen = SCR_OTA;
  if (otaStatus.length() == 0) otaStatus = "待機";
  drawOtaScreen();
}

void openPhotoView() {
  returnScreen = SCR_PHOTO_MENU;
  screen = SCR_PHOTO_VIEW;
  refreshPhotoList();
  if (photoIdx >= photoCount) photoIdx = 0;
  drawPhotoView();
}

void openPhotoInfo() {
  returnScreen = SCR_PHOTO_MENU;
  screen = SCR_PHOTO_INFO;
  refreshPhotoList();
  drawPhotoInfo();
}

void adjustScanTop() {
  if (scanSel < scanTop) scanTop = scanSel;
  if (scanSel >= scanTop + VISIBLE_ROWS) scanTop = scanSel - VISIBLE_ROWS + 1;
}

void drawLock(int x, int y, bool enc, uint16_t color) {
  if (!enc) return;
  lcd.drawRect(x, y + 3, 7, 5, color);
  lcd.drawRect(x + 2, y, 3, 4, color);
}

void drawSignalBars(int x, int y, int rssi, uint16_t onColor, uint16_t offColor) {
  int lvl = (rssi + 95) / 10;
  if (lvl < 1) lvl = 1;
  if (lvl > 4) lvl = 4;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    lcd.fillRect(x + i * 4, y + 12 - h, 3, h, (i < lvl) ? onColor : offColor);
  }
}

void drawScanScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("掃描網路");
  drawFooter("P0:下/長按上  P14:連線/長按返回");

  if (scanN == 0) {
    lcd.setFont(&fonts::efontTW_16_b);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString("找不到網路", 160, 72);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("P14 重新掃描 / 長按返回", 160, 100);
    return;
  }

  for (int i = 0; i < VISIBLE_ROWS; i++) {
    int idx = scanTop + i;
    if (idx >= scanN) break;
    int y = 30 + i * ROW_H;
    bool sel = (idx == scanSel);
    uint16_t bg = sel ? C_ACCENT : C_CARD;
    uint16_t fg = sel ? C_BG : C_TEXT;
    uint16_t dim = sel ? C_CARD : C_DARK;
    lcd.fillRoundRect(6, y, 306, 20, 4, bg);
    if (!sel) lcd.drawRoundRect(6, y, 306, 20, 4, C_LINE);

    String name = fitText(scanSsid[idx], 220, &fonts::efontTW_14);
    lcd.setFont(&fonts::efontTW_14);
    lcd.setTextSize(1.0f, 1.0f);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(fg, bg);
    lcd.drawString(name, 16, y + 10);

    drawLock(246, y + 6, scanEnc[idx] != WIFI_AUTH_OPEN, fg);
    drawSignalBars(260, y + 4, scanRssi[idx], fg, dim);

    lcd.setFont(&fonts::Font0);
    lcd.setTextDatum(textdatum_t::middle_right);
    lcd.setTextColor(fg, bg);
    lcd.drawString(String(scanRssi[idx]), 310, y + 10);
  }
}

void doScan() {
  returnScreen = SCR_WIFI_MENU;
  screen = SCR_SCAN;
  drawBusy("掃描網路", "正在掃描 WiFi...", "請稍候");
  bool wasConnected = (WiFi.status() == WL_CONNECTED);
  WiFi.scanDelete();
  int found = WiFi.scanNetworks(false, true);
  scanN = 0;
  if (found > 0) {
    for (int i = 0; i < found && scanN < MAX_SCAN; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      bool dup = false;
      for (int k = 0; k < scanN; k++) {
        if (scanSsid[k] == ssid) {
          dup = true;
          break;
        }
      }
      if (dup) continue;
      scanSsid[scanN] = ssid;
      scanRssi[scanN] = WiFi.RSSI(i);
      scanEnc[scanN] = (uint8_t)WiFi.encryptionType(i);
      scanN++;
    }
  }
  WiFi.scanDelete();
  for (int i = 0; i < scanN - 1; i++) {
    for (int j = i + 1; j < scanN; j++) {
      if (scanRssi[j] > scanRssi[i]) {
        int tr = scanRssi[i];
        scanRssi[i] = scanRssi[j];
        scanRssi[j] = tr;
        String ts = scanSsid[i];
        scanSsid[i] = scanSsid[j];
        scanSsid[j] = ts;
        uint8_t te = scanEnc[i];
        scanEnc[i] = scanEnc[j];
        scanEnc[j] = te;
      }
    }
  }
  if (wasConnected && WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  scanSel = 0;
  scanTop = 0;
  resetButtons();
  drawScanScreen();
}

void attemptConnect(const String& ssid, const String& pass) {
  screen = SCR_WIFI_MENU;
  drawBusy("WiFi", "正在連線...", ssid);
  bool ok = connectWiFiBlocking(ssid, pass, WIFI_CONNECT_TIMEOUT_MS);
  resetButtons();
  if (ok) {
    saveWiFi(ssid, pass);
    showToast("已連線", C_ACCENT);
  } else {
    showToast("連線失敗", C_RED);
  }
  showWifiMenu();
}

void removeLastUtf8(String& s) {
  if (s.length() == 0) return;
  int i = s.length() - 1;
  while (i > 0 && (((uint8_t)s[i]) & 0xC0) == 0x80) i--;
  s.remove(i);
}

void drawKeyboard() {
  lcd.fillScreen(C_BG);
  drawHeader(kbTitle);

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_MUTED, C_CARD);
  lcd.drawString("長按返回", 150, 12);

  lcd.fillRoundRect(5, 25, 310, 18, 4, C_CARD);
  lcd.drawRoundRect(5, 25, 310, 18, 4, kbShift ? C_ACCENT : C_LINE);

  String shown;
  if (kbMask) {
    for (unsigned int i = 0; i < kbValue.length(); i++) shown += '*';
  } else {
    shown = kbValue;
  }
  shown += "_";
  lcd.setFont(&fonts::efontTW_14);
  while (shown.length() && lcd.textWidth(shown) > 296) {
    int cut = 1;
    while (cut < (int)shown.length() && (((uint8_t)shown[cut]) & 0xC0) == 0x80) cut++;
    shown.remove(0, cut);
  }
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_TEXT, C_CARD);
  lcd.drawString(shown, 12, 34);

  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 10; c++) {
      int idx = r * 10 + c;
      bool sel = (idx == kbCursor);
      int x = 5 + c * 31;
      int y = 46 + r * 24;
      bool shiftKey = (r == 4 && c == 0);
      uint16_t bg = sel ? C_ACCENT : C_CARD;
      if (shiftKey && kbShift) bg = sel ? C_ACCENT2 : C_CARD2;
      uint16_t fg = sel ? C_BG : (r == 4 ? C_MUTED : C_TEXT);
      if (shiftKey && kbShift) fg = sel ? C_BG : C_ACCENT;

      lcd.fillRoundRect(x, y, 29, 22, 3, bg);
      if (!sel) lcd.drawRoundRect(x, y, 29, 22, 3, C_LINE);

      String lab = KB_KEYS[r][c];
      if (kbShift && lab.length() == 1 && isalpha((uint8_t)lab[0])) lab.toUpperCase();

      lcd.setFont(&fonts::Font0);
      lcd.setTextSize(1.0f, 1.0f);
      lcd.setTextDatum(textdatum_t::middle_center);
      lcd.setTextColor(fg, bg);
      lcd.drawString(lab, x + 14, y + 11);
    }
  }
}

void kbCancel() {
  if (kbReturn == SCR_SCAN) {
    screen = SCR_SCAN;
    drawScanScreen();
  } else if (kbReturn == SCR_STOCK_MENU) {
    showStockMenu();
  } else {
    showWifiMenu();
  }
}

void kbSubmit() {
  if (kbMode == KB_SYMBOL) {
    String s = kbValue;
    s.trim();
    s.toUpperCase();
    if (s.length() == 0) s = "2330.TW";
    saveSymbol(s);
    showToast("已儲存", C_ACCENT);
    showStockMenu();
    return;
  }

  if (kbMode == KB_SSID) {
    if (kbValue.length() == 0) {
      showToast("SSID 不可為空", C_RED);
      drawKeyboard();
      return;
    }
    pendingSsid = kbValue;
    kbMode = KB_PASS;
    kbTitle = "輸入密碼";
    kbValue = (pendingSsid == cfgSsid) ? cfgPass : "";
    kbMask = true;
    kbShift = false;
    kbCursor = 0;
    drawKeyboard();
    return;
  }

  attemptConnect(pendingSsid, kbValue);
}

void pressKey(uint8_t idx) {
  int r = idx / 10;
  int c = idx % 10;
  String k = KB_KEYS[r][c];

  if (k == "SFT") {
    kbShift = !kbShift;
    drawKeyboard();
    return;
  }
  if (k == "OK") {
    kbSubmit();
    return;
  }
  if (k == "DEL") {
    removeLastUtf8(kbValue);
    drawKeyboard();
    return;
  }

  if (k == "SPC") {
    kbValue += " ";
  } else if (k == ".com") {
    kbValue += ".com";
  } else {
    char ch = k[0];
    if (kbShift && isalpha((uint8_t)ch)) ch = toupper(ch);
    kbValue += ch;
    if (kbShift) kbShift = false;
  }

  int maxLen = 32;
  if (kbMode == KB_PASS) maxLen = 64;
  if (kbMode == KB_SYMBOL) maxLen = 16;
  if ((int)kbValue.length() > maxLen) kbValue.remove(maxLen);

  drawKeyboard();
}

void openManualSetup() {
  kbMode = KB_SSID;
  kbTitle = "設定 SSID";
  kbValue = cfgSsid;
  kbMask = false;
  kbShift = false;
  kbCursor = 0;
  kbReturn = SCR_WIFI_MENU;
  screen = SCR_KEYBOARD;
  drawKeyboard();
}

void openSymbolKeyboard() {
  kbMode = KB_SYMBOL;
  kbTitle = "股票代號";
  kbValue = cfgSymbol;
  kbMask = false;
  kbShift = false;
  kbCursor = 0;
  kbReturn = SCR_STOCK_MENU;
  screen = SCR_KEYBOARD;
  drawKeyboard();
}

void connectScanSelection() {
  if (scanN == 0) {
    doScan();
    return;
  }
  pendingSsid = scanSsid[scanSel];
  if (scanEnc[scanSel] == WIFI_AUTH_OPEN) {
    attemptConnect(pendingSsid, "");
    return;
  }
  kbMode = KB_PASS;
  kbTitle = "輸入密碼";
  kbValue = (pendingSsid == cfgSsid) ? cfgPass : "";
  kbMask = true;
  kbShift = false;
  kbCursor = 0;
  kbReturn = SCR_SCAN;
  screen = SCR_KEYBOARD;
  drawKeyboard();
}

void drawClearScreen() {
  lcd.fillScreen(C_BG);
  drawHeader("清除設定");
  drawFooter("P0:切換  P14:確認 長按:返回");

  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString("清除 WiFi 設定?", 160, 56);

  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.drawString(fitText(cfgSsid.length() ? cfgSsid : "目前沒有儲存設定", 280, &fonts::efontTW_14), 160, 82);

  int by = 104;
  uint16_t bgNo = clearChoice ? C_CARD : C_ACCENT;
  uint16_t fgNo = clearChoice ? C_TEXT : C_BG;
  uint16_t bgYes = clearChoice ? C_RED : C_CARD;
  uint16_t fgYes = clearChoice ? C_TEXT : C_MUTED;
  lcd.fillRoundRect(68, by, 70, 34, 6, bgNo);
  lcd.fillRoundRect(182, by, 70, 34, 6, bgYes);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(fgNo, bgNo);
  lcd.drawString("否", 103, by + 17);
  lcd.setTextColor(fgYes, bgYes);
  lcd.drawString("是", 217, by + 17);
}

void openClearConfirm() {
  returnScreen = SCR_WIFI_MENU;
  screen = SCR_WIFI_CLEAR;
  clearChoice = false;
  drawClearScreen();
}

void activateMenuItem() {
  if (screen == SCR_MAIN) {
    if (menu.sel == 0) showSysMenu();
    else if (menu.sel == 1) showWifiMenu();
    else if (menu.sel == 2) openWeather();
    else if (menu.sel == 3) openClock();
    else if (menu.sel == 4) showStockMenu();
    else if (menu.sel == 5) showPhotoMenu();
    else if (menu.sel == 6) openOtaScreen();
  } else if (screen == SCR_SYS_MENU) {
    if (menu.sel == 0) openWifiInfo();
    else if (menu.sel == 1) openSystemInfo();
    else if (menu.sel == 2) openDisplayInfo();
  } else if (screen == SCR_WIFI_MENU) {
    if (menu.sel == 0) doScan();
    else if (menu.sel == 1) openManualSetup();
    else if (menu.sel == 2) openClearConfirm();
  } else if (screen == SCR_STOCK_MENU) {
    if (menu.sel == 0) openStockQuote();
    else if (menu.sel == 1) openStockChart();
    else if (menu.sel == 2) openSymbolKeyboard();
  } else if (screen == SCR_PHOTO_MENU) {
    if (menu.sel == 0) openPhotoView();
    else if (menu.sel == 1) openPhotoInfo();
  }
}

void handleInput(uint8_t evNext, uint8_t evOk) {
  if (screen == SCR_SPLASH) return;
  if (otaRunning || fsRunning) return;

  if (screen == SCR_KEYBOARD) {
    if (evNext == EV_SHORT) {
      kbCursor = (kbCursor + 1) % 50;
      drawKeyboard();
    } else if (evNext == EV_LONG) {
      kbCursor = (kbCursor + 10) % 50;
      drawKeyboard();
    } else if (evOk == EV_SHORT) {
      pressKey(kbCursor);
    } else if (evOk == EV_LONG) {
      kbCancel();
    }
    return;
  }

  if (screen == SCR_SCAN) {
    if (scanN > 0) {
      if (evNext == EV_SHORT) {
        scanSel = (scanSel + 1) % scanN;
        adjustScanTop();
        drawScanScreen();
      } else if (evNext == EV_LONG) {
        scanSel = (scanSel + scanN - 1) % scanN;
        adjustScanTop();
        drawScanScreen();
      }
    }
    if (evOk == EV_SHORT) {
      if (scanN == 0) doScan();
      else connectScanSelection();
    } else if (evOk == EV_LONG) {
      showWifiMenu();
    }
    return;
  }

  if (screen == SCR_WIFI_CLEAR) {
    if (evNext == EV_SHORT) {
      clearChoice = !clearChoice;
      drawClearScreen();
    } else if (evOk == EV_SHORT) {
      if (clearChoice) {
        clearWifiSettings();
        showToast("已清除設定", C_ACCENT);
      }
      showWifiMenu();
    } else if (evOk == EV_LONG) {
      showWifiMenu();
    }
    return;
  }

  if (screen == SCR_CLOCK) {
    if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_WEATHER) {
    if (evOk == EV_SHORT) openWeather();
    else if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_STOCK_QUOTE) {
    if (evOk == EV_SHORT) openStockQuote();
    else if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_STOCK_CHART) {
    if (evOk == EV_SHORT) openStockChart();
    else if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_OTA) {
    if (evOk == EV_SHORT) drawOtaScreen();
    else if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_PHOTO_VIEW) {
    if (evNext == EV_SHORT) {
      photoStep(1);
    } else if (evNext == EV_LONG) {
      photoStep(-1);
    } else if (evOk == EV_SHORT) {
      refreshPhotoList();
      if (photoCount == 0) drawPhotoEmpty();
      else {
        if (photoIdx >= photoCount) photoIdx = 0;
        drawPhotoView();
      }
    } else if (evOk == EV_LONG) {
      goBack();
    }
    return;
  }

  if (screen == SCR_WIFI_INFO || screen == SCR_SYS_INFO || screen == SCR_PHOTO_INFO) {
    if (evOk == EV_LONG) goBack();
    return;
  }

  if (screen == SCR_DISPLAY) {
    if (evNext == EV_SHORT) {
      brightIdx = (brightIdx + 1) % 4;
      lcd.setBrightness(BRIGHT_LEVELS[brightIdx]);
      drawDisplayInfo();
    } else if (evOk == EV_LONG) {
      goBack();
    }
    return;
  }

  if (screen == SCR_MAIN || screen == SCR_SYS_MENU || screen == SCR_WIFI_MENU ||
      screen == SCR_STOCK_MENU || screen == SCR_PHOTO_MENU) {
    if (evNext == EV_SHORT) menuMove(1);
    else if (evNext == EV_LONG) menuMove(-1);
    else if (evOk == EV_SHORT) activateMenuItem();
    else if (evOk == EV_LONG && screen != SCR_MAIN) showMainMenu();
  }
}

void drawSplash() {
  lcd.fillScreen(C_BG);
  lcd.fillRoundRect(16, 30, 288, 110, 12, C_CARD);
  lcd.drawRoundRect(16, 30, 288, 110, 12, C_ACCENT);
  lcd.setFont(&fonts::efontTW_16_b);
  lcd.setTextSize(1.0f, 1.0f);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_TEXT, C_CARD);
  lcd.drawString("T-Display S3", 160, 58);
  lcd.setFont(&fonts::efontTW_14);
  lcd.setTextColor(C_ACCENT2, C_CARD);
  lcd.drawString("ESP32-S3 控制面板", 160, 84);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(C_MUTED, C_CARD);
  lcd.drawString("Booting...", 160, 112);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LCD_POWER, OUTPUT);
  digitalWrite(PIN_LCD_POWER, HIGH);

  btnNext.begin();
  btnOk.begin();

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT, ADC_11db);

  loadConfig();

  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(BRIGHT_LEVELS[brightIdx]);
  lcd.fillScreen(C_BG);

  drawSplash();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("TDisplayS3");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  startAccessPoint();
  initFileSystem();
  refreshPhotoList();
  setupWebServer();

  if (cfgSsid.length()) {
    WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    wifiTrying = true;
    wifiTryStart = millis();
  }

  updateBattery();
  delay(700);
  showMainMenu();

  lastBatMs = millis();
  lastIconMs = millis();
}

void loop() {
  webServer.handleClient();

  uint8_t evNext = btnNext.poll();
  uint8_t evOk = btnOk.poll();
  if (evNext != EV_NONE || evOk != EV_NONE) {
    handleInput(evNext, evOk);
  }

  uint32_t now = millis();

  if (wifiTrying) {
    if (WiFi.status() == WL_CONNECTED) wifiTrying = false;
    else if (now - wifiTryStart > WIFI_TRY_TIMEOUT_MS) wifiTrying = false;
  }

  if (WiFi.status() == WL_CONNECTED) ensureTimeService();

  if (now - lastBatMs >= BAT_INTERVAL_MS) {
    lastBatMs = now;
    updateBattery();
    if (screen != SCR_SPLASH && screen != SCR_PHOTO_VIEW) drawStatusIcons();
  }

  if (now - lastIconMs >= ICON_INTERVAL_MS) {
    lastIconMs = now;
    iconBlink = !iconBlink;
    if (screen != SCR_SPLASH && screen != SCR_PHOTO_VIEW) drawStatusIcons();
  }

  if (screen == SCR_CLOCK) updateClock();

  if (screen == SCR_PHOTO_VIEW && photoCount > 1 && now - photoLastMs >= PHOTO_SLIDE_MS) {
    photoIdx = (photoIdx + 1) % photoCount;
    drawPhotoView();
  }

  delay(5);
}
