# Plan — C API smoke stability

Branch `capi-smoke-stability`, cut from **`ab75e340e`** (`merge(TerminalCApiRepair): integrate the
terminal and C API repairs into next`). The requested starting point `2f6691384` was already merged
into `next`; `ab75e340e` is the `next` commit that contains it and is therefore the base.

Scope: make the C API smoke suite green. Two defect classes, one shared lifetime root cause and a
set of expectations that went stale while `-DCNA_BUILD_C_API=ON` did not build. Not a C API
redesign, not a renderer change, and explicitly not the limitations-generator backlog.

| Task | Subject | Status |
|---|---|---|
| CSS-1 | Baseline: reproduce and classify every red `^CApi` test at the starting SHA | ✅ |
| CSS-2 | Root-cause the process-exit crash under a debugger and under ASan/UBSan | ✅ |
| CSS-3 | Fix the lifetime defect at its root | ✅ |
| CSS-4 | Repair the stale smoke expectations, each against demonstrated canonical behaviour | |
| CSS-5 | Lifetime and shutdown regression tests, plus repeated-subprocess stress | |
| CSS-6 | Full validation: ABI, release gate, renderer invariants, TERMINAL, corpus | |

## CSS-1 — the measured baseline

Build reused: `cmake-build-debug/` (Debug, HEADLESS, `CNA_BUILD_C_API=ON`, ccache launchers). The
C API links and the static archive reports **4 055 exported `cna_*` symbols**, matching the ABI
baseline the previous branch recorded.

`ctest -R '^CApi'` — **104 tests, 88 pass, 16 fail**:

- **3 generator gates** — `CApiCoverageMatrix`, `CApiLimitations`, `CApiReleaseGate`. All three are
  the one pre-existing unmapped-symbol backlog the previous branch exposed and left open.
  **Out of scope by instruction**; this branch must not worsen them.
- **13 pure-C smoke tests**, below.

### The previous audit's split is wrong, and that matters

`plans/plan_terminal_capi_repair.md` recorded "six stale expectations and seven crashes". Measuring
each binary directly instead of reading ctest's single verdict gives a different and larger picture:
**eight crash**, and **twelve of the thirteen carry a real assertion failure as well**. The crash
happens at process exit, after `main` has already returned its failure code, so ctest reports only
the signal and the assertion underneath it is invisible in the summary. `CApi_GameSecondaryGraphics‑
DeviceContext` was filed as a stale expectation and is in fact both.

The consequence for this branch: fixing the crash does not make eight tests green. Each assertion
has to be classified on its own evidence.

| Test | Assertion failure | Crash | Classification |
|---|---|---|---|
| `CApi_GraphicsDeviceSmoke` | `GraphicsDeviceSmoke.c:1651`, exit 2 | — | |
| `CApi_Draw3DSmoke` | exit 2, no message | — | |
| `CApi_GameSecondaryGraphicsDeviceContext` | callback stage 6 → `CNA_Result` 3; frame → 9 | SEGV | |
| `CApi_VertexValueSmoke` | `VertexValueSmoke.c:382`, `validate_defaults()` | — | |
| `CApi_VertexBufferSmoke` | none observed | SEGV | |
| `CApi_IndexBufferSmoke` | `IndexBufferSmoke.c:505`, exit 1 | SEGV | |
| `CApi_EffectSmoke` | `EffectSmoke.c:180`, lifecycle stage 1 | SEGV | |
| `CApi_ModelMeshPartSmoke` | `ModelMeshPartSmoke.c:259`, lifecycle stage 1 | SEGV | |
| `CApi_MorphTargetSmoke` | `MorphTargetSmoke.c:197` | — | |
| `CApi_SkinnedModelSmoke` | `SkinnedModelSmoke.c:312`, lifecycle stage 1 | SEGV | |
| `CApi_TextureSmoke` | exit 3, no message | — | |
| `CApi_ContentSmoke` | `ContentSmoke.c:2391`, then `:2472` exit 2 | SEGV | |
| `CApi_DevicesSmoke` | `DevicesSmoke.c:750`, exit 1 | SEGV | |

Classification column filled in by CSS-4 as each is proven.

