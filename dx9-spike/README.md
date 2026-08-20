# `dx9-spike` — proven artifacts from `plans/plan_dx9.md` Phase D9-0

Everything here **has actually been run and has actually worked** on this machine
(2026-07-14). None of it is a sketch. It exists because the Phase D9-0 spikes were executed
in a session scratchpad that does not survive, and rewriting working, proven code from a
plan's prose is a waste.

**`fxc_tool.cpp` and `compare_against_fxb.py` have moved** (Phase D9-7, `D9-71`) to their real home:
`src/CNA/Internal/Backends/D3D9/shaders/` (alongside the new `compile_shaders_sm2.py` driver and
the checked-in, auto-generated `d3d9_shaders.hpp`).

**`xna-oracle/Oracle.cs` has also moved** (Phase D9-A, `D9-A3`) to `tools/xna-oracle/Oracle.cs` —
rewritten there to be scene-driven (reads `tools/xna-oracle/scenes/*.scene`, the same declarative
format `tools/xna-oracle/CnaOracleRender.cpp` reads) rather than hardcoding one triangle. See
`tools/xna-oracle/README.md` for the current build/run commands for both sides of the diff
harness. This directory (`dx9-spike/`) now keeps only what has not moved yet — nothing, as of
`D9-A3` (2026-07-15); it stays checked in as this Phase D9-0/D9-A history's own record.

---

## `fxc_tool.cpp` — the shader compiler (`D9-1` ✅)

Compiles one entry point of a Microsoft XNA Stock Effect `.fx` to real D3D9 bytecode.
Cross-built with MinGW-w64, run under Wine. Adds the one thing
`D3DCommon/shaders/hlsl_compiler_tool.cpp` lacks and the `.fx` files require: an
`ID3DInclude` handler that resolves `Macros.fxh` / `Common.fxh` / `Lighting.fxh` /
`Structures.fxh` relative to the `.fx` file's own directory.

```bash
x86_64-w64-mingw32-g++ -std=c++23 fxc_tool.cpp -o fxc_tool.exe \
    -ld3dcompiler -static-libgcc -static-libstdc++

export WINEPREFIX=$HOME/.wine-cna-d3d9-spike        # <- MUST be this one, see below
wine ./fxc_tool.exe <path>/BasicEffect.fx VSBasic vs_2_0 out.bin
```

**Result: 66/66 entry points across all 6 stock effects compiled, 0 failures**, from
Microsoft's **unmodified** sources.

Two findings that save the next person a day:

- **No source preprocessing is needed.** The real fxc silently ignores the Effect-framework
  tail (`VertexShader VSArray[]`, `int VSIndices[]`, `Technique`) when invoked with
  `/T vs_2_0`. The `--strip` flag in this tool exists only because an earlier hypothesis
  said it would be necessary; **it is not, and the vendored `.fx` files must stay
  byte-identical to Microsoft's** (plan design decision 3). Do not use `--strip`.
- The entry-point list must be **parsed from the `.fx` files' own `compile vs_2_0 …` /
  `compile ps_2_0 …` statements**, never hand-maintained:
  ```bash
  grep -o "compile [vp]s_2_0 [A-Za-z0-9_]*" *.fx | sort -u
  ```

## `compare_against_fxb.py` — the bytecode oracle (`D9-73`)

Compares our compiled output against the bytecode Microsoft actually shipped, inside FNA's
original `.fxb` files. **61 of 66 matched exactly.**

The 5 that did not are all `PixelLighting` vertex variants
(`VSBasicPixelLighting`, `VSBasicPixelLightingTx`,
`VSSkinnedPixelLighting{One,Two,Four}Bones`). The cause is a **compiler-version** difference
(we have `d3dcompiler_47`; Microsoft built these with the XNA-era `D3DCompiler_43`), **not a
compile flag** — `OPTIMIZATION_LEVEL0/1/2/3`, `SKIP_OPTIMIZATION` and `AVOID_FLOW_CONTROL`
were all tried, none matched.

The project owner decided (2026-07-14) that **CNA compiles the shaders itself** and the
`.fxb` is a verification oracle only. That decision carries an obligation: **those 5 shaders
must be proven equivalent against the Phase D9-A oracle.** Keep this script in CI-adjacent
reach; it is how you notice if a future compiler bump silently changes the answer.

## `xna-oracle/Oracle.cs` — real XNA 4.0, rendering (`D9-A1`/`D9-A2` ✅, moved `D9-A3`)

**Moved to `tools/xna-oracle/Oracle.cs`, 2026-07-15** — rewritten to be scene-driven (see that
file's own header comment and `tools/xna-oracle/README.md` for current build/run commands). The
DXVK-into-this-prefix fix this section used to flag as still-needed is **done**: `dxvk-setup
install` was run against `~/.wine-cna-xna40` as part of `D9-A3`'s own verification (adapter string
now correctly reports the real `AMD Radeon 780M (RADV PHOENIX)`, not WineD3D's spoofed `ATI
Radeon HD 5600 Series`) — see `plans/plan_dx9.md` `D9-A4`'s own closure note.

---

## Wine prefixes (these DO survive; they live in `$HOME`)

`programs.md` in the repo documents the D3D11 prefix but knows nothing about these two.
That gap should be closed when this work lands.

| Prefix | Arch | Contains | Do not confuse with |
|--------|------|----------|---------------------|
| `~/.wine-cna-d3d9-spike` | win64 | **The real Microsoft `d3dcompiler_47.dll`** (4,346,120 bytes, `winetricks -q d3dcompiler_47`, native override) **+ DXVK** (added `D9-74`, `dxvk-setup install` — same command as `~/.wine-cna-d3d11`'s own DX-2 install). Now has both the real compiler AND a live D3D9 device in one prefix, per `D9-74`'s own row recommendation (option (a): install DXVK here rather than stand up a 4th prefix, since by this point the compiled bytecode is already embedded in a checked-in header and the compiler DLL no longer needs to coexist with the runtime device in the same *step*, only the same *machine*). | — |
| `~/.wine-cna-xna40` | **win32** | .NET Framework 4.0 + XNA 4.0 Redistributable (`winetricks -q dotnet40 xna40`); all ten `Microsoft.Xna.Framework.*` assemblies in the GAC; an in-prefix `csc.exe` **+ DXVK** (added `D9-A3`, `dxvk-setup install`, 32-bit `wine32` build — real XNA now runs through the same DXVK D3D9 path CNA/D3D9 does, not WineD3D) | — |
| `~/.wine-cna-d3d11` | win64 | **Wine's builtin `d3dcompiler_47.dll`** (1,093,743 bytes) + DXVK | **Leave it alone.** It is what the existing D3D11/D3D12 CTests run against. Wine's builtin compiler **cannot** compile SM2/SM3 — that is exactly why `~/.wine-cna-d3d9-spike` exists as a separate prefix. |

The critical, non-obvious fact: **Wine's builtin `d3dcompiler_47.dll` is backed by
vkd3d-shader, whose SM1/2/3 code generator is incomplete.** It fails outright on an ordinary
alpha-test ternary (`E5017: Aborting due to not yet implemented feature: SM1 non-float
expression`), and where it does succeed it emits garbage — a 72-bone skinning shader came out
at **1.4 MB**, fully unrolled, because it cannot do relative addressing. The real Microsoft
DLL compiles the same shader to **2,160 bytes**. Any D3D9 shader work done in a prefix with
the builtin compiler is worthless, and it fails in ways that look like your bug, not the
compiler's.
