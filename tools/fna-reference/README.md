# FNA reference-app generator

Task 471 (`plan_graphics.md` Phase 53, "FNA comparison harness"). A small C# console app that
references the real FNA.dll and emits selected reference values as JSON, so CNA's own C++ tests
can eventually diff against ground truth produced by *running* FNA itself, not just reading its
source. Not part of the CNA C++ build; never run by CNA at runtime.

## Prerequisites

- `mono`/`xbuild` (this project targets the same legacy `ToolsVersion=4.0` .NET Framework 4.0
  project style FNA's own `FNA.csproj` uses — no `dotnet` CLI or NuGet required).
- A built `FNA.dll` at `/rv/data/library/github.com/FNA-XNA/FNA/bin/Debug/FNA.dll` (this
  project's own documented "Source Reference" checkout, see `CLAUDE.md`). If missing:

  ```bash
  cd /rv/data/library/github.com/FNA-XNA/FNA
  git submodule update --init lib/FAudio lib/FNA3D lib/SDL2-CS lib/SDL3-CS lib/Theorafile lib/dav1dfile
  xbuild FNA.csproj /p:Configuration=Debug
  ```

## Build and run

```bash
cd tools/fna-reference
xbuild FnaReference.csproj /p:Configuration=Debug
mono bin/Debug/FnaReference.exe [output.json]
```

Defaults to writing `reference-values.json` next to the built executable if no output path is
given.

## Status

Task 471 (this scaffold) is done: proves the whole harness end to end — real `FNA.dll` resolves
and loads under mono (`<Private>True</Private>` copies it next to the built exe; the default
`Private=False` reference-only mode fails at runtime with a `FileNotFoundException`, since mono's
assembly resolver doesn't consult the original `HintPath` location), a real non-`GraphicsDevice`-
dependent FNA API call executes correctly (`MathHelper.Pi`/`PiOver2`/`PiOver4`/`TwoPi`,
`Color.CornflowerBlue`'s real packed RGBA value), and the result is written as JSON via
`JsonWriter.cs` (a tiny, dependency-free hand-rolled writer — no NuGet-fetched JSON library is
viable in this sandbox).

Task 472 (`NonRenderingApiReference.cs`) is done: reflection-based dump of 20 Graphics-namespace
enum types and all 16 built-in `BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`
presets — no `GraphicsDevice` needed. Surfaced one genuine, previously-unremarked finding purely
from the generic reflection approach: `BlendState` has 4 separate `ColorWriteChannels`/`1`/`2`/`3`
properties (one per MRT render-target slot).

Task 473 (`PackedVectorReference.cs`) is done: all 17 `PackedVector` types, using the exact same
input values as Task 197's own hand-derived `tests/PackedVectorGolden.md` (Python re-implementing
FNA's bit-packing formulas from reading the source, not from running FNA). **Every single value
across all 17 types matches Task 197's golden table exactly** — a genuine, comprehensive
cross-validation confirming Task 197's hand-derived formulas were correct, not just an assumption.

Tasks 474/475/477/478 are DEFERRED: they all fundamentally need a real, live `GraphicsDevice`
(`BasicEffect(GraphicsDevice device)`'s only constructor, screenshot generation via a real
render+present+readback cycle), which needs a native `FNA3D` shared library not built in this
sandbox and with its own multi-layer dependency chain (a separately-uninitialized nested
`MojoShader` submodule inside `lib/FNA3D`, plus unresolved SDL2/SDL3 linkage) — a substantially
larger undertaking than every other task in this phase, deferred rather than attempted blind. See
`plan_graphics.md` Tasks 474/475/477/478 for the full investigation.

Task 476 (`ViewportReference.cs`) is done: `Viewport.Project`/`Unproject`, genuinely tractable
without a `GraphicsDevice` (a plain value struct, pure `Matrix`/`Vector3` math). Covers 3
identity-matrix cases (hand-derived and cross-checked before trusting the real output) plus a real
non-identity camera case; every case round-trips through `Unproject(Project(source))` as a
self-consistency check.

Task 479 (`tools/cna-reference/` + `scripts/compare-fna-reference.py`) is done: the CNA-side C++
mirror of Tasks 472/473/476's own categories (enums, state presets, PackedVector, Viewport) plus a
Python script that diffs the two JSON outputs key-for-key. Running it for real found exactly one
genuine divergence — `IndexElementSize`'s numeric values: FNA uses `SixteenBits=0`/
`ThirtyTwoBits=1`, CNA at the time used `16`/`32` — after several tooling bugs in the new comparison
harness itself were found and fixed first (`ostringstream`'s default 6-significant-digit precision
silently truncating large packed-value integers and sub-millimeter float differences; a few
state-preset properties and 7 `SurfaceFormat` `*EXT` enum members omitted from the first draft of
the C++ dump). **This divergence was tracked as Task 921 and fixed 2026-07-09** — CNA's
`IndexElementSize` now uses `SixteenBits=0`/`ThirtyTwoBits=1` too, matching FNA exactly; re-running
this comparison today would no longer show that mismatch. See `tools/cna-reference/README.md` for
how to run the comparison.

Task 480 (the rest of this phase) documents how to regenerate this reference data — not yet
started.

## `--effects`: compiled Effect Framework reflection (plan_fx.md FX-005)

`FnaReference.exe --effects <directory-of-fxb> [output.json]` emits FNA's own reflection of every
`.fxb` in a directory. This is the FX-005 oracle: every other reflection check in the compiled
effect suite compares CNA against the format or against CNA's own fixtures, which is
self-consistency; this one compares it against reflection produced by *running* FNA.

It does not need FNA's windowing or `Game` stack. FNA builds its public object graph in
`Effect.INTERNAL_parseEffectStruct`, which reads only the parsed `MOJOSHADER_effect` and touches
no `GraphicsDevice`, so the tool creates an FNA3D device itself through P/Invoke, asks FNA3D for
the parsed effect, and then lets FNA's own method build `Parameters`/`Techniques` on an `Effect`
that never ran its constructor.

The native layer is deliberately the same one CNA links -- the FNA3D revision CNA pins, including
CNA's managed MojoShader robustness patch. That is the point: the oracle is FNA's C# reflection
mapping, not a second parser. A difference in this output is a difference in how CNA and FNA
interpret an identical parse tree.

### Regenerating

FNA3D has to exist as a shared library for mono to P/Invoke (CNA links it statically), so build one
from the revision CNA pins:

```bash
CNA_FNA3D_SRC=<cna-build-dir>/_deps/fna3d-src        # carries CNA's applied MojoShader patch
SDLROOT=<cna>/.sdl-prebuilt-Linux-x86_64
cmake -S "$CNA_FNA3D_SRC" -B /tmp/fna3d-shared -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON -DCMAKE_PREFIX_PATH="$SDLROOT/install"
cmake --build /tmp/fna3d-shared -j3

cd tools/fna-reference && xbuild FnaReference.csproj /p:Configuration=Debug && cd ../..
SDL_VIDEODRIVER=offscreen \
LD_LIBRARY_PATH=/tmp/fna3d-shared:$SDLROOT/SDL/build \
MONO_PATH=/rv/data/library/github.com/FNA-XNA/FNA/bin/Debug \
  mono tools/fna-reference/bin/Debug/FnaReference.exe --effects \
    modules/renderers/fna3d/effects \
    tests/fixtures/compiled-effects/fna-effect-reflection.json
```

`Fna3dCompiledEffectTest.StockFixtureReflectionMatchesTheFnaOracle` reads that checked-in JSON and
compares CNA's reflection of the same six binaries against it, subtree by subtree.
