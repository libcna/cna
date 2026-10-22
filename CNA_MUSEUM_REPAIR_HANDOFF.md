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

Mixer publication repair committed as `844e3ce41eff298e3cc17011706b3de87e06a13f`,
local next, not pushed. **MUSEUM-AUDIO-002 FIXED_NATIVE_SANITIZER_VALIDATED:** pending-pan static
teardown. The SDL3 mixer has no process-exit hook; its selected audio device may
still run when SoundEffect's static cleanup drains queued pan states. CnaMixer's
separate engine already registers a first-use exit hook to close its device before
preexisting statics. A lock in the pan destructor alone does not stop future enqueue.
Baseline55798 completed: active and recreated mixer cases fail deterministically
because generation/stopped callbacks show no shutdown before static cleanup;
explicit-close control passes. This reproduces ordering, not the museum's actual
TSan delete/read trace. Candidate registers DestroyMixer on first initialization,
stopping/joining the device and retiring all tracks before earlier statics destruct.
After53523: all three cases PASS plain and TSan, peak351772672/events0/no drift.
Final job83680 completed, scope `cna-museum-mixer-exit-final`, drivers demos
`build-probe/repair-museum-mixer-exit.mjs final` then `repair-museum-mixer-exit-native.mjs final`.
Instruments actual AudioMixer.cpp
and SoundEffect.cpp (preserving inherited ownership work), with a public/native
fixture checking mixer generation invalidation and stopped callbacks before static
cleanup, real long/short fire-and-forget pan sounds, first-callback synchronization,
and active/recreated/explicitly-closed mixer cases. No arbitrary sleep. Permanent
fixture is `tools/audio/mixer_exit_order_test.cpp`, three CTests. Final job adds
ASan/UBSan/leaks: all three cases PASS under each of plain/TSan/ASan+UBSan+LSan,
peak454717440/events0/no findings or source drift. Native rebuild13488ms; seven
publication/exit CTests PASS2930ms; audio775=771PASS/4unchangedSKIP8736ms,
media355/355PASS12239ms; both exactly match prior publication checkpoint by case.
MinGW actual AudioMixer TU compile1428ms; peak837533696/events0/no drift. Five
boundary gates PASS. No native Windows/macOS runtime claim.
Consumer job19835 completed, scope `cna-museum-mixer-exit-external`, existing
11-demo SOFTWARE/GLES consumer driver with suffix `exit-final`:all44 executions PASS,
no status/check/knownIssue/assertion delta or source drift. Rebuild59330ms,
peak3862011904/events0. No heavy job remains. SoundEffect.cpp has not been modified
by this repair; its inherited ownership work remains uncommitted. No full aggregate,
museum run, native Windows/macOS audio runtime or physical listening claim.
Next investigation: FNA3D SDL_GPU deferred sampler lifetime. Read-only source evidence:
CNA caches raw native textures in boundPixelTextures_/boundVertexTextures_, but its
resource destructors do not invalidate them; Fna3dCompiledEffect::ApplyNativeSampler
can reuse these caches. FNA3D FreeTextureHandle also releases the SDL texture without
clearing native sampler arrays. Neither observation yet proves the museum crash's
exact cause. Need a focused reproducer before selecting the repair layer.
Dependency source is `/home/robertvokac/deps/FNA3D/src/FNA3D_Driver_SDL.c` (not
the old _SDLGPU.c spelling); configured by cmake-build-multi. Its AGENTS.md/CLAUDE.md
explicitly prohibit AI-authored code contributions and allow issue identification.
Do not edit FNA3D/MojoShader sources; investigate CNA ownership and report any
dependency-only repair requiring a human author. No vendor changes made.

