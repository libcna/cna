# BATCH_6_STABILIZATION.md — Skia + Direct2D + LLGL + Metal · 2026-08-09 · `debian`

> ## CHECKPOINT RETAKE — **COMPLETE; LOCAL SIGNED TAG TAKEN**
>
> The required clean/signature/invariant retake passed. Signed annotated tag
> **`integration/checkpoint-batch6-20260809`** was created once without force with exact message
> **`CNA integration Batch 6 checkpoint`**. Tag object
> **`8d347c933a3da3c39f22711e40e80cf7a29c4682`** peels to
> **`012b158eb8246ce267887acbd4fc7a2468d89e52`**; `git tag -v` exits 0 with a Good signature from
> Robert Vokac under fingerprint `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. The tag is local
> only and was not pushed. Section 8 is the authoritative completed retake; §7 preserves the
> initial technically READY, pre-tag decision. No final campaign or `develop` readiness is claimed.
>
> ## INITIAL TECHNICAL DECISION — **READY; CHECKPOINT RETAKE AUTHORIZED; TAG NOT YET CREATED**
>
> Group G is complete at **4/4**, the authoritative inventory is **21/21 integrated with zero
> pending lanes**, and the accepted integration target is signed Metal merge
> **`012b158eb8246ce267887acbd4fc7a2468d89e52`**. This record authorizes one fresh
> clean/signature/invariant retake and, only if it remains green, creation of signed annotated tag
> **`integration/checkpoint-batch6-20260809`** with exact message
> **`CNA integration Batch 6 checkpoint`** at that target. **The tag does not yet exist and was not
> created by the planning or integration sessions.** This is a Batch 6 technical integration
> decision, not a final campaign or `develop` readiness decision.

Full lane records: `integration/lanes/skia.md`, `integration/lanes/direct2d.md`,
`integration/lanes/llgl.md`, and `integration/lanes/metal.md`.

## 1. Membership and accepted history

Group G retains its original four-lane, 356-historical-commit membership. Skia landed early by
explicit owner instruction; intervening group and checkpoint work does not remove it from Batch 6.
Direct2D was later boundedly unfrozen, followed by LLGL and Metal.

| Lane | Original | Adaptation | Signed integration | Accepted boundary |
|---|---|---|---|---|
| Skia | `ca046f013bfd9797aab0292194e547d1caa4fef8` · 141 commits | `a071e1e23120a142840d54777d41c4e58fc2345c` · 151 commits | `1381ff930a88cdda2a17a25136a1b1fd93b3adcf` | Genuine 32nd identity; CPU-raster 2D; 172/172 lane suite; Ganesh remains owner-paused and unreachable |
| Direct2D | `9b17e783e74e87a3f23b9cc47bd3c7cd6dad9d81` · 48 commits | `1b740d962d85bb648d6ae2997bba9b1ba09dfd87` · 55 commits | `7af760bee2896960270cfd7bd6c822b96c13be94`; signed wording-only follow-up `21b1fcd172f5e16875e8f28f500f14219166c73a` | Genuine 39th identity; Windows Direct2D 1.1, 2D-only; MinGW/Wine/Xvfb 4/4 |
| LLGL | `fa26e72dcda612de2a8cff814e748c7479e45836` · 68 commits | `c74fbaebb93745de08130d050e11230639df3259` · 69 commits | `4ac696c748fb18eef7dd06cca82a0486549bcd5d` | Genuine 40th identity; Linux/X11 x86_64 LLGL OpenGL; 145 registered, 137 passed, 8 deliberately disabled |
| Metal | `48928d113cb864f78d754256d2d559d914d4f1a7` · 99 commits | `e2ffe7290ddf5aab5c211b1fc2c00f0e09bd42f1` · 94 commits | `012b158eb8246ce267887acbd4fc7a2468d89e52` | Genuine 41st identity; direct macOS Metal; conservative source-continuity acceptance with adapted Apple validation external |

The final Metal merge has parents `4ac696c748fb18eef7dd06cca82a0486549bcd5d`
and `e2ffe7290ddf5aab5c211b1fc2c00f0e09bd42f1`. Its tree
`31200b608cd2a4c8ccd0f7cb9d6325540cec9458` is byte-identical to the adaptation tree.
There are exactly **21 signed first-parent logical lane merges** since phase-1 checkpoint
`d79214e7600c0411ce912be11f8e762866be23ee`; public backend identity count is **41**.

## 2. Metal source continuity and supported contract

The final lane was accepted under the owner's explicit no-Mac/source-continuity policy. The
supported implementation is native Objective-C++/MSL over Metal, QuartzCore, and Foundation. SDL3
supplies only the macOS window, high-pixel-density Metal view, and `CAMetalLayer`; no SDL_Renderer,
SDL_GPU, or another CNA backend performs rendering. CMake accepts `METAL` on macOS only and rejects
iOS, tvOS, and non-Darwin hosts. No explicit minimum macOS deployment target is claimed.

The current complete 13-capability contract is conservative:

- `ThreeD`, `DepthStencilBuffer`, `AnisotropicFiltering`, `WireFrame`, `Texture3D`,
  `StencilBuffer`, and `AdditiveBlending` report true within the lane card's guarded contract;
- MSAA, MRT, OcclusionQuery, CustomEffects, MultiStreamVertexInput, and Instancing report false and
  reject or clamp deterministically; and
- backbuffer readback throws rather than reporting the historically known-wrong clear-only pixels.

The shared production delta at the final target is the required registration/build union and one
Metal-guarded `SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY` selection in
`GraphicsDevice.cpp`. There is no common `IGraphicsBackend`, `GraphicsCapability`, or shared
`Texture2D` production change. The historical removed `GpuDrawParams::instanceVb` write was not
restored; current stream arrays, offsets, declaration validation, render-target descriptors, and
resource contracts remain authoritative.

`METAL-258/-259/-266` close through truthful disablement. `METAL-260` through `-265`, `-267`
through `-280`, and `-281` close through implementation plus portable policy/oracle coverage,
with native proof explicitly external where required. `METAL-281` guarantees only independently
retained backend handles and their native resources: it does not claim a public
Texture/GraphicsResource wrapper is safe after its raw-pointer-owning `GraphicsDevice` dies.
Historical `METAL-257` is corrected, not silently rewritten: the Metal window selection already
requested `SDL_WINDOW_HIGH_PIXEL_DENSITY`.

## 3. Evidence matrix

| Gate | Accepted result | Boundary |
|---|---|---|
| Skia lane | **172/172** dedicated; EasyGL and Sokol controls exposed and gated the two shared blockers before acceptance | CPU-raster identity; Ganesh remains paused, unreachable, and unclaimed |
| Direct2D lane | MinGW GCC 14 x64 build; Wine 10/Xvfb **4/4**, including Unit 19/19; post-fix focus 26/26 | Compatibility evidence; native MSVC/physical Windows lifetime, DPI, and debug-layer gates remain external |
| LLGL lane | **145 registered / 137 passed / 0 failed / 8 disabled**; full corpus 5210/5203/7; strict sanitizer matrix 9/9 | Supported runtime is Linux/X11 x86_64 OpenGL; Vulkan, i686, Windows, and non-X11 routes unclaimed |
| Historical Metal Apple run | Actions `29814126178`, macOS 14/Xcode 15.4, Metal validation; **136/143** | Original production commit `e0f42426836ce9f2d4823d50732850877020aef1` only; final four original commits are docs-only |
| Adapted Metal portable | HEADLESS build; **206/206** unique portable cases; **207/207** `ctest -R '^Metal'` registrations | No Objective-C++/`.mm` node exists in this graph |
| Adapted Metal sanitizer | GNU 14.2 ASan+UBSan **206/206**; complete log has no ASan, LSan, UBSan, or runtime-error diagnostic | Portable C++ boundary only |
| Non-Darwin Metal selection | Intended macOS-only configure rejection; no Objective-C++ or `.mm` artifact | Platform policy, not native build evidence |
| Current EasyGL control | **125 total = 124 pass + 1 intentional wireframe-inapplicable skip**; real RT/readback/viewport-scissor **2/2** | Principal shared-backend continuity after Metal merge |
| Current LLGL control | Focused **48/48** plus smoke/RT/viewport-scissor **3/3** | Accepted Group G supported-path continuity after Metal merge |

The seven historical Metal failures are classified, not hidden. Six—PBR, SkinnedPBR, DrawUser
VPC, SpriteBatch custom effect, MRT, and backbuffer MSAA—read only the clear colour. The separate
RT2D MSAA case applied four samples but produced a binary edge. `Metal_Capabilities` proved only
then-current booleans. Because the adaptation changes interfaces, transfer/lifetime policy, and
supported behavior, none of that original-tree run is inherited as an adapted compile or runtime
pass.

There is therefore no adapted Apple Objective-C++ compile, framework link, MSL compile, native
resource-lifetime run, pixel result, Retina/frame-pacing result, physical-display result, or
Intel/Apple-Silicon comparison. A fresh successful macOS workflow is an external support-confidence
gate. Under the authorized source-continuity decision it is not a Batch 6 integration blocker, and
this record does not turn absence into success.

## 4. History, signatures, and provenance

Metal's machine-readable 99-row map records **87 replayed + 1 replayed-then-adapted-out + 5
omitted diagnostics + 2 omitted handoffs + 2 omitted already-integrated + 2 omitted superseded**.
That is 88 retained chronological replays and 11 explained omissions. Replay range-diff is **76
`=` / 12 `!` / 11 omitted**. Six signed post-audit commits then form the 94-commit adaptation:

| Scope | Test/fix-or-doc pair |
|---|---|
| `METAL-258`–`-262` | `48f6b46f` / `4ccc1d56` |
| `METAL-263`–`-280` | `d3e25ddf` / `64087202` |
| `METAL-281` | `fbfe0756` / `e2ffe729` |

The adaptation reflog proves **10 sequencer conflict stops**; deterministic three-tree replay
reconstruction yields **15 file-conflict events across 11 unique paths** (per-stop counts
5, 1, 1, 2, 1, 1, 1, 1, 1, 1). The affected paths are the root/backend/test Metal CMake files,
backend identity and shared `GraphicsDevice` registration, the Metal implementation, compile-
definition test, `README.md`, and `plans/plan_metal.md`; the exact path list and adapted stop SHAs are in
the lane card. The reconstruction is explicit because resolved index stages are not retained.
Two of the 12 range-diff `!` pairs applied cleanly, so `!` is context/semantic difference rather
than a conflict count; the lane card also records the one heuristic pairing for which the curated
99-row TSV, not range-diff similarity, is authoritative.

Metal adaptation is 94/94 Good, and adaptation plus its merge is 95/95 Good and Robert-authored /
Robert-committed. The complete integration range is **899/899 `%G? = U`** under fingerprint
`255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. The four Group G merge commits above are signed
two-parent merges. Attribution and prohibited-trailer sweeps are empty; a broad Metal-range
`Claude` sweep finds only three factual references to tracked filename `CLAUDE.md`.

