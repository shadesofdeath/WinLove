"""resources/strings/{en,tr}.json (welcome section) + resources/strings/welcome/<code>.json
-> resources/strings/welcome-langs.json

One combined file { "<code>": { "<welcomeKey>": "text", ... }, ... } that the app embeds and the
welcome wizard reads, so it can show itself in the installed Windows' language (D-103). English is
always present and first; any key a translation is missing falls back to the English text, so every
language is complete. The welcome/ folder may be empty or partial — only en/tr are required.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRINGS = ROOT / "resources" / "strings"
WELCOME_DIR = STRINGS / "welcome"
DST = STRINGS / "welcome-langs.json"


def _welcome_section(lang: str) -> dict:
    data = json.loads((STRINGS / f"{lang}.json").read_text(encoding="utf-8"))
    return dict(data.get("welcome", {}))


def generate() -> str:
    english = _welcome_section("en")
    keys = list(english.keys())  # the authoritative key set and order

    langs: dict[str, dict] = {"en": english, "tr": _welcome_section("tr")}
    if WELCOME_DIR.is_dir():
        for path in sorted(WELCOME_DIR.glob("*.json")):
            code = path.stem
            if code in ("en", "tr"):
                continue
            try:
                table = json.loads(path.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                continue
            if isinstance(table, dict):
                langs[code] = table

    out: dict[str, dict] = {}
    for code in ["en", "tr"] + sorted(c for c in langs if c not in ("en", "tr")):
        table = langs[code]
        # Complete every language against the English key set; English fills any gap.
        out[code] = {k: str(table.get(k, english[k])) for k in keys}

    return json.dumps(out, ensure_ascii=False, indent=2) + "\n"


if __name__ == "__main__":
    DST.write_text(generate(), encoding="utf-8")
    print(f"wrote {DST}")
