#!/usr/bin/env python3
"""DOBACK UDP dashboard for Windows.

Receives JSON telemetry by UDP, serves a local HTML dashboard, records selected
measurements to CSV, and sends UDP JSON commands back to the Jetson.
"""

import argparse
import csv
import json
import math
import os
import queue
import re
import socket
import socketserver
import tempfile
import threading
import time
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse


DEFAULT_TELEMETRY_PORT = 50100
DEFAULT_COMMAND_PORT = 50101
DEFAULT_HTTP_PORT = 8080
SAFE_NAME_RE = re.compile(r"[^A-Za-z0-9._-]+")


class ThreadingHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True


def sanitize_measurement_name(name):
    clean = SAFE_NAME_RE.sub("_", name.strip()).strip("._-")
    return clean[:80] or datetime.now().strftime("measurement_%Y%m%d_%H%M%S")


def compute_physics(
    mass_kg,
    track_width_m,
    cg_height_m,
    roll_inertia_kg_m2,
):
    values = [mass_kg, track_width_m, cg_height_m, roll_inertia_kg_m2]
    if any(not math.isfinite(value) for value in values):
        raise ValueError("Los parámetros deben ser números finitos.")
    if mass_kg <= 0 or track_width_m <= 0 or cg_height_m <= 0:
        raise ValueError("Peso, ancho de vía y altura del centro de gravedad deben ser mayores que cero.")
    if roll_inertia_kg_m2 < 0:
        raise ValueError("El momento de inercia no puede ser negativo.")

    d1 = math.sqrt(cg_height_m**2 + (track_width_m / 2.0) ** 2)
    ixx = mass_kg * d1**2 + roll_inertia_kg_m2
    if ixx <= 0:
        raise ValueError("Ixx no puede ser cero.")
    fic = math.atan(track_width_m / (2.0 * cg_height_m)) * 180.0 / math.pi
    return {
        "d1_m": d1,
        "ixx_kg_m2": ixx,
        "fic_deg": fic,
        "coeff_si": 2.0 * mass_kg * 9.81 / ixx,
        "alfa_deg": 90.0 - fic,
    }


def parse_json_body(handler):
    length = int(handler.headers.get("Content-Length", "0"))
    if length > 65536:
        raise ValueError("Cuerpo demasiado grande.")
    raw = handler.rfile.read(length) if length else b"{}"
    data = json.loads(raw.decode("utf-8"))
    if not isinstance(data, dict):
        raise ValueError("El cuerpo debe ser un objeto JSON.")
    return data


class Recorder:
    def __init__(self, output_dir):
        self.output_dir = output_dir
        self.active = False
        self.name = ""
        self.rows = []
        self.started_at = None

    def start(self, name):
        if self.active:
            raise ValueError("Ya hay una medición en curso.")
        self.name = sanitize_measurement_name(name)
        self.rows = []
        self.started_at = datetime.now(timezone.utc).isoformat()
        self.active = True

    def add(self, telemetry):
        if self.active:
            self.rows.append(flatten_telemetry(telemetry))

    def stop(self, name=None):
        if not self.active:
            raise ValueError("No hay una medición en curso.")
        if name:
            self.name = sanitize_measurement_name(name)
        filename = f"{self.name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        path = self.output_dir / filename
        write_csv_atomic(path, self.rows)
        self.active = False
        return path


class AppState:
    def __init__(self, output_dir, jetson_ip, command_port):
        self.lock = threading.Lock()
        self.condition = threading.Condition(self.lock)
        self.last_telemetry = None
        self.last_seen_monotonic = None
        self.last_sender_ip = None
        self.recorder = Recorder(output_dir)
        self.clients = []
        self.jetson_ip = jetson_ip
        self.command_port = command_port
        self.last_command_status = ""
        self.current_physics = None

    def update_telemetry(self, telemetry, sender_ip):
        with self.lock:
            self.last_telemetry = telemetry
            self.last_sender_ip = sender_ip
            self.last_seen_monotonic = time.monotonic()
            self.recorder.add(self.last_telemetry)
            self._broadcast_locked()

    def snapshot(self):
        with self.lock:
            return self._snapshot_locked()

    def _snapshot_locked(self):
        age_s = None
        if self.last_seen_monotonic is not None:
            age_s = max(0.0, time.monotonic() - self.last_seen_monotonic)
        return {
            "telemetry": self.last_telemetry,
            "age_s": age_s,
            "recording": self.recorder.active,
            "recording_name": self.recorder.name,
            "recorded_rows": len(self.recorder.rows),
            "jetson_ip": self.last_sender_ip or self.jetson_ip,
            "last_command_status": self.last_command_status,
            "physics": self.current_physics,
        }

    def _broadcast_locked(self):
        payload = json.dumps(self._snapshot_locked(), separators=(",", ":"))
        live_clients = []
        for client in self.clients:
            try:
                client.put_nowait(payload)
                live_clients.append(client)
            except queue.Full:
                pass
        self.clients = live_clients
        self.condition.notify_all()

    def send_command(self, payload):
        raw = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        with self.lock:
            target_ip = self.last_sender_ip if self.jetson_ip == "auto" else self.jetson_ip
        if not target_ip:
            raise ValueError("Aún no se conoce la IP de la Jetson; espera la primera telemetría.")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(raw, (target_ip, self.command_port))
        with self.lock:
            self.last_command_status = f"Enviado {payload.get('type')} a {target_ip}:{self.command_port}"
            if payload.get("type") == "config":
                self.current_physics = {k: v for k, v in payload.items() if k != "type"}
                self.current_physics.update(compute_physics(
                    float(payload["mass_kg"]),
                    float(payload["track_width_m"]),
                    float(payload["cg_height_m"]),
                    float(payload["roll_inertia_kg_m2"]),
                ))
            self._broadcast_locked()


