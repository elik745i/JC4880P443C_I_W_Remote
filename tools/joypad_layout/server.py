from __future__ import annotations

import base64
import binascii
import json
import mimetypes
import os
import subprocess
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from generate_header import normalize_layout_document, read_layouts_from_header, write_header, write_lvgl_png_asset


ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "joypad_layout"
LAYOUT_PATH = TOOL_DIR / "joypad_layout.json"
HEADER_PATH = ROOT / "components" / "apps" / "setting" / "joypad" / "SettingJoypadLayout.hpp"
CONTROLLER_IMAGE = ROOT / "3D" / "map" / "controller.png"
CONTROLLER_IMAGE_ASSET = ROOT / "components" / "apps" / "setting" / "ui" / "images" / "ui_img_controller_png.c"
IDF_EXPORT_BAT = Path(r"C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat")
IDF_PYTHON = Path(r"C:\Users\Elik\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe")
IDF_PY = Path(r"C:\Espressif\frameworks\esp-idf-v5.5.4\tools\idf.py")
RISCV_GCC = Path(r"C:\Users\Elik\.espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin\riscv32-esp-elf-gcc.exe")
RISCV_GXX = Path(r"C:\Users\Elik\.espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin\riscv32-esp-elf-g++.exe")
RISCV_TOOLCHAIN_BIN = Path(r"C:\Users\Elik\.espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin")
CMAKE_BIN = Path(r"C:\Users\Elik\.espressif\tools\cmake\3.30.2\bin")
NINJA_BIN = Path(r"C:\Users\Elik\.espressif\tools\ninja\1.12.1")
IDF_EXE_BIN = Path(r"C:\Users\Elik\.espressif\tools\idf-exe\1.0.3")
CCACHE_BIN = Path(r"C:\Users\Elik\.espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64")


def _load_layout_document() -> dict:
    return normalize_layout_document(json.loads(LAYOUT_PATH.read_text(encoding="utf-8")))


def _write_layout_cache(layout_document: dict) -> dict:
    normalized_document = normalize_layout_document(layout_document)
    LAYOUT_PATH.write_text(json.dumps(normalized_document, indent=2) + "\n", encoding="utf-8")
    return normalized_document


def _save_layout(layout_document: dict) -> None:
    normalized_document = _write_layout_cache(layout_document)
    write_header(LAYOUT_PATH, HEADER_PATH)


def _validate_layout_contract(candidate_document: dict) -> None:
    current_document = _load_layout_document()
    for layout_key in ("bleController", "localController"):
        candidate_layout = candidate_document["layouts"][layout_key]
        current_layout = current_document["layouts"][layout_key]
        for collection_name in ("triggerBars", "shoulders", "sticks", "dpadButtons", "faceButtons"):
            candidate_ids = [item.get("id") for item in candidate_layout[collection_name]]
            current_ids = [item.get("id") for item in current_layout[collection_name]]
            if candidate_ids != current_ids:
                raise ValueError(
                    f"{layout_key}.{collection_name} changed mapped button ids. "
                    "Use the add/delete controls to hide or restore existing supported buttons instead of changing ids or counts."
                )


def _merge_layout_geometry(current_layout: dict, header_layout: dict) -> dict:
    merged = json.loads(json.dumps(current_layout))
    merged["deviceCanvas"] = header_layout["deviceCanvas"]
    merged["previewFrame"] = header_layout["previewFrame"]
    merged["controllerSource"] = header_layout["controllerSource"]

    rect_collections = [
        ("triggerBars", ("x", "y", "width", "height")),
        ("shoulders", ("x", "y", "width", "height")),
        ("sticks", ("x", "y", "size")),
        ("dpadButtons", ("centerX", "centerY", "size")),
        ("faceButtons", ("centerX", "centerY", "size")),
    ]
    for collection_name, keys in rect_collections:
        for index, item in enumerate(header_layout[collection_name]):
            target = merged[collection_name][index]
            for key in keys:
                target[key] = item[key]

    return merged


def _merge_geometry_from_header(current_document: dict, header_document: dict) -> dict:
    merged_document = normalize_layout_document(current_document)
    for layout_key, layout in merged_document["layouts"].items():
        header_layout = header_document["layouts"].get(layout_key)
        if header_layout is None:
            continue
        merged_document["layouts"][layout_key] = _merge_layout_geometry(layout, header_layout)
    return merged_document


def _load_layout_document_from_code() -> dict:
    cached_document = _load_layout_document()
    code_document = read_layouts_from_header(HEADER_PATH)
    merged_document = _merge_geometry_from_header(cached_document, code_document)
    return _write_layout_cache(merged_document)


