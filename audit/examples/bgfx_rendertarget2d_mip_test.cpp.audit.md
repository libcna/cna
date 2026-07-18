# Audit: examples/bgfx_rendertarget2d_mip_test.cpp

## Metadata

- Source file: `examples/bgfx_rendertarget2d_mip_test.cpp` (194 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTarget2D` real mip-chain-generation pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_bgfx_rendertarget2d_mip`
  / `Bgfx_RenderTarget2D_MipChain`, `cmake/Tests/BgfxTests.cmake:491-494`)
- XNA/FNA relevance: direct — `RenderTarget2D(..., mipMap=true, ...)` and mip-aware texture sampling
  (`SamplerState` min/mag/mip filter) are real XNA behavior.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxRenderTargetBackend`'s constructor, lines 668-706), `bgfx::createFrameBuffer` (external,
  verified against the actual vendored bgfx source, see below).

## Purpose

Task 906: verifies `RenderTarget2D`'s mip chain is genuinely generated with real, correctly-downsampled
content on Bgfx (not merely present-but-undefined storage). A direct port of
`examples/vulkan_rendertarget2d_mip_test.cpp` (Task 878)'s methodology: render a 7:1 asymmetric red/blue
split into a `mipMap=true` RT, unbind it (triggering bgfx's own auto-mip-regeneration), then sample it
back two ways: (1) at native 1:1 size — must stay crisp (level 0 only, no minification); (2) scaled to a
1×1 destination (exactly 64:1 minification, `log2(64)=6`) — always samples the texture's exact centre
(deep inside the red region) but forces GPU LOD selection to the single coarsest mip level, whose content
must be the whole image's true 7:1 weighted average (~`(223,0,32)`), *not* pure red — proving the mip
levels were actually regenerated from real content, not left undefined/zeroed/duplicated-from-level-0.

## Executive Verdict

**Healthy.** Independently verified the file's most load-bearing technical claim — that bgfx's plain
`createFrameBuffer(num, TextureHandle*, bool)` overload (the one this backend's `BgfxRenderTargetBackend`
actually calls) defaults to `BGFX_RESOLVE_AUTO_GEN_MIPS` whenever the underlying texture was created with
`hasMips=true` — by reading the actual vendored bgfx source (`bgfx.cpp`'s `createFrameBuffer` overload),
not merely trusting the file's own comment. The claim is exactly correct, and the test's numeric
assertions independently check out.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D`'s 6-arg constructor call
(`RenderTarget2D(device, kRTSize, kRTSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None)`)
matches the real constructor signature exactly (`include/Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp:43-50`).

