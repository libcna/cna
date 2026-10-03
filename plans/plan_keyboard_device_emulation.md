# Keyboard device emulation

Owner authorization: 2026-09-28, two independent CNA opt-ins and orientation emulation in
SAMPLE-102. Task ID `INPUT-EMU-001`.

| Task | Status | Scope |
|---|---|---|
| INPUT-EMU-002 | ✅ | Owner authorized keyboard→GamePad on 2026-10-03 and approved the WASD/arrows/TFGH/KLJI layout. Shared process-wide off-by-default opt-in for player one, physical input merge, capabilities, Guide isolation/release barrier and effective-state packet numbers. Native unit/runtime gates and real SAMPLE-085 OPENGLES3/WEBGL2 qualification pass. |
| INPUT-EMU-001 | ✅ | Shared keyboard accelerometer service, ordinary sensor events/availability/lifecycle, per-window orientation requests through the normal graphics lifecycle, focused tests, real native/web qualification and English documentation. |

The public additions are CNAEXT-marked; no XNA member or default behavior is replaced.
See `docs/keyboard-device-emulation.md` for exact API, units, controls and lifecycle boundaries.
No .NET stub, Sharp Runtime change, renderer-specific sample path or C ABI addition is required.

## Qualification

Artifacts and reproducible helpers live in sibling cna-samples SAMPLE-102's stable root:
`/rv/tmp/samples/SAMPLE-102-Orientation_4_0/evidence/keyboard-emulation-20260928/`.
All builds use canonical sibling CNA/Sharp sources, OPENGLES3/WEBGL2, the shared ccache and all cores.
Runtime GPU tests run through `tools/platform/run_gpu_tests_private.sh`.

Current focused test result: runtime 43/43; sensor suites 91 passed and 4 physical-sensor-only skips
out of 95; input 83/83. Added regressions exercise ordinary event order/units, preconstructed
instances, diagonal/opposite/focus-neutral values, independent throttling/restart, reentrant
sensor disposal, polling/enumeration and the cross-thread session-close barrier. Orientation tests
exercise all three values, persistent/ambiguous input, default restriction, lock/pending unlock,
backbuffer dimensions/presentation parameters, exact event counts, late opt-in and disabling.
The unchanged default physical accelerometer/gyroscope and mouse-to-touch regressions also pass.
Real Release OPENGLES3 102 passes portrait 480×800, both 800×480 landscapes, real mouse Tap
locking, blocked Right followed by queued unlock, focus-loss rejection and normal close exit 0.
The selected original scenario #4 landscape diagnostic, native, WEBGL2 and actual gallery image
match pixel-for-pixel at 800×480. The desktop original does not qualify physical Phone rotation.
Plain-HTTP Chrome current-product/exact-gallery runs pass 719/717 game draws, three orientation
shapes, lock/pending unlock, no runtime/required-resource errors and Back cleanup (GL contexts
1→0). Their real viewport checks allow Chrome's physical fullscreen canvas to change size.
A separate public-API WEBGL2 client confirms support for an instance constructed before opt-in,
ordinary neutral/right/diagonal/released events and Back cleanup. Its test source/input fixtures
are retained in artifact scripts and are not deployed in the sample/gallery. Gallery desktop,
mobile, controls/navigation and 83 unique entries pass. No push or sample pruning is part of
this task. New code introduces no stub, Sharp Runtime edit or physical-device qualification claim.

## INPUT-EMU-002 qualification — 2026-10-03

Artifacts: `/rv/tmp/samples/SAMPLE-085-AvatarAnimationBlendingSample_4_0/evidence/keyboard-gamepad-20261003/`.
Reused test build: SAMPLE-106's `cna-native-opengles3-analysis/`, canonical sibling sources, Release,
static OPENGLES3, shared ccache and full parallelism. Baseline: input 528/528 and focused runtime
32/32. After the addition: input 560/560 and the same runtime selection plus the pump regression
33/33. All run through `tools/platform/run_gpu_tests_private.sh --exec`.

The 32 added input cases cover every button/axis, missing service and untouched other/invalid slots,
mode changes, focus loss, normalization/opposites, dead zones, physical merge and restored axes,
effective-state packet numbers, capabilities/physical extras and Guide dismissal. The new runtime
case checks Game's once-per-frame publication before Update, ordinary Keyboard visibility and the
neutral no-keyboard-service path. No Sharp Runtime, renderer or native C ABI change is required.
Real OPENGLES3/WEBGL2 qualification is tracked in sibling SAMPLE-085's `missing.md`.

Final SAMPLE-085 consumer qualification: original GamePad paths for all four presets, LB blending,
RB random appearance, right-stick orbit/reset, triggers and Back pass on native OPENGLES3 and on
the byte-identical gallery WEBGL2 copy in visible system Chrome over plain HTTP. The browser has
1280×720 WebGL2, 600 rAF callbacks, no exceptions/rejections/HTTP errors and context cleanup 1→0.
Pixel measurements establish real changing poses, text toggling and camera/avatar/zoom changes.
No physical controller was attached; these gates qualify the approved keyboard software source.
