#!/usr/bin/env python3
"""
DairyLedger — Local Dashboard Preview Server

Serves the gateway dashboard with mock API data so you can preview and
iterate on the frontend without any ESP32 hardware.

Usage:
    python3 tools/mock_server.py

Then open http://localhost:8080 in your browser.
"""

import json
import math
import os
import random
import time
from datetime import datetime, timedelta, timezone
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

PORT = 8080
DATA_DIR = os.path.join(os.path.dirname(__file__),
                        '..', 'firmware', 'gateway', 'data')

# ─── Mock Data ────────────────────────────────────────────────────────

START_TIME = time.time()

MOCK_NODES = [
    {
        "id": "K3VP8N",
        "label": "Main Fridge",
        "online": True,
        "probe_count": 3,
        "sd_status": 1,
        "pending_sync": 0,
        "mac": "AA:BB:CC:11:22:33",
        "thresholds": {
            "warn_high": 3.3,
            "crit_high": 5.0,
            "warn_low": -2.2,
            "crit_low": -3.9
        },
        "base_temps": [1.8, 2.1, 2.4],
        "probe_labels": ["Top Shelf", "Middle Shelf", "Bottom Shelf"],
    },
    {
        "id": "T7WM3R",
        "label": "Cheese Cave",
        "online": True,
        "probe_count": 2,
        "sd_status": 1,
        "pending_sync": 3,
        "mac": "AA:BB:CC:44:55:66",
        "thresholds": {
            "warn_high": 3.3,
            "crit_high": 5.0,
            "warn_low": -2.2,
            "crit_low": -3.9
        },
        "base_temps": [2.5, 2.8],
        "probe_labels": ["Upper Rack", "Lower Rack"],
    },
    {
        "id": "N5HF2C",
        "label": "Yogurt Fridge",
        "online": True,
        "probe_count": 2,
        "sd_status": 1,
        "pending_sync": 0,
        "mac": "AA:BB:CC:77:88:99",
        "thresholds": {
            "warn_high": 3.3,
            "crit_high": 5.0,
            "warn_low": -2.2,
            "crit_low": -3.9
        },
        "base_temps": [1.5, 1.9],
        "probe_labels": ["Door Side", "Back Wall"],
    },
    {
        "id": "B8QR4X",
        "label": "Basement Fridge 1",
        "online": True,
        "probe_count": 2,
        "sd_status": 2,
        "pending_sync": 12,
        "mac": "AA:BB:CC:AA:BB:CC",
        "thresholds": {
            "warn_high": 3.3,
            "crit_high": 5.0,
            "warn_low": -2.2,
            "crit_low": -3.9
        },
        "base_temps": [3.1, 3.5],  # Running warm — near warning
        "probe_labels": ["Probe 1", "Probe 2"],
    },
    {
        "id": "G4JK9Y",
        "label": "Basement Fridge 2",
        "online": False,  # Simulates an offline node
        "probe_count": 2,
        "sd_status": 3,
        "pending_sync": 47,
        "mac": "AA:BB:CC:DD:EE:FF",
        "thresholds": {
            "warn_high": 3.3,
            "crit_high": 5.0,
            "warn_low": -2.2,
            "crit_low": -3.9
        },
        "base_temps": [4.2, 4.8],  # Was running hot before going offline
        "probe_labels": ["Probe 1", "Probe 2"],
    },
]


def get_live_temps(node):
    """Generate slowly-drifting realistic temperatures."""
    now = time.time()
    temps = []
    for i, base in enumerate(node["base_temps"]):
        # Slow sine drift + small random noise
        drift = 0.3 * math.sin(now / 600 + i * 1.5)
        noise = random.gauss(0, 0.05)
        temps.append(round(base + drift + noise, 2))
    return temps


def generate_csv_data(node, date_str):
    """Generate a day's worth of fake CSV data for a node."""
    try:
        date = datetime.strptime(date_str, "%Y-%m-%d").replace(
            tzinfo=timezone.utc)
    except ValueError:
        return ""

    lines = []
    # One reading every 15 minutes = 96 readings per day
    for i in range(96):
        ts = int(date.timestamp()) + i * 900
        temps = []
        for j, base in enumerate(node["base_temps"]):
            # Simulate daily temperature cycle (compressor on/off)
            hour_frac = (i * 15) / 60.0
            cycle = 0.4 * math.sin(hour_frac * math.pi / 3)
            noise = random.gauss(0, 0.08)
            temps.append(f"{base + cycle + noise:.2f}")
        line = f"{ts},{','.join(temps)}"
        lines.append(line)

    return "\n".join(lines) + "\n"


def node_to_api(node, include_detail=False):
    """Convert mock node to API response format."""
    now = int(time.time())
    temps = get_live_temps(node) if node["online"] else node["base_temps"]

    result = {
        "id": node["id"],
        "label": node["label"],
        "online": node["online"],
        "probe_count": node["probe_count"],
        "sd_status": node["sd_status"],
        "pending_sync": node["pending_sync"],
        "last_seen": now - (10 if node["online"] else 7200),
        "last_reading": now - (15 if node["online"] else 7200),
        "temperatures": [f"{t:.2f}" for t in temps],
        "thresholds": node["thresholds"],
        "probe_labels": node.get("probe_labels",
                                 [f"Probe {i+1}" for i in range(node["probe_count"])]),
    }

    if include_detail:
        result["mac"] = node["mac"]
        result["calibration"] = ["0.00"] * node["probe_count"]
        result["reading_interval_sec"] = node.get("reading_interval_sec", 900)
        # Last 7 days of data available
        today = datetime.now(timezone.utc)
        result["dates"] = [
            (today - timedelta(days=d)).strftime("%Y-%m-%d")
            for d in range(7)
        ]

    return result


