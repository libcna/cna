# Audit: include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: C++ header, header-only (136 lines)
- XNA/FNA relevance: `VertexElementFormat`/`VertexElementUsage` -> bgfx `VertexLayout`/`Attrib` mapping
- Graphics backend relevance: intended to support arbitrary/custom `VertexDeclaration` layouts on this backend
- Main related tests: `examples-tests-bgfx`'s `bgfx_vertex_format_test.cpp` (already audited this session)

## Purpose

Declares `VertexElementFormatToBgfx()`, `VertexElementUsageToBgfxAttrib()`, and `VertexElementFormatSize()` —
correct, complete mapping tables from XNA's generic vertex-declaration types to bgfx's own `VertexLayout`
concepts.

## Executive Verdict

**Needs attention — the mapping logic itself is correct and complete, but this entire file's public API is dead
code in production, confirmed at the header level (not just inferred from the test file).**

## Checklist Results

### Systematic FNA parity gaps — confirmed dead code
**F1 (MEDIUM, confirmed — see `AUDIT_CROSS_CUTTING_FINDINGS.md` and `bgfx_vertex_format_test.cpp`'s own already-
recorded finding):** `grep`ing `BgfxGraphicsBackend.cpp` (the only other file in this backend) for
`VertexElementFormatToBgfx`, `VertexElementUsageToBgfxAttrib`, or `VertexElementFormatSize` returns **zero
matches** — none of this file's three functions are called anywhere in production. The real vertex-layout
dispatch (`MakeBgfxLayout()`, confirmed via the already-completed `bgfx_vertex_format_test.cpp` audit) instead
switches on hardcoded byte-size (stride), never consulting this file's generic, per-element mapping logic at
all. This confirms, at the header level, that the earlier test-file-level finding ("the test's entire subject
may be dead code") is accurate: **this file's entire public API is unused in production.**

### API / XNA parity (the mapping logic itself, independent of whether it's called)
Every `VertexElementFormat`/`VertexElementUsage` mapping is individually correct: `Color` correctly maps to
normalized `Uint8`x4 (matching packed-color convention), `Byte4`/`Short2`/`Short4` correctly distinguish
normalized vs. non-normalized/int variants, and `VertexElementFormatSize()`'s byte counts are correctly
cross-referenced as matching FNA's own `GetTypeSize()` values and "identical to `VulkanVertexFormatHelper::
VertexElementFormatSize()`" (not independently re-verified against that file in this pass, but a specific,
checkable claim).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found in the mapping logic itself.

## Detailed Findings

**F1 (MEDIUM):** entire file is dead code in production — confirmed via grep, not just inferred.

## Cross-File Observations

Confirms and strengthens `bgfx_vertex_format_test.cpp`'s own already-recorded finding (that file's own
`UploadAndCheck()` never calling `SetData`, silently testing the same hardcoded stride-16 layout regardless of
which declaration is nominally under test) — this header-level check proves the underlying production functions
themselves are unreachable, not just under-tested.

## Missing or Weak Tests

The one test that exists for this file (`bgfx_vertex_format_test.cpp`) doesn't actually exercise its functions
either, per that file's own already-recorded audit finding — this file has effectively zero real test coverage
of any kind.

## Positive Findings

The mapping logic itself, if it were ever wired up, is correct and complete — a reasonable "ahead of need"
implementation that simply never got connected to the real dispatch path.

## Final Assessment

One MEDIUM finding (F1: entire public API is confirmed dead code in production); the mapping logic itself is
correct.
