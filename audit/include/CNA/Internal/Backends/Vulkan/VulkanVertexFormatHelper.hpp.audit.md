# Audit: include/CNA/Internal/Backends/Vulkan/VulkanVertexFormatHelper.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Vulkan/VulkanVertexFormatHelper.hpp`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: C++ header, header-only (63 lines)
- XNA/FNA relevance: `VertexElementFormat` -> `VkFormat`/byte-size mapping
- Graphics backend relevance: intended to support arbitrary/custom `VertexDeclaration` layouts on this backend
- Main related tests: `examples/vulkan_vertex_format_test.cpp`

## Purpose

Declares `VertexElementFormatToVk()` and `VertexElementFormatSize()` — a mapping table from XNA's generic
vertex-declaration format enum to Vulkan's own `VkFormat` and byte-size concepts.

## Executive Verdict

**Needs attention — the mapping logic itself is correct and complete, but this file's public API is dead code
in production, confirmed at the header level (2nd confirmed instance of this exact pattern, after Bgfx's own
`BgfxVertexFormatHelper.hpp`).**

## Checklist Results

### Systematic FNA parity gaps — confirmed dead code (2nd instance of a cross-backend pattern)
**Confirmed via exhaustive grep of the entire Vulkan backend directory: `VertexElementFormatToVk`/
`VertexElementFormatSize` are called nowhere in `VulkanGraphicsBackend.cpp`.** The real per-pipeline
`VkVertexInputAttributeDescription` arrays are all hardcoded per-stride/per-shader instead — exactly mirroring
Bgfx's own `MakeBgfxLayout()` hardcoded-stride dispatch (see that file's own already-recorded finding). Unlike
Bgfx's equivalent test, however, `examples/vulkan_vertex_format_test.cpp` directly unit-tests these two
functions against an explicit expected-value table for every `VertexElementFormat` enumerator — the mapping
logic itself is genuinely, directly verified correct, just never wired into the real dispatch path. See
`AUDIT_CROSS_CUTTING_FINDINGS.md` for the full cross-backend writeup of this now-2-instance pattern.

### API / XNA parity (the mapping logic itself, independent of whether it's called)
Every `VertexElementFormat` mapping is individually correct: `Color` -> `VK_FORMAT_R8G8B8A8_UNORM` (matching
packed-color convention), `Byte4`/`Short2`/`Short4` correctly distinguish normalized vs. non-normalized/int
variants, `VertexElementFormatSize()`'s byte counts match FNA's own `GetTypeSize()` values.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found in the mapping logic itself.

## Detailed Findings

Entire file is dead code in production (confirmed via grep), though — unlike Bgfx's equivalent — its own test
does directly and correctly verify its logic in isolation.

## Cross-File Observations

2nd confirmed instance (after Bgfx) of "a correct, well-mapped generic vertex-format helper header that is
never wired into the real backend dispatch" — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

None — the existing test directly and correctly exercises this file's own logic (a positive, unlike Bgfx's
equivalent test file).

## Positive Findings

The mapping logic itself is correct and complete, and — unlike Bgfx's parallel case — is genuinely,
directly unit-tested rather than merely inferred through an indirect `SetData` path.

## Final Assessment

One MEDIUM-equivalent finding (entire public API confirmed dead code in production, consistent with the
already-recorded Bgfx instance); the mapping logic itself, and its direct test coverage, are both correct.
