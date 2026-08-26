#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Emit the Emscripten -sEXPORTED_FUNCTIONS list for the C ABI.

plans/plan_cabi.md CABI-14. The ELF build declares its surface with a pattern --
`cna_*` is public, everything else local (`cmake/CnaCApiExports.map`). wasm-ld has no
counterpart: Emscripten needs every exported name spelled out, and anything missing from the list
is simply absent from the module, with no diagnostic at build time.

So the list is generated from the same headers the ELF rule covers, using the parser
`check_declared_exports.py` already runs, rather than maintained by hand beside it. A route added
to a header reaches the wasm module for the same reason it reaches the shared library: because it
was declared, not because somebody remembered.

Emscripten prefixes C symbols with an underscore. The runtime helpers are included because a
module that cannot allocate or free memory cannot receive a string from its caller.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_declared_exports import declared_routes  # noqa: E402

# Not C ABI routes, but a consumer cannot pass a string or a struct in without them.
RUNTIME_HELPERS = ("_malloc", "_free")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers", required=True, type=Path,
                        help="Directory holding the public C headers (modules/c-api/include/CNA/C).")
    parser.add_argument("--output", type=Path,
                        help="Write the JSON array here instead of stdout.")
    arguments = parser.parse_args()

    routes = sorted(declared_routes(arguments.headers))
    if not routes:
        print(f"error: no CNA_C_API routes found under {arguments.headers}", file=sys.stderr)
        return 2

    exported = [f"_{route}" for route in routes] + list(RUNTIME_HELPERS)
    text = json.dumps(exported, separators=(",", ":"))
    if arguments.output:
        arguments.output.write_text(text + "\n")
        print(f"{len(routes)} routes + {len(RUNTIME_HELPERS)} helpers -> {arguments.output}",
              file=sys.stderr)
    else:
        sys.stdout.write(text + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