Both local and remote original Metal refs remain `48928d113cb864f78d754256d2d559d914d4f1a7`.
Sole annotated archive tag `archive/preintegration/metal-20260804` is object
`43f6eab8d40c6006265cd4e19223cdd3d68c1fc3`, verifies Good, and peels to that head. The original
was inspected ref-only; no historical Metal worktree was created or checked out during adaptation.

## 5. Findings and residual gates

No known defect remains reachable through Metal's conservative supported contract. All
`METAL-258` through `METAL-281` have explicit dispositions in `integration/lanes/metal.md` and
`plans/plan_metal.md`; native proof remains external where stated. Carried group/campaign residuals stay
truthful:

- `REMED-GFX-224` remains MEDIUM/OPEN for the pre-existing EasyGL-only render-target `SetData`
  behavior; it is unchanged and is not a Group G supported-path blocker;
- Skia Ganesh remains paused at `SKIA-163` without becoming a second backend identity;
- physical Windows/MSVC Direct2D validation and native COM/live-object evidence remain external;
- LLGL's unsupported Vulkan, i686, Windows, and non-X11 routes remain outside its accepted
  Linux/X11 OpenGL contract; and
- adapted Apple validation remains the Metal confidence gate described above.

None is represented as complete. None changes the technical READY decision within the accepted
contracts. Final campaign/`develop` review remains a separate owner decision.

