# Audit: include/CNA/Graphics/PbrMaterial.hpp

## Metadata

- Source file: `include/CNA/Graphics/PbrMaterial.hpp`
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

Declares PbrMaterial: a glTF 2.0 metallic-roughness PBR material settings bag (texture slots + scalar factors) for the future NOXNA extended render layer.

## Executive Verdict

Needs attention — clean API, but 2 findings worth recording (no consumer yet; no range validation on documented-bounded setters).

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**No production consumer**: confirmed via grep that no backend or XNA-facing code reads any `PbrMaterial` accessor — the class's own doc comment honestly discloses this ("Actual PBR rendering requires a matching `PbrEffect` (Task N11) that reads these values"), so this is a disclosed, forward-looking scaffold, not a silent gap. **No range validation**: `setMetallicFactor`/`setRoughnessFactor`/`setOcclusionStrength`/`setAlphaCutoff` are documented as `[0,1]` but nothing clamps an out-of-range value — consistent with this project's own stated principle of not validating at non-system-boundary internal APIs, but worth noting since a future `PbrEffect` consumer would need to either trust the documented contract or clamp itself. Texture pointers are correctly documented as non-owning ("caller is responsible for lifetime management").

### Testing
No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Detailed Findings

**No production consumer**: confirmed via grep that no backend or XNA-facing code reads any `PbrMaterial` accessor — the class's own doc comment honestly discloses this ("Actual PBR rendering requires a matching `PbrEffect` (Task N11) that reads these values"), so this is a disclosed, forward-looking scaffold, not a silent gap. **No range validation**: `setMetallicFactor`/`setRoughnessFactor`/`setOcclusionStrength`/`setAlphaCutoff` are documented as `[0,1]` but nothing clamps an out-of-range value — consistent with this project's own stated principle of not validating at non-system-boundary internal APIs, but worth noting since a future `PbrEffect` consumer would need to either trust the documented contract or clamp itself. Texture pointers are correctly documented as non-owning ("caller is responsible for lifetime management").

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found anywhere under tests/ — the only exercise of this API is examples/noxna_settings_example.cpp, a manual-assert compile-time example (not integrated into the GTest suite), itself gated behind CNA_BUILD_EXAMPLES AND CNA_NOXNA.

## Positive Findings

Clean, minimal, correctly Doxygen-documented settings bag.

## Final Assessment

See findings above; this shard is an intentionally incomplete, opt-in (default-OFF), forward-looking settings
scaffold — its own doc comments honestly disclose that backend wiring is future work.
