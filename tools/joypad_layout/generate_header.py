from __future__ import annotations

import json
import re
import struct
from pathlib import Path


LAYOUT_SYMBOLS = {
    "bleController": "kBleCalibrationLayout",
    "localController": "kLocalControllerLayout",
}


def _clone_json(value: dict) -> dict:
    return json.loads(json.dumps(value))


def normalize_layout_document(layout_document: dict) -> dict:
    if "layouts" in layout_document:
        layouts = dict(layout_document["layouts"])
        if "bleController" not in layouts:
            first_layout = next(iter(layouts.values()), {})
            layouts["bleController"] = _clone_json(first_layout)
        if "localController" not in layouts:
            layouts["localController"] = _clone_json(layouts["bleController"])
        return {
            "selectedTarget": layout_document.get("selectedTarget", "bleController"),
            "layouts": {
                "bleController": layouts["bleController"],
                "localController": layouts["localController"],
            },
        }

    base_layout = _clone_json(layout_document)
    return {
        "selectedTarget": "bleController",
        "layouts": {
            "bleController": base_layout,
            "localController": _clone_json(base_layout),
        },
    }


def _read_layout_document(layout_path: Path) -> dict:
    return normalize_layout_document(json.loads(layout_path.read_text(encoding="utf-8")))


def _escape_cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def _point_lines(points: list[dict]) -> str:
    if not points:
        return ""
    return "\n".join(f"            {{{point['x']}, {point['y']}}}," for point in points)


def _visual_block(item: dict) -> str:
    visual = item["visual"]
    shape = visual["shape"]
    preview = visual.get("preview", {})
    point_lines = _point_lines(shape.get("points", []))
    points_block = "            {\n"
    if point_lines:
        points_block += f"{point_lines}\n"
    points_block += "            }"
    return f'''{{
        {str(visual.get('enabled', True)).lower()},
        "{_escape_cpp_string(visual['label'])}",
        "{_escape_cpp_string(visual['fillColor'])}",
        "{_escape_cpp_string(visual['borderColor'])}",
        {visual['borderWidth']},
        "{_escape_cpp_string(visual['textColor'])}",
        {visual['textSize']},
        "{_escape_cpp_string(visual['textStyle'])}",
        "{_escape_cpp_string(visual.get('functionType', 'tactile'))}",
        {int(preview.get('analogLevel', 50))},
        {int(preview.get('dpadX', 0))},
        {int(preview.get('dpadY', 0))},
        {{
            "{_escape_cpp_string(shape['type'])}",
            {shape['cornerRadius']},
            {int(shape.get('rotationDegrees', 0))},
            {len(shape.get('points', []))},
{points_block}
        }}
    }}'''


def _render_visual_array(items: list[dict]) -> str:
    return "\n".join(f"        {_visual_block(item)}," for item in items)


def _preview_offset(item: dict, axis: str) -> int:
    visual = item.get("visual", {})
    preview = visual.get("preview", {})
    return int(preview.get(f"offset{axis}", 0) or 0)


def _applied_rect(item: dict) -> dict[str, int]:
    return {
        "x": int(item["x"]) - _preview_offset(item, "X"),
        "y": int(item["y"]) - _preview_offset(item, "Y"),
        "width": int(item["width"]),
        "height": int(item["height"]),
    }


def _applied_circle(item: dict) -> dict[str, int]:
    return {
        "centerX": int(item["centerX"]) - _preview_offset(item, "X"),
        "centerY": int(item["centerY"]) - _preview_offset(item, "Y"),
        "size": int(item["size"]),
    }