### Environment recorded at the baseline

Working tree clean at `ab75e340e`. Sibling repositories untouched and recorded; those already
carrying local modifications before this branch existed are `cna-car-simulator` (2),
`cna-java` (1), `house-simulator` (12), `mesh-craft` (3), `myra-cna` (2).

## CSS-2 — the crash, root-caused

AddressSanitizer names it exactly, and it is a **heap-use-after-free**, not the null/garbage
dereference the SIGSEGV suggested. The bad read happens one line earlier than the previous branch
recorded — at `UninstallPlatform`'s `std::find` (`Game.cpp:209`), not at `TransferPins`
(`CurrentPlatform.cpp:103`); the segfault was simply the first *unmapped* address reached after
several reads of freed memory had already gone unnoticed.

```
READ of size 8 at 0x502000008570
    #4  UninstallPlatform                    modules/runtime/src/Game.cpp:209
    #5  Microsoft::Xna::Framework::Game::~Game  modules/runtime/src/Game.cpp:347
    #6  ~CGame                               modules/c-api/src/CnaCApiRuntime.cpp:80
    #15 CNA::C::Detail::HandleRegistry::Slot::~Slot      CnaCApiDetail.hpp:558
    #22 CNA::C::Detail::HandleRegistry::~HandleRegistry  CnaCApiDetail.hpp:515
    #23 ~RuntimeState                        modules/c-api/src/CnaCApiRuntime.cpp:40
    #25 exit

freed by thread T0 here:
    #6  std::vector<CNA::Platform::IPlatform*>::~vector()
    #7  libc __run_exit_handlers

previously allocated by thread T0 here:
    #8  InstallPlatform                      modules/runtime/src/Game.cpp:195
    #21 cna_game_create                      modules/c-api/src/CnaCApiRuntime.cpp:613
    #22 main                                 tests/pure_c/VertexBufferSmoke.c:739
```

### Why the order is not a race but a certainty

Measured with breakpoints on each holder's first call, reading its guard byte to catch the call
that constructs it. First-touch order in a crashing run:

1. `GetRuntimeState()::state` — the C API handle registry, from `cna_game_create`
2. `PlatformStack()::stack` — from `InstallPlatform`, inside the `Game` that call is constructing
3. `SubsystemPins()::pins` — from `SetCurrentPlatform`, a few lines later

Function-local statics are destroyed in reverse order of construction, so the registry — which owns
the `Game` — is destroyed **last**, after both holders `~Game` needs. And this can never come out
any other way: the registry has to exist *before* the object it is about to store, and the holders
are created *by* that object's construction. Every ordering that could be blamed is the only
ordering the code can produce.

`Installed()` is the exception that proves the rule: a raw `IPlatform*` is trivially destructible,
registers no destructor at all, and so was still perfectly readable at the moment of the crash.

### Ownership, as the contract already defines it

`docs/c-api/OWNERSHIP.md` and `docs/c-api/HANDLES.md` answer this without needing a new decision:

1. **Who owns registered objects?** The registry, through `std::shared_ptr<void>` in `Slot::object`.
2. **Own or merely track?** Own. `Release` moves the pointer out and drops it outside the lock.
3. **When is it drained?** By the caller: *"must release it exactly once before runtime shutdown"*,
   and *"C code remains responsible for explicit release before shutdown so release failures are
   observable and leak tests remain meaningful."*
4. **Is static destruction a designed fallback?** **No** — *"C code must release resources
   deterministically; no garbage collector, finalizer or C++ destructor is assumed at the ABI
   boundary."* `~RuntimeState` running C++ destructors is an accident of the registry being a
   static with a non-trivial destructor, not a cleanup path anyone specified.
5. **Can callers destroy everything first?** Yes, and every green smoke test does.
6. **What if they do not?** Before this branch: the crash above. The contract says the handle leaks;
   it does not say the process dies.

That last point is what makes the eight crashes reachable at all. Every one of them **does** call
`cna_game_destroy` — but only after its assertions pass, and each bails out early on a stale
expectation, leaving a live game handle for the exit-time path nothing had ever exercised.

