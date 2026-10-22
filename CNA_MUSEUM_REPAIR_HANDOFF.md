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

MUSEUM-NET-002: FIXED_NATIVE_VALIDATED, awaiting museum revalidation. Public Join
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

Next: MUSEUM-TRAY-001, retained label pointers in Sdl3Tray. A deterministic native
retaining-SDL fixture is prepared at `tools/platform/tray_label_lifetime_test.cpp`,
not yet built/registered; production code remains unchanged. Run it before repair,
then preserve per-entry string storage, validate with sanitizers and the platform
suite. A real private-D-Bus host revalidation is distinct from that seam.

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
The museum's already-fixed missing-asset diagnostic and disposed StorageContainer
entries remain historical evidence, not new repairs. No platform/service runtime
coverage is inferred from source review.
