#!/usr/bin/env python3
from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

props = {}
for line in (ROOT / "library.properties").read_text(encoding="utf-8").splitlines():
    if "=" in line and not line.lstrip().startswith("#"):
        k, v = line.split("=", 1)
        props[k.strip()] = v.strip()

arduino_version = props.get("version")
pio_version = json.loads((ROOT / "library.json").read_text(encoding="utf-8")).get("version")
header = (ROOT / "src" / "DCC_LocoAddr.h").read_text(encoding="utf-8")
m = re.search(r'#define\s+DCC_LOCOADDR_VERSION\s+"([^"]+)"', header)
header_version = m.group(1) if m else None

versions = {
    "library.properties": arduino_version,
    "library.json": pio_version,
    "DCC_LOCOADDR_VERSION": header_version,
}

for where, version in versions.items():
    print(f"{where}: {version}")

if None in versions.values() or len(set(versions.values())) != 1:
    print("ERREUR : les versions ne sont pas synchronisées.", file=sys.stderr)
    sys.exit(1)

print("OK : versions synchronisées.")
