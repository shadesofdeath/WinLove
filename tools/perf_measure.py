"""Measure WinLove: startup, per-page memory / idle CPU, render times, wlcli read operations.

    python -I tools/perf_measure.py <label> [--config=release|debug] [--only=startup,pages,render,cli]
                                    [--iso=<path>] [--runs=3]

Writes build\\lab\\perf\\<label>.json and prints Markdown tables (docs/PERF-BASELINE.md is made from them).
The binaries are copied to build\\lab\\perf\\bin\\<label>\\ first, so a later build does not disturb a run and
the same binaries can be measured again next to a newer build.

Nothing here needs an administrator and nothing writes to an image:
  - the windowed runs use --no-elevate and --profile=build\\lab\\perf\\profile (the user's recent list,
    settings and work folder are not touched); the window is found by its process id and closed with WM_CLOSE;
  - the ISO is only read (wlcli info / ls / verify, and "open source" in the app).
"""
import ctypes
import ctypes.wintypes as wt
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PERF = ROOT / "build" / "lab" / "perf"
DEFAULT_ISO = Path(r"C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso")

PAGES = ["source", "download", "images", "components", "apps", "programs", "features", "updates", "languages",
         "drivers", "registry", "services", "tasks", "tweaks", "startmenu", "hosts", "branding", "icons",
         "unattended", "postsetup", "files", "apply", "iso", "presets", "logs", "settings", "about"]

