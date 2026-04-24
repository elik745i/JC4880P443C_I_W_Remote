import argparse
from pathlib import Path

from PIL import Image


HEADER = """#ifdef __has_include
    #if __has_include(\"lvgl.h\")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include \"lvgl.h\"
#else
    #include \"lvgl/lvgl.h\"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_CONST
#endif

#ifndef {attribute_macro}
#define {attribute_macro}
#endif

"""


def chunked(values, chunk_size):
    for index in range(0, len(values), chunk_size):
        yield values[index:index + chunk_size]


def format_bytes(values):
    lines = []
    for chunk in chunked(values, 12):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def pixel_bytes_rgb565(image, swap_bytes):
    output = []
    for red, green, blue, alpha in image.getdata():
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        low = pixel & 0xFF
        high = (pixel >> 8) & 0xFF
        if swap_bytes:
            output.extend((high, low, alpha))
        else:
            output.extend((low, high, alpha))
    return output


def pixel_bytes_rgba8888(image):
    output = []
    for red, green, blue, alpha in image.getdata():
        output.extend((blue, green, red, alpha))
    return output


def write_lvgl_asset(input_path: Path, output_path: Path, symbol: str):
    image = Image.open(input_path).convert("RGBA")
    width, height = image.size
    attribute_macro = f"LV_ATTRIBUTE_IMG_{symbol.upper()}"

    rgb565 = format_bytes(pixel_bytes_rgb565(image, swap_bytes=False))
    rgb565_swap = format_bytes(pixel_bytes_rgb565(image, swap_bytes=True))
    rgba8888 = format_bytes(pixel_bytes_rgba8888(image))

    contents = HEADER.format(attribute_macro=attribute_macro)
    contents += f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {attribute_macro} uint8_t {symbol}_map[] = {{\n"
    contents += "#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0\n"
    contents += "  /* Pixel format: RGB565 with alpha byte */\n"
    contents += rgb565 + "\n"
    contents += "#elif LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0\n"
    contents += "  /* Pixel format: RGB565 swapped with alpha byte */\n"
    contents += rgb565_swap + "\n"
    contents += "#else\n"
    contents += "  /* Pixel format: BGRA8888 */\n"
    contents += rgba8888 + "\n"
    contents += "#endif\n"
    contents += "};\n\n"
    contents += f"const lv_img_dsc_t {symbol} = {{\n"
    contents += "  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n"
    contents += "  .header.always_zero = 0,\n"
    contents += "  .header.reserved = 0,\n"
    contents += f"  .header.w = {width},\n"
    contents += f"  .header.h = {height},\n"
    contents += f"  .data_size = {width * height} * LV_IMG_PX_SIZE_ALPHA_BYTE,\n"
    contents += f"  .data = {symbol}_map,\n"
    contents += "};\n"

    output_path.write_text(contents, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--symbol", required=True)
    args = parser.parse_args()

    write_lvgl_asset(Path(args.input), Path(args.output), args.symbol)


if __name__ == "__main__":
    main()