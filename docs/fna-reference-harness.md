# FNA reference harness

Current source/build contract checked on 2026-11-01. Phase 53 (Tasks 471–480) introduced the
non-rendering value comparison; FX-005 later added compiled-effect reflection/state/pixel modes.
The old Phase 53 environment blockers are historical, not current dependency detection.

FNA remains the normal readable implementation reference. A running FNA comparison is useful
independent evidence, but measured Microsoft XNA 4.0 behavior wins where it disagrees; record those
decisions in [the behavior ledger](xna-fna-behavior-decisions.md).

## Components and scope

| Component | Role |
|---|---|
| tools/fna-reference | C# developer probe running the chosen FNA.dll |
| tools/cna-reference/CnaReferenceDump.cpp | Native cna_reference_dump producing the mirrored value categories |
| scripts/compare-fna-reference.py | Selected-category, FNA-to-CNA JSON comparison |
| tools/xna-oracle and tests/reference/xna40 | Separate genuine-XNA probes/evidence |

This developer probe is not a language binding. It does not expose CNA to managed applications.

The default three compared categories are:

| Category | Current value coverage |
|---|---|
| NonRenderingApis | 21 graphics enums and 16 state presets |
| PackedVector | 17 packed-vector types and selected input/output cases |
| Viewport | Five Project/Unproject cases, including two nonidentity-camera points |

The FNA generator also emits assembly version, MathHelper and Color smoke values; the CNA dump
does not mirror those root sections and the comparator defaults do not select them. This is not
an exhaustive test of every method, input, exception, preset mutation or GPU behavior.

## Building and generating values

Use an existing FNA checkout outside CNA, or clone one there once. No project-wide absolute
location is required. Record its revision, assembly version, managed-source dependencies and build
configuration with the output. Initialize the submodules referenced by that checkout's .csproj;
the current managed source list includes SDL2-CS, SDL3-CS, FAudio, Theorafile and dav1dfile.

```sh
FNA_REFERENCE_ROOT=/path/to/FNA
xbuild "$FNA_REFERENCE_ROOT/FNA.csproj" /p:Configuration=Debug
```

The harness is an old-style ToolsVersion=4.0/x86 C# project. Mono/xbuild is the documented route;
this does not imply dotnet/NuGet are absent from the machine or that the probe is an SDK project.

Before building, set the FNA Reference's HintPath in tools/fna-reference/FnaReference.csproj to
the resulting FNA.dll. The project still hardcodes the old
/rv/data/library/github.com/FNA-XNA/FNA/bin/Debug/FNA.dll location: that is a current harness
configuration limitation, not the checkout policy. Private=true copies the chosen assembly next
to the probe.

```sh
xbuild tools/fna-reference/FnaReference.csproj /p:Configuration=Debug
mono tools/fna-reference/bin/Debug/FnaReference.exe /tmp/fna-reference-values.json
cmake --build cmake-build-multi --target cna_reference_dump --parallel 8
tools/platform/run_gpu_tests_private.sh --exec \
  cmake-build-multi/cna_reference_dump /tmp/cna-reference-values.json
python3 scripts/compare-fna-reference.py \
  /tmp/fna-reference-values.json /tmp/cna-reference-values.json
```

The native target requires CNA_BUILD_EXAMPLES and is excluded for Android/Emscripten. Its ordinary
value mode needs no GraphicsDevice; the private runner is the safe default for developer execution.
Neither generator nor the full external comparison is registered as CTest. A normal CNA build
alone does not install/build the managed/native FNA reference dependencies.

## Comparator meaning and limits

Every selected FNA dictionary leaf must exist and match on the CNA side. Extra CNA keys are
ignored to allow CNAEXT entries. --category is repeatable and --tolerance defaults to an absolute
1e-4; numeric comparison also converts integers to Python float, while strings compare exactly.
The packed 64-bit cases use hexadecimal strings.

The script is not a general schema/finite-number validator or an API-completeness audit. A requested
category absent from the FNA JSON emits a warning and is skipped; the command fails for no
compared categories, but can pass if some other categories were compared. Inspect the reported
category list. Large integer/nonfinite numeric inputs outside the trusted generators' expected
shape require stronger checks than this comparator supplies.

A passing value set proves agreement for its chosen assembly, cases and tolerance. It does not
override a measured XNA disagreement or imply that unselected categories were checked.

## Compiled-effect modes

Program.cs also accepts these modes, each with a directory of .fxb inputs and optional output:

```text
--effects        reflection
--effect-states  public device state after exact pass application
--effect-pixels  bounded render-target samples for each pass
```

They use native FNA3D/SDL and need a compatible library built for the selected FNA checkout,
not whichever FNA3D binary CNA currently uses. Reflection constructs the FNA object graph through
its internal parser/reflection route while creating a native device; it is not a pure managed/
no-device mode. State and pixel modes check FNA3D_LinkedVersion against the assembly-derived
expected version before executable operations. Matching version alone is not provenance proof.

Run all native/window modes through the private runner with explicit library search paths.
The tool README contains mode details, but its original Phase 53 status paragraphs are historical.
Do not overwrite committed reference JSON incidentally during a documentation audit.
See [compiled effects](fx-compiled-effects.md) for the runtime boundary and genuine-XNA exceptions.

## Historical findings and current verification

The first campaign found harness precision/omission bugs and an IndexElementSize mismatch.
Task 921 corrected CNA to SixteenBits=0/ThirtyTwoBits=1. This history is not a substitute for a
fresh full comparison.

On 2026-11-01 cna_reference_dump builds and emits valid JSON containing the 21/16/17/5 coverage
above and those corrected enum values. Mono, xbuild and dotnet are installed; a local FNA source
checkout exists elsewhere, but the hardcoded DLL path, a built reference DLL and its managed
submodule sources are absent. No fresh running-FNA comparison, native effect probe, screenshot
qualification or C++/C ABI change is claimed by this audit.