def _render_layout_constant(symbol_name: str, layout: dict) -> str:
    trigger_bar_items = [_applied_rect(item) for item in layout["triggerBars"]]
    shoulder_items = [_applied_rect(item) for item in layout["shoulders"]]
    stick_items = [
        _applied_rect({
            "x": item["x"],
            "y": item["y"],
            "width": item.get("width", item["size"]),
            "height": item.get("height", item.get("width", item["size"])),
            "visual": item.get("visual", {}),
        })
        for item in layout["sticks"]
    ]
    dpad_items = [_applied_circle(item) for item in layout["dpadButtons"]]
    face_items = [_applied_circle(item) for item in layout["faceButtons"]]

    trigger_bars = "\n".join(
        f"        {{{item['x']}, {item['y']}, {item['width']}, {item['height']}}}," for item in trigger_bar_items
    )
    shoulders = "\n".join(
        f"        {{{item['x']}, {item['y']}, {item['width']}, {item['height']}}}," for item in shoulder_items
    )
    sticks = "\n".join(
        f"        {{{item['x']}, {item['y']}, {item['width']}, {item['height']}}}," for item in stick_items
    )
    dpad = "\n".join(
        f"        {{{item['centerX']}, {item['centerY']}, {item['size']}}}," for item in dpad_items
    )
    face = "\n".join(
        f"        {{{item['centerX']}, {item['centerY']}, {item['size']}}}," for item in face_items
    )
    trigger_visuals = _render_visual_array(layout["triggerBars"])
    shoulder_visuals = _render_visual_array(layout["shoulders"])
    stick_visuals = _render_visual_array(layout["sticks"])
    dpad_visuals = _render_visual_array(layout["dpadButtons"])
    face_visuals = _render_visual_array(layout["faceButtons"])

    return f'''inline constexpr Layout {symbol_name} = {{
    {layout['deviceCanvas']['width']},
    {layout['deviceCanvas']['height']},
    {{{layout['previewFrame']['x']}, {layout['previewFrame']['y']}, {layout['previewFrame']['width']}, {layout['previewFrame']['height']}}},
    {layout['controllerSource']['width']},
    {layout['controllerSource']['height']},
    {{
{trigger_bars}
    }},
    {{
{shoulders}
    }},
    {{
{sticks}
    }},
    {{
{dpad}
    }},
    {{
{face}
    }},
    {{
{trigger_visuals}
    }},
    {{
{shoulder_visuals}
    }},
    {{
{stick_visuals}
    }},
    {{
{dpad_visuals}
    }},
    {{
{face_visuals}
    }},
}};'''


def _render_header(layout_document: dict) -> str:
    normalized_document = normalize_layout_document(layout_document)
    layout_constants = "\n\n".join(
        _render_layout_constant(symbol_name, normalized_document["layouts"][layout_key])
        for layout_key, symbol_name in LAYOUT_SYMBOLS.items()
    )
    return f'''#pragma once

namespace jc4880::joypad_layout {{

struct Rect {{
    int x;
    int y;
    int width;
    int height;
}};

struct Circle {{
    int center_x;
    int center_y;
    int size;
}};

struct Point {{
    int x;
    int y;
}};

struct Shape {{
    const char *type;
    int corner_radius;
    int rotation_degrees;
    int point_count;
    Point points[16];
}};

struct Visual {{
    bool enabled;
    const char *label;
    const char *fill_color;
    const char *border_color;
    int border_width;
    const char *text_color;
    int text_size;
    const char *text_style;
    const char *function_type;
    int preview_analog_level;
    int preview_dpad_x;
    int preview_dpad_y;
    Shape shape;
}};

struct Layout {{
    int device_canvas_width;
    int device_canvas_height;
    Rect preview_frame;
    int controller_source_width;
    int controller_source_height;
    Rect trigger_bars[2];
    Rect shoulder_indicators[2];
    Rect stick_bases[2];
    Circle dpad_buttons[4];
    Circle face_buttons[4];
    Visual trigger_bar_visuals[2];
    Visual shoulder_visuals[2];
    Visual stick_visuals[2];
    Visual dpad_visuals[4];
    Visual face_visuals[4];
}};

{layout_constants}

}} // namespace jc4880::joypad_layout
'''


def _extract_brace_block(text: str, marker: str) -> str:
    marker_index = text.find(marker)
    if marker_index < 0:
        raise ValueError(f"Could not find marker {marker!r}")

    start = text.find("{", marker_index)
    if start < 0:
        raise ValueError(f"Could not find initializer block after {marker!r}")

    depth = 0
    in_string = False
    escape = False
    for index in range(start, len(text)):
        char = text[index]
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]

    raise ValueError(f"Unterminated initializer block after {marker!r}")