## CSS-3 — the fix: immortal ambient bookkeeping

The holders `~Game` unregisters itself from are **process-lifetime registries whose clients can be
destroyed at any point of process teardown**. That is a lifetime statement, and CNA has already
made it once, for this same file family and found by the same tool: `modules/platform/src/X11/`
`X11Error.cpp` keeps its display list and mutex *deliberately IMMORTAL* because
`~X11Connection` reached them after their destructors had run
(`plans/plan_native_platform_validation.md` NPV-0102). `AlsaLibrary`, `MixerEngine` and
`DevicesShutdownCoordinator` use the same idiom for the same reason. This branch applies the
established pattern one level up, to the state NPV-0102's comment already singles out as the thing
that outlives every ordinary static.

| Holder | File | Change |
|---|---|---|
| `PlatformStackMutex()` | `modules/runtime/src/Game.cpp` | immortal |
| `PlatformStack()` | `modules/runtime/src/Game.cpp` | immortal |
| `StateMutex()` | `modules/platform/src/CurrentPlatform.cpp` | immortal |
| `LazyDefault()` | `modules/platform/src/CurrentPlatform.cpp` | immortal |
| `SubsystemPins()` | `modules/platform/src/CurrentPlatform.cpp` | immortal |
| `Installed()` | `modules/platform/src/CurrentPlatform.cpp` | unchanged — trivially destructible already |

Why this is the safe class of fix rather than a workaround:

- **It removes the dependency instead of ordering it.** Nothing now depends on when the registry is
  destroyed relative to anything else, so there is no ordering left to get wrong — including for a
  future owner that is not the C API.
- **It is not a leak of C API objects.** Every registered object is still destroyed, deterministically,
  by the registry. What is never destroyed is six bytes-to-a-few-dozen-bytes of borrowed-pointer
  bookkeeping, reachable from static storage for the whole process — so LeakSanitizer, which treats
  statics as roots, has nothing to report either.
- **Explicit teardown is untouched.** `ResetCurrentPlatform()` still destroys a lazily created
  default platform on the spot. The only behaviour that disappears is the *implicit* destruction of a
  never-reset default at process exit — which is precisely the destruction that was reaching into
  already-dead state, and which no caller could observe.
- **It was not chosen over a deterministic alternative; there isn't one.** Draining the registry
  earlier needs a hook that runs before holders it cannot know about, and `atexit`'s LIFO order can
  only be aimed at holders that already exist at registration time.

`~Game` reaches nothing else: it calls `Dispose(false)`, whose `disposing == false` path only sets
`isDisposed_` and touches no static, and then `UninstallPlatform`. Member destruction afterwards
lands in `~IPlatform`, already covered by NPV-0102.

### Measured effect

Rebuilt `cmake-build-debug` and re-ran `^CApi`: **all eight SEGFAULTs become plain assertion
failures**, 16 failures still (3 generator gates + 13 smokes), none of them a crash. The same
reproducer under ASan+UBSan with `detect_leaks=1` reports nothing at all.

`Platform|Runtime|CurrentPlatform|Game` — 916 tests, 2 fail:
`XnaPipelineGenuineRuntimeBuiltFamilies` (in the standing pre-existing set
`plans/plan_terminal_capi_repair.md` records) and `CApi_GameSecondaryGraphicsDeviceContext`
(a CSS-4 stale expectation).

## CSS-5 — the regression test the suite did not have

`modules/c-api/tests/pure_c/TeardownLifetimeSmoke.c`, one binary with a mode per process because
what it asserts happens *after* `main` returns: the exit status is the assertion, and a mode that
passes its own checks and then dies in teardown fails.

Nothing covered this before. Every other smoke test destroys what it creates, and the eight that
crashed reached the fallback path only by bailing out early on an unrelated stale expectation — so
repairing those expectations alone would have turned the suite green with the defect still in place.

