# REMEDIATION_DEPENDENCIES.md — Ordering, Parallelization, Merge Cadence

## The one thing to understand first

**Until `REMED-BUILD-001` lands, ~220 tests do not run under `ctest`, and CI has never run the general
suite at all (`REMED-BUILD-004`).** Every "verified" claim made against CTest before that fix is
unreliable. This is why a one-line CMake change outranks a CRITICAL memory-corruption bug in the
schedule: the CRITICAL bug's own regression test
(`XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`) is **in the broken set.**

Fix the measuring instrument, then measure.

## Dependency graph

### Root — nothing depends on these; everything depends on them evidentially

```
REMED-BUILD-001 (WORKING_DIRECTORY)  ──┐
REMED-BUILD-002 (xact build break)   ──┤
                                       │
                                       ├──▶ trustworthy test signal
                                       │
REMED-BUILD-004 (CI runs general set) ─┘   (needs BUILD-001 first, or it goes red immediately)
```

### Hard dependencies (B cannot start, or cannot be verified, until A lands)

| Blocker | Blocks | Why |
|---|---|---|
| `REMED-BUILD-001` | `CONTENT-001`, `-002`, `-003`, `-004`, `-006`, `NET-001`, `MEDIA-001`, `MEDIA-003`, and every GFX task with a CTest | Their tests are in the ~220 that do not currently run |
| `REMED-BUILD-001` | `REMED-BUILD-003`, `REMED-BUILD-004` | The failing-test list is not trustworthy until the working directory is right |
| `REMED-BUILD-008` (D3D12 gets >1 test) | `REMED-GFX-014`, `REMED-GFX-015` | **Both fixes are unverifiable without a test that reaches their trigger conditions.** `D3D12_Smoke` does not. |
| `REMED-TEST-002` (Game/GDM test harness) | `REMED-CORE-006`, `REMED-CORE-007` | Both production bugs currently have **zero** test coverage; there is no harness to verify a fix |
| `REMED-GFX-005` (correct fog formula) | `REMED-GFX-009` (SdlGpu fog from scratch) | SdlGpu must copy the *corrected* formula, not one of the three wrong ones |
| `REMED-GFX-025` (XNA-layer dest offset) | its own backend implementations | Fixing backends first adds an unreachable parameter |

### Atomic pairs — must land in the same commit

Landing either half alone turns CI red for a correct change, which invites reverting the fix rather
than the test.

| Production task | Test task | Reason |
|---|---|---|
| `REMED-GFX-022` (EffectParameter Matrix) | `REMED-TEST-001` (a) | The test asserts the exact inverse convention as correct |
| `REMED-CORE-003` (exception base classes) | `REMED-TEST-001` (b) | 6 tests assert `std::runtime_error` inheritance |
| `REMED-CORE-002` (exception types, gamerservices slice) | `REMED-TEST-001` (c) | `GamerServicesDataTests.cpp` asserts the raw types |
| `REMED-NET-001` (host authority) | `REMED-NET-003` (resend guard) | Same file, same threat model, same review |
| `REMED-GFX-005` (fog formula) | its own test expectations | Affected tests currently assert the **wrong** values |

### Ordering constraints that are not dependencies

| Do this | Before this | Why |
|---|---|---|
| `REMED-GFX-016`, `-017`, `-018` | `REMED-BUILD-003` (`WILL_FAIL` rollout) | **Do not annotate a test whose bug is being fixed.** `WILL_FAIL` is for accepted limitations, not work in progress. |
| All P0/P1 fixes | `REMED-DOCS-001`, `REMED-DOCS-002` | Several docs describe behavior currently being changed; sweeping first means sweeping twice |
| `REMED-GFX-021` — check Software first | fixing Dx3 | The rotation formula is a byte-for-byte port of Software's; if shared, one fix closes both |
| `REMED-CONTENT-002` shared helper | its 3 call sites | Do not let three lanes each write their own containment helper |

### Soft couplings — same files, coordinate but no strict order

- `REMED-GFX-006` ↔ `REMED-GFX-008` — same skinned shaders; do together per backend
- `REMED-GFX-011` ↔ `REMED-GFX-006`/`-007` — Vulkan `pbr3d_skinned` and `env_map3d` overlap; do all three together for Vulkan
- `REMED-GFX-033` ↔ `-034` ↔ `-049` — one PackedVector pass, one reference-data regeneration
- `REMED-GFX-025` ↔ `-026` — both are `IGraphicsBackend` signature gaps; consider **one** coordinated interface revision rather than two churns
- `REMED-CORE-006` ↔ `-007` ↔ `-009` — all in `Game.cpp`/`GraphicsDeviceManager.cpp`
- `REMED-GFX-024` ↔ `-048` — both touch `BasicEffect`
- `REMED-BUILD-005` ↔ `-009` — both are implicit-transitive-linking assumptions that hold only on native GCC

