# Audit: examples/easygl_texture_anisotropic_effect_test.cpp

## Metadata

- Source file: `examples/easygl_texture_anisotropic_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 299, `SamplerState.MaxAnisotropy` caps/fallback on
  a 3D stock effect (`DualTextureEffect`)
- File type: hand-rolled `Game`-subclass executable (not `PixelTestGame`-based), CTest-registered as
  `EasyGL_TextureAnisotropic_DualTextureEffect` (`cmake/Tests/EasyGLTests.cmake:1316-1318`). The
  identical source is also reused verbatim for `Vulkan_TextureAnisotropic_DualTextureEffect`
  (`cmake/Tests/VulkanTests.cmake:92-98`) — the `easygl_` filename prefix is a historical artifact,
  not a hard backend dependency; the file only calls public `Microsoft::Xna::Framework` API.
- XNA/FNA relevance: `SamplerState.MaxAnisotropy`, `TextureFilter::Anisotropic`, `DualTextureEffect`
  are all real XNA 4.0 API surface.
- Related production code: `EasyGLGraphicsBackend::ApplySamplerState` (case `2`, anisotropic branch,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:2055-2125`);
  `EasyGLTextureBackend`'s constructor (same file, lines 432-446).

## Purpose

Verifies that requesting an absurdly over-cap `SamplerState.MaxAnisotropy` (`9999`) on a real 3D
draw does not crash or throw — the test's own stated "primary, load-bearing assertion" — and prints
diagnostic `[INFO]` classification of the sampled pixel (solid black vs. a normal ~50/50 R/G blend)
without failing on either outcome.

## Executive Verdict

**Needs attention** — not because the test's actual pass/fail logic is wrong (it correctly checks
only for "does this crash," which it does correctly), but because roughly two-thirds of the file's
own header comment (lines 5-31) is now **stale, factually incorrect documentation** that describes a
codebase state which has since been fixed. This was verifiable by reading the current backend code
directly, not merely inferred.

## Checklist Results

### API / XNA / FNA parity
`SamplerState.MaxAnisotropy` (int, XNA-compatible), `TextureFilter::Anisotropic`,
`DualTextureEffect` — matches FNA's stock effect and sampler-state surface. No parity issues.

### Behavioral correctness
The test's real assertion (lines 96-136): construct a `SamplerState` with
`MaxAnisotropy=9999`, `Filter=Anisotropic`, apply a `DualTextureEffect`, draw a full-screen quad from
a 2-texel (Red|Green) `tex2_`, and confirm no exception escapes. This is correctly implemented —
`threw`/`result_` are set consistently, and `Exit()` is called unconditionally in both the `try` and
`catch` paths (line 161, after the `if`/`else` — reached in both cases since `catch` doesn't itself
return).

### Logic / Testing — stale header comment (see Finding F1)
The file's own header comment makes two falsifiable factual claims that this audit checked against
the current source and found **contradicted**:

1. Lines 8-12: *"EasyGL's `TextureFilter::Anisotropic` silently falls back to plain trilinear
   filtering with NO anisotropic effect at all (the underlying easy-gl library has no anisotropy
   support whatsoever)."* — **False today.** `EasyGLGraphicsBackend::ApplySamplerState` (lines
   2114-2125) genuinely calls `s.set_parameter(SamplerParameter::MaxAnisotropy, clamped)` gated on
   `metagl::HasExtension("GL_EXT_texture_filter_anisotropic")`, with `clamped` derived from a live
   `glGetFloatv(GetParameter::MaxTextureMaxAnisotropy, ...)` query — a real, working implementation,
   not a silent fallback.
