#  RFID Access Controller — Embedded + API

A small example that connects an **Arduino Nano** (RDM6300 RFID reader)
to an **ESP8266 gateway**, verifies tag UIDs via a **FastAPI backend**, and triggers an
**ESP-01S relay** for a short pulse (e.g., door unlock).

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Arduino%20%7C%20ESP8266-blue)
![Backend](https://img.shields.io/badge/backend-FastAPI-green)
![Dockerized](https://img.shields.io/badge/deployment-Docker-orange)
[![CI](https://github.com/meirg9/rfid-access-controller/actions/workflows/lint.yml/badge.svg)](https://github.com/meirg9/rfid-access-controller/actions)

---

##  Topology
- **arduino/Nano/Nano.ino** — ATmega328P: PWM breathing LED, ADC-based tick rate, INT0 Turbo button, UART to ESP8266.
- **arduino/ESP8266/ESP8266.ino** — NodeMCU/ESP8266: parses UID frames from Nano, calls FastAPI `/api/scan`, optionally calls ESP-01S relay.
- **arduino/ESP01S_Relay/ESP01S_Relay.ino** — ESP-01S: minimal HTTP server exposing `/relay_on`, `/relay_off`, `/relay_open`.
- **api/main.py** — FastAPI backend with SQLite (allowlist + scan history) and a minimal HTML dashboard.
- **api/public/dashboard.html** — shows the last 10 scans with a 1 Hz live poll.
- **api/Dockerfile**, **api/docker-compose.yml** — containerized backend.
- **config.example.h** — template to copy as `config.h` (excluded via `.gitignore`).

---

##  Quick Start (Backend)
```bash
cd api
docker compose up --build
# Open http://localhost:8000/  (dashboard)
# Add UID to allowlist:
curl -X POST -H "Content-Type: application/json"      -d '{"uid":"360046D804"}'      http://localhost:8000/api/allow
```

---

##  Firmware Build
1. Copy `config.example.h` → `config.h` and fill credentials.
2. Flash each sketch to its board:
   - `arduino/Nano/Nano.ino` → Arduino Nano/Uno (ATmega328P)
   - `arduino/ESP8266/ESP8266.ino` → NodeMCU/ESP8266
   - `arduino/ESP01S_Relay/ESP01S_Relay.ino` → ESP-01S
3. The ESP8266 expects UID frames from Nano:
   `STX=0x02` + 10 ASCII-HEX + `ETX=0x03`.

---

##  Security Notes
- **Never commit `config.h`** — it contains Wi-Fi credentials.
  Use `config.example.h` as a safe template.
- No authentication/HTTPS — use inside a trusted LAN or protect behind a reverse proxy (TLS, auth).

---

##  License
Released under the [MIT License](LICENSE).
