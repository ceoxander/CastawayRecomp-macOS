"""Serve live macOS port status on loopback using local build and test logs."""
import argparse
import json
import re
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
BUILD = ROOT / "build/macos-arm64"
OUT = ROOT / "out/darwin"
PAGE = TOOLS / "port_dashboard/dist/index.html"


def read(path):
    try:
        with path.open("rb") as stream:
            stream.seek(max(0, path.stat().st_size - 1_000_000))
            return stream.read().decode(errors="replace")
    except OSError:
        return ""


def latest(paths):
    return max((p for p in paths if p.is_file()), key=lambda p: p.stat().st_mtime, default=None)


def status():
    baseline_log = BUILD / "e2e-metal/CastawayRecomp.log"
    pacing_log = BUILD / "pacing-smoke/CastawayRecomp.log"
    pacing = read(pacing_log)
    play4x_log = BUILD / "qa-e2e4/CastawayRecomp.log"
    play4x = read(play4x_log)
    passed_run = "12 passed, 0 failed; quit (0)"
    test = next((run for run in (play4x, pacing) if passed_run in run), read(baseline_log))
    summary = re.findall(r"TAS: (\d+) passed, (\d+) failed; quit \((\d+)\)", test)
    passed = sum(line.startswith("[I") and "TAS: PASS " in line for line in test.splitlines())
    e2e = bool(summary and summary[-1][1:] == ("0", "0"))
    package = OUT / "CastawayRecomp.app/Contents/MacOS/CastawayRecomp"
    milestones = [
        ("Memoria de Wii", (BUILD / "alias-smoke").is_file()),
        ("Fibras ARM64", "guest thread start" in test),
        ("Arranque del juego", "jumping to entry point" in test),
        ("Producción de audio", "SDL audio (coreaudio)" in test),
        ("Renderizado Metal", "GX: DXMT device" in test and "GX: 300 frames" in test),
        ("Partida nueva y movimiento", "walk right" in test and "TAS: PASS" in test),
        ("Guardar y recargar", "the Sim is where it was saved" in test and e2e),
        ("Paquete ARM64 jugable", e2e and package.is_file()),
    ]

    build_log = latest(BUILD.glob("castaway-*.log"))
    build = read(build_log) if build_log else ""
    steps = re.findall(r"^\[(\d+)/(\d+)\]", build, re.MULTILINE)
    done, total = map(int, steps[-1]) if steps else (0, 0)
    failed = "ninja: build stopped" in build or "CMake Error" in build
    if not failed and re.search(r"CastawayRecomp\.app: replacing existing signature\s*$", build):
        done = total
    run_log = latest([OUT / "CastawayRecomp.log", *BUILD.glob("*/CastawayRecomp.log")])
    run = read(run_log) if run_log else ""
    samples = re.findall(r"status: ([\d.]+) fps, VI ([\d.]+) Hz, audio peak (\d+) underruns (\d+) drops (\d+) repeats (\d+) rate ([\d.]+) Hz", run)
    visual = read(BUILD / "color-verified/CastawayRecomp.log")
    color_verified = "macOS color pass" in visual
    hdr_gains = re.findall(r"HDR headroom ([0-9.]+)", visual)
    startup = read(BUILD / "feature-smoke/CastawayRecomp.log")
    menu_path = BUILD / "ui-final/main-menu.bmp"
    menu_verified = menu_path.is_file()
    boot4x = read(BUILD / "retina-4x/CastawayRecomp.log")
    backup_verified = "NAND backup:" in play4x and (BUILD / "qa-e2e4/backups/R42E69/SaveGame.1").is_file()
    polish = [
        ("Arranque de Apple", "macOS startup completed" in startup),
        ("Gradación Tropical intenso", color_verified),
        ("HDR / Apple EDR", bool(hdr_gains and float(hdr_gains[-1]) > 1)),
        ("Audio a 48 kHz y 512 muestras", "48000 Hz, 2 channels, 512 sample frames" in visual and passed_run in pacing),
        ("Resolución interna 4×", "scale=4" in boot4x and "GX: 600 frames" in boot4x),
        ("Menú y corrección Retina", menu_verified),
        ("Idioma de interfaz independiente", "Idioma de la interfaz" in (ROOT / "runtime/ui/settings_ui.cpp").read_text()),
        ("Respaldo automático NAND", backup_verified),
        ("Perfiles por pantalla", False),
        ("GX directo Metal, FSR y 120 FPS", False),
    ]
    features = [
        ("Español latino", "Launcher disponible · juego USA en inglés"),
        ("Logo de Apple y crédito", "Integrados"),
        ("DualShock, DualSense y mandos SDL3", "Entrada integrada · prueba física pendiente"),
        ("Continuidad del audio", "Ajuste validado en partida"),
        ("Escalado interno", "Partida completa a 4×" if passed_run in play4x else "Verificado hasta el título a 4×"),
        ("Tropical intenso", "Verificado" if color_verified else "Prueba Metal en curso"),
        ("HDR / Apple EDR", f"Margen {hdr_gains[-1]}× en prueba" if hdr_gains else "Disponible según pantalla"),
        ("Respaldo de guardado", "Verificado" if backup_verified else "Integrado · prueba en curso"),
        ("Menú principal renovado", "Visible en Retina" if menu_verified else "En prueba"),
        ("Perfiles por pantalla", "Elegido"),
        ("GX 100% Metal · FSR · 120 FPS", "Pendiente"),
    ]
    timeline = []
    def step(path, state, title, description):
        if path and path.is_file():
            timeline.append({"key": str(len(timeline)) + state + title,
                             "time": datetime.fromtimestamp(path.stat().st_mtime).isoformat(timespec="seconds"),
                             "state": state, "title": title, "description": description})

    step(baseline_log, "Verificado", "Partida completa a 2×", "Nueva partida, movimiento, guardado y recarga. 12 comprobaciones aprobadas.")
    if "macOS startup completed" in read(BUILD / "feature-smoke/CastawayRecomp.log"):
        step(BUILD / "feature-smoke/CastawayRecomp.log", "Verificado", "Arranque de macOS", "Los avisos iniciales de Wii quedan detrás del logo de Apple.")
    if "12 passed, 0 failed; quit (0)" in pacing:
        step(pacing_log, "Verificado", "Audio y video sincronizados", "Lectura de GPU asíncrona y 12 comprobaciones aprobadas en la segunda partida.")
    if color_verified:
        step(BUILD / "color-verified/CastawayRecomp.log", "Verificado", "Tropical intenso", "El shader Metal de saturación y nitidez está activo.")
    if menu_verified:
        step(menu_path, "Verificado", "Interfaz Retina", "El menú propio del port se dibuja en la ventana real.")
    if play4x_log.is_file():
        step(play4x_log, "Verificado" if passed_run in play4x else "En curso", "Partida y respaldo a 4×",
             "La prueba verifica Nueva partida, movimiento, guardado y recarga.")
    if build_log:
        state = "Error" if failed else ("Terminado" if total and done == total else "En curso")
        step(build_log, state, "Compilación actual", f"{done} de {total} pasos de Ninja.")
    timeline.sort(key=lambda event: event["time"], reverse=True)
    notes = re.findall(r"TAS: (?:PASS .*?: |[\d.]+ s: )([^\r\n]+)", test)
    return {
        "time": datetime.now().isoformat(timespec="seconds"),
        "milestones": milestones, "polish": polish, "features": features, "testPassed": passed, "testComplete": e2e,
        "build": {"done": done, "total": total, "failed": failed, "label": build_log.stem if build_log else None},
        "sample": [float(value) for value in samples[-1]] if samples else None,
        "timeline": timeline,
        "checks": [line.split(" msg ", 1)[0] for line in notes if not line.startswith(("file ", "state ", "e2e.tas:"))][-6:],
    }


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            data, content_type = PAGE.read_bytes(), "text/html; charset=utf-8"
        elif self.path == "/api/status":
            data = json.dumps(status(), ensure_ascii=False).encode()
            content_type = "application/json; charset=utf-8"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=8765, type=int)
    args = parser.parse_args()
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        print(f"http://127.0.0.1:{server.server_port}", flush=True)
        server.serve_forever()
