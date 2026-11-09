"""Fails when an i18n/*.txt table misses a UI string from src/ or has one the code no longer uses."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
source = "\n".join(p.read_text(encoding="utf-8") for p in sorted((root / "src").glob("*.[ch]*")))
keys = set(re.findall(r'(?:NEEDS_)?TRANS\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)', source))
# Instrument names reach TRANS through displayName (), the way the parts list shows them.
instruments = re.search(r"instrumentNames\(\)\s*\{(.*?)\};", source, re.S).group(1)
keys |= {n.replace("_", " ").capitalize() for n in re.findall(r'"(\w+)"', instruments)}

failed = False
for table in sorted((root / "i18n").glob("*.txt")):
    mapped = dict(re.findall(r'^"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*$', table.read_text(encoding="utf-8"), re.M))
    for problem, names in (("missing", keys - mapped.keys()), ("unused", mapped.keys() - keys)):
        for name in sorted(names):
            print(f"{table.name}: {problem}: {name}")
            failed = True
    for key, value in mapped.items():
        if set(re.findall(r"%\w+%", key)) != set(re.findall(r"%\w+%", value)):
            print(f"{table.name}: placeholders differ: {key}")
            failed = True
sys.exit(1 if failed else 0)