def _strip_outer_braces(block: str) -> str:
    block = block.strip()
    if not (block.startswith("{") and block.endswith("}")):
        raise ValueError("Expected outer braces")
    return block[1:-1]


def _split_top_level(text: str) -> list[str]:
    parts: list[str] = []
    depth = 0
    in_string = False
    escape = False
    start = 0
    for index, char in enumerate(text):
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == "," and depth == 0:
            part = text[start:index].strip()
            if part:
                parts.append(part)
            start = index + 1
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def _parse_ints(block: str, expected_count: int) -> list[int]:
    values = [int(value) for value in re.findall(r"-?\d+", block)]
    if len(values) < expected_count:
        raise ValueError(f"Expected at least {expected_count} integers in {block!r}")
    return values[:expected_count]


def _parse_rect_array(block: str) -> list[dict[str, int]]:
    rects = []
    for entry in _split_top_level(_strip_outer_braces(block)):
        x, y, width, height = _parse_ints(entry, 4)
        rects.append({"x": x, "y": y, "width": width, "height": height})
    return rects


def _parse_circle_array(block: str) -> list[dict[str, int]]:
    circles = []
    for entry in _split_top_level(_strip_outer_braces(block)):
        center_x, center_y, size = _parse_ints(entry, 3)
        circles.append({"centerX": center_x, "centerY": center_y, "size": size})
    return circles


def _tokenize_initializer(text: str) -> list[object]:
    tokens: list[object] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char.isspace():
            index += 1
            continue
        if char in "{},":
            tokens.append(char)
            index += 1
            continue
        if char == '"':
            index += 1
            escaped = False
            value_chars: list[str] = []
            while index < length:
                current = text[index]
                if escaped:
                    value_chars.append(current)
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    break
                else:
                    value_chars.append(current)
                index += 1
            if index >= length or text[index] != '"':
                raise ValueError("Unterminated string in initializer")
            tokens.append("".join(value_chars))
            index += 1
            continue
        if char in "-0123456789":
            start = index
            index += 1
            while (index < length) and text[index].isdigit():
                index += 1
            tokens.append(int(text[start:index]))
            continue
        if char.isalpha() or char == "_":
            start = index
            index += 1
            while (index < length) and (text[index].isalnum() or text[index] == "_"):
                index += 1
            identifier = text[start:index]
            if identifier == "true":
                tokens.append(True)
            elif identifier == "false":
                tokens.append(False)
            else:
                tokens.append(identifier)
            continue
        raise ValueError(f"Unexpected token {char!r} in initializer")
    return tokens


def _parse_tokenized_value(tokens: list[object], index: int = 0) -> tuple[object, int]:
    if index >= len(tokens):
        raise ValueError("Unexpected end of initializer")

    token = tokens[index]
    if token != "{":
        return token, index + 1

    values: list[object] = []
    index += 1
    while index < len(tokens):
        token = tokens[index]
        if token == "}":
            return values, index + 1
        if token == ",":
            index += 1
            continue
        value, index = _parse_tokenized_value(tokens, index)
        values.append(value)
    raise ValueError("Unterminated brace block in initializer")


def _parse_initializer_block(block: str) -> list[object]:
    values, next_index = _parse_tokenized_value(_tokenize_initializer(block))
    if next_index <= 0:
        raise ValueError("Failed to parse initializer block")
    if not isinstance(values, list):
        raise ValueError("Expected brace-enclosed initializer list")
    return values


