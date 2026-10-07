"""Drive a real WinLove window for in-app tests: clicks, keys and text go to the window as messages
(the user's mouse and keyboard are never used) and screenshots come from PrintWindow (only the
WinLove window, never the screen). Everything a run writes lives under build\\lab\\gui.

The app runs elevated (mount / servicing), and Windows only lets an elevated process send input
messages to it, so the first command starts an elevated server (one UAC elevation) that executes
the commands; this client only queues them and prints the answer.

    python tools/gui.py start [--dist] [--size=1360x800] [-- app args]   # the app with --profile=build\\lab\\gui\\profile
    python tools/gui.py shot out.png [--hires] [--crop=x,y,w,h]          # DIPs; --hires keeps physical pixels
    python tools/gui.py click X Y [--right] [--double]                   # DIPs in the client area (= shot pixels)
    python tools/gui.py move X Y | wheel X Y NOTCHES | key enter esc ctrl+a ... | type "text"
    python tools/gui.py dialog <path>        # fill the open / save / folder dialog the app shows and accept it
    python tools/gui.py dialog --cancel      # cancel it
    python tools/gui.py windows              # the app's top-level windows (dialogs) with their classes
    python tools/gui.py wait-log REGEX [--timeout=600]   # until a new line of the run's log matches
    python tools/gui.py mark                 # wait-log ignores what the log holds so far
    python tools/gui.py log [N]              # the last N lines of the run's log
    python tools/gui.py exec -- <command>    # an elevated console command, output printed (e.g. wlcli cleanup)
    python tools/gui.py close | kill | quit  # WM_CLOSE / terminate the app / stop the server

The profile folder holds recent.json, settings.json (work folder build\\lab\\gui\\work) and answers.dat,
so the user's own lists and settings are never touched.
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GUI = ROOT / "build" / "lab" / "gui"
QUEUE = GUI / "queue"
PROFILE = GUI / "profile"
HEARTBEAT = GUI / "server.alive"
APP_STATE = GUI / "app.json"
LOGS = Path(os.environ["LOCALAPPDATA"]) / "WinLove" / "logs"

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
gdi32 = ctypes.windll.gdi32
shell32 = ctypes.windll.shell32
user32.SendMessageTimeoutW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT,
                                       ctypes.POINTER(ctypes.c_size_t)]
user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, ctypes.c_void_p]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.GetParent.restype = wt.HWND
user32.GetDlgItem.restype = wt.HWND

WM_CLOSE, WM_SETTEXT, WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_COMMAND = 0x10, 0x0C, 0x100, 0x101, 0x102, 0x111
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK = 0x200, 0x201, 0x202, 0x203
WM_RBUTTONDOWN, WM_RBUTTONUP, WM_MOUSEWHEEL = 0x204, 0x205, 0x20A
BM_CLICK, MK_LBUTTON, SMTO_ABORTIFHUNG = 0xF5, 0x1, 0x2
KEYS = {"enter": 0x0D, "esc": 0x1B, "tab": 0x09, "space": 0x20, "back": 0x08, "del": 0x2E, "home": 0x24, "end": 0x23,
        "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "pgup": 0x21, "pgdn": 0x22, "f2": 0x71, "f5": 0x74}
MODIFIERS = {"ctrl": 0x11, "shift": 0x10, "alt": 0x12}


# ---- client ----------------------------------------------------------------------------------

def server_alive() -> bool:
    try:
        return time.time() - HEARTBEAT.stat().st_mtime < 10
    except FileNotFoundError:
        return False


def ensure_server():
    if server_alive():
        return
    GUI.mkdir(parents=True, exist_ok=True)
    QUEUE.mkdir(exist_ok=True)
    params = f'"{Path(__file__).resolve()}" serve'
    # "runas": elevated (this machine elevates administrators without a prompt); hidden console.
    if shell32.ShellExecuteW(None, "runas", sys.executable, params, str(ROOT), 0) <= 32:
        sys.exit("could not start the elevated server")
    for _ in range(100):
        if server_alive():
            return
        time.sleep(0.1)
    sys.exit("the elevated server did not start (see build\\lab\\gui\\server.log)")


def client(argv) -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ensure_server()
    name = f"{time.time_ns()}"
    request = QUEUE / f"{name}.cmd.json"
    reply = QUEUE / f"{name}.res.json"
    request.with_suffix(".tmp").write_text(json.dumps({"argv": argv, "cwd": os.getcwd()}), encoding="utf-8")
    request.with_suffix(".tmp").replace(request)
    while not reply.exists():
        if not server_alive():
            sys.exit("the server stopped (see build\\lab\\gui\\server.log)")
        time.sleep(0.05)
    time.sleep(0.02)
    result = json.loads(reply.read_text(encoding="utf-8"))
    reply.unlink()
    if result["out"]:
        print(result["out"], end="" if result["out"].endswith("\n") else "\n")
    return result["code"]


# ---- server: the app window -------------------------------------------------------------------

def app_state() -> dict:
    try:
        return json.loads(APP_STATE.read_text(encoding="utf-8"))
    except (FileNotFoundError, ValueError):
        return {}


def process_alive(pid: int) -> bool:
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = wt.DWORD()
    kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel32.CloseHandle(handle)
    return code.value == 259  # STILL_ACTIVE


def top_windows(pid: int):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def visit(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    return found


def class_name(hwnd) -> str:
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buffer, 256)
    return buffer.value


def window_text(hwnd) -> str:
    buffer = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buffer, 512)
    return buffer.value


def main_window():
    pid = app_state().get("pid")
    if not pid or not process_alive(pid):
        raise RuntimeError("the app is not running (gui.py start)")
    for hwnd in top_windows(pid):
        if class_name(hwnd) == "WinLove.Window":
            return hwnd
    raise RuntimeError("the app has no main window")


def scale_of(hwnd) -> float:
    return user32.GetDpiForWindow(hwnd) / 96.0


def lparam(x, y) -> int:
    return (int(y) & 0xFFFF) << 16 | (int(x) & 0xFFFF)


def send(hwnd, message, wparam=0, lp=0, timeout_ms=3000) -> bool:
    """Synchronous where possible: False when the app did not answer in time (e.g. the click opened a
    modal dialog, whose loop runs inside the handler)."""
    result = ctypes.c_size_t()
    return bool(user32.SendMessageTimeoutW(hwnd, message, wparam, lp, SMTO_ABORTIFHUNG, timeout_ms, ctypes.byref(result)))


def point(hwnd, x: str, y: str):
    s = scale_of(hwnd)
    return round(float(x) * s), round(float(y) * s)


def do_click(args):
    hwnd = main_window()
    x, y = point(hwnd, args[0], args[1])
    right, double = "--right" in args, "--double" in args
    send(hwnd, WM_MOUSEMOVE, 0, lparam(x, y))
    time.sleep(0.03)
    down, up, held = (WM_RBUTTONDOWN, WM_RBUTTONUP, 0x2) if right else (WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON)
    for n in range(2 if double else 1):
        send(hwnd, WM_LBUTTONDBLCLK if (double and n == 1 and not right) else down, held, lparam(x, y))
        time.sleep(0.03)
        answered = send(hwnd, up, 0, lparam(x, y), timeout_ms=1500)
        time.sleep(0.05)
    time.sleep(0.25)
    return "" if answered else "note: the app is busy in a modal loop (a dialog opened?)"


def do_move(args):
    hwnd = main_window()
    x, y = point(hwnd, args[0], args[1])
    send(hwnd, WM_MOUSEMOVE, 0, lparam(x, y))
    time.sleep(0.3)
    return ""


def do_wheel(args):
    hwnd = main_window()
    x, y = point(hwnd, args[0], args[1])
    origin = wt.POINT(x, y)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    notches = int(args[2])
    send(hwnd, WM_MOUSEMOVE, 0, lparam(x, y))
    for _ in range(abs(notches)):
        delta = 120 if notches > 0 else -120
        send(hwnd, WM_MOUSEWHEEL, (delta & 0xFFFF) << 16, lparam(origin.x, origin.y))
        time.sleep(0.03)
    time.sleep(0.3)
    return ""


def with_modifiers(hwnd, modifiers, action):
    """GetKeyState in the app reads the thread's key state: share it (AttachThreadInput) and set it."""
    if not modifiers:
        return action()
    target = user32.GetWindowThreadProcessId(hwnd, None)
    ours = kernel32.GetCurrentThreadId()
    user32.AttachThreadInput(ours, target, True)
    try:
        state = (ctypes.c_ubyte * 256)()
        user32.GetKeyboardState(state)
        saved = bytes(state)
        for vk in modifiers:
            state[vk] = 0x80
        user32.SetKeyboardState(state)
        try:
            return action()
        finally:
            user32.SetKeyboardState((ctypes.c_ubyte * 256)(*saved))
    finally:
        user32.AttachThreadInput(ours, target, False)


