# FNA reference harness: how to regenerate reference data

Written as the closing documentation task for Phase 53 (Tasks 471-480, "FNA comparison harness").
This phase built a way to check CNA's own C++ implementation against the real, running FNA
implementation — not just FNA's source code — for API surfaces that don't need a live
`GraphicsDevice`.

## Why this exists

Reading FNA's C# source and re-implementing it in C++ is this project's normal porting workflow
(`CLAUDE.md`'s "Behavior Fidelity" section). But a hand read-and-transcribe pass can still get a
formula, an enum's numeric values, or a bit-packing layout subtly wrong — and a second hand
re-derivation (e.g. `tests/PackedVectorGolden.md`'s own Python re-implementation, Task 197) can
make the exact same mistake the first pass did, since both are just careful reading of the same
source.

This harness instead *runs* the real FNA.dll (built from this project's own documented "Source
Reference" checkout, `CLAUDE.md`) and dumps its actual output as JSON, then dumps CNA's own C++
output in the same shape, and diffs the two automatically. Agreement is a genuine, independent
confirmation; disagreement is either a real CNA bug or a real, previously-wrong assumption — Task
479's own first real run found one of each (see below).

## The three pieces

```
tools/fna-reference/     — C# console app, references the real FNA.dll, emits JSON
tools/cna-reference/     — C++ console app (cna_reference_dump), mirrors the same categories
scripts/compare-fna-reference.py — diffs the two JSON outputs key-for-key
```

### 1. Build the local FNA reference checkout (one-time, or after a submodule reset)

```bash
cd /rv/data/library/github.com/FNA-XNA/FNA
git submodule update --init lib/FAudio lib/FNA3D lib/SDL2-CS lib/SDL3-CS lib/Theorafile lib/dav1dfile
xbuild FNA.csproj /p:Configuration=Debug
```

Produces `bin/Debug/FNA.dll`, which `tools/fna-reference/FnaReference.csproj` references by
absolute `HintPath`. Needs `mono`/`xbuild` (this sandbox has no `dotnet` CLI/NuGet, so
`FnaReference.csproj` is deliberately old-style `ToolsVersion=4.0`, matching FNA's own
`FNA.csproj` exactly).

### 2. Generate the FNA-side reference values

```bash
cd tools/fna-reference
xbuild FnaReference.csproj /p:Configuration=Debug
mono bin/Debug/FnaReference.exe /tmp/fna-reference-values.json
```

### 3. Generate the CNA-side reference values

```bash
cmake --build cmake-build-debug --target cna_reference_dump
./cmake-build-debug/cna_reference_dump /tmp/cna-reference-values.json
```

`cna_reference_dump` is a normal CMake target (built whenever `CNA_BUILD_EXAMPLES` is on, any
graphics renderer) — no special setup needed beyond a normal CNA build.

### 4. Compare

```bash
python3 scripts/compare-fna-reference.py /tmp/fna-reference-values.json /tmp/cna-reference-values.json
```

Prints `PASS`/`FAIL` plus a per-key breakdown of any mismatch. The comparison is one-directional:
every key on the FNA side must exist and match on the CNA side; CNA-only keys (real CNAEXT
extensions, e.g. `PrimitiveType.PointListEXT`) are not reported as mismatches. Float comparisons
use an absolute tolerance (`--tolerance`, default `1e-4`).

Neither the FNA-side app nor the CNA-side tool is registered as a `ctest` — the full comparison
needs `mono`/`xbuild` and a locally-built `FNA.dll`, which isn't guaranteed on every machine that
builds CNA. This is a manually-invoked developer verification workflow, not an automated
regression gate.

## What's covered today

| Category | FNA-side generator | CNA-side dump | Needs `GraphicsDevice`? |
|---|---|---|---|
| Enums (21 Graphics-namespace enums) | `NonRenderingApiReference.cs` (Task 472) | `DumpEnums()` | No |
| State presets (`BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`, 16 presets) | `NonRenderingApiReference.cs` (Task 472) | `DumpStatePresets()` | No |
| `PackedVector` (all 17 types) | `PackedVectorReference.cs` (Task 473) | `DumpPackedVector()` | No |
| `Viewport.Project`/`Unproject` | `ViewportReference.cs` (Task 476) | `DumpViewport()` | No |
| `BasicEffect` defaults/lighting | — | — | Yes — **DEFERRED**, see below |
| `SpriteFont.MeasureString` | — | — | Yes — **DEFERRED**, see below |
| Reference screenshots (SpriteBatch/BasicEffect) | — | — | Yes — **DEFERRED**, see below |

## Why some categories are still missing

`BasicEffect`'s only constructor is `BasicEffect(GraphicsDevice device)` (Task 474) —
`GraphicsDevice`'s own constructor calls the native `FNA3D_CreateDevice` P/Invoke, which needs a
real, pre-built `libFNA3D.so`. This sandbox doesn't have one, and building it turned out to be a
substantially larger undertaking than everything else in this phase: `FNA3D` has its own nested,
separately-uninitialized `MojoShader` git submodule (one layer deeper than the 6 top-level FNA
submodules already fixed for Task 471), plus unresolved SDL2/SDL3 linkage. `SpriteFont`'s
constructor is additionally `internal`, reachable only through real compiled `.xnb` content via
`ContentManager` — an even larger prerequisite. Reference screenshots need the same live
`GraphicsDevice` plus a real render+present+readback cycle. All deferred with the exact blockers
documented in `plans/plan_graphics.md` (Tasks 474/475/477/478), not attempted blind or silently skipped.

## Adding a new category

1. **FNA side**: add a new `<Category>Reference.cs` file to `tools/fna-reference/` (see
   `ViewportReference.cs` for the smallest example), add it to `FnaReference.csproj`'s
   `<Compile>` list and to `Program.cs`'s assembled root `JsonWriter`.
2. **CNA side**: add a matching `Dump<Category>()` function to
   `tools/cna-reference/CnaReferenceDump.cpp`, using the exact same key names and input values as
   the C# side (the comparison script matches by key path, not by position).
3. **Compare**: pass `--category <Category>` to `scripts/compare-fna-reference.py`, or add it to
   the script's own `DEFAULT_CATEGORIES` list if it should always be checked.

If the new category needs a `GraphicsDevice` on the FNA side, it hits the same native `FNA3D`
build blocker as Tasks 474/475/477/478 above — check `tools/fna-reference/README.md`'s own status
section before starting.

## What Task 479's first real run found

Running the comparison for the first time found and fixed 3 bugs in the new harness itself before
finding anything real: `JsonWriter.hpp`'s `Add(double)` used `ostringstream`'s default
6-significant-digit precision (silently truncating large packed-value integers and sub-millimeter
float differences); the first draft of the state-preset dump omitted `BlendState.BlendFactor` and
all 4 state classes' inherited `GraphicsResource.Name`/`IsDisposed` properties; and the first draft
of the `SurfaceFormat` enum dump omitted 7 real `*EXT`-suffixed members present in this sandbox's
FNA build (version 26.5.0.0) but not yet added to the C++ side.

After fixing those, the comparison found exactly one genuine divergence: `IndexElementSize`'s
numeric values did not match FNA at the time (`SixteenBits=0`/`ThirtyTwoBits=1` in FNA, `16`/`32` in
CNA — see `AUDIT.md` and `plans/plan_graphics.md` Task 921). **Fixed by Task 921 on 2026-07-09** — CNA now
uses `SixteenBits=0`/`ThirtyTwoBits=1` too; re-running this comparison today would find no
divergence here. Every other compared value — all 21 enums, all 16 state presets, all 17
`PackedVector` types, and all 5 `Viewport` cases — matches the real, running FNA implementation
exactly.
