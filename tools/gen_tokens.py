"""tokens.json (design handoff) -> src/ui/generated/Tokens.g.h

The header has no Windows/D2D dependency: colors are 0xAARRGGBB, sizes are DIP floats.
wl::ui::Theme converts them to D2D types at runtime.
"""
import json
from pathlib import Path

from genutil import ROOT, HEADER, cpp_float, pascal, write_if_changed

SRC = ROOT / "WinLove-UI-Handoff" / "01_tokens" / "tokens.json"
DST = ROOT / "src" / "ui" / "generated" / "Tokens.g.h"
THEMES = [("dark", "kDark"), ("light", "kLight"), ("hc", "kHighContrast")]


def argb(hex_color: str) -> str:
    h = hex_color.lstrip("#")
    rgb, a = h[:6], (h[6:8] if len(h) == 8 else "FF")
    return f"0x{a.upper()}{rgb.upper()}u"


def num(v) -> str:
    return cpp_float(v)


def generate() -> str:
    t = json.loads(SRC.read_text(encoding="utf-8"))
    colors = t["color"]
    names = list(colors["dark"].keys())
    for theme, _ in THEMES:
        if list(colors[theme].keys()) != names:
            raise SystemExit(f"tokens.json: color keys of '{theme}' differ from 'dark'")
    enum_of = {n: pascal(n) for n in names}

    out = [HEADER.format(src="WinLove-UI-Handoff/01_tokens/tokens.json", tool="gen_tokens.py"),
           "#pragma once", "#include <array>", "#include <cstdint>", "",
           "namespace wl::ui::tokens {", "",
           f'inline constexpr const char* kVersion = "{t["meta"]["version"]}";', "",
           "// ---- Color -------------------------------------------------------------",
           "enum class Color : std::uint8_t {"]
    out += [f"    {enum_of[n]}," for n in names]
    out += ["    Count", "};",
            f"inline constexpr std::size_t kColorCount = {len(names)};",
            "inline constexpr std::array<const char*, kColorCount> kColorNames = {"]
    out += [f'    "{n}",' for n in names]
    out += ["};", "// 0xAARRGGBB"]
    for theme, var in THEMES:
        out.append(f"inline constexpr std::array<std::uint32_t, kColorCount> {var} = {{")
        out += [f"    {argb(colors[theme][n])}, // {n}" for n in names]
        out.append("};")

    ty = t["typography"]
    fam = ty["family"]
    out += ["", "// ---- Typography --------------------------------------------------------",
            "namespace font {",
            f'inline constexpr const wchar_t* kUi = L"{fam["ui"]}";',
            f'inline constexpr const wchar_t* kUiFallback = L"{fam["uiFallback"]}";',
            f'inline constexpr const wchar_t* kMono = L"{fam["mono"]}";',
            f'inline constexpr const wchar_t* kMonoFallback = L"{fam["monoFallback"]}";',
            "} // namespace font",
            "enum class FontRole : std::uint8_t { Ui, Mono };",
            "struct TypeSpec {", "    FontRole role;", "    float size;", "    float lineHeight;",
            "    std::uint16_t weight;", "    float letterSpacing;", "    bool uppercase;", "};",
            "enum class TypeStyle : std::uint8_t {"]
    styles = ty["styles"]
    out += [f"    {pascal(s)}," for s in styles]
    out += ["    Count", "};",
            f"inline constexpr std::array<TypeSpec, {len(styles)}> kTypeStyles = {{{{"]
    for name, s in styles.items():
        role = "FontRole::Mono" if s.get("family") == "mono" else "FontRole::Ui"
        upper = "true" if s.get("transform") == "uppercase" else "false"
        out.append(f"    {{{role}, {num(s['size'])}, {num(s['lineHeight'])}, {s['weight']}, "
                   f"{num(s['letterSpacing'])}, {upper}}}, // {name}")
    out.append("}};")

    def float_ns(ns: str, values: dict) -> None:
        out.extend(["", f"namespace {ns} {{"])
        for k, v in values.items():
            if isinstance(v, list):
                out.append(f"inline constexpr float {k}W = {num(v[0])};")
                out.append(f"inline constexpr float {k}H = {num(v[1])};")
            else:
                out.append(f"inline constexpr float {k} = {num(v)};")
        out.append(f"}} // namespace {ns}")

    out.append("")
    out.append("// ---- Metrics (DIP) -----------------------------------------------------")
    float_ns("spacing", t["spacing"])
    float_ns("radius", t["radius"])
    float_ns("size", t["size"])
    float_ns("opacity", t["opacity"])

    out += ["", "// ---- Elevation ---------------------------------------------------------",
            "struct Shadow { float offsetX; float offsetY; float blurRadius; Color color; };",
            "namespace elevation {"]
    for level, layers in t["elevation"].items():
        if not layers:
            continue
        items = ", ".join(f"Shadow{{{num(l['offsetX'])}, {num(l['offsetY'])}, {num(l['blurRadius'])}, "
                          f"Color::{enum_of[l['color']]}}}" for l in layers)
        out.append(f"inline constexpr std::array<Shadow, {len(layers)}> {level} = {{{{{items}}}}};")
    out.append("} // namespace elevation")

    m = t["motion"]
    out += ["", "// ---- Motion ------------------------------------------------------------",
            "struct CubicBezier { float x1; float y1; float x2; float y2; };",
            "struct Spring { float stiffness; float damping; float mass; };",
            "namespace motion {"]
    out += [f"inline constexpr float {k}Ms = {num(v)};" for k, v in m["durations"].items()]
    for k, v in m["easings"].items():
        if isinstance(v, list):
            out.append(f"inline constexpr CubicBezier {k} = {{{', '.join(num(x) for x in v)}}};")
        else:
            out.append(f"inline constexpr Spring {k} = {{{num(v['stiffness'])}, {num(v['damping'])}, {num(v['mass'])}}};")
    out.append("} // namespace motion")

    out += ["", "namespace zorder {"]
    out += [f"inline constexpr int {k} = {v};" for k, v in t["zOrder"].items()]
    out += ["} // namespace zorder", "", "} // namespace wl::ui::tokens", ""]
    return "\n".join(out)


if __name__ == "__main__":
    write_if_changed(DST, generate())
