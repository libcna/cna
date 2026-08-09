# Lane card — `magnum` · ✅ **INTEGRATED 2026-08-06** · merge `e7d46c4c` — the eleventh lane, Batch 2 closes 2 of 2

> **Outcome.** Verified, recreated, validated and merged in one session. The backend had never
> been checked out, built or run; it now builds against the pinned Corrade/Magnum revisions,
> renders on a real GL context, and passes the full corpus with zero backend-owned failures.
> Validation found **two real defects, both fixed in-lane**: the adaptation's own
> declaration-guard comparing split multi-stream declarations in the wrong space (caught by the
> shared oracles this lane armed), and a production `sizeof(T)`-fold sprite-flush heap overread
> the lane had carried invisibly since its first commit (caught by ASan, root-caused to Corrade's
> typed-pointer `ArrayView<const void>` constructor scaling its size argument, and the true cause
> of a deterministic radeonsi SIGSEGV). **Nothing was pushed; no other lane was begun; `audit/`
> untouched.**

| Field | Value |
|---|---|
| Logical lane | `magnum` |
| Refs | **`refs/remotes/origin/claude/cna-magnum-gr-backend-211xsx` — remote-only**, unchanged |
| Original head | `9b903db8` — unchanged locally and on `origin` |
| Archive tag | `archive/preintegration/magnum-20260804` → `9b903db8` · GPG **Good** · unchanged |
| Real fork point | `2338b44f` on `feature/audit` — audit-stacked, **248 behind** the integration head at adaptation |
| Own commits / files | **13 / 45** · original `+9650, −16` |
| Adaptation branch / head | `adapt/magnum` → **`b7fe9b24`** (worktree `cnaintegration-magnum`, retained) |
| Adapted commits | **19** (13 replayed + 3 obligations + 2 validation-driven fixes + 1 docs) |
| Merge commit | **`e7d46c4c`** — signed, `--no-ff`, zero conflicts, merged tree byte-identical to `adapt/magnum` |
| History class | AUTHOR/TRAILER CLEANUP REQUIRED — total (13/13), **re-verified at the object level** |
| Path taken | **ADAPTATION** |

---

## 1. History — re-verified at the object level

All 13 commits authored **and** committed by `Claude <noreply@anthropic.com>`; all 13 SSH-signed
by the campaign's one known non-maintainer ed25519 key (`…rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs`);
0 maintainer-PGP, 0 genuinely unsigned; 13× `Co-Authored-By: Claude Opus 5` and 13×
`Claude-Session:` (one session id); 0 merges, 0 WIP/fixup, 0 status-only commits. The multiline
sweep found no body-level narration beyond status text.

**Message cleanup at replay:** both trailers stripped 13/13; the per-commit corpus-count status
lines dropped (`CnaTests reports 5750/5774/5782/5791 passing` — stale and re-derived by this
session, the `opengles1` "87 skipped" lesson); verification refrains reworded into test-design
descriptions; one process phrase (*"Investigated while looking for the next task"*) removed; and
commit 1's *"CNA's fifteenth graphics backend, and its first desktop-OpenGL one"* dropped — false
at the head, where OPENGL1/2/4 landed first and MAGNUM is the twenty-ninth identity.

## 2. What the lane is

