"""Enforce layer boundaries (docs/ARCHITECTURE.md §1) on #include "..." lines.

Linking already keeps core and ui apart, but header-only includes would slip through;
this check catches them. Run by ./build.ps1 on every build.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

ALLOWED = {
    "base": {"base"},
    "core": {"base", "core"},
    "ui": {"base", "ui"},
    "app": {"base", "core", "ui", "app"},
    "cli": {"base", "core", "cli"},
}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"/]+)/', re.M)


def main() -> int:
    violations = []
    for file in SRC.rglob("*"):
        if file.suffix not in {".h", ".cpp", ".rc"}:
            continue
        layer = file.relative_to(SRC).parts[0]
        allowed = ALLOWED.get(layer)
        if allowed is None:
            violations.append(f"{file.relative_to(ROOT)}: unknown layer '{layer}' (add it to tools/check_layers.py)")
            continue
        for target in INCLUDE.findall(file.read_text(encoding="utf-8", errors="replace")):
            if target in ALLOWED and target not in allowed:
                violations.append(f"{file.relative_to(ROOT)}: '{layer}' must not include '{target}/'")
    if violations:
        print("layer violations:\n  " + "\n  ".join(violations), file=sys.stderr)
        return 1
    print("layers ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
