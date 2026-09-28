"""Run every code generator. Called by `./build.ps1 -Gen`."""
import gen_icons
import gen_strings
import gen_tokens
from genutil import write_if_changed

if __name__ == "__main__":
    write_if_changed(gen_tokens.DST, gen_tokens.generate())
    write_if_changed(gen_icons.DST, gen_icons.generate())
    tables = gen_strings.load()
    problems = gen_strings.validate(tables)
    if problems:
        raise SystemExit("strings validation failed:\n  " + "\n  ".join(sorted(problems)))
    write_if_changed(gen_strings.DST, gen_strings.generate(tables))
