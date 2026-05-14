// WifiExport.h — WiFi AP hotspot + HTTP server for log download and delete.
//
// Starts "TurboESP32" access point and serves LittleFS log files at
// http://192.168.4.1. BLE OBD2 state machine is paused while active
// (the main loop returns early when menuState == MENU_EXPORT).
// Active in real and DEMO builds — not SIMULATION.
//
// Routes:
//   GET  /            — HTML file listing with Download and Delete links
//   GET  /file?n=name — stream file as text/csv download
//   POST /delete?n=name — delete file, redirect back to /

#if !defined(SIMULATION)

#include <WiFi.h>
#include <WebServer.h>

#define WIFI_AP_SSID  "TurboESP32"
#define WIFI_AP_PASS  "turbo1234"

static WebServer wifiServer(80);
static bool      wifiActive = false;

static void serveRoot() {
  if (!LittleFS.begin(true)) { wifiServer.send(500, "text/plain", "FS error"); return; }

  String html =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<title>Turbo Logs</title>"
    "<style>"
      "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 12px}"
      "h2{margin-bottom:4px}"
      "table{width:100%;border-collapse:collapse}"
      "td{padding:6px 8px;border-bottom:1px solid #ddd}"
      ".sz{color:#888;font-size:.85em}"
      "a.dl{text-decoration:none;color:#0070f3}"
      "button.del{background:#e33;color:#fff;border:none;border-radius:4px;"
                  "padding:4px 10px;cursor:pointer}"
    "</style></head><body>"
    "<h2>Turbo OBD2 Logs</h2>";

  File root = LittleFS.open("/");
  File f    = root.openNextFile();
  bool any  = false;

  html += "<table>";
  while (f) {
    if (!f.isDirectory()) {
      any         = true;
      String name = f.name();
      String sz   = String(f.size()) + " B";
      html += "<tr>"
              "<td><a class='dl' href='/file?n=" + name + "' download>" + name + "</a>"
              " <span class='sz'>(" + sz + ")</span></td>"
              "<td style='width:80px'>"
                "<form method='POST' action='/delete' style='margin:0'>"
                  "<input type='hidden' name='n' value='" + name + "'>"
                  "<button class='del' type='submit' "
                    "onclick=\"return confirm('Delete " + name + "?')\">Delete</button>"
                "</form>"
              "</td>"
              "</tr>";
    }
    f = root.openNextFile();
  }
  html += "</table>";

  if (!any) html += "<p><em>No logs recorded yet.</em></p>";
  html += "</body></html>";
  wifiServer.send(200, "text/html", html);
}

static void serveFile() {
  String name = wifiServer.arg("n");
  if (!name.startsWith("/")) name = "/" + name;
  if (!LittleFS.exists(name)) { wifiServer.send(404, "text/plain", "Not found"); return; }
  File f = LittleFS.open(name, "r");
  // Force browser download with the original filename
  wifiServer.sendHeader("Content-Disposition",
    "attachment; filename=\"" + name.substring(1) + "\"");
  wifiServer.streamFile(f, "text/csv");
  f.close();
}

static void handleDelete() {
  String name = wifiServer.arg("n");
  if (!name.startsWith("/")) name = "/" + name;
  if (LittleFS.exists(name)) LittleFS.remove(name);
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(303);
}

void startWifiExport() {
  mountFs();  // ensure LittleFS is mounted (mountFs defined in Recorder.h)
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  wifiServer.on("/",       serveRoot);
  wifiServer.on("/file",   serveFile);
  wifiServer.on("/delete", HTTP_POST, handleDelete);
  wifiServer.begin();
  wifiActive = true;
}

void stopWifiExport() {
  wifiServer.stop();
  WiFi.softAPdisconnect(true);
  wifiActive = false;
}

void tickWifiExport() {
  if (wifiActive) wifiServer.handleClient();
}

void drawExportScreen(uint32_t now) {
  static uint32_t lastExportDrawMs = 0;
  if (lastExportDrawMs != 0 && now - lastExportDrawMs < 2000) return;
  lastExportDrawMs = now;

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(12, 11, "WiFi EXPORT");
  display.drawHLine(0, 13, 128);
  display.drawStr(0, 27, "SSID: " WIFI_AP_SSID);
  display.drawStr(0, 40, "Pass: " WIFI_AP_PASS);
  display.drawStr(0, 53, "http://192.168.4.1");
  display.drawStr(0, 63, "[Click] Stop");
  display.sendBuffer();
}

#endif // !SIMULATION
