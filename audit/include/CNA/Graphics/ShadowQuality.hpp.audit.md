# Audit: include/CNA/Graphics/ShadowQuality.hpp

## Metadata

- Source file: `include/CNA/Graphics/ShadowQuality.hpp`
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

Declares ShadowQuality: a Disabled/Low/Medium/High/Ultra shadow-map quality preset enum, with each level's doc comment specifying its intended shadow-map resolution and PCF filter size.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Minimal, clean enum; each level's Doxygen comment gives a concrete, specific intended implementation detail (512x512 no filter -> 4096x4096 PCF 5x5) even though no backend yet implements shadow mapping at all — a reasonable, specific design target for future work, not vague.

### Testing
No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Detailed Findings

Minimal, clean enum; each level's Doxygen comment gives a concrete, specific intended implementation detail (512x512 no filter -> 4096x4096 PCF 5x5) even though no backend yet implements shadow mapping at all — a reasonable, specific design target for future work, not vague.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Positive Findings

Clean, minimal, correctly Doxygen-documented settings bag.

## Final Assessment

See findings above; this shard is an intentionally incomplete, opt-in (default-OFF), forward-looking settings
scaffold — its own doc comments honestly disclose that backend wiring is future work.
