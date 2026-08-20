# BATCH_1_STABILIZATION.md — Batch 1 stabilization and checkpoint record

Companion to `BATCH_0_COMPLETE.md` / `BATCH_0_STABILIZATION.md`. Covers the five Batch 1 lanes
after their integration, the carry-forward repairs they deliberately deferred, and the checkpoint
decision.

**Scope.** Stabilization only. No feature lane was integrated in this session, no published history
was rewritten, `audit/` was not touched, and nothing was pushed.

---

## 1. Identity

| Item | Value |
|---|---|
| Repository | `/rv/data/development/github.com/openeggbert/cna` (shared object store) |
| Integration worktree | `/rv/data/development/github.com/openeggbert/cnaintegration` |
| Integration branch | `integration/post-audit-phase1` |
| Integration HEAD at session start | `9e6d62ed` |
| Planning worktree | `/rv/data/development/github.com/openeggbert/cnaaudit`, branch `feature/audit`, head `23ce8000` |
| Phase-1 checkpoint | `cna-post-audit-remediation-phase1` = `d79214e7` (ancestor ✅) |
| Batch 0 checkpoints | `integration/checkpoint-batch0-20260804`, `integration/checkpoint-batch0-complete-20260804` = `990d6b8a` (both ancestors ✅) |
| Published remote head | `origin/integration/post-audit-phase1` = `61bd1a1b` — ancestor of local HEAD, local is **197 ahead / 0 behind** ⇒ fast-forward only, **no rewrite** |
| Signing key | `FB9CE8E20AADA55F` (`commit.gpgsign=true`; `tag.gpgsign` unset ⇒ `git tag -s` passed explicitly) |

**Lane merges present: exactly 9.**

| # | Lane | Merge |
|---|---|---|
| 1 | depthcrt | `61bd1a1b` |
| 2 | gltf | `722a2f5a` |
| 3 | ext (NOXNA) | `8a374b9f` |
| 4 | dxold | `990d6b8a` |
| 5 | **stub** | `99ae7d11` |
| 6 | **opengles1** | `df6b7cc6` |
| 7 | **opengl4** | `bc29a976` |
| 8 | **opengl1** | `c0876fca` |
| 9 | **opengl2** | `9e6d62ed` |

### 1.1 Refreshed lane count (after `git fetch --all --prune --tags`)

**21 logical lanes — 9 integrated, 12 pending.**

Pending: `direct2d`, `gl`, `magnum`, `wicked`, `diligent`, `gdi`, `glide`, `llgl`, `skia`, `sokol`,
`html-dom`, `metal`.

> **Method note.** Integration status is **not** inferable from ancestry. Adapted lanes replay their
> commits, so an original head is never an ancestor of the integration branch; testing
> `merge-base --is-ancestor <archive-tag> HEAD` reports only `gltf` (the one direct merge) and would
> have under-counted 8 of the 9. The authoritative instrument is the set of merge commits on the
> integration branch.

---

## 2. Provenance review — all five Batch 1 lanes

Each lane was verified independently and directly against git: original ref and head, archive tag
and signature, merge base, original commit count, adaptation branch and head, integration merge,
original→adapted mapping, intentional omissions, signature status, attribution cleanliness,
losslessness, runtime identity and sanitizer disposition.

| Lane | Original head | Archive tag verify | Originals | Adapted | Merge | Verdict |
|---|---|---|---|---|---|---|
| stub | `a35651e8` | Good ✅ | 5 | 7 | `99ae7d11` | **PROVENANCE CLEAN** |
| opengles1 | `3d576da2` | Good ✅ | 26 | 31 | `df6b7cc6` | **PROVENANCE CLEAN** |
| opengl4 | `c49e0ba2` | Good ✅ | 28 | 28 (24 replayed + 4 new) | `bc29a976` | **PROVENANCE CLEAN** |
| opengl1 | `fc14f37b` | Good ✅ | 31 | 37 | `c0876fca` | **PROVENANCE CLEAN** |
| opengl2 | `77d36d9e` | Good ✅ | 40 | 47 | `9e6d62ed` | **PROVENANCE CLEAN** |

