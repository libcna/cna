# Standard avatar coordinate and attachment contract

Task `GS-009g`, 2026-10-03. Status: ✅, qualified and committed locally.

## Evidence and fix

The unchanged Microsoft SAMPLE-085 uses `World = RotationY(Pi)` and a camera on +Z; its
official preview shows the face. CNA initially drew the back because its frozen original-art
catalog is authored facing +Z. The standard XNA avatar space faces -Z (left on -X). The source
and preview in `/rv/tmp/XNAGameStudio/Samples/AvatarAnimationBlendingSample_4_0/` establish this
consumer convention without pretending that the Xbox-only program ran in this workspace.

A half-turn about Y converts `AvatarAnimation.BoneTransforms` and `AvatarRenderer.BindPose`
to the public XNA space. `AvatarRenderer` converts supplied local bones back to asset space,
skins the unmodified catalog, and applies the half-turn before the caller's `World`. Thus
the entire chain, including externally attached objects, shares one space. This is a proper
rotation, with determinant +1; no winding, model, material, description, ABI or catalog change.
The editor and art-review viewer orient their own presentation for that standard API.

The attachment check also exposed a second defect: `AvatarAnimation` put each catalog bind
offset in the animation matrix, though the renderer separately applied BindPose. Microsoft's
SAMPLE-101 `BonesToWorldSpace` explicitly multiplies `animationPose * bindPose * parentWorld`.
The old public poses therefore counted those offsets twice for attachments. Preset matrices
now contain animation deltas: rotation and root translation, zero non-root translation.
The renderer's existing XNA rule of taking non-root offsets from BindPose is preserved.
Caller-supplied custom animations still use the normal standard Draw overload.

## Qualification

Evidence: `/rv/tmp/samples/SAMPLE-085-AvatarAnimationBlendingSample_4_0/evidence/avatar-space-20261003/`.
Reused canonical Release/static OPENGLES3 analysis build in SAMPLE-106, shared ccache and all cores;
test execution on private displays through `tools/platform/run_gpu_tests_private.sh --exec`.
Baseline 91/91; final 94/94 for AvatarAnimation, AvatarRenderer, AvatarCatalog, AvatarEditor and
GuideAvatarEditor. Added checks cover all 31 presets and all 71 bones, translations and rotations,
public BindPose, and complete attachment composition at two times on both body types with a
translated/rotated caller World. The art-motion tests now compose animation deltas with BindPose,
matching actual rendering. Frozen manifests, assembled v1 model digests, assets and description
tests retain their original expected values and pass.

Native SAMPLE-085 visibly faces forward with its unchanged original World/camera; the numeric
250 ms blend check passes 377 assertions. Final native and exact-gallery WEBGL2 gates pass all four presets, blending toggle, new avatar,
camera orbit/reset, zoom and Back. Visible Chrome over plain HTTP reports 1280×720 WebGL2,
600 rAF callbacks, no runtime/HTTP errors and live GL contexts 1→0 on exit. This does not qualify original Xbox appearance, timing or an Xbox reference run.
