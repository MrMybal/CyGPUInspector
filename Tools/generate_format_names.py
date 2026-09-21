# Regenerates CyGPUInspectorCore/Source/FormatNames.inc from the vendored ReShade SDK header.
#
# Run from the repository root:  python Tools/generate_format_names.py
#
# Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "ThirdParty", "reshade", "include", "reshade_api_format.hpp")
OUTPUT = os.path.join(ROOT, "CyGPUInspectorCore", "Source", "FormatNames.inc")

def main():
    if not os.path.isfile(HEADER):
        print("ReShade header not found: %s" % HEADER)
        return 1

    with open(HEADER, encoding="utf8", errors="ignore") as handle:
        source = handle.read()

    match = re.search(r"enum class format : uint32_t\s*\{(.*?)\n\t\};", source, re.S)
    if match is None:
        print("could not locate 'enum class format' in the header")
        return 1

    body = re.sub(r"/\*.*?\*/", "", re.sub(r"//.*", "", match.group(1)), flags=re.S)

    seen = set()
    entries = []
    for entry in re.finditer(r"([a-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*,", body):
        name, value = entry.group(1), int(entry.group(2), 0)
        if value in seen:
            continue  # alias of an earlier name, keep the first spelling
        seen.add(value)
        entries.append((name, value))

    entries.sort(key=lambda item: item[1])

    lines = [
        "// CyGPUInspector - generated from ThirdParty/reshade/include/reshade_api_format.hpp.",
        "// The values are DXGI_FORMAT compatible; the FourCC ones are ReShade extensions.",
        "// Do not edit by hand: regenerate with Tools/generate_format_names.py.",
        "",
    ]
    lines += ['\t{ %du, "%s" },' % (value, name) for name, value in entries]

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")

    print("wrote %d formats to %s" % (len(entries), OUTPUT))
    return 0

if __name__ == "__main__":
    sys.exit(main())
