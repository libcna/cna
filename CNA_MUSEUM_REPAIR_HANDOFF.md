# Museum-discovered CNA repairs

## Scope and evidence

Owner-requested intake on 2026-10-22. Permanent external inventory:
`../museum-simulator/docs/CNA_FINDINGS.md`, section **Open**. The museum owns that
inventory and moves entries to **Fixed in CNA** only after rebuilding and rerunning
its reproducer. Do not edit the museum or its CNA worktree as part of these repairs.
Do not invent CNA-DEMOS issue IDs for museum-only findings.

Intake CNA: `28046c8bbc48bea787007881e21e9b3ab71bcfd6`, branch `next`.
Museum reference CNA: `7b028ebe8`, an ancestor 36 commits behind. No intervening
committed changes touch `modules/net` or `Sdl3Tray.cpp`.
The shared worktree contains unrelated content/audio ownership work and the
uncommitted CNA-DEMOS-ISSUE-113 occlusion-query candidate. Preserve all of it.
The latter still needs its uint32-result/XNA compatibility review before closure;
its last external-v4 run preserved 132 outcomes (including eight known ISSUE-082
failures) and passed six exact visual references. It is not a completed repair.

## Current task

**2026-10-22 MUSEUM-WEBGPU-001: FIXED_BROWSER_AND_NATIVE_VALIDATED**, awaiting museum revalidation.
This repair follows CNA `93baa9363` on `next`; its commit contains this checkpoint.
The existing museum browser artifacts are not modified. CNA had no configured browser
tree, so `cmake-build-wasm-webgpu` is now its persistent Emscripten WEBGPU variant,
reusing the existing `.sdl-prebuilt-emscripten` and shared ccache. Configure succeeded.
Baseline build30563 completed0 in1025714ms, no source drift, peak529199104/events0.
Baseline browser72828 reproduced the exact rgba16unorm/tier1 JavaScript TypeError in
both runs (adapter feature available/hidden), with no reached success summary. This
was a successful reproduction of failure, not a passing product test. Peak436781056.
Candidate requests tier1 in browsers, keeps native TextureFormat16bitNorm, and gates
all six16-bit normalized probe formats before creating a texture. Exact renderer
factories refuse unsupported formats with NotSupportedException. The public XNA
preferred-format fallback is preserved, as required by shipped RenderTarget2D's
GraphicsAdapter.QueryFormat call and existing RenderTargetFormatAgreement tests.
Fixture now checks that distinction explicitly rather than requiring the public
preferred-format constructor to throw. Its original baseline never reached those
assertions because the capability probe already terminated the page.
After build56744:0/48084ms/peak401289216. Browser18939:10/10 PASS available and10/10 PASS
with feature hidden, actual device feature enabled/disabled respectively. Complete
capability report, exact2D/cube factories, public preferred-format selection and all16
readback pixels checked; no JavaScript/uncaptured GPU errors. Peak422395904/events0,
no source/artifact drift. Actual Chrome/private Vulkan runs, not Node-only Wasm runs.
Reports are demos `build/linux-full/reports/repair-museum-webgpu-{before,after}-*`.
Reusable regression driver added as `scripts/run-webgpu-optional-format-test.mjs`
(Node22+, existing browser build and optional report directory as arguments). Linux
must use the private GPU runner. No museum executable/reproducer run is claimed.

Native job47658 completed0: existing multi-renderer library/graphics/renderer targets
built in656309ms; native fixture9PASS; WebGPU renderer suite67=66PASS/1SKIP; format and
capability checks28PASS on each of WEBGPU/OPENGLES3/SOFTWARE. The57 previously recorded
WebGPU effect cases have identical outcomes, including the Reach volume-texture skip;
the10 reflection cases also pass. All28 format/capability outcomes match the previous
full native aggregate. No full aggregate or museum browser tour rerun is claimed.
Native peak2134278144 bytes, no memory events or source drift. Seventeen warnings are
in unchanged HEAD test sources (two MorphTargetEXT nodiscard calls and fifteen FNA3D
enum comparisons); none in changed source. Browser builds before/after have the same
six existing warnings (32-bit size_t constant truncation and unhandled draw enum cases).
Reports: demos `build/linux-full/reports/repair-museum-webgpu-native-*`.
The committed browser driver itself also ran successfully (job10085):10/10 available,
10/10 absent; no JavaScript/GPU errors or artifact drift. Its retained reports are in
`build/linux-full/reports/repair-museum-webgpu-portable/`.
Optional-format repair commit: `b6fc8e441`, local `next`, not pushed.

