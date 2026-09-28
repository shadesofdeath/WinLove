"""Replace <text> elements in an SVG with outlined <path>s using the bundled fonts.

Usage: python tools/outline_text.py <in.svg> <out.svg>

Supports the attributes the handoff uses: x, y, font-family, font-size, font-weight,
letter-spacing, text-anchor (start|middle|end), fill. Font is picked from resources/fonts.
"""
import re
import sys
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "resources" / "fonts"

WEIGHT_FILES = {
    ("sans", 400): "IBMPlexSans-Regular.ttf",
    ("sans", 500): "IBMPlexSans-Medium.ttf",
    ("sans", 600): "IBMPlexSans-SemiBold.ttf",
    ("mono", 400): "JetBrainsMono-Regular.ttf",
}

TEXT_RE = re.compile(r"<text([^>]*)>([^<]*)</text>")
ATTR_RE = re.compile(r'([\w-]+)="([^"]*)"')


def load_font(family: str, weight: int) -> TTFont:
    kind = "mono" if "Mono" in family else "sans"
    name = WEIGHT_FILES.get((kind, weight)) or WEIGHT_FILES[(kind, 400)]
    return TTFont(FONTS / name)


def outline(attrs: dict, text: str) -> str:
    font = load_font(attrs.get("font-family", ""), int(attrs.get("font-weight", "400")))
    size = float(attrs["font-size"])
    spacing = float(attrs.get("letter-spacing", "0"))
    scale = size / font["head"].unitsPerEm
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    hmtx = font["hmtx"]

    names = [cmap[ord(c)] for c in text]
    advances = [hmtx[n][0] * scale + spacing for n in names]
    width = sum(advances) - spacing
    x = float(attrs.get("x", "0"))
    anchor = attrs.get("text-anchor", "start")
    if anchor == "middle":
        x -= width / 2
    elif anchor == "end":
        x -= width
    y = float(attrs.get("y", "0"))

    pen = SVGPathPen(glyphs, ntos=lambda v: f"{v:.2f}".rstrip("0").rstrip("."))
    for name, adv in zip(names, advances):
        # font units are y-up; SVG is y-down
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
        x += adv
    fill = attrs.get("fill", "currentColor")
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


def main() -> None:
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    svg = src.read_text(encoding="utf-8")
    svg = re.sub(r"<metadata>.*?</metadata>", "", svg, flags=re.S)
    svg = re.sub(r'\s*xmlns:c2pa="[^"]*"', "", svg)
    svg = TEXT_RE.sub(lambda m: outline(dict(ATTR_RE.findall(m.group(1))), m.group(2)), svg)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(svg, encoding="utf-8")
    print(f"{src.name} -> {dst}")


if __name__ == "__main__":
    main()