Phone repair committed as `20fe6efd26661ef1f19651d4c1eeeeac30ae8cee`, local next,
not pushed. Completed batch **MUSEUM-AUDIO-001 FIXED_NATIVE_TSAN_VALIDATED**,
callback publication in the SDL3
mixer adapter. Pending-pan static teardown remains a separate open root cause.
Baseline fixture calls actual adapter TU and real SDL: while mixer is locked,
control updates prematurely replace callback/userdata; deterministic mix and stopped
checks both fail (first0/second2). First TSan attempt caught a test-fixture lock-order
mistake (resource creation while holding mixer); moved setup outside the lock.
Before-v3 real audio-thread stress then reproduces the museum's exact data race,
MixCallback:245 read vs SetMixerTrackMixCallback:559 write, exit66. No suppression.
Fix publishes both tuples under the owning native mixer lock and makes explicit
Stop follow mixer->track order, preserving callback-side replacement without inversion.
Permanent fixture `tools/audio/mixer_callback_publication_test.cpp`, four CTests:
mix, stopped, fixed-workload real audio stress, reentrant stopped-callback replacement.
Final76516: all four PASS plain and TSan, peak346636288/events0/no drift.
Native20876: build13767ms, four CTests PASS2465ms, full audio8692ms/media12245ms,
MinGW actual adapter compile1131ms. Peak1170313216/events0/no drift. Existing
audio outcomes775=771PASS/4unchangedSKIP and media355/355PASS match the previous
full native checkpoint exactly, case by case.
Five platform boundary gates PASS. No native Windows/macOS audio runtime claim;
real SDL audio thread uses dummy output, not a physical speaker/listening check.
Consumer job90674 completed successfully, scope `cna-museum-mixer-external-final`,
driver demos `build-probe/repair-museum-mixer-external.mjs final`:11 demos on SOFTWARE
and GLES before/after rebuild, including standalone C sound-effect client. All44
executions PASS with identical status/check count/knownIssue outcomes; no assertion
or source drift. Rebuild59025ms, peak4109045760 bytes/events0. Reports
`repair-museum-mixer-external-final-*`. No full CNA aggregate or museum runtime
claim. Preserve inherited audio ownership changes in SoundEffect.cpp. No push.
No heavy job remains. Next: reproduce process-exit device/callback shutdown ordering
behind the pending-pan static teardown race, using the existing CnaMixer exit hook
as design evidence; do not just add a lock to a destructor while callbacks can
still enqueue after that destructor returns.

Latest checkpoint: **MUSEUM-PHONE-001 FIXED_LINUX_TSAN_AND_WINE_VALIDATED**.
Repair follows5b6aab981 on local next; its commit contains this checkpoint.
Native Windows/macOS and museum repro validation remain unclaimed. The
external job35138 completed successfully: both phone demos preserve all outcomes
(9+2 checks) on SOFTWARE and OPENGLES3. Rebuild142706ms, peak4305600512 bytes;
memory.high2358, max/oom/oom_kill0, no source drift. Reports
`repair-museum-phone-external-*`. This replaces the historical "current job" below.
Windows control `repair-museum-winsock-control.log` measured shutdown=-1/10057 and
accept still waiting; closesocket then releases it. An initial event-wait listener
candidate passed complete100 and idle2000 under Wine, but partial body still hung
in blocking recv (bounded45s test termination, report `phone-windows-after-*`).
Candidate now uses cancellable Windows FD_READ/FD_CLOSE waits as well, preserves
socket ownership until worker exit, and explicitly unregisters socket events before
releasing their handles. Same fixture has real Winsock import observers; it waits
for the first receive continuation, not an arbitrary delay.
Job69392 (`phone-windows-after-v2`) completed: all five cases PASS against actual
channel TU and real SharpRuntime/core archives; compile7320ms/link67ms, complete1899ms,
idle2800ms, partial386ms, partial-header296ms, truncated495ms. Peak598634496/events0.
Linux final job94627: all five cases plain and TSan PASS; five native CTests PASS;
all18 existing phone tests PASS, exact outcome parity with previous full native
aggregate `repair-occlusion-native-full.xml`. Build19067ms, CTest3382ms, phone907ms,
MinGW actual-TU compile3721ms. Peak974692352/events0/no source drift. No sanitizers
were claimed for uninstrumented SharpRuntime/SDL dependencies.
Final external job19798 rebuilt against the final source and preserved both phone
demo outcomes (9+2 checks) on SOFTWARE and OPENGLES3, no knownIssue changes.
Build24649ms, peak3663585280/events0/no drift. Reports `phone-external-final-*`.
Tests retain real recv/WSA waits; partial requests cancel, EOF-truncated bodies are
rejected, complete queued bodies survive Close, registry/URI disappear and repeated
Close does not reclaim a reused handle. New CTests cover native Linux and MinGW64;
no fake native Windows claim. Final CMake-only adjustment limits MinGW wrappers to
64-bit import spelling; no source behavior changed. No full CNA aggregate rerun.
Next independent task: SDL3 mixer callback/userdata races, then pending-pan teardown.

Museum-owned inventory now confirms the browser tour at5b6aab981:320 rooms,
zero destroyed-texture/uncaptured errors; the unrelated PbrEffect finding remains.
It also records successful museum net/tray revalidation. These are consumer-reported
results, not additional runs performed by this agent.

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

