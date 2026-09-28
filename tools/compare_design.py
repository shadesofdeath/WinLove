"""Side-by-side + diff of a design screen vs WinLove's own offscreen render.

Usage:
  python tools/compare_design.py 01-welcome-source [--theme=dark|light] [--crop=x,y,w,h] [--zoom=2]
         [--exe=build/x64-debug/bin/WinLove.exe] [-- extra WinLove args]

Output: build/visual/<screen>-<theme>.png — three rows: design, WinLove, difference (bright = differs).
The design SVG is 1440×900 at 100%; WinLove renders the same size at scale 1.
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageOps
from playwright.sync_api import sync_playwright

from browser import launch

ROOT = Path(__file__).resolve().parents[1]
SCREENS = ROOT / "WinLove-UI-Handoff" / "04_screens"
OUT = ROOT / "build" / "visual"


def render_design(svg_path: Path, png_path: Path) -> None:
    svg = svg_path.read_text(encoding="utf-8").replace("<svg ", '<svg width="1440" height="900" ', 1)
    with sync_playwright() as p:
        browser = launch(p)
        page = browser.new_page(device_scale_factor=1)
        page.set_viewport_size({"width": 1440, "height": 900})
        page.set_content(f'<body style="margin:0">{svg}</body>')
        page.screenshot(path=str(png_path))
        browser.close()


def main() -> int:
    args = sys.argv[1:]
    extra = args[args.index("--") + 1:] if "--" in args else []
    args = args[:args.index("--")] if "--" in args else args
    screen = args[0]
    opts = dict(a[2:].split("=", 1) for a in args[1:] if a.startswith("--") and "=" in a)
    theme = opts.get("theme", "dark")
    zoom = int(opts.get("zoom", "1"))
    exe = opts.get("exe", str(ROOT / "build" / "x64-debug" / "bin" / "WinLove.exe"))

    OUT.mkdir(parents=True, exist_ok=True)
    design_png = OUT / f"{screen}-{theme}.design.png"
    ours_png = OUT / f"{screen}-{theme}.winlove.png"
    render_design(SCREENS / f"{screen}-{theme}.svg", design_png)
    result = subprocess.run([exe, f"--render={ours_png}", f"--theme={theme}", "--size=1440x900", *extra])
    if result.returncode != 0:
        print("WinLove render failed", file=sys.stderr)
        return 1

    design = Image.open(design_png).convert("RGB")
    ours = Image.open(ours_png).convert("RGB")
    if "crop" in opts:
        x, y, w, h = (int(v) for v in opts["crop"].split(","))
        design, ours = design.crop((x, y, x + w, y + h)), ours.crop((x, y, x + w, y + h))
    diff = ImageOps.autocontrast(ImageChops.difference(design, ours).convert("L")).convert("RGB")

    w, h = design.size
    sheet = Image.new("RGB", (w, h * 3 + 4), (255, 0, 255))
    for row, image in enumerate((design, ours, diff)):
        sheet.paste(image, (0, row * (h + 2)))
    if zoom > 1:
        sheet = sheet.resize((sheet.width * zoom, sheet.height * zoom), Image.NEAREST)
    out = OUT / f"{screen}-{theme}.png"
    sheet.save(out)
    print(f"design | winlove | diff -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