2. Lines 20-31: *"binding [`tex2_`] with `TextureFilter::Anisotropic` renders **solid black** ...
   because EasyGL never sets `GL_TEXTURE_MAX_LEVEL` to match each texture's real (here: 1-level) mip
   count ... This test does NOT fail on the black result (that's Task 867's fix to make, not this
   test's job to paper over)."* — **Also false today.** `EasyGLTextureBackend`'s constructor
   (lines 439-444) already calls
   `texture.set_parameter(TextureTarget::Texture2D, TextureParameter::MaxLevel, mipLevels_ - 1)` with
   an explicit comment crediting "Task 924" for exactly this fix.

### FNA/XNA comparison
N/A for the crash-safety check itself; the stale claims above are about CNA-internal backend
behavior, not FNA parity.

## Detailed Findings

### F1 — Header comment documents a pre-fix backend state as if still current/open

- Severity: MEDIUM
- Confidence: HIGH (both claims independently re-checked against live production source, and
  against `plans/plan_graphics.md`'s own task-tracking rows)
- Category: maintainability / documentation-accuracy
- Location/symbol: file header comment, lines 5-31
- Evidence: `plans/plan_graphics.md` row 918 records **Task 918 (✅ CLOSED)** — "EasyGL has NO real
  anisotropic filtering support at all ... `TextureFilter::Anisotropic` silently falls back to plain
  trilinear filtering ... (Task 456 finding)" — the *exact* claim this test's comment still makes —
  fixed by adding real `GL_EXT_texture_filter_anisotropic` support. Row 924 records **Task 924
  (✅ CLOSED)** — "Fix EasyGL's mip-incomplete-texture black-screen bug: set `GL_TEXTURE_MAX_LEVEL`
  on every `Texture2D`..." (split from Task 867, the row this file's own comment cites as still
  open) — with its own regression test (`EasyGL_Texture2D_AnisotropicSingleLevel`) asserting the
  sampled color is *not* solid black for exactly this scenario (`Texture2D::CreateFromPixels`,
  `TextureFilter::Anisotropic`, no mip data uploaded).
- Why it matters: a maintainer reading this file today (its stated purpose is literally "verify
  anisotropic filtering caps and fallback," with the header explicitly presented as "Audit findings
  this task confirmed via code reading") would reasonably conclude EasyGL still has no anisotropic
  support and still black-screens non-mipmapped textures under this filter — both wrong. Because the
  test's pass/fail logic never actually asserts on the black-vs-blended distinction (it only prints
  an `[INFO]` line either way, correctly anticipating both outcomes at lines 148-157), this staleness
  is invisible to CI and would persist indefinitely without a source read like this one.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (real fixes, see
  above); `plans/plan_graphics.md` rows 918/924 (closure records).
- Suggested future action (not implemented by this audit): update the header comment to reflect that
  both Task 918 and Task 924 have landed; if desired, the `[INFO]`-only branches could be promoted to
  a real assertion now that the "solid black" outcome is a regression signal rather than an
  accepted/expected result.

### F2 — Test verifies "doesn't crash," not "the requested cap was actually honored/clamped"

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: lines 96-161 (the whole `try` block only ever inspects `threw` and one sampled
  pixel's blend-vs-black classification)
- Evidence: no assertion anywhere in this file reads back the actual clamped `MaxAnisotropy` value
  that reached the GPU sampler.
- Why it matters: this is explicitly acknowledged as out of scope by the file's own stated goal
  ("the primary, load-bearing assertion" is non-crash), and real value-level verification does exist
  elsewhere (`easygl_anisotropic_gl_state_test.cpp`, Task 918's own follow-up, per `plans/plan_graphics.md`
  row 918) — not a gap in overall coverage, just worth noting this specific file doesn't itself close
  the loop.

## Cross-File Observations

- Confirms (independently, via `cmake/Tests/VulkanTests.cmake:92-98`) that this exact source file is
  compiled and run as a Vulkan test too, with a code comment there noting "This is the one backend
  where MaxAnisotropy is actually respected" — consistent with Vulkan's device-capability-driven
  clamp being the "real" implementation and EasyGL's being the newer, extension-gated one.
- `TextureFilter::Anisotropic`'s GL mapping (`ApplySamplerState` case `2`: `minF =
  LinearMipmapLinear`, a genuinely mip-aware minification filter) is what makes the "requires a
  complete mip chain" observation in this file's comment correct in principle — that specific claim
  is *not* stale, only the "still renders black today" conclusion is, now that Task 924's
  `GL_TEXTURE_MAX_LEVEL` fix makes a 1-level texture's single-level "chain" trivially complete.

## Missing or Weak Tests

See F2. A direct assertion in *this* file that the sampled result is a normal blend (not black) once
Task 924 is confirmed permanently in place would also convert this into a real regression test for
that fix, rather than leaving it purely advisory.

## Positive Findings

- The actual crash-safety logic is correct and defensive: `try`/`catch(const std::exception&)`
  correctly distinguishes a real fault from either "expected" pixel outcome, and `Exit()`/`result_`
  are set unambiguously on every path.
- The file does not hard-fail on an ambiguous/environment-dependent outcome (black vs. blended) —
  a reasonable design given driver/extension availability varies, even though the surrounding prose
  explaining *why* that ambiguity exists is now outdated (F1).
- `RasterizerState::CullNone` is applied with an inline comment correctly attributing the need to a
  separate, already-tracked finding (Task 896 — CCW winding under CNA's real default rasterizer
  state), rather than silently working around it.

## Final Assessment

The test's pass/fail behavior is sound, but roughly half its header commentary is now factually
wrong relative to the current, already-fixed backend (Tasks 918 and 924 both closed after this file
was written) — a documentation-staleness defect that would mislead any future reader relying on this
file as a source of truth about EasyGL's anisotropic-filtering limitations.