Canvas repair commit: `31a3d54b8`, local `next`, not pushed.

MUSEUM-NET-003: FIXED_UBSAN_AND_NATIVE_VALIDATED, awaiting museum revalidation.
This repair follows `31a3d54b8` on local `next`; its commit contains this checkpoint.
CNA-native C fixture calls the actual protocol TU and real queue/receive path, covering
every bit, duplicates, rollover and stale packets. Initial test registration was absent
because CNA's root project only enabled CXX; enabling C in the test's native Linux scope
fixed that harness error (no production change). Baseline78874 then reproduced UBSan
`protocol.c:492:53: left shift of 1 by 31 places cannot be represented in type int`.
Both literals changed to1u; after29501 passes26630 checks, build921ms/test566ms,
peak157011968/events0/no source drift. Narrow vendor change documented in
`third_party/enet/CNA_PATCHES.md`. First full-net driver had a JavaScript syntax error
and never built or ran tests; fixed and syntax-checked before relaunch.
Full regression completed in scope `cna-museum-enet-net-after-v2`, session89881, driver demos
`build-probe/repair-museum-enet-net.mjs net-after`: existing debug build/net harness,
UBSan CTest and full native network suite, one worker/6GiB/private GPU/private network,
owner UID and zero capabilities. Build2679ms, UBSan CTest494ms, full net46462ms:547/547PASS,
exact case-by-case parity with the preceding welcome repair. Peak1505333248/events0,
no source drift or build diagnostics. Reports `repair-museum-enet-*` in demos.
No heavy job remains. No museum net_check run, full CNA aggregate, other platform or
extra renderer run claimed for this transport-only, wire-format-preserving repair.

Next independent work: notification/mixer lifetime races; browser ErrorOccurred API.
ENet repair commit: `5b6aab981`, local `next`, not pushed.

Notification baseline in progress: actual HttpNotificationChannel.cpp plus standalone
public-API fixture linked against existing debug libcna. Plain100 complete-request
open/close cycles pass; the same TSan run reports no race (not proof of absence).
The deterministic partial-request case observes the listener entering its second real
recv, then calls Close: it hangs and its own bounded process is terminated after10s.
Report `repair-museum-notification-before-*`; no source drift, peak265203712/events0.
Before-v2 session59902 completed: both TSan complete-request and2000 idle cycles reproduce
the exact Close:223 write/Listen:180 read race, exit66; partial Close still times out.
Candidate passes all five cases plain and TSan (complete100, idle2000, partial body,
partial header, truncated100). Job27856 peak272269312/events0/no drift. Listener captures
the native-width socket handle; Close shuts down accepted work under the same mutex as
its close, joins, then releases the listener. Incomplete bodies no longer become queued
successes. Queued complete notifications are retained after Close. Fixture is now
`tools/phone/notification_lifecycle_test.cpp`; five standalone CTests registered.
The recv wrapper only observes entry; it calls the real syscall and parser.
Native35451 completed: build16918ms, five CTests PASS, all18 phone tests PASS, MinGW
actual-TU cross-compile PASS; peak1022484480/events0/no drift. Not Windows runtime proof.
Current sole heavy job:session35138, scope `cna-museum-phone-external`, driver demos
`build-probe/repair-museum-phone-external.mjs`. Captures old phone demo outcomes on
SOFTWARE/GLES, rebuilds cna-demos against current CNA, then compares both phone scenarios
exactly. One worker/private GPU/private network/6GiB. Do not edit C++ during this job.
**Before committing, audit Windows cancellation:** shutdown on a listening Winsock
socket is not a documented accept-cancellation mechanism; delaying closesocket until
join may change the old Windows behavior. Need a focused Windows/Wine control (or
event-based cancellation if required), not just the successful cross-compile. No
Windows runtime pass or notification repair closure is claimed yet.
Current HttpNotificationChannel source and tests read. Close writes listenSocket_ while
Listen reads it, and closes the fd before join; accepted connections are not shut down,
so a partial request can strand Close in join. Reads can also deliver truncated bodies,
contrary to the source's own stated contract. Need deterministic cancellation tests and
actual-TU TSan reproduction before repair. Do not infer a thread-safe public Open/Close
contract: Microsoft's instance methods are not guaranteed thread-safe, but the class
must synchronize its own listener. Browser ErrorOccurred is a separate public contract
repair; Microsoft Learn names the enum **ChannelErrorType**, not the museum's suggested
NotificationChannelErrorType. Reference:
https://learn.microsoft.com/en-us/previous-versions/ff707848(v=vs.110) .

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