**MUSEUM-WEBGPU-002: FIXED_BROWSER_AND_NATIVE_VALIDATED**, awaiting museum revalidation.
This repair follows `b6fc8e441` on local `next`; its commit contains this checkpoint.
Before browser fixture:7checks/3FAIL,
three destroyed-canvas-texture submission errors. Before native fixture:SIGABRT from
wgpuSurfaceConfigure, `SurfaceOutput must be dropped before a new Surface is made`.
Reports: demos `build/linux-full/reports/repair-museum-canvas-before-*` and
`repair-museum-canvas-native-before-*`; source/artifact drift empty, memory events0.
The repair uses cached owned browser backbuffer storage and only acquires/copies/submits
the canvas at Present. Surface reconfiguration first drops native acquired output.
Expanded14-check fixture covers partial-draw preservation, resize and MSAA as well.
First candidate browser build65662 passed in44370ms, peak402931712, no events/drift.
Browser job62634 was the expected failing baseline; job48287 the native abort baseline.
Intermediate browser71049 passed14/14. Source review then corrected Present's copy to
AcquireFrameEncoderEXT, not BeginReplayEXT (the latter may map occlusion results/yield),
and added a pending-query frame. Final build28474 passed0/45551ms/peak397864960/events0.
Final browser27103 passed16/16 and exact screenshot pixels2880/2880; no JS/GPU errors or
artifact drift. Actual MSAA4 was applied. Screenshot is under demos reports
`repair-museum-canvas-final-browser/`, viewed and independently pixel-checked.
Native23052 completed0: build64863ms, fixture16PASS, existing clear/readback10PASS,
MSAA7PASS, RenderTarget2D13PASS, cube18PASS, occlusion3PASS. WebGPU suite66PASS/1SKIP and
format/capability28PASS each on WEBGPU/GLES/SOFTWARE. No build/fixture compiler diagnostics;
peak1478066176/events0/source drift0. Only private-header comments changed afterwards.
Browser regression build86381 completed0/280539ms/peak398274560/events0/no drift.
Seven existing warnings repeat the same two known messages (32-bit hash truncation,
unhandled draw enum cases); the coverage page adds one more inclusion of that header.
Regression job60945 passed: final canvas16/16 plus **all3840/3840 screenshot pixels**,
optional formats10/10 enabled and10/10 disabled, browser coverage9/9, EnvironmentMap4/4,
Skinned9/9, DualTexture4/4, AlphaTest4/4. No JS/uncaptured GPU errors or artifact drift;
peak479354880/events0. Native67 renderer and28 format cases per renderer have exact
outcome parity with the preceding optional-format checkpoint. Reports:
`build/linux-full/reports/repair-museum-canvas-regression-*` in demos.
No heavy job remains. No museum tour, full native aggregate, compiled browser Effects,
Firefox, native Windows/macOS or sanitizer run is claimed for this GPU-lifetime batch.
GPU validation and exact pixels cover the actual invalid-texture defect.
Current SDK header exposes TextureFormatsTier1, and Chrome's primary feature reference
identifies rgba16unorm as tier1, not the tier2 claimed by CNA's stale comments:
https://developer.chrome.com/blog/new-in-webgpu-142 . Native's extension remains distinct.
The pre-fix browser RequestDeviceOnlyEXT omitted tier1 and ProbeRenderTargetFormatEXT
created RGBA16Unorm without a feature guard. Canvas lifetime is a separate root cause:
frame encoding survives async capability/shader waits, not just backbuffer readback.
The two repairs remain separate commits and separate regression fixtures.