def do_key(args):
    hwnd = main_window()
    for chord in args:
        parts = chord.lower().split("+")
        modifiers = [MODIFIERS[p] for p in parts[:-1]]
        name = parts[-1]
        vk = KEYS.get(name) or (ord(name.upper()) if len(name) == 1 else None)
        if vk is None:
            raise RuntimeError(f"unknown key: {chord}")

        def press():
            send(hwnd, WM_KEYDOWN, vk, 1, timeout_ms=1500)
            send(hwnd, WM_KEYUP, vk, 0xC0000001, timeout_ms=1500)

        with_modifiers(hwnd, modifiers, press)
        time.sleep(0.15)
    time.sleep(0.2)
    return ""


def do_type(args):
    hwnd = main_window()
    for ch in " ".join(args):
        send(hwnd, WM_CHAR, ord(ch), 1)
    time.sleep(0.2)
    return ""


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def capture_client(hwnd):
    from PIL import Image
    window = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    w, h = window.right - window.left, window.bottom - window.top
    screen_dc = user32.GetDC(None)
    mem_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap = gdi32.CreateCompatibleBitmap(screen_dc, w, h)
    gdi32.SelectObject(mem_dc, bitmap)
    user32.PrintWindow(hwnd, mem_dc, 2)  # PW_RENDERFULLCONTENT: DirectX / composition content too
    header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bitmap, 0, h, buffer, ctypes.byref(header), 0)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(None, screen_dc)
    image = Image.frombuffer("RGBA", (w, h), buffer, "raw", "BGRA", 0, 1).convert("RGB")
    client = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(client))
    origin = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    left, top = origin.x - window.left, origin.y - window.top
    return image.crop((left, top, left + client.right, top + client.bottom))


