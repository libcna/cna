# FNA reference harness

Current source/build and bounded edge contract updated for MTH-CONF-003 on 2026-11-02. Phase 53 (Tasks 471–480) introduced the
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
builds the probe, generates values-1.json and values-2.json and compares all eight mirrored
non-rendering categories (including the five edge categories below) at zero tolerance. FNA and harness outputs stay in their own normal
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
without build-revision metadata. The recipe's optional --compare-to accepts an ordinary CNA candidate dump
and compares all eight categories; retained XNA differences therefore make that raw comparison fail.
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
and mandatory in the recipe's ten-category FNA repeatability check alongside the follow-up categories below.

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
Follow-up 1 expanded the audit to **155**; follow-up 2 brought it to **566**. The current geometry batch has **680 retained and zero undecided** diagnostics; see follow-up 3 below:

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
  --category MatrixEdges --category GeometryEdges --tolerance 0
# Expected exit 1: 680 documented retained diagnostics, preserved in the raw diagnostic.
python3 tools/fna-reference/check-value-edges.py "$FNA_EDGE_REFERENCE" "$CNA_EDGE_CANDIDATE"
# Expected exit 0: exactly 680 retained differences; every other selected field matches.
# Unlisted, changed or disappearing differences fail review.
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

The geometry batch stops here for the owner to commit. Color and all PackedVector edge
expansion, then Curve, remain unstarted. General geometry precision, invalid frustum matrices,
nonfinite convex-volume inputs and unselected array/aliasing routes are not qualified by this
comparison. The negative-radius disposition preserves the ported algorithm; it is not a
separate empirical qualification of that input against Microsoft XNA.
