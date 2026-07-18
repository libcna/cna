# Audit: examples/easygl_surface_format_throws_test.cpp

## Metadata

- Source file: `examples/easygl_surface_format_throws_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test (no pixel readback; construction/
  exception behavior only)
- File type: C++ executable test (`Game` subclass, no gtest), 152 lines
- XNA/FNA relevance: exercises `Texture2D`/`Texture3D`/`TextureCube` constructor validation against
  `Microsoft::Xna::Framework::Graphics::SurfaceFormat`
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SurfaceFormat.cs` (enum definition)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/Texture.cpp:109-116` (`Texture::ValidateFormat`,
  shared by all three texture types), called from `Texture2D.cpp:166`, `Texture3D.cpp:68`, `TextureCube.cpp:66,83`
- Naming note: despite the `easygl_` prefix, this file contains **no EasyGL-specific code** — it only exercises
  CPU-side/shared validation logic in `Texture.cpp`. Confirmed via `cmake/Tests/EasyGLTests.cmake:882-885`,
  `cmake/Tests/VulkanTests.cmake:737`, and `cmake/Tests/BgfxTests.cmake:139` that this exact source file is
  compiled and registered as a test three times, once per backend (EasyGL, Vulkan, Bgfx) — a deliberate,
  legitimate shared-test-source reuse pattern, not a misnamed or misplaced file.

## Purpose

Task 176: verifies that constructing a `Texture2D`/`Texture3D`/`TextureCube` with any `SurfaceFormat` other than
`SurfaceFormat::Color` throws `std::runtime_error`, while `SurfaceFormat::Color` itself never throws — guarding
against the historical failure mode of "silently falling back to RGBA8/Color for an unsupported format" (per the
file's own header comment).

## Executive Verdict

**Healthy.** `Texture::ValidateFormat`'s actual logic (`if (fmt==Color) return; else throw`) was confirmed to be a
simple, uniform binary check with no per-format branching, so this test's own stated scope ("representative
unsupported formats," not exhaustive) is fully adequate to prove the implementation correct — there is no
per-format code path this representative sample could fail to exercise, since none exists.

## Checklist Results

### API / XNA / FNA parity
Cross-checked `SurfaceFormat`'s full enum listing (`include/Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp`)
against FNA's `SurfaceFormat.cs` line-by-line: identical ordering and identical member set, including the
FNA-specific `...EXT`-suffixed extensions (`ColorBgraEXT`, `ColorSrgbEXT`, `Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`,
`ByteEXT`, `UShortEXT`) that FNA itself documents as beyond the strict XNA4 spec but ships as real, named
members — this test's use of `ColorSrgbEXT`/`Bc7EXT`/`Bc7SrgbEXT`/`Dxt5SrgbEXT`/`ColorBgraEXT` (lines 88-107) is
therefore testing real, correctly-named FNA-compatible enum values, not CNA inventions.
Constructor signatures used (`Texture2D(dev,w,h,mipMap,format)`, `Texture3D(dev,w,h,d,mipMap,format)`,
`TextureCube(dev,size,mipMap,format)`) were verified against `Texture2D.hpp:64-65`, `Texture3D.hpp:33`,
`TextureCube.hpp:35` — all match exactly.

### Behavioral correctness
Traced `Texture::ValidateFormat` (`Texture.cpp:109-116`) directly:
```cpp
void Texture::ValidateFormat(SurfaceFormat fmt)
{
    if (fmt == SurfaceFormat::Color) return;
    throw std::runtime_error(...);
}
```
Confirmed all three constructors call this shared helper before accepting a format: `Texture2D.cpp:166`,
`Texture3D.cpp:68`, `TextureCube.cpp:66` (the 4-arg public ctor) and `TextureCube.cpp:83` (a second internal ctor,
per that file's own comment referencing a prior Task 774 finding where this same validation had been
accidentally skipped for one constructor — now present). Since the check is a flat binary comparison with no
per-value branch, every one of this test's 11 `expectThrows` cases (`ColorSrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`,
`Dxt5SrgbEXT`, `ColorBgraEXT`, `HalfVector4`, `HdrBlendable`, `Dxt1`, `Alpha8`, `Bgr565`, `Vector4`) and 3
`expectNoThrow` cases (`Color` for all three texture types) are logically redundant with each other from a
pure code-coverage standpoint — but each still validates the constructor call site independently, which is a
reasonable and correct test design given `Texture2D`/`Texture3D`/`TextureCube` each have their own separate
constructor definitions, all delegating to the same shared validator.

### Logic
`expectThrows`/`expectNoThrow` (lines 31-64) are small, correct helper templates (`auto fn` C++20 abbreviated
template parameter) that catch `std::runtime_error` specifically for the "must throw" case and any
`std::exception` more broadly for the "must not throw" case, correctly distinguishing "threw the wrong exception
type" (line 44-48, `[FAIL] ... wrong exception type`) from "threw nothing" (line 36) and "threw when it
shouldn't" (line 59-62) — a well-structured three-way outcome check, not a blunt try/catch.

### Robustness
The `Texture2D`/`Texture3D`/`TextureCube` objects constructed inside each lambda (lines 76-129) are local to the
lambda and destructed immediately after each `fn()` call returns (or throws) — no resource leak across the 14
construction attempts, including the 11 that throw mid-construction (each throwing constructor's own partial
state, if any, is a local variable never escaping the lambda).

### Testing
Confirmed this file's own `SurfaceFormatThrowsTest::Draw` is an empty override (line 66, `void Draw(const
GameTime&) override {}`) — all assertions run inside `Initialize()` (lines 69-134), with `Exit()` called at the
end of `Initialize()` itself. This is a valid, if slightly unusual, pattern for this test family (most sibling
tests in this shard defer assertions to `Draw()`); confirmed it does not skip a frame/present cycle that any of
these particular assertions depend on, since none of them need a rendered frame — all are pure construction/
exception checks with `GetBackBufferData` not used anywhere in this file.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Representative (not exhaustive) format coverage — correctly justified by the validator's actual simplicity, but worth noting for future maintenance

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `SurfaceFormatThrowsTest::Initialize` (lines 69-134); untested enum members: `Bgra5551`,
  `Bgra4444`, `Dxt3`, `NormalizedByte2`, `NormalizedByte4`, `Rgba1010102`, `Rg32`, `Rgba64`, `Single`,
  `HalfSingle`, `HalfVector2`, `ByteEXT`, `UShortEXT`
- Evidence: `Texture::ValidateFormat` has exactly one branch (`== Color`) with no per-format special-casing, so
  none of the untested enum values above can behave differently from the tested ones under the *current*
  implementation. This was confirmed by direct code reading, not assumed.
- Why it matters: this is a forward-looking maintenance note, not a present defect — **if** `ValidateFormat` is
  ever extended to support additional formats individually (e.g. adding real `Dxt1`/`Dxt3`/`Dxt5` support, which
  would require removing `Dxt1` from this test's own `expectThrows` list), the remaining untested enum values
  would not automatically gain coverage and a future partial-implementation bug (e.g. supporting `Dxt1` but not
  `Dxt3`) could go unnoticed unless this test is revisited at that time.
- FNA/XNA comparison: N/A — this is purely about CNA's own validator completeness relative to its own test.
- Suggested action (not implemented by this audit): no action needed today; flagged so that any future work
  expanding `ValidateFormat` beyond the single `Color`-only check also revisits this test's format list.

## Cross-File Observations

- This file (registered in `EasyGLTests.cmake`, `VulkanTests.cmake`, and `BgfxTests.cmake`) and
  `easygl_texture2d_partial_rect_test.cpp` (audited in this same batch, also registered in all three of those
  files) share the identical multi-backend reuse pattern; `easygl_texture2d_mip_test.cpp` (also in this batch) is
  registered in `EasyGLTests.cmake` and `VulkanTests.cmake` only (not Bgfx). This is a consistent, deliberate
  "author once under an `easygl_` name, reuse as a backend-agnostic regression test" convention across this
  shard, not file-specific duplication or a naming inconsistency worth flagging as a defect.

## Missing or Weak Tests

- See F1 — the untested subset of `SurfaceFormat` values is currently safe given the validator's simplicity, but
  is worth tracking if that validator is ever extended.
- No test exercises the *message content* of the thrown `std::runtime_error` (only its type) — a minor omission
  given the message is purely diagnostic text, not load-bearing behavior.

## Positive Findings

- `Texture::ValidateFormat`'s single shared implementation, confirmed called from all three texture types'
  constructors (including a second internal `TextureCube` constructor that a prior task, per its own source
  comment, had found missing this exact call), is a clean, DRY validation pattern with no per-type divergence risk.
- Test helper design (`expectThrows`/`expectNoThrow`) correctly distinguishes three distinct failure modes (wrong
  exception type / no exception / unexpected exception) rather than a blunt boolean pass/fail.
- Confirmed the `SurfaceFormat` enum itself is a faithful, order-and-value-identical port of FNA's own enum,
  including its `EXT`-suffixed extensions — a genuine parity check, not assumed.

## Final Assessment

A correctly-scoped, well-implemented validation test. Its "representative, not exhaustive" format sampling is
justified by the production validator's actual (and confirmed) simplicity — a flat "only `Color` is allowed"
check with no per-format logic for the sample to miss. The shared-source reuse across three backend CMake
registrations is a deliberate, legitimate pattern, not a misfiled EasyGL-only test.
