"""Render every page with two builds and compare the frames pixel by pixel.

    python -I tools/render_diff.py <old WinLove.exe> <new WinLove.exe> [--iso=<path>] [--out=build\\lab\\perf\\diff]

For changes that must not alter what is drawn (caching, culling, refactors of paint code). Each page and
each sample state (--demo-*) is rendered headless (--render, no window) in dark and light; frames that
differ are listed with the number of differing pixels and their bounding box, and both PNGs are kept.
Exit code 1 when any frame differs.
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent
PAGES = ["source", "download", "images", "overview", "components", "apps", "programs", "features", "updates", "languages",
         "drivers", "registry", "services", "tasks", "tweaks", "startmenu", "hosts", "branding", "icons",
         "unattended", "postsetup", "files", "apply", "iso", "presets", "logs", "settings", "about", "gallery"]
DEMOS = [
    ["--page=components", "--demo-components"], ["--page=components", "--demo-components", "--demo-compat"],
    ["--page=features", "--demo-features"], ["--page=registry", "--demo-registry"],
    ["--page=registry", "--demo-registry=dialog"], ["--page=services", "--demo-services"],
    ["--page=tasks", "--demo-tasks"], ["--page=tasks", "--demo-task-create"], ["--page=tweaks", "--demo-tweaks"],
    ["--page=tweaks", "--demo-tweaks", "--demo-image-values"], ["--page=drivers", "--demo-drivers"],
    ["--page=drivers", "--demo-drivers", "--demo-boot-drivers"], ["--page=drivers", "--demo-image-drivers"],
    ["--page=postsetup", "--demo-postsetup"], ["--page=postsetup", "--demo-wifi"],
    ["--page=unattended", "--demo-unattended"], ["--page=unattended", "--demo-unattended=welcome"],
    ["--page=presets", "--demo-presets"], ["--page=logs", "--demo-logs"], ["--page=hosts", "--demo-hosts"],
    ["--page=files", "--demo-files"], ["--page=apps", "--demo-apps"], ["--page=apps", "--demo-apps=defaults"],
    ["--page=startmenu", "--demo-startmenu"], ["--page=startmenu", "--demo-startmenu=settings"],
    ["--page=startmenu", "--demo-startmenu=taskbar"], ["--page=icons", "--demo-icons"],
    ["--page=icons", "--demo-icons=redirect"], ["--page=branding", "--demo-branding"],
    ["--page=source", "--demo-mounts"], ["--page=source", "--demo-host-dism"],
    ["--page=languages", "--demo-languages=dialog"], ["--page=apps", "--demo-store=dialog"],
    ["--page=download", "--demo-download=pick"], ["--page=download", "--demo-download=downloading"],
    ["--page=download", "--demo-download=apps"], ["--page=download", "--demo-download=failed"],
    # D-149: the official ESD as the source — chosen, not in the catalog, "Fark ne?" open, the job's stages.
    ["--page=download", "--demo-download=esd"], ["--page=download", "--demo-download=uup-only"],
    ["--page=download", "--demo-download=esd-info"], ["--page=download", "--demo-download=esd-downloading"],
    ["--page=download", "--demo-download=esd-verifying"], ["--page=download", "--demo-download=esd-converting"],
    ["--page=download", "--demo-download=esd-failed"],
    ["--demo-features", "--demo-apply=running"], ["--demo-features", "--demo-apply=paused"],
    ["--demo-features", "--demo-apply=done"], ["--demo-features", "--demo-apply=skipped"],
    ["--demo-features", "--demo-apply=held"], ["--demo-features", "--demo-apply=confirm"],
    ["--page=settings", "--demo-app-menu"], ["--page=source", "--palette=dis"],
    ["--page=components", "--demo-components", "--hover-at=600,300"],
    ["--page=features", "--demo-features", "--click-at=500,200"],
    ["--page=source", "--dialog=admin"], ["--page=source", "--tab=3"],
    # D-138: the image overview and the Apply screens over a mounted image with its lists read.
    ["--demo-overview"], ["--demo-overview=unread"], ["--demo-overview=running"], ["--demo-overview=paused"],
    ["--demo-overview=done"], ["--demo-overview=held"], ["--page=apply", "--demo-overview"],
    ["--page=apply", "--demo-overview=running"], ["--page=apply", "--demo-overview=paused"],
    ["--page=apply", "--demo-overview=done"], ["--page=apply", "--demo-overview=held"],
    # D-139: the working pages with a row selected — the detail card beside the page (1207 DIP keeps it).
    ["--page=components", "--demo-components", "--toast-timeout", "--click-at=264,324", "--click-at=440,436"],
    ["--page=services", "--demo-services", "--click-at=330,240"], ["--page=registry", "--demo-registry", "--click-at=450,352"],
    ["--page=features", "--demo-features", "--click-at=262,332", "--click-at=450,360"],
    ["--page=drivers", "--demo-drivers", "--click-at=450,335"], ["--page=drivers", "--demo-image-drivers", "--click-at=450,279"],
    ["--page=drivers", "--demo-drivers", "--demo-boot-drivers", "--click-at=450,335"],
    ["--page=tasks", "--demo-tasks", "--click-at=450,240"], ["--page=apps", "--demo-apps", "--click-at=450,311"],
    ["--page=languages", "--demo-languages", "--click-at=450,275"],
    ["--page=programs", "--demo-features", "--click-after-load=330,378"],
    # D-140: the update check's answers on the About card, and the notice (a toast, the status bar) elsewhere.
    ["--page=about", "--demo-update"], ["--page=about", "--demo-update=current"],
    ["--page=about", "--demo-update=failed"], ["--page=about", "--demo-update=checking"],
    ["--page=source", "--demo-update"],
    # 2c (after D-139): Hosts, Dosyalar and Kurulum Sonrası with a row selected — the detail card beside them.
    ["--page=hosts", "--demo-hosts", "--click-at=450,224"], ["--page=files", "--demo-files", "--click-at=450,395"],
    ["--page=postsetup", "--demo-postsetup", "--click-at=450,302"],
    # D-141: a VHDX source (made up, nothing attached): Sürümler, the mount question, Özet closed / mounted,
    # the unmount question, a saved run (Özet and Uygula), ISO Oluştur's reason.
    ["--demo-vhd"], ["--demo-vhd=confirm"], ["--page=overview", "--demo-vhd"], ["--demo-vhd=mounted"],
    ["--demo-vhd=unmount"], ["--demo-vhd=done"], ["--page=apply", "--demo-vhd=done"], ["--demo-vhd=iso"],
    # 2026-11-11: an image another tool mounted, taken over (Kaynak): the work on its row (no percentage), DISM's
    # refusal said, Sürümler and Özet saying whose folder it is, the unmount question for it.
    ["--demo-mounts=adopting"], ["--demo-mounts=remounting"], ["--demo-mounts=failed"], ["--demo-mounts=adopted"],
    ["--demo-mounts=overview"], ["--demo-mounts=unmount"],
    # 2026-11-11: Özet's Kuyruk holding only the welcome's two files (the owner's screenshot): a chip for them.
    ["--demo-unattended=welcome", "--demo-overview=unread", "--page=overview"],
    # 2026-11-11: a stopped run, then its discard / save started elsewhere — running, done, failed.
    ["--demo-features", "--demo-apply=stopped"], ["--demo-features", "--demo-apply=discarding"],
    ["--demo-features", "--demo-apply=discarded"], ["--demo-features", "--demo-apply=saving"],
    ["--demo-features", "--demo-apply=saved"], ["--demo-features", "--demo-apply=closefailed"],
    # D-144: "Bitince ISO oluştur" — the option on (no source here: shown unavailable, with why), its line in a
    # running run's steps, and the ISO after the run in each state on the Apply page and the overview.
    ["--page=apply", "--demo-overview", "--auto-iso"], ["--page=apply", "--demo-overview=running", "--auto-iso"],
    ["--page=apply", "--demo-overview=iso-running"], ["--page=apply", "--demo-overview=iso-done"],
    ["--page=apply", "--demo-overview=iso-failed"], ["--page=apply", "--demo-overview=iso-cancelled"],
    ["--page=apply", "--demo-overview=iso-skipped"], ["--demo-overview=iso-running"], ["--demo-overview=iso-done"],
    # D-145: "Yükle" in each state (About; the status bar and the toasts elsewhere), the status bar's menu, and the
    # changelog window alone (from tests/integration/fixtures/changelog-sample.json).
    ["--page=about", "--demo-update=downloading"], ["--page=about", "--demo-update=verifying"],
    ["--page=about", "--demo-update=ready"], ["--page=about", "--demo-update=install-failed"],
    ["--page=about", "--demo-update=updated"], ["--page=source", "--demo-update=downloading"],
    ["--page=source", "--demo-update=ready"], ["--page=source", "--demo-update=install-failed"],
    ["--page=source", "--demo-update=updated"], ["--page=source", "--demo-update", "--toast-timeout", "--click-at=200,688"],
    ["--demo-changelog"], ["--demo-changelog", "--keys=pgdn,pgdn"], ["--demo-changelog", "--keys=end"],
]
WITH_SOURCE = [["--page=images"], ["--page=images", "--select=2,3"], ["--page=iso"], ["--page=iso", "--demo-usb"],
               ["--page=updates", "--demo-updates"], ["--page=updates", "--demo-updates", "--demo-catalog=dialog"],
               ["--page=images", "--demo-upgrade=dialog"], ["--page=images", "--operation=mount", "--progress=0.4"],
               ["--page=images", "--verified=sound"], ["--page=apply", "--demo-editions"],
               ["--page=images", "--demo-tool=recompress"], ["--page=overview"], ["--demo-overview"],
               ["--demo-overview=running"], ["--demo-overview=done"], ["--page=apply", "--demo-overview"],
               ["--page=updates", "--demo-updates", "--click-at=450,400"],
               # 2026-11-11: each work's own word in the Durum column (a delete said "Bağlanıyor").
               ["--page=images", "--operation=delete", "--select=2"], ["--page=images", "--operation=prepare-delete", "--select=2"],
               ["--page=images", "--operation=delete", "--select=2,3"], ["--page=images", "--operation=rename", "--select=2"],
               # D-144: the Apply summary with the option on and its file; the ISO after the run, from an ISO source.
               ["--page=apply", "--demo-overview", "--auto-iso"], ["--page=apply", "--demo-overview=iso-running"],
               ["--page=apply", "--demo-overview=iso-done"], ["--page=overview", "--demo-overview=iso-running"]]


def render(exe, out, args):
    try:
        done = subprocess.run([str(exe), f"--render={out}", "--scale=1.75", "--size=1207x700", *args], cwd=str(ROOT),
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        return done.returncode
    except subprocess.TimeoutExpired:
        return "timeout"


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    old, new = Path(args[0]).resolve(), Path(args[1]).resolve()
    option = lambda name, default: next((a.split("=", 1)[1] for a in args if a.startswith(f"--{name}=")), default)
    out = Path(option("out", str(ROOT / "build" / "lab" / "perf" / "diff")))
    out.mkdir(parents=True, exist_ok=True)
    iso = option("iso", "")
    cases = [[f"--page={p}"] for p in PAGES] + DEMOS
    if iso:
        cases += [c + [iso] for c in WITH_SOURCE]
    same = different = failed = 0
    for theme in ("dark", "light"):
        for n, case in enumerate(cases):
            label = " ".join(a for a in case if a != iso)
            a, b = out / f"{theme}-{n:03}-old.png", out / f"{theme}-{n:03}-new.png"
            for f in (a, b):
                f.unlink(missing_ok=True)
            codes = (render(old, a, [f"--theme={theme}", *case]), render(new, b, [f"--theme={theme}", *case]))
            if codes != (0, 0) or not a.exists() or not b.exists():
                # A state one build cannot render is only a difference when the other can.
                if codes[0] == codes[1]:
                    print(f"skip  {theme} {label}: both builds exit {codes[0]}")
                    for f in (a, b):
                        f.unlink(missing_ok=True)
                    continue
                failed += 1
                print(f"FAIL  {theme} {label}: exit old {codes[0]}, new {codes[1]}")
                continue
            # RGB: on an RGBA image Pillow's getbbox() looks at the alpha channel alone, and every
            # frame (all opaque) came out "identical" whatever its colours.
            ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
            box = ImageChops.difference(ia, ib).getbbox() if ia.size == ib.size else (0, 0, *ib.size)
            if box is None:
                same += 1
                a.unlink()
                b.unlink()
            else:
                different += 1
                pixels = sum(1 for p in ImageChops.difference(ia, ib).convert("L").getdata() if p) if ia.size == ib.size else -1
                print(f"DIFF  {theme} {label}: {pixels} px in {box} -> {a.name} / {b.name}", flush=True)
    print(f"\n{same} identical, {different} different, {failed} failed -> {out}")
    return 1 if different or failed else 0


if __name__ == "__main__":
    sys.exit(main())