| Field | Value |
|---|---|
| Public identity | **`MAGNUM`** — `CNA::GraphicsBackendType::Magnum`, name `"MAGNUM"`, the **29th** identity |
| Selector | `-DCNA_GRAPHICS_BACKEND=MAGNUM`; option `CNA_BACKEND_MAGNUM`; define `CNA_BACKEND_MAGNUM` |
| Architecture | rendering through **Magnum's typed `Magnum::GL` wrappers** on a **desktop OpenGL 3.3 core** context; SDL3 owns window and context, `Platform::GLContext` adopts the current one |
| Underlying API | desktop OpenGL only (Magnum's Vulkan is not used); one CNA backend, not several |
| Platform context | `GlxContext` (Linux default) / `EglContext` (`CNA_MAGNUM_USE_EGL=ON`) / `WglContext` / `CglContext`; Emscripten is a configure-time `FATAL_ERROR` |
| Dependency | **two repositories**: Corrade pin `783e4e48`, Magnum pin `5a742464` — both MIT, both verified = their upstream master tips (fresh clones into `~/deps/corrade` + `~/deps/magnum` reproduce them exactly); routes `CNA_MAGNUM_ROOT` → system → FetchContent, with `FETCHCONTENT_SOURCE_DIR_{CORRADE,MAGNUM}` proven for offline builds; static, deprecated API off, only GL + one platform-context component built |
| Binaries / patches | none vendored, none carried — no upstream patch was needed |
| Tests | 8 registered pixel CTests (label `Magnum`) + a context-free GTest suite inside `CnaTests` |

## 3. Contract

Supported and pixel-verified: context/present, every Clear combination, Texture2D/Cube/3D (RGBA8),
RenderTarget2D/Cube with MSAA resolve + mip regen + MRT×4 (multisample storage kept inside a set),
SpriteBatch, runtime-compiled `ShaderEffect` GLSL (per-stage declared `#version`), all seven stock
effects + both `PreferPerPixelLighting` families, indexed/non-indexed/instanced draws, per-stream
offsets and instance frequencies, multi-stream re-slotting (ordinary and instanced), full state set
incl. real `glPolygonMode` wireframe, occlusion queries, VAO caching keyed on monotonic buffer
identity + declaration revision.

Refused deterministically, device left usable: a custom declaration a stock stride-template would
misread (`RequireFaithfulDeclarationEXT`, asymmetric, split streams lifted by `combinedByteBase`);
a stock draw whose layout selects no program (**was a silent no-op** — now
`System::NotSupportedException` naming stride and flags). Declared boundaries: `SurfaceFormat`
Color-only storage; `BlendState.MultiSampleMask` default-only (Magnum wraps no sample-mask state);
context-loss keeps `IGraphicsBackend` defaults.

Capabilities: exhaustive **eleven-member switch, no default arm** (was `default: return true` — the
ES1 hazard's shape; every post-fork member silently answered true, `Instancing` correctly only by
accident). All eleven answers proven at runtime by probe (17/17) and the shared suites.

## 4. Interface drift and unions

**Compile probe: zero errors** — same fork as `wicked`, after the whole stale-fork drift set; the
only lane class needing no interface adaptation. The registration union (the **eighth** of the
campaign) kept all 28 identities token-exact and added MAGNUM: `BackendSelection.cmake` (29 in
docstring/STRINGS/guard/dispatch), `GraphicsBackendType.hpp` (29 enum + `#elif` + name table),
`ExpectedNameFor()` arm (a removed MAGNUM arm fails the guard), compile-definition count,
instanced-oracle gates, RTCube-SetData acceptance arm, `BackendLibraries`/`CMakeLists`/`UnitTests`,
README, and CLAUDE.md — whose selector list the lane had rewritten with its fork-era **15** values
(its `DX3` meaning free-direct); rewritten to the full current 29. The lane's re-gate of the flat
"no wireframe" assertion was **fully superseded** by the head's REMED-GFX-209 contract — that file
resolved to exactly HEAD; MAGNUM instead joined `WireFrameTriangleOracle.hpp`'s measured rendering
set. The `preferPerPixelLighting` doc-comment correction was **re-measured at the head** before
replay: the honouring set is D3D9, D3D11, D3D12, WebGPU, Vulkan, bgfx, EasyGL, OpenGL4 (+ Magnum),
not the lane's five-name list.

## 5. Disposition of every original commit

| # | Original | Adapted | Disposition |
|---|---|---|---|
| 1 | `66a4e93b` | `6e988c8e` | **TRANSFERRED** (+ registration union; capability-test edit superseded by head) |
| 2–13 | `5a13c0df` … `9b903db8` | `5ef66a9f` … `896c06fc` | **TRANSFERRED**, 1:1 in order |
| — | *(new)* | `a7c2c52c` | ADDED — exhaustive capabilities, declaration guard, stock-null refusal |
| — | *(new)* | `46a466cb` | ADDED — wireframe pixel-oracle arm |
| — | *(new)* | `d832ec72` | ADDED — corpus-glob exclusion (the WICKED-71 class, applied prospectively) |
| — | *(new)* | `901cc42c` | ADDED — guard fix: split declarations compared in combined space |
| — | *(new)* | `f9b3fd41` | ADDED — sprite-flush ArrayView element/byte fix (MAGNUM-65) |
| — | *(new)* | `b7fe9b24` | ADDED — integration-validated docs |

**Zero OMITTED, zero SUPERSEDED commits.** Range-diff pairs 13/13 in order; **31 of 45 files
byte-identical at the replay boundary** — the 14 differing are exactly the pre-existing
union/drift surface. Attribution sweep over the adapted range: one hit, the **allowed** literal
filename `CLAUDE.md` (policy §2.1). Signatures: 20/20 (19 commits + merge) verify Good.

## 6. The two validation-driven fixes

**6.1 Guard space defect (`901cc42c`, adaptation-owned).** The obligation commit's guard
concatenated per-stream declared elements as if offsets were combined-space; XNA split
declarations are stream-local (both streams declare offset 0), so every mixed-stream stock draw
was refused with a phantom "claim the same bytes". Caught by the armed
`InstancedDrawMultiStreamTest`/`OrdinaryDrawMultiStreamTest` suites on their first run (9
failures); fixed by lifting each element by its stream's `combinedByteBase` — the mapping
`MapCombinedOffsetToStream()` inverts; 9/9 pass after.

**6.2 `MAGNUM-65` sprite-flush heap overread (`f9b3fd41`, original-lane production defect).**
Corrade's typed-pointer `ArrayView<const void>` constructor takes an **element count** and scales
by `sizeof(T)`; `FlushBatch()` passed byte counts → a `sizeof(Vertex)`-fold (and 2× for indices)
overread past the pending vectors **on every flush since the lane's first commit**. It rendered
correctly regardless (GL stored the oversized copy; the draw read only the real prefix), which is
why 8/8 pixel suites and two full corpus runs were green over it. Surfaced twice independently:
a **deterministic SIGSEGV inside `glNamedBufferData` on radeonsi** (three `coredumpctl` dumps,
`GuideTest.RenderPendingKeyboardInput…`), and ASan's heap-buffer-overflow (READ of 4096 from a
128-byte region — 4 vertices × 32 bytes × 32 = the exact double-scale arithmetic) on the first
sanitized flush. Every other `ArrayView` construction in the backend audited: all pass `void*` or
byte-typed pointers and are correct. Fixed by passing element counts; proven by ASan going clean.
The radeonsi reproducer was deliberately **not** re-run — it needs the real display, which this
campaign's validation environment excludes.