### Behavioral correctness — independent verification of the bgfx mip-regeneration claim
The file's header claims: *"bgfx::createTexture2D's hasMips=true plus the framebuffer-attachment default
resolve flag (BGFX_RESOLVE_AUTO_GEN_MIPS, confirmed in bgfx.cpp's createFrameBuffer) makes bgfx call the
platform's own mip-generation primitive... automatically, every time bgfx switches away from that
framebuffer."* This audit located the actual vendored bgfx source (via a sibling repository's fetched
build tree) and confirmed this precisely: `BgfxRenderTargetBackend`'s constructor
(`BgfxGraphicsBackend.cpp:689-705`) calls `bgfx::createFrameBuffer(numAttachments, attachments, true)` —
the `TextureHandle*`-array overload, **not** the `Attachment*` overload whose *declared* default
parameter is `BGFX_RESOLVE_AUTO_GEN_MIPS`. Critically, `bgfx.cpp`'s actual implementation of that
`TextureHandle*` overload (`createFrameBuffer(uint8_t, const TextureHandle*, bool)`) internally builds an
`Attachment` per handle with:
```cpp
at.init(_handles[ii], Access::Write, 0, 1, 0,
        !ref.hasMips() || ref.isDepth() ? BGFX_RESOLVE_NONE : BGFX_RESOLVE_AUTO_GEN_MIPS);
```
i.e. it *does* resolve to `BGFX_RESOLVE_AUTO_GEN_MIPS` exactly when the referenced texture was created
with `hasMips=true` (this backend's `mipMap` parameter) — confirming the file's claim is correct down to
the specific code path actually used, not just "some overload of createFrameBuffer somewhere defaults to
this."

### Logic — numeric assertion re-verification
Re-derived Check 2's expected value independently: 7 rows red `(255,0,0)`, 1 row blue `(0,0,255)` over 8
rows, weighted average = `(7·255/8, 0, 1·255/8) = (223.125, 0, 31.875)` ≈ `(223,0,32)` — matches the
test's own tolerance window (`R∈[190,240]`, `G≤20`, `B∈[15,55]`) comfortably, and is a real, exact
mathematical property of a correct box-filter mip chain over this specific pattern (not an arbitrarily
chosen tolerance).

### C++ correctness
`RenderIntoRTAndReadColumn()` constructs a **fresh** `RenderTarget2D` per call rather than reusing a
persistent instance across checkpoints — the file's own header explains why: reusing the *same*
already-rendered RT/texture across more than one independent `Clear+Draw+GetBackBufferData` cycle does
not reliably reproduce the first cycle's correct content on this backend (confirmed unrelated to the mip
feature itself, reproduces even with `mipMap=false`). This is a real, previously-discovered Bgfx quirk
(Task 406) that this file correctly works around using the same pattern established elsewhere in this
shard (e.g. `bgfx_rendertarget2d_msaa_test.cpp`'s `RenderAndReadRow`).

### Testing
Both checks assert real pixel values with justified tolerance windows, not placeholder "did not crash"
assertions — a strong, quantitatively-verified test.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM defects found — this file's technical claims about bgfx's own internal
mip-regeneration behavior were independently verified against the actual bgfx source and hold up exactly.

## Cross-File Observations

- **This file directly falsifies claims made by three sibling files in this same batch.**
  `bgfx_render_target_sample_test.cpp` and `bgfx_render_target_usage_test.cpp` both claim (or rely on the
  claim) that pixel-level verification of a `RenderTarget2D` sampled via `SpriteBatch` after unbinding is
  not possible on the Bgfx backend — this file does exactly that (`RenderIntoRTAndReadColumn`'s
  `sb_->Draw(rt, ...)` followed by `device.GetBackBufferData(...)`) and gets back numerically-correct
  content in both of its checks. See those two files' own audit reports for the stale-claim findings this
  cross-check substantiates.
- Correctly identifies and cites the specific prior methodology mistake it avoids ("an earlier 50/50-split
  version of this methodology was a false positive" — presumably because a symmetric split cannot
  distinguish "real mip average" from "any blend of the two halves", including a broken duplicate-of-
  level-0). The asymmetric 7:1 split is a materially better test design for exactly this reason.

## Missing or Weak Tests

None identified specific to this file's own scope.

## Positive Findings

- This audit independently verified the file's central technical claim about bgfx's internal
  `createFrameBuffer`/`BGFX_RESOLVE_AUTO_GEN_MIPS` behavior against the actual vendored bgfx source code
  (not just the file's own comment) and found it precisely accurate, including the subtlety that the
  *specific* overload used (`TextureHandle*`, not `Attachment*`) still resolves to the same auto-mip-gen
  default via its own internal `Attachment` construction.
- The asymmetric 7:1 split methodology is a genuinely stronger test design than a naive 50/50 split,
  and the file's header explains why, referencing a real prior false-positive.
- Correctly works around a known Bgfx readback quirk (Task 406) using the established fresh-RT-per-
  checkpoint pattern, rather than reusing a single instance and risking a false result.

## Final Assessment

A rigorous, independently-reproducible test whose most extraordinary claim (bgfx auto-regenerates real
mip content via a resolve-flag default, not a custom shader) was checked against the actual upstream
bgfx implementation and confirmed correct. This file also serves as direct, concrete counter-evidence to
stale "pixel verification isn't possible on Bgfx" claims made elsewhere in this same shard.
