"""Rebuild the embedded masks from the approved, transparent PNG icon assets."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


NAMES = (
    "shape", "arrow", "pen", "marker", "mosaic", "text", "eraser",
    "undo", "redo", "close", "pin", "save", "copy", "size", "position",
    "color", "lock", "center", "refresh", "create", "material",
    "settings", "reset",
)
SIDE = 96
ROOT = Path(__file__).resolve().parent
HEADER = ROOT.parent / "icon_art_data.h"


def runs_for(name: str) -> list[int]:
    image = Image.open(ROOT / f"{name}.png").convert("RGBA")
    if image.size != (SIDE, SIDE):
        raise ValueError(f"{name}: expected {SIDE}x{SIDE} PNG")
    pixels = list(image.get_flattened_data())
    if any((r, g, b) != (0, 0, 0) for r, g, b, alpha in pixels if alpha):
        raise ValueError(f"{name}: nonblack icon ink")
    values = [alpha for _, _, _, alpha in pixels]
    result: list[int] = []
    index = 0
    while index < len(values):
        end = index + 1
        while end < len(values) and values[end] == values[index]:
            end += 1
        result.extend((end - index, values[index]))
        index = end
    return result


def render_header() -> str:
    ranges: list[tuple[int, int]] = []
    packed: list[int] = []
    for name in NAMES:
        encoded = runs_for(name)
        ranges.append((len(packed), len(encoded)))
        packed.extend(encoded)
    lines = [
        "// Generated from icons/*.png by icons/generate_png_masks.py. Do not edit.",
        "#pragma once",
        "#include <cstdint>",
        "namespace lume::platform::windows::icon_art::generated {",
        f"inline constexpr int MaskSide = {SIDE};",
        "struct Range { std::uint32_t offset, length; };",
        "inline constexpr Range Ranges[] = {" + ",".join(f"{{{a},{b}}}" for a, b in ranges) + "};",
        "inline constexpr std::uint16_t Runs[] = {",
    ]
    for offset in range(0, len(packed), 32):
        lines.append("  " + ",".join(map(str, packed[offset:offset + 32])) + ",")
    lines += ["};", "}"]
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render_header()
    if args.check:
        if not HEADER.exists() or HEADER.read_text(encoding="utf-8") != expected:
            raise SystemExit("icon_art_data.h differs from approved PNG assets")
    else:
        HEADER.write_text(expected, encoding="utf-8")
    print(f"{len(NAMES)} PNG icons, generated header {'verified' if args.check else 'written'}")


if __name__ == "__main__":
    main()
