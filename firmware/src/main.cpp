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

  tft.drawRoundRect(x, y, width, height, 3, TFT_DARKGREY);

  int fillWidth = ((width - 4) * percent) / 100;
  if (fillWidth > 0) {
    tft.fillRoundRect(x + 2, y + 2, fillWidth, height - 4, 2, color);
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

void drawMessage(const char* title, const char* detail, uint16_t color) {
  if (coverMode) return;
  tft.fillScreen(TFT_BLACK);

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
