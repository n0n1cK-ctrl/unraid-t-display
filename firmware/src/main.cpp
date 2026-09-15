#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

#include "secrets.h"
#include "cover.h"

constexpr uint32_t REFRESH_INTERVAL_MS = 5000;

TFT_eSPI tft;
constexpr uint16_t UNRAID_ORANGE = 0xF2C4;
uint32_t lastRefresh = 0;

uint16_t statusColor(int value, int yellowAt, int redAt) {
  if (value >= redAt) return TFT_RED;
  if (value >= yellowAt) return TFT_YELLOW;
  return TFT_GREEN;
}

void drawBar(int x, int y, int width, int height, int percent, uint16_t color) {
  percent = constrain(percent, 0, 100);

  tft.fillRoundRect(x, y, width, height, 3, TFT_DARKGREY);

  int fillWidth = (width * percent) / 100;
  if (fillWidth > 0) {
    tft.fillRoundRect(x, y, fillWidth, height, 3, color);
  }
}

String compactUptime(const char* uptime) {
  String value = uptime ? uptime : "";

  value.replace("weeks", "W");
  value.replace("week", "W");
  value.replace("days", "D");
  value.replace("day", "D");
  value.replace("hours", "H");
  value.replace("hour", "H");
  value.replace("minutes", "M");
  value.replace("minute", "M");
  value.replace(",", "");

  value.trim();

  int firstSpace = value.indexOf(' ');
  if (firstSpace < 0) return value;

  int secondSpace = value.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) return value;

  int thirdSpace = value.indexOf(' ', secondSpace + 1);
  if (thirdSpace < 0) return value;

  return value.substring(0, thirdSpace);
}

void drawLabelValue(const char* label, const String& value, int y, uint16_t color = TFT_WHITE) {
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(label, 8, y);

  tft.setTextColor(color, TFT_BLACK);
  int valueWidth = tft.textWidth(value);
  tft.drawString(value, 162 - valueWidth, y);
}

void drawDiskIcon(int x, int y) {
  tft.drawRoundRect(x, y, 32, 22, 3, TFT_LIGHTGREY);
  tft.drawFastHLine(x + 3, y + 6, 26, TFT_DARKGREY);
  tft.fillCircle(x + 24, y + 15, 3, UNRAID_ORANGE);
  tft.drawCircle(x + 9, y + 15, 3, TFT_LIGHTGREY);
}

void drawRamIcon(int x, int y) {
  tft.drawRoundRect(x, y, 34, 16, 2, TFT_LIGHTGREY);
  for (int i = 0; i < 4; i++) {
    tft.fillRect(x + 5 + i * 6, y + 4, 4, 6, UNRAID_ORANGE);
    tft.drawFastVLine(x + 5 + i * 6, y + 16, 4, TFT_LIGHTGREY);
  }
}

void drawFanIcon(int x, int y) {
  int cx = x + 14, cy = y + 14;
  tft.drawRect(x, y, 28, 28, TFT_WHITE);
  tft.drawLine(cx, cy, cx + 2, cy - 11, TFT_WHITE);
  tft.drawLine(cx + 1, cy, cx + 11, cy + 5, TFT_WHITE);
  tft.drawLine(cx - 1, cy, cx - 10, cy + 6, TFT_WHITE);
  tft.drawLine(cx - 1, cy, cx + 4, cy - 9, TFT_WHITE);
  tft.drawLine(cx + 1, cy + 1, cx + 9, cy + 4, TFT_WHITE);
  tft.drawLine(cx - 1, cy + 1, cx - 8, cy + 5, TFT_WHITE);
  tft.fillCircle(cx, cy, 3, UNRAID_ORANGE);
}

