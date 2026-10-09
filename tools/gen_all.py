"""Run every code generator. Called by `./build.ps1 -Gen`."""
import gen_icons
import gen_scripts
import gen_strings
import gen_tokens
import gen_welcome_langs
from genutil import write_if_changed

if __name__ == "__main__":
    write_if_changed(gen_tokens.DST, gen_tokens.generate())
    write_if_changed(gen_icons.DST, gen_icons.generate())
    tables = gen_strings.load()
    problems = gen_strings.validate(tables)
    if problems:
        raise SystemExit("strings validation failed:\n  " + "\n  ".join(sorted(problems)))
    write_if_changed(gen_strings.DST, gen_strings.generate(tables))
    scripts = gen_scripts.load()
    output = gen_scripts.generate(scripts)
    if not gen_scripts.DST.exists() or gen_scripts.DST.read_text(encoding="utf-8") != output:
        gen_scripts.parse_check(scripts)
    write_if_changed(gen_scripts.DST, output)
    write_if_changed(gen_welcome_langs.DST, gen_welcome_langs.generate())
