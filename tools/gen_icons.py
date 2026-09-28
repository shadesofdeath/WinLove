"""Icon set (design handoff) -> src/ui/generated/Icons.g.h

Sources:
- 02_icons/icons-paths.json : regular 16px and 24px path data (stroke icons)
- 02_icons/16/filled/*.svg  : filled 16px variant (fill+stroke evenodd, or heavier stroke)

Plus the brand mark (resources/brand/logo-mark.svg) as icon "brand-mark", so the title bar
draws it through the same path as every other icon.

Each variant becomes one SVG path-data string (sub-paths joined), which wl::ui parses into
an ID2D1PathGeometry.
"""
import json
import re

from genutil import ROOT, HEADER, cpp_float, cpp_string, pascal, write_if_changed

ICONS = ROOT / "WinLove-UI-Handoff" / "02_icons"
BRAND_MARK = ROOT / "resources" / "brand" / "logo-mark.svg"
DST = ROOT / "src" / "ui" / "generated" / "Icons.g.h"

SVG_ATTR = re.compile(r'<svg([^>]*)>')
PATH_D = re.compile(r'<path[^>]*\sd="([^"]+)"')
ATTR = re.compile(r'([\w-]+)="([^"]*)"')
STROKE_24 = 1.5  # icons.md: "24 set: koordinatlar ×1.5, stroke 1.5"
FORBIDDEN = re.compile(r"<(filter|mask|clipPath|text|image|style|use|defs)\b")


def read_filled(name: str):
    svg = (ICONS / "16" / "filled" / f"{name}.svg").read_text(encoding="utf-8")
    svg = re.sub(r"<metadata>.*?</metadata>", "", svg, flags=re.S)
    if FORBIDDEN.search(svg):
        raise SystemExit(f"icon {name}: forbidden SVG element")
    attrs = dict(ATTR.findall(SVG_ATTR.search(svg).group(1)))
    filled = attrs.get("fill", "none") != "none"
    stroke = float(attrs.get("stroke-width", "1.25"))
    return " ".join(PATH_D.findall(svg)), stroke, filled


def scale_path(d: str, k: float) -> str:
    """Scale every coordinate of a path without arcs (arc flags must not be scaled)."""
    if re.search(r"[aA]", d):
        raise SystemExit("scale_path: arcs not supported")
    return re.sub(r"-?\d*\.?\d+", lambda m: f"{float(m.group()) * k:g}", d)


def brand_mark() -> dict:
    svg = BRAND_MARK.read_text(encoding="utf-8")
    paths = PATH_D.findall(svg)
    stroke = float(dict(ATTR.findall(SVG_ATTR.search(svg).group(1)))["stroke-width"])
    return {"viewBox": "0 0 16 16", "viewBox24": "0 0 24 24", "strokeWidth": stroke, "paths": paths,
            "paths24": [scale_path(p, 1.5) for p in paths], "filled": (" ".join(paths), stroke, False)}


def generate() -> str:
    data = json.loads((ICONS / "icons-paths.json").read_text(encoding="utf-8"))
    data["brand-mark"] = brand_mark()
    names = sorted(data)
    out = [HEADER.format(src="WinLove-UI-Handoff/02_icons", tool="gen_icons.py"),
           "#pragma once", "#include <array>", "#include <cstdint>", "",
           "namespace wl::ui::icons {", "",
           "enum class Icon : std::uint16_t {"]
    out += [f"    {pascal(n)}," for n in names]
    out += ["    Count", "};", "",
            "struct IconVariant {",
            "    const char* pathData; // SVG path data, viewBox 0 0 <size> <size>",
            "    float strokeWidth;    // round cap/join",
            "    bool filled;          // fill with evenodd rule in addition to the stroke",
            "};",
            "struct IconDef {",
            "    const char* name;",
            "    IconVariant regular16;",
            "    IconVariant filled16;",
            "    IconVariant regular24;",
            "};", "",
            f"inline constexpr std::size_t kIconCount = {len(names)};",
            "inline constexpr std::array<IconDef, kIconCount> kIcons = {{"]
    for n in names:
        d = data[n]
        if d["viewBox"] != "0 0 16 16" or d["viewBox24"] != "0 0 24 24":
            raise SystemExit(f"icon {n}: unexpected viewBox")
        r16 = " ".join(d["paths"])
        r24 = " ".join(d["paths24"])
        f16, fstroke, ffill = d["filled"] if "filled" in d else read_filled(n)
        sw = d["strokeWidth"]
        out.append(f'    {{"{n}",\n'
                   f'     {{"{cpp_string(r16)}", {cpp_float(sw)}, false}},\n'
                   f'     {{"{cpp_string(f16)}", {cpp_float(fstroke)}, {"true" if ffill else "false"}}},\n'
                   f'     {{"{cpp_string(r24)}", {cpp_float(STROKE_24)}, false}}}},')
    out += ["}};", "", "} // namespace wl::ui::icons", ""]
    return "\n".join(out)


if __name__ == "__main__":
    write_if_changed(DST, generate())
