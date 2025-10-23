/* ============================================================================
 * File: ESP8266.ino — NodeMCU (ESP8266) API client and bridge
 * Purpose:
 *   - Parse UID frames (STX + 10 ASCII-HEX + ETX) from Nano over HW UART (swapped).
 *   - POST to FastAPI `/api/scan` and stream back a single byte to Nano: 0x01/0x00.
 *   - On success, best-effort trigger ESP-01S relay via HTTP GET.
 * Networking:
 *   - Wi-Fi credentials and endpoints are provided by ../config.h
 * Robustness:
 *   - One-shot Wi-Fi connect on boot and on demand (non-blocking long loops).
 *   - De-dup cache to debounce repeated scans within a 3s window.
 * ========================================================================== */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <cstring>
#include <cstdio>
#include "../config.h"  // WIFI_SSID, WIFI_PASS, API_URL, RELAY_HOST, RELAY_PATH

constexpr uint16_t WIFI_TMO_MS      = 6000;
constexpr uint16_t HTTP_TMO_MS      = 1200;
constexpr unsigned long DEBOUNCE_MS = 3000;

enum S: uint8_t { WAIT_STX, READ_UID };
static S st = WAIT_STX;
static char uid[10];
static uint8_t ulen = 0;
static bool pend = false;
static char p_uid[10];

static char last_uid[10] = {0};
static unsigned long last_uid_ts = 0;
static bool last_ok = false;

// One-shot connect (used on boot and by ensure_wifi())
static void wifi_once() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TMO_MS) delay(100);
}

// Best-effort: if disconnected → try to (re)connect once, non-blocking-long
static inline void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  wifi_once();
}

static void relay_get() {
  ensure_wifi();
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient c; HTTPClient h; char url[96];
  // Build http://<host><path>
  snprintf(url, sizeof(url), "http://%s%s", RELAY_HOST, RELAY_PATH);
  if (h.begin(c, url)) { h.setTimeout(HTTP_TMO_MS); (void)h.GET(); h.end(); }
}

static bool api_check(const char u10[10]) {
  ensure_wifi();
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient c; HTTPClient h;
  if (!h.begin(c, API_URL)) return false;
  h.addHeader("Content-Type", "application/json");

  // Body: {"uid":"XXXXXXXXXX"}
  char jbuf[64];
  int n = snprintf(jbuf, sizeof(jbuf), "{\"uid\":\"");
  if (n <= 0) { h.end(); return false; }
  if ((size_t)(n + 10 + 2) >= sizeof(jbuf)) { h.end(); return false; } // 10 chars + "\"}"
  memcpy(jbuf + n, u10, 10); n += 10;
  n += snprintf(jbuf + n, sizeof(jbuf) - n, "\"}");

  h.setTimeout(HTTP_TMO_MS);
  int code = h.POST((uint8_t*)jbuf, (size_t)n);

  // Stream-scan for `"ok":true` without heap allocations
  WiFiClient* s = h.getStreamPtr();
  bool ok = false; uint8_t i = 0;
  static const char* P = "\"ok\":true";
  while (s->connected() && s->available()) {
    char ch = (char)s->read();
    if (ch == P[i]) { i++; if (i == 9) { ok = true; break; } }
    else i = (ch == P[0]) ? 1 : 0;
  }
  h.end();
  return (code == 200 && ok);
}

void setup() {
  Serial.begin(9600);
  Serial.swap();  // RX=D7(GPIO13), TX=D8(GPIO15)
  wifi_once();    // initial connect
}

void loop() {
  // Parse UID frames from Nano
  while (Serial.available()) {
    int v = Serial.read(); if (v < 0) break; uint8_t c = (uint8_t)v;
    if (st == WAIT_STX) { if (c == 0x02) { ulen = 0; st = READ_UID; } continue; }
    if (c == 0x03) {
      if (ulen == 10) { memcpy(p_uid, uid, 10); pend = true; }
      st = WAIT_STX; ulen = 0; continue;
    }
    const bool is_hex = (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');
    if (st == READ_UID && ulen < 10 && is_hex) { if (c>='a'&&c<='f') c -= ('a'-'A'); uid[ulen++] = (char)c; }
  }

  if (pend) {
    const unsigned long now = millis();
    // de-dup cache: same UID within window → reuse last result
    if (memcmp(p_uid, last_uid, 10) == 0 && (now - last_uid_ts < DEBOUNCE_MS)) {
      Serial.write(last_ok ? (uint8_t)0x01 : (uint8_t)0x00);
      pend = false; return;
    }
    const bool ok = api_check(p_uid);
    Serial.write(ok ? (uint8_t)0x01 : (uint8_t)0x00);
    if (ok) relay_get();
    memcpy(last_uid, p_uid, 10); last_uid_ts = now; last_ok = ok;
    pend = false;
  }
}