def do_shot(args, cwd):
    from PIL import Image
    hwnd = main_window()
    out = Path(args[0]) if Path(args[0]).is_absolute() else Path(cwd) / args[0]
    image = capture_client(hwnd)
    s = scale_of(hwnd)
    hires = "--hires" in args
    crop = next((a.split("=", 1)[1] for a in args if a.startswith("--crop=")), None)
    if crop:
        x, y, w, h = (float(v) for v in crop.split(","))
        image = image.crop((round(x * s), round(y * s), round((x + w) * s), round((y + h) * s)))
    if not hires:
        image = image.resize((max(1, round(image.width / s)), max(1, round(image.height / s))), Image.LANCZOS)
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out)
    return f"{out} {image.width}x{image.height} (scale {s:g})"


def do_start(args, cwd):
    state = app_state()
    if state.get("pid") and process_alive(state["pid"]):
        raise RuntimeError("the app is already running (gui.py close / kill)")
    app_args = args[args.index("--") + 1:] if "--" in args else []
    own = args[:args.index("--")] if "--" in args else args
    built = ROOT / ("dist" if "--dist" in own else "build/x64-debug/bin") / "WinLove.exe"
    # A copy runs: a build may relink the original while the test goes on.
    (GUI / "bin").mkdir(parents=True, exist_ok=True)
    exe = GUI / "bin" / "WinLove.exe"
    shutil.copy2(built, exe)
    size = next((a.split("=", 1)[1] for a in own if a.startswith("--size=")), "1360x800")
    PROFILE.mkdir(parents=True, exist_ok=True)
    settings = PROFILE / "settings.json"
    if not settings.exists():
        # The run's own work folder; a fresh profile otherwise has the defaults.
        settings.write_text(json.dumps({"version": 1, "theme": "dark", "accent": "copper", "language": "tr",
                                        "reduceMotion": True, "workRoot": str(GUI / "work"), "mountFolder": "",
                                        "isoFolder": str(GUI / "out")}, indent=2), encoding="utf-8")
    started = time.time()
    process = subprocess.Popen([str(exe), f"--profile={PROFILE}", *app_args], cwd=str(ROOT))
    hwnd = 0
    for _ in range(200):
        hwnd = next((h for h in top_windows(process.pid) if class_name(h) == "WinLove.Window"), 0)
        if hwnd:
            break
        time.sleep(0.05)
    if not hwnd:
        process.kill()
        raise RuntimeError("no window")
    s = scale_of(hwnd)
    w, h = (int(v) for v in size.split("x"))
    # Client size in DIPs: the frame of this custom-drawn window is the client area's border.
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    window, client = wt.RECT(), wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    user32.GetClientRect(hwnd, ctypes.byref(client))
    extra_w = (window.right - window.left) - client.right
    extra_h = (window.bottom - window.top) - client.bottom
    user32.SetWindowPos(hwnd, None, 0, 0, round(w * s) + extra_w, round(h * s) + extra_h, 0x0004 | 0x0010)  # NOZORDER | NOACTIVATE
    time.sleep(0.5)
    log = None
    for _ in range(40):
        candidates = [p for p in LOGS.glob("WinLove-*.log") if p.stat().st_mtime >= started - 1]
        if candidates:
            log = max(candidates, key=lambda p: p.stat().st_mtime)
            break
        time.sleep(0.05)
    APP_STATE.write_text(json.dumps({"pid": process.pid, "log": str(log) if log else "", "logPos": 0}), encoding="utf-8")
    user32.GetClientRect(hwnd, ctypes.byref(client))
    return f"pid {process.pid}, client {client.right / s:.0f}x{client.bottom / s:.0f} DIPs, scale {s:g}, log {log}"


