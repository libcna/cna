# Audit: src/CNA/Internal/Xnb/StockEffectContentTypeReaders.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/StockEffectContentTypeReaders.cpp`
- Audit status: AUDITED (full read, 165 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's 5 stock-effect readers' field order exactly
- Main related tests: not independently located in this pass

## Purpose
Implements all 5 stock-effect readers' `Read()` methods and their registration.

## Executive Verdict
Healthy -- field read order independently checked against known FNA `EffectReader` binary layouts for all
5 effect types and found correct.

## Checklist Results

### FNA parity: field order verified correct for all 5 readers
- `BasicEffectReader`: texture, diffuseColor, emissiveColor, specularColor, specularPower, alpha,
  vertexColorEnabled.
- `AlphaTestEffectReader`: texture, alphaFunction (`CompareFunction`), referenceAlpha (uint), diffuseColor,
  alpha, vertexColorEnabled.
- `DualTextureEffectReader`: texture, texture2, diffuseColor, alpha, vertexColorEnabled.
- `EnvironmentMapEffectReader`: texture, environmentMap, environmentMapAmount, environmentMapSpecular,
  fresnelFactor, diffuseColor, emissiveColor, alpha.
- `SkinnedEffectReader`: texture, weightsPerVertex, diffuseColor, emissiveColor, specularColor,
  specularPower, alpha.

All 5 match FNA's real per-effect binary layout.

### Optional-texture handling: consistent and correct
Every texture/environment-map field is read via `ReadExternalReference<T>()` and only assigned
(`SetOwnedTexture()`/etc.) if it resolved to a real value -- correctly handles the common "effect has no
texture" case without a null-reference issue.

## Detailed Findings
None.

## Cross-File Observations
Reuses `RequireGraphicsDevice()`'s pattern consistently across all 5 readers (parameterized by reader name
for clear error messages) rather than duplicating the null-check 5 times with divergent wording.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct field order across all 5 distinct effect binary layouts -- independently verified, not merely
assumed given the file's overall care.

## Final Assessment
No issues found.
