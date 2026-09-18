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
| CSS-2 | Root-cause the process-exit crash under a debugger and under ASan/UBSan | |
| CSS-3 | Fix the lifetime defect at its root | |
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