| Mode | Shape | Without CSS-3 |
|---|---|---|
| `explicit` | game + children created, all destroyed in order, double-destroy still refused | pass |
| `game-alive` | one frame run, game left alive | **SEGFAULT** |
| `game-alive-no-frame` | no frame, so no GraphicsDevice, game left alive | **SEGFAULT** |
| `children-alive` | game and two textures left alive | **SEGFAULT** |
| `partial` | one child destroyed, one left; game destroy correctly refused | **SEGFAULT** |
| `cycles` | three clean create/destroy rounds, then a fourth left alive | **SEGFAULT** |
| `device-alive` | standalone device, no game, left alive | pass |

The two that pass without the fix are the two that should: explicit teardown was never broken, and
a standalone device owns no `Game`, so it never reaches `UninstallPlatform`. Measured by rebuilding
this test against `ab75e340e`'s `Game.cpp` and `CurrentPlatform.cpp` and re-running it.

**Stress:** 200 subprocess iterations of each of the seven modes — 1 400 processes, 0 failures.

## CSS-4 — the thirteen assertions, classified

Every one is a deliberate C++ change that landed while `-DCNA_BUILD_C_API=ON` did not compile, with
one exception: `CApi_VertexBufferSmoke` is a **genuine wrapper defect** that the same window hid.
Each row's evidence is the commit that made the change, the plan row stating the reasoning, and a
C++ test that pins the new behaviour — a value that merely makes the test pass was not accepted as
evidence for any of them.

| Test | What failed | Class | Evidence | Fix |
|---|---|---|---|---|
| `GraphicsDeviceSmoke` | default `Viewport().MaxDepth` expected `1.0f` | stale | `a0a3bc8d4` SOFTWARE-201; `ViewportTests.cpp:21` `DefaultConstructorZeroFields` | expect `0.0f` |
| `GraphicsDeviceSmoke` | `Viewport::ToString` expected `0.250000` | stale | SOFTWARE-201 + `a24bd647c` SOFTWARE-203 `setprecision(7)`; `ViewportTests.cpp:352` | expect `0.25` |
| `GraphicsDeviceSmoke` | 16 vertex sampler slots on every profile | stale | `6c1d1c451` SOFTWARE-225: 4 under HiDef, **0** under Reach, and vertex fetch takes no `Color` | size the loop from the device's profile; expect the `Color` vertex bind to be refused |
| `GraphicsDeviceSmoke` | clearing depth/stencil accepted as `NOT_SUPPORTED` | stale | `08c9cae96` SOFTWARE-333; `BackBufferDepthStencilContractTests.cpp:482` | expect `INVALID_STATE` when the plane does not exist |
| `GraphicsDeviceSmoke` | unbound draw expected `CNA_RESULT_INTERNAL` | stale | SOFTWARE-209 missing-effect guard, scope settled by `ba7604944` SOFTWARE-322; `DrawUserIndexedPrimitivesTests.cpp:88` | expect `INVALID_STATE` |
| `Draw3DSmoke` | fixed three-plane clear | stale | SOFTWARE-333, as above | derive the mask from the device's `depth_stencil_format` |
| `GameSecondaryGraphicsDeviceContext` | same, on a secondary device | stale | SOFTWARE-333, as above | create that device with `DEPTH24_STENCIL8`, which is what it asks to clear |
| `VertexValueSmoke` | `VertexPositionColor` default expected white | stale | `5018ea222` SOFTWARE-240 (with `890eaf5d3`); FNA and XNA both leave it all-zero | expect all-zero |
| `VertexValueSmoke` | two `GetHashCode` results expected `0` | stale | `bb7d58a4a` SOFTWARE-241 replaced FNA's zero stub with XNA's word-XOR for the four classic types | assert equal values hash equally, the one property that holds for all of them |
| `VertexValueSmoke` | `VertexElement::ToString` expected `{{…UsageIndex: 3}}` | stale | `8bd5fd504` SOFTWARE-202; `VertexElementTests.cpp:190` | single braces, no space |
| `VertexBufferSmoke` | raw-with-options readback was all zeros | **defect** | see below | route to the raw C++ methods the header already describes |
| `IndexBufferSmoke` | 32-bit index buffers expected under Reach | stale | `6c1172e30` SOFTWARE-208; `GraphicsProfileResourceCeilingTests.cpp` | request HiDef first, as `GraphicsDeviceSmoke` already does for `OcclusionQuery` |
| `EffectSmoke` | clearing `CurrentTechnique` expected to succeed | stale | `585b45dcf` SOFTWARE-257; `EffectTests.cpp:802` `SetCurrentTechniqueRejectsNull` | expect `INVALID_ARGUMENT`, and release the alias the query now mints |
| `ModelMeshPartSmoke`, `SkinnedModelSmoke` | `vertex_count` of `0` expected to be accepted | stale | `6e8324327` SOFTWARE-204, which also re-documented the field as positive | ask for the three vertices the three indices need |
| `MorphTargetSmoke` | a tangent read on a target with no tangents expected refusal | stale | `2c08fdcca` BINDFIX-009, reported by cna-java | expect success with count `0`, which the next check in the same test already required |
| `TextureSmoke` | 6 elements for a 2×2 region; every non-`COLOR` type expected refused | stale | `14c815cb7` SOFTWARE-276; `Texture2DTests.cpp:1094` | exact totals, and accept the widths that divide the texel and total exactly |
| `ContentSmoke` | `Alpha8` expected to be undecodable | stale | `cedfc735c` SOFTWARE-275 gave the reader every classic format | use an out-of-range ordinal, as the C++ mirror already does |
| `DevicesSmoke` | desktop expected `DeviceType::Device` | stale | `cb2c902087` SAMPLE-061 and the rewritten `Environment.hpp` contract | ask `cna_platform_get_is_mobile_ext`, the discriminator the C++ asks |