**Original branches and archive tags are unchanged.** For all five lanes the local branch head, the
archive tag target and `origin/feature/<lane>` are byte-identical. All five archive tags verify
`Good signature from "Robert Vokac <robertvokac@robertvokac.com>"`, RSA
`255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. The four pre-existing user stashes are untouched.

### 2.1 Per-lane confirmations

**Stub** — clean original history but genuinely content-required adaptation, confirmed from the tree
rather than asserted: `SetVertexDeclaration` and `SetRenderTargets` are non-pure with different
signatures at the lane's fork point `ac3aaaeb`, and **pure virtual** at the integration base, so a
direct merge would have left `StubGraphicsBackend` abstract. Repaired by `b7d472d7` (two empty
`override`s). No-op placeholder contract confirmed: `SupportsCapability` returns `false`
unconditionally. Merge `99ae7d11`; merged tree byte-identical to `adapt/stub`.

**OpenGL ES 1** — 26 originals confirmed. Exactly **two** non-maintainer commits (`cc4c39c0`,
`67cfcc5d`), both SSH-signed and both carrying `Co-Authored-By:` and `Claude-Session:` trailers;
both cleanly re-authored with author dates preserved and both trailers removed. Real
`OpenGL ES-CM 1.1` runtime (re-verified this session, §5). Corrected final test arithmetic
(5733 = 5689 + 43 + 1) reproduces exactly at merged HEAD. **Sanitizer omission is recorded as an
omission**, in the strongest available form — the card states it "did not run", names it "an
omission, not a platform limitation", and argues against itself ("the backend is native Linux/GCC
and instrumentable"). Merge `df6b7cc6`.

**OpenGL 4** — 28 originals, **all 28** authored *and* committed by a non-maintainer identity and
all 28 SSH-signed (object-level inspection; `%G?` is unreliable here and reports `N`). The first 8
carry both prohibited trailers across two session identifiers. **24 technical commits transferred;
4 intentionally omitted** — `a81d8638`, `7e402c96`, `fe4a0a01`, `16d0b212`. Each was verified with
`git show --name-status` to touch **`NEXT.md` and nothing else**, so no source content was dropped;
each is named on the lane card *and* in the merge message with a reason and a named superseding
artefact (policy P6 satisfied). Real `OpenGL 4.5 (Core Profile) Mesa 25.0.7`. Merge `bc29a976`.

**OpenGL 1** — 31 originals, all maintainer-authored and all PGP-signed at object level. A
multiline-aware scan found **exactly 3** narrative bodies, precisely the three the card names —
including two that a line-based grep cannot see because the phrases wrap (`this\nsession`,
`explicit\nuser go-ahead`, `from\nearlier tonight`). The fixed-function claim is proven independently
of the host's reported `4.5 (Compatibility Profile)` string by a source-level property: **zero shader
entry points** anywhere in the backend, and a `1/1` context request with no profile mask. Exact
three-pair fog oracle confirmed in `49fa4940`. Merge `c0876fca`.

**OpenGL 2** — 40 originals, 47 adapted (7 new). Nine narrative bodies, **two of which are visible
only to a multiline scan** (`0bb8cca7`, `1824a9aa` score zero on a line-based `this session` grep).
The **RenderTarget2D orientation defect is real and the fix is real production source**: `289410a6`
adds an `RtFlipActive()`-gated FNA-style render-time clip-Y flip with `glFrontFace` winding
compensation — 95 of 108 changed lines are in the backend `.cpp`, not the test. The lane grew
`GraphicsCapability` from 10 to **11** (`Instancing`) and added truthful arms to four other backends.
Merge `9e6d62ed`.

### 2.2 Provenance record defects found (documentation, not history)

None of these alters a SHA, hides a commit or breaks losslessness. All are corrections to the
*record*.

| Lane | Defect |
|---|---|
| stub | **No sanitizer disposition recorded at all** — the card neither reports a run, nor records an omission, nor declares it inapplicable. Closed by §6 below. |
| stub | The quoted `range-diff` in §8 is an abridged excerpt; the real output carries two further `>` lines (documented elsewhere on the card). |
| opengles1 | §1 says "17 new, 6 pre-existing"; measured **16 new, 7 pre-existing**. |
| opengles1 | §9 says "15 blob-identical, 8 differ"; measured **14 identical, 9 differ** (the card's own enumeration of causes sums to 9). |
| opengles1 | The retracted "87 skipped" figure **survives in two immutable commit bodies** (`a0d07e88` and merge `df6b7cc6`). Post-merge commits cannot be amended, so the correction can only be recorded, never applied to history. |
| opengles1 | `NEXTopengles1.md` ships an inherited "UBSan clean" claim in the merged tree that the card's own sanitizer disposition cannot verify. |
| opengl1 | The phrase "with explicit user go-ahead" was **reworded, not removed** (`99f041a1` now reads "with the project owner's explicit go-ahead"). Defensible under §2.1/F1, but the card implies full removal. |
| opengl4 | GLSL 4.50 is asserted without a verbatim `GL_SHADING_LANGUAGE_VERSION` string; the backend contains no `glGetString(GL_SHADING_LANGUAGE_VERSION)` call at all. GL 4.5 **Core** is fully evidenced. |
| opengl2 | **Four §2.2 process-narration phrases survive** into integrated history — `b4550cf2`, `6f1bb99c`, `d14ccf2d`, `473f119d` (see §7). Two are in bodies the sweep did edit: it removed one phrase and missed a second. |

---

## 3. README backend identity repair

The omission was verified directly and then **re-derived rather than assumed** — which found more
than the carry-forward item named.

`CNA::GraphicsBackendType` has **27** members and `getCurrentGraphicsBackendName()` maps all 27. The
README's `CNA_GRAPHICS_BACKEND` selector list carried **24**. Missing:

- **`OPENGLES1`** — the mandated item.
- **`D3D9`** — has its own README prose section, its own `docs/d3d9-backend.md` and its own build
  recipe *in the same README*, but was absent from the selector list.
- **`SDL_GPU`** — real backend directory, real CMake target, real `cmake/Tests/SdlGpuTests.cmake`;
  absent from the README entirely.

All three were added (one line each, in taxonomy order), plus a descriptive `OPENGLES1` bullet
alongside the other GL-family entries. The list is now machine-verified exhaustive: **27 listed,
27 in the enum, 0 missing, 0 extra.**

Boundaries respected: EasyGL was **not** added as a new public identity (it is already an enum
member and the current default build backend — the "EasyGL remains internal and hidden" rule belongs
to `feature/gl`'s *future* public identity list); no `feature/gl` identity (OpenGL ES 3, OpenGL 3,
WebGL 1, WebGL 2) was added; `DX3` and `FREEDIRECT` remain distinct; no stale `DX30` identity exists;
the list was not reordered and no unrelated prose was rewritten.

---

## 4. `NameMatchesTypeForEveryBackend` repair

**One** test bears this name (`tests/CNA/GraphicsBackendTypeTests.cpp:32`).
`tests/Microsoft/.../GraphicsDeviceBackendTests.cpp` checks delegation, not names, and needs no arm.

**The defect was worse than "missing Batch 1 arms".** The test switched directly on the active
backend, listed **15 of 27** members and had **no failure arm**. For the other 12 backends the test
body executed *no assertion at all* and reported a vacuous pass. Missing were all 8 `dxold`
identities (`DX1`, `DX2`, `DX3`, `DX5`, `DX6`, `DX7`, `DX8`, `D3D10`) plus `OPENGLES1`, `OPENGL4`,
`OPENGL1`, `OPENGL2`. `STUB` was already present.

Repaired to an exhaustive 27-arm `ExpectedNameFor()` helper with **no permissive `default:`**, and a
runtime `ASSERT_FALSE(expected.empty())` that converts a missing arm into a hard failure.

| step | result |
|---|---|
| Focused test **before** repair (STUB build) | 6/6 pass (Stub is one of the 15 covered arms) |
| Focused test **after** repair | pass |
| **Bounded mutation** — remove the `Stub` arm | **FAILS**: `no expected-name arm for the active backend (… reports "STUB") -- add it to ExpectedNameFor()` |
| Arm restored, full suite | 9/9 pass (`GraphicsBackendTypeTest` + `GraphicsDeviceBackendTest`) |

**Honest limit on the compile-time guard.** Omitting `default:` would let `-Wswitch` catch a new enum
member at compile time, but this project builds tests with `CXX_FLAGS = -g -std=c++23` and does not
enable `-Wall`, so **no compiler diagnostic is produced** — the mutation build completed silently.
The runtime guard is the only mechanism actually in force here, and the test comment says so rather
than claiming a protection that does not exist.

### 4.1 Related finding, recorded not fixed

`SupportsCapability` carries a permissive `default:` arm in **6 backends** (Bgfx, EasyGL, Headless,
**OpenGLES1**, Software, Vulkan); only OpenGL1/2/4 are exhaustive. OPENGLES1's default is currently
unreachable — all 11 `GraphicsCapability` members have explicit arms — so it is not lying today, but
it is the same trap that previously made ES1 claim `Texture3D`. Systemic and pre-existing across
backends outside this batch; **out of this stabilization's bounded scope**, recorded here.

---

## 5. Networking flake — evidence-based disposition

*(build/test matrix in §6; this section records the triage)*

### 5.1 Exact identity

| Field | Value |
|---|---|
| Registered test | `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` |
| Binary | `CnaTests` (gtest; **not** a separate CTest test) |
| Source | `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp:214` |
| Spawned harness | `tools/net/net_two_process_harness.cpp`, path baked in as `CNA_NET_HARNESS_PATH` |
| Process structure | 3 spawned processes: `--role=migration-host`, and `--role=migration-survivor` twice (`SurvivorA`, `SurvivorB`) |
| Timeouts | harness-internal 30 s; outer watchdog 50 s (`kOuterWatchdogSeconds + 30`) |
| Ports | host binds an **ephemeral** UDP port, handed to survivors out-of-band via a pipe; `ENetDiscoveryService` additionally uses fixed well-known UDP **61190** with `SO_REUSEADDR` (delivery among multiple bound sockets is, per the source's own comment, "OS-arbitrary") |
| Synchronization | `SurvivorA`'s `JOINED` line is awaited before `SurvivorB` is spawned, to order host-side wire-id assignment |
| Failure shape | **not** a crash, bind failure or connection refusal — all three processes exit 1 on their own internal 30 s timeout |

### 5.2 Measured on current integration HEAD

Run on the Stub build (this test needs no display), gtest filtered to the single test.

| Matrix | Condition | Runs | Pass | Fail |
|---|---|---|---|---|
| A | **isolated**, idle machine (Tctl ~44 °C, loadavg ~0.3) | 20 | **10** | **10** |
| B | **loaded**, concurrent `-j3` backend build | 10 | **9** | **1** |
| C (control) | passing runs printing both `PROMOTED` and `RECONNECTED` | 10/10 | — | — |
| D (control) | leftover `cna_net_two_process_harness` processes before/after every run | 0 / 0 | — | — |

### 5.3 Three recorded beliefs are refuted

1. **"Times out under heavy concurrent load."** Inverted. It passes **90 %** under load and **50 %**
   at rest. A fast, idle machine makes it *worse*.
2. **"Worsening trend 3/3 → 2/3 → 2/6."** Those are samples of size 3, 3 and 6 drawn from what 20
   runs show to be a ~50 % process. The apparent trend is small-sample noise. **There is no trend**,
   and the Batch 1 "watch item" dissolves.
3. **"Environmental."** No stale processes were observed before or after any of the 30 runs, and no
   port-collision or bind failure appears in any failure.

Only the **opengles1** datapoint has preserved raw logs
(`cnaintegration-opengles1/cmake-build-opengles1/preserved-validation-logs/`). The `opengl1` "2/3"
and `opengl2` "2/6" figures exist only as lane-card prose; the `opengl1`/`opengl2` build trees retain
`LastTestsFailed.log` records that do not cover this test. This is stated rather than smoothed over.

### 5.4 Mechanism

From the failure text, the two survivors fail *asymmetrically*:

```
SurvivorA exited with code 1; output: JOINED wireid=1
SurvivorA: timed out waiting for the full 3-gamer roster to join