def find_dialog(pid):
    for hwnd in top_windows(pid):
        if class_name(hwnd) == "#32770":
            return hwnd
    return 0


def children(hwnd):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def visit(child, _):
        found.append(child)
        return True

    user32.EnumChildWindows(hwnd, visit, 0)
    return found


def do_dialog(args):
    pid = app_state().get("pid")
    dialog = 0
    for _ in range(100):
        dialog = find_dialog(pid)
        if dialog:
            break
        time.sleep(0.1)
    if not dialog:
        raise RuntimeError("no dialog is open")
    title = window_text(dialog)
    if args and args[0] == "--cancel":
        user32.PostMessageW(dialog, WM_COMMAND, 2, 0)  # IDCANCEL
        return f"cancelled '{title}'"
    path = " ".join(args)
    edit = 0
    # The file name box: Edit inside the ComboBoxEx32 1148 (open), or the Edit 1148 / 1152 itself (save, folder).
    for child in children(dialog):
        cls, cid = class_name(child), user32.GetDlgCtrlID(child)
        if cls == "Edit" and user32.IsWindowVisible(child):
            parent = user32.GetParent(child)
            grand = user32.GetParent(parent) if parent else 0
            if cid in (1148, 1152) or (grand and user32.GetDlgCtrlID(grand) == 1148) or user32.GetDlgCtrlID(parent) == 1148:
                edit = child
                break
    if not edit:
        raise RuntimeError(f"no file name box in '{title}'")
    buffer = ctypes.create_unicode_buffer(path)
    user32.SendMessageW(edit, WM_SETTEXT, 0, ctypes.addressof(buffer))
    time.sleep(0.2)
    ok = user32.GetDlgItem(dialog, 1)
    if ok:
        user32.PostMessageW(ok, BM_CLICK, 0, 0)
    else:
        user32.PostMessageW(dialog, WM_COMMAND, 1, 0)
    for _ in range(50):
        time.sleep(0.1)
        if not user32.IsWindow(dialog):
            return f"'{title}' <- {path}"
    still = [f"{class_name(h)} '{window_text(h)}'" for h in top_windows(pid) if h != dialog and class_name(h) == "#32770"]
    return f"'{title}' still open after accepting {path}" + (f"; other dialogs: {still}" if still else "")


def do_windows(_args):
    pid = app_state().get("pid")
    lines = []
    for hwnd in top_windows(pid):
        rect = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        lines.append(f"{hwnd:#x} {class_name(hwnd)} '{window_text(hwnd)}' {rect.left},{rect.top},{rect.right},{rect.bottom}")
        if class_name(hwnd) == "#32770":
            for child in children(hwnd):
                if user32.IsWindowVisible(child) and class_name(child) in ("Edit", "Button", "Static", "ComboBoxEx32"):
                    lines.append(f"    {class_name(child)} id={user32.GetDlgCtrlID(child)} '{window_text(child)}'")
    return "\n".join(lines) or "no windows"


def new_log_lines(state):
    log = state.get("log")
    if not log:
        return [], 0
    data = Path(log).read_bytes()
    pos = state.get("logPos", 0)
    text = data[pos:].decode("utf-8", errors="replace")
    return text.splitlines(), len(data)


