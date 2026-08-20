# Lane card — `direct2d` · **INTEGRATED 2026-08-08**

| Field | Value |
|---|---|
| Logical lane | `direct2d` |
| Original refs | `refs/heads/feature/direct2d` and `refs/remotes/origin/feature/direct2d` — both unchanged at **`9b17e783e74e87a3f23b9cc47bd3c7cd6dad9d81`** |
| Archive tag | **`archive/preintegration/direct2d-20260804`** → `9b17e783` · sole Direct2D archive · annotated · GPG-good · unchanged |
| Fork/base | **`a7a49e3dc135cd3394b04dbc761123584b4e1d45`** |
| Historical commits / files | **48 / 25** · zero merges · 48/48 Robert-authored and GPG-good |
| Replay head | **`3ad403e2470567af064b0b39c42605d969cf8925`** — 48 chronological recreated commits |
| Adaptation head | **`1b740d962d85bb648d6ae2997bba9b1ba09dfd87`** — 55 signed commits: 48 replay + 7 adaptation |
| Integration merge | **`7af760bee2896960270cfd7bd6c822b96c13be94`** · signed `--no-ff` · parents `c805fd73` / `1b740d96` · tree equals adaptation |
| Integration head after documentation precision fix | **`21b1fcd17`** — signed `D2D-54` logical-pixel/DPI wording only |
| Historical worktree | `/rv/data/development/github.com/openeggbert/cnadirect2d` — clean |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-direct2d` — clean |
| Subsystem | Genuine Windows-only Direct2D 1.1, permanently **2D-only** |
| Public identity | **39th** CNA backend: `DIRECT2D`; no fallback |
| Shared production delta | Backend identity/build registration only; current `GraphicsDevice.cpp`, `IGraphicsBackend.hpp`, and both shared `Texture2D` files are byte-identical to `c805fd73` |
| Conflict class | MEDIUM — four semantically adapted replay pairs, all accounted |
| Final status | **READY / INTEGRATED** — no known supported-path Direct2D production defect remains |

## 1. Owner freeze and bounded unfreeze

The historical owner statement remains exact: **“The project owner has explicitly frozen this
branch: no further development will be performed on it.”** No deeper owner motive was recorded and
none is inferred. The 2026-08-08 owner authorization superseded that status only for integrating
and stabilizing this existing lane against the post-audit head. It did not authorize feature
expansion, API redesign, LLGL, Metal, modularization, or broad modernization.

The frozen-head record contained two arithmetic errors, corrected here without rewriting the
original branch:

- exactly **three**, not four, commits followed `6cd6ad06`: `701ea9e2`, `09411e77`, `9b17e783`;
- `plans/plan_direct2d.md` had **128 rows = 32 `✅` + 35 `🟨` + 61 `⬜`**, so its own status rules made
  **96 incomplete**, not the stale 88 repeated by earlier planning prose.

### Freeze-reason disposition

| Class | Recorded reason | Disposition |
|---|---|---|
| **A — resolved by current architecture** | Direct2D must not grow a second 3D renderer; the historical NET=OFF ENet failure was infrastructure, not a reason to emulate another backend | The current unsupported-3D policy preserves the 2D boundary. `REMED-BUILD-019` aligns NET=OFF tests with the omitted Net/GamerServices modules |
| **B — still technically relevant** | The frozen lane carried a large mixture of correctness, evidence, performance, CI, and refactor work | Supported-path defects were repaired or converted to tested deterministic rejection. Remaining rows are classified in `plans/plan_direct2d.md` as native/external evidence, stronger fault coverage, or nonblocking process/performance/refactor work |
| **C — historical/process-only** | Direct2D moved during exit reconciliation, so integration lacked a stable source | Resolved by freezing `9b17e783` and preserving it behind the sole signed archive tag; neither ref moved |
| **D — unsupported-host/environment** | Wine/Proton cannot prove native built-in effects/composites, physical presentation/DPI, adapter behavior, debug-layer/live-object output, or physical-Windows lifetime | Still external. Wine/Xvfb results are reported only as compatibility runtime evidence |
| **E — owner governance/sequencing** | Explicit owner freeze and deferred Group G sequencing | Boundedly superseded for this lane only by the current authorization; the historical governance record remains intact |

No record attributes the freeze specifically to Wine, COM, ENet, D3D dependencies, or a hidden
technical motive.

## 2. Provenance and adaptation

All 48 original commits were replayed chronologically. Author name, email, author date, subject,
and body compare byte-for-byte with the original sequence. `git range-diff` maps **44 `=` and four
`!`** pairs; the four content adaptations are confined to current backend registration/build/test
context and the current shared Texture2D authority. Historical Texture2D changes were not restored:
the current `REMED-GFX-223` implementation is authoritative.

The seven additional signed commits are:

| Commit | Purpose |
|---|---|
| `c10d391e0` | `REMED-BUILD-019` — exclude exactly the disabled Net/GamerServices test-module roots under NET=OFF |
| `7d376eff6` | `D2D-21` — adapt the backend to current truthful 2D contracts |
| `7f30aa68b` | `D2D-134` — fix the parity test callback lifetime |
| `02b396ac2` | `D2D-118/-119` — add the dedicated Direct2D unit gate/runner |
| `3d7acd166` | `D2D-131` — document the actual support boundary |
| `6fc7e396e` | `D2D-135` — commit blend state only after embedded factor validation |
| `1b740d962` | `D2D-136` — include Direct2D in the exactly-one-backend macro oracle |

All **55/55** adaptation-range commits and merge `7af760bee` verify GPG-good with fingerprint
`255C 69CC 1D09 CA54 EF0C C9DF FB9C E8E2 0AAD A55F`. Attribution and trailer sweeps are empty.
The final delta from `c805fd73` is 23 files, `+7147/-18`; `audit/` remains tree
`168c9b668763b78e63106e27d942a76d2457f41d`.

## 3. Genuine implementation and supported contract

Application drawing uses `ID2D1DeviceContext`. A BGRA-capable D3D11 device and DXGI 1.2
flip-sequential HWND swap chain host Direct2D and present only; they do not implement CNA draw
calls. The backend uses no DirectWrite, WIC, SDL Renderer, GDI, EasyGL, or D3D fallback. SDL3's
`SDL_PROP_WINDOW_WIN32_HWND_POINTER` supplies the HWND.

The Windows target is Direct2D 1.1 / DXGI 1.2, verified here as x64 MinGW GCC 14. Native workflow
coverage is MSVC x64; no i686 Direct2D claim is made.

| `GraphicsCapability` | Result | Evidence class |
|---|---:|---|
| `ThreeD` | false | deterministic rejection / policy-aware safe stubs |
| `DepthStencilBuffer` | false | deterministic format/state rejection |
| `MultiSampleAntiAliasing` | false | 0/1 convention only; larger requests reject |
| `MultipleRenderTargets` | false | deterministic rejection |
| `AnisotropicFiltering` | true | D2D1.1 native mode; unit and Wine pixel coverage |
| `WireFrame` | false | deterministic rejection |
| `OcclusionQuery` | false | deterministic rejection |
| `CustomEffects` | false | deterministic rejection |
| `Texture3D` | false | deterministic rejection |
| `MultiStreamVertexInput` | false | no 3D stream consumption |
| `Instancing` | false | no 3D stream consumption |
| `StencilBuffer` | false | no real buffer; state rejects |
| `AdditiveBlending` | false | XNA SourceAlpha/One rejected rather than approximated as D2D One/One |

PBR is not a current capability-enum member and is outside this 2D backend.

`Texture2D` supports level zero and explicitly authored mips, including real MipLinear blending
between initialized levels. `RenderTarget2D` supports a single Color target at level zero,
readback, upload, and sampling after unbind. Mipmapped RTs reject: the historical NPOT generator
was removed because D2D-78 proved that it omitted source quadrants. Present with an active RT and
self-sampling an active RT reject deterministically.

The only surface contract is public RGBA8 `SurfaceFormat::Color` mapped to native premultiplied
BGRA8. Depth is `None`; there is no real depth/stencil resource. Odd widths, padded pitch,
asymmetric channels, nontrivial alpha, repeated update, short pitch, overflow/extreme dimensions,
partial readback, and failure transactionality are covered. `Opaque`, `AlphaBlend`, and
`NonPremultiplied` are supported; arbitrary blend state and Additive reject.

SpriteBatch supports crop, origin, flips, rotation, nonuniform scale, batch transform,
Clamp/Wrap/Mirror, viewport, and scissor. D2D-82's real flipped-origin transform bug is fixed and
pinned by an eight-point pixel oracle. Ordinary/indexed/instanced 3D draw families are never
emulated and never consume offsets or streams.

CNA sprite, viewport, and scissor coordinates are logical framebuffer pixel units, not Windows
DIPs. The D2D context/bitmaps use 96 DPI so one D2D unit equals one logical target pixel.
`NativeBackBuffer` is 1:1 with client pixels; other modes map logical pixels during presentation.
Empty-frame resize is tested. Physical multi-monitor DPI and desktop capture remain external
Windows gates.

COM ownership uses WRL `ComPtr`; deferred image/brush/effect resources live through `EndDraw`.
Registered textures recover from RGBA shadows, RTs recreate transparent, stale generations reject,
and lost/resetting/reset ordering is tested. Native COM/live-object leak absence is not claimed
from Linux sanitizers.

## 4. Baseline, validation, and findings

Historical x64 MinGW build of the three test executables succeeded at `-j2`. Under Wine 10.0/Xvfb,
Smoke and Lifetime passed, while Parity timed out after a page fault. The root cause was D2D-134:
callbacks captured a destroyed local vector. This corrected the older nondeterministic 3/3 record;
it was a test-harness lifetime defect, not a production device-loss failure.

Final adapted evidence from `cmake-build-direct2d-integration`:

- MinGW GCC 14 x64 Release/Ninja/ccache, NET=OFF; all four targets built at `--parallel 2`;
- Wine 10.0 + Xvfb: **4/4** — Smoke 2.43 s, Parity 4.04 s, Lifetime 2.67 s, Unit 1.97 s;
  **11.11 test-seconds**, 13.66 s CTest wall; Unit **19/19** from six suites;
- post-D2D-136 identity/definition/unit focus: **26/26** from eight suites;
- OPENGLES/EasyGL: focused identity/capability **19 pass + one expected inverse-capability skip**,
  identity/definition **7/7**, textured-quad exact red pixel pass;
- GDI: focused smoke passed under Wine/Xvfb;
- HTML DOM: host contract **57/57**;
- non-Windows native Direct2D selection rejected at configure time as required;
- parallel-policy verifier passed.

ASan/UBSan was not rerun for the Windows-only COM/D2D implementation: no portable production
helper or shared graphics production path changed, and a native Linux sanitizer cannot execute or
prove Windows COM/D2D handle lifetime. This limitation is explicit rather than substituted with
another backend.

New independent findings are all resolved: `REMED-BUILD-019`, `D2D-134`, `D2D-135`, and
`D2D-136`. Existing Direct2D tasks resolved or bounded by tested rejection retain their original
IDs. `REMED-GFX-223` remains resolved; `REMED-GFX-224` remains **MEDIUM/OPEN** and unaffected.
`REMED-CONTENT-007/-008/-011` remain DONE and their containment files did not change.

## 5. Group and next action

Direct2D is the nineteenth integrated lane. The logical inventory is **19/21**; pending is exactly
`llgl` and `metal`. Authoritative Batch 6 / Group G remains the original four members:
`direct2d` (48), `llgl` (68), `metal` (99), and `skia` (141) — 356 historical commits. Skia and
Direct2D are integrated, so Group G is **2/4**.

Direct2D alone does not complete Group G. `INTEGRATION_ORDER.md` defines no per-Direct2D or Batch 6
checkpoint, so **no checkpoint was created**. LLGL and Metal were not begun. The next action is an
owner decision on the next remaining Group G lane; do not begin it from this record.