def _parse_visual_block(block: list[object]) -> dict:
    if len(block) < 13:
        raise ValueError("Visual block does not contain the expected number of fields")

    shape_block = block[12]
    if not isinstance(shape_block, list) or len(shape_block) < 5:
        raise ValueError("Shape block is malformed")

    points_block = shape_block[4]
    if not isinstance(points_block, list):
        raise ValueError("Shape points block is malformed")

    points: list[dict[str, int]] = []
    for point in points_block[: int(shape_block[3])]:
        if not isinstance(point, list) or len(point) < 2:
            raise ValueError("Shape point is malformed")
        points.append({"x": int(point[0]), "y": int(point[1])})

    return {
        "enabled": bool(block[0]),
        "label": str(block[1]),
        "fillColor": str(block[2]),
        "borderColor": str(block[3]),
        "borderWidth": int(block[4]),
        "textColor": str(block[5]),
        "textSize": int(block[6]),
        "textStyle": str(block[7]),
        "functionType": str(block[8]),
        "preview": {
            "analogLevel": int(block[9]),
            "dpadX": int(block[10]),
            "dpadY": int(block[11]),
        },
        "shape": {
            "type": str(shape_block[0]),
            "cornerRadius": int(shape_block[1]),
            "rotationDegrees": int(shape_block[2]),
            "points": points,
        },
    }


def _parse_visual_array(block: list[object], ids: list[str]) -> list[dict]:
    visuals: list[dict] = []
    for index, entry in enumerate(block):
        if not isinstance(entry, list):
            raise ValueError("Visual entry is malformed")
        visual = _parse_visual_block(entry)
        visual["id"] = ids[index]
        visuals.append(visual)
    return visuals


def _parse_rect_entries(block: object) -> list[dict[str, int]]:
    if not isinstance(block, list):
        raise ValueError("Rect block is malformed")
    rects: list[dict[str, int]] = []
    for entry in block:
        if not isinstance(entry, list) or len(entry) < 4:
            raise ValueError("Rect entry is malformed")
        rects.append({"x": int(entry[0]), "y": int(entry[1]), "width": int(entry[2]), "height": int(entry[3])})
    return rects


def _parse_circle_entries(block: object) -> list[dict[str, int]]:
    if not isinstance(block, list):
        raise ValueError("Circle block is malformed")
    circles: list[dict[str, int]] = []
    for entry in block:
        if not isinstance(entry, list) or len(entry) < 3:
            raise ValueError("Circle entry is malformed")
        circles.append({"centerX": int(entry[0]), "centerY": int(entry[1]), "size": int(entry[2])})
    return circles


def _parse_layout_block(layout_block: str) -> dict:
    entries = _parse_initializer_block(layout_block)
    if len(entries) < 15:
        raise ValueError("Layout header does not contain the expected geometry entries")

    preview_block = entries[2]
    if not isinstance(preview_block, list) or len(preview_block) < 4:
        raise ValueError("Preview frame block is malformed")

    trigger_items = _parse_rect_entries(entries[5])
    shoulder_items = _parse_rect_entries(entries[6])
    stick_items = _parse_rect_entries(entries[7])
    dpad_items = _parse_circle_entries(entries[8])
    face_items = _parse_circle_entries(entries[9])

    trigger_visuals = _parse_visual_array(entries[10], [f"trigger_{index}" for index in range(len(trigger_items))])
    shoulder_visuals = _parse_visual_array(entries[11], [f"shoulder_{index}" for index in range(len(shoulder_items))])
    stick_visuals = _parse_visual_array(entries[12], [f"stick_{index}" for index in range(len(stick_items))])
    dpad_visuals = _parse_visual_array(entries[13], [f"dpad_{index}" for index in range(len(dpad_items))])
    face_visuals = _parse_visual_array(entries[14], [f"face_{index}" for index in range(len(face_items))])

    trigger_bars = []
    for index, item in enumerate(trigger_items):
        trigger_bars.append({
            "id": trigger_visuals[index]["label"],
            **item,
            "visual": trigger_visuals[index],
        })

    shoulders = []
    for index, item in enumerate(shoulder_items):
        shoulders.append({
            "id": shoulder_visuals[index]["label"],
            **item,
            "visual": shoulder_visuals[index],
        })

    sticks = []
    for index, item in enumerate(stick_items):
        sticks.append({
            "id": stick_visuals[index]["label"],
            "x": item["x"],
            "y": item["y"],
            "size": item["width"],
            "width": item["width"],
            "height": item["height"],
            "visual": stick_visuals[index],
        })

    dpad_buttons = []
    for index, item in enumerate(dpad_items):
        dpad_buttons.append({
            "id": dpad_visuals[index]["label"],
            **item,
            "width": item["size"],
            "height": item["size"],
            "visual": dpad_visuals[index],
        })

    face_buttons = []
    for index, item in enumerate(face_items):
        face_buttons.append({
            "id": face_visuals[index]["label"],
            **item,
            "width": item["size"],
            "height": item["size"],
            "visual": face_visuals[index],
        })

    return {
        "deviceCanvas": {
            "width": int(entries[0]),
            "height": int(entries[1]),
        },
        "previewFrame": {
            "x": int(preview_block[0]),
            "y": int(preview_block[1]),
            "width": int(preview_block[2]),
            "height": int(preview_block[3]),
        },
        "controllerSource": {
            "width": int(entries[3]),
            "height": int(entries[4]),
        },
        "triggerBars": trigger_bars,
        "shoulders": shoulders,
        "sticks": sticks,
        "dpadButtons": dpad_buttons,
        "faceButtons": face_buttons,
    }


