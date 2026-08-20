# Audit: examples/d3d9_common_test.cpp

## Metadata

- Source file: `examples/d3d9_common_test.cpp` (149 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — pure-function format/state/vertex-layout mapping-table
  checks (`plans/plan_dx9.md` D9-20/D9-21/D9-22).
- File type: standalone `main()`-based executable, no `Game`/device/window — CTest-registered as
  `D3D9_Common` (`cna_d3d9_test(cna_test_d3d9_common examples/d3d9_common_test.cpp)`,
  `cmake/Tests/D3D9Tests.cmake:28-35`), gated behind `CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND
  STREQUAL "D3D9"`.
- XNA/FNA relevance: indirect — validates D3D9-specific format/state enum translation tables that
  back `Microsoft::Xna::Framework::Graphics::SurfaceFormat`/`DepthFormat`/`Blend`/`BlendFunction`/
  `CompareFunction`/`CullMode`/`FillMode`/`StencilOperation`/`TextureAddressMode`/`TextureFilter`.
- Related production code: `include/CNA/Internal/Backends/D3D9/D3D9FormatMapping.hpp`+`.cpp`,
  `D3D9StateMapping.hpp`+`.cpp`, `D3D9VertexDeclarations.hpp`+`.cpp` — all read in full for this
  report.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only. This report is based
entirely on static source reading; no build or execution was attempted or claimed in this Linux
sandbox. The functions under test are pure (no device/window/GPU dependency), which is exactly why
this file was designed to need no device — the mapping tables themselves were nonetheless verified
by direct comparison against Microsoft's own documented D3DFORMAT/D3DDECLTYPE semantics as cited in
the production code's own comments, not by execution.

## Purpose

A `main()`-based, non-`Game`, exit-code CTest that exercises three independent, self-contained
D3D9 mapping tables with zero device dependency: `SurfaceFormatToD3D9`/`DepthFormatToD3D9`
(`D3D9FormatMapping`), `BlendToD3D9`/`BlendFunctionToD3D9`/`CompareFunctionToD3D9`/
`CullModeToD3D9`/`FillModeToD3D9`/`TextureAddressModeToD3D9`/`StencilOperationToD3D9`/
`TextureFilterToD3D9` (`D3D9StateMapping`), and `VertexElementsForStrideD3D9`
(`D3D9VertexDeclarations`). Placement is correct: a pure-function CTest with no `Game` subclass,
appropriately kept separate from the device-dependent `D3D9_Draw`/`D3D9_DrawEx` tests.

## Executive Verdict

**Healthy** — every assertion in this file was independently traced against the current production
source and, where a Microsoft API convention was invoked (D3DFMT_A2B10G10R10 vs.
D3DFMT_A2R10G10B10, D3DDECLTYPE_UBYTE4N vs. D3DDECLTYPE_D3DCOLOR), against the underlying
byte-layout reasoning that production code's own comments cite. All 24 checks assert something the
current production code actually does, and each assertion is a real, non-trivial discriminator (not
a tautology).

## Checklist Results

### API / XNA / FNA parity
N/A directly — this file tests `CNA::Internal::Backends::D3D9` translation tables, a backend
implementation detail, not the XNA-facing API surface itself. Indirectly relevant: the
`SurfaceFormat`/`DepthFormat`/`Blend`/`CullMode`/etc. enums it feeds in are the real
`Microsoft::Xna::Framework::Graphics` enums (`#include`s at lines 11-20), so the checks are
exercising the real XNA-to-native mapping boundary, correctly scoped to that boundary.

### Behavioral correctness
- `SurfaceFormat::Color -> D3DFMT_A8B8G8R8` (line 40-41) and `ColorBgraEXT -> D3DFMT_A8R8G8B8`
  (line 42-43): confirmed against `D3D9FormatMapping.cpp` lines 15-19/50-53, whose own comment
  correctly derives these from Microsoft's documented D3D9-to-D3D10 legacy-format table
  (`D3DFMT_A8B8G8R8 <-> DXGI_FORMAT_R8G8B8A8_UNORM`, matching XNA's own R,G,B,A ascending
  `Color.PackedValue` byte order) — this is the same R/G/B channel-order reasoning this project's
  own D3D9 draw test (`d3d9_draw_test.cpp`) later proves empirically via an actual GPU readback.
- `Rgba1010102 -> D3DFMT_A2B10G10R10` (NOT `D3DFMT_A2R10G10B10`, line 46-47): the test's own label
  explicitly calls out that this is bit-layout-driven, not name-similarity-driven — confirmed
  against `D3D9FormatMapping.cpp` lines 31-38's own from-first-principles derivation (traces both
  formats' actual channel-order bit layout rather than assuming the `A2R10G10B10` name is the
  match because it "looks like" `Rgba1010102`). Both file and test independently reach the same,
  correct, non-obvious conclusion.
