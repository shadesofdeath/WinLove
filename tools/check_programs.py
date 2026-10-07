"""Checks resources/catalog/programs.json against winget's index (D-078): every id must be a package.

    python tools/check_programs.py [<index.db>]

Without an argument the index wlcli keeps (wlcli programs-index --cache=build/lab/winget) is used.
Prints the name and latest version of each id; exits 1 when one is not in the index.
"""
import json
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "resources" / "catalog" / "programs.json"


def main() -> int:
    index = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "lab" / "winget" / "index.db"
    db = sqlite3.connect(f"file:{index}?mode=ro", uri=True)
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    missing = []
    seen = set()
    total = 0
    for category in catalog["categories"]:
        print(f"{category['en']}:")
        for pid in category["programs"]:
            total += 1
            if pid.lower() in seen:
                missing.append(f"{pid} (twice)")
            seen.add(pid.lower())
            row = db.execute("SELECT id, name, latest_version FROM packages WHERE lower(id) = lower(?)", (pid,)).fetchone()
            if row is None:
                missing.append(pid)
                print(f"  MISSING  {pid}")
            elif row[0] != pid:
                missing.append(f"{pid} (is {row[0]})")
                print(f"  CASE     {pid} -> {row[0]}")
            else:
                print(f"  ok       {pid:<40} {row[1]:<40} {row[2]}")
    for bundle in catalog.get("bundles", []):
        for pid in bundle["programs"]:
            total += 1
            if db.execute("SELECT id FROM packages WHERE id = ?", (pid,)).fetchone() is None:
                missing.append(f"{pid} (bundle {bundle['id']})")
                print(f"  MISSING  {pid} (bundle {bundle['id']})")
    for category in catalog["categories"]:
        for tag in category.get("tags", []):
            n = db.execute("SELECT count(*) FROM tags2_map m JOIN tags2 t ON t.rowid = m.tag WHERE t.tag = ?", (tag,)).fetchone()[0]
            if n == 0:
                missing.append(f"tag {tag} ({category['id']}) tags nothing")
            print(f"  tag      {category['id']:<12} {tag:<20} {n}")
    print(f"\n{total} id(s), {len(missing)} problem(s)")
    for m in missing:
        print("  " + m)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