MUSEUM-NET-001: FIXED_NATIVE_VALIDATED (`cd9868526`), awaiting museum revalidation. Migration
now excludes the departed host's whole shared NetworkMachine. Client peer maps
cannot do this correctly because the star topology relays other machines through
the same upstream connection. No new retries or timing changes in production.
Before repair: two minimized real-ENet cases FAIL (both target DepartingGuest);
all 538 pre-existing network tests PASS. After repair: all 541 network tests PASS,
including both minimized cases and the new three-process/two-host-local case,
which proves promotion, rediscovery/reconnect and a post-migration payload echo.
The original one-local-host multiprocess case remains unchanged and passes.
The first expanded harness attempt failed because it read ACKs only from local
gamer zero; the actual Host identity can be the other local gamer. The harness now
reads both host inboxes and still requires two distinct survivors' exact ACKs.
Its pre-existing duplicate delete on the timeout path was also removed.
Final build 5558 ms, full net 45846 ms; peak 642084864 bytes, no memory events,
no source drift or build diagnostics. No full aggregate/GPU/sanitizer claim for
this networking-only change. Museum `tools/net_check.sh` was not run here.
Reports: `../cna-demos/build/linux-full/reports/repair-museum-net-before-*`,
`repair-museum-net-migration-after-*` (retained harness failure),
`repair-museum-net-migration-v2-after-*` (accepted).
Runner: `../cna-demos/build-probe/repair-museum-net.mjs`.

MUSEUM-NET-002: FIXED_NATIVE_VALIDATED (`04fb4978a`), awaiting museum revalidation. Public Join
in two real processes reproduced Lobby/events=0 before repair. The welcome now
carries Playing in an optional tagged suffix after guest flags; Lobby's exact old
bytes and the strict online-relay envelope stay unchanged. An old welcome defaults
to Lobby. Unknown suffixes remain ignored; truncated/invalid state is rejected.
The normal owner-thread state event is pumped before native EndJoin returns.
Shipped XNA `NetworkSession.GameStarted.add` invokes each new handler when Playing
(reference file under `../xna4-decomp/reference/xna4/decompiled/windows/`); CNA's
existing replay hook therefore delivers exactly one immediate notification after
Join, with no second notification on the next Update. The museum text's statement
that XNA does not replay is contradicted by that accessor; museum confirmation is
still required. No genuine Xbox/live-service reference run is claimed.
Six new tests PASS (codec, real ENet state/event timing and public two-process Join).
All 547 native network tests PASS; all previous 541 outcomes unchanged. Build
130821 ms, focused13 2103 ms, full net46543 ms, peak1182662656 bytes, no memory
events or source drift. The rebuild reports31 existing nodiscard warnings in the
unchanged NetworkSessionTests.cpp/NetworkSessionTypePolicyTests.cpp; no diagnostic
in a changed source. Reports: `repair-museum-net-welcome-before-*` (expected1FAIL),
`repair-museum-net-welcome-after-*` (accepted), under the report directory above.

MUSEUM-TRAY-001: FIXED_NATIVE_VALIDATED (`93baa9363`), awaiting museum revalidation. Sdl3Tray now
owns a heap-stable string per entry; all allocations precede native insertion,
replacement keeps the old label alive through the native update, and label storage
outlives SDL_DestroyTray. No vendor changes or public API changes.
Before: the actual Sdl3Tray TU against a retaining-SDL seam gives3FAIL/1PASS.
After:4PASS, including short/long labels, replacement,128 entries with vector
growth, callbacks and failed insertion. Dedicated CTest CnaTrayLabelLifetimeTests
passes and keeps the fake SDL exports out of every other test executable.
Real SDL on private Wayland and private D-Bus also passes:42 exact menu labels
after caller storage expired,42 after replacement, two dbusmenu callbacks and
clean exit. This uses the committed tools/platform/tray_dbus_harness.cpp and
tray_dbus_check.py, not the museum executable or the owner's panel/bus.
Platform suite530 =522PASS/8SKIP, exact case-by-case parity with the prior full
aggregate checkpoint repair-occlusion-native-full.xml. ASan/UBSan/float-cast/leaks
on the actual adapter TU:4PASS, no findings. All five platform boundary gates PASS
(inventory, classify, renderer audit, ratchet, hot-path lint).
Normal build23448ms, D-Bus522ms, platform8612ms, CTest3924ms;
sanitizer build21536ms/test37ms. Peaks635338752/538132480 bytes; no memory events
or source drift. Reports: `repair-museum-tray-before-*`, `-after-*`, `-sanitizer-*`.
Private portal processes log shutdown warnings when their private bus ends; the
tray harness itself exits0 and all assertions pass. No native Windows/macOS claim.

