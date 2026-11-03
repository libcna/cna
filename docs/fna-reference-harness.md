# FNA reference harness

Bounded evidence map reconciled for MTH-CONF-007 on 2026-11-03. Phase 53 (Tasks 471–480) introduced the
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

## Campaign evidence map (MTH-CONF-001–007)

**Declared** API is inventoried in [runtime member coverage](xna-4-runtime-member-coverage.md);
it is not an execution count. **Tested behaviour** below means only the listed input domains.
**Platform-qualified** for the value rows is Linux x86_64 CPU execution in Release/SOFTWARE/shared
with DISPLAY/WAYLAND_DISPLAY unset, offscreen video and dummy audio; those runs do not qualify
GPU output. Later renderer/C API/browser captures have their own scope linked below. Other
operating systems and unselected inputs are **unassessed** by these runs.
[Evidence descriptions, reproduction commands and source-identity limits](execution-evidence.md#compatibility-campaign-captures-mth-conf-007)
identify the runs; commits below identify integrated changes, not a claim that pre-commit binaries
were built at those exact Git HEADs. All CONF-003 rows use the pinned FNA identity stated below,
seed `0xC0FFEE03`, tolerance zero and explicit signed-zero/nonfinite strings.

| Family / integrated commit | Oracle and executable witness | Result / bounded detail |
|---|---|---|
| Comparator, CONF-001 `a31835b10` | Independent malformed/numeric CLI fixtures; `FnaReferenceComparisonToolTests` (`scripts/test_compare_fna_reference.py`) | 36 tests pass; five former false passes now refuse. Tool correctness, not runtime parity. |
| Reference repeatability, CONF-002 `aa0a7364e` | Actual FNA.dll, `run-reference.py`; `test_run_reference.py` | Two byte-identical dumps; three original categories repeat at zero tolerance; comparator 49 and recipe 5 pass. Native FNA3D/SDL3 not loaded. |
| Vector2 / perspective, CONF-003 `a8132d099` | FNA `ValueEdgeReference.cs`; literal `Vector2EdgeReferenceTest.*`, `MatrixPerspectiveEdgeReferenceTest.*`, `MatrixPerspectiveCompatibilityTest.*` | [Domains and retained decisions](#bounded-value-and-exception-edges-mth-conf-003); scalar fields superseded below. |
| Vector3/4 / Quaternion, `72305ac4d` | FNA spatial/quaternion probes; `SpatialVectorTransformEdgeTest.*`, `QuaternionEdgeReferenceTest.*` | [Arithmetic, magnitudes, transforms and factory-rounding repair](#follow-up-1-vector3-vector4-and-quaternion). |
| Matrix, `e70314d6b` | FNA `MatrixEdgeReference.cs`; literal `MatrixEdgeReferenceTest.*` | [87 cases, factories, aliasing, inversion/decomposition and trig repair](#follow-up-2-matrix-beyond-perspective). |
| Plane / Bounding* / Ray, `1841a8150` | FNA `GeometryEdgeReference.cs`; literal `GeometryEdgeReferenceTest.*` | [Coplanar-plane repair and 114 retained fields](#follow-up-3-plane-boundingbox-boundingsphere-boundingfrustum-and-ray). Three negative-radius fields retain an algorithm decision, without a separate XNA measurement. |
| Color / 17 PackedVector types, `ee5c7b4b4` | FNA `PackingEdgeReference.cs`; `ColorEdgeReferenceTest.*`, `PackedVectorEdgeReferenceTest.*`; shipped-XNA HalfUtils IL plus literal `XnaHalfConversionTest.*` | [366 cases and half repair](#follow-up-4-color-and-all-17-packedvector-types). No successful original-XNA runtime run. |
| Curve, `ee5c7b4b4` | FNA `CurveEdgeReference.cs`; literal `CurveEdgeReferenceTest.*`; shipped-XNA Add/ComputeTangent IL | [12 curves, loop/tangent overloads, insertion ordering](#follow-up-5-curve-evaluation-and-tangents). IL settles 20 fields; no general tangent equivalence. |
| Scalar division follow-up, `661925667` (MTH-TEST-003) | Shipped-XNA IL; typed `VectorScalarDivisionContractTest/*.*`, `Vector3CompoundAssignmentTest.*`; independent rational binary32 proof | 57 more retained fields; final audit **1,859**, zero undecided, equal remainder. Reuses the aligned FNA dump; no FNA rerun. [Decision](xna-fna-behavior-decisions.md#vector-scalar-division). |
| Native lifecycle, CONF-004 `3671c7ee0` | Shipped-XNA IL / inspected FNA paths plus event/cache transcripts | [Owning decision and witness map](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection); separate HEADLESS qualification. |
| Graphics, CONF-005 / TEST-007 / EXIT-002, `cba2e9e36`, `9036515ca`, `177b55ecc`, `74a61a0b2` | FNA source semantics, hand-derived pixels and independent literals; observational line/point witness kept separate | [Claim-to-CTest/run/limit map](execution-evidence.md#renderer-c-api-and-browser-follow-up-2026-11-03): deferred uploads, sprite destination/depth transforms, readback/blend/MSAA, FNA3D viewport/storage, runtime adapter queries. Six Linux pixel renderers; capability subskips retained. |
| C API, SAFE-004 / TEST-005 / EXIT-002, `ade6f921e`, `f942e63ba`, `96dadb0c0` | C ownership/result contracts, independent byte/value expectations; XNA IL/FNA agree on component removal | Same execution map: lifetime sequences, subscription teardown, texture refusals/output atomicity and literal value checks. C handles are a separate contract from native disposal; no new XNA/FNA decision. |
| Browser / performance, `982d75273`, `b0d1b2c5a` | Browser pixel/lifecycle checks; EasyGL A/B checksum and call counters (neither is an XNA oracle) | Same execution map: Chrome WEBGL2/WEBGPU campaign pages and Linux OPENGLES3/OPENGL33 Debug benchmark. Non-CTest witnesses, platform and timing limits are explicit. |

The raw comparator still exits **1** on the 1,859 retained differences; only the exact
disposition audit exits **0**. This is not FNA parity. Earlier batch counts below are historical.
The final scalar follow-up ran 1,084 math tests (two missing ShipGame XML fixture skips),
172 CPU packing tests and 32 real-dump evidence tests; no selected new scalar witness skipped.

| Unassessed family | Precise next experiment / limitation |
|---|---|
| CONF-002 native effects | Pin compatible FNA3D/SDL3 hashes and exported versions; privately run `run-reference.py` separately with `--mode effects`, `effect-states`, `effect-pixels`, twice per mode. Old goldens lack current provenance; pure-value repeatability does not qualify them. |
| CONF-003 remaining value domains | Add paired fixed witnesses for near-endpoint Hermite (settle FNA epsilon versus XNA IL first), nonfinite/zero-span outside-loop Curve queries, NaN payloads and unselected alias/array paths. Existing finite domains and IL decisions do not establish these results. |
| CONF-005 remaining domains | Selected renderer witnesses now ran (map above). Measure L35/L36 line/point centers on native XNA/D3D9 hardware; current readback observations and Wine/DXVK cannot settle them. Add a physical presented-image observation for FNA3D; SOFTWARE has no physical layout. Measure projective SpriteBatch clip W on WEBGPU/SDLGPU and retain sample-mask restrictions (SDLGPU all/none, FNA3D SDL GPU unsupported). Broader sweep failures remain in the execution map and handoff §5.3. |
| CONF-006 **not started** | Reconcile L49; choose recently changed reader/writer/processor representatives, rebuild through `cna-content build` using the [sample sweep](../tools/xna-sample-sweep/README.md), then run taxonomy, accepted-difference and tolerance/semantic mutation audits. Preserve source/reference bytes, timestamps and exact rebuilt corpus/revision. Metadata refresh is not a new production sweep; unavailable assets remain unassessed. |

## Building and generating values

The harness targets .NET 8 and consumes FNA.Core.csproj's FNA.dll. Use an existing external
checkout with its pinned managed submodules available: SDL2-CS, SDL3-CS, FAudio, Theorafile and
dav1dfile. The generation recipe never clones or edits dependencies. Choose a reviewed full
commit ID and keep the tracked checkout clean.

```sh
# Set FNA_SOURCE_DIR to the clean external checkout, and EVIDENCE_DIR to a fresh external directory.
export FNA_EXPECTED_REVISION=00498b0b81425b14bf8da23e68655215b9b5daa7
python3 tools/fna-reference/run-reference.py --output-dir "$EVIDENCE_DIR/fna-values"
```

The script rebuilds FNA, discovers its TargetPath through MSBuild, hashes the built assembly,
builds the probe, generates values-1.json and values-2.json and compares all twelve mirrored
non-rendering categories (including the nine edge categories below) at zero tolerance. FNA and harness outputs stay in their own normal
build directories. Existing dump files or repository output directories require an explicit
`--update`; ordinary runs never overwrite committed goldens. The direct probe executable
requires an explicit output path but does not provide this wrapper's overwrite protections.

Manual builds use `-p:FnaAssemblyPath="$FNA_ASSEMBLY_PATH"` or the FNA_ASSEMBLY_PATH environment
variable. Missing/unset assemblies are explicit build errors. Runtime requires FNA_SOURCE_DIR,
FNA_EXPECTED_REVISION, FNA_ASSEMBLY_PATH and FNA_EXPECTED_SHA256. It checks the actual source
revision with Git and hashes the actual loaded assembly, rejecting dirty tracked sources,
wrong pins and stale copied DLLs. See [the tool README](../tools/fna-reference/README.md) for
manual recipes and native-library settings.

Provenance enforcement landed in `aa0a7364e`. If generation reports `Output exists`,
choose a fresh evidence directory; use `--update` only for an intentional replacement. A dirty
source/revision or assembly-hash refusal requires the reviewed clean FNA pin and a fresh wrapper
build, rather than copying a DLL into the probe output. .NET 8 SDK, Git, Python 3 and the managed
submodules above are prerequisites. Native effects additionally need pinned FNA3D/SDL3 libraries;
`not_loaded` on a value dump is expected and cannot qualify those modes.

Every dump includes metadata.reference schemaVersion 1: sourceRevision, assemblySha256,
assemblyVersion and nativeLibraries entries for FNA3D and SDL3. Each native entry is either
explicitly not_loaded or has a loaded SHA-256 and an exported integer version. Diagnostic
paths are separate from the comparable identity. No timestamp enters the identity.

The native cna_reference_dump target remains separate and requires CNA_BUILD_EXAMPLES;
it is excluded for Android/Emscripten. Its current output has no metadata and is accepted as a
candidate without claiming to have loaded FNA. Optional metadata.producer can identify the CNA
tool/build independently; the native tool currently lacks a build-time Git revision and was left
without build-revision metadata. The recipe's optional --compare-to accepts an ordinary CNA candidate dump
and compares all twelve categories; retained XNA differences therefore make that raw comparison fail.
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

## Bounded value and exception edges (MTH-CONF-003)

`ValueEdgeReference.cs` and the native dump add **Vector2Edges** and
**MatrixPerspectiveEdges**. They call each implementation directly with identical inputs;
the strict comparator is unchanged. These categories are explicit opt-ins to its CLI,
and mandatory in the recipe's twelve-category FNA repeatability check alongside the follow-up categories below.

| Family | Reviewed input domain and operations |
|---|---|
| Vector2 arithmetic | Eight fixed inputs: zero, signed zero, clamp endpoints ±2, just outside ±2.125, infinities, NaN, maximum finite Single, minimum subnormal. Divisors include signed zero, ±1, ±2 and 0.5. Component/scalar Divide, both operators and both out-ref forms; Clamp and reversed Clamp, value/out forms; copied-value equality/inequality and hash consistency. Eight seeded cases exercise all these operations. |
| Vector2 magnitude/value contracts | Zero, (3,4), (1e20,0), (1e-30,0), (infinity,0), (NaN,0): Length, LengthSquared and instance/value/out Normalize. Equal/unequal comparisons, signed-zero hash equality, and invariant `ToString` for (1.5,-2.25). Hash integers and general runtime-sensitive decimal formats are not compared. |
| Matrix perspective factories | Size, field-of-view and symmetric off-center forms, value and out overloads, all 16 output fields. Ordinary, zero/negative dimensions, zero near/far, equal/reversed depths, both zero, NaN near/far/dimension, infinite far, and four seeded positive depth pairs per factory. FOV additionally checks public Pi, its preceding Single, FNA's literal 3.141593f boundary, and infinite angle. Total: 52 factory cases. On refusal the out matrix starts as translation (7,8,9), so output preservation is observable. Exact exception type is emitted; messages/localization are not compared. |

Each family resets the deterministic unsigned 32-bit LCG to **0xC0FFEE03 (3237998083)**:
`state = state * 1664525 + 1013904223` modulo 2^32; a coordinate is
`((state >> 16) % 257 - 128) / 8`. The C++ probe evaluates draws sequentially to preserve C#
argument order. Vector samples take four coordinates (value X/Y, divisor X/Y). Matrix samples
use `near = abs(draw)+1`, `far = near+abs(draw)+1`, with dimensions/angle 1 and 2.
The dyadic coordinates are exactly representable; no language-specific random generator is used.

All NaNs emit `"nan"` (payload/sign unassessed), infinities `"+inf"`/`"-inf"`, and zeros
`"+zero"`/`"-zero"`. Other fields contain the actual Single promoted to a JSON number. Thus
signed-zero changes are detectable in these categories, while raw JSON NaN/Infinity remain
invalid. The original categories make no integer packing/cast or aliasing claim. Matrix aliasing is
bounded explicitly in follow-up 2 below; there is no exhaustive math/API claim.

The independent expectations are literal arithmetic/IEEE witnesses and analytically derived
perspective coefficients in `Vector2EdgeReferenceTest`, `MatrixPerspectiveEdgeReferenceTest`
and `test_value_edges.py`. The Python test checks both real dumps. Copy-hash equality and
agreement between overloads are consistency checks only: implementations can share a mistake.
Large/tiny axis expectations use the **existing measured-XNA decision**, independently of the
fresh FNA run. No genuine XNA binaries were run for CONF-003.

### Retained dispositions and reproduction

On Linux x86_64, Release/SOFTWARE/shared CNA and the pinned FNA/net8 reference, the strict
initial zero-tolerance edge comparison reported **60 differences**, all retained.
Follow-up 1 expanded the audit to **155**; follow-up 2 brought it to **566**. The geometry batch reached **680 retained and zero undecided** diagnostics. The packing batch reached **807 retained and zero undecided** after the shipped-XNA half IL correction. The Curve batch reached **1,802 retained and zero undecided** after shipped-XNA Curve IL inspection; the [scalar-division follow-up](#campaign-evidence-map-mth-conf-001007) raises the current audit to **1,859**; see follow-ups 3–5 below:

| Witnesses | Count of comparator diagnostics | Disposition |
|---|---:|---|
| Vector2 large/tiny Length and three Normalize forms | 11 | Preserve XNA's wide intermediates, commit `bd46b6ffe`, `MagnitudeCompatibilityTests` and the new literal-axis regression. Large FNA Length is +inf and normalization X is +zero; tiny FNA Length is +zero and normalization is (+inf,nan). CNA retains the finite magnitude and (1,+zero). |
| Perspective refusal exception types | 38 | Preserve `ArgumentOutOfRangeException` versus FNA's `ArgumentException`, measured-XNA repair `2a245b91f` and `MatrixPerspectiveCompatibilityTests`. Both implementations preserve the sentinel on these refusals. |
| FOV at public MathHelper.Pi | 11 | The same measured-XNA repair rejects public Pi before touching the output. FNA accepts it below its slightly larger literal cutoff and writes a projection. The diagnostics include a missing successful return object, two outcome fields and eight changed output coefficients. |

`check-value-edges.py` is a separate disposition audit, **not a parity comparator**. It uses
the strict comparator at zero tolerance, requires the complete reviewed witness shapes and
reference revision, and verifies exactly these paths **and both values**, plus the follow-up dispositions below. An extra, changed or
disappearing difference fails review; there are no wildcard exclusions or tolerance increases.
The script's pass does not turn the raw comparison failure into an FNA parity claim.

After generating fresh dumps with the recipe and `cna_reference_dump`, run:

```sh
# FNA_EDGE_REFERENCE and CNA_EDGE_CANDIDATE name the actual fresh files.
python3 scripts/compare-fna-reference.py "$FNA_EDGE_REFERENCE" "$CNA_EDGE_CANDIDATE" \
  --category Vector2Edges --category MatrixPerspectiveEdges \
  --category Vector3Edges --category Vector4Edges --category QuaternionEdges \
  --category MatrixEdges --category GeometryEdges --category PackingEdges \
  --category CurveEdges --tolerance 0
# Expected exit 1: 1,859 exact retained differences, including the scalar-division follow-up.
python3 tools/fna-reference/check-value-edges.py "$FNA_EDGE_REFERENCE" "$CNA_EDGE_CANDIDATE"
# Expected exit 0: 1,859 exact retained differences, zero undecided.
# Every other field must match; unlisted, changed or disappearing differences fail.
export FNA_EDGE_REFERENCE CNA_EDGE_CANDIDATE
python3 -B tools/fna-reference/test_value_edges.py -v
```

The real-dump suite checks independent literal results and negative controls (changed/missing
witnesses, a boolean substituted for a retained number, disappearing retained differences,
and missing/wrong reference provenance). Without the two dump paths it explicitly skips.

CONF-003 execution on 2026-11-02: both allowed C++ targets built, FNA's two dumps were
byte-identical and all five categories repeated at zero tolerance. The FNA assembly identity
remains the 26.10.0.0 / `f973ec1edd32290f19750d6d3d26fba23ab1a813fedd71f1ea026edf03c9647c`
identity above, with FNA3D/SDL3 not loaded. CnaMathTests: **978 passed, 0 failed, 2 skipped**
(missing external ShipGame XML fixtures), including all seven new tests. Comparator tests:
49 passed; recipe tests: 5 passed; real-edge evidence tests: 9 passed. Executions unset DISPLAY
and WAYLAND_DISPLAY and use offscreen video/dummy audio; no GPU/window test ran.

A supplementary comparison of the old default categories still reports eight pre-existing
NonRenderingApis differences: BlendFunction Min/Max ordinals (CNA Min=3/Max=4 versus FNA's
reverse order, `9a42a7e82`, `StateEnumFallbackTest.BlendFunctionValuesMatchMicrosoftRuntime`)
and StencilMask/StencilWriteMask for all three depth presets (-1 versus FNA's 2147483647,
XNA repair `ce2d52444`, `DepthStencilStateTest`). They are retained outside this task's selected
families; no graphics state code was changed or graphics test newly run. PackedVector and
Viewport have no differences in that comparison at its existing 1e-4 tolerance. This is not
a fresh expansion or qualification of those families.

### Follow-up 1: Vector3, Vector4 and Quaternion

The follow-up adds one category per family, using the same seed, explicit IEEE classifications,
source revision and assembly identity. `SpatialVectorEdgeReference.cs` and
`QuaternionEdgeReference.cs` share the original helper through a partial internal probe class.
CNA invokes its corresponding real API calls in `CnaReferenceDump.cpp`.

| Category | Reviewed domain |
|---|---|
| Vector3Edges | 16 Divide/Clamp/equality/hash cases and six magnitude cases extending the Vector2 inputs to three components. Twelve spatial cases call Lerp, SmoothStep, Hermite, CatmullRom, Barycentric, Cross, Reflect, matrix/quaternion Transform and TransformNormal; each value and out-ref overload is emitted. Two additional Cross cases use zero vectors and parallel (1e20,1e20,0) vectors to expose subtraction zero signs and intermediate overflow/cancellation. |
| Vector4Edges | The same 16 arithmetic and six magnitude cases in four components. Twelve spatial cases cover the five interpolants and matrix/quaternion Transform, including Vector2, Vector3 and Vector4 source overloads, each value/out. Vector4 has no Cross, Reflect or TransformNormal; none is invented. |
| QuaternionEdges | Eleven unary cases: zero, identity, (1,2,3,4), large/tiny X-axis, NaN/infinity X-axis and four seeded quaternions; Length/LengthSquared, Normalize and Conjugate instance/value/out, Inverse value/out. Fifteen paired cases cover Lerp, Slerp and Concatenate value/out: identical, opposite, zero, dot=0.999999 threshold, seven identity/half-turn amounts, four seeded interpolation amounts between orthogonal unit quaternions. Twelve axis-angle/YPR factory cases and five rotation-matrix cases exercise value/out forms, nonunit/zero axes, nonfinite angles, seeded angles and all four diagonal-selection branches plus a zero matrix. |

Vector spatial fixed inputs are `(1,2,3[,4])`, `(4,3,2[,1])`, `(-2,1,0[,3])`,
`(2,-1,4[,-3])`, with amounts -1, 0, 0.25, 0.5, 1, 2, NaN and +infinity.
Four seeded spatial cases draw four vectors and use the first four amounts. Barycentric's
second amount is 0.25. The matrix is literal rows `(2,0,1,0.5)`, `(0,-3,0,0)`,
`(1,0,4,0)`, `(7,8,9,1)`; the quaternion is `(0.25,-0.5,0.75,1)` (deliberately nonunit).
These dyadic cases test arithmetic, extrapolation, clamping, component order and homogeneous
coordinates; they do not establish general interpolation precision or array/aliasing behavior.

The LCG resets before arithmetic and spatial samples in each vector category. Quaternion
unary samples draw four components; interpolation draws one coordinate divided by eight;
factory samples draw yaw/pitch/roll divided by eight. Each subgroup resets the same seed.
Fixed factory angles are zero, ±Pi/2, Pi, NaN and +infinity. Factory axes include UnitX,
UnitY, zero and `(2,-1,0.5)`. Matrix cases are identity, exact X/Y/Z half turns and all zero.
No inverse-trigonometric domain rejection or unit-axis normalization is assumed.

The new quaternion factory probes exposed **eight unequal fields** in value/out results at
seeded angles. CNA called the `float` overloads of sin/cos; the inspected FNA methods round
the half-angle to Single, call double Math.Sin/Math.Cos and cast back to Single before
multiplication. `Quaternion::CreateFromAxisAngle` and `CreateFromYawPitchRoll` now preserve
that evaluation order. For example, UnitX/angle=0.390625 changed X from bits `3e46bb1a` to
`3e46bb19`. The exact-bit native regression failed before the fix and passes after it;
all factory fields now compare at **zero tolerance**. This follows FNA, with no claim of a
new genuine-XNA measurement and no change to public signatures or documented XNA deviations.

The follow-up retains these **95 additional differences**, all pinned by path and both values:

| Family | Retained diagnostics | Existing decision |
|---|---:|---|
| Vector3 magnitude | 14 | `bd46b6ffe`, wide Length/Normalize |
| Vector3 Cross | 6 | `0e7be08e0`, wide cancellation and subtraction order; FNA emits -zero for Y and NaN for overflowed parallel Z |
| Vector4 magnitude | 17 | `bd46b6ffe` |
| Quaternion magnitude/Normalize | 38 | `bd46b6ffe`; includes finite rounding witnesses as well as overflow/underflow |
| Quaternion Inverse | 20 | `df9234202`, wide norm/reciprocal/product; finite rounding and extreme magnitudes |

The audit after follow-up 1 was **155** (original 60 + follow-up 95). No factory differences are
accepted. The strict comparator remains unchanged, no tolerance was widened, and every other
selected field matches exactly. The audit freezes these Quaternion observations in
`quaternion_retained_differences()` and checks every declared case/overload/component exists.

Follow-up verification on the same Linux Release/SOFTWARE/shared configuration: reference
recipe **8/8** categories reproduce at zero tolerance; the two dumps are byte-identical.
CnaMathTests **998 passed, 0 failed, 2 skipped** (the same missing ShipGame XML fixtures),
including **20 new tests**: 13 vector and seven quaternion tests. The real-dump suite has
**13 passing tests**, including literal vector/matrix/quaternion assertions on both dumps,
the factory regression, and omission/mutation controls for the new categories. Literal
arithmetic, Hamilton-product order and analytic rotations are independent expectations;
exact factory bits are measured FNA regressions. Analytic sqrt(1/2) assertions use 1e-7 in
native tests; this does not change the zero tolerance used in the paired dump comparison.

Follow-up 1 stopped after Vector3/Vector4/Quaternion. Follow-up 2 below adds Matrix coverage;
geometry, Color/PackedVector expansion and Curve remain unassessed by this campaign. No BoundingFrustum ray behavior,
C API, GPU/window, browser or native-Windows changes/runs are included. Native fixture skips
and remaining unassessed input domains are not turned into broader parity claims.


### Follow-up 2: Matrix beyond perspective

`MatrixEdgeReference.cs` adds **MatrixEdges**, paired with direct native calls in
`CnaReferenceDump.cpp`. The reviewed domain has **87 cases and 9,771 scalar leaves**. Each
matrix result records all 16 coefficients with the same explicit nonfinite/zero classifications.

| Group | Cases | Reviewed operations and inputs |
|---|---:|---|
| cameras | 20 | CreateLookAt, CreateWorld, CreateBillboard and CreateConstrainedBillboard, each value/out. Axis-aligned, coincident, explicit/default/nonunit optional forwards, zero/parallel up, both constrained fallback axes, positions just below/above the 0.0001 squared-distance threshold, directions on both sides of the 0.9982547 parallel threshold, large/tiny positions, NaN/infinity and four seeded triples of vectors. World interprets the `target` input as its forward vector; the name is shared only to expose the identical input. |
| rotations | 18 | CreateFromAxisAngle, CreateFromQuaternion and CreateFromYawPitchRoll, each value/out. Zero, ±Pi/2, Pi, NaN/infinite angles; zero/nonunit/large/tiny axes and quaternions; eight seeded angle triples. Quaternions include identity, zero, (1,2,3,4), extreme X-axis and (0.5,-0.5,0.5,-0.5); no normalization is assumed. |
| planes | 14 | CreateReflection and CreateShadow, value/out. Unit/scaled/zero/near-unit/large/tiny/NaN/infinite normals, parallel/zero light, four seeded normal/D pairs. The usual light is (1,-2,3); plane inputs and D are emitted. |
| algebra | 18 | Determinant, Invert and Transpose value/out/in-place, Lerp value/out/output-alias-first/output-alias-second. Identity, zero, singular axis, tiny/large diagonal, sequential 1..16, near-singular 2x2 block separated by one Single ulp, a matrix with -zero/NaN/infinity, six extra interpolation amounts (-1,0,1,2,NaN,infinity), four seeded dense matrices. Interpolation's other matrix is identity. |
| decomposition | 17 | Decompose status, exact exception type, scale/rotation/translation. Unit/odd-negative/tied-negative/two-negative scales, zero axis/all-zero, 1e-5 and 1e-4 scales, large/NaN/infinite X scale, shear 0.5, non-affine fourth column and four seeded diagonal scales. Translation is (7,8,9) for scale cases. Outputs start at distinct literal sentinels so FNA's partial writes before ArithmeticException are observable. |

Each group resets the same **0xC0FFEE03** LCG. Camera samples draw position, target and up,
three components each; rotations draw angle/pitch/roll and divide each coordinate by eight;
planes draw normal XYZ then D; algebra draws 16 row-major entries; decomposition draws three
scales. C++ brace initializers sequence every draw. The evidence suite checks independent
literal inputs and outputs, and rejects omitted matching fields even if removed from both dumps.
No array overloads, exhaustive threshold sweep, or general decomposition/recomposition claim is made.

The initial strict Matrix comparison had **415 diagnostics**. Four were a confirmed factory
defect: `CreateFromAxisAngle(UnitX, -0.84375)` returned M22/M33 bits `3f2a2789` instead of
`3f2a2788`, in both value and output forms. It called float sin/cos; the inspected reference
calls double Math.Sin/Math.Cos, then rounds to Single before the products. CNA now does that.
`MatrixEdgeReferenceTest.AxisAngleRoundsDoubleTrigBeforeMatrixArithmetic` failed all four
cosine assertions before the fix and passes after it. No public API or new XNA decision changed.

The remaining **411 Matrix diagnostics** are retained under verified existing decisions:

| Group | Retained diagnostics | Existing measured-XNA decision |
|---|---:|---|
| plane-derived matrices | 290 | `e4ddea6e0`: shadow normalizes its input plane, unlike FNA; Plane.Normalize uses wide squared magnitude and reciprocal with a Single sqrt store, including its near-unit rule. Reflection shares that normalization. This covers finite precision, extreme magnitudes and nonfinite propagation. Existing `PlaneNormalizationCompatibilityTests` exercises the storage model and both factories. |
| inversion | 72 | `6c96fc674`, XNASWEEP-172: narrowed minors/cofactors and wide determinant/reciprocal; verified against its 52 genuine-XNA inversion witnesses. The new exact-bit native seeded witness pins M13 `bd9dd283` versus FNA `bd9dd284`, in value/out/in-place forms. |
| decomposition | 49 | `705ea2324`: largest-axis reflection sign with stable ties, zero-axis success with undefined rotation, tiny/sheared refusal, wide magnitude, and nonfinite acceptance. FNA's Math.Sign may instead throw before assigning scale/rotation; the sentinel differences are frozen explicitly. Existing `MatrixDecomposeCompatibilityTests` records that boundary. |

`matrix_retained_differences()` freezes every path and **both observed values**, with overload
expansion only for explicitly named cases/operations. A changed, additional or disappearing
retained difference fails. The audit after follow-up 2 had **566 diagnostics** (155 earlier + 411).
No axis-angle difference is accepted; all remaining selected fields match at zero tolerance.
Camera, determinant, transpose and Lerp results have no observed differences in this domain.
This is a bounded disposition audit, **not FNA parity** or a new Microsoft XNA binary run.

Verification on 2026-11-02, Linux x86_64 Release/SOFTWARE/shared: both allowed native targets
built; **9/9** reference categories repeated at zero tolerance and the two real-FNA dumps were
byte-identical. The source/assembly identity above is unchanged; FNA3D and SDL3 were not loaded.
CnaMathTests: **1,010 passed, 0 failed, 2 skipped**, including **12 new Matrix tests** with
literal matrices, analytic degeneracy checks and frozen reference bits. The skips are the same
missing external ShipGame XML fixtures. Real-dump tests: **18 passed**; recipe tests: **5 passed**;
comparator tests: **49 passed**. Every executable ran with DISPLAY/WAYLAND_DISPLAY unset,
offscreen video and dummy audio. No graphics/window test was run.

This batch stops after Matrix. Plane/BoundingBox/BoundingSphere/BoundingFrustum/Ray comparisons,
Color/PackedVector edge expansion and Curve remain for subsequent batches. In particular,
BoundingFrustum::Intersects(Ray) was neither implemented nor probed. No C ABI, language binding,
GPU, browser or native-Windows qualification is claimed.


### Follow-up 3: Plane, BoundingBox, BoundingSphere, BoundingFrustum and Ray

`GeometryEdgeReference.cs` and the native dump add **GeometryEdges**: **49 cases and 3,902
scalar leaves**, with identical fixed/seeded inputs and explicit signed-zero/nonfinite strings.
No new probe calls or changes a frustum-ray method. Null managed references have no counterpart in
these C++ value/reference inputs and are outside the selected domain.

| Group | Cases | Reviewed domain |
|---|---:|---|
| planes | 13 | Normalize instance/value/out/in-place, Dot/DotCoordinate/DotNormal value/out, matrix/quaternion Transform value/out/in-place. Unit/scaled/zero/near-unit/large/tiny/NaN/infinite normals, four seeded normal/D pairs and a singular matrix. The ordinary matrix is diagonal (2,4,-1,1) with translation (7,8,9); quaternion (0.5,-0.5,0.5,-0.5); dot point (1,-2,3), homogeneous W=2. |
| relations | 20 | Box/sphere/frustum Contains against points and volumes; box/sphere/ray/plane/frustum intersections in every selected value/out form available on both APIs; CreateMerged box/sphere, CreateFromSphere and CreateFromBoundingBox value/out. Inside, touching, disjoint, zero-volume/zero-direction, inside-out box, mutated negative radius, stationary ray, parallel outside, six direction thresholds and six seeded centers/radii. Fixed comparison box is [-1,1]^3, sphere center (0,0,0.5)/radius 1 and frustum identity. |
| pointSets | 11 | Box and sphere CreateFromPoints on empty, single, repeated, axis pair, tied right triangle, large/tiny pairs and four seeded five-point sets. Empty-input exception names and point counts/coordinates are explicit. No null enumerable or array-overload claim. |
| frustums | 5 | Identity, touching (X translation 2), separated (3), enclosing/enclosed scales 0.5/2; bidirectional Contains/Intersects, all eight GetCorners value/output, CreateFromFrustum. Matrices are literal, so previous trigonometric precision decisions do not obscure the geometry comparisons. |

Each group resets the existing **0xC0FFEE03** LCG. Plane cases draw normal XYZ then D;
relation cases draw center XYZ divided by eight, then abs(draw)/8 as radius, with fixed box
half-extents (0.25,0.5,0.75), ray direction UnitX and plane Up/D=0. Point sets draw five XYZ
triples. The six fixed ray-direction X values are 0.5e-6, 1e-6, 2e-6, 9e-6, 1e-5 and 1.1e-5.
A negative radius is assigned through the public mutable field after valid construction; no
constructor refusal is bypassed or changed in CNA production code.

After correcting a native probe helper to update its JsonWriter by reference, the strict
geometry comparison reported **117 diagnostics**. Three were a new confirmed mismatch:
`BoundingFrustum::Intersects(Plane)` classified zero-distance corners separately from negative
corners; the inspected FNA method compares only `distance > 0`, grouping zero with the
nonpositive side. A zero plane returned Intersecting rather than Back, also through the
Plane forwarding method. CNA now uses the same boolean classification. The focused native
regression failed six assertions before the fix (zero plane and a nonzero plane coplanar with
the far face, each through the three public routes). All three dumped fields now match.
This follows the reference; no new genuine-XNA measurement is claimed.

The remaining **114 geometry diagnostics** are inventoried as follows:

| Disposition | Count | Existing evidence |
|---|---:|---|
| Plane normalization | 48 retained | `e4ddea6e0`: wide magnitude/reciprocal with Single sqrt store, over all four forms. |
| Ray-box stationary and parallel threshold | 12 retained | `e1377c437`: nonnegative parameter interval and strict abs(direction)<1e-6. Both box/ray value/output routes are explicit. |
| Sphere point/sphere containment and external tangency | 8 retained | `5ab1c7729`: strict point surface exclusion, signed radius difference for Contains(sphere), external tangent spheres do not intersect. |
| Box/sphere Contains(frustum) | 8 retained | `5ab1c7729`: actual convex separation precedes corner containment. Includes distant and inside-out boxes. |
| Sphere construction / CreateFromFrustum | 31 retained | `52f797f60`, `f46f26ea8`, `f5063e08c`: later-axis seed ties, wide distances/half-span radius, growth storage order and Distance-based span selection. Existing sphere-oracle tests remain authoritative for the XNA model; these are new FNA/CNA observations. |
| Touching frustum pair | 4 retained | `5ab1c7729`: GJK intersection rather than conservative Contains planes. |
| Negative-radius/frustum intersection | 3 retained | FNA false, CNA true for sphere.Intersects(frustum), frustum.Intersects(sphere), and its out form. Integration-owner decision (2026-11-02), under `5ab1c7729`: "algorithmic consequence of the measured XNA GJK port; negative radii were not separately measured against XNA". CNA's support mapping is center + v * Radius / length(v), while FNA uses a plane-distance test. Production behavior is unchanged. |

The seven-edge-category raw comparison reports **680 retained diagnostics and zero undecided**
(the prior 566 + 114 retained geometry). The audit pins every path and both values, then
**exits 0** only when those exact dispositions match and every other selected field equals the
reference at zero tolerance. The temporary undecided inventory and its test-only bypass were
removed after the integration-owner decision. Mutation controls still reject unlisted changes,
altered retained values and disappearing differences; the three negative-radius paths are
frozen separately in the evidence assertions. This is a disposition audit, not FNA parity.

Verification on 2026-11-02: both allowed native targets built; reference recipe **10/10**
categories reproduce at zero tolerance and its two dumps are byte-identical. Source revision,
assembly SHA-256 and unloaded native-library identities are unchanged from above. CnaMathTests
**1,020 passed, zero failed, two skipped** (the same external ShipGame XML fixtures), including
**10 new geometry tests**. Real-dump evidence suite: **22 passed**; recipe tests: **5 passed**.
The comparator implementation is unchanged; its 49 tests already passed in the Matrix batch.
Runs unset DISPLAY/WAYLAND_DISPLAY and use offscreen video/dummy audio. No GPU/window, C ABI,
language binding, dependency or new XNA-oracle execution is included.

Follow-up verification after the integration-owner decision: the default exact audit passes
with **680 retained differences, zero undecided**; the **22** real-dump evidence tests and
CnaMathTests (**1,020 passed, zero failed, two existing fixture skips**) pass again using the
same dumps and native binary. No production change or new XNA measurement was made to resolve
the three fields. Their caveat remains: "algorithmic consequence of the measured XNA GJK port; negative radii were not separately measured against XNA".

The geometry batch ended here; follow-ups 4–5 below complete the selected packing/Curve expansion. General geometry precision, invalid frustum matrices,
nonfinite convex-volume inputs and unselected array/aliasing routes are not qualified by this
comparison. The negative-radius disposition preserves the ported algorithm; it is not a
separate empirical qualification of that input against Microsoft XNA.


### Follow-up 4: Color and all 17 PackedVector types

`PackingEdgeReference.cs` and the native dump add **PackingEdges**: **366 cases, 10,654 scalar
leaves**. Every packed word uses exactly **16 lowercase hexadecimal digits**, including 8/16/32-bit
storage, so no 64-bit value crosses JSON's floating-point integer boundary. Expanded float channels
use the same explicit `+zero`/`-zero`/`nan`/`+inf`/`-inf` classifications as earlier probes.

| Group | Cases | Reviewed domain |
|---|---:|---|
| packedTypes | 323 (19 per type) | Alpha8, Bgr565, Bgra4444, Bgra5551, Byte4, HalfSingle, HalfVector2, HalfVector4, NormalizedByte2/4, NormalizedShort2/4, Rg32, Rgba1010102, Rgba64, Short2/4. Component and vector constructors (Alpha8/HalfSingle have only scalar constructors), IPackedVector.PackFromVector4/ToVector4, raw PackedValue setter/getter, and re-packing the raw expansion. No universal lossless float round-trip assumption. |
| colors | 19 | Color(Vector4), Color(Vector3), three/four-float constructors, PackFromVector4, raw PackedValue setter/getter, FromNonPremultiplied(Vector4); each result emits RGBA, packed word, ToVector3 and ToVector4. |
| integers | 6 | Three/four-int constructors and FromNonPremultiplied(int,int,int,int). Byte boundaries and signed extremes; integer premultiplication products are deliberately within Int32 range. Int32.MinValue/MaxValue are paired with alpha zero; no C++ signed-overflow claim. |
| interpolation | 18 | Color.Lerp, Multiply and Color*scale between fixed colors (255,1,0,254)/(0,254,1,0), with endpoint/extrapolation/NaN/infinite/subunit fixed-point factors and four seeded factors. |

The 19 vector cases are zero, signed zero, one, negative one, quarters, channel half-ties,
the immediately preceding/following Single at each tie, low/high clamp vectors, byte/short
limits, half-float normal/subnormal/overflow boundaries, nonfinite channels and four seeded
vectors. Tie channels are (0.5/sX,1.5/sY,2.5/sZ,3.5/sW), where s is the format's integer
channel maximum (31/63/31 for Bgr565, 31/31/31/1 for Bgra5551, 1023/1023/1023/3 for
Rgba1010102; 255,15,127,32767,65535 as appropriate elsewhere; raw integer and half types use 1).
Alpha8 consumes W, HalfSingle consumes X, and smaller vector types consume their documented
leading channels. Only the common IPackedVector ToVector4 expansion is newly compared; natural
ToVector2/ToVector3/ToAlpha/ToSingle methods remain covered by earlier tests, not a new paired claim.

The half boundary vector is (1.00048828125,1.00146484375,65504,65520); the tiny vector is
(2^-25,3*2^-25,2^-24,1023*2^-24). The nonfinite vector uses a positive canonical Single NaN
(bits 7fc00000), +infinity, -infinity and -zero; arbitrary NaN payloads/signs are unassessed.
For each case, raw words cycle through 0, 1, ffffffffffffffff, 8000800080008000,
7fff7fff7fff7fff, 7e007c00fc008000, masked to the type's width. This includes the otherwise
unreachable minimum SNORM codes and exponent-31 half codes.

The deterministic **0xC0FFEE03** LCG resets for every packed type and for the Color vector
subgroup. Four seeded vectors draw XYZW/8, with C++ brace initialization enforcing draw order.
The four seeded interpolation factors continue the Color subgroup's state after its 16 vector
draws. All fixed and seeded input vectors, raw words, integer channels and interpolation factors
are emitted. No timestamp or standard-library random generator is involved.

The initial strict comparison reported **175 packing diagnostics** before the half correction below:

| Disposition | Count | Evidence boundary |
|---|---:|---|
| Minimum SNORM raw-code expansion | 39 retained | `b6f127b15`, inspected PackUtils.UnpackSNorm decision: 0x80/0x8000 expands to exactly -1 in CNA, while FNA divides the signed value by 127/32767. Four NormalizedByte/Short types; the repacked words agree. |
| Color fixed-point Lerp/Multiply/operator | 40 retained | `1ad0a3916`, ISSUE-132 and its 1,782-check ColorFixedPointOracle: NaN Lerp retains the first color, positive-infinite scaling saturates nonzero channels, and packed-factor boundaries differ from FNA's floating arithmetic. Exact paths/values are frozen, including derived expanded channels. |
| HalfSingle/HalfVector2/HalfVector4 decode and repack | 96 initially undecided; now settled | The subsequent shipped-XNA HalfUtils IL inspection below requires extended finite decoding and different saturation. These observations are no longer accepted as IEEE behavior or left undecided. |

Before correction, all selected constructor and PackFromVector4 words matched FNA. All Color
constructors/premultiplication/int/raw conversions still match. The actual pinned FNA Color
constructors already use Math.Round; historical comments describing truncation are not authoritative.

The initial 96 half observations (15 HalfSingle, 27 HalfVector2, 54 HalfVector4) prompted inspection
of the installed original Microsoft XNA 4.0 Windows assembly. The types live in
**Microsoft.Xna.Framework.dll**, SHA-256
`38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130`, version `4.0.0.0`,
public key token `842cf8be1de50553`. The
[HalfUtils inspection and local regeneration notes](../tools/fna-reference/xna-half/README.md) identify:

- `Unpack(uint16)`, 139 bytes, IL_005f–IL_008a: exponent 31 is finite, so 7c00/7e00/7fff
  expand to 65536/98304/131008 (with the stored sign). CNA's former IEEE special case was wrong.
- `Pack(float32)`, 145 bytes, IL_001b–IL_002c: unsigned absolute bits above 47ffefff saturate
  to sign|7fff, including NaN/infinity. IL_002e–IL_008e handles subnormals, rebias and ties to even.
  Exponent-31 finite values are representable. FNA's IEEE-style overflow/NaN packing also differs.
- Constructors, interface pack/unpack, natural ToSingle/ToVector2/ToVector4 and raw PackedValue
  paths were inspected: they flow through HalfUtils without normalizing the raw integer setter.

Microsoft disassembly stays outside the repository; the linked notes provide the local
`ikdasm` regeneration command and original prose summaries, without copied instruction listings.
CNA now matches the inspected integer/bit behavior. `XnaHalfConversionTest` has three independent
literal-bit regressions for helper overloads and public paths; all three failed before the fix.
Old native IEEE expectations were corrected to the authoritative XNA contract. The original
assembly probe compiles with mcs for all 57 half records, including every originally undecided
input, but Mono fails in the mixed-mode `<Module>` initializer (`_getFiberPtrId`) before any
measurement. **No successful XNA runtime measurement is claimed**; the exact shipped IL settles it.

After correction, **57 of the original 96 fields match FNA**, while 39 existing paths have changed
XNA results and nine constructor/pack paths become new retained differences. The **48 remaining
half diagnostics** are frozen against the new IL decision: nine constructor/PackFrom words,
18 expanded constructor/interface channels, 21 raw repacked words. Direct raw expansion agrees
with FNA. Example: positive Single NaN now packs to 7fff and expands to 131008 in CNA, while FNA
packs 7e00 and expands to 98304; raw ffff expands to -131008 on both, then repacks to ffff in CNA
and fc00 in FNA. See `half_retained_differences()` for every exact path and both values.

The eight-edge-category raw comparison now reports **807 retained differences, zero undecided**
(previous 680 + 39 SNORM + 40 Color + 48 half). The default audit passes only on those exact
path/value pairs and an equal remainder at tolerance zero. The temporary undecided inventory and
bypass are removed; changed, unlisted or disappearing differences fail. This remains a bounded
disposition audit, not FNA parity.

Verification on 2026-11-02, the same Linux x86_64 Release/SOFTWARE/shared build and pinned FNA
source/assembly: reference recipe **11/11** categories reproduce at zero tolerance, byte-identical
dumps; no SDL3/FNA3D loaded by the managed probe. Native targets cna_reference_dump, CnaMathTests
and CnaGraphicsTests built. CnaMathTests **1,023 passed, zero failed, two existing fixture skips**;
selected CPU-only packed-vector/value-contract/oracle graphics suites **141 passed, zero failed,
zero skipped**. This includes **20 new tests** (three Color, one for each packed type), using
literal independent reference words/channel values and the existing measured-XNA fixed-point
witnesses. Those initial tests left half decoding undecided; the additional IL regressions now settle that contract. Real-dump
suite **26 passed**; recipe tests **5 passed**. Native executions unset both displays and use
offscreen video/dummy audio; no GPU/window test is included.

Half correction verification: the affected targets build; the selected CPU packing suites pass
with **154 tests**, including all three new IL regressions, and CnaMathTests passes **1,023 tests**
with two existing fixture skips. The real-dump suite passes **26 tests** on the corrected candidate;
the default audit passes with **807 retained, zero undecided**. FNA source/dumps are unchanged.
No GraphicsDevice, GPU/window, browser, native-Windows runtime or new original-assembly execution
success is claimed. General hash/text contracts, all named Color constants, arbitrary NaN payloads,
integer-overflow domains and every unselected input/array/aliasing path are outside this paired coverage.


### Follow-up 5: Curve evaluation and tangents

`CurveEdgeReference.cs` and the native dump add **CurveEdges**, with **12 input curves and
13,003 scalar leaves**. Each curve runs every **25 pre/post CurveLoopType combination** at
**12 positions** (3,600 evaluations total), and all **nine in/out CurveTangent combinations**
through both ComputeTangents overloads and both ComputeTangent overloads. Every tangent run
uses freshly constructed keys; Curve.Clone reference identity/shallow-copy semantics are not assumed.

Fixed inputs: empty; single (position 2/value 7); smooth keys (position,value,in,out) at
(0,1,2,3),(1,-2,4,5),(3,5,6,7); shifted Step keys (2,1,0,0),(4,9,0,0); duplicate-start keys
(0,0,0,0),(0,10,0,0),(1,20,0,0); all-equal keys (0,3,1,2),(0,9,4,5); tiny and epsilon spans
with value 7 at position 0 and value 9 at 1e-11 or 2^-23, initially zero tangents. Four seeded
curves use positions 0,1,3 and draw value/TangentIn/TangentOut per key sequentially from the same
**0xC0FFEE03** LCG. The seed resets once before all four curves (36 coordinate draws).

Finite evaluation positions are first + span * [-2,-1,-0.5,-0,0,0.25,0.5,1,1.5,2,3], followed
by NaN. Empty/single curves use a synthetic first/span for queries; all-equal curves query only
their actual position and NaN. This keeps finite cyclic counts small and avoids undefined C++
float-to-int overflow for nonfinite or very distant cycle counts. Infinity cycle positions and
zero-span outside-loop behavior are explicitly unassessed. Negative and end key-index exception
names are recorded through the two single-key tangent overloads.

FNA inserts equal-position keys in a different order from CNA. The initial direct-Add probe
reported 1,355 diagnostics, many downstream effects of unequal actual evaluation inputs. The
final probe records **insertionKeys** separately, then replaces indexed keys with fresh keys
in the intended sorted input order before evaluation/tangent computation. Every input position
list is nondecreasing, so these replacements keep the position and only restore the intended
key data. The evidence tests require identical actual evaluation keys on both implementations.
The insertion discrepancy remains explicit; it is not silently filtered or treated as XNA evidence.

Final Curve diagnostics: **995**:

| Disposition | Count | Evidence / limitation |
|---|---:|---|
| Evaluate boundaries | 975 retained | Verified existing `d753d0997`: NaN scan outcome, normalized Step segment threshold, duplicate/tiny segment selection (span <=1e-10), and last-key tangent for post-linear extrapolation. Exact case/query/loop names and both values are frozen by `curve_retained_differences()`; 68 grouped observations expand only to explicitly listed loop keys. |
| Equal-position insertion ordering | **8 retained** | For duplicateStart, FNA Add gives values [10,0,20], CNA [0,10,20]. For allEqual, FNA reverses the two keys, including their value/in/out fields; CNA preserves insertion order. Local [Curve inspection notes](../tools/fna-reference/xna-curve/README.md), Add `IL_0020`–`IL_005a`, advances past equal keys: CNA agrees. The original direct-Add dumps remain in the lane report. |
| Tiny-span Smooth outgoing tangent | **12 retained** | Keys at positions [0,1e-11], values [7,9], initially zero tangents. FNA gives first outgoing tangent 0, CNA 2 through the affected bulk/single-key forms. Shipped Curve.ComputeTangent `IL_010e`–`IL_011f` checks the value delta against 2^-23; `IL_012d`–`IL_013e` calculates 2 for this input. Delegating overloads share that calculation. CNA agrees for these selected fields; no production change is needed. |

No Curve production source was changed. Eight new native regressions provide independent literal
pre/post loop values, empty/single behavior, existing measured-XNA Step/tiny behavior, linear/smooth
normal-span tangents through every overload, index-refusal state preservation, and the two
IL-settled contracts above. Shape/omission controls cover every loop/tangent entry;
mutation/disappearance controls protect all retained observations.

Across nine edge categories, the raw comparison reports **1,802 retained diagnostics and zero
undecided** (807 earlier + 975 existing Curve evaluation decisions + 20 shipped-IL decisions).
The default audit passes only with that exact inventory and a remainder equal at tolerance zero.
The temporary undecided map and inventory-only bypass have been removed. The [local inspection notes](../tools/fna-reference/xna-curve/README.md) record
`Microsoft.Xna.Framework.dll`, version `4.0.0.0`, public key token `842cf8be1de50553`,
SHA-256 `38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130`,
method offsets and the command to regenerate disassembly from a maintainer's own copy.
Microsoft disassembly stays outside the repository and is not required by the audit or tests; the mixed-mode original assembly could not initialize under
Mono, so this is not a successful original-XNA runtime measurement. The selected tangent result
does not establish general equality of CNA and XNA tangent implementations.

Verification on 2026-11-02: reference recipe **12/12** categories reproduce at zero tolerance;
two aligned-fixture FNA dumps are byte-identical, with unchanged pinned source/assembly identity
and no native SDL3/FNA3D loaded. cna_reference_dump and CnaMathTests build. CnaMathTests
**1,031 passed, zero failed, two existing ShipGame fixture skips**, including all eight new Curve
tests. Real-dump tests **30 passed**; recipe tests **5 passed**. The earlier selected CPU half/packing
run remains **154 passed**. No GPU/window/native-Windows execution is claimed.

All requested bounded families have been attempted and all observed differences are decided. The comparisons do not establish exhaustive
Curve precision, Clone/collection reference semantics, arbitrary invalid enums, unbounded cycle
conversion, hashes/text or the complete XNA API's behavior.
