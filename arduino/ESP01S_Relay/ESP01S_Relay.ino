/* ============================================================================
 * File: ESP01S_Relay.ino — Minimal HTTP relay (ESP-01S)
 * Endpoints:
 *   GET /relay_on   → energize relay (LOW, active‑low module)
 *   GET /relay_off  → release relay (HIGH)
 *   GET /relay_open → pulse ON for PULSE_MS then OFF
 * Security: no auth/HTTPS; restrict to trusted LAN.
 * ========================================================================== */
// ESP01S_Relay_HTTP.ino — Minimal HTTP relay (ESP-01S)
// Endpoints: /relay_open (pulse), /relay_on, /relay_off
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "../config.h"  // WIFI_SSID, WIFI_PASS

constexpr uint8_t  RELAY_PIN = 0;   // GPIO0
constexpr uint16_t PULSE_MS  = 800; // pulse length (ms)

inline void relay_on()  { digitalWrite(RELAY_PIN, LOW);  }  // active-low modules
inline void relay_off() { digitalWrite(RELAY_PIN, HIGH); }

ESP8266WebServer server(80);
static void send_ok() { server.send(200, "text/plain", "OK\n"); }

static void handle_relay_on()   { relay_on();  send_ok(); }
static void handle_relay_off()  { relay_off(); send_ok(); }
static void handle_relay_open() { relay_on(); delay(PULSE_MS); relay_off(); send_ok(); }
static void handle_not_found()  { server.send(404, "text/plain", "404\n"); }

void setup() {
  pinMode(RELAY_PIN, OUTPUT); relay_off();
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 8000) { delay(100); }
  server.on("/relay_on",   HTTP_GET, handle_relay_on);
  server.on("/relay_off",  HTTP_GET, handle_relay_off);
  server.on("/relay_open", HTTP_GET, handle_relay_open);
  server.onNotFound(handle_not_found);
  server.begin();
}

void loop() { server.handleClient(); }
