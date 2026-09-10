# FNA reference-app generator

Task 471 (`plans/plan_graphics.md` Phase 53, "FNA comparison harness"). A small C# console app that
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
  git submodule update --init --recursive lib/FAudio lib/FNA3D lib/SDL2-CS lib/SDL3-CS lib/Theorafile lib/dav1dfile
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
`plans/plan_graphics.md` Tasks 474/475/477/478 for the full investigation.

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

## `--effects`: compiled Effect Framework reflection (plans/plan_fx.md FX-005)

`FnaReference.exe --effects <directory-of-fxb> [output.json]` emits FNA's own reflection of every
`.fxb` in a directory. This is the FX-005 oracle: every other reflection check in the compiled
effect suite compares CNA against the format or against CNA's own fixtures, which is
self-consistency; this one compares it against reflection produced by *running* FNA.

It does not need FNA's windowing or `Game` stack. FNA builds its public object graph in
`Effect.INTERNAL_parseEffectStruct`, which reads only the parsed `MOJOSHADER_effect` and touches
no `GraphicsDevice`, so the tool creates an FNA3D device itself through P/Invoke, asks FNA3D for
the parsed effect, and then lets FNA's own method build `Parameters`/`Techniques` on an `Effect`
that never ran its constructor.

The native layer must match the `FNA.dll` being loaded. In particular, the current reference FNA
checkout is FNA 26.05 and pins FNA3D 26.05, while CNA currently pins FNA3D 26.08. Mixing those two
versions crashed executable Effect operations instead of producing an oracle. The state and pixel
modes query `FNA3D_LinkedVersion` before creating a device and reject a mismatch explicitly. The
oracle is FNA's C# mapping and execution of an identical compiled effect, not a second parser.

### Regenerating

FNA3D has to exist as a shared library for mono to P/Invoke (CNA links it statically), so build the
revision pinned by the FNA source checkout, including that FNA3D revision's MojoShader submodule:

```bash
FNA_ROOT=/rv/data/library/github.com/FNA-XNA/FNA
FNA3D_SRC="$FNA_ROOT/lib/FNA3D"
SDLROOT=<cna>/.sdl-prebuilt-Linux-x86_64-wayland
git -C "$FNA_ROOT" submodule update --init --recursive lib/FNA3D
cmake -S "$FNA3D_SRC" -B /tmp/fna3d-fna-version -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON -DCMAKE_PREFIX_PATH="$SDLROOT/install"
cmake --build /tmp/fna3d-fna-version -j3

cd tools/fna-reference && xbuild FnaReference.csproj /p:Configuration=Debug && cd ../..
SDL_VIDEODRIVER=offscreen \
LD_LIBRARY_PATH=/tmp/fna3d-fna-version:$SDLROOT/install/lib \
MONO_PATH="$FNA_ROOT/bin/Debug" \
  mono tools/fna-reference/bin/Debug/FnaReference.exe --effects \
    modules/renderers/fna3d/effects \
    tests/fixtures/compiled-effects/fna-effect-reflection.json
```

`Fna3dCompiledEffectTest.StockFixtureReflectionMatchesTheFnaOracle` reads that checked-in JSON and
compares CNA's reflection of the same seven binaries against it, subtree by subtree.

## `--effect-states`: what FNA installs when a pass is applied (plans/plan_fx.md FX-005)

`FnaReference.exe --effect-states <directory-of-fxb> [output.json]` is the state half of the same
oracle. Where `--effects` compares the object graph CNA *reads*, this compares what CNA *does* with
it.

FNA's `Effect.INTERNAL_applyEffect` folds the state changes MojoShader reports through
`PipelineCache` and assigns the results to the public `GraphicsDevice.BlendState`,
`DepthStencilState`, `RasterizerState` and `SamplerStates` properties. CNA's
`Effect::ApplyCompiledPassState` does the same, so those properties compare directly.

This mode needs a real managed `GraphicsDevice`, because that is where `PipelineCache` and the
property assignments live. It builds one straight from an SDL window rather than through FNA's
`Game` stack, which keeps the tool free of windowing and content plumbing:

```bash
SDL_VIDEODRIVER=offscreen \
LD_LIBRARY_PATH=/tmp/fna3d-fna-version:$SDLROOT/install/lib \
MONO_PATH="$FNA_ROOT/bin/Debug" \
  mono tools/fna-reference/bin/Debug/FnaReference.exe --effect-states \
    modules/renderers/fna3d/effects \
    tests/fixtures/compiled-effects/fna-effect-states.json
```

Every pass is applied from the same starting device state (`BlendState.Opaque`,
`DepthStencilState.Default`, `RasterizerState.CullCounterClockwise`, `SamplerState.LinearWrap` on
the first four slots), so a pass that assigns nothing is recorded as leaving that selection alone.
"Unchanged" is as much a result as "replaced", and
`Fna3dEffectStateOracleTest.EveryPassInstallsTheStateFnaInstalls` checks both.

## `--effect-pixels`: what FNA renders for every compiled pass (plans/plan_fx.md FX-005)

`FnaReference.exe --effect-pixels <directory-of-fxb> [output.json]` renders every technique/pass
of all seven committed compiler-produced effects through FNA's public `Effect` and
`GraphicsDevice` APIs. Each pass starts from a clean state and an 8x8 render target, and uses flat
textures, identity transforms, and a full-screen triangle pair. The output records the number of
pixels changed from the clear colour and three interior RGBA samples. Flat inputs avoid depending
on interpolation or render-target orientation while still proving shader selection, parameter and
texture binding, pass application, drawing, and readback.

```bash
SDL_VIDEODRIVER=offscreen \
LD_LIBRARY_PATH=/tmp/fna3d-fna-version:$SDLROOT/install/lib \
MONO_PATH="$FNA_ROOT/bin/Debug" \
  mono tools/fna-reference/bin/Debug/FnaReference.exe --effect-pixels \
    modules/renderers/fna3d/effects \
    tests/fixtures/compiled-effects/fna-effect-pixels.json
```

`Fna3dEffectPixelOracleTest.EveryCompilerProducedPassMatchesFnaPixels` recreates the same inputs
through CNA's public API. It requires the same changed-pixel count and allows only three integer
levels of per-channel variance in the sampled pixels, covering normal cross-driver rounding without
turning the oracle into a broad visual approximation.
