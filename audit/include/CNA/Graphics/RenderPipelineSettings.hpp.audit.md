# Audit: include/CNA/Graphics/RenderPipelineSettings.hpp

## Metadata

- Source file: `include/CNA/Graphics/RenderPipelineSettings.hpp`
- Audit status: AUDITED
- Subsystem: `cna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: N/A — this entire shard is `CNA::Graphics` (not `Microsoft::Xna`), a NOXNA extended
  render-pipeline layer gated behind the `CNA_NOXNA` CMake option (`option(CNA_NOXNA "Enable CNA NOXNA
  extended graphics layer (beyond XNA 4.0)" OFF)`, default OFF)
- Graphics backend relevance: none yet — confirmed via grep that no backend or any other production file reads
  any of this shard's settings to affect actual rendering
- Main related tests: none (see Missing or Weak Tests)

## Purpose

Declares RenderPipelineSettings: an HDR/tonemapping/bloom/SSAO/shadow-quality settings bag for the future NOXNA extended render pipeline.

## Executive Verdict

Needs attention — 1 confirmed documentation-rot finding; otherwise clean, honestly-scoped scaffold.

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed documentation-rot**: the class doc comment states "Construct via `GraphicsDevice::GetRenderPipelineSettings()` or standalone" — **`GraphicsDevice::GetRenderPipelineSettings()` does not exist anywhere in the codebase**, confirmed via exhaustive grep (zero matches outside this one doc-comment reference). Either this method was planned and never implemented, or was removed without updating this comment — either way, a reader following this comment's own construction guidance would find no such method. **No production consumer**: same as `PbrMaterial`, honestly disclosed in-comment ("once backend support is implemented for each feature") — zero backends currently read `isHDREnabled()`/`getTonemappingMode()`/`isBloomEnabled()`/etc. to affect any actual render pass.

### Testing
No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Detailed Findings

**Confirmed documentation-rot**: the class doc comment states "Construct via `GraphicsDevice::GetRenderPipelineSettings()` or standalone" — **`GraphicsDevice::GetRenderPipelineSettings()` does not exist anywhere in the codebase**, confirmed via exhaustive grep (zero matches outside this one doc-comment reference). Either this method was planned and never implemented, or was removed without updating this comment — either way, a reader following this comment's own construction guidance would find no such method. **No production consumer**: same as `PbrMaterial`, honestly disclosed in-comment ("once backend support is implemented for each feature") — zero backends currently read `isHDREnabled()`/`getTonemappingMode()`/`isBloomEnabled()`/etc. to affect any actual render pass.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Positive Findings

The settings themselves are sensibly named/defaulted/documented (e.g. gamma defaults to 2.2, the real standard display gamma) even though nothing consumes them yet.

## Final Assessment

See findings above; this shard is an intentionally incomplete, opt-in (default-OFF), forward-looking settings
scaffold — its own doc comments honestly disclose that backend wiring is future work.