## The two bottlenecks

### Bottleneck 1 — shader serialization inside the GRAPHICS lane

`REMED-GFX-005`, `-006`, `-007`, `-008`, `-009`, `-010`, `-011`, `-020` all edit the same shader
files. `D3DCommon` alone is touched by four of them, and `D3DCommon` feeds **both** D3D11 and D3D12.

**This is the plan's critical path.** Eight tasks, ~14 backends, largely serialized, and three of them
(`-005`, `-006`, `-009`) are the biggest single pieces of work in the plan.

**Recommended handling — organize by backend, not by defect:**

```
for each backend group (D3DCommon → Bgfx → Vulkan → SdlGpu → EasyGL → WebGPU → D3D9):
    apply every applicable shader fix in one pass
    update that backend's test expectations in the same commit
    verify against the cross-backend conformance suite
```

D3DCommon first: one edit closes D3D11 and D3D12 together — the highest-leverage single change in the
graphics lane. Vulkan is the heaviest (fog + skinned normal + ambient/emissive + Y-flip in one pass).

**Build the cross-backend conformance tests before the shader work, not after.** Fog, skinned
lighting, and env-map all need tests that current tests structurally cannot provide (non-identity
World, mid-range Z, non-default colors, off-center pixels). Without them there is no way to know a fix
worked, and no way to stop the next port from re-propagating the bug.

### Bottleneck 2 — `REMED-CORE-002` touches files every lane owns

The exception-type sweep spans `GraphicsDevice.cpp`, the `Texture*` family, effects, collections,
`Model.cpp`, `PropertyDictionary`, and three `xna-framework-core` files — owned by GRAPHICS, NET, and
CORE respectively.

