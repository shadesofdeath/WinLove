"""Sizes inside a mounted Windows image, for the P07 component survey (tools/lab_scan_components.ps1).

    python tools/scan_image_tree.py <mount> <out-folder>

Writes tree.csv (every folder down to 4 levels), winsxs.csv (WinSxS component folders summed by name,
version / hash dropped), files.csv (every file of 1 MB or more) and drivers.csv (inbox driver packages:
folder, INF, Class, ClassGuid, Provider, size). Never follows a junction or symlink: an image's
"Documents and Settings" junction points at the HOST's C:\\Users.
"""
import csv
import os
import re
import stat
import sys
from collections import defaultdict

SXS_NAME = re.compile(r"_[0-9a-f]{16}_[\d.]+_[^_]+_[0-9a-f]+$")


def is_link(entry: os.DirEntry) -> bool:
    """A junction / symlink (name-surrogate reparse tag). Every file of a DISM mount is a reparse point
    too (the WIM filter's tag) until it is written: those are real files and are counted."""
    try:
        st = entry.stat(follow_symlinks=False)
    except OSError:
        return True
    if not st.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        return False
    return bool(st.st_reparse_tag & 0x20000000)


def walk(root: str):
    """(relative path, size) of every regular file; reparse points are skipped, unreadable folders too."""
    stack = [""]
    while stack:
        rel = stack.pop()
        try:
            entries = list(os.scandir(os.path.join(root, rel)))
        except OSError:
            continue
        for e in entries:
            if is_link(e):
                continue
            child = os.path.join(rel, e.name) if rel else e.name
            try:
                if e.is_dir(follow_symlinks=False):
                    stack.append(child)
                else:
                    yield child, e.stat(follow_symlinks=False).st_size
            except OSError:
                continue


def inf_version(path: str) -> dict:
    fields = {"Class": "", "ClassGuid": "", "Provider": ""}
    try:
        raw = open(path, "rb").read(16384)
    except OSError:
        return fields
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("latin-1")
    for line in text.splitlines()[:120]:
        m = re.match(r'\s*(Class|ClassGuid|Provider)\s*=\s*"?([^";\r\n]+)', line, re.I)
        if m:
            key = {"class": "Class", "classguid": "ClassGuid", "provider": "Provider"}[m.group(1).lower()]
            if not fields[key]:
                fields[key] = m.group(2).strip()
    return fields


def main() -> int:
    mount, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    tree = defaultdict(int)
    sxs = defaultdict(int)
    repo = defaultdict(int)
    big = []
    count = 0
    for rel, size in walk(mount):
        count += 1
        parts = rel.split("\\")
        for d in range(1, min(4, len(parts) - 1) + 1):
            tree["\\".join(parts[:d])] += size
        low = [p.lower() for p in parts]
        if len(parts) >= 4 and low[0] == "windows" and low[1] == "winsxs":
            sxs[SXS_NAME.sub("", parts[2])] += size
        if len(parts) >= 6 and low[0] == "windows" and low[1] == "system32" and low[2] == "driverstore" and low[3] == "filerepository":
            repo[parts[4]] += size
        if size >= 1 << 20:
            big.append((rel, size))

    def write(name, header, rows):
        with open(os.path.join(out, name), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(rows)
        print(f"  {name} ({len(rows)} rows)")

    write("tree.csv", ["Folder", "Bytes"], sorted(tree.items(), key=lambda kv: -kv[1]))
    write("winsxs.csv", ["Component", "Bytes"], sorted(sxs.items(), key=lambda kv: -kv[1]))
    write("files.csv", ["Path", "Bytes"], sorted(big, key=lambda kv: -kv[1]))

    base = os.path.join(mount, "Windows", "System32", "DriverStore", "FileRepository")
    rows = []
    for folder, size in repo.items():
        infs = [n for n in os.listdir(os.path.join(base, folder)) if n.lower().endswith(".inf")]
        v = inf_version(os.path.join(base, folder, infs[0])) if infs else {"Class": "", "ClassGuid": "", "Provider": ""}
        rows.append((folder, infs[0] if infs else "", v["Class"], v["ClassGuid"], v["Provider"], size))
    rows.sort(key=lambda r: (r[2].lower(), r[0]))
    write("drivers.csv", ["Folder", "Inf", "Class", "ClassGuid", "Provider", "Bytes"], rows)

    fonts = os.path.join(mount, "Windows", "Fonts")
    rows = sorted(((e.name, e.stat().st_size) for e in os.scandir(fonts) if e.is_file()), key=lambda r: -r[1])
    write("fonts.csv", ["Name", "Bytes"], rows)
    print(f"  {count} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
