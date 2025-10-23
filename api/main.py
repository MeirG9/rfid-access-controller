# =============================================================================
# File: main.py — FastAPI access-control backend (SQLite, no ORM)
# Purpose:
#   - Maintain an allowlist of UIDs (RDM6300 tag IDs, 10 ASCII-HEX chars).
#   - Accept scan events from the ESP8266 and return {"ok": true|false}.
#   - Persist scans to SQLite with Unix timestamp for auditing/history.
#   - Serve a simple HTML dashboard (static file) at "/".
#
# Storage:
#   - SQLite database at data/db.sqlite3 (created on first run).
#   - Tables:
#       allowed(uid TEXT PRIMARY KEY)
#       scans(ts INTEGER, uid TEXT, ok INTEGER)
#     Notes:
#       * ok is stored as 0/1 (INTEGER). ts is Unix epoch seconds.
#       * Minimal schema; no foreign keys required for this use case.
#
# Concurrency:
#   - sqlite3 connect(check_same_thread=False) to allow access from FastAPI
#     worker threads. A coarse threading.Lock (_lock) protects multi-statement
#     mutation in _insert_scan (insert + cache update).
#
# API:
#   POST /api/allow  — body: {"uid":"XXXXXXXXXX"} → insert into allowlist.
#   POST /api/scan   — body: {"uid":"XXXXXXXXXX"} → check allowlist; log scan.
#   GET  /api/recent — query: ?limit=N (default 10) → recent scans (JSON).
#   GET  /            — serves public/dashboard.html (static).
#
# Input Validation:
#   - UIDs are normalized to uppercase ASCII and truncated to 10 chars.
#   - /api/allow enforces minimum length 10 (hex chars).
#
# Operational Notes:
#   - No authentication/authorization; deploy behind a trusted network/reverse
#     proxy if needed. Consider adding rate-limit/auth for production.
#   - No migrations: schema is created idempotently on startup.
#   - For high write rates, consider batching or WAL mode; out of scope here.
# =============================================================================

from fastapi import FastAPI
from fastapi.responses import HTMLResponse, JSONResponse, FileResponse
from typing import Dict, Any, List
import sqlite3
import time
import threading
import os

# --- DB (SQLite) ---
db = sqlite3.connect("data/db.sqlite3", check_same_thread=False)
db.execute("CREATE TABLE IF NOT EXISTS allowed(uid TEXT PRIMARY KEY)")
db.execute(
    "CREATE TABLE IF NOT EXISTS scans("
    "ts INTEGER, uid TEXT, ok INTEGER)"
)
db.commit()

app = FastAPI()
_last: Dict[str, Any] = {"ts": 0, "uid": "", "ok": 0}  # in-memory last-scan cache
_lock = threading.Lock()  # protects DB write + _last update in _insert_scan()

def _insert_scan(uid: str, ok: int) -> None:
    """Append scan row; update last-scan cache (for potential live/SSE)."""
    t = int(time.time())  # Unix epoch seconds
    with _lock:
        db.execute(
            "INSERT INTO scans(ts,uid,ok) VALUES(?,?,?)",
            (t, uid, int(ok)),
        )
        db.commit()
        _last.update({"ts": t, "uid": uid, "ok": int(ok)})

# --- API: allowlist management ---
@app.post("/api/allow")
async def add_allow(item: Dict[str, Any]):
    # Normalize UID: strip spaces, uppercase, check length, then truncate to 10.
    uid = str(item.get("uid", "")).strip().upper()
    if len(uid) < 10:
        return JSONResponse(
            {"error": "uid must be at least 10 hex chars"},
            status_code=400
        )
    uid = uid[:10]
    db.execute("INSERT OR IGNORE INTO allowed(uid) VALUES(?)", (uid,))
    db.commit()
    return {"ok": True, "stored_uid": uid}

# --- API: scan from ESP (accepts uid only) ---
@app.post("/api/scan")
async def scan(item: Dict[str, Any]):
    # Normalize and bound UID to 10 chars; missing key yields empty string.
    uid = str(item.get("uid", "")).strip().upper()[:10]
    # Membership check: returns a row if allowed; None otherwise.
    ok = (1 if db.execute("SELECT 1 FROM allowed WHERE uid=?", (uid,))
          .fetchone() else 0)
    _insert_scan(uid, ok)  # persist audit trail + update in-memory cache
    return {"ok": bool(ok)}  # JSON-friendly boolean

# --- API: recent scans (default 10) ---
@app.get("/api/recent")
async def recent(limit: int = 10) -> List[Dict[str, Any]]:
    # Simple DESC indexless query; adequate for small volumes.
    rows = db.execute(
        "SELECT ts,uid,ok FROM scans ORDER BY ts DESC LIMIT ?",
        (limit,),
    ).fetchall()
    return [{"ts": ts, "uid": uid, "ok": bool(ok)} for ts, uid, ok in rows]

# --- Serve the static dashboard ---
@app.get("/", response_class=HTMLResponse)
async def dashboard():
    # Serve /public/dashboard.html relative to this file’s directory.
    path = os.path.join(os.path.dirname(__file__), "public", "dashboard.html")
    return FileResponse(path)
