# Audit: include/CNA/Internal/Graphics/ImageData.hpp

## Metadata

- Source file: `include/CNA/Internal/Graphics/ImageData.hpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: internal implementation detail behind XNA-facing content/text APIs
- Graphics backend relevance: see purpose
- Main related tests: see Missing or Weak Tests

## Purpose

Declares ImageData: a minimal width/height/RGBA8-pixels/mipLevels struct used as the internal image-decode result type.

## Executive Verdict

Needs attention — 1 minor, latent (not actively triggered) code-quality finding.

## Checklist Results

### Behavioral correctness / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**`width`/`height` have no default member initializers, unlike `pixels`/`mipLevels`** (which default to an empty vector / `1` respectively) — an inconsistency within the same struct. Confirmed this is NOT an active bug in this codebase's only production consumer (`ImageLoader.cpp`'s `surfaceToImageData()` always explicitly sets both fields on every return path before use), but it's a latent risk for any other caller that might default-construct `ImageData{}` directly and read `width`/`height` before setting them — reading uninitialized memory. Low severity given no such caller currently exists.

### Testing
Not independently located in this pass.

## Detailed Findings

**`width`/`height` have no default member initializers, unlike `pixels`/`mipLevels`** (which default to an empty vector / `1` respectively) — an inconsistency within the same struct. Confirmed this is NOT an active bug in this codebase's only production consumer (`ImageLoader.cpp`'s `surfaceToImageData()` always explicitly sets both fields on every return path before use), but it's a latent risk for any other caller that might default-construct `ImageData{}` directly and read `width`/`height` before setting them — reading uninitialized memory. Low severity given no such caller currently exists.

## Cross-File Observations

None.

## Missing or Weak Tests

Not independently located in this pass.

## Positive Findings

Clean, correct implementation.

## Final Assessment

See findings above.