def read_layouts_from_header(header_path: Path) -> dict:
    text = header_path.read_text(encoding="utf-8")
    layouts: dict[str, dict] = {}
    for layout_key, symbol_name in LAYOUT_SYMBOLS.items():
        try:
            layouts[layout_key] = _parse_layout_block(_extract_brace_block(text, symbol_name))
        except ValueError:
            continue

    if "bleController" not in layouts:
        raise ValueError("Could not find BLE layout in generated header")
    if "localController" not in layouts:
        layouts["localController"] = _clone_json(layouts["bleController"])
    return {
        "selectedTarget": "bleController",
        "layouts": layouts,
    }


def _png_dimensions(image_bytes: bytes) -> tuple[int, int]:
    if image_bytes[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected a PNG file")
    if image_bytes[12:16] != b"IHDR":
        raise ValueError("PNG is missing an IHDR chunk")
    width, height = struct.unpack(">II", image_bytes[16:24])
    return width, height


def read_lvgl_png_asset(asset_path: Path) -> bytes:
    text = asset_path.read_text(encoding="utf-8")
    byte_values = [int(match, 16) for match in re.findall(r"0x([0-9A-Fa-f]{2})", text)]
    image_bytes = bytes(byte_values)
    _png_dimensions(image_bytes)
    return image_bytes


def write_lvgl_png_asset(
    image_path: Path,
    asset_path: Path,
    source_label: str = "3D/map/controller.png",
    symbol: str = "ui_img_controller_png",
) -> None:
    image_bytes = image_path.read_bytes()
    width, height = _png_dimensions(image_bytes)
    byte_lines = []
    for offset in range(0, len(image_bytes), 12):
        chunk = image_bytes[offset : offset + 12]
        byte_lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")

    contents = f'''// Generated from {source_label}
#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t {symbol}_data[] = {{
{"\n".join(byte_lines)}
}};

const lv_img_dsc_t {symbol} = {{
    .header.always_zero = 0,
    .header.w = {width},
    .header.h = {height},
    .data_size = sizeof({symbol}_data),
    .header.cf = LV_IMG_CF_RAW_ALPHA,
    .data = {symbol}_data,
}};
'''
    asset_path.write_text(contents, encoding="utf-8")


def write_header(layout_path: Path, header_path: Path) -> None:
    layout_document = _read_layout_document(layout_path)
    write_header_document(layout_document, header_path)


def write_header_document(layout_document: dict, header_path: Path) -> None:
    header_path.write_text(_render_header(normalize_layout_document(layout_document)), encoding="utf-8")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    layout_path = root / "tools" / "joypad_layout" / "joypad_layout.json"
    header_path = root / "components" / "apps" / "setting" / "joypad" / "SettingJoypadLayout.hpp"
    image_path = root / "3D" / "map" / "controller.png"
    asset_path = root / "components" / "apps" / "setting" / "ui" / "images" / "ui_img_controller_png.c"
    write_header(layout_path, header_path)
    write_lvgl_png_asset(image_path, asset_path)