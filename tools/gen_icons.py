"""Icon set -> src/ui/generated/Icons.g.h

Sources:
- resources/icons/icon-map.json : D-092 — the handoff's icon names mapped onto Lucide icons
                                  (third_party/lucide/icons/*.svg, ISC), plus "extra" names;
                                  "keep" names stay the handoff's (window caption glyphs).
- WinLove-UI-Handoff/02_icons   : the kept icons' 16 / 24 px path data and filled variants.
- resources/brand/logo-mark.svg : the brand mark, drawn through the same path as every icon.

Each variant becomes one SVG path-data string (sub-paths joined), which wl::ui parses into an
ID2D1PathGeometry. Lucide draws on a 24 grid: the 16 px variant is the same drawing scaled by 2/3
(arc radii and end points scaled, arc flags and rotation kept), its stroke scaled with it.
"""
import json
import re

from genutil import ROOT, HEADER, cpp_float, cpp_string, pascal, write_if_changed

ICONS = ROOT / "WinLove-UI-Handoff" / "02_icons"
MAP = ROOT / "resources" / "icons" / "icon-map.json"
LUCIDE = ROOT / "third_party" / "lucide" / "icons"
BRAND_MARK = ROOT / "resources" / "brand" / "logo-mark.svg"
DST = ROOT / "src" / "ui" / "generated" / "Icons.g.h"

SVG_ATTR = re.compile(r'<svg([^>]*)>')
PATH_D = re.compile(r'<path[^>]*\sd="([^"]+)"')
ATTR = re.compile(r'([\w-]+)="([^"]*)"')
ELEMENT = re.compile(r'<(path|circle|ellipse|rect|line|polyline|polygon)\b([^>]*?)/?>', re.S)
STROKE_24 = 1.5  # icons.md: "24 set: koordinatlar ×1.5, stroke 1.5"
FORBIDDEN = re.compile(r"<(filter|mask|clipPath|text|image|style|use|defs)\b")
NUMBER = re.compile(r"\s*,?\s*(-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)")


def read_filled(name: str):
    svg = (ICONS / "16" / "filled" / f"{name}.svg").read_text(encoding="utf-8")
    svg = re.sub(r"<metadata>.*?</metadata>", "", svg, flags=re.S)
    if FORBIDDEN.search(svg):
        raise SystemExit(f"icon {name}: forbidden SVG element")
    attrs = dict(ATTR.findall(SVG_ATTR.search(svg).group(1)))
    filled = attrs.get("fill", "none") != "none"
    stroke = float(attrs.get("stroke-width", "1.25"))
    return " ".join(PATH_D.findall(svg)), stroke, filled


def fmt(v: float) -> str:
    s = f"{round(v, 4):g}"
    return "0" if s == "-0" else s


def scale_path(d: str, k: float) -> str:
    """Every coordinate of a path times k; in arcs the radii and end point, not the rotation or flags."""
    out = []
    i = 0
    cmd = None
    while i < len(d):
        c = d[i]
        if c.isalpha():
            cmd = c
            out.append(c)
            i += 1
            continue
        if c in " ,\t\r\n":
            i += 1
            continue
        if cmd is None:
            raise SystemExit(f"path data starts with a number: {d[:30]}")
        if cmd in "Aa":
            args = []
            for slot in range(7):
                if slot in (3, 4):  # flags: one character each, separators optional
                    while i < len(d) and d[i] in " ,\t\r\n":
                        i += 1
                    if i >= len(d) or d[i] not in "01":
                        raise SystemExit(f"bad arc flag in {d[:40]}")
                    args.append(d[i])
                    i += 1
                    continue
                m = NUMBER.match(d, i)
                if not m:
                    raise SystemExit(f"bad arc in {d[:40]}")
                v = float(m.group(1))
                args.append(fmt(v) if slot == 2 else fmt(v * k))
                i = m.end()
            out.append(" ".join(args))
            continue
        m = NUMBER.match(d, i)
        if not m:
            raise SystemExit(f"bad number at {d[i:i + 20]!r}")
        out.append(fmt(float(m.group(1)) * k))
        i = m.end()
    # Letters stand alone, numbers separated by spaces.
    text = ""
    for token in out:
        if text and not (token[0].isalpha() or text[-1].isalpha()):
            text += " "
        text += token
    return text


def element_path(kind: str, attrs: dict) -> str:
    f = lambda key, default=0.0: float(attrs.get(key, default))  # noqa: E731
    if kind == "path":
        # A path's first moveto is absolute even when written "m"; joined after another element's
        # drawing it would turn relative, so it is spelled "M".
        # The pairs after it stay relative line-tos ("m6 9 6 6" is M6 9 l6 6).
        d = attrs["d"].strip()
        if not d.startswith("m"):
            return d
        x = NUMBER.match(d, 1)
        y = NUMBER.match(d, x.end())
        rest = d[y.end():]
        follows = NUMBER.match(rest)
        return f"M{x.group(1)} {y.group(1)}" + ("l" + rest.lstrip(" ,") if follows else rest)
    if kind in ("circle", "ellipse"):
        cx, cy = f("cx"), f("cy")
        rx = f("r") if kind == "circle" else f("rx")
        ry = f("r") if kind == "circle" else f("ry")
        return (f"M{fmt(cx - rx)} {fmt(cy)}a{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(2 * rx)} 0"
                f"a{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(-2 * rx)} 0z")
    if kind == "rect":
        x, y, w, h = f("x"), f("y"), f("width"), f("height")
        rx = f("rx", attrs.get("ry", 0))
        ry = f("ry", attrs.get("rx", 0))
        if rx <= 0:
            return f"M{fmt(x)} {fmt(y)}H{fmt(x + w)}V{fmt(y + h)}H{fmt(x)}z"
        return (f"M{fmt(x + rx)} {fmt(y)}H{fmt(x + w - rx)}A{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(x + w)} {fmt(y + ry)}"
                f"V{fmt(y + h - ry)}A{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(x + w - rx)} {fmt(y + h)}"
                f"H{fmt(x + rx)}A{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(x)} {fmt(y + h - ry)}"
                f"V{fmt(y + ry)}A{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(x + rx)} {fmt(y)}z")
    if kind == "line":
        return f"M{fmt(f('x1'))} {fmt(f('y1'))}L{fmt(f('x2'))} {fmt(f('y2'))}"
    points = [float(v) for v in re.split(r"[\s,]+", attrs["points"].strip()) if v]
    pairs = [f"{fmt(points[j])} {fmt(points[j + 1])}" for j in range(0, len(points) - 1, 2)]
    return "M" + " L".join(pairs) + ("z" if kind == "polygon" else "")


def lucide(name: str) -> str:
    svg = (LUCIDE / f"{name}.svg").read_text(encoding="utf-8")
    if FORBIDDEN.search(svg):
        raise SystemExit(f"lucide {name}: forbidden SVG element")
    attrs = dict(ATTR.findall(SVG_ATTR.search(svg).group(1)))
    if attrs.get("viewBox") != "0 0 24 24":
        raise SystemExit(f"lucide {name}: unexpected viewBox")
    paths = [element_path(kind, dict(ATTR.findall(body))) for kind, body in ELEMENT.findall(svg)]
    if not paths:
        raise SystemExit(f"lucide {name}: no drawing")
    return " ".join(paths)


def brand_mark() -> dict:
    svg = BRAND_MARK.read_text(encoding="utf-8")
    paths = PATH_D.findall(svg)
    stroke = float(dict(ATTR.findall(SVG_ATTR.search(svg).group(1)))["stroke-width"])
    return {"viewBox": "0 0 16 16", "viewBox24": "0 0 24 24", "strokeWidth": stroke, "paths": paths,
            "paths24": [scale_path(p, 1.5) for p in paths], "filled": (" ".join(paths), stroke, False)}


def variants() -> dict:
    """name -> (regular16, stroke16, filled16, fstroke, ffill, regular24, stroke24)."""
    mapping = json.loads(MAP.read_text(encoding="utf-8"))
    weight = float(mapping["strokeWidth"])  # on Lucide's 24 grid
    handoff = json.loads((ICONS / "icons-paths.json").read_text(encoding="utf-8"))
    handoff["brand-mark"] = brand_mark()
    result = {}
    for name in mapping["keep"]:
        d = handoff[name]
        if d["viewBox"] != "0 0 16 16" or d["viewBox24"] != "0 0 24 24":
            raise SystemExit(f"icon {name}: unexpected viewBox")
        f16, fstroke, ffill = d["filled"] if "filled" in d else read_filled(name)
        result[name] = (" ".join(d["paths"]), d["strokeWidth"], f16, fstroke, ffill, " ".join(d["paths24"]), STROKE_24)
    for name, source in {**mapping["map"], **mapping["extra"]}.items():
        d24 = lucide(source)
        d16 = scale_path(d24, 16 / 24)
        s16 = weight * 16 / 24
        result[name] = (d16, s16, d16, s16, False, d24, weight)
    unmapped = sorted(set(handoff) - set(result))
    if unmapped:
        raise SystemExit(f"icon-map.json: handoff icons without an icon: {unmapped}")
    return result


def generate() -> str:
    data = variants()
    names = sorted(data)
    out = [HEADER.format(src="resources/icons/icon-map.json (Lucide) + WinLove-UI-Handoff/02_icons", tool="gen_icons.py"),
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
        r16, sw, f16, fstroke, ffill, r24, sw24 = data[n]
        out.append(f'    {{"{n}",\n'
                   f'     {{"{cpp_string(r16)}", {cpp_float(sw)}, false}},\n'
                   f'     {{"{cpp_string(f16)}", {cpp_float(fstroke)}, {"true" if ffill else "false"}}},\n'
                   f'     {{"{cpp_string(r24)}", {cpp_float(sw24)}, false}}}},')
    out += ["}};", "", "} // namespace wl::ui::icons", ""]
    return "\n".join(out)


if __name__ == "__main__":
    write_if_changed(DST, generate())
