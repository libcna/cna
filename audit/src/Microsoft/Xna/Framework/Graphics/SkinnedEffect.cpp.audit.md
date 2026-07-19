# Audit: src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`
- Audit status: AUDITED (full read, 525 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/SkinnedEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `SkinnedEffect`'s constructors, `Clone()`, `CacheEffectParameters()`, `OnApply()`,
`FillGpuDrawParams()`, `EnableDefaultLighting()`, bone-transform accessors, and every property.

## Executive Verdict
Rendering-path fidelity to FNA is excellent and independently confirms a previously-only-inferred
cross-cutting hypothesis (see Detailed Findings, Finding 1). However, four distinct validation
call sites use raw `std::` exceptions instead of this project's own `System::` exception types
(Finding 2), the established cross-cutting pattern flagged repeatedly elsewhere in this audit.

## Checklist Results
- **`EnableDefaultLighting()` (lines 222-241): numerically identical to FNA's
  `EffectHelpers.EnableDefaultLighting()`**, matching `BasicEffect`'s own already-verified values
  exactly.
- **`OnApply()` (lines 406-518): a complete, correct, line-for-line port of FNA's real
  `SkinnedEffect.OnApply()`** — `WorldViewProj`/fog-vector computation (`SetWorldViewProjAndFog`
  inlined), `WorldInverseTranspose`/eye-position (`SetLightingMatrices` inlined), material-color
  computation, the one-light shader-index optimization (`!DirectionalLight1.Enabled &&
  !DirectionalLight2.Enabled`), and the final shader-index formula
  (`weightsPerVertex==2→+2, ==4→+4`; `preferPerPixelLighting→+12, else oneLight→+6`) all match
  FNA's real `SkinnedEffect.cs` exactly, term-for-term.
- `SetBoneTransforms()`/`GetBoneTransforms()` correctly restore `M44 = 1` when reading back bones
  (matching FNA's own "Convert matrices from 43 to 44 format" comment/behavior).

## Detailed Findings

### Positive, independently significant — confirms `SkinnedEffect::FillGpuDrawParams()`
pre-folds ambient into `emissiveColor`, never touches `ambientColor`
Lines 335-338:
```cpp
p.emissiveColor[0] = (emissiveColor_.X + ambientLightColor_.X * diffuseColor_.X) * alpha_;
p.emissiveColor[1] = (emissiveColor_.Y + ambientLightColor_.Y * diffuseColor_.Y) * alpha_;
p.emissiveColor[2] = (emissiveColor_.Z + ambientLightColor_.Z * diffuseColor_.Z) * alpha_;
```
This is byte-for-byte the same formula as FNA's real `EffectHelpers.SetMaterialColor()`'s
lighting-enabled branch (`emissive.X = (emissiveColor.X + ambientLightColor.X * diffuseColor.X) *
alpha`). `p.ambientColor` (the separate `GpuDrawParams` field several GPU backends' skinned
shaders were found, in prior audit work, to never read) is never written anywhere in this file.
This is direct, first-hand confirmation — not inference — of a hypothesis
`audit/AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Systematic FNA parity gaps" section had previously
reached only indirectly (via cross-backend comment archaeology across EasyGL/Bgfx/SdlGpu/D3D9):
**the C++-side `FillGpuDrawParams()` computes the correct, FNA-faithful value and intentionally
routes it through `emissiveColor`, not `ambientColor`** — so any backend whose skinned shader reads
`ambientColor` for the skinned draw path (Vulkan, per prior audit work) or has no `emissiveColor`
uniform slot for skinned draws at all (D3D11/D3D12, per prior audit work) is doing the misconsuming,
not this upstream C++ code. This resolves that cross-cutting finding's open question in favor of
"2 backends misconsume an already-correct upstream value," not "the upstream value itself is
wrong."

### MEDIUM — four validation-throw call sites use raw `std::` exceptions instead of this
project's own `System::` exception types
- `setLightingEnabledProperty(false)` (line 215): `throw std::runtime_error(...)`. FNA's real
  `IEffectLights.LightingEnabled` setter throws `NotSupportedException` for this exact case
  (`"SkinnedEffect does not support setting LightingEnabled to false."`). Should be
  `System::NotSupportedException`.
- `SetBoneTransforms()` (lines 296-297, two throw sites): `throw std::invalid_argument(...)` for
  both the empty-array case and the exceeds-`MaxBones` case. FNA's real `SetBoneTransforms` throws
  `ArgumentNullException` for the former and `ArgumentException` for the latter — two distinct
  exception types, both currently collapsed into one wrong C++ type here. Should be
  `System::ArgumentNullException`/`System::ArgumentException` respectively.
- `GetBoneTransforms(int)` (line 307): `throw std::out_of_range(...)`. FNA's real
  `GetBoneTransforms` throws `ArgumentOutOfRangeException("count")`. Should be
  `System::ArgumentOutOfRangeException`.
- `setWeightsPerVertexProperty()` (line 289): `throw std::out_of_range(...)`. FNA's real
  `WeightsPerVertex` setter throws `ArgumentOutOfRangeException("value")`. Should be
  `System::ArgumentOutOfRangeException`.

**Why it matters**: a caller written against this project's established convention (catching
`System::ArgumentException`/`System::NotSupportedException`/etc., as e.g. this session's own
`xna-net` shard audits confirmed callers correctly do elsewhere) cannot catch these four specific
validation failures by their expected sharp-runtime type — only by the unrelated `std::exception`
base, losing type-specific handling. This is the same recurring cross-cutting pattern flagged
across multiple other files/shards in this audit (most recently `PropertyDictionary` in
`xna-gamerservices`, 9 methods in one file); here it recurs across 4 methods in one file.

## Cross-File Observations
The same `std::runtime_error`-for-`NotSupportedException` pattern recurs identically in
`EnvironmentMapEffect.cpp`, `PbrEffect.cpp`, and `SkinnedPbrEffect.cpp` (all audited in this same
batch) — all four `IEffectLights`-implementing effects that disallow disabling lighting share the
exact same exception-type miss, suggesting a single common authoring habit rather than four
independent oversights.

## Missing or Weak Tests
A test asserting `setLightingEnabledProperty(false)` throws specifically
`System::NotSupportedException` (not just "throws something") would have caught this; not
independently located in this pass.

## Positive Findings
The `OnApply()`/`EnableDefaultLighting()`/`FillGpuDrawParams()` rendering path is an exceptionally
faithful, verified-correct port of FNA's real `SkinnedEffect`, and directly resolves a standing
cross-cutting open question in this project's own audit trail.

## Final Assessment
One MEDIUM finding: four raw-`std::`-exception validation throws should use this project's own
`System::` exception types. One significant positive/confirmatory finding regarding the
`ambientColor`/`emissiveColor` cross-cutting hypothesis.
