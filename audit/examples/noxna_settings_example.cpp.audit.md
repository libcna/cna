# Audit: examples/noxna_settings_example.cpp

## Metadata

- Source file: `examples/noxna_settings_example.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — the most purely backend-agnostic file in this batch:
  no `GraphicsDevice`/`Game`/window at all, gated only on `CNA_NOXNA`
  (`cmake/Examples.cmake:251-261`: `if(CNA_BUILD_EXAMPLES AND CNA_NOXNA)`), registered directly as
  `add_test(NAME NOXNA_Settings_Compile_Run COMMAND cna_example_noxna_settings)` with no
  `CNA_GRAPHICS_BACKEND` condition anywhere.
- File type: standalone `main()`-only compile/behavior smoke test (113 lines, no `Game` subclass).
- XNA/FNA relevance: **N/A** — exercises only `CNA::Graphics` (non-XNA) settings-bag classes;
  correctly excluded from the `Microsoft::Xna` namespace per project convention.
- Related production code: `include/CNA/Graphics/RenderPipelineSettings.hpp`/`.cpp`,
  `include/CNA/Graphics/PbrMaterial.hpp`/`.cpp` (both confirmed pure `#ifdef CNA_NOXNA` "settings
  bag" classes with no clamping/validation logic in their setters).

## Purpose

Per its own header comment, a "compile-time demonstration of CNA extended graphics API" that
"exercises the NOXNA settings API so the compiler verifies that all declarations compile
correctly" and "does not draw anything." Two sub-tests: `testRenderPipelineSettings()` (defaults +
full round-trip of all 10 properties) and `testPbrMaterial()` (defaults + partial round-trip of
`PbrMaterial`'s scalar factors). Falls back to a one-line "NOXNA is disabled" stub under
`#else`/non-`CNA_NOXNA` builds, so the executable always compiles and always exits 0 regardless of
the `CNA_NOXNA` build flag.

## Executive Verdict

**Mostly healthy** — `RenderPipelineSettings`'s coverage is complete and every asserted default/
round-tripped value was independently verified against the production header/`.cpp`. `PbrMaterial`'s
coverage has a real, confirmed gap: the file's own stated goal ("verifies that all declarations
compile correctly") is not fully met — 9 of `PbrMaterial`'s public members (5 texture setters, plus
`getAlbedoColor`/`setAlbedoColor`/`getEmissiveColor`/`setEmissiveColor`) are never referenced
anywhere in this file, so they are not even compile-checked by it, let alone behavior-checked
(F1). A secondary, production-header-level documentation defect was also found and is reported here
as a cross-file observation (F2) since it directly concerns the class this file exercises.

## Checklist Results

### Purpose
Correctly placed and scoped; the `#ifdef CNA_NOXNA … #else … #endif` structure means the file
compiles and passes trivially in non-NOXNA builds too, which is appropriate for a feature-gated
compile-check example.

### API / XNA / FNA parity
N/A — `CNA::Graphics::RenderPipelineSettings`/`PbrMaterial` are intentional NOXNA extensions, not
XNA API. Correctly kept out of `Microsoft::Xna::Framework`.

### Behavioral correctness
Every `RenderPipelineSettings` default and round-tripped value asserted in
`testRenderPipelineSettings()` was cross-checked against the production header's in-class default
member initializers (`RenderPipelineSettings.hpp:85-97`) and its trivial pass-through `.cpp`
getters/setters — all match exactly (`hdrEnabled_=false`, `exposure_=1.0f`, `gamma_=2.2f`,
`tonemappingMode_=TonemappingMode::None`, `bloomEnabled_=false`, `bloomIntensity_=1.0f`,
`ssaoEnabled_=false`, `renderQuality_=RenderQuality::Medium`, `shadowQuality_=ShadowQuality::Disabled`,
`shadowsEnabled_=false`).