**Recommended handling:** slice by area with explicit hand-offs, smallest area first to build
confidence before `GraphicsDevice.cpp` (~27 sites, the framework's most central class). Schedule the
large slices in a quiet window. Note this is a **catch-site-visible** change: grep the whole repo
(including `examples/` and `tools/`) for `catch (std::runtime_error`-style handlers before starting.

## Parallelization plan

### Lanes

Seven lanes are justified by the findings. Two proposed lanes are not, and are deliberately not created.

| Lane | Branch | Tasks | Notes |
|---|---|---|---|
| **build-tests-ci** | `remediation/build-tests-ci` | 19 | **Start here.** Owns BUILD, TEST, DOCS. |
| **graphics** | `remediation/graphics` | 51 | Largest. Internally serialized for shaders. Consider 2 sub-branches: `graphics/shaders` (serialized) and `graphics/api` (parallel-safe XNA-facing work). |
| **core** | `remediation/core` | 13 | |
| **content** | `remediation/content` | 5 | Highest security density — 4 of 5 are security-impacting. |
| **net** | `remediation/net` | 7 | Includes GamerServices. |
| **media** | `remediation/media` | 4 | |
| **devices** | `remediation/devices` | 3 | |
| ~~audio~~ | — | 2 | **No lane.** Two LOW tasks; fold into whichever lane has capacity. |
| ~~input~~ | — | 0 | **No lane.** Zero tasks; its single finding is inside `REMED-CORE-012`. |

### Cross-lane single-ownership rules

The instruction "no root cause assigned independently to multiple owners" is enforced by these
assignments. Each of the following spans several subsystems but has **exactly one owner**:

| Root cause | Spans | Sole owner |
|---|---|---|
| `fs::path` containment pitfall | Storage, Content, Media | **CONTENT** (`REMED-CONTENT-002`) |
| Raw `std::` exception convention | Graphics, Net, GamerServices, Core, Content | **CORE** (`REMED-CORE-002`) |
| Fog formula | Bgfx, Vulkan, D3D11, D3D12 | **GRAPHICS** (`REMED-GFX-005`) |
| SkinnedEffect normal transform | all 14 backends, 2 variants | **GRAPHICS** (`REMED-GFX-006`) |
| EnvironmentMapEffect emissive | 5 backend groups | **GRAPHICS** (`REMED-GFX-007`) |
| `GetTypeName()` omissions | Graphics + GamerServices | **GRAPHICS** (`REMED-GFX-050`) |
| Malformed-texture validation | Content + Vulkan + WebGPU | **CONTENT** (`REMED-CONTENT-001`) — explicitly **not** per-backend |
| `WILL_FAIL` policy | all 14 backends' cmake files | **BUILD_TEST_CI** (`REMED-BUILD-003`) |
| Documentation rot | every shard | **BUILD_TEST_CI** (`REMED-DOCS-001`) |

**If you find yourself fixing one of these outside its owning lane, stop.** That is the exact failure
mode this plan exists to prevent — and it is how several of these defects reached 5+ backends in the
first place.

### Parallel-safety summary

| PS | Count | Handling |
|---|---|---|
| **YES** | 61 | Freely parallel within and across lanes |
| **CONDITIONAL** | 21 | Safe under the stated constraint — read the task's field before starting |
| **NO** | 22 | Must serialize. Mostly the shader cluster, the exception sweep, and the atomic test pairs |

## Recommended waves and merge cadence

### Wave 0 — make the tests trustworthy (days, not weeks)

`REMED-BUILD-001` · `REMED-BUILD-002`

Two small changes. **Merge immediately and independently — do not batch them with anything.**
Expect follow-on triage: ~220 tests will run for the first time, and some will genuinely fail.
Budget for that triage; do not assume a clean result.

### Wave 1 — security and memory safety (the first implementation wave)

Detailed in `MASTER_REMEDIATION_PLAN.md`; the full P0 set:

`REMED-CONTENT-001` · `-002` · `-003` · `-006` · `REMED-GFX-001` · `-002` · `-003` ·
`REMED-NET-001` (+`-003`) · `REMED-DEVICES-001` · `REMED-MEDIA-001`

Five lanes run genuinely in parallel here (CONTENT, GRAPHICS, NET, DEVICES, MEDIA) with almost no file
overlap — this is the plan's best parallelism. In parallel, BUILD_TEST_CI starts `REMED-BUILD-004`,
`REMED-BUILD-008`, and `REMED-TEST-002`, each of which unblocks later work.

**Merge cadence:** per-task, as each completes. Do not hold a security fix waiting for a sibling.

### Wave 2 — correctness foundations

`REMED-CORE-001` (foundational logging) · `REMED-CORE-006`/`-007` (lifecycle, after `TEST-002`) ·
`REMED-CORE-004` · `REMED-GFX-004` · `REMED-GFX-012`/`-013`/`-016`/`-017`/`-018`/`-019` (isolated
per-backend defects, highly parallel) · `REMED-MEDIA-002` · `REMED-NET-002` · `REMED-CONTENT-004` ·
`REMED-BUILD-003` (after the GFX fixes it would otherwise mask)

**Merge cadence:** weekly integration. These are independent enough to batch.

### Wave 3 — the shader campaign (the long pole)

`REMED-GFX-005` → `-006` → `-007` → `-008` → `-011` → `-009` → `-010` → `-020`, organized **by
backend** per Bottleneck 1, with the conformance test suite built first.

**Merge cadence:** per backend group, after that group passes the conformance suite. Do not merge a
half-migrated backend — a partially-fixed matrix is worse than a uniformly-wrong one, because it
silently splits behavior across backends.

### Wave 4 — API surface, architecture, conventions

The remaining P2 set, including the two interface revisions (`REMED-GFX-025`/`-026` — do as one) and
the exception sweep (`REMED-CORE-002`, scheduled into a quiet window).

**Merge cadence:** weekly, with the exception sweep given a dedicated window.

### Wave 5 — hygiene

All P3, plus `REMED-DOCS-001`/`-002` **last**, so the documentation sweep describes the post-remediation
state rather than a moving target.

## Critical path

```
BUILD-001 ──▶ [triage ~220 newly-running tests] ──▶ BUILD-004 ──▶ trustworthy CI
                                                          │
CONTENT-001 (CRITICAL) ───────────────────────────────────┤
                                                          │
BUILD-008 ──▶ GFX-014, GFX-015 ───────────────────────────┤
                                                          │
TEST-002 ──▶ CORE-006, CORE-007 ──────────────────────────┤
                                                          ▼
        [conformance test suite] ──▶ GFX-005 ──▶ GFX-009 ──▶ shader campaign
                                                              (longest single chain)
```

The **shader campaign is the schedule driver** — eight largely-serialized tasks across 14 backends.
Everything else can be absorbed around it. If the effort needs to be time-boxed, the honest split is:
Waves 0–2 deliver nearly all the safety and reliability value; Wave 3 delivers FNA rendering fidelity
and is where most of the remaining effort actually sits.
