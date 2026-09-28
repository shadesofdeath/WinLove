"""Build app brand resources from the design handoff.

- resources/brand/*.svg : cleaned copies (metadata stripped, <text> outlined)
- resources/brand/WinLove.ico : multi-size icon, each size from its own pixel-tuned SVG

Usage: python tools/build_brand.py
Needs: fonttools, pillow, playwright (bundled Chromium or installed Chrome/Edge).
"""
import io
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image
from playwright.sync_api import sync_playwright

from browser import launch as browser_launch

ROOT = Path(__file__).resolve().parents[1]
BRAND_SRC = ROOT / "WinLove-UI-Handoff" / "00_brand"
BRAND_DST = ROOT / "resources" / "brand"
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def clean(svg: str) -> str:
    svg = re.sub(r"<metadata>.*?</metadata>", "", svg, flags=re.S)
    return re.sub(r'\s*xmlns:c2pa="[^"]*"', "", svg)


def copy_svgs() -> None:
    BRAND_DST.mkdir(parents=True, exist_ok=True)
    for name in ["logo-mark.svg", "logo-mono-light.svg", "logo-mono-dark.svg", "app-icon.svg"]:
        (BRAND_DST / name).write_text(clean((BRAND_SRC / name).read_text(encoding="utf-8")), encoding="utf-8")
    subprocess.run([sys.executable, str(ROOT / "tools" / "outline_text.py"),
                    str(BRAND_SRC / "logo-full.svg"), str(BRAND_DST / "logo-full.svg")], check=True)


def rasterize(page, svg: str, size: int) -> Image.Image:
    page.set_viewport_size({"width": size, "height": size})
    page.set_content(
        "<html><body style='margin:0;background:transparent'>"
        f"<div style='width:{size}px;height:{size}px'>{svg}</div>"
        f"<style>svg{{width:{size}px;height:{size}px;display:block}}</style></body></html>")
    png = page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": size, "height": size})
    return Image.open(io.BytesIO(png)).convert("RGBA")


def build_ico() -> None:
    with sync_playwright() as p:
        browser = browser_launch(p)
        page = browser.new_page(device_scale_factor=1)
        images = [rasterize(page, clean((BRAND_SRC / f"app-icon-{s}.svg").read_text(encoding="utf-8")), s)
                  for s in ICO_SIZES]
        browser.close()
    largest = images[-1]
    largest.save(BRAND_DST / "WinLove.ico", format="ICO",
                 sizes=[(s, s) for s in ICO_SIZES], append_images=images[:-1])
    largest.save(BRAND_DST / "app-icon-256.png")
    print(f"WinLove.ico ({len(ICO_SIZES)} sizes)")


if __name__ == "__main__":
    copy_svgs()
    build_ico()