# ─── HTTP Handler ─────────────────────────────────────────────────────

class MockHandler(SimpleHTTPRequestHandler):
    """Serves dashboard static files + mock API endpoints."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DATA_DIR, **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        # Browsers request favicon — return empty 204 since we don't have one
        if path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return

        # ── API Routes ──
        if path == "/api/nodes":
            data = [node_to_api(n) for n in MOCK_NODES]
            return self._json(data)

        if path == "/api/status":
            now = datetime.now(timezone.utc)
            data = {
                "uptime_sec": int(time.time() - START_TIME),
                "time": int(time.time()),
                "time_iso": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "time_quality": 2,
                "has_rtc": True,
                "has_ntp": True,
                "node_count": len(MOCK_NODES),
                "free_heap": 180224,
            }
            return self._json(data)

        if path == "/api/gateway/config":
            data = {
                "default_warn_high": 3.3,
                "default_crit_high": 5.0,
                "default_warn_low": -2.2,
                "default_crit_low": -3.9,
                "node_timeout_sec": 1800,
                "reading_interval_sec": 900,
                "ntp_sync_interval_min": 60,
            }
            return self._json(data)

        # /api/node/<id>
        if path.startswith("/api/node/"):
            parts = path.split("/")
            # /api/node/<id>/data?date=...
            if len(parts) == 5 and parts[4] == "data":
                node_id = parts[3]
                node = next((n for n in MOCK_NODES if n["id"] == node_id),
                            None)
                if not node:
                    return self._error(404, "Node not found")
                date = query.get("date", [None])[0]
                if not date:
                    return self._error(400, "Missing 'date' parameter")
                csv = generate_csv_data(node, date)
                self.send_response(200)
                self.send_header("Content-Type", "text/csv")
                self.end_headers()
                self.wfile.write(csv.encode())
                return

            # /api/node/<id>
            if len(parts) == 4:
                node_id = parts[3]
                node = next((n for n in MOCK_NODES if n["id"] == node_id),
                            None)
                if not node:
                    return self._error(404, "Node not found")
                data = node_to_api(node, include_detail=True)
                return self._json(data)

        # ── Static Files ──
        super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        content_len = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_len).decode() if content_len else ""

        # /api/node/<id>/label
        if "/label" in path:
            return self._json({"ok": True})

        # /api/node/<id>/probe-labels
        if "/probe-labels" in path:
            parts = path.split("/")
            if len(parts) >= 4:
                node_id = parts[3]
                node = next((n for n in MOCK_NODES if n["id"] == node_id),
                            None)
                if node:
                    # Parse URL-encoded body
                    from urllib.parse import parse_qs as pqs
                    params = pqs(body)
                    for i in range(node["probe_count"]):
                        key = f"probe{i}"
                        if key in params:
                            node["probe_labels"][i] = params[key][0]
                    print(f"  Updated probe labels for {node_id}: "
                          f"{node['probe_labels']}")
            return self._json({"ok": True})

        # /api/node/<id>/config
        if "/config" in path and "/gateway/" not in path:
            return self._json({"ok": True})

        # /api/gateway/config
        if path == "/api/gateway/config":
            print("  [MOCK] Gateway config updated")
            return self._json({"ok": True})

        # /api/wifi
        if path == "/api/wifi":
            return self._json({
                "ok": True,
                "msg": "(Mock) WiFi settings saved"
            })

        self._error(404, "Not Found")

    def do_DELETE(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # /api/node/<id>/data
        if path.startswith("/api/node/") and path.endswith("/data"):
            parts = path.split("/")
            node_id = parts[3]
            node = next((n for n in MOCK_NODES if n["id"] == node_id), None)
            if not node:
                return self._error(404, "Node not found")
            print(f"  [MOCK] Cleared all logs for node {node_id} "
                  f"({node['label']})")
            return self._json({"ok": True})

        self._error(404, "Not Found")

    def _json(self, data):
        body = json.dumps(data)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body.encode())

    def _error(self, code, msg):
        body = json.dumps({"error": msg})
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(body.encode())

    def log_message(self, format, *args):
        """Quieter logs — only show API calls, not static files."""
        try:
            msg = str(args[0]) if args else ""
            path = msg.split()[1] if " " in msg else ""
            if path.startswith("/api/"):
                print(f"  {msg}")
        except (IndexError, AttributeError):
            pass


# ─── Main ─────────────────────────────────────────────────────────────

def main():
    data_path = os.path.abspath(DATA_DIR)
    print(f"""
╔════════════════════════════════════════════════════════════════╗
║  � DairyLedger — Dashboard Preview Server                    ║
╠════════════════════════════════════════════════════════════════╣
║                                                                ║
║  Serving dashboard from:                                       ║
║    {data_path:<55s}║
║                                                                ║
║  Mock nodes: {len(MOCK_NODES)} fridges (1 offline, 1 degraded SD)          ║
║                                                                ║
║  Open in your browser:                                         ║
║    → http://localhost:{PORT}                                      ║
║    → http://localhost:{PORT}/admin.html                           ║
║    → http://localhost:{PORT}/export.html                          ║
║    → http://localhost:{PORT}/node.html?id=K3VP8N                  ║
║                                                                ║
║  Press Ctrl+C to stop                                          ║
╚════════════════════════════════════════════════════════════════╝
""")

    server = HTTPServer(("0.0.0.0", PORT), MockHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")
        server.server_close()


if __name__ == "__main__":
    main()
