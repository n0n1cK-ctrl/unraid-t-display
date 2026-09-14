#include "cover.h"
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
using fs::File;
extern TFT_eSPI tft;
extern const char* COVER_TOKEN;
extern uint32_t lastRefresh;
bool coverMode = false;
static WebServer server(80);
static Preferences prefs;
static bool storageOK = false, uploadOK = false;
static File upload;
static size_t received;
static String slot, wantedHash;

static constexpr size_t IMAGE_BYTES = 170 * 320 * 2;
static String current, coverTitle, coverEpisode;
static bool authorized() { return server.header("Authorization") == String("Bearer ") + COVER_TOKEN; }
static String hashFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f || f.size() != IMAGE_BYTES) return "";
  mbedtls_sha256_context ctx; mbedtls_sha256_init(&ctx); mbedtls_sha256_starts_ret(&ctx, 0);
  uint8_t bytes[1024], digest[32];
  while (f.available()) { size_t n = f.read(bytes, sizeof(bytes)); if (!n) { f.close(); mbedtls_sha256_free(&ctx); return ""; } mbedtls_sha256_update_ret(&ctx, bytes, n); }
  mbedtls_sha256_finish_ret(&ctx, digest); mbedtls_sha256_free(&ctx);
  char hex[65]; for (int i=0;i<32;i++) sprintf(hex+i*2,"%02x",digest[i]);
  return String(hex);
}
static void drawCover() {
  tft.fillScreen(TFT_BLACK);
  File f = storageOK ? LittleFS.open(current, "r") : File();
  if (!f || f.size() != IMAGE_BYTES) {
    tft.setTextFont(2); tft.setTextSize(1); tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.drawString(storageOK ? "Noch kein Cover" : "Speicherfehler", 8, 140); return;
  }
  uint16_t row[170];
  // Wire format is little-endian RGB565, matching the ESP32's uint16_t.
  tft.setSwapBytes(true);
  for (int y=0;y<320;y++) { if(f.read((uint8_t*)row,sizeof(row)) != sizeof(row)) break; tft.pushImage(0,y,170,1,row); }
  tft.setSwapBytes(false);
  if (coverTitle.length()) {
    tft.fillRect(0, 288, 170, 32, TFT_DARKGREY);
    tft.setTextFont(1); tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    String line = coverTitle;
    if (coverEpisode.length()) line += " " + coverEpisode;
    while (line.length() && tft.textWidth(line) > 162) line.remove(line.length()-1);
    tft.drawString(line, 4, 292);
  }
}
void coverSetup() {
  pinMode(14, INPUT_PULLUP);
  storageOK = prefs.begin("cover", false);
  coverMode = prefs.getBool("mode", false);
  // Never format automatically: a mount error must not erase the saved image.
  bool mounted = LittleFS.begin(false);
  // First-time initialization is an explicit five-second hold of GPIO14 at startup.
  if (!mounted && !prefs.isKey("commit") && digitalRead(14) == LOW) {
    uint32_t start = millis();
    while (digitalRead(14) == LOW && millis()-start < 5000) delay(10);
    if (digitalRead(14) == LOW) { LittleFS.format(); mounted=LittleFS.begin(false); }
  }
  // A blank/corrupt first-time filesystem is safe to format only while the
  // user holds GPIO14 during startup. Otherwise keep the dashboard usable.
  storageOK = mounted && storageOK;
  current = prefs.getString("slot", "");
  coverTitle = prefs.getString("title", "");
  coverEpisode = prefs.getString("episode", "");
  if (storageOK && hashFile(current) != prefs.getString("hash", "invalid")) current = "";
  if (coverMode) drawCover();
  const char* headers[] = {"Authorization"}; server.collectHeaders(headers, 1);
  server.on("/info", HTTP_GET, []() {
    if (!authorized()) { server.send(401); return; }
    String result = "{\"width\":170,\"height\":320,\"format\":\"rgb565le\",\"storage\":";
    result += storageOK ? "true" : "false";
    result += ",\"sha256\":\"" + prefs.getString("hash", "") + "\"}";
    server.send(200,"application/json",result);
  });
  server.on("/cover", HTTP_POST, []() {
    if (!authorized()) { server.send(401); return; }
    server.send(uploadOK ? 200 : 400,"text/plain", uploadOK ? "saved" : "Upload or storage failure");
    if (uploadOK && coverMode) drawCover();
    uploadOK = false;
  }, []() {
    HTTPUpload& u=server.upload();
    if(u.status==UPLOAD_FILE_START) {
      uploadOK=false; received=0; if(upload) upload.close();
      if(!authorized() || !storageOK) return;
      wantedHash=server.arg("sha256");
      if(wantedHash.length()!=64) return;
      for(unsigned i=0;i<64;i++) if(!isxdigit(wantedHash[i])) return;
      wantedHash.toLowerCase();
      slot=current=="/cover-a.raw" ? "/cover-b.raw" : "/cover-a.raw";
      upload=LittleFS.open(slot,"w");
    } else if(u.status==UPLOAD_FILE_WRITE) {
      if(!upload) return;
      received+=u.currentSize;
      if(received>IMAGE_BYTES || upload.write(u.buf,u.currentSize)!=u.currentSize) { upload.close(); received=IMAGE_BYTES+1; }
    } else if(u.status==UPLOAD_FILE_END) {
      if(!upload) return;
      upload.flush(); upload.close();
      if(received!=IMAGE_BYTES || hashFile(slot)!=wantedHash) return;
      // Store slot+digest in a single NVS value so power loss cannot split the commit.
      String commit=slot+"|"+wantedHash;
      if(prefs.putString("commit",commit)!=commit.length()) return;
      current=slot;
      coverTitle=server.arg("title");
      String season=server.arg("season"), episode=server.arg("episode");
      coverEpisode = (season.length() && episode.length()) ? String("S") + (season.toInt()<10?"0":"") + season + " E" + (episode.toInt()<10?"0":"") + episode : "";
      prefs.putString("title",coverTitle); prefs.putString("episode",coverEpisode);
      uploadOK=true;
      prefs.putString("slot",slot); prefs.putString("hash",wantedHash);
    } else if(u.status==UPLOAD_FILE_ABORTED) { if(upload) upload.close(); uploadOK=false; }
  });
  // Recover the authoritative atomic commit (legacy keys are only /info mirrors).
  String commit=prefs.getString("commit", ""); int sep=commit.indexOf('|');
  if(storageOK && sep>0) {
    String path=commit.substring(0,sep), digest=commit.substring(sep+1);
    if(hashFile(path)==digest) {current=path; prefs.putString("hash",digest);}
  }
  if(coverMode) drawCover();
  server.begin();
}
void coverLoop() {
  server.handleClient();
  static bool raw=true, stable=true; static uint32_t changed=0;
  bool now=digitalRead(14);
  if(now!=raw) {raw=now;changed=millis();}
  if(now!=stable && millis()-changed>=35) {
    stable=now;
    if(!stable) {
      coverMode=!coverMode; prefs.putBool("mode",coverMode);
      if(coverMode) drawCover(); else lastRefresh=millis()-5000;
    }
  }
}