## 7. Validation at the merged content

| Gate | Result |
|---|---|
| Build | Corrade+Magnum+backend+CNA+`CnaTests`+all harnesses from the pins, **0 errors**; first build and first execution in this backend's history |
| Runtime identity | `4.5 (Core Profile) Mesa 25.0.7` · **llvmpipe** (LLVM 19.1.7) on Xvfb `:101`, `SDL_VIDEODRIVER=x11` — the campaign's software-rasterizer environment |
| Dedicated suites | **8/8** pixel CTests (39 assertions), re-confirmed inside every corpus run |
| Lifecycle probe | bare / query-only / repeated / early-disposal — **4/4 clean exits**; the WICKED-78 teardown-abort class is absent |
| Guard probe | **17/17** — all 11 capability answers, custom-declaration refusal with the target proven unmutated against a lighting control, stock acceptance (not over-wide), unlisted-stride refusal |
| Corpus run 1 (discovery) | 5843 · 5822 · 15 failed — classified: 9 = guard space defect (fixed); 2 SEGV = MAGNUM-65 on the real display (fixed); 1 not-run = unbuilt harness (built); 3 = pre-existing audio/networking classes |
| **Corpus official (run 3, fixed content)** | **5843 registered/selected/executed · 5835 passed · 2 failed · 6 truthful skips · 0 not run · 0 aborts/timeouts** (1114.6 s) |
| The 2 failures | the known networking Outcome-C flake, and one wall-clock audio race — **both control-classified on the pre-Magnum principal binary** (2/12 control failures with shuffling victims) |
| The 6 skips | 4 sensor + WireFrame-refusal (inapplicable: truthful `true`) + Texture3DUnsupported (inapplicable: genuine Texture3D) — the Wicked set exactly |
| Sanitizers (`cmake-build-magnum-asan`, address+undefined) | 8/8 suites, 39/39 assertions, **0 ASan errors, 0 UBSan runtime errors** post-fix (the pre-fix run is what caught MAGNUM-65); leaks 100 956 B / 449 allocs in three records, **all rooted in `libGLX_mesa`, zero CNA frames**; `detect_leaks=0` controls all exit 0 |
| Principal EasyGL control (merged head, `cmake-build-noxna`, incremental) | **6212 registered · 6203 passed · 5 failed · 4 skipped.** The fresh reconfigure surfaced **+299 EasyGL dedicated registrations** (in `cmake/Tests/EasyGLTests.cmake` since 2026-08-02, configure-gated out of every earlier 5913-corpus baseline). Inside the baseline-comparable range the failures are exactly the two known pre-existing classes (networking 637, audio 1628) — **zero regressions**. The three failures in the never-measured range are mechanism-classified as EasyGL-owned or upstream, orthogonal to this lane's diff: `EasyGL_DeviceValidation` ("SetVertexBuffers(16) does not throw"), `EasyGL_GraphicsDevice_ReferenceStencil` (override reference 0x99 not rejected), `easy-gl-resource-smoke-tests` (an assertion inside the easy-gl sibling project's own smoke suite). All three handed to the Batch 2 stabilization inbox |
| Provenance | original ref and archive tag unchanged; merged tree **byte-identical** to `adapt/magnum`; 11 lane merges; all four checkpoint tags ancestors; `git diff --check` clean |