# Render-only sample states (src/app/App.h): the pages with rows in them. --demo-languages is left out:
# it is known to spin forever (docs/STATUS.md).
DEMOS = [
    ("components", ["--demo-components"]),
    ("features", ["--demo-features"]),
    ("registry", ["--demo-registry"]),
    ("services", ["--demo-services"]),
    ("tasks", ["--demo-tasks"]),
    ("tweaks", ["--demo-tweaks"]),
    ("drivers", ["--demo-drivers"]),
    ("postsetup", ["--demo-postsetup"]),
    ("unattended", ["--demo-unattended"]),
    ("presets", ["--demo-presets"]),
    ("logs", ["--demo-logs"]),
    ("hosts", ["--demo-hosts"]),
    ("apply", ["--demo-features", "--demo-apply=running"]),
]

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class PMC(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


psapi.GetProcessMemoryInfo.argtypes = [wt.HANDLE, ctypes.POINTER(PMC), wt.DWORD]
kernel32.GetProcessTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
kernel32.GetProcessHandleCount.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
user32.GetGuiResources.argtypes = [wt.HANDLE, wt.DWORD]
user32.WaitForInputIdle.argtypes = [wt.HANDLE, wt.DWORD]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.IsWindowVisible.argtypes = [wt.HWND]
user32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
ENUM = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [ENUM, wt.LPARAM]


def memory(handle):
    pmc = PMC()
    pmc.cb = ctypes.sizeof(PMC)
    psapi.GetProcessMemoryInfo(handle, ctypes.byref(pmc), pmc.cb)
    return pmc


def cpu_seconds(handle):
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    kernel32.GetProcessTimes(handle, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    ticks = lambda f: (f.dwHighDateTime << 32) | f.dwLowDateTime
    return (ticks(k) + ticks(u)) / 1e7


def app_window(pid):
    found = []

    def each(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            if name.value == "WinLove.Window" and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
        return True

    user32.EnumWindows(ENUM(each), 0)
    return found[0] if found else 0


def mb(n):
    return round(n / (1024 * 1024), 1)


def windowed(exe, profile, args, settle=3.0, idle_window=5.0):
    """Starts the app, waits until its window is up and quiet, samples it, closes it."""
    started = time.perf_counter()
    process = subprocess.Popen([str(exe), f"--profile={profile}", "--no-elevate", "--size=1360x800", *args],
                               cwd=str(ROOT))
    handle = int(process._handle)
    hwnd = 0
    while not hwnd and time.perf_counter() - started < 20:
        hwnd = app_window(process.pid)
        if not hwnd:
            if process.poll() is not None:
                return {"error": f"exited {process.returncode} before a window"}
            time.sleep(0.005)
    if not hwnd:
        process.kill()
        return {"error": "no window in 20 s"}
    t_window = time.perf_counter() - started
    user32.WaitForInputIdle(handle, 10000)
    t_idle = time.perf_counter() - started
    time.sleep(settle)
    cpu0, wall0 = cpu_seconds(handle), time.perf_counter()
    time.sleep(idle_window)
    cpu1, wall1 = cpu_seconds(handle), time.perf_counter()
    pmc = memory(handle)
    handles = wt.DWORD()
    kernel32.GetProcessHandleCount(handle, ctypes.byref(handles))
    result = {
        "windowMs": round(t_window * 1000), "idleMs": round(t_idle * 1000),
        "privateMb": mb(pmc.PrivateUsage), "workingSetMb": mb(pmc.WorkingSetSize),
        "peakWorkingSetMb": mb(pmc.PeakWorkingSetSize),
        "idleCpuPercent": round(100 * (cpu1 - cpu0) / (wall1 - wall0), 2),
        "startCpuSeconds": round(cpu0, 2),
        "handles": handles.value, "gdi": user32.GetGuiResources(handle, 0), "userObjects": user32.GetGuiResources(handle, 1),
    }
    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    try:
        process.wait(8)
        result["exit"] = process.returncode
    except subprocess.TimeoutExpired:
        process.kill()
        result["exit"] = "killed (did not close in 8 s)"
    return result


def timed(command, timeout=120):
    """One console / render run: wall time, peak working set, exit code."""
    started = time.perf_counter()
    process = subprocess.Popen(command, cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        process.wait(timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        return {"error": f"timeout {timeout} s"}
    elapsed = time.perf_counter() - started
    pmc = memory(int(process._handle))
    return {"ms": round(elapsed * 1000), "peakWorkingSetMb": mb(pmc.PeakWorkingSetSize),
            "peakPrivateMb": mb(pmc.PeakPagefileUsage), "exit": process.returncode}


def repeat(command, runs, timeout=120):
    results = [timed(command, timeout) for _ in range(runs)]
    bad = [r for r in results if "error" in r or r.get("exit") != 0]
    if bad:
        return bad[0]
    times = [r["ms"] for r in results]
    return {"medianMs": round(statistics.median(times)), "minMs": min(times), "maxMs": max(times),
            "peakWorkingSetMb": max(r["peakWorkingSetMb"] for r in results),
            "peakPrivateMb": max(r["peakPrivateMb"] for r in results), "runs": runs}


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 2
    label = args[0]
    option = lambda name, default: next((a.split("=", 1)[1] for a in args if a.startswith(f"--{name}=")), default)
    config = option("config", "release")
    only = option("only", "startup,pages,render,cli").split(",")
    iso = Path(option("iso", str(DEFAULT_ISO)))
    runs = int(option("runs", "3"))

    source_bin = ROOT / "build" / f"x64-{config}" / "bin"
    bin_dir = PERF / "bin" / label
    bin_dir.mkdir(parents=True, exist_ok=True)
    for name in ("WinLove.exe", "wlcli.exe"):
        if not (bin_dir / name).exists() or "--recopy" in args:
            shutil.copy2(source_bin / name, bin_dir / name)
    app, cli = bin_dir / "WinLove.exe", bin_dir / "wlcli.exe"
    profile = PERF / "profile"
    profile.mkdir(parents=True, exist_ok=True)
    (profile / "settings.json").write_text(json.dumps({
        "version": 1, "theme": "dark", "language": "tr", "reduceMotion": False,
        "workRoot": str(PERF / "work"), "mountFolder": "", "isoFolder": str(PERF / "out")}, indent=2), encoding="utf-8")
    out_file = PERF / f"{label}.json"
    data = json.loads(out_file.read_text(encoding="utf-8")) if out_file.exists() else {}
    data["label"] = label
    data["config"] = config
    data["exeBytes"] = {"WinLove.exe": app.stat().st_size, "wlcli.exe": cli.stat().st_size}

    def save():
        out_file.write_text(json.dumps(data, indent=2), encoding="utf-8")

    if "startup" in only:
        rows = [windowed(app, profile, [], settle=1.0, idle_window=1.0) for _ in range(5)]
        ok = [r for r in rows if "error" not in r]
        data["startup"] = {"runs": rows,
                           "windowMsMedian": round(statistics.median(r["windowMs"] for r in ok)) if ok else None,
                           "idleMsMedian": round(statistics.median(r["idleMs"] for r in ok)) if ok else None}
        print(f"startup: window {data['startup']['windowMsMedian']} ms, input idle {data['startup']['idleMsMedian']} ms "
              f"(runs: {[r.get('windowMs') for r in rows]})", flush=True)
        save()

    if "pages" in only:
        data["pages"] = {}
        scenarios = [(p, [f"--page={p}"]) for p in PAGES]
        if iso.exists():
            scenarios.append(("images + ISO acik", ["--page=images", str(iso)]))
        print("\n| Sayfa | Private Bytes MB | Working Set MB | Bosta CPU % | Handle | GDI | USER |\n|---|---|---|---|---|---|---|")
        for name, extra in scenarios:
            r = windowed(app, profile, extra)
            data["pages"][name] = r
            if "error" in r:
                print(f"| {name} | HATA: {r['error']} |")
            else:
                print(f"| {name} | {r['privateMb']} | {r['workingSetMb']} | {r['idleCpuPercent']} | {r['handles']} | "
                      f"{r['gdi']} | {r['userObjects']} | {'' if r['exit'] == 0 else r['exit']}", flush=True)
            save()

    if "render" in only:
        data["render"] = {}
        shot = PERF / "render.png"
        base = [str(app), f"--render={shot}", "--theme=dark", "--scale=1.75", "--size=1207x700"]
        print("\n| Render | medyan ms | en az | en cok | tepe WS MB |\n|---|---|---|---|---|")
        scenarios = [(p, [f"--page={p}"]) for p in PAGES]
        scenarios += [(f"{p} ({' '.join(d)})", [f"--page={p}", *d]) for p, d in DEMOS]
        for name, extra in scenarios:
            r = repeat(base + extra, runs, timeout=60)
            data["render"][name] = r
            if "medianMs" in r:
                print(f"| {name} | {r['medianMs']} | {r['minMs']} | {r['maxMs']} | {r['peakWorkingSetMb']} |", flush=True)
            else:
                print(f"| {name} | HATA: {r} |", flush=True)
            save()

    if "cli" in only:
        data["cli"] = {}
        commands = [("help", ["help"])]
        if iso.exists():
            commands += [("info <iso>", ["info", str(iso)]), ("info <iso> --json", ["info", str(iso), "--json"]),
                         ("ls <iso>", ["ls", str(iso)]), ("ls <iso> sources", ["ls", str(iso), "sources"]),
                         ("verify <iso> (her akisin SHA-1'i)", ["verify", str(iso)])]
        # A real resource file of this PC, read only: the icon patch holds several copies of it at once.
        mun = Path(r"C:\Windows\SystemResources\imageres.dll.mun")
        if mun.exists():
            ico = PERF / "icon3.ico"
            subprocess.run([str(cli), "icon-extract", str(mun), "#3", str(ico)], cwd=str(ROOT),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if ico.exists():
                commands.append(("icon-patch imageres.dll.mun (24 MB)",
                                 ["icon-patch", str(mun), f"3={ico}", f"--out={PERF / 'imageres.patched.mun'}"]))
        sample = ROOT / "tests" / "integration" / "fixtures" / "sample-changeset.json"
        if sample.exists():
            commands.append(("plan sample-changeset.json", ["plan", str(sample)]))
        print("\n| wlcli | medyan ms | en az | en cok | tepe WS MB |\n|---|---|---|---|---|")
        for name, extra in commands:
            r = repeat([str(cli), *extra], runs, timeout=600)
            data["cli"][name] = r
            if "medianMs" in r:
                print(f"| {name} | {r['medianMs']} | {r['minMs']} | {r['maxMs']} | {r['peakWorkingSetMb']} |", flush=True)
            else:
                print(f"| {name} | HATA: {r} |", flush=True)
            save()
    save()
    print(f"\n-> {out_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
