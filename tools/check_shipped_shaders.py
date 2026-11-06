#!/usr/bin/env python3
"""Check that a built or deployed DLL really contains the shader strings we tested.

Every shader test in tests/test_mask_shaders.cpp has a limit: it proves things about
src/MaskShaders.h, not about the binary that ships. The gap is not theoretical - a stale
build, a second definition, or a build from a different tree all produce a green suite
while the running plugin uses other HLSL.

The gap closes here because both masks are plain `constexpr char[]` string literals, so
the exact bytes are embedded in the DLL and can be found in it. This script extracts them
back out and compares byte for byte against the header.

    python tools/check_shipped_shaders.py <dll> [<header>]

Exit status is 0 only when every shader string in the header appears verbatim in the DLL.

What this does NOT prove: that the LOADED plugin is the file you checked. It proves the
file you checked contains the shader the tests ran against. Confirming the running process
has those bytes is a separate step (the deploy record's SHA256 against the installed file,
which is what tools/deploy-plugin.ps1 attests).

Per-shader reporting matters rather than being decoration. A binary can contain one shader
and not the other - the vertex shader is the smaller and more stable of the two - so a
checker that collapsed to a single "all present" flag could be satisfied by the one that
never changed while the interesting one was missing.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

DEFAULT_HEADER = Path(__file__).resolve().parent.parent / "src" / "MaskShaders.h"


def header_strings(header: Path) -> dict[str, bytes]:
    """Pull every `constexpr char kName[] = R"(...)"` out of the header."""
    text = header.read_text(encoding="utf-8", errors="replace")
    found = {}
    for m in re.finditer(r'constexpr\s+char\s+(k\w+)\[\]\s*=\s*R"\((.*?)\)";', text, re.S):
        found[m.group(1)] = m.group(2).encode("utf-8")
    return found


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2

    dll = Path(argv[1])
    header = Path(argv[2]) if len(argv) > 2 else DEFAULT_HEADER

    if not dll.is_file():
        print(f"no such file: {dll}")
        return 2
    if not header.is_file():
        print(f"no such header: {header}")
        return 2

    strings = header_strings(header)
    if not strings:
        # A silent zero here would read as success, which is the failure mode this
        # whole check exists to avoid. Refuse instead.
        print(f"FAIL: no shader literals found in {header} - the parser or the header changed")
        return 1

    blob = dll.read_bytes()
    ok = True
    duplicates = []
    for name, literal in strings.items():
        occurrences = blob.count(literal)
        present = occurrences > 0
        size = len(literal)
        print(f"{'ok  ' if present else 'FAIL'} {name} ({size} bytes, x{occurrences}) {'found in binary' if present else 'NOT in binary'}")
        ok = ok and present
        if occurrences > 1:
            # More than one copy means a second definition could be the one that ships, which
            # is exactly the state this check exists to catch. Report it rather than passing
            # quietly on the first match.
            duplicates.append(f"{name} appears {occurrences} times")

    for duplicate in duplicates:
        print(f"WARN {duplicate}: a second definition may be the one compiled in")

    print()
    print(f"{dll}")
    print("PASS: every tested shader string is embedded in the binary" if ok
          else "FAIL: the binary does not contain what the tests were run against")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