## 8. Session environment notes

- **Thermal.** The machine could not hold one full-boost core (idle 47–54 °C; a 19 s unquoted
  busy-loop reached **88.8 °C** — that control burst was this session's one thermal misstep).
  All heavy work ran under a `systemd-run --user` scope with **CPUQuota=40 %** — dry-run-proven
  (47.1 s vs 18.8 s ≈ 40 % duty; cgroup containment is structural), calibrated to a **68 °C
  plateau** (58–73 °C across builds and test runs), with an armed 82 °C alarm and two
  stop-cool-restart cycles early on (80.2 °C at `-j2`, 83.5 °C at `-j1`). Parallelism never
  exceeded `-j2`; effectively `-j1`+quota throughout. No SIGSTOP was ever sent to a test.
- **Display.** Corpus run 1 unintentionally inherited the shell's `DISPLAY=:0` for tests without
  a per-test display property — they ran on the **real display/radeonsi** (a policy breach,
  unwitting, and also what exposed MAGNUM-65). Runs 2 and 3 and all probes forced `:101`/Xvfb.
  `:0` was never used deliberately; `:99` was not needed (no Wine route).
- The audio wall-clock class and the networking Outcome-C flake shuffle victims between runs on
  this machine; the audio class was control-proven pre-existing on the integration head's
  principal binary.

## 9. Residuals

Unchanged and not claimed: `REMED-GFX-221` (LOW), `REMED-CONTENT-007`/`-008` (HIGH/P1 — this lane
touches no `Content/` file, re-checked), networking Outcome C, Direct2D OWNER-FROZEN. Lane-local:
real-display/real-GPU verification remains the standing declared boundary (`MAGNUM-59`'s
cross-backend parity run not started; the radeonsi path exercised only far enough to fault and
then root-cause MAGNUM-65); `MAGNUM-54` (SurfaceFormat beyond Color), `MAGNUM-55`
(MultiSampleMask, blocked on Magnum wrapping no sample-mask state), `MAGNUM-58` (context-loss
channel) remain open plan rows. One observation recorded without a defect claim: `SetDataRaw` on
a declaration-constructed buffer renders nothing where typed `SetData` renders (shared-layer
route distinction predating this lane; its declaration-less use, which the lane's own SkinnedEffect
route relies on, works and is corpus-covered).
