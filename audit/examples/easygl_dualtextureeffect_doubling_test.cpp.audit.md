# Audit: examples/easygl_dualtextureeffect_doubling_test.cpp

## Metadata

- Source file: `examples/easygl_dualtextureeffect_doubling_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DualTextureEffect_Doubling`
  (`cmake/Tests/EasyGLTests.cmake:1144-1146`, `cna_test_easygl_dualtextureeffect_doubling`)
- Related production code: `EasyGLGraphicsBackend::EnsureDualTextured3DProgram()`
  (`EasyGLGraphicsBackend.cpp:3009-3070`), specifically the `base.rgb *= 2.0;` line (3051).
- XNA/FNA relevance: FNA `DualTextureEffect.fx` `PSDualTexture`'s `color.rgb *= 2;` — a real,
  load-bearing part of the stock effect's pixel formula, not an incidental detail.
- Main related tests: this file's own header comment states it is the origin of a genuine 3-backend
  bug fix (EasyGL/Vulkan/Bgfx all silently missing the doubling factor before this test existed);
  reused/superseded in scope by `easygl_dualtextureeffect_combined_test.cpp`'s capstone scene.

## Purpose

`DualTextureDoublingTest` specifically isolates and verifies FNA's `color.rgb *= 2` doubling factor
on `Texture` (slot 0) — the one behavior every earlier `DualTextureEffect` test in the project
(explicitly enumerated in this file's own comment as "Tasks 133/135/191/293/294/296/297") could not
detect, because they all used pure 0/1-saturated texture colors where a missing `×2` is invisible
after clamping. Correct placement/naming/registration.

## Executive Verdict

**Healthy — and notable as genuine regression-catching infrastructure.** This is one of the strongest
files in this batch: its own header comment documents a real, cross-backend bug it found and that was
fixed as a direct consequence of writing this test (not a hypothetical "this *would* catch a bug" —
an actual "this *did* catch a bug" claim), and the sub-case design explicitly separates a
discriminating case (a) from a non-discriminating regression check (b), with the file honestly
labeling case (b) as "not discriminating" in its own comment rather than overstating its coverage.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty`/`setTexture2Property`/`setDiffuseColorProperty` — correct XNA-property style.

### Behavioral correctness
Verified case (a) by hand: `tex0=gray(100,100,100)`→`base=100/255=0.3922`; `base.rgb*=2→0.7843`;
`×tex1(white=1)×diffuse(default white,alpha1)=0.7843`→`200/255` (8-bit `≈200`) — matches the test's
expected `Color(200,200,200,255)` (line 131-132) and the file's own hand-derivation in its header
comment (line 21). The "buggy, no-doubling" hypothesis would instead read `100/255→100` — a 100-unit
separation on a 20-unit tolerance, giving this case very strong discriminating power (5× the
tolerance). Case (b) (`tex0=white,tex1=white,diffuse=red`) is correctly and explicitly documented as
non-discriminating (both the correct-doubling and no-doubling hypotheses saturate identically to
`(255,0,0)`), included only as a plain regression/sanity check per the comment (lines 24-27) —
verified this is an accurate self-assessment, not an overclaim.

### Logic
`colourMatch`/`closeTo` with `tol=20` (line 54-61) — appropriate given case (a)'s 100-unit separation
comfortably clears this tolerance in either direction.

### Memory/resource lifetime
`texWhite`/`texGray` are stack-local, non-owning-pointer-referenced for the duration of each
`{ }` sub-block — correct, consistent with the rest of this batch.

### C++ correctness
Correctly includes `<cstdlib>` for `std::abs`.

### Performance / Thread safety / Portability
N/A — single-frame test, no platform-specific code.

### Architecture
Correct XNA-only public API usage.

### Maintainability
Exceptionally clear, evidence-based header comment: states the FNA formula verbatim, explains exactly
why prior tests missed the bug (0/1 saturation invisibility), and is explicit about which sub-case
does/doesn't discriminate. This is close to a model example of the kind of self-documenting test
rationale the project's other files in this batch also aspire to.

### Robustness
N/A (test file).

### Testing
This file provides the doubling-factor coverage that is a prerequisite for every other
`DualTextureEffect` test's numeric derivations in this batch (`_alpha_test.cpp`, `_combined_test.cpp`,
`_fog_test.cpp`, `_null_texture0/2_test.cpp` all use a "gray cancels doubling" or "white/doubling"
technique that depends on this exact factor being correctly implemented) — this file is effectively
the foundational proof the rest of the suite's math relies on.

### Cross-file consistency
Directly consistent with the real shader (`EasyGLGraphicsBackend.cpp:3051`,
`base.rgb*=2.0;`) and with FNA's `PSDualTexture` (`color.rgb *= 2;`). The header comment's claim that
this was a genuine 3-backend bug (EasyGL/Vulkan/Bgfx) fixed as a result of writing this test is
corroborated by the presence of the identical `base.rgb*=2.0;` line and its explanatory "Task 383"
comment context in `EnsureDualTextured3DProgram()`/`EnsureDualTexturedColored3DProgram()` — both
carry matching doubling-factor code, consistent with a coordinated fix.

## Detailed Findings

No HIGH/MEDIUM/LOW findings — this file is a clean, accurate, well-scoped regression test with no
identified defect.

## Cross-File Observations

- This file is the historical/methodological anchor for the "gray texture cancels doubling" technique
  reused by `_alpha_test.cpp`, `_combined_test.cpp`, and `_fog_test.cpp` in this same batch.

## Missing or Weak Tests

None for its stated scope. (Whether the doubling factor is *correctly scoped* to RGB-only, not alpha,
per FNA's `color.rgb *= 2` — as opposed to `color *= 2` — is not directly tested by an alpha-channel
assertion here, but `colourMatch` only checks RGB anyway, so this isn't a coverage gap this file
claims to close; the alpha-only-untouched behavior is implicitly relied upon by
`_alpha_test.cpp`'s independent alpha-premultiplication test instead.)

## Positive Findings

- Honest, explicit self-assessment of which sub-case discriminates and which doesn't — a good example
  other test files in this project could be measured against.
- Directly documents a real historical 3-backend bug it caused to be fixed, with concrete before/after
  numbers.

## Final Assessment

One of the strongest files in this batch: a precisely-targeted, evidence-based regression test with a
documented history of catching a real cross-backend defect, honest about the limits of its own
sub-cases, and foundational to the rest of the `DualTextureEffect` test suite's numeric assumptions.