- `Bc7EXT -> D3DFMT_UNKNOWN` (line 50-51): confirmed — `D3D9FormatMapping.cpp` line 60's comment
  correctly notes BC7 postdates D3D9 (introduced with D3D11), so `D3DFMT_UNKNOWN` is a genuine
  "no equivalent," not a silent wrong substitution — the `default:` case at the end of the switch
  (line 67) would also produce this, but `Bc7EXT` is explicitly enumerated with its own comment
  rather than silently falling through, so the check is verifying a documented decision, not an
  accidental default.
- `DepthFormat::Depth24 -> D3DFMT_D24X8` (line 54-55): correctly distinguishes D3D9 (which has a
  genuine depth-only 24-bit format) from D3D11 (which does not, per the test's own comment) —
  confirmed against `D3D9FormatMapping.cpp` lines 74-75.
- State-mapping checks (lines 60-80): `BlendToD3D9`, `BlendFunctionToD3D9`, `CompareFunctionToD3D9`,
  `CullModeToD3D9` (both winding directions), `FillModeToD3D9`, `TextureAddressModeToD3D9`,
  `StencilOperationToD3D9` (specifically distinguishing `IncrementSaturation` (`D3DSTENCILOP_INCRSAT`,
  clamping) from `Increment` (`D3DSTENCILOP_INCR`, wrapping) — a real, easy-to-invert pair) — all
  directly confirmed against `D3D9StateMapping.cpp`'s corresponding `case` labels (grepped
  directly: `CullClockwiseFace -> D3DCULL_CW`/`CullCounterClockwiseFace -> D3DCULL_CCW`,
  `Increment -> D3DSTENCILOP_INCR`/`IncrementSaturation -> D3DSTENCILOP_INCRSAT` all match exactly).
- `TextureFilterToD3D9` decomposition checks (lines 82-96): `MinLinearMagPointMipLinear ->
  {LINEAR,POINT,LINEAR}`, `Anisotropic -> {ANISOTROPIC,ANISOTROPIC,LINEAR}` (the test's own comment
  correctly notes D3D9 cannot express an anisotropic mip filter, so `LINEAR` is the only sane
  fallback for the mip stage), `Point -> {POINT,POINT,POINT}` — a `D3D9FilterTriple{min,mag,mip}`
  struct return, confirmed present in `D3D9StateMapping.cpp` (`TextureFilterToD3D9` function
  found at that file).
