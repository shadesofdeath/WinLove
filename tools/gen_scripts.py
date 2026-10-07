"""resources/scripts/*.ps1 -> src/core/generated/Scripts.g.h

Scripts WinLove writes into an image (D-078: programs.ps1) are kept as files of their own, so they
can be read, diffed and parse-checked; the engine gets them as string constants. Fails on a byte
outside ASCII (Windows PowerShell reads a script without a byte order mark as ANSI) and, when the
text changed, on a PowerShell parse error.
"""
import json
import re
import subprocess
import tempfile
from pathlib import Path

from genutil import ROOT, HEADER, pascal, write_if_changed

SCRIPTS = ROOT / "resources" / "scripts"
DST = ROOT / "src" / "core" / "generated" / "Scripts.g.h"
CHUNK = 12000  # MSVC: one string literal piece stays under 16 KB
DELIMITER = "wlps"

PARSE = r"""
param([string] $Path)
$errors = $null
[void] [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref] $null, [ref] $errors)
foreach ($e in $errors) { Write-Output ('{0}:{1}: {2}' -f $Path, $e.Extent.StartLineNumber, $e.Message) }
"""


def load():
    scripts = {}
    for path in sorted(SCRIPTS.glob("*.ps1")):
        data = path.read_bytes()
        bad = next((i for i, b in enumerate(data) if b > 127), None)
        if bad is not None:
            line = data[:bad].count(b"\n") + 1
            raise SystemExit(f"{path.relative_to(ROOT)}:{line}: a byte outside ASCII")
        text = data.decode("ascii").replace("\r\n", "\n")
        if f"){DELIMITER}\"" in text:
            raise SystemExit(f"{path.relative_to(ROOT)}: holds the raw string delimiter")
        scripts[path.stem] = text
    return scripts


def parse_check(scripts):
    with tempfile.TemporaryDirectory() as folder:
        checker = Path(folder) / "parse.ps1"
        checker.write_text(PARSE, encoding="ascii")
        for name, text in scripts.items():
            source = SCRIPTS / f"{name}.ps1"
            result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(checker), str(source)],
                                    capture_output=True, text=True)
            if result.stdout.strip():
                raise SystemExit("PowerShell parse errors:\n" + result.stdout)


TOKENS = ROOT / "WinLove-UI-Handoff" / "01_tokens" / "tokens.json"
TOKEN = re.compile(r"@@(dark|light)\.([A-Za-z.]+)@@")


def fill_tokens(name, text):
    """@@dark.bg.base@@ -> #1A1918: the design tokens are the colours' only source (rule 3)."""
    colors = json.loads(TOKENS.read_text(encoding="utf-8"))["color"]

    def value(match):
        theme, key = match.group(1), match.group(2)
        if key not in colors[theme]:
            raise SystemExit(f"resources/scripts/{name}.ps1: no colour token {theme}.{key}")
        return colors[theme][key]

    return TOKEN.sub(value, text)


def generate(scripts):
    lines = [HEADER.format(tool="gen_scripts.py", src="resources/scripts/*.ps1"), "#pragma once", "", "#include <string_view>", "",
             "namespace wl::core::scripts {", ""]
    for name, text in scripts.items():
        text = fill_tokens(name, text)
        # Line ends stay "\n": a raw string literal turns the source's CR LF into LF anyway, and
        # PowerShell reads either.
        pieces = [text[i:i + CHUNK] for i in range(0, len(text), CHUNK)]
        literal = "\n".join(f'    R"{DELIMITER}({piece}){DELIMITER}"' for piece in pieces)
        lines.append(f"// resources/scripts/{name}.ps1")
        lines.append(f"inline constexpr std::string_view k{pascal(name)} =\n{literal};")
        lines.append("")
    lines.append("} // namespace wl::core::scripts")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    loaded = load()
    output = generate(loaded)
    if not DST.exists() or DST.read_text(encoding="utf-8") != output:
        parse_check(loaded)
    write_if_changed(DST, output)