SurvivorB exited with code 1; output: JOINED wireid=2
RECONNECTED
SurvivorB: timed out waiting for the echo
```

`SurvivorB` — the **late** joiner — receives the whole roster atomically in its `ServerWelcome`,
reaches 3 immediately, observes the host's death and migrates, printing `RECONNECTED`. `SurvivorA` —
the **early** joiner — depends on a subsequent incremental `GamerJoined` broadcast, never reaches 3,
therefore never registers its migration handler and is never promoted, so `SurvivorB` waits forever
for an echo from a host that was never promoted.

The trigger is teardown timing. `RunMigrationHost` calls `Dispose()` the instant **its own** roster
reaches 3, and `PumpUntil` evaluates its predicate *before* `Update()`, so the host returns without
servicing ENet again. `TeardownSession` then disconnects every peer using
`enet_peer_disconnect()` — the **immediate** variant (`ENetHostHandle.cpp:129`) — rather than
`enet_peer_disconnect_later()`, the variant that waits until queued outgoing packets have been sent,
and destroys the host immediately after a single `Flush()`.

This explains the load inversion exactly: under load the gap between the join broadcast and the
teardown widens, and `SurvivorA` wins the race; on a fast idle machine the teardown crowds the
broadcast and `SurvivorA` loses it about half the time.

Production is **not** naively at fault for failing to flush: `HandleClientHello` does queue the
broadcast to every other peer and calls `Host.Flush()` before returning. The open question is
narrower — whether an immediate `enet_peer_disconnect()` during a graceful `Dispose()` may tear down
the peer before the receiver has dispatched an already-queued reliable join broadcast.

### 5.5 Disposition — **OUTCOME C, cause unresolved**

Reproduced and characterised, cause localised to one line, **not proven**. The single decisive
experiment — temporarily swapping `enet_peer_disconnect` for `enet_peer_disconnect_later`, measuring
20 isolated runs, and reverting — **was blocked by the environment's permission classifier and was
not performed**. It was not worked around. The file is unmodified.

Accordingly:

- The test is **retained as a visible residual**. It was not weakened, skipped, marked `WILL_FAIL`,
  hidden behind a backend skip, given a longer timeout, or "fixed" with a sleep.
- **This is not an integration regression.** It fails identically at the Batch 0 checkpoint
  (`30 073 ms`, recorded on the depthcrt card) and at phase-1, so it predates every Batch 1 lane.
- The characterisation on the lane cards is nonetheless **materially wrong** and is corrected here.

**One ticket is warranted** — not for a "known environmental flake" (the exclusion no longer applies,
because it is demonstrably not environmental) but for a reproducible ~50 % failure of a real
cross-process test whose ownership between harness sequencing and production teardown is unresolved.
See §9.

---

*(Sections 6–10 — consolidated build/test matrix, sanitizer matrix, attribution gate, findings and
checkpoint decision — are appended once all five backends and the sanitizer trees have completed.)*

---

## 6. Consolidated five-backend baseline at merged HEAD

Every figure below was measured on the **merged integration HEAD** (`9e6d62ed` plus the two
stabilization repairs of §3/§4), not on any retained pre-merge adaptation head.

**Common configuration.** Toolchain `g++ (Debian 14.2.0-19) 14.2.0` via `/usr/bin/c++`,
`CMAKE_BUILD_TYPE=Debug`, `CNA_BUILD_TESTS=ON`, `CNA_USE_CCACHE=ON` with
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, Unix Makefiles, `CNA_TEST_DISPLAY=:101`. GPU tests run
serially on `:101` with `SDL_VIDEODRIVER=x11`. `:0` was never used; `:99` was not required (no Wine
control in scope). **No build under `/tmp`, `/var/tmp` or `/dev/shm`; no `git clean`; no build
directory deleted or recreated.**

**Build directories — all five are NEW persistent in-repo directories in the integration worktree**,
because no compatible configuration existed there at merged HEAD (the pre-existing
`cmake-build-noxna{,-asan}` are EASYGL). They are `.gitignore`d and survive for the next session.

> **ccache measurement.** ccache delivered **0 hits and 368 misses** over the first backend build and
> behaved the same for the rest. `cmake/BackendSelection.cmake` issues
> `add_compile_definitions(CNA_BACKEND_<X>)` at directory scope, so *every* translation unit —
> including all of SharpRuntime — hashes differently per backend. **There is no cross-backend ccache
> reuse to be had**; each backend is a full ~1200-TU compile. This is worth recording because it sets
> the real cost of any future multi-backend sweep.

### 6.1 Build and test matrix

| Backend | Build dir | Selector | Build | Registered | Selected | Executed | Passed | Failed | Skipped | Not run | Dedicated CTest |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Stub | `cmake-build-stub` | `STUB` | exit 0, 0 errors | 5733 | 5733 | 5733 | 5642 | **18** | 73 | 0 | `Stub_Smoke` **1/1** |
| OpenGL ES 1 | `cmake-build-opengles1` | `OPENGLES1` | exit 0, 0 errors | 5733 | 5733 | 5733 | 5689 | **1** | 43 | 0 | **7/7** |
| OpenGL 4 | `cmake-build-opengl4` | `OPENGL4` | exit 0, 0 errors | 5737 | 5737 | 5737 | 5729 | **2** | 6 | 0 | **25/25** |
| OpenGL 1 | `cmake-build-opengl1` | `OPENGL1` | exit 0, 0 errors | 5737 | 5737 | 5737 | 5692 | **1** | 44 | 0 | **38/38** |
| OpenGL 2 | `cmake-build-opengl2` | `OPENGL2` | exit 0, 0 errors | 5737 | 5737 | 5737 | 5731 | **0** | 6 | 0 | **48/48** |
| **EasyGL control** | `cmake-build-noxna` | `EASYGL` | exit 0 | 5912 | 5912 | 5912 | 5906 | **0** | 6 | 0 | — |

**Every row's arithmetic is exact**: passed + failed + skipped + not-run = registered = selected =
executed. `--gtest_list_tests` shows no `DISABLED_` tests, so registered = selected.

Dedicated CTest suites across the five backends: **119 registered, 119 passed, 0 failed.**

> **Arithmetic hazard, avoided.** A raw `grep -c '\[  SKIPPED \]'` over a gtest log **double-counts**,
> because gtest prints each skip inline *and* again in its trailing summary. For the Stub run that
> grep returns 146; the authoritative summary line is **73**, and only 73 makes the arithmetic close.
> This is precisely the artefact that produced the opengles1 lane's retracted "87 skipped". Every
> figure in this table comes from the `[  PASSED  ] N tests.` / `[  SKIPPED ] N tests, listed below:`
> / `[  FAILED  ] N tests, listed below:` summary lines, never from an inline grep.

### 6.2 Runtime identities — measured, and honest about the host

| Backend | Evidence | Verdict |
|---|---|---|
| Stub | `SDL_WasInit(SDL_INIT_VIDEO) == 0`, `GetWindowInternal() == nullptr`; smoke run **7/7 with `DISPLAY` and `WAYLAND_DISPLAY` both unset**; `SupportsCapability` returns `false` unconditionally | **No display dependency, deterministic no-op, all capabilities false** ✅ |
| OpenGL ES 1 | `OpenGLES1GraphicsBackend initialized with OpenGL ES **OpenGL ES-CM 1.1 Mesa 25.0.7 (git-742a20f48c)**`, softpipe, via `scripts/opengles1-test-env.sh` against `~/deps/mesa-es1-install` | **Real ES 1.1 Common profile, not a desktop fallback** ✅ |
| OpenGL 4 | `OpenGL4GraphicsBackend initialized with **OpenGL 4.5 (Core Profile) Mesa 25.0.7-2+deb13u1**`; source requests 4.1 minimum with `SDL_GL_CONTEXT_PROFILE_CORE` | **Real core profile, satisfies the 4.1 minimum** ✅ |
| OpenGL 1 | Host grants `GL 4.5` **compatibility**. Reported as the driver's identity, not the backend's. Fixed-function proven **independently of the host**: `glCreateShader`/`glUseProgram`/`glCompileShader`/`glLinkProgram` = **0 occurrences across every file in the backend**, 14 fixed-function call sites, context requested as `1/1` with **no** profile mask | **Fixed-function path proven without relying on `GL_VERSION`** ✅ |
| OpenGL 2 | Context requested as `SDL_GL_CONTEXT_PROFILE_COMPATIBILITY`, major 2 minor 1 (`OpenGL2GraphicsBackend.cpp:2041-2043`); host grants the same 4.5 compatibility context. GLSL level proven **by construction**: **zero `#version` directives** in the backend, so every runtime-compiled program takes the GLSL 1.10 spec default | **2.1 compatibility request, GLSL 1.10 by construction** ✅ |

