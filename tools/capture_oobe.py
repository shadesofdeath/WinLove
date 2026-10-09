"""Render the welcome wizard (resources/scripts/oobe.ps1) without a VM, into a PNG.

Usage: python tools/capture_oobe.py <out.png> [--page=N] [--theme=dark|light] [--lang=tr|en]

It builds a preview oobe.json with wlcli (the real welcomeJson), adds the preview-only fields
(made-up networks, a filled-in account, which page to show), runs oobe.ps1 in preview mode (nothing
is written to this PC), finds that process's own window and captures it with PrintWindow — never the
whole screen. The window is closed afterwards.
"""
import ctypes
import json
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

PW_RENDERFULLCONTENT = 0x00000002


def capture(hwnd):
    from PIL import Image  # pillow is used by the other capture tool too
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w, h = rect.right - rect.left, rect.bottom - rect.top
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)

    class BMI(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("w", wintypes.LONG), ("h", wintypes.LONG),
                    ("planes", wintypes.WORD), ("bpp", wintypes.WORD), ("comp", wintypes.DWORD),
                    ("sizeimg", wintypes.DWORD), ("xppm", wintypes.LONG), ("yppm", wintypes.LONG),
                    ("used", wintypes.DWORD), ("important", wintypes.DWORD)]

    bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buffer, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    img = Image.frombuffer("RGB", (w, h), buffer, "raw", "BGRX", 0, 1)
    return img


def window_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def check(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            if length > 0:
                found.append(hwnd)
        return True

    user32.EnumWindows(check, 0)
    return found[0] if found else 0


def main():
    args = sys.argv[1:]
    out = Path(args[0])
    page = next((int(a.split("=", 1)[1]) for a in args if a.startswith("--page=")), 0)
    theme = next((a.split("=", 1)[1] for a in args if a.startswith("--theme=")), "dark")
    lang = next((a.split("=", 1)[1] for a in args if a.startswith("--lang=")), "tr")
    # --ui-lang=<code>: show the wizard in that language via the real welcome-langs.json (D-103),
    # the same path the installed image uses (textsByLang + the installed-culture pick).
    ui_lang = next((a.split("=", 1)[1] for a in args if a.startswith("--ui-lang=")), None)

    folder = Path(tempfile.gettempdir()) / "WinLove-oobe-capture"
    folder.mkdir(exist_ok=True)
    wlcli = ROOT / "build" / "x64-debug" / "bin" / "wlcli.exe"
    strings = ROOT / "resources" / "strings" / f"{lang}.json"
    base = folder / "base.json"
    subprocess.run([str(wlcli), "welcome-json", str(base), str(strings)], check=True, capture_output=True)
    data = json.loads(base.read_text(encoding="utf-8"))

    data["preview"] = True
    data["defaults"]["theme"] = theme
    data["previewPage"] = page
    data["previewNetworks"] = [
        {"ssid": "Ev Agi 5GHz", "signal": 4, "secure": True, "auth": 0, "connected": False},
        {"ssid": "WinLove-Guest", "signal": 3, "secure": True, "auth": 0, "connected": False},
        {"ssid": "Kafe Ucretsiz", "signal": 2, "secure": False, "auth": 0, "connected": False},
        {"ssid": "TP-LINK_A24F", "signal": 2, "secure": True, "auth": 0, "connected": False},
        {"ssid": "Komsu", "signal": 1, "secure": True, "auth": 0, "connected": False},
    ]
    data["previewFill"] = {"name": "Berkay", "password": "", "computer": "BERKAY-PC"}
    if ui_lang:
        langs_file = ROOT / "resources" / "strings" / "welcome-langs.json"
        data["textsByLang"] = json.loads(langs_file.read_text(encoding="utf-8"))
        data["previewLang"] = ui_lang
    (folder / "oobe.json").write_text(json.dumps(data, ensure_ascii=True), encoding="ascii")
    script = ROOT / "resources" / "scripts" / "oobe.ps1"
    (folder / "oobe.ps1").write_text(script.read_text(encoding="ascii"), encoding="ascii")

    process = subprocess.Popen(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                                "-File", str(folder / "oobe.ps1")], cwd=str(folder))
    hwnd = 0
    for _ in range(200):
        hwnd = window_of(process.pid)
        if hwnd:
            break
        time.sleep(0.05)
    if not hwnd:
        process.kill()
        print("window not found", file=sys.stderr)
        return 1
    time.sleep(1.4)  # the entrance animation
    img = capture(hwnd)
    img.save(out)
    print(f"size={img.size[0]}x{img.size[1]} -> {out}")
    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    try:
        process.wait(8)
    except subprocess.TimeoutExpired:
        process.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