Every `PbrMaterial` value asserted (or round-tripped) in `testPbrMaterial()` also matches the
production header's defaults exactly (5× `nullptr` texture slots, `metallicFactor_=0.0f`,
`roughnessFactor_=0.5f`, `normalScale_=1.0f`, `occlusionStrength_=1.0f`, `alphaBlend_=false`,
`alphaCutoff_=0.5f`) — but see F1 for the members never exercised at all.

### Logic
Both classes' setters are simple, unconditional field assignments with no clamping (confirmed by
reading `RenderPipelineSettings.cpp`/`PbrMaterial.cpp` in full) despite doc comments like "in
[0,1]" for `metallicFactor`/`roughnessFactor`/`occlusionStrength`/`alphaCutoff` — this is
consistent with both classes being explicitly documented as "a pure settings bag" with "no
rendering itself," so absence-of-clamping is a design choice, not a defect, and this example
correctly does not claim otherwise.

### C++ correctness
Straightforward `assert()`-based checks; no UB, no lifetime issues (both objects are stack-local,
no pointers stored beyond the function's own scope).

### Testing
See F1 (PbrMaterial coverage gap). `RenderPipelineSettings` coverage is complete: all 10 public
properties (`HDREnabled`, `Exposure`, `Gamma`, `TonemappingMode`, `BloomEnabled`, `BloomIntensity`,
`SSAOEnabled`, `RenderQuality`, `ShadowQuality`, `ShadowsEnabled`) have both a default check and a
round-trip check.

### Cross-file consistency
`RenderPipelineSettings.hpp`'s own doc comment states "Construct via
`GraphicsDevice::GetRenderPipelineSettings()` or standalone" — this audit confirmed via
`grep -rn "GetRenderPipelineSettings"` across `include/`/`src/` that no such method exists anywhere
in `GraphicsDevice.hpp`/`.cpp`, nor does any other production file reference either
`RenderPipelineSettings` or `PbrMaterial` outside their own declaration/definition/this example —
see F2.

## Detailed Findings

### F1 — 9 of `PbrMaterial`'s 22 public members are never referenced by this file, contradicting its own stated "verifies that all declarations compile correctly" purpose

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `testPbrMaterial()` (lines 60-93); compare against
  `include/CNA/Graphics/PbrMaterial.hpp`'s full public member list.
- Evidence: `testPbrMaterial()` calls `getAlbedoTexture/getNormalTexture/
  getMetallicRoughnessTexture/getAmbientOcclusionTexture/getEmissiveTexture` (getters only, never
  the corresponding 5 setters — `setAlbedoTexture`/`setNormalTexture`/
  `setMetallicRoughnessTexture`/`setAmbientOcclusionTexture`/`setEmissiveTexture` are not called
  anywhere in this file) and never calls `getAlbedoColor`/`setAlbedoColor`/`getEmissiveColor`/
  `setEmissiveColor` at all (neither the getter nor the setter, in either the defaults block or the
  round-trip block). That is 5 untested setters + 4 entirely-untested get/set pairs = 9 of
  `PbrMaterial`'s 22 public methods never invoked by this file.
- Why it matters: the file's own header comment states its purpose is that "the compiler verifies
  that all declarations compile correctly" — but 9 declarations are simply never referenced, so
  they receive zero compile-time (let alone behavioral) verification from this file. A signature
  regression in any of the 5 texture setters or the 2 color properties (e.g. an accidental parameter
  type change, or `setAlbedoColor` swapping R/B channels) would not be caught by this example at all.
  This is a straightforward, easy-to-close gap — the missing setters only need a `nullptr`/a stub
  `Texture2D*` and a `Color` value to exercise, no rendering required (`PbrMaterial` is a pure data
  bag with no backend dependency).
- FNA/XNA comparison: N/A — `PbrMaterial` is a NOXNA-only extension type, not XNA API, so the
  project's XNA-specific "every public method must have a test" rule does not strictly bind here,
  but the file's own stated purpose creates an equivalent self-imposed bar that it does not meet.
- Related files: `include/CNA/Graphics/PbrMaterial.hpp`/`.cpp` (both already read in full and
  confirmed to be simple, side-effect-free accessors — adding the missing calls would carry no
  behavioral risk).
- Suggested action (not implemented by this audit): add default/round-trip assertions for
  `getAlbedoColor`/`setAlbedoColor`, `getEmissiveColor`/`setEmissiveColor`, and exercise each of the
  5 texture setters (e.g. by pointing them at a `nullptr`-then-back-to-`nullptr` round trip, or by
  constructing a throwaway `Texture2D*` if a headless one is available in this file's build
  configuration).

## Cross-File Observations

### F2 — `RenderPipelineSettings.hpp`'s doc comment claims a `GraphicsDevice::GetRenderPipelineSettings()` construction path that does not exist anywhere in the codebase

- Severity: LOW
- Confidence: HIGH
- Category: documentation accuracy (production header, surfaced via this example's own usage)
- Location/symbol: `include/CNA/Graphics/RenderPipelineSettings.hpp:18`: "Construct via
  `GraphicsDevice::GetRenderPipelineSettings()` or standalone."
- Evidence: `grep -rn "GetRenderPipelineSettings" include/ src/` (including
  `GraphicsDevice.hpp`/`.cpp` specifically) returns zero matches anywhere in the codebase.
  `grep -rln "RenderPipelineSettings\|PbrMaterial" src/ include/` (excluding the classes' own
  declaration/definition files) also returns zero matches — i.e. neither class is consumed by any
  other production file today; both are currently orphaned settings bags, matching
  `PbrMaterial.hpp`'s own more honest disclosure ("Actual PBR rendering requires a matching
  `PbrEffect` (Task N11) that reads these values").
- Why it matters: this example file itself only ever constructs `RenderPipelineSettings` standalone
  (`RenderPipelineSettings s;`), which is consistent with the fact that the
  `GraphicsDevice`-integration half of the doc comment's claim does not exist yet — so a reader of
  just this example would have no reason to doubt the header's claim, but the header itself
  currently documents a construction path that a developer would be unable to actually use.
- FNA/XNA comparison: N/A — pure documentation-accuracy issue in a NOXNA-only header.
- Related files: `include/CNA/Graphics/RenderPipelineSettings.hpp`.
- Suggested action (not implemented by this audit): either add
  `GraphicsDevice::GetRenderPipelineSettings()` if that integration is still intended, or remove the
  claim from the doc comment until it exists.

## Missing or Weak Tests

- See F1 for the concrete `PbrMaterial` coverage gap.
- Neither sub-test exercises out-of-contract values (e.g. `setMetallicFactor(-1.0f)` or
  `setExposure(-5.0f)`) to confirm the deliberate absence of clamping — reasonable to omit given
  both classes are explicitly documented as unclamped pass-through settings bags, so this is noted
  as a design confirmation opportunity rather than a defect.

## Positive Findings

- `RenderPipelineSettings` coverage is complete and every single asserted value (10 defaults + 10
  round-trips) was independently verified against the current production header/`.cpp` — no drift
  found.
- The `#ifdef CNA_NOXNA`/`#else` structure is a clean, low-risk way to keep this example always
  buildable regardless of the `CNA_NOXNA` flag, and the CTest registration
  (`NOXNA_Settings_Compile_Run`) correctly requires no graphics backend or window at all — the
  cleanest-scoped file in this entire batch.

## Final Assessment

A small, well-targeted compile/behavior smoke test whose `RenderPipelineSettings` half is complete
and verified-accurate, but whose `PbrMaterial` half falls short of the file's own stated goal by
leaving 9 public members (5 setters + 2 full get/set pairs) completely unexercised — an easy,
low-risk fix given both classes are simple, side-effect-free data bags.
