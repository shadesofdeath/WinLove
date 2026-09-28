"""resources/strings/{tr,en}.json -> src/app/generated/StringKeys.g.h

The JSON files are the living source of all UI text (seeded from the design handoff's 07_copy;
new pages add their keys here). Validation — fails the build step on:
- a key present in one language but not the other
- different {placeholder} sets for the same key
- empty values, or two keys mapping to the same C++ enum name
"""
import json
import re
import sys

from genutil import ROOT, HEADER, pascal, write_if_changed

STRINGS = ROOT / "resources" / "strings"
LANGS = ["tr", "en"]
DST = ROOT / "src" / "app" / "generated" / "StringKeys.g.h"
PLACEHOLDER = re.compile(r"\{(\w+)\}")


def flatten(obj: dict, prefix: str = "") -> dict:
    flat = {}
    for k, v in obj.items():
        key = f"{prefix}.{k}" if prefix else k
        if isinstance(v, dict):
            flat.update(flatten(v, key))
        else:
            flat[key] = v
    return flat


def load() -> dict:
    return {lang: flatten(json.loads((STRINGS / f"{lang}.json").read_text(encoding="utf-8"))) for lang in LANGS}


def validate(tables: dict) -> list[str]:
    errors = []
    base = tables[LANGS[0]]
    for lang in LANGS[1:]:
        other = tables[lang]
        errors += [f"missing in {lang}: {k}" for k in base.keys() - other.keys()]
        errors += [f"missing in {LANGS[0]}: {k}" for k in other.keys() - base.keys()]
        for k in base.keys() & other.keys():
            if set(PLACEHOLDER.findall(base[k])) != set(PLACEHOLDER.findall(other[k])):
                errors.append(f"placeholder mismatch: {k}")
    for lang, table in tables.items():
        errors += [f"empty value in {lang}: {k}" for k, v in table.items() if not str(v).strip()]
    seen = {}
    for k in base:
        e = pascal(k)
        if e in seen:
            errors.append(f"enum name collision: {k} / {seen[e]}")
        seen[e] = k
    return errors


def generate(tables: dict) -> str:
    keys = sorted(tables[LANGS[0]])
    out = [HEADER.format(src="resources/strings/*.json", tool="gen_strings.py"),
           "#pragma once", "#include <array>", "#include <cstdint>", "",
           "namespace wl::app {", "",
           "enum class Str : std::uint16_t {"]
    out += [f"    {pascal(k)}," for k in keys]
    out += ["    Count", "};", "",
            f"inline constexpr std::size_t kStrCount = {len(keys)};",
            "inline constexpr std::array<const char*, kStrCount> kStrKeys = {"]
    out += [f'    "{k}",' for k in keys]
    out += ["};", "", "} // namespace wl::app", ""]
    return "\n".join(out)


if __name__ == "__main__":
    tables = load()
    problems = validate(tables)
    if problems:
        print("strings validation failed:\n  " + "\n  ".join(sorted(problems)), file=sys.stderr)
        sys.exit(1)
    write_if_changed(DST, generate(tables))
