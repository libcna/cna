# CNA reference-value dump tool

Task 479 (`plans/plan_graphics.md` Phase 53, "FNA comparison harness"). A standalone C++ console tool
that mirrors `tools/fna-reference/`'s own reference-value categories (enums, state presets,
PackedVector, Viewport) using CNA's real C++ implementation, and emits the same JSON shape so
`scripts/compare-fna-reference.py` can diff the two outputs key-for-key. None of these 4
categories need a `GraphicsDevice`, so this tool runs standalone with no window/renderer
initialization.

Registered as the `cna_reference_dump` CMake target (built whenever `CNA_BUILD_EXAMPLES` is on,
any graphics renderer). Not registered as a `ctest`: the full comparison additionally needs
`mono`/`xbuild` and a locally-built `FNA.dll` (`tools/fna-reference/README.md`), which isn't
guaranteed on every machine that builds CNA — this is a manually-invoked developer verification
tool, not an automated regression gate.

## Build and run

```bash
cmake --build cmake-build-debug --target cna_reference_dump
./cmake-build-debug/cna_reference_dump cna-reference-values.json
```

Defaults to writing `cna-reference-values.json` in the current directory if no output path is
given.

## Comparing against FNA

```bash
# 1. Generate the FNA-side reference values (see tools/fna-reference/README.md).
cd tools/fna-reference
xbuild FnaReference.csproj /p:Configuration=Debug
mono bin/Debug/FnaReference.exe /tmp/fna-reference-values.json
cd ../..

# 2. Generate the CNA-side reference values.
./cmake-build-debug/cna_reference_dump /tmp/cna-reference-values.json

# 3. Diff them.
python3 scripts/compare-fna-reference.py /tmp/fna-reference-values.json /tmp/cna-reference-values.json
```

The comparison is one-directional: every key present on the FNA side must exist on the CNA side
with an equal (or, for floats, sufficiently close — `--tolerance`, default `1e-4`) value. Keys
that exist only on the CNA side are not reported as mismatches — CNA has real CNAEXT extensions
with no FNA equivalent (e.g. `PrimitiveType.PointListEXT`), and requiring the reverse direction
too would make legitimate, intentional CNA extensions look like failures.

## Status

Running the real comparison found exactly one genuine divergence after fixing several bugs in
this new harness itself (`ostringstream`'s default 6-significant-digit precision silently
truncating large packed-value integers and sub-millimeter float differences; several state-preset
properties and 7 `SurfaceFormat` `*EXT` enum members missing from the first draft of the C++
dump): `IndexElementSize`'s numeric values did not match real FNA at the time (`SixteenBits=0`/
`ThirtyTwoBits=1` in FNA vs. `16`/`32` in CNA) — tracked as `plans/plan_graphics.md` Task 921, not fixed
in this task since it was a public-API enum-value change with its own existing (then-wrong) test
coverage to update, out of this task's own "build the comparison tooling" scope. **Task 921 has
since fixed this (2026-07-09)** — CNA's `IndexElementSize` now uses `SixteenBits=0`/
`ThirtyTwoBits=1` too; re-running this comparison today would find no divergence here.

Every other compared value — all 21 enums, all 16 state presets, all 17 `PackedVector` types, and
all 5 `Viewport.Project`/`Unproject` cases — matches the real, running FNA implementation exactly.
