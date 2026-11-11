#!/usr/bin/env python3
import argparse
import io
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw, ImageFont

LETTERS = "OPENSTRAMDCK"


def load_font(path: Path, weight: int, size: int) -> ImageFont.FreeTypeFont:
    font = TTFont(path)
    if "fvar" in font:
        font = instancer.instantiateVariableFont(font, {"wght": weight})
    font.flavor = None
    data = io.BytesIO()
    font.save(data)
    data.seek(0)
    return ImageFont.truetype(data, size)


def render(font: ImageFont.FreeTypeFont, letter: str, top: int, height: int):
    left, _, right, _ = font.getbbox(letter)
    image = Image.new("L", (right - left + 4, height + 4), 0)
    ImageDraw.Draw(image).text((2 - left, 2 - top), letter, font=font, fill=255)
    box = image.getbbox()
    image = image.crop((box[0], 0, box[2], image.height))
    rows = [y for y in range(image.height) if any(image.getpixel((x, y)) for x in range(image.width))]
    return image, rows[0], rows[-1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path)
    parser.add_argument("--weight", type=int, default=600)
    parser.add_argument("--size", type=int, default=68)
    parser.add_argument("--output", type=Path, default=Path("include/boot_glyphs.h"))
    args = parser.parse_args()

    font = load_font(args.font, args.weight, args.size)
    top = min(font.getbbox(c)[1] for c in LETTERS)
    bottom = max(font.getbbox(c)[3] for c in LETTERS)
    images = [render(font, c, top, bottom - top) for c in LETTERS]
    first = min(i[1] for i in images)
    last = max(i[2] for i in images)
    height = last - first + 1

    widths, offsets, nibbles = [], [], []
    for image, _, _ in images:
        offsets.append(len(nibbles))
        widths.append(image.width)
        for y in range(first, last + 1):
            for x in range(image.width):
                nibbles.append((image.getpixel((x, y)) * 15 + 127) // 255)
    if len(nibbles) % 2:
        nibbles.append(0)
    packed = [(nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2)]

    lines = [
        "#ifndef AKP_BOOT_GLYPHS_H",
        "#define AKP_BOOT_GLYPHS_H",
        "#include <stdint.h>",
        "",
        f"#define BOOT_GLYPH_HEIGHT {height}u",
        f"#define BOOT_GLYPH_MAX_WIDTH {max(widths)}u",
        "",
        "static const uint8_t boot_glyph_width[] = {"
        + ", ".join(str(w) for w in widths)
        + "};",
        "",
        "static const uint32_t boot_glyph_offset[] = {"
        + ", ".join(str(o) for o in offsets)
        + "};",
        "",
        "static const uint8_t boot_glyph_alpha[] = {",
    ]
    for i in range(0, len(packed), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in packed[i : i + 16]) + ",")
    lines += ["};", "#endif", ""]
    args.output.write_text("\n".join(lines), newline="\n")
    print(f"{args.output}: height {height}, widths {widths}, {len(packed)} bytes")


if __name__ == "__main__":
    main()
