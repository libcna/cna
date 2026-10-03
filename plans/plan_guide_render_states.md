# Guide overlay rendering states

Task `GS-009h`, 2026-10-03. Status: ✅, qualified and committed locally.

## Evidence and correction

The signed-in SAMPLE-094 displays CNA's sign-in notification after the title Draw. The system
Guide's own SpriteBatch sets AlphaBlend, DepthStencilState.None, CullNone and LinearClamp.
GuideUi::draw previously returned without restoring them. The title's ground Model uses the
normal wrap sampler with UVs 0..32, so subsequent frames sampled a flat border instead of the
repeating original texture. An owned native capture measured zero changed ground pixels above
an eight-channel-value threshold when walking; the unchanged game itself translated correctly.

A private scope around GuideUi::draw now retains and restores the original blend, depth-stencil,
rasterizer and pixel sampler zero payloads. Assignment deliberately preserves reference identity;
copy construction would clone the state. Capture occurs before style/portrait/system UI work,
and restoration follows SpriteBatch::End. The separate drawGameDialogs entry point uses a
caller-owned begun batch and keeps its existing contract. No public API, assets or sample-side
state workaround is added.

## Qualification

Evidence: `/rv/tmp/samples/SAMPLE-094-CustomAvatarAnimation_4_0/evidence/qualification-20261003/`.
Existing GuideInput, GuideNotification, GuideUi and SystemGuide baseline: 34/34. Both new
GuideRenderState regressions fail before the correction, for a toast and an open Guide screen;
they check the inherited state after display and after dismissal plus original reference identity.
Final focused OPENGLES3 run: 130/130 (36 Guide tests and the unchanged 94 avatar checks).
Runs use the canonical SAMPLE-106 analysis tree, shared ccache, all cores and the owned private
GPU runner. Native and exact-gallery visible Chrome WEBGL2 SAMPLE-094 consumer gates pass all five custom
actions, walking, profile/random avatar, camera and Back. Walking ground change above eight channel
values is 0 → 66,823 pixels natively and 71,968 on final WEBGL2. Chrome reports 1280×720 WebGL2,
600 rAF, no exceptions/rejections/HTTP errors and live contexts 1→0 on Back. Exact gallery hashes
and desktop/mobile UI pass.

Pre-correction images and runtime results remain in `pre-guide-fix/`. This qualification does
not claim an Xbox run or certify other renderer families.
