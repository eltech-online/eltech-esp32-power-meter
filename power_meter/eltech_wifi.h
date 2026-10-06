#pragma once

// eltech_wifi.h — the WiFi part that every ElTech-Online kit with a web page
// shares. It turns the board into its own WiFi network (an "Access Point", so
// no router or internet is needed) and holds the look of the web page.
//
// It is a separate file so the main sketch can stay about the kit's own
// lesson. A file ending in .h in the sketch folder shows up as a second tab in
// the Arduino IDE, and the line  #include "eltech_wifi.h"  in the main sketch
// pastes it in at that point.
//
// The main sketch must set these BEFORE the #include line:
//   AP_SSID, AP_PASSWORD, AP_OPEN_NETWORK   - the WiFi settings
//   KIT_SSID_PREFIX                         - e.g. "ElTech-ML-"
//   KIT_PREFS                               - name of this kit's flash "notebook"

#include <WiFi.h>
#include <Preferences.h>

// The network name/password actually in use, the page address, and whether the
// Access Point started. Every function in the sketch can read these.
String apSsid, apPassword, apUrl;
bool wifiOK = false;
String wifiError;

// Prefix + the last 4 hex digits of this board's MAC address. Every ESP32 has a
// different MAC, so two kits in the same room never clash.
String makeUniqueSsid() {
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char tail[5];
  snprintf(tail, sizeof(tail), "%02X%02X", mac[4], mac[5]);
  return String(KIT_SSID_PREFIX) + tail;
}

// Loads this board's saved password, or creates and saves a random one on first
// boot. Lowercase letters and digits only, minus look-alikes (i, l, o, 0, 1),
// so it's easy to read and to type on a phone. To get a new one, set
// Tools > "Erase All Flash Before Sketch Upload" to Enabled and upload once
// (that also clears the kit's other saved settings).
String loadOrCreatePassword() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  String password = prefs.getString("ap_password", "");
  if (password.length() < 8) {
    const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    password = "";
    for (int i = 0; i < 8; i++) {
      password += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    prefs.putString("ap_password", password);
  }
  prefs.end();
  return password;
}

// Starts the Access Point and returns true if it worked. On failure, wifiError
// says why.
bool startAccessPoint() {
  // Turning WiFi on first also powers up the radio, which esp_random() uses as a
  // source of true randomness for the generated password.
  WiFi.mode(WIFI_AP);

  apSsid = strlen(AP_SSID) ? String(AP_SSID) : makeUniqueSsid();
  if (AP_OPEN_NETWORK) {
    apPassword = "";
  } else {
    apPassword = strlen(AP_PASSWORD) ? String(AP_PASSWORD) : loadOrCreatePassword();
  }

  if (apSsid.length() > 32) {
    wifiError = "name over 32 chars";
    return false;
  }
  if (!AP_OPEN_NETWORK && (apPassword.length() < 8 || apPassword.length() > 63)) {
    wifiError = "pass not 8-63 chars";
    return false;
  }
  if (!WiFi.softAP(apSsid.c_str(), AP_OPEN_NETWORK ? NULL : apPassword.c_str())) {
    wifiError = "softAP() failed";
    return false;
  }
  // The ESP32-C3 SuperMini's tiny antenna can't handle full transmit power:
  // the signal gets so distorted that phones can't see or join the network.
  // Lowering it to 8.5 dBm fixes this and is still plenty for a room.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  apUrl = "http://" + WiFi.softAPIP().toString();
  return true;
}

// Prints the connection details to Serial Monitor.
void printWifiDetails() {
  Serial.println("--- WiFi Access Point ---");
  if (wifiOK) {
    Serial.print("SSID:     "); Serial.println(apSsid);
    Serial.print("Password: "); Serial.println(AP_OPEN_NETWORK ? "(open network)" : apPassword.c_str());
    Serial.print("URL:      "); Serial.println(apUrl);
  } else {
    Serial.print("FAILED to start: "); Serial.println(wifiError);
  }
}

// The style sheet (colours, spacing, fonts) for the web page. The page asks
// for it at /style.css. Keeping it here means page_template.h only holds what
// is special about this kit's page.
//
// R"CSS(...)CSS" is a C++ raw string literal: everything between the markers
// is taken literally, including quotes and new lines.
const char STYLE_CSS[] PROGMEM = R"CSS(
body { font-family: -apple-system, Arial, sans-serif; background:#0f172a; color:#e2e8f0;
       margin:0; padding:24px; text-align:center; }
h1 { font-size:20px; font-weight:600; margin:0 0 2px; }
.sub { color:#94a3b8; font-size:13px; margin-bottom:24px; }
.cards { display:flex; gap:12px; justify-content:center; flex-wrap:wrap; }
.card { background:#1e293b; border-radius:12px; padding:20px 24px; min-width:110px; }
.card .label { color:#94a3b8; font-size:12px; text-transform:uppercase; letter-spacing:0.05em; }
.card .value { font-size:32px; font-weight:700; margin-top:6px; }
.card .state { margin-top:8px; }
.panel { margin-top:24px; background:#1e293b; border-radius:12px; padding:16px 20px;
         text-align:left; max-width:420px; margin-left:auto; margin-right:auto; }
.panel .head { display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; }
.panel h2 { font-size:14px; margin:0; color:#e2e8f0; }
.row { display:flex; justify-content:space-between; align-items:center; gap:12px;
       padding:7px 0; border-top:1px solid #334155; font-size:13px; }
.row .name { color:#e2e8f0; }
.row .detail { color:#94a3b8; font-size:12px; margin-left:6px; }
.badge { font-size:11px; font-weight:700; letter-spacing:0.04em; padding:3px 8px;
         border-radius:999px; white-space:nowrap; }
.ok   { background:#14532d; color:#86efac; }
.fail { background:#7f1d1d; color:#fca5a5; }
.warn { background:#713f12; color:#fde68a; }
.hint { color:#cbd5e1; font-size:13px; line-height:1.6; margin:10px 0 0; padding-left:20px; }
.buttons { display:flex; gap:8px; flex-wrap:wrap; margin-top:12px; }
button { font:inherit; font-size:13px; font-weight:600; color:#0f172a; background:#7dd3fc;
         border:0; border-radius:8px; padding:9px 14px; }
button.quiet { background:#334155; color:#e2e8f0; }
button.on { background:#86efac; }
input[type=range] { width:100%; margin-top:10px; }
input[type=color] { width:100%; height:48px; border:0; padding:0; background:none; margin-top:10px; }
input[type=number] { font:inherit; font-size:13px; width:80px; padding:6px 8px; border-radius:8px;
                     border:1px solid #334155; background:#0f172a; color:#e2e8f0; }
.msg { color:#94a3b8; font-size:12px; margin-top:10px; min-height:15px; }
.panel a { color:#7dd3fc; }
.footer { margin-top:24px; color:#64748b; font-size:11px; }
.footer a { color:#94a3b8; }
)CSS";