Reproduce the real integration check from CNA (no live desktop):
`tools/platform/run_gpu_tests_private.sh --exec dbus-run-session -- /usr/bin/python3 tools/platform/tray_dbus_check.py cmake-build-debug/cna_tray_dbus_harness`.

Next priority: ENet's signed unsequenced-window shifts under UBSan. Draft deterministic
C fixture `../cna-demos/build-probe/enet_unsequenced_window_test.c` calls the actual
protocol handler and real queue/receive path, covering every bit, duplicates and window
rollover. Not compiled/run yet. Promote to a CNA-native test and capture the sanitizer
failure before the two-literal vendor repair. Then notification/mixer lifetime races.

Resource discipline: one heavy job and one compiler worker, shared ccache
`/rv/cnaccache` with base `/rv`, existing `cmake-build-debug`, MemoryHigh 4 GiB,
MemoryMax 6 GiB, no swap, private display and network namespace. No push or merge.

## Intake queue (not closure claims)

All entries below require reproduction/contract verification, except where an
explicit current result is recorded above. Source headings, not this shorthand,
identify the permanent museum evidence. Additional entries in the current file
take precedence over the shorter request summary.

1. Blocking: MUSEUM-NET-001 host migration; MUSEUM-NET-002 join in progress;
   tray entry-label lifetime abort; browser WebGPU tier-1 texture feature gating;
   browser WebGPU destroyed canvas texture submission.
2. Safety: ENet signed unsequenced-window shifts; notification Close/listener race;
   mixer callback race; pending-pan static teardown race; FNA3D SDL_GPU deferred
   sampler crash; post-sign-out profile race; Android half-float capability/readback;
   missing notification ErrorOccurred; DEF_PROP private/protected access leakage.
3. Runtime/graphics: frustum outside-ray clipping; WebGPU PBR lighting;
   SDL_GPU PBR no-tangent declarations; SDL_GPU volume shader layout/crash;
   WebGPU/SDL_GPU vertex texture fetch; HapticDevice incomplete-type constructor
   (overlaps CNA-DEMOS-ISSUE-009); demangled vertex-type diagnostics;
   tray support reporting; message-box default buttons; EnvironmentMapEffect texture
   reflection; render-target readback capability suggestion; surface-free WebGPU.
4. Storage: async End-once and owner checks, size/name/path argument types,
   directory IO exception mapping; SetAppNameEXT eager root resolution.
5. Pipeline: generated nested-name scope; TextureProcessor alias result type;
   Convert exceptions; AddDependency policy/exceptions; EffectMaterial validation;
   ReadRawObject<Int32>; truncated XNB wrapping; intentionally accepted XNB v4;
   Color/Rectangle XML reflection; zero bone weights; scanner changed-list result;
   serializer-attribute equality; embedded glTF images deployment; WAV comments;
   TaskItem equality; compiler-task library linkage; clean lock-file lifetime.
6. C surface: duplicate process CNA state; uncompressed CNB declared-size validation;
   MouseState/GamePadState ToString; achievement missing-key result;
   PresentationParameters defaults; devices.h desktop/Emulator wording.

Suggestions and documented intentional differences are not automatically defects.

Read-only preparation while the browser baseline compiles (no additional runtime claim):
`HttpNotificationChannel::Listen` reads the plain `listenSocket_` concurrently with
Close's replacement. Capturing the listener handle is necessary, but preserve its
ownership until the listener exits (closing before join also permits descriptor reuse).
The listener can also block in ReadRequestBody on an accepted connection; merely
shutting down the listening socket does not wake that recv. Cover partial requests and
Close before calling this lifecycle repaired. Header uses int for SOCKET and socket
failure tests use `< 0`; inspect Windows handle width/INVALID_SOCKET in the same audit.
`SetMixerTrackMixCallback` writes callback/userdata before the locking SDL setter, while
MixCallback reads them. The stopped-callback pair has the analogous pattern. SDL_mixer
offers MIX_LockMixer; existing CNA MixerLock is already used as a callback barrier by
PendingPanStateCleanup::DrainAfterMixerBarrier, but its destructor calls DrainClaimed
without that barrier. Audio files carry inherited ownership work: do not overwrite it.

The museum's already-fixed missing-asset diagnostic and disposed StorageContainer
entries remain historical evidence, not new repairs. No platform/service runtime
coverage is inferred from source review.