void drawDashboard(JsonDocument& doc) {
  int cpu = doc["cpu"] | 0;
  int cpuTemp = doc["cpu_temp"] | 0;
  int docker = doc["docker"] | 0;

  int ramUsedMb = doc["memory"]["used"] | 0;
  int ramTotalMb = doc["memory"]["total"] | 1;
  int ramPercent = ramTotalMb > 0 ? (ramUsedMb * 100) / ramTotalMb : 0;

  float ramUsedGb = ramUsedMb / 1024.0f;
  float ramTotalGb = ramTotalMb / 1024.0f;

  const char* arrayStatus = doc["array"] | "UNKNOWN";
  const char* uptime = doc["uptime"] | "";

  tft.fillScreen(TFT_BLACK);

  // Header
  tft.fillRect(0, 0, 170, 30, UNRAID_ORANGE);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, UNRAID_ORANGE);
  tft.drawString("UNRAID", 8, 7);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, UNRAID_ORANGE);
  tft.drawString("ONLINE", 116, 10);

  // CPU
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("CPU", 8, 40);

  String cpuText = String(cpu) + "%";
  int cpuWidth = tft.textWidth(cpuText);
  tft.drawString(cpuText, 162 - cpuWidth, 40);

  drawBar(8, 62, 154, 10, cpu, statusColor(cpu, 65, 85));

  // RAM
  tft.drawString("RAM", 8, 82);

  String ramText = String(ramUsedGb, 1) + "/" + String(ramTotalGb, 0) + " GB";
  int ramWidth = tft.textWidth(ramText);
  tft.drawString(ramText, 162 - ramWidth, 82);

  drawBar(8, 104, 154, 10, ramPercent, statusColor(ramPercent, 70, 90));

  // System status section
  tft.drawFastHLine(8, 126, 154, TFT_DARKGREY);

  drawLabelValue("CPU TEMP", String(cpuTemp) + " C", 136, statusColor(cpuTemp, 60, 75));
  drawLabelValue("ARRAY", String(arrayStatus), 151,
                 String(arrayStatus) == "STARTED" ? TFT_GREEN : TFT_RED);
  drawLabelValue("DOCKER", String(docker), 166, TFT_CYAN);

  // HDD section
  tft.drawFastHLine(8, 184, 154, TFT_DARKGREY);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("DISKS", 8, 192);

  JsonObject hdds = doc["hdds"].as<JsonObject>();
  int y = 216;

  for (JsonPair disk : hdds) {
    if (y > 272) break;

    const char* name = disk.key().c_str();
    int temp = disk.value().as<int>();

    tft.setTextFont(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(name, 12, y);

    String tempText = String(temp) + " C";
    tft.setTextColor(statusColor(temp, 45, 55), TFT_BLACK);
    tft.drawString(tempText, 128, y);

    y += 14;
  }

  // Footer
  tft.fillRect(0, 292, 170, 28, TFT_DARKGREY);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("UPTIME", 8, 301);

  String uptimeText = compactUptime(uptime);
  int uptimeWidth = tft.textWidth(uptimeText);
  tft.drawString(uptimeText, 162 - uptimeWidth, 301);
}


void drawStorage(JsonDocument& doc) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 170, 30, UNRAID_ORANGE);
  tft.setTextFont(2); tft.setTextColor(TFT_WHITE, UNRAID_ORANGE); tft.drawString("SPEICHER", 8, 7);
  JsonObject storage = doc["storage"].as<JsonObject>();
  long long freeBytes = storage["free_bytes"] | (doc["free_bytes"] | 0LL);
  long long totalBytes = storage["total_bytes"] | (doc["total_bytes"] | 0LL);
  if (freeBytes <= 0) {
    tft.setTextFont(2); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("Keine Daten", 8, 140); return;
  }
  float freeTb = freeBytes / 1099511627776.0f;
  float totalTb = totalBytes > 0 ? totalBytes / 1099511627776.0f : 0;
  tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); tft.drawString("FREIER SPEICHER", 8, 52);
  tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String(freeTb, 1) + " TB", 8, 70);
  drawDiskIcon(128, 72);
  tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (totalTb > 0) tft.drawString(String("VON ") + String(totalTb, 1) + " TB GESAMT", 8, 112);
  if (totalBytes > 0) {
    int used = 100 - (int)((freeBytes * 100) / totalBytes);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(used) + "% BELEGT", 8, 136);
    drawBar(8, 154, 154, 14, used, statusColor(used, 70, 90));
  }
  long ramUsedMb = doc["memory"]["used"] | 0LL;
  long ramTotalMb = doc["memory"]["total"] | 0LL;
  if (ramTotalMb > 0) {
    int ramPercent = (int)((ramUsedMb * 100LL) / ramTotalMb);
    tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("RAM FREI", 8, 188);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String((ramTotalMb - ramUsedMb) / 1024.0f, 1) + " GB", 8, 200);
    drawRamIcon(128, 202);
    tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(String("VON ") + String(ramTotalMb / 1024.0f, 1) + " GB GESAMT", 8, 228);
    drawBar(8, 244, 154, 14, ramPercent, statusColor(ramPercent, 70, 90));
  }
}

void drawGpu(JsonDocument& doc) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 170, 30, UNRAID_ORANGE);
  tft.setTextFont(2); tft.setTextColor(TFT_WHITE, UNRAID_ORANGE); tft.drawString("GPU", 8, 7);
  JsonObject gpu = doc["gpu"].as<JsonObject>();
  const char* name = gpu["name"] | "Intel i915";
  tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK); tft.drawString(name, 8, 46);
  const char* labels[] = {"RENDER/3D", "VIDEO", "VIDEO ENHANCE"};
  const char* keys[] = {"render", "video", "video_enhance"};
  for (int i = 0; i < 3; i++) {
    int y = 82 + i * 52;
    int value = constrain((int)(gpu[keys[i]] | 0), 0, 100);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString(labels[i], 8, y);
    tft.drawString(String(value) + "%", 122, y);
    drawBar(8, y + 18, 154, 14, value, statusColor(value, 70, 90));
  }
}