`OPENGL4` is the only backend whose GLSL level is *not* independently evidenced — it contains no
`glGetString(GL_SHADING_LANGUAGE_VERSION)` call, so the lane's "GLSL 4.50" figure remains an
unquoted external observation. GL 4.5 Core mandates GLSL 4.50 and its shaders are written to
`#version 410 core`, so the claim is consistent — but it is asserted, not measured (§2.2).

### 6.3 Failure classification — no integration regression

| Failure | Count | Classification |
|---|---|---|
| `TwoProcessLoopbackTest.HostMigration…` | 1 in Stub, ES1, GL4, GL1 runs; **passed** in the GL2 and EasyGL runs | **Known residual, ~50 % coin flip** (§5). Fails identically at the Batch 0 checkpoint ⇒ predates Batch 1 |
| `GraphicsDeviceCapabilityTest.Supports{ThreeD,DepthStencilBuffer,MultipleRenderTargets,OcclusionQuery,CustomEffects}` (Stub) | 5 | **Known residual, pre-existing.** Verified at the Stub lane's own merge `99ae7d11`: `SupportsThreeD` was *already* an unconditional `EXPECT_TRUE`, and the STUB arm exists only in the WireFrame chain. The Stub lane armed WireFrame and never these five. **Not a Batch 1 regression** |
| `TextureCubeTest.*` ×9, `Texture3DTextureCubeContentTypeReaderTest`, `XnbBuiltInReaderRegistrationTest`, `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile` (Stub) | 12 | **Expected status taxonomy.** The Stub contract answering truthfully: `NotSupportedException: "this graphics backend creates no cube-map texture resource, so a cube face's content cannot be stored"`. A resource-less backend cannot satisfy shared tests that assume a real resource |
| `CnjCapabilityMatrixTest.SpriteFontRejectsSourceFile` (GL4) | 1 | **Environmental flake.** `SDL_InitSubSystem(SDL_INIT_VIDEO) failed: x11 not available`; the *next* test in the same suite obtained a real GL 4.5 context, and the test passed **3/3 in isolation** immediately after. The documented "different victim each run" X11 blip |

