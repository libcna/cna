#!/usr/bin/env python3
"""plans/plan_dx9.md D9-73: compare our compiled D3D9 shader bytecode against the bytecode
Microsoft actually shipped inside FNA's original XNA Stock Effects .fxb files.

Why the comparison strips comment tokens: D3D9 shader bytecode carries the constant
table (CTAB) and the compiler's own creator string in comment blocks. Those legitimately
differ between compiler versions and source paths, so a raw byte compare always reports
0 matches even when the instruction streams are identical. Stripping comment tokens and
comparing what is left compares the actual shader.

Result on 2026-07-14: 61 of our 66 shaders matched Microsoft's shipped instruction stream
exactly. The 5 that did not are all PixelLighting vertex variants, and the cause is a
compiler-version difference (d3dcompiler_47 vs the XNA-era D3DCompiler_43), NOT a compile
flag -- OPTIMIZATION_LEVEL0/1/2/3, SKIP_OPTIMIZATION and AVOID_FLOW_CONTROL were all tried
and none matched.

usage: compare_against_fxb.py <dir-with-our-.bin-files> [<path-to-FNA-FXB-dir>]
"""

import struct
import sys
from pathlib import Path

DEFAULT_FXB = Path("/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/"
                   "StockEffects/FXB")

EFFECTS = ["BasicEffect", "AlphaTestEffect", "DualTextureEffect",
           "EnvironmentMapEffect", "SkinnedEffect", "SpriteEffect"]


def strip_comments(b: bytes):
    """Keep the version token, the instruction tokens, and the END token. Drop comment blocks.

    D3D9 bytecode layout: a DWORD version token, then DWORD tokens until the END token
    (0x0000FFFF). A comment block has 0xFFFE in its low 16 bits and its length (in DWORDs)
    in the high 16 bits -- that is where CTAB and the compiler creator string live.
    """
    if len(b) < 4:
        return None
    out = [b[0:4]]
    i = 4
    while i + 4 <= len(b):
        tok = struct.unpack_from("<I", b, i)[0]
        if tok == 0x0000FFFF:
            out.append(b[i:i + 4])
            return b"".join(out)
        if (tok & 0xFFFF) == 0xFFFE:
            i += 4 + 4 * ((tok >> 16) & 0x7FFF)
            continue
        out.append(b[i:i + 4])
        i += 4
    return b"".join(out)


def extract_shaders(fxb: bytes):
    """Scan an Effect binary for embedded D3D9 shaders by looking for version tokens.

    vs_x_y = 0xFFFE____, ps_x_y = 0xFFFF____, major version 1..3. A few spurious hits are
    possible (a constant could coincidentally look like a version token); they are harmless
    -- random data will never equal a real 400..2200-byte shader.
    """
    found = []
    for i in range(0, len(fxb) - 4, 4):
        tok = struct.unpack_from("<I", fxb, i)[0]
        if (tok & 0xFFFF0000) in (0xFFFE0000, 0xFFFF0000) and 1 <= ((tok >> 8) & 0xFF) <= 3:
            j = i
            while j + 4 <= len(fxb):
                t = struct.unpack_from("<I", fxb, j)[0]
                if t == 0x0000FFFF:
                    found.append(fxb[i:j + 4])
                    break
                if (t & 0xFFFF) == 0xFFFE and j != i:
                    j += 4 + 4 * ((t >> 16) & 0x7FFF)
                    continue
                j += 4
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ours = Path(sys.argv[1])
    fxbd = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_FXB

    total_hit = total = 0
    mismatches = []
    for eff in EFFECTS:
        fxb_path = fxbd / f"{eff}.fxb"
        if not fxb_path.exists():
            print(f"{eff:22s} SKIP -- {fxb_path} not found")
            continue
        ms = {s for s in (strip_comments(x) for x in extract_shaders(fxb_path.read_bytes())) if s}
        mine = sorted(ours.glob(f"{eff}.*.bin"))
        hit = 0
        for f in mine:
            if strip_comments(f.read_bytes()) in ms:
                hit += 1
            else:
                mismatches.append(f.name)
        total_hit += hit
        total += len(mine)
        print(f"{eff:22s} {hit:2d}/{len(mine):2d} of our shaders match Microsoft's shipped "
              f"instruction stream")

    print(f"\nTOTAL: {total_hit}/{total} exact instruction-stream matches")
    if mismatches:
        print("\nDivergent (must be proven equivalent against the D9-A oracle, "
              "per plans/plan_dx9.md design decision 4):")
        for m in mismatches:
            print(f"  - {m}")


if __name__ == "__main__":
    main()