- Vertex-declaration checks (lines 99-145): stride 16 (`POSITION0`+`COLOR0` at offset 12,
  `D3DDECLTYPE_UBYTE4N`), stride 24 (adds `TEXCOORD0` at offset 16), stride 28 (the D3D9-only
  `Position+TexCoord0+TexCoord1` layout for `DualTextureEffect`'s real `VSInputTx2`), stride 52
  (full skinned layout, `BLENDINDICES` at offset 48 as `D3DDECLTYPE_UBYTE4`), and the
  `D3DDECL_END()` sentinel check right after stride 52's 5 real elements — every one of these was
  cross-checked field-by-field against `D3D9VertexDeclarations.cpp`'s `kStride16`/`kStride24`/
  `kStride28`/`kStride52` arrays and matches exactly, including the offsets, `D3DDECLTYPE_*`
  types, and `UsageIndex` values (stride 28's two `TEXCOORD` elements at `UsageIndex 0`/`1`).
- Unrecognized-stride check (line 141-145): `VertexElementsForStrideD3D9(999, count)` returns
  `nullptr`/`count=0` — confirmed against the production `switch`'s `default: count = 0; return
  nullptr;` (last line of `VertexElementsForStrideD3D9`). A real robustness check, not a tautology
  (a buggy fall-through could easily return a stale `count` or a dangling table pointer).

### Logic
The `D3DDECLTYPE_UBYTE4N` vs. `D3DDECLTYPE_D3DCOLOR` choice for `COLOR0` (asserted at lines
102-107 and 112-117) is the single most safety-critical assertion in this file — a regression back
to `D3DDECLTYPE_D3DCOLOR` would silently swap the R/B channels of every vertex color in the engine
without any compile error. `D3D9VertexDeclarations.cpp`'s own header comment (lines 4-17) documents
this exact historical bug (real finding, reproduced live: opaque red fed through the old
declaration read back as opaque blue) and the fix, giving this specific assertion unusually strong,
independently-documented backing.

### C++ correctness
Trivial by design (a linear sequence of pure function calls and boolean assertions via a shared
`check()` helper, lines 30-35). No lifetime/ownership/threading concerns — `D3D9VERTEXELEMENT9`
pointers returned by `VertexElementsForStrideD3D9` point at static/`constexpr` storage in the
production `.cpp`'s anonymous namespace, so the raw pointers used at lines 101/111/121/131/143 are
never dangling.

### Memory/resource lifetime, Thread safety
N/A — no device, no allocation, no cross-thread interaction anywhere in this file.

### Performance
N/A — a one-shot diagnostic binary, not a hot path.

### Architecture
Correctly scoped: exercises only `CNA::Internal::Backends::D3D9` internals through their own
headers, does not reach into `IGraphicsBackend`/`GraphicsDevice`/`Game`, consistent with its stated
"no device/window/GPU needed" design (line 3).

### Maintainability
Clean, consistent `check(bool, label)` idiom throughout; every label documents WHY the expected
value is what it is (memory order, bit layout, wrapping-vs-clamping semantics), not just WHAT is
expected — a good example of self-documenting test labels per this project's own testing
conventions.

### Robustness
The unrecognized-stride case (line 141-145) and the `D3DDECL_END()` sentinel check (line 136-139)
are the file's two genuine edge-case/malformed-input checks; both are real (not degenerate) and
both pass against current production code.

### Testing
This file itself IS the test coverage for `D3D9FormatMapping.cpp`/`D3D9StateMapping.cpp`/
`D3D9VertexDeclarations.cpp`'s pure-function surface. Coverage is broad across all three tables but
not exhaustive — e.g. `SurfaceFormatToD3D9`'s production switch has ~24 cases (Bgr565, Bgra5551,
Bgra4444, Dxt1, Dxt3, NormalizedByte4, Rg32, Rgba64, Alpha8, Single, Vector2, Vector4, HalfSingle,
HalfVector2, HalfVector4, HdrBlendable, ColorSrgbEXT, Dxt5SrgbEXT, Bc7SrgbEXT, ByteEXT, UShortEXT)
and this test exercises only 6 of them plus the `default:` path implicitly via `Bc7EXT`. Similarly,
`D3D9StateMapping.cpp` almost certainly has more `Blend`/`CullMode`/etc. enum values than the one
representative case each asserted here (e.g. only `Blend::One`/`InverseSourceAlpha` are checked out
of the full XNA `Blend` enum). This is consistent with the file's own stated scope ("unit checks,"
not an exhaustive enum-completeness sweep) but is worth flagging as a coverage gap per
`AUDIT_CHECKLIST.md` §13 — see Missing or Weak Tests below.

### Cross-file consistency
All three production files (`D3D9FormatMapping.cpp`, `D3D9StateMapping.cpp`,
`D3D9VertexDeclarations.cpp`) were read in full and every assertion in this test traces to a real,
current `case`/array entry in them — no stale or already-superseded expected value was found in
this file (unlike some findings in sibling D3D9 test files in this batch, see
`d3d9_drawex_test.cpp.audit.md`).

## Detailed Findings

No CRITICAL/HIGH findings. No MEDIUM findings either — this is one of the stronger files in this
batch precisely because every assertion is independently traceable to current, correct production
logic with a documented rationale.

## Missing or Weak Tests

- **Enum-completeness gap (LOW, confidence HIGH):** each of `SurfaceFormatToD3D9`,
  `DepthFormatToD3D9`, `BlendToD3D9`, `BlendFunctionToD3D9`, `CompareFunctionToD3D9`,
  `CullModeToD3D9`, `FillModeToD3D9`, `TextureAddressModeToD3D9`, `StencilOperationToD3D9` has
  meaningfully more enum cases in its production `switch` than this file exercises (e.g. only 1-2
  representative values per table, out of what is typically 6-24 real cases per XNA enum). A
  regression that broke an untested case (e.g. `Blend::Zero`, `TextureAddressMode::Clamp`,
  `CompareFunction::Always`) would not be caught by this file. Not a defect in the file as written
  (it explicitly scopes itself to spot-checking the trickiest/most error-prone cases — the ones
  with non-obvious bit-layout or wrapping-vs-clamping semantics — which is a defensible triage
  choice for a hand-written unit test), but a genuine coverage gap worth flagging for a future
  task that would benefit from a table-driven, enum-complete sweep (e.g. a `for` loop over every
  enum value cross-checked against a second, independently-authored oracle table).

## Positive Findings

- The `D3DDECLTYPE_UBYTE4N`-vs-`D3DDECLTYPE_D3DCOLOR` R/B-swizzle check (lines 102-107) targets a
  real, previously-live, empirically-confirmed bug (documented in
  `D3D9VertexDeclarations.cpp`'s own header) rather than a hypothetical one — strong evidence this
  test was authored with genuine knowledge of the underlying risk, not boilerplate.
- The `Rgba1010102`/`Bc7EXT`/`IncrementSaturation`-vs-`Increment` checks each specifically target a
  plausible off-by-name-similarity mistake (A2R10G10B10 vs. A2B10G10R10; INCR vs. INCRSAT) rather
  than a generic "does it return non-garbage" smoke check — genuinely discriminating assertions.

## Final Assessment

A small, well-targeted, and — as far as this audit's static reading and independent cross-checking
against current production source can determine — fully correct pure-function test file. Its one
legitimate gap is breadth (not every enum case of every table is exercised), which is a reasonable
and disclosed scope choice for a hand-authored spot-check test rather than a defect.