void drawFans(JsonDocument& doc) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 170, 30, UNRAID_ORANGE);
  tft.setTextFont(2); tft.setTextColor(TFT_WHITE, UNRAID_ORANGE); tft.drawString("AIRFLOW", 8, 7);
  JsonArray fans = doc["fans"].as<JsonArray>();
  int y = 112;
  for (JsonObject fan : fans) {
    if (y > 260) break;
    int rpm = fan["rpm"] | 0;
    const char* label = fan["name"] | "LUEFTER";
    tft.setTextFont(1); tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int labelX = (170 - tft.textWidth(label)) / 2;
    tft.drawString(label, labelX, y);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String rpmText = String(rpm) + " RPM";
    int rpmX = (170 - tft.textWidth(rpmText)) / 2;
    tft.drawString(rpmText, rpmX, y + 14);
    y += 82;
  }
  if (!fans.size()) { tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("Keine Daten", 8, 120); }
}

void drawMessage(const char* title, const char* detail, uint16_t color) {
  if (coverMode) return;
  tft.fillScreen(TFT_BLACK);

  if (String(title) == "VERBINDUNG" || String(title) == "WLAN") {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    int cx = 85, cy = 150;
    tft.drawCircle(cx, cy + 18, 5, TFT_WHITE);
    tft.drawArc(cx, cy + 18, 24, 18, 130, 230, TFT_WHITE, TFT_BLACK);
    tft.drawArc(cx, cy + 18, 42, 36, 130, 230, TFT_WHITE, TFT_BLACK);
    tft.drawArc(cx, cy + 18, 60, 54, 130, 230, TFT_WHITE, TFT_BLACK);
    tft.drawLine(cx - 12, 224, cx + 12, 248, TFT_RED);
    tft.drawLine(cx + 12, 224, cx - 12, 248, TFT_RED);
    return;
  }

  tft.setTextFont(2);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(title, 10, 30);

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(detail, 10, 65);
}

// Network work runs separately so the hardware button and cover server stay responsive.
static SemaphoreHandle_t statusMutex;
static String latestPayload, latestError;
static bool statusDirty = false;
static void statusWorker(void*) {
  for (;;) {
    String payload, error;
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect(); error = "Keine WLAN-Verbindung";
    } else {
      HTTPClient http;
      http.setConnectTimeout(4000); http.setTimeout(5000); http.begin(API_URL);
      int code = http.GET();
      if (code == HTTP_CODE_OK) payload = http.getString();
      else error = "Status HTTP " + String(code);
      http.end();
    }
    xSemaphoreTake(statusMutex, portMAX_DELAY);
    latestPayload = payload; latestError = error; statusDirty = true;
    xSemaphoreGive(statusMutex);
    vTaskDelay(pdMS_TO_TICKS(REFRESH_INTERVAL_MS));
  }
}
void fetchAndDraw() {
  if (coverMode) return;
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  String payload = latestPayload, error = latestError;
  statusDirty = false;
  xSemaphoreGive(statusMutex);
  if (error.length()) { drawMessage("VERBINDUNG", error.c_str(), TFT_RED); return; }
  if (!payload.length()) { drawMessage("WLAN", "Verbinde...", TFT_YELLOW); return; }
  JsonDocument doc;
  auto result = deserializeJson(doc, payload);
  if (result) { drawMessage("JSON FEHLER", result.c_str(), TFT_RED); return; }
  if (displayPage == 1) { drawStorage(doc); return; }
  if (displayPage == 2) { drawGpu(doc); return; }
  if (displayPage == 3) { drawFans(doc); return; }
  drawDashboard(doc);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  Serial.printf("Display: %d x %d\n", tft.width(), tft.height());

  // Initialize the network stack before coverSetup() calls server.begin().
  // Starting WebServer before WiFi is initialized causes an lwIP mbox assert.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  statusMutex = xSemaphoreCreateMutex();
  coverSetup();
  xTaskCreate(statusWorker, "unraid-status", 8192, nullptr, 1, nullptr);
  fetchAndDraw();
}

void loop() {
  coverLoop();
  delay(2);
  if (millis() - lastRefresh >= REFRESH_INTERVAL_MS) {
    lastRefresh = millis();
    fetchAndDraw();
  }
}