**Zero failures classified as integration regression, stale harness, platform limitation or new
production defect.**

### 6.4 Principal control

The EasyGL/current-default control was run once at the final HEAD:
**5912 executed · 5906 passed · 6 skipped · 0 failed.**

That is **identical to the Batch 0 baseline (5912/5906/6/0)** and is the cleanest control result the
campaign has recorded — the `opengl4` card measured 5912/5905/6/1 and the `opengl1`/`opengl2` cards
5912/5904/6/2, both times with environmental failures. **Zero regressions attributable to any Batch 1
merge.**

---

## 7. History, signature and attribution gate

Scanned range: `d79214e7..HEAD`.

| Measure | Count |
|---|---|
| Commits in range | **205** (203 lane/adaptation + 2 stabilization repairs) |
| Merge commits | **9** — exactly the nine lanes, no tenth |
| Signature classes (`%G?`) | **`U` × 205**; `G` 0, **`N` 0, `E` 0, `B`/`X`/`Y`/`R` 0** |
| Distinct authors | **1** — `Robert Vokac <robertvokac@robertvokac.com>` |
| Distinct committers | **1** — `Robert Vokac <robertvokac@robertvokac.com>` |
| Trailers across the whole range | **1** — `Verified: 7/7 checks pass.` on `13f50353` (technical, not attribution) |

Per policy §8, `U` is this project's normal pass state (good signature from an uncertified key). Zero
`N` (unsigned) and zero `E` (unverifiable SSH) means **every adapted commit, every integration merge
and every directly-preserved commit is correctly signed**, and the 187 originally non-human-authored
commits across the campaign have all been re-authored.

### 7.1 Attribution sweep — multiline aware

The sweep flattens each commit's subject, full body, author and committer into a single whitespace-
collapsed string before matching, so a banned token split across a line break (`Co-authored-\nby`,
`this\nsession`) cannot hide. Tokens: `CC OK|Claude|Anthropic|Co-authored-by|generated by|authored
by|\bbot\b|\bagent\b|\bAI\b|this session|autonomous`.

