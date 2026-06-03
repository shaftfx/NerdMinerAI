#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "web_ui.h"
#include "ota_manager.h"
#include "drivers/storage/storage.h"
#include "drivers/storage/nvMemory.h"
#include "thermal.h"
#include "version.h"
#include "monitor.h"

// Mining globals (defined in stratum.cpp / mining.cpp)
extern uint32_t templates;
extern uint32_t Mhashes;
extern uint32_t totalKHashes;
extern uint32_t shares;
extern uint32_t valids;
extern double   best_diff;

// Monitor globals (defined in monitor.cpp)
extern unsigned int bitcoin_price;
extern String       current_block;
extern global_data  gData;

// Config (defined in wManager.cpp)
extern TSettings Settings;
extern nvMemory  nvMem;

static WebServer* webServer = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// HTML pages
// ─────────────────────────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NerdMinerAI</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:monospace;margin:0;padding:16px}
h1{color:#f7931a;margin:0 0 6px}
.sub{color:#8b949e;font-size:12px;margin-bottom:14px}
.nav{margin-bottom:14px}.nav a{color:#58a6ff;text-decoration:none;margin-right:14px;font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}
.card{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:10px}
.lbl{font-size:10px;color:#8b949e;text-transform:uppercase;letter-spacing:1px}
.val{font-size:17px;font-weight:bold;margin-top:4px;color:#58a6ff;word-break:break-all}
.orange{color:#f7931a}.green{color:#3fb950}.red{color:#f85149}
</style></head><body>
<div class="nav"><a href="/">&#9889; Dashboard</a><a href="/settings">&#9881; Settings</a></div>
<h1>NerdMinerAI</h1>
<div class="sub" id="sub">Loading...</div>
<div class="grid" id="g"></div>
<script>
const F=[
  ['hashrate','Hash Rate','orange'],
  ['shares','Shares','green'],
  ['valids','Valid Shares','green'],
  ['bestDiff','Best Diff','orange'],
  ['templates','Templates',''],
  ['uptime','Uptime',''],
  ['temp','Temperature',''],
  ['btcPrice','BTC Price','orange'],
  ['blockHeight','Block Height',''],
  ['globalHash','Net Hash',''],
  ['difficulty','Difficulty',''],
  ['halfHourFee','Fee 30m',''],
  ['poolDisplay','Pool',''],
  ['ip','IP Address','']
];
const g=document.getElementById('g');
F.forEach(([k,l,c])=>g.innerHTML+=`<div class="card"><div class="lbl">${l}</div><div class="val ${c}" id="${k}">-</div></div>`);
async function upd(){
  try{
    const d=await(await fetch('/api/stats')).json();
    F.forEach(([k])=>{const e=document.getElementById(k);if(e&&d[k]!=null)e.textContent=d[k];});
    document.getElementById('sub').textContent='Version '+d.version+' • Updated '+new Date().toLocaleTimeString();
  }catch(e){document.getElementById('sub').textContent='Connection error';}
}
upd();setInterval(upd,5000);
</script></body></html>
)rawhtml";

static const char SETTINGS_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NerdMinerAI Settings</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:monospace;margin:0;padding:16px}
h1{color:#f7931a;margin:0 0 6px}
.nav{margin-bottom:14px}.nav a{color:#58a6ff;text-decoration:none;margin-right:14px;font-size:13px}
.sec{color:#f7931a;font-size:12px;margin:16px 0 6px;text-transform:uppercase;letter-spacing:1px}
label{display:block;margin:10px 0 3px;font-size:11px;color:#8b949e}
input[type=text],input[type=number]{background:#161b22;border:1px solid #30363d;color:#e6edf3;padding:7px;border-radius:4px;width:100%;max-width:420px;box-sizing:border-box}
.chk{display:flex;align-items:center;gap:8px;margin:12px 0}
.chk label{margin:0;cursor:pointer}
.btn{background:#f7931a;color:#000;border:none;padding:9px 18px;border-radius:4px;cursor:pointer;margin-top:14px;font-weight:bold;margin-right:8px;font-family:monospace}
.btn-red{background:#da3633;color:#fff}
#msg{margin-top:10px;padding:8px;border-radius:4px;display:none;font-size:13px}
hr{border:none;border-top:1px solid #30363d;margin:14px 0}
</style></head><body>
<div class="nav"><a href="/">&#9889; Dashboard</a><a href="/settings">&#9881; Settings</a></div>
<h1>Settings</h1>
<form id="f">
<div class="sec">Mining Pool</div>
<label>Pool URL</label><input type="text" name="poolUrl" id="poolUrl" maxlength="80">
<label>Pool Port</label><input type="number" name="poolPort" id="poolPort" min="1" max="65535">
<label>Pool Password (optional)</label><input type="text" name="poolPass" id="poolPass" maxlength="80">
<label>BTC Wallet Address</label><input type="text" name="wallet" id="wallet" maxlength="80">
<label>Timezone (UTC offset, e.g. -5 or +2)</label><input type="number" name="timezone" id="timezone" min="-12" max="12">
<div class="sec">Fallback Pools</div>
<label>Fallback Pool 2 URL</label><input type="text" name="poolUrl2" id="poolUrl2" maxlength="80">
<label>Fallback Pool 2 Port</label><input type="number" name="poolPort2" id="poolPort2" min="1" max="65535">
<label>Fallback Pool 3 URL</label><input type="text" name="poolUrl3" id="poolUrl3" maxlength="80">
<label>Fallback Pool 3 Port</label><input type="number" name="poolPort3" id="poolPort3" min="1" max="65535">
<div class="sec">Display</div>
<label>Screen Brightness (0-255)</label><input type="number" name="brightness" id="brightness" min="0" max="255">
<div class="chk"><input type="checkbox" name="invertColors" id="invertColors"><label for="invertColors">Invert Display Colors</label></div>
<div class="sec">General</div>
<div class="chk"><input type="checkbox" name="saveStats" id="saveStats"><label for="saveStats">Save Mining Stats to Flash</label></div>
<hr>
<button type="submit" class="btn">Save Settings</button>
<button type="button" class="btn btn-red" onclick="resetWifi()">Reset WiFi Config</button>
</form>
<div id="msg"></div>
<script>
fetch('/api/stats').then(r=>r.json()).then(d=>{
  ['poolUrl','poolPort','poolPass','wallet','timezone','poolUrl2','poolPort2','poolUrl3','poolPort3','brightness'].forEach(k=>{
    const e=document.getElementById(k);if(e&&d[k]!=null)e.value=d[k];
  });
  if(d.saveStats!=null)document.getElementById('saveStats').checked=!!d.saveStats;
  if(d.invertColors!=null)document.getElementById('invertColors').checked=!!d.invertColors;
});
document.getElementById('f').addEventListener('submit',async e=>{
  e.preventDefault();
  const fd=new FormData(document.getElementById('f'));
  const p=new URLSearchParams();
  for(const[k,v]of fd.entries())p.append(k,v);
  ['saveStats','invertColors'].forEach(k=>{if(!fd.has(k))p.append(k,'0');});
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
  const d=await r.json();
  const m=document.getElementById('msg');
  m.style.display='block';m.style.background=d.ok?'#1f4428':'#3d1f1f';m.textContent=d.message;
  if(d.restart)setTimeout(()=>location.href='/',4000);
});
function resetWifi(){
  if(!confirm('Reset WiFi settings and restart?\nYou will need to reconnect to the NerdMinerAI access point.'))return;
  fetch('/api/reset-wifi',{method:'POST'}).then(()=>{alert('Device restarting. Connect to NerdMinerAI AP.');});
}
</script></body></html>
)rawhtml";

static const char OTA_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><title>OTA Update</title>
<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px}
h2{color:#f7931a}progress{width:100%;height:20px}
.btn{background:#f7931a;color:#fff;border:none;padding:10px 24px;border-radius:4px;cursor:pointer;font-size:1em}
</style></head><body>
<h2>Firmware Update</h2>
<form id="f" method="POST" action="/update" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin" required><br><br>
  <button class="btn" type="submit">Flash</button>
</form>
<progress id="p" value="0" max="100" style="display:none"></progress>
<p id="s"></p>
<script>
document.getElementById('f').onsubmit=function(e){
  e.preventDefault();
  var fd=new FormData(this);
  var x=new XMLHttpRequest();
  x.upload.onprogress=function(e){if(e.lengthComputable){
    var p=document.getElementById('p');p.style.display='';p.value=100*e.loaded/e.total;
  }};
  x.onload=function(){document.getElementById('s').textContent=x.responseText;};
  x.onerror=function(){document.getElementById('s').textContent='Upload failed';};
  x.open('POST','/update');x.send(fd);
};
</script>
</body></html>
)rawhtml";

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

static void handleDashboard() {
    webServer->send_P(200, "text/html", DASHBOARD_HTML);
}

static void handleSettings() {
    webServer->send_P(200, "text/html", SETTINGS_HTML);
}

static void handleStats() {
    // Uptime from millis()
    unsigned long secs = millis() / 1000;
    unsigned long h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    char uptime[24];
    snprintf(uptime, sizeof(uptime), "%luh %02lum %02lus", h, m, s);

    // Hash rate — lifetime average in H/s, formatted
    double hr_hs = secs > 0 ? (double)totalKHashes * 1000.0 / (double)secs : 0.0;
    char hashrate[24];
    if (hr_hs >= 1000000.0)
        snprintf(hashrate, sizeof(hashrate), "%.2f MH/s", hr_hs / 1000000.0);
    else if (hr_hs >= 1000.0)
        snprintf(hashrate, sizeof(hashrate), "%.2f KH/s", hr_hs / 1000.0);
    else
        snprintf(hashrate, sizeof(hashrate), "%.0f H/s", hr_hs);

    // Best difficulty
    char bestDiff[24];
    if (best_diff >= 1e12)
        snprintf(bestDiff, sizeof(bestDiff), "%.3fT", best_diff / 1e12);
    else if (best_diff >= 1e9)
        snprintf(bestDiff, sizeof(bestDiff), "%.3fG", best_diff / 1e9);
    else if (best_diff >= 1e6)
        snprintf(bestDiff, sizeof(bestDiff), "%.3fM", best_diff / 1e6);
    else
        snprintf(bestDiff, sizeof(bestDiff), "%.2f", best_diff);

    // Temperature
    float temp_c = thermal_read_celsius();
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_c);

    // BTC price
    char btcPrice[16];
    snprintf(btcPrice, sizeof(btcPrice), "$%u", bitcoin_price);

    // Fees
    char feeStr[20];
    snprintf(feeStr, sizeof(feeStr), "%d sat/vB", gData.halfHourFee);

    // Global hash rate string (already in EH/s from monitor.cpp)
    String globalHash = gData.globalHash.length() > 0 ? (gData.globalHash + " EH/s") : "-";

    // Pool display
    char poolDisplay[128];
    snprintf(poolDisplay, sizeof(poolDisplay), "%s:%d", Settings.PoolAddress.c_str(), Settings.PoolPort);

    DynamicJsonDocument doc(1536);

    // Dashboard display fields
    doc["hashrate"]    = hashrate;
    doc["shares"]      = shares;
    doc["valids"]      = valids;
    doc["bestDiff"]    = bestDiff;
    doc["templates"]   = templates;
    doc["uptime"]      = uptime;
    doc["temp"]        = temp_str;
    doc["btcPrice"]    = btcPrice;
    doc["blockHeight"] = current_block;
    doc["globalHash"]  = globalHash;
    doc["difficulty"]  = gData.difficulty.length() > 0 ? gData.difficulty : String("-");
    doc["halfHourFee"] = feeStr;
    doc["poolDisplay"] = poolDisplay;
    doc["ip"]          = WiFi.localIP().toString();
    doc["version"]     = CURRENT_VERSION;

    // Settings fields (for /settings page pre-fill)
    doc["poolUrl"]   = Settings.PoolAddress;
    doc["poolPort"]  = Settings.PoolPort;
    doc["poolPass"]  = String(Settings.PoolPassword);
    doc["wallet"]    = String(Settings.BtcWallet);
    doc["timezone"]  = Settings.Timezone;
    doc["saveStats"] = Settings.saveStats;
    doc["poolUrl2"]  = Settings.PoolAddress2;
    doc["poolPort2"] = Settings.PoolPort2;
    doc["poolUrl3"]  = Settings.PoolAddress3;
    doc["poolPort3"] = Settings.PoolPort3;
    doc["brightness"]    = Settings.Brightness;
    doc["invertColors"]  = Settings.invertColors;

    String json;
    serializeJson(doc, json);
    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->send(200, "application/json", json);
}

static void handleSaveSettings() {
    bool needRestart = false;

    if (webServer->hasArg("poolUrl"))
        Settings.PoolAddress = webServer->arg("poolUrl");
    if (webServer->hasArg("poolPort"))
        Settings.PoolPort = webServer->arg("poolPort").toInt();
    if (webServer->hasArg("poolPass"))
        strncpy(Settings.PoolPassword, webServer->arg("poolPass").c_str(), sizeof(Settings.PoolPassword) - 1);
    if (webServer->hasArg("wallet"))
        strncpy(Settings.BtcWallet, webServer->arg("wallet").c_str(), sizeof(Settings.BtcWallet) - 1);
    if (webServer->hasArg("timezone"))
        Settings.Timezone = webServer->arg("timezone").toInt();
    if (webServer->hasArg("saveStats"))
        Settings.saveStats = (webServer->arg("saveStats") != "0");
    if (webServer->hasArg("poolUrl2"))
        Settings.PoolAddress2 = webServer->arg("poolUrl2");
    if (webServer->hasArg("poolPort2"))
        Settings.PoolPort2 = webServer->arg("poolPort2").toInt();
    if (webServer->hasArg("poolUrl3"))
        Settings.PoolAddress3 = webServer->arg("poolUrl3");
    if (webServer->hasArg("poolPort3"))
        Settings.PoolPort3 = webServer->arg("poolPort3").toInt();

    #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
    if (webServer->hasArg("brightness")) {
        int b = constrain(webServer->arg("brightness").toInt(), 0, 255);
        if (b != Settings.Brightness) { Settings.Brightness = b; needRestart = true; }
    }
    if (webServer->hasArg("invertColors")) {
        bool inv = (webServer->arg("invertColors") != "0");
        if (inv != Settings.invertColors) { Settings.invertColors = inv; needRestart = true; }
    }
    #endif

    nvMem.saveConfig(&Settings);

    String resp = needRestart
        ? "{\"ok\":true,\"restart\":true,\"message\":\"Settings saved. Restarting in 4s...\"}"
        : "{\"ok\":true,\"restart\":false,\"message\":\"Settings saved.\"}";

    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->send(200, "application/json", resp);

    if (needRestart) {
        delay(500);
        ESP.restart();
    }
}

static void handleResetWifi() {
    webServer->send(200, "application/json", "{\"ok\":true}");
    delay(300);
    extern void reset_configuration();
    reset_configuration();
}

static void handleRestart() {
    webServer->send(200, "application/json", "{\"ok\":true}");
    webServer->client().stop();
    delay(300);
    ESP.restart();
}

static void handleNotFound() {
    webServer->send(404, "text/plain", "Not found");
}

static void handleOTAPage() {
    webServer->send_P(200, "text/html", OTA_HTML);
}

static void handleOTAUpload() {
    HTTPUpload& upload = webServer->upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (g_ota_in_progress) return;
        Serial.printf("[OTA] Web flash: %s\n", upload.filename.c_str());
        g_ota_in_progress = true;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
            g_ota_in_progress = false;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            g_ota_in_progress = false;
        }
    }
}

static void handleOTAComplete() {
    if (Update.hasError()) {
        String err = Update.errorString();
        Serial.printf("[OTA] Web flash FAILED: %s\n", err.c_str());
        webServer->send(500, "text/plain", "OTA FAILED: " + err);
        g_ota_in_progress = false;
    } else {
        Serial.println("[OTA] Web flash OK — rebooting");
        webServer->send(200, "text/plain", "OK - rebooting");
        webServer->client().stop();
        delay(500);
        ESP.restart();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void webUI_init() {
    webServer = new WebServer(8080);
    webServer->on("/",            HTTP_GET,  handleDashboard);
    webServer->on("/settings",    HTTP_GET,  handleSettings);
    webServer->on("/api/stats",   HTTP_GET,  handleStats);
    webServer->on("/api/settings",HTTP_POST, handleSaveSettings);
    webServer->on("/api/reset-wifi", HTTP_POST, handleResetWifi);
    webServer->on("/api/restart",    HTTP_POST, handleRestart);
    webServer->on("/update",      HTTP_GET,  handleOTAPage);
    webServer->on("/update",      HTTP_POST, handleOTAComplete, handleOTAUpload);
    webServer->onNotFound(handleNotFound);
    webServer->begin();
    Serial.println("Web UI started on port 8080");
}

void webUI_process() {
    webServer->handleClient();
}