def _build_p4_firmware() -> dict:
    if not IDF_EXPORT_BAT.is_file():
        raise ValueError(f"Could not find ESP-IDF export script at {IDF_EXPORT_BAT}")
    if not IDF_PYTHON.is_file():
        raise ValueError(f"Could not find ESP-IDF Python at {IDF_PYTHON}")
    if not IDF_PY.is_file():
        raise ValueError(f"Could not find idf.py at {IDF_PY}")
    if not RISCV_GCC.is_file() or not RISCV_GXX.is_file():
        raise ValueError("Could not find the configured ESP32-P4 RISC-V toolchain")

    build_script = TOOL_DIR / ".joypad_layout_build.cmd"
    build_log = TOOL_DIR / ".joypad_layout_build.log"
    build_script.write_text(
        "@echo off\n"
        f'set "PATH={IDF_PYTHON.parent};%PATH%"\n'
        f'call "{IDF_EXPORT_BAT}" >nul\n'
        f'set "PATH={RISCV_TOOLCHAIN_BIN};{CMAKE_BIN};{NINJA_BIN};{IDF_EXE_BIN};{CCACHE_BIN};%PATH%"\n'
        f'set "CMAKE_C_COMPILER={RISCV_GCC}"\n'
        f'set "CMAKE_CXX_COMPILER={RISCV_GXX}"\n'
        f'set "CMAKE_ASM_COMPILER={RISCV_GCC}"\n'
        f'"{IDF_PYTHON}" "{IDF_PY}" build\n',
        encoding="utf-8",
    )
    try:
        with build_log.open("w", encoding="utf-8", errors="replace") as log_file:
            completed = subprocess.run(
                ["cmd.exe", "/d", "/c", str(build_script)],
                cwd=ROOT,
                env=os.environ.copy(),
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=1800,
            )
        output = build_log.read_text(encoding="utf-8", errors="replace").strip() if build_log.exists() else ""
        tail_lines = output.splitlines()[-40:] if output else []
        return {
            "ok": completed.returncode == 0,
            "exitCode": completed.returncode,
            "outputTail": "\n".join(tail_lines),
        }
    finally:
        build_script.unlink(missing_ok=True)
        build_log.unlink(missing_ok=True)


def _replace_background_image(payload: dict) -> None:
    data_url = payload.get("data", "")
    if not data_url.startswith("data:image/png;base64,"):
        raise ValueError("Expected a PNG file encoded as a data URL")
    try:
        image_bytes = base64.b64decode(data_url.split(",", 1)[1], validate=True)
    except (IndexError, binascii.Error) as exc:
        raise ValueError(f"Invalid PNG payload: {exc}") from exc

    if image_bytes[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Uploaded file is not a valid PNG")

    CONTROLLER_IMAGE.write_bytes(image_bytes)
    write_lvgl_png_asset(CONTROLLER_IMAGE, CONTROLLER_IMAGE_ASSET)


class Handler(BaseHTTPRequestHandler):
    def _send_bytes(self, body: bytes, content_type: str, status: int = HTTPStatus.OK) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, payload: dict, status: int = HTTPStatus.OK) -> None:
        body = json.dumps(payload, indent=2).encode("utf-8")
        self._send_bytes(body, "application/json; charset=utf-8", status)

    def _serve_file(self, path: Path) -> None:
        if not path.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        content_type, _ = mimetypes.guess_type(str(path))
        self._send_bytes(path.read_bytes(), content_type or "application/octet-stream")

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/layout":
            try:
                self._send_json(_load_layout_document_from_code())
            except ValueError as exc:
                self._send_json({"error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        if parsed.path == "/controller.png":
            self._serve_file(CONTROLLER_IMAGE)
            return
        if parsed.path in {"/", "/index.html"}:
            self._serve_file(TOOL_DIR / "index.html")
            return

        relative = parsed.path.lstrip("/")
        self._serve_file(TOOL_DIR / relative)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/layout/from-header":
            try:
                merged_document = _load_layout_document_from_code()
            except ValueError as exc:
                self._send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return
            self._send_json({
                "ok": True,
                "layout": merged_document,
                "layoutPath": str(LAYOUT_PATH),
                "headerPath": str(HEADER_PATH),
            })
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(content_length)

        if parsed.path == "/api/background":
            try:
                payload = json.loads(raw_body.decode("utf-8"))
                _replace_background_image(payload)
            except (json.JSONDecodeError, ValueError) as exc:
                self._send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return
            self._send_json({
                "ok": True,
                "imagePath": str(CONTROLLER_IMAGE),
                "assetPath": str(CONTROLLER_IMAGE_ASSET),
            })
            return

        if parsed.path != "/api/layout":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            layout_document = json.loads(raw_body.decode("utf-8"))
        except json.JSONDecodeError as exc:
            self._send_json({"error": f"Invalid JSON: {exc}"}, HTTPStatus.BAD_REQUEST)
            return

        normalized_document = normalize_layout_document(layout_document)
        try:
            _validate_layout_contract(normalized_document)
            _save_layout(normalized_document)
        except ValueError as exc:
            self._send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return

        self._send_json({
            "ok": True,
            "layout": _load_layout_document_from_code(),
            "layoutPath": str(LAYOUT_PATH),
            "headerPath": str(HEADER_PATH),
        })


if __name__ == "__main__":
    if HEADER_PATH.is_file():
        try:
            _load_layout_document_from_code()
        except ValueError:
            write_header(LAYOUT_PATH, HEADER_PATH)
    else:
        write_header(LAYOUT_PATH, HEADER_PATH)
    write_lvgl_png_asset(CONTROLLER_IMAGE, CONTROLLER_IMAGE_ASSET)
    server = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    print("Joypad layout configurator running at http://127.0.0.1:8765")
    server.serve_forever()