# FNA reference harness

Current source/build contract updated for MTH-CONF-002 on 2026-11-02. Phase 53 (Tasks 471–480) introduced the
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

The harness targets .NET 8 and consumes FNA.Core.csproj's FNA.dll. Use an existing external
checkout with its pinned managed submodules available: SDL2-CS, SDL3-CS, FAudio, Theorafile and
dav1dfile. The generation recipe never clones or edits dependencies. Choose a reviewed full
commit ID and keep the tracked checkout clean.

```sh
export FNA_SOURCE_DIR=/path/to/FNA
export FNA_EXPECTED_REVISION=<reviewed-40-character-commit-id>
python3 tools/fna-reference/run-reference.py --output-dir /path/to/fresh/evidence
```

The script rebuilds FNA, discovers its TargetPath through MSBuild, hashes the built assembly,
builds the probe, generates values-1.json and values-2.json and compares each of the three
non-rendering categories at zero tolerance. FNA and harness outputs stay in their own normal
build directories. Existing dump files or repository output directories require an explicit
`--update`; ordinary runs never overwrite committed goldens. The direct probe executable
requires an explicit output path but does not provide this wrapper's overwrite protections.

Manual builds use `-p:FnaAssemblyPath=/path/to/FNA.dll` or the FNA_ASSEMBLY_PATH environment
variable. Missing/unset assemblies are explicit build errors. Runtime requires FNA_SOURCE_DIR,
FNA_EXPECTED_REVISION, FNA_ASSEMBLY_PATH and FNA_EXPECTED_SHA256. It checks the actual source
revision with Git and hashes the actual loaded assembly, rejecting dirty tracked sources,
wrong pins and stale copied DLLs. See [the tool README](../tools/fna-reference/README.md) for
manual recipes and native-library settings.

Every dump includes metadata.reference schemaVersion 1: sourceRevision, assemblySha256,
assemblyVersion and nativeLibraries entries for FNA3D and SDL3. Each native entry is either
explicitly not_loaded or has a loaded SHA-256 and an exported integer version. Diagnostic
paths are separate from the comparable identity. No timestamp enters the identity.

The native cna_reference_dump target remains separate and requires CNA_BUILD_EXAMPLES;
it is excluded for Android/Emscripten. Its current output has no metadata and is accepted as a
candidate without claiming to have loaded FNA. Optional metadata.producer can identify the CNA
tool/build independently; the native tool currently lacks a build-time Git revision and was left
unchanged. The recipe's optional --compare-to accepts an ordinary CNA candidate dump.
Neither generator nor the external comparison is registered as CTest. A normal CNA build
does not install/build external FNA dependencies.

## Comparator meaning and limits

The first input must contain valid metadata.reference: schema version, full FNA source revision,
assembly SHA-256/version, and native-library identities/statuses. The second input is a candidate
and may omit reference metadata entirely, as CNA does not load FNA.dll. If it supplies
metadata.reference, that identity must be valid and **identical** to the first input. A present
malformed/null identity is an error. This preserves strict FNA-vs-FNA determinism checking while
allowing FNA-vs-CNA comparisons. Identity equality is independent of numeric tolerance; diagnostic
paths may differ. A filename alone is never native compatibility evidence.

Candidate metadata must be an object when supplied. Optional metadata.producer must contain
exactly `tool` (nonempty string) and `cnaRevision` (40 lowercase hexadecimal digits). This
independent CNA identity is validated and printed in the PASS/FAIL comparison summary. It is not
compared to the FNA identity and cannot substitute for the first input's required reference.
No metadata is added to or required from legacy CNA output to make it a valid candidate.

Every selected FNA dictionary leaf must exist and match on the CNA side. Extra CNA keys are
ignored for comparison to allow CNAEXT entries, including inside array elements. Arrays must
have equal lengths and compare in order. Diagnostic paths use JSON Pointer escaping (`~0` for
`~`, `~1` for `/`), so keys containing slashes remain literal keys.

`--category` is repeatable and replaces the default selection when supplied. Every selected
category is required on **both** sides; the defaults require all three categories listed above.
Each category must be an object or array containing at least one scalar leaf. Empty categories,
including trees containing only empty containers, fail even when another category matches.
Nested empty objects/arrays are valid in populated categories (for example, effect annotations).
The FNA document supplies the expected nested keys and types; this is structural validation,
not a fixed inventory of required XNA members or an API-completeness audit.

The numeric/type policy, hardened by MTH-CONF-001, is:

| Values | Comparison rule |
|---|---|
| Two JSON integer tokens | Exact arbitrary-precision integer equality; never converted to float and never relaxed by tolerance |
| Finite floating-point fields | Each scalar field independently requires `abs(FNA - CNA) <= tolerance`; no relative tolerance or aggregate error budget |
| Mixed integer/float tokens | Same absolute tolerance, with the integer preserved exactly; both writers can serialize integral floats without a decimal point |
| Signed zero | `+0`, `-0`, `+0.0` and `-0.0` compare equal, including at zero tolerance |
| Non-finite numbers | Unsupported input: reject `NaN`, `Infinity`, `-Infinity` and floating-point overflow such as `1e999`, even against themselves |
| Booleans, strings, null | Exact value and type; `true` is not `1`, and `null` is not an empty string |
| Objects/arrays | Container type must match, including empty nested containers |

`--tolerance` defaults to the unchanged absolute `1e-4` and must be finite and nonnegative.
Floating-point JSON tokens and the tolerance are parsed as Python binary64 floats; differences
are then evaluated exactly for those parsed values, without rounding an integer through float.
The packed 64-bit cases remain exact hexadecimal strings. No field currently has an approved
NaN-equivalence rule: even NaN versus NaN is rejected as unsupported input, not accepted as a
numeric match. A future exception needs a documented field-specific rule and regression tests;
there is no global NaN opt-in. Literal strings such as `"NaN"` remain ordinary strings.

Both documents must be valid UTF-8 JSON objects. Invalid syntax, duplicate object keys,
non-finite numbers (also in unselected metadata or extra CNA entries), unreadable files and
invalid category shapes fail closed with an input diagnostic. Exit status is `0` for a complete
selected-category match, `1` for invalid input or a mismatch, and `2` for invalid command-line
arguments, including invalid tolerances.

The CPU-only regression suite uses Python's standard library and needs no FNA installation:

```sh
python3 -B scripts/test_compare_fna_reference.py -v
```

It checks that the three legacy compiled-effect dumps without reference provenance are rejected
in the first input position, and
separately tests their payload shape using explicitly synthetic unit-test metadata. That test
does not establish provenance or parity and does not modify the fixtures. The recipe's output
protection suite is `python3 -B tools/fna-reference/test_run_reference.py -v`.

A passing value set proves agreement for its chosen assembly, cases and tolerance. It does not
override a measured XNA disagreement or imply that unselected categories were checked.

## Compiled-effect modes

Program.cs also accepts these modes, each with a directory of .fxb inputs and required output:

```text
--effects        reflection
--effect-states  public device state after exact pass application
--effect-pixels  bounded render-target samples for each pass
```

They use native FNA3D/SDL and need a compatible library built for the selected FNA checkout,
not whichever FNA3D binary CNA currently uses. Reflection constructs the FNA object graph through
its internal parser/reflection route while creating a native device; it is not a pure managed/
no-device mode. All three modes check FNA3D_LinkedVersion against the assembly-derived expected version before
device operations. Native paths and SHA-256 pins are mandatory; the resolver and Linux dladdr
identify the actual loaded exports, and mapped SDL/FNA3D files are checked for unexpected
transitive identities. SDL_GetVersion must identify SDL3. Matching version alone is not proof
of compatibility. Native provenance is currently Linux-only; other effect-mode hosts are
explicitly refused pending a platform-specific identity implementation.

Run all native/window modes through the private runner with explicit library search paths.
The tool README contains the explicit paths/hash settings and owner-run recipes.
Do not overwrite committed reference JSON incidentally during a documentation audit.
See [compiled effects](fx-compiled-effects.md) for the runtime boundary and genuine-XNA exceptions.

## Historical findings and current verification

The first campaign found harness precision/omission bugs and an IndexElementSize mismatch.
Task 921 corrected CNA to SixteenBits=0/ThirtyTwoBits=1. This history is not a substitute for a
fresh full comparison.

On 2026-11-02, the owner provisioned the five pinned managed submodules after the initial
missing-source build failure. FNA.Core.csproj at revision
00498b0b81425b14bf8da23e68655215b9b5daa7 and the complete harness then built with SDK 8.0.425.
The assembly reports 26.10.0.0; its built/loaded SHA-256 is
`f973ec1edd32290f19750d6d3d26fba23ab1a813fedd71f1ea026edf03c9647c`.
The real recipe produced two byte-identical dumps and passed all three category comparisons at
zero tolerance. Neither FNA3D nor SDL3 was loaded, and FNA3D's uninitialized source submodule
was not needed. Five runtime negative checks rejected missing or wrong references. Checks with
real dumps reject missing first-input provenance and changed candidate reference identities,
while accepting a candidate without metadata. All 49 comparator and 5 recipe tests passed,
including candidate/producer validation. Builds had no errors, with upstream CA2255 and existing
probe SYSLIB0050 warnings.

This is measured non-rendering reference repeatability on the Linux host. No fresh CNA/FNA
parity, native effect run, screenshot qualification or C++/C ABI change is claimed. Native
validation and golden regeneration remain separate integration-owner work.
