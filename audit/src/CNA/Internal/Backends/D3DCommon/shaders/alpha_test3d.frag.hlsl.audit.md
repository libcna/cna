# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: AlphaTestEffect fragment stage (alpha compare/discard + fog mix)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: AlphaTestEffect.fx
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Samples the texture, applies the AlphaFunction/ReferenceAlpha test (equality-with-tolerance or less-than encoding, matching EasyGL's own established convention), discards on failure, mixes toward FogColor by the incoming FogFactor.

## Executive Verdict

**Healthy — correct alpha-test logic; correctly consumes the (upstream-buggy) FogFactor without adding its own fog-formula error.**

## Checklist Results

### API / XNA parity
Alpha-test encoding (`AlphaTol > 0` → equality-with-tolerance; else → less-than, weight-based discard) is a faithful re-implementation of `AlphaTestEffect`'s compare-function semantics, consistent with the equivalent convention already verified elsewhere in this audit (EasyGL).

### Logic
Fog mix (`lerp(FogColor, outColor.rgb, input.FogFactor)`, line 55) is itself correct — this fragment shader has no independent fog-formula defect; the mirrored-formula bug lives entirely in the *vertex* shader's `FogFactor` computation (`alpha_test3d.vert.hlsl`), which this file just consumes as given.

## Detailed Findings

None in this file specifically — see `alpha_test3d.vert.hlsl.audit.md` for the upstream fog-formula defect this file's `FogFactor` input inherits.

## Cross-File Observations

Every other alpha-test/textured/colored D3DCommon fragment shader follows the identical "lerp toward FogColor by FogFactor" pattern with no independent defects — the fog bug is consistently isolated to the vertex stage across this entire directory.

## Missing or Weak Tests

No dedicated discard-branch test found for this specific shader (as opposed to AlphaTestEffect's C++-level coverage elsewhere).

## Positive Findings

Clean separation of concerns: this file trusts the vertex stage's FogFactor without re-deriving it, so a future fix to the vertex-shader formula requires no change here.

## Final Assessment

No defects found in this file; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