**Result: 1 hit, 0 violations.**

| Hit | Verdict |
|---|---|
| `069b073c` — "docs(**CLAUDE.md**): document build-directory convention…" | **ALLOWED** — the literal tracked filename, explicitly permitted by policy §2.1 |

No prohibited AI attribution, no non-human author or committer, no `CC OK`, no agent-status prose,
no `Co-Authored-By:` trailer, no session-identifier URL anywhere in the range.

### 7.2 Narrative residue — recorded, not a §2 violation

A deliberately wider sweep (bare `session`, `handoff`, `another agent`, `in this pass`) returns a
larger set, which splits cleanly:

- **ALLOWED — factual `plans/plan_opengl2.md` "Session N" heading citations** (policy F3): `cd730f41`,
  `7f2143a5`, `6f263e3c`, `842dc3cd`, `ef1268ca`, `44de48ed`, and the `6f1bb99c` subject. These name
  a real section of a tracked planning document.
- **ALLOWED — provenance description in merge bodies**: `bc29a976` and `9e6d62ed` describe what was
  *removed* ("carry both prohibited attribution trailers, across two session identifiers", "the
  session-narrative phrasing nine commit bodies carried reworded at replay"). Describing a cleanup is
  not performing an attribution; `bc29a976` deliberately writes "a non-human identity" rather than
  naming it.
- **RESIDUAL — genuine §2.2 process narration that survived**: `b4550cf2`, `6f1bb99c`, `d14ccf2d`,
  `473f119d` (quoted in §2.2). Found independently by this session's sweep and by the opengl2
  provenance review. Two are in bodies the lane's own sweep *did* edit — it removed one phrase and
  missed a second.

These four are **not** attribution violations: no authorship claim, no AI identity, no session URL.
They are prose about how work was produced across sessions, which §2.2 says should have been
rewritten out. They sit in **merged** history, and this campaign forbids rebasing the integration
branch or amending a lane merge, so they **cannot be corrected without violating a stricter rule**.
Recorded here and on the opengl2 lane card as a permanent residual.

---

## 8. Sanitizer consolidation

Four persistent in-repository sanitizer trees were configured and built at merged HEAD, all with
`CNA_SANITIZE=address,undefined`, GCC 14.2.0, Debug, ccache, `:101`. None is under `/tmp`. Each
backend was run twice: once with LeakSanitizer active (to classify every report) and once with
`ASAN_OPTIONS=detect_leaks=0` as a control proving the checks genuinely execute under
instrumentation.

| Tree | Build | ASan errors | UBSan runtime errors | Leak reports | **Production allocation sites** | Control (`detect_leaks=0`) |
|---|---|---|---|---|---|---|
| `cmake-build-opengl4-asan` | exit 0 | **0** | **0** | 80 | **0** | **25/25 pass, exit 0** |
| `cmake-build-opengl1-asan` | exit 0 | **0** | **1** ⚠ | 114 | **0** | **38/38 pass, exit 0** |
| `cmake-build-opengl2-asan` | exit 0 | **0** | **0** | 144 | **0** | **48/48 pass, exit 0** |
| `cmake-build-opengles1-asan` | exit 0 | **0** | **0** | 41 | **0** | **7/7 pass, exit 0** |

### 8.1 How leaks were classified — and a counting trap

`LEAKS_ON_EXIT=8` on every tree, i.e. "0% tests passed", is a LeakSanitizer artefact: LSan exits
non-zero whenever *any* leak exists, including the driver's own. The `detect_leaks=0` control is what
establishes the tests actually pass — and all four do, unanimously.

A naive "count frames naming CNA source" metric is **misleading** and was discarded. It reports 27
frames for OpenGL 4 and 108 for OpenGL ES 1, which looks alarming, but it counts *call-path* frames
as well as allocation sites. The meaningful question is what sits at frame `#1` — the allocating
call. Classified that way:

| Tree | driver/system | test harness | **production (`src/`)** |
|---|---|---|---|
| OpenGL 4 | 75 | 1 | **0** |
| OpenGL 1 | 114 | 0 | **0** |
| OpenGL 2 | 144 | 0 | **0** |
| OpenGL ES 1 | 21 | 4 | **0** |

The residual frames name STL allocator inlining (`new_allocator.h:151`, `unique_ptr.h:1077`) for
containers owned by objects the harness allocates in `LoadContent()` and deliberately never frees;
their blocks are **Indirect** leaks whose stacks trace through `Game::Run()` to `main` in
`examples/…_test.cpp`. Verified directly rather than inferred from the lane cards.

**Zero CNA-originating leaks in any tree.**

### 8.2 OpenGL ES 1 — the previously-missing tree

The opengles1 lane recorded, honestly, that sanitizers **did not run** and that this was "an
omission, not a platform limitation". This session tested that claim and it was correct: a bounded
ES1 ASan/UBSan tree configures, builds and runs in the same native environment, driven by
`scripts/opengles1-test-env.sh`'s locally built ES1-capable Mesa. All 7 suites — covering context
bring-up, buffers/render-target resources, draw paths, pixel readback and context-loss/disposal —
run clean: **0 ASan errors, 0 UBSan errors, 0 production leak sites, control 7/7.**

**The prior omission is closed, and it is not repeated.**

### 8.3 Stub — explicit disposition

**No sanitizer tree was built for Stub, deliberately, and this is recorded rather than left silent**
(the stub lane card recorded no sanitizer disposition at all — §2.2).

Stub owns no GPU resources, allocates no backend objects, keeps "no bookkeeping of any kind"
(`docs/stub-backend.md`), and its overrides are empty bodies; `SupportsCapability` returns `false`
unconditionally. There are no Stub-owned lifecycle paths for ASan or UBSan to instrument. The shared
CNA core that a Stub build *does* execute — `Game`, `GraphicsDevice`, `SpriteBatch`, content
pipeline, `Color`/`BlendState` — is exactly what the four native trees above instrument, and it is
the same source. Stub additionally ran the full 5733-test `CnaTests` corpus and its `Stub_Smoke`
contract with no display at all.

A Stub sanitizer tree would therefore re-measure the shared core through a backend that adds nothing
to it. If that judgement is ever doubted, the cheap check is to build one — it needs no display.

### 8.4 The one finding: `REMED-GFX-220`

The single UBSan violation, under OpenGL 1 only:

```
include/Microsoft/Xna/Framework/Color.hpp:22:12: runtime error: member access within address
0x... which does not point to an object of type 'Color'   (note: object has invalid vptr, memory all zeros)
  #1 BlendState::BlendState()                   BlendState.cpp:22   blendFactor_(Color::White)
  #3 __static_initialization_and_destruction_0  BlendState.cpp:6
  #4 _GLOBAL__sub_I__ZN...BlendState8AdditiveE  BlendState.cpp:74
```

A **static initialization order fiasco across translation units**: `BlendState.cpp`'s four
namespace-scope presets copy `Color::White`, defined in `Color.cpp`. Order between translation units
is unspecified; when `BlendState.cpp` initializes first the source is zeroed `.bss`. `Color` is
polymorphic, so UBSan's vptr check catches it.

- **Not an OpenGL 1 defect and not a Batch 1 regression.** Introduced 2026-06-06 by `2345f8fc`,
  which predates the phase-1 checkpoint. It is latent in every configuration; manifestation depends
  on link order, which is why OpenGL 4 and OpenGL 2 link the same two translation units and show
  nothing. Reproduced 2/2 under OpenGL 1.
- **Uncovered by the suite.** `BlendStateTest.DefaultBlendFactorWhite` passes even in an affected
  binary, because it constructs a `BlendState` at runtime after static initialization completes.
  Nothing asserts the *static* presets' `BlendFactor`.
- **Evidence boundary.** Proven: the UB, the zeroed source at copy time, and that
  `GraphicsDevice.cpp:2448-2456` forwards the value to the backend as the blend colour. **Not
  proven:** an observably different rendered frame — `gdb` is unavailable here, so the stored value
  was not read back post-initialization and no pixel oracle was run against a `Blend::BlendFactor`
  mode. Whether exit criterion **E3** (supported-path silent wrong result) is satisfied is therefore
  **arguable, not demonstrated**.

Full ticket: `plans/plan_postaudit.md` §17.

---

## 9. New findings

**One**, and it is an independent production defect, not a carry-forward item:

| ID | Severity | Status |
|---|---|---|
| **`REMED-GFX-220`** | HIGH | **OPEN** — static initialization order fiasco between `BlendState` and `Color`; see §8.4 and `plans/plan_postaudit.md` §17 |

No ticket was raised for the README omissions, the missing name-table arms, the provenance record
corrections, or the sanitizer bookkeeping — all are carry-forward or expected-correction classes
that the campaign's rules exclude. The networking test is **not** filed as a defect ticket because
its ownership between harness sequencing and production teardown is unresolved (§5.5); it is
recorded as a visible residual with a corrected characterisation.

`REMED-CONTENT-007` / `-008` remain **OPEN, HIGH/P1**, re-verified still present at merged HEAD this
session (0 containment guards in `SongContentTypeReader.cpp`, `VideoContentTypeReader.cpp` and
`ContentManager.cpp`). They are outside every Batch 1 lane file set and remain non-blocking for this
checkpoint under the campaign's consistently-applied rule, but they are required before any
public security-clean claim.

---

## 10. Checkpoint decision — **BLOCKED**

Measured against the Batch 1 READY criteria:

| Criterion | Result |
|---|---|
| Provenance clean, all five lanes | ✅ |
| README public backend identity accurate | ✅ 27/27 |
| Backend-name coverage exhaustive | ✅ 27 arms, mutation-proven |
| Networking flake honestly disposed | ✅ Outcome C, characterisation corrected |
| No integration regression | ✅ principal control 5912/5906/6/0 = Batch 0 baseline |
| Five-backend current-HEAD baseline complete | ✅ 119/119 dedicated suites |
| **Required sanitizer evidence clean** | ❌ **one CNA-originating UBSan finding** |
| Signatures and attribution clean | ✅ 205 × `U`, 0 violations |
| Worktrees clean, `git diff --check` | ✅ |

**Decision: OUTCOME B — BLOCKED. No checkpoint tag was created.**

The trigger is Phase 6's requirement of *zero CNA-originating ASan/UBSan findings* and Phase 9's
*"sanitizer finds a CNA defect"*. It is **not** an integration regression — every Batch 1 quality
gate passed, and the principal control is the cleanest the campaign has recorded.

This is a deliberately literal reading of the stated criterion. The owner may reasonably decide that
a two-month-old latent defect, unrelated to Batch 1 and with an unproven render consequence, should
be classified non-blocking in the same way `REMED-CONTENT-007`/`-008` are — that is an owner
judgement, not one this session should make silently. What this session will not do is take the
checkpoint tag while a required gate is failing.

**Next task — exactly one, not begun:** fix `REMED-GFX-220` (remove the cross-translation-unit static
dependency in `BlendState.cpp:22` and add coverage asserting the static presets' `BlendFactor`), then
re-run the OpenGL 1 sanitizer tree and re-take the Batch 1 checkpoint decision.

Everything is preserved: all branches, all worktrees, all archive tags, the four user stashes, and
every build tree used above.

---

## 11. Checkpoint decision retaken — 2026-08-05

> §10 above records the **BLOCKED** decision exactly as it was taken, and it stays. This section is
> the separate, later decision made after its single blocker was removed. Nothing in §10 is revised.

### 11.1 What changed

`REMED-GFX-220` — the only entry on the blocker set — is **fixed and closed**. Full record:
`plans/plan_postaudit.md` §17.

The defect was reproduced before anything was modified, on the unmodified integration HEAD
`5a0ca509`. Decoding `.init_array` across all 38 OpenGL 1 sanitizer binaries showed the hazard is
carried by **exactly one** of them, `cna_test_opengl1_anisotropic_gl_state`
(CTest `OpenGL1_Anisotropic_GlState`), where `BlendState`'s initializer is entry **#167** and
`Color`'s is **#185**; in the other 37 `Color` runs first. That is why a latent, two-month-old defect
produced exactly one UBSan line in §8's matrix. The mechanism is **UBSan's `vptr` check**, not ASan
initialization-order detection, and the process **exits 0** — the diagnostic never failed a test.

Two things the original ticket left open are now settled:

- **The suggested fix does not compile.** `Color(UInt32)` is private; only `Color.cpp` may call it.
  The applied fix uses the XNA-public component constructor, `blendFactor_(255, 255, 255, 255)`,
  which packs to `0xFFFFFFFF` — byte-identical to `Color::White`.
- **The "not proven" wrong value is now proven.** A probe linking the same source twice, once with
  `BlendState.cpp.o` ahead of `Color.cpp.o` and once behind, forces the initialization order instead
  of accepting whatever the build produced. Pre-fix in the hazardous order all four presets read
  `0x00000000` (transparent black); post-fix both orders read `0xFFFFFFFF`. `gdb` was never needed.

A bounded same-pattern scan over the object code — transitive from each unit's static-init function,
because a direct-reference scan and a source grep both miss this very defect — found **one** further
instance of the identical root cause (`GestureDetector.cpp` reading `Vector2::Zero`) and three benign
vtable references. It is filed separately as **`REMED-GFX-221`** and deliberately **not** fixed here:
different subsystem, and its value consequence is benign because `Vector2::Zero` is `(0,0)`, which is
what the zeroed `.bss` already contains.

### 11.2 Re-validation at the fixed HEAD

| Gate | Result |
|---|---|
| Exact GL1 reproducer | **0 runtime errors**, with the hazardous link order **still present** (`BlendState #167` before `Color #185`) — the dependency was removed, not the ordering |
| Object-code invariant | `BlendState.cpp.o` no longer carries an undefined reference to `Color::White` at all |
| GL1 sanitizer matrix | **38/38 pass**, **0 UBSan** (was 1), **0 ASan**, 114 leak reports — **114/114 name `libGLX_mesa.so.0` at frame `#1`**, **0 name `src/`**; `detect_leaks=0` control **38/38, exit 0** |
| Batch 1 dedicated suites | **119/119** — OpenGL 1 38/38, OpenGL 2 48/48, OpenGL 4 25/25, OpenGL ES 1 7/7, Stub 1/1. Unchanged from §6.1 |
| Focused BlendState/state | `BlendStateTest` **39/39** (38 + the new preset test) |
| Additional sanitizer control | EasyGL `address,undefined` tree, 191 state tests across `BlendState`/`Color`/`DepthStencilState`/`RasterizerState`/`SamplerState`: **191/191, 0 UBSan, 0 ASan** — a second native configuration, different backend, proving the shared fix introduced no new initialization or lifetime issue |
| Sanitized hazardous-order control | The forced-order probe rebuilt **with** `address,undefined` against the sanitizer tree's own objects, `BlendState #95` before `Color #96`: all four presets `0xFFFFFFFF`, **0 UBSan runtime errors**, exit 0. The fixed code is clean under instrumentation *in the exact order that triggered the defect*, not merely in orders that never triggered it |

### 11.3 Principal control

Run from the **repository root**, which the `MediaLibrary` fixtures require (they redirect
`MediaLibraryPaths` at the relative `tests/assets/media/{music,pictures}` trees; run from the build
directory instead, those paths do not resolve and the suite dies part-way — a methodology trap worth
recording, not a defect).

| | Registered | Selected | Executed | Passed | Failed | Skipped | Not run |
|---|---|---|---|---|---|---|---|
| Batch 0 / Batch 1 baseline | 5912 | 5912 | 5912 | 5906 | 0 | 6 | 0 |
| **This run** | **5913** | **5913** | **5913** | **5907** | **0** | **6** | **0** |

The entire difference is **+1 registered / +1 passed**: `gtest_discover_tests` registers the one new
regression test. The six skips are the same six tests, unchanged. `5907 + 0 + 6 = 5913` exactly.
**Zero regressions.**

### 11.4 Networking — unchanged

`TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
**passed** in this run (715 ms).

**This changes nothing.** §5 measured it at 10/10 over 20 isolated runs — a ~50 % coin flip — so a
single pass is exactly what a coin flip produces half the time and is **not** evidence the cause was
found or fixed. The cause remains localised to one line but unproven, the decisive experiment
remains blocked by the environment, and the test was not modified, skipped, weakened or slept
around. **Disposition remains Outcome C: a visible, unresolved residual.**

### 11.5 Decision — **OUTCOME A · READY**

| Criterion | Result |
|---|---|
| `REMED-GFX-220` reproduced and fixed | ✅ execution proof pre- and post-fix |
| Exact GL1 sanitizer gate clean | ✅ 0 runtime errors, hazardous order retained |
| No CNA-originating sanitizer finding | ✅ 0 across GL1 matrix and the EasyGL control |
| 119-test Batch 1 matrix green | ✅ 119/119 |
| Principal EasyGL baseline acceptable | ✅ 5913/5907/6/0, difference fully explained |
| No new integration regression | ✅ |
| Networking honest Outcome C | ✅ retained, incidental pass not claimed as a fix |
| Provenance, signatures, attribution | ✅ |
| Worktrees clean, `git diff --check` | ✅ |
| `audit/` untouched | ✅ |

**Batch 1 is complete and stabilized. 9 of 21 lanes integrated, 12 pending. No tenth lane was
started.** One new finding, `REMED-GFX-221`, is open and non-blocking.