def flatten_telemetry(telemetry):
    row = {
        "received_utc": datetime.now(timezone.utc).isoformat(),
        "sequence": telemetry.get("sequence"),
        "doback_timestamp_utc": telemetry.get("doback_timestamp_utc"),
    }
    for section in ("orientation", "measurement", "gps", "physics"):
        value = telemetry.get(section)
        if isinstance(value, dict):
            for key, item in value.items():
                row[f"{section}.{key}"] = item
    return row


def write_csv_atomic(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    columns = []
    for row in rows:
        for key in row:
            if key not in columns:
                columns.append(key)
    if not columns:
        columns = ["received_utc"]

    fd, tmp_name = tempfile.mkstemp(prefix=path.name, suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", newline="", encoding="utf-8") as tmp:
            writer = csv.DictWriter(tmp, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
        os.replace(tmp_name, path)
    except Exception:
        try:
            os.unlink(tmp_name)
        finally:
            raise


def make_handler(state, static_dir):
    class Handler(BaseHTTPRequestHandler):
        server_version = "DobackDashboard/1.0"

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path in ("/", "/index.html"):
                self.send_static(static_dir / "index.html", "text/html; charset=utf-8")
            elif parsed.path == "/app.css":
                self.send_static(static_dir / "app.css", "text/css; charset=utf-8")
            elif parsed.path == "/app.js":
                self.send_static(static_dir / "app.js", "application/javascript; charset=utf-8")
            elif parsed.path == "/api/state":
                self.send_json(state.snapshot())
            elif parsed.path == "/events":
                self.send_events()
            else:
                self.send_error(HTTPStatus.NOT_FOUND)

        def do_POST(self):
            parsed = urlparse(self.path)
            try:
                if parsed.path == "/api/start":
                    data = parse_json_body(self)
                    with state.lock:
                        state.recorder.start(str(data.get("name", "")))
                        state._broadcast_locked()
                    self.send_json({"ok": True})
                elif parsed.path == "/api/stop":
                    data = parse_json_body(self)
                    with state.lock:
                        path = state.recorder.stop(str(data.get("name", "") or state.recorder.name))
                        state._broadcast_locked()
                    self.send_json({"ok": True, "path": str(path)})
                elif parsed.path == "/api/calibrate":
                    state.send_command({"type": "calibrate"})
                    self.send_json({"ok": True})
                elif parsed.path == "/api/config":
                    data = parse_json_body(self)
                    payload = {
                        "type": "config",
                        "mass_kg": float(data["mass_kg"]),
                        "track_width_m": float(data["track_width_m"]),
                        "cg_height_m": float(data["cg_height_m"]),
                        "roll_inertia_kg_m2": float(data["roll_inertia_kg_m2"]),
                    }
                    physics = compute_physics(
                        payload["mass_kg"],
                        payload["track_width_m"],
                        payload["cg_height_m"],
                        payload["roll_inertia_kg_m2"],
                    )
                    state.send_command(payload)
                    self.send_json({"ok": True, "physics": physics})
                else:
                    self.send_error(HTTPStatus.NOT_FOUND)
            except (KeyError, ValueError, json.JSONDecodeError) as exc:
                self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)

        def send_static(self, path, content_type):
            data = path.read_bytes()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def send_json(self, payload, status=HTTPStatus.OK):
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def send_events(self):
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            client = queue.Queue(maxsize=5)
            with state.lock:
                state.clients.append(client)
                client.put_nowait(json.dumps(state._snapshot_locked(), separators=(",", ":")))
            while True:
                try:
                    payload = client.get(timeout=15)
                    self.wfile.write(f"data: {payload}\n\n".encode("utf-8"))
                    self.wfile.flush()
                except queue.Empty:
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    break

        def log_message(self, fmt, *args):
            return

    return Handler


def udp_listener(host, port, state, stop):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))
        sock.settimeout(0.5)
        while not stop.is_set():
            try:
                raw, sender = sock.recvfrom(65535)
            except socket.timeout:
                continue
            try:
                telemetry = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(telemetry, dict) and telemetry.get("type") == "telemetry":
                state.update_telemetry(telemetry, sender[0])


def parse_args():
    parser = argparse.ArgumentParser(description="Panel HTML local para DOBACK por UDP.")
    parser.add_argument("--http-host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--udp-host", default="0.0.0.0")
    parser.add_argument("--udp-port", type=int, default=DEFAULT_TELEMETRY_PORT)
    parser.add_argument(
        "--jetson-ip",
        default="auto",
        help="IP de Jetson; 'auto' usa el origen de la última telemetría.",
    )
    parser.add_argument("--command-port", type=int, default=DEFAULT_COMMAND_PORT)
    parser.add_argument("--output-dir", default=str(Path.cwd() / "measurements"))
    return parser.parse_args()


def main():
    args = parse_args()
    base_dir = Path(__file__).resolve().parent
    state = AppState(Path(args.output_dir).resolve(), args.jetson_ip, args.command_port)
    stop = threading.Event()
    listener = threading.Thread(
        target=udp_listener,
        args=(args.udp_host, args.udp_port, state, stop),
        daemon=True,
    )
    listener.start()
    server = ThreadingHTTPServer(
        (args.http_host, args.http_port),
        make_handler(state, base_dir / "static"),
    )
    print(f"Panel DOBACK: http://{args.http_host}:{args.http_port}")
    print(f"Escuchando telemetría UDP en {args.udp_host}:{args.udp_port}")
    print(f"Comandos UDP a Jetson {args.jetson_ip}:{args.command_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()


if __name__ == "__main__":
    main()
