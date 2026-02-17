#!/usr/bin/env python3
"""Generate C sprites from Aseprite's mini font atlas."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

BMP_SPRITE_FILES = [
    "background.bmp",
    "title.bmp",
    "ir.bmp",
    "rf.bmp",
    "lselector.bmp",
    "rselector.bmp",
]

# Aseprite mini font atlas layout:
# - ASCII starts at code 32 (' ')
# - 16 columns
# - 11px stride per cell (5x5 glyph + padding)
# - top-left glyph pixel starts at (1, 1)
ASCII_FIRST = 32
ASCII_LAST = ord("z")
GRID_COLUMNS = 16
CELL_STRIDE = 11
GLYPH_SIZE = 5
GRID_OFFSET = 1
WHITE = (255, 255, 255, 255)
BLACK = (0, 0, 0, 255)


def encode_image_columns(image: Image.Image) -> list[int]:
    width, height = image.size
    pages = (height + 7) // 8
    encoded: list[int] = []

    for page in range(pages):
        base_y = page * 8
        for x in range(width):
            value = 0
            for bit in range(8):
                y = base_y + bit
                if y >= height:
                    continue
                if image.getpixel((x, y)) == WHITE:
                    value |= 1 << bit
            encoded.append(value)
    return encoded


def c_array(name: str, values: list[int]) -> str:
    if not values:
        return f"static const uint8_t {name}[] = {{}};"

    chunks = [values[i : i + 12] for i in range(0, len(values), 12)]
    lines = [f"static const uint8_t {name}[] = {{"]
    for chunk in chunks:
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def symbol_from_filename(filename: str) -> str:
    return Path(filename).stem.replace("-", "_")


def render_bmp_section(sprites_dir: Path) -> str:
    lines: list[str] = []

    for filename in BMP_SPRITE_FILES:
        sprite_path = sprites_dir / filename
        if not sprite_path.exists():
            raise FileNotFoundError(f"Missing sprite image: {sprite_path}")

        symbol = symbol_from_filename(filename)
        with Image.open(sprite_path) as img:
            rgba = img.convert("RGBA")
            width, height = rgba.size
            data = encode_image_columns(rgba)

        lines.append(f"// array size is {len(data)}")
        lines.append(c_array(f"sprite_{symbol}_image", data))
        lines.append("")
        lines.append(f"const Sprite sprite_{symbol} = {{")
        lines.append(f"    .width = {width},")
        lines.append(f"    .height = {height},")
        lines.append(f"    .image = sprite_{symbol}_image,")
        lines.append("};")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def glyph_origin(code_point: int) -> tuple[int, int]:
    if code_point < ASCII_FIRST:
        raise ValueError(f"Unsupported code point: {code_point}, expected >= 32")
    glyph_index = code_point - ASCII_FIRST
    row = glyph_index // GRID_COLUMNS
    col = glyph_index % GRID_COLUMNS
    x = GRID_OFFSET + col * CELL_STRIDE
    y = GRID_OFFSET + row * CELL_STRIDE
    return x, y


def glyph_cell(atlas: Image.Image, code_point: int) -> Image.Image:
    x, y = glyph_origin(code_point)
    return atlas.crop((x, y, x + CELL_STRIDE, y + GLYPH_SIZE))


def glyph_width(cell: Image.Image) -> int:
    # Width is delimited by the first full-white column in glyph rows.
    for col in range(CELL_STRIDE):
        if all(cell.getpixel((col, row)) == WHITE for row in range(GLYPH_SIZE)):
            return max(1, col)
    return CELL_STRIDE


def encode_glyph_columns(cell: Image.Image, width: int) -> list[int]:
    columns: list[int] = []
    for col in range(width):
        value = 0
        for row in range(GLYPH_SIZE):
            if cell.getpixel((col, row)) == BLACK:
                value |= 1 << row
        columns.append(value)
    return columns


def render_font_section(glyphs: list[tuple[int, int, list[int]]]) -> str:
    lines: list[str] = [
        "// Aseprite mini font:",
        "// https://github.com/aseprite/aseprite/blob/efc30e24a55a01d0666358a0475af4e43fd7e2d2/data/fonts/aseprite_mini.png",
        ""
    ]

    for code_point, _, column_bytes in glyphs:
        lines.append(c_array(f"aseprite_mini_{code_point:02x}", column_bytes))

    lines.extend(
        [
            "",
            "const Sprite aseprite_mini[ASEPRITE_MINI_END - ASEPRITE_MINI_START + 1] = {",
        ]
    )
    for code_point, width, _ in glyphs:
        lines.append(
            "    "
            f"{{.width = {width}, .height = {GLYPH_SIZE}, .image = aseprite_mini_{code_point:02x}}},"
        )
    lines.extend(["};", ""])
    return "\n".join(lines)


def write_sprites_c(destination: Path, bmp_section: str, font_section: str) -> None:
    output = "//===----------------------------------------------------------------------===//\n"
    output += "///\n"
    output += "/// \\file\n"
    output += "/// Sprites y otros gráficos. Generados automáicamente con custom/sprites.py.\n"
    output += "///\n"
    output += "//===----------------------------------------------------------------------===//\n\n"
    output += '#include "modules/sprites.h"\n\n'
    output += bmp_section
    output += "\n"
    output += font_section
    destination.write_text(output)


def main() -> None:
    base_dir = Path(__file__).resolve().parent
    sprites_dir = base_dir / "sprites"
    source = sprites_dir / "aseprite_mini.png"
    repo_root = base_dir.parents[1]
    output_c = repo_root / "program" / "src" / "modules" / "sprites.c"

    if not source.exists():
        raise FileNotFoundError(f"Missing source image: {source}")
    if not sprites_dir.exists():
        raise FileNotFoundError(f"Missing sprites directory: {sprites_dir}")

    glyphs: list[tuple[int, int, list[int]]] = []
    with Image.open(source) as atlas:
        atlas = atlas.convert("RGBA")
        for code_point in range(ASCII_FIRST, ASCII_LAST + 1):
            cell = glyph_cell(atlas, code_point)
            width = glyph_width(cell)
            columns = encode_glyph_columns(cell, width)
            glyphs.append((code_point, width, columns))

    bmp_section = render_bmp_section(sprites_dir)
    font_section = render_font_section(glyphs)
    write_sprites_c(output_c, bmp_section, font_section)
    print(f"Wrote {output_c}")


if __name__ == "__main__":
    main()