## 6. Build, storage, process, and repository controls

Metal stabilization used stable in-repository build directories, ccache,
`CNA_MAX_VENDORED_BUILD_JOBS=2`, and explicit `-j4` or lower. Final tree sizes were:

| Tree | Bytes | Reported size |
|---|---:|---:|
| HEADLESS | 524,162,423 | 508 MiB |
| non-Apple Metal rejection | 271,419 | 344 KiB allocated |
| OPENGLES/EasyGL | 1,639,609,425 | 1.6 GiB |
| LLGL | 2,054,503,530 | 2.0 GiB |

The final ccache snapshot was shared host-global, not lane-exclusive: 29,865 cacheable calls,
3,926 hits, 25,939 misses, 1,023 uncacheable, and 4.7/10.0 GB used. One attempted wider sanitizer
build completed the portable target, then hit an unrelated pre-existing HEADLESS harness link
error (`CNA::Logger::Warn` unresolved); it is excluded from acceptance. The final helper-only
ASan+UBSan build/run is the 206/206 result in §3. Session-owned Xvfb `:193`, PID `3342458`, was
stopped and its lock/socket removed. Foreign pre-existing `:101` and `:102` were untouched. No
build/test child remained at handoff.

The observed 16-logical-CPU saturation came from foreign PID `3091920`, an unbounded
`ninja -C cmake-build-compile-software iron_gang --quiet`, not Metal. The Metal session stopped
launching work, did not kill or signal the foreign process tree, and resumed only after it exited;
one unrelated single-thread syntax check remained. Metal commands used `env -u DISPLAY`, no
`DISPLAY=:0`, and no temporary build tree. Thermal readings were not captured and are therefore
recorded as **not measured**, not inferred.

