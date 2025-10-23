// config.example.h — copy to config.h and fill your real values
#pragma once

// --- Wi‑Fi credentials ---
#define WIFI_SSID   "<YOUR_SSID>"
#define WIFI_PASS   "<YOUR_PASSWORD>"

// --- Backend API endpoint ---
#define API_URL     "http://<BACKEND_IP>:8000/api/scan"

// --- Relay host (ESP‑01S micro‑relay) ---
#define RELAY_HOST  "<RELAY_IP>"
#define RELAY_PATH  "/relay_open"
