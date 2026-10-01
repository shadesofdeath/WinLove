"""What each CBS package of an image really carries, for the P07 component catalog (D-059).

    python tools/analyze_cbs.py <scan folder> [--json out.json] [--files]

Reads a tools/lab_scan_components.ps1 output (hives\\COMPONENTS, mum\\, winsxs.csv, cbs.txt). The
COMPONENTS hive says which deployments own a component ("c!<deployment>" values under
DerivedData\\Components) and which packages install a deployment ("i!CBS_<package>" under
CanonicalData\\Deployments, the full identity in the value data). The .mum files give the package
tree (a package's child packages). For every installed package family it prints the bytes of the
WinSxS components its tree owns, and the bytes only that tree owns ("exclusive": what removing it
can free). Cumulative updates (Package_for_RollupFix, Package_N_for_KB…, servicing stack) re-own
whatever they updated and are not counted as owners.
"""
import csv
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import offreg  # noqa: E402

NS = re.compile(r"\{[^}]*\}")
UPDATE = re.compile(r"^(package_\d*_?for_|package_for_)", re.I)
COMPONENT_NAME = re.compile(r"^(.*)_[0-9a-f]{16}_[\d.]+_[^_]+_[0-9a-f]+$")


def family_of(identity: str) -> str:
    return identity.split("~")[0].lower()


def package_identity(data: bytes) -> str:
    # <int32 length><int32 flags><ascii identity of that length>…
    if len(data) < 8:
        return ""
    n = int.from_bytes(data[:4], "little")
    return data[8:8 + n].decode("ascii", "replace")


def main() -> int:
    scan = sys.argv[1]
    out_json = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    with_files = "--files" in sys.argv

    sizes = {}
    with open(os.path.join(scan, "winsxs.csv"), encoding="utf-8") as f:
        for row in csv.DictReader(f):
            sizes[row["Component"].lower()] = int(row["Bytes"])

    installed, visibility = set(), {}
    for line in open(os.path.join(scan, "cbs.txt"), encoding="utf-8"):
        m = re.match(r"\s*(\w+)\s+(0x[0-9a-f]+)\s+(\S+)", line)
        if m and int(m.group(2), 16) >= 0x70:
            installed.add(m.group(3).lower())
            visibility[family_of(m.group(3))] = m.group(1)

    # Package tree from the manifests (installed identities only).
    children = defaultdict(set)
    display = {}
    for name in os.listdir(os.path.join(scan, "mum")):
        ident = name[:-4].lower()
        if ident not in installed:
            continue
        try:
            root = ET.parse(os.path.join(scan, "mum", name)).getroot()
        except ET.ParseError:
            continue
        fam = family_of(ident)
        if root.get("displayName"):
            display[fam] = root.get("displayName")
        for el in root.iter():
            if NS.sub("", el.tag) != "package":
                continue
            ai = next((c for c in el if NS.sub("", c.tag) == "assemblyIdentity"), None)
            if ai is not None and ai.get("name"):
                children[fam].add(ai.get("name").lower())

    hive = offreg.open_hive(os.path.join(scan, "hives", "COMPONENTS"))
    deployments = hive.open(r"CanonicalData\Deployments")
    dep_owners = {}  # deployment key -> installing package families
    for dep in deployments.subkeys():
        owners = set()
        for vname, _, data in deployments.open(dep).values():
            if vname.startswith("i!") and isinstance(data, bytes):
                ident = package_identity(data)
                if ident:
                    owners.add(family_of(ident))
        dep_owners[dep.lower()] = owners

    comps = hive.open(r"DerivedData\Components")
    comp_owners = defaultdict(set)  # component name (no version) -> owning families (updates left out)
    comp_files = defaultdict(set)
    for key in comps.subkeys():
        m = COMPONENT_NAME.match(key)
        cname = (m.group(1) if m else key).lower()
        owners = set()
        files = []
        for vname, _, _ in comps.open(key).values():
            if vname.startswith("c!"):
                owners |= dep_owners.get(vname[2:].lower(), set())
            elif vname.startswith("f!") and with_files:
                files.append(vname[2:])
        owners = {o for o in owners if not UPDATE.match(o)}
        if owners:
            comp_owners[cname] |= owners
            comp_files[cname].update(files)

    owned = defaultdict(set)  # family -> components it owns directly
    for cname, owners in comp_owners.items():
        for o in owners:
            owned[o].add(cname)

    def tree(fam: str, seen=None):
        seen = set() if seen is None else seen
        if fam not in seen:
            seen.add(fam)
            for c in children.get(fam, ()):
                tree(c, seen)
        return seen

    result = []
    for fam in sorted({family_of(i) for i in installed if not i.split("~")[3]}):
        if UPDATE.match(fam):
            continue
        fams = tree(fam)
        mine = set().union(*(owned.get(f, set()) for f in fams))
        total = sum(sizes.get(c, 0) for c in mine)
        exclusive_set = {c for c in mine if comp_owners[c] <= fams or all(
            o in fams or o.startswith(tuple(f + "~" for f in fams)) for o in comp_owners[c])}
        exclusive = sum(sizes.get(c, 0) for c in exclusive_set)
        entry = {"family": fam, "display": display.get(fam, ""), "visibility": visibility.get(fam, ""),
                 "tree": len(fams), "components": len(mine), "bytes": total, "exclusive": exclusive,
                 "exclusiveComponents": sorted(exclusive_set)}
        if with_files:
            entry["files"] = sorted({f for c in exclusive_set for f in comp_files[c]})
        result.append(entry)
    if "--groups" in sys.argv:
        # {"id": [families]} -> bytes only that union of trees owns
        groups = json.load(open(sys.argv[sys.argv.index("--groups") + 1], encoding="utf-8"))
        for gid, fams_list in groups.items():
            union = set()
            for f in fams_list:
                union |= tree(f.lower())
            mine = set().union(*(owned.get(f, set()) for f in union))
            ex = sum(sizes.get(c, 0) for c in mine if comp_owners[c] <= union)
            print(f"GROUP {gid} {ex}")
        return 0
    result.sort(key=lambda r: -r["exclusive"])
    for r in result:
        print(f'{r["exclusive"] / 2**20:8.1f} {r["bytes"] / 2**20:8.1f} {r["components"]:5} '
              f'{r["visibility"][:1]} {r["family"]}  {r["display"][:50]}')
    if out_json:
        json.dump(result, open(out_json, "w", encoding="utf-8"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