Integration and adaptation worktrees were clean. The original lane refs and archive objects were
not moved. `audit/` remained tree `168c9b668763b78e63106e27d942a76d2457f41d`. The protected stash
objects remained, newest to oldest:

1. `888c3dcc8fb4fc6949bf3790a1483862328b6033`
2. `d3b92226e00deb239c7587592c0c5bfc73078aaf`
3. `5623d2202fea60b64eb50afa120745595b75d89b`
4. `8f8b8f55c647eb9e57a14093e4f5e30f55fe4157`

No destructive operation, original-ref rewrite, tag creation, or push belongs to the Batch 6
integration record. Local tracking evidence—not a live remote query—left
`origin/integration/post-audit-phase1` at `c805fd737f4321568fba378e8d1b8fe5b5270666` while local
integration ended at `012b158e`, 222 commits and four first-parent commits ahead.

## 7. Initial checkpoint decision and required retake — historical

**Decision: READY, conditional only on the immediate checkpoint invariant retake.** Group G is
4/4, the inventory is 21/21 with zero pending, all 21 logical lane merges are present and signed,
and the current supported-path controls are green. The planning record now authorizes creation of
the Batch 6 checkpoint using this exact convention:

| Field | Required value |
|---|---|
| Tag kind | Signed annotated tag |
| Name | `integration/checkpoint-batch6-20260809` |
| Message | `CNA integration Batch 6 checkpoint` |
| Target | `012b158eb8246ce267887acbd4fc7a2468d89e52` |
| Present state | **Not created locally; not pushed** |
| Meaning | Batch 6 / Group G technical integration checkpoint only; no final campaign or `develop` readiness claim |

Immediately before tag creation, the tagging session must freshly verify:

1. integration, Metal adaptation, and audit/planning tracked worktrees are clean apart from the
   preserved owner-provided untracked `AGENTS.md` in the planning tree;
2. final target, parents, and tree are the exact objects above, and the tree still equals
   `adapt/metal`;
3. exactly 21 first-parent logical lane merges exist since `d79214e7`, Group G is 4/4, inventory is
   21/21, and the complete integration signature sweep remains 899/899 Good;
4. Metal original local/remote refs, sole archive tag object/peel/signature, 99-row map, audit tree,
   and all four stash object IDs remain unchanged;
5. no conflicting local tag exists; and
6. after creation, the annotated tag object is distinct from its peeled commit, peels exactly to
   `012b158e`, and verifies Good; measured local and remote/push state is then recorded.

If any invariant differs, do not tag; update the evidence and retake the technical decision. The
planning commit itself is intentionally on `feature/audit` and is not the checkpoint target. The
tag remains absent at this handoff, and no push is authorized by this document.

## 8. Completed checkpoint retake — COMPLETE

The one authorized retake completed without changing the accepted Metal integration target or the
technical READY decision. The exact completed checkpoint evidence is:

| Field | Completed value |
|---|---|
| Decision | **COMPLETE** — Group G 4/4, inventory 21/21, zero pending |
| Tag kind | Annotated, signed tag |
| Name | `integration/checkpoint-batch6-20260809` |
| Exact message | `CNA integration Batch 6 checkpoint` |
| Tag object | `8d347c933a3da3c39f22711e40e80cf7a29c4682` |
| Target / peel | `012b158eb8246ce267887acbd4fc7a2468d89e52` |
| Creation | Created once without force |
| Signature | `git tag -v` exit 0; Good signature from Robert Vokac; fingerprint `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F` |
| Distribution | Local only; not pushed |
| Meaning | Batch 6 / Group G technical integration checkpoint only; no final campaign or `develop` readiness claim |

Post-creation object inspection reports type `tag`, an annotated object distinct from its peeled
commit, the exact tagger identity `Robert Vokac <robertvokac@robertvokac.com>`, and the exact
message above. The target remains the signed Metal merge, whose parents/tree/adaptation identity
are recorded in §§1 and 4. The invariant retake retains 21 first-parent logical lane merges and the
899/899 Good integration signature result; original Metal refs/archive, audit tree, and the four
protected stash IDs remain unchanged.

**Final Batch 6 checkpoint status: COMPLETE.** The initial source-continuity/no-Mac support
boundary remains binding, and a fresh adapted Apple run remains external. The local checkpoint was
not pushed, and checkpoint completion does not imply final campaign or `develop` readiness.