def do_wait_log(args):
    pattern = re.compile(args[0])
    timeout = float(next((a.split("=", 1)[1] for a in args if a.startswith("--timeout=")), "600"))
    state = app_state()
    deadline = time.time() + timeout
    while True:
        lines, end = new_log_lines(state)
        for i, line in enumerate(lines):
            if pattern.search(line):
                # Consume up to and including this line.
                consumed = "\n".join(lines[:i + 1]) + "\n"
                state["logPos"] = state.get("logPos", 0) + len(consumed.encode("utf-8"))
                APP_STATE.write_text(json.dumps(state), encoding="utf-8")
                return line
        if time.time() > deadline:
            raise RuntimeError(f"timeout; last lines:\n" + "\n".join(lines[-8:]))
        if not process_alive(state.get("pid", 0)):
            raise RuntimeError("the app exited; last lines:\n" + "\n".join(lines[-8:]))
        time.sleep(0.5)


def do_mark(_args):
    """wait-log from here on: what the log already holds no longer matches."""
    state = app_state()
    state["logPos"] = Path(state["log"]).stat().st_size if state.get("log") else 0
    APP_STATE.write_text(json.dumps(state), encoding="utf-8")
    return ""


def do_log(args):
    log = app_state().get("log")
    n = int(args[0]) if args else 30
    lines = Path(log).read_text(encoding="utf-8", errors="replace").splitlines() if log else []
    return "\n".join(lines[-n:])


def do_exec(args, cwd):
    command = args[args.index("--") + 1:] if "--" in args else args
    done = subprocess.run(command, cwd=cwd, capture_output=True, timeout=7200)
    text = (done.stdout + done.stderr).decode("utf-8", errors="replace")
    return text + f"\n[exit {done.returncode}]"


def do_close(args):
    hwnd = main_window()
    pid = app_state()["pid"]
    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    for _ in range(100):
        if not process_alive(pid):
            return "closed"
        time.sleep(0.1)
    return "still running (a question in the window?)"


def do_kill(_args):
    pid = app_state().get("pid")
    if pid and process_alive(pid):
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        return "killed"
    return "not running"


COMMANDS = {"click": do_click, "move": do_move, "wheel": do_wheel, "key": do_key, "type": do_type,
            "dialog": do_dialog, "windows": do_windows, "wait-log": do_wait_log, "log": do_log, "mark": do_mark,
            "close": do_close, "kill": do_kill}


def serve():
    GUI.mkdir(parents=True, exist_ok=True)
    QUEUE.mkdir(exist_ok=True)
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: physical pixels
    log = open(GUI / "server.log", "a", encoding="utf-8")
    log.write(f"--- server {os.getpid()} {time.ctime()}\n")
    log.flush()
    # One server at a time: an earlier one (stuck in a long command) would race for the queue.
    try:
        earlier = int(HEARTBEAT.read_text())
        if earlier != os.getpid() and process_alive(earlier):
            subprocess.run(["taskkill", "/PID", str(earlier), "/F"], capture_output=True)
    except (FileNotFoundError, ValueError):
        pass
    stop = threading.Event()

    def beat():  # alive while a command runs for minutes (wait-log, exec)
        while not stop.is_set():
            HEARTBEAT.write_text(str(os.getpid()))
            stop.wait(1)

    threading.Thread(target=beat, daemon=True).start()
    idle_since = time.time()
    while time.time() - idle_since < 4 * 3600:
        requests = sorted(QUEUE.glob("*.cmd.json"))
        if not requests:
            time.sleep(0.05)
            continue
        idle_since = time.time()
        request = requests[0]
        body = json.loads(request.read_text(encoding="utf-8"))
        request.unlink()
        argv, cwd = body["argv"], body.get("cwd", str(ROOT))
        code, out = 0, ""
        try:
            name, rest = argv[0], argv[1:]
            if name == "quit":
                (QUEUE / request.name.replace(".cmd.", ".res.")).write_text(json.dumps({"code": 0, "out": "bye"}))
                break
            elif name == "start":
                out = do_start(rest, cwd)
            elif name == "shot":
                out = do_shot(rest, cwd)
            elif name == "exec":
                out = do_exec(rest, cwd)
            elif name in COMMANDS:
                out = COMMANDS[name](rest)
            else:
                code, out = 2, f"unknown command: {name}"
        except Exception as error:  # report, keep serving
            code, out = 1, f"error: {error}"
        log.write(f"{time.strftime('%H:%M:%S')} {argv} -> {code}\n")
        log.flush()
        reply = QUEUE / request.name.replace(".cmd.", ".res.")
        reply.with_suffix(".tmp").write_text(json.dumps({"code": code, "out": out}), encoding="utf-8")
        reply.with_suffix(".tmp").replace(reply)
    stop.set()
    HEARTBEAT.unlink(missing_ok=True)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == "serve":
        serve()
    else:
        sys.exit(client(sys.argv[1:]))
