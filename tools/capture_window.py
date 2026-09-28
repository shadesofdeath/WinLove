"""Launch WinLove.exe, capture ONLY its window (PrintWindow, not the screen) and close it.

Checks what --render cannot: the real HWND path (custom frame, NC hit-testing, swap chain, DPI).
Other windows on the desktop are never captured, even if they cover WinLove.

Usage: python tools/capture_window.py <out.png> [--maximized] [--exe=build/x64-debug/bin/WinLove.exe] [-- app args]
Prints the window's DPI, visible bounds and the app's exit code.
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
dwmapi = ctypes.windll.dwmapi

PW_RENDERFULLCONTENT = 2  # includes DirectX/flip-model content
DWMWA_EXTENDED_FRAME_BOUNDS = 9
WM_CLOSE = 0x0010
SW_MAXIMIZE = 3


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def capture(hwnd) -> Image.Image:
    window = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    visible = wt.RECT()
    dwmapi.DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, ctypes.byref(visible), ctypes.sizeof(visible))
    w, h = window.right - window.left, window.bottom - window.top

    screen_dc = user32.GetDC(None)
    mem_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap = gdi32.CreateCompatibleBitmap(screen_dc, w, h)
    gdi32.SelectObject(mem_dc, bitmap)
    user32.PrintWindow(hwnd, mem_dc, PW_RENDERFULLCONTENT)

    header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bitmap, 0, h, buffer, ctypes.byref(header), 0)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(None, screen_dc)

    image = Image.frombuffer("RGBA", (w, h), buffer, "raw", "BGRA", 0, 1).convert("RGB")
    # Drop the invisible resize borders: keep what the user actually sees.
    return image.crop((visible.left - window.left, visible.top - window.top,
                       visible.right - window.left, visible.bottom - window.top))


def main() -> int:
    args = sys.argv[1:]
    app_args = args[args.index("--") + 1:] if "--" in args else []
    args = args[:args.index("--")] if "--" in args else args
    out = Path(args[0])
    maximized = "--maximized" in args
    exe = next((a.split("=", 1)[1] for a in args if a.startswith("--exe=")),
               str(ROOT / "build" / "x64-debug" / "bin" / "WinLove.exe"))

    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: physical pixels
    process = subprocess.Popen([exe, *app_args])
    hwnd = 0
    for _ in range(100):
        hwnd = user32.FindWindowW("WinLove.Window", None)
        if hwnd:
            break
        time.sleep(0.05)
    if not hwnd:
        process.kill()
        print("window not found", file=sys.stderr)
        return 1
    if maximized:
        user32.ShowWindow(hwnd, SW_MAXIMIZE)
    time.sleep(0.8)
    image = capture(hwnd)
    image.save(out)
    print(f"dpi={user32.GetDpiForWindow(hwnd)} size={image.size[0]}x{image.size[1]} -> {out}")

    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    try:
        code = process.wait(10)
    except subprocess.TimeoutExpired:
        process.kill()
        print("app did not exit after WM_CLOSE", file=sys.stderr)
        return 1
    print(f"exit={code}")
    return code


if __name__ == "__main__":
    sys.exit(main())