### The genuine defect: raw vertex uploads stopped being raw

`cna_vertex_buffer_set_data_raw_with_options` and its `_at_` sibling routed through the *generic*
`DynamicVertexBuffer::SetData<TVertex>` template instantiated with `std::uint8_t`. The wrapper's own
comment said why that was equivalent: "passing `startIndex` 0 makes `data` the exact bytes to
upload". `d14d9482b` (SOFTWARE-249) ended that, correctly — it restored XNA's generic semantics,
where tightly packed `sizeof(T)` source elements are written into buffer locations separated by
`vertexStride`. With `T` a byte, that copies `vertexCount` **single bytes at `vertexStride`
spacing** instead of the `vertexCount * vertexStride` contiguous bytes
`vertex_resources.h:402` documents. The vertices read back as zeros; the raw route's
vertex-boundary check and its zero-count no-op were bypassed too.

The C contract was right and the C++ change was right; only the wrapper's choice of door was wrong.
`SetDataRawWithOptions`/`SetDataRawAtWithOptions` are `protected` on `VertexBuffer`, so
`DynamicVertexBuffer` gains two public CNAEXT forwarders — the streaming counterparts of the public
`SetDataRaw`/`SetDataRawAtEXT` pair that already exist for the same reason — and both routes call
those. No C signature, symbol or constant changes.

### Two documentation defects repaired alongside

- `effects.h:1429` promised that `CNA_INVALID_HANDLE` clears the current technique. SOFTWARE-257
  made that a refusal; the Doxygen now says so.
- `graphics_device.h:726,:743` promised `CNA_RESULT_NOT_SUPPORTED` for a missing depth/stencil
  plane. SOFTWARE-333 made it `CNA_RESULT_INVALID_STATE`; three of the stale C assertions were
  written against that promise, so the header was the thing that was wrong.

### Two suites that failed silently now say where

`VertexBufferSmoke.c` printed nothing at all on failure — the crash then overwrote its exit code,
so the defect above left no trace anywhere. It and `GraphicsDeviceSmoke.c`'s stage chain now use
`CnaTestReport.h`, which is what found the fifth `GraphicsDeviceSmoke` expectation.

### Coverage accounting

The two new public C++ methods are bound by the two C routes that call them, so they are recorded
in the existing `vertex-buffer-raw-with-options` rule rather than left unmapped. Measured either
side of the change, the unmapped total is **3 007 planned rows over 14 tasks — identical**. The
pre-existing generator failures are untouched, and `docs/c-api/COVERAGE.md` cannot be regenerated on
this branch for the same pre-existing reason (`--write` hits the planned-row-owner error), having
already been stale by roughly 3 200 symbols at the branch point.
