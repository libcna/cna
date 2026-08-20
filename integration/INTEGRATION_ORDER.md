# INTEGRATION_ORDER.md — dependency graph, batches, first lane

Companion to `INTEGRATION_BRANCH_INVENTORY.md` (lane data) and `INTEGRATION_HISTORY_POLICY.md`
(how commits are adapted). Derived from the same 2026-08-04T14:31+02:00 fetch and the same
checkpoint `d79214e7`.

> **Post-campaign external-history addendum (2026-08-09).** The owner subsequently completed and
> pushed the authorized MetaGL/EasyGL public-history rewrite. Current public authority is MetaGL
> `develop` `571d3a62fe166b9781ac6193d137b12ff3757620` (tree
> `a7771c5593a4ec4b71283d38523a0cde3fbf6d4b`) and EasyGL `develop`
> `0b46d35c394a9fb6aea6a85c6587894b5013da33` (tree
> `e89ff546d3782e2b32e02f4b9dc56da42c4c463a`). Those trees equal the content accepted during
> Batch 4. The external archive tags were also rewritten and are now unsigned targets with legacy
> annotations; old external SHAs and signature/tag claims below remain dated evidence of the
> integration event, not current dependency pins or current-ref assertions. No further external
> rewrite is planned. CNA's own 21 original refs/archive tags and all checkpoint history are
> unchanged. See `integration/FINAL_RECONCILIATION.md`.
>

---

## 1. What actually constrains the order

Only four things create real ordering constraints. Everything else is preference.

| Constraint | Affects | Nature |
|---|---|---|
| **C1 — external repository chain** | `gl` | **Hard.** CNA `feature/gl` builds against `../easy-glrvc` by path. It cannot be integrated before EasyGL `rvc` and MetaGL `feature/followup-audit` reach their `develop` branches. See inventory §7 |
| **C2 — rebase onto the checkpoint** | `magnum`, `wicked` | **Hard, self-contained.** Both were cut from `feature/audit` at `2338b44f` and are 24 commits behind the checkpoint. Each must be rebased before adaptation. No other lane gates them |
| **C3 — `GpuDrawParams` unification (`fc0dd2a2`)** | 19 of 21 lanes | **Mechanical, per-lane, not an ordering constraint.** Four removed fields (`instanceVb`, `instanceVertexOffset`, `instanceFrequency`, `vertexBufferOffset`). Each lane pays it independently; no lane's payment helps another's. `magnum`/`wicked` already contain `fc0dd2a2` and pay nothing. `depthcrt`, `dxold`, `ext`, `gltf` reference none of the four fields and also pay nothing |
| **C4 — shared-interface textual drift** | the 16 lanes touching `GraphicsDevice.cpp` / `IGraphicsBackend.hpp` / `GraphicsCapability.hpp` | **Soft, cumulative.** Each landed backend adds registration/dispatch/capability entries to the same three files. Conflict cost grows with the number of lanes already landed, so cheap lanes should not be paid for at high-drift prices — but nothing *blocks* |

**There is no lane-to-lane hard dependency inside CNA.** Every ordering choice below other than C1
and C2 is a risk-management preference, and is labelled as such.

> **Corrected 2026-08-06 (Batch 2 stabilization).** C2's "24 commits behind the checkpoint" was a
> **behind-count read as a work count** — the `behind` half of `git rev-list --left-right --count`.
> The real work was **10 own commits** (`wicked`) and **13** (`magnum`), and the behind-count itself
> had grown to **230/248** by the time each lane was adapted (`integration/lanes/wicked.md` §1).
> The §2 graph's "(rebase 24)" labels carry the same stale figure. Both lanes are integrated
> (merges `683a00a5`, `e7d46c4c`), so C2 is discharged.

### 1.1 A post-audit obligation every new-backend lane inherits

Three checkpoint-era decisions apply to any lane that adds a rasterizing backend. These are not
conflicts — they are work the lane does not yet contain, because it was branched before the decision
existed:

- **`REMED-GFX-DECL-GUARD`** — a backend with a native vertex layout must call
  `RequireFaithfulDeclarationEXT` **at draw time**, raising `System::NotSupportedException` for a
  declaration it cannot represent faithfully. Present in Vulkan, WebGPU, Software, SDL_GPU, D3D9,
  D3D11, D3D12 at the checkpoint. The rule is **asymmetric** — check only what the caller declared;
  never require equality against the backend's own template.
- **`REMED-GFX-209` / `WEBGPU-115`** — `WireFrame` capability must be reported **truthfully**, and a
  backend that reports `false` must refuse polygon topologies before queueing, not render solid.
- The helper must be **header-only**: backend static libraries link
  `cna_backend_graphics_common` + SharpRuntime and cannot see a `src/CNA/**.cpp`.

**Every lane that adds or modifies a backend inherits this, in whatever group it sits.** It belongs
in that lane's adaptation scope, not in a follow-up.

> **Corrected 2026-08-04.** This paragraph used to read *"every lane in groups B, C, D, F and G"*,
> which understated Batch 0: `dxold` is a Group A lane and adds **eight** rasterizing backends. The
> obligations were applied to all six of its stride-dispatching backends at adaptation
> (`integration/lanes/dxold.md`). Derive the obligation from **what the lane adds**, never from its
> group letter.

---

## 2. Dependency graph

```
                     integration/post-audit-phase1  (= checkpoint d79214e7)
                                     │
   ┌───────────────┬─────────────────┼──────────────────┬─────────────────────┐
   │               │                 │                  │                     │
 GROUP A         GROUP B          GROUP C            GROUP D              GROUP G
 process         GL-family        audit-stacked      medium               deferred
 validation      + small          (C2 rebase)        backends             (owner call)
   │               │                 │                  │                     │
 depthcrt ←FIRST  stub            wicked  (rebase 24)  sokol             direct2d  (integrated)
 ext              opengles1       magnum  (rebase 24)  diligent          llgl      (integrated)
 gltf             opengl4                                                metal     (integrated)
 dxold            opengl1                                                skia      (integrated)
                  opengl2
                                                       GROUP F
                                                       high-conflict
                                                         glide
                                                         gdi
                                                         html-dom

 GROUP E — cross-repository, gated by C1
   MetaGL feature/followup-audit ──► MetaGL develop
                                          │
                       EasyGL rvc ────────┴──► EasyGL develop
                                                    │
                                        CNA feature/gl (GLB-38) ──► integration branch
```

---

## 3. Proposed batches

Validation is stated per batch. **No batch begins before the previous one's validation passes.**

### Batch 0 — process validation · **4 lanes, 36 commits** · `depthcrt` ✅ · `gltf` ✅ · `ext` ✅ · `dxold` ✅ — **COMPLETE**

`depthcrt` (**first — integrated, merge `61bd1a1b`**) → `gltf` (**integrated, merge `722a2f5a`**) →
`ext` (**integrated, merge `8a374b9f`**) → `dxold`

> **Batch 0 progress: 4 of 4 — CLOSED 2026-08-04.** All three history classes are covered.
> `depthcrt` landed as 5 signed, human-authored commits plus one signed non-fast-forward merge, with
> `f05e07c8` dropped as superseded. `gltf` landed the same day by **direct merge** — its single
> commit was preserved unchanged, not recreated. `ext` landed the same day as **one** adapted,
> re-authored, trailer-stripped, GPG-signed commit `c6a28036`. `dxold` closed the batch.
>
> **Batch 0 has two signed checkpoints, and they are not interchangeable** (see
> `integration/BATCH_0_COMPLETE.md` §1):
>
> | | Tag | Target | Lanes | Record |
> |---|---|---|---|---|
> | **A — intermediate** | `integration/checkpoint-batch0-20260804` | `e0332214` | **3** | `integration/BATCH_0_STABILIZATION.md` |
> | **B — final** | `integration/checkpoint-batch0-complete-20260804` | `990d6b8a` | **4** | `integration/BATCH_0_COMPLETE.md` |
>
> **A is correct and must never be moved, recreated, retargeted or deleted.** It marks a real state
> — the batch's process-validation objective, proven on three lanes — and it predates `dxold`
> deliberately, because §5 sequenced `dxold` after it.
>
> **`dxold` LANDED 2026-08-04 and closes Batch 0** — adapted on `adapt/dxold` as **35** signed
> commits (28 replayed originals, one bounded interface-adaptation commit, the two owner-ordered
> naming-transition commits DX3→FREEDIRECT / DX30→DX3, and three validation-driven completion
> fixes), merged as **`990d6b8a`** (signed, `--no-ff`). It adds **eight Historical-class public
> backends** (DX1, DX2, DX3-real, DX5, DX6, DX7, DX8, D3D10), all validated 137/137 on their
> dedicated Wine suites, plus the renamed FREEDIRECT identity for the free-direct backend. One
> §1.1 correction: this Batch-0 lane **does** add rasterizing backends, so the post-audit
> declaration-guard/WireFrame obligations were applied at adaptation — §1.1's "groups B, C, D, F
> and G" sentence understated Batch 0. Full record: **`integration/lanes/dxold.md`**.
>
> **The order was taken as `gltf` before `ext`, not the `ext → gltf` written above.** Both are
> one-commit documentation lanes and neither gates the other, so the order is preference, not
> constraint. `gltf` was run first because it is `HISTORY CLEAN` and signed while `ext` is
> `AUTHOR/TRAILER CLEANUP REQUIRED` — running `gltf` first proves the **direct-merge** path on the
> cheapest possible lane before `ext` exercises adaptation again.
>
> **One claim in this batch's premise turned out to be wrong.** §4 below argues `ext` and `gltf`
> "would validate nothing but the commit ceremony". That held for `gltf`; it did **not** hold for
> `ext`. Being a one-file documentation lane made its conflict risk *low*, not *zero* — `ext`
> rewrites and renumbers the same `NOXNA.md` table `depthcrt` had already appended four rows to, and
> a plain resolution favouring the incoming side would have silently deleted four rows of
> already-integrated work. **File count is not a proxy for conflict risk when two lanes edit the same
> file.** See `integration/lanes/ext.md`.

Every lane here touches **none** of the three shared interfaces and references **none** of the four
removed `GpuDrawParams` fields. Conflict risk is structurally near zero, so what is being validated
is the *process*, not the code: archive-tag provenance, history recreation, authorship policy, GPG
signing, range-diff reporting.

The batch deliberately spans all three history classes — `depthcrt`/`ext` are 100 % Claude-authored
and unsigned, `dxold` is mixed (3 of 28), `gltf` is already clean and signed. If the policy in
`INTEGRATION_HISTORY_POLICY.md` is wrong, it is wrong here, on 36 commits, and not on `skia`'s 141.

**Validation:** full build of the default `cmake-build-debug` target plus `CnaTests`; the two new
demo harnesses (`cna_depth_effect_demo`, `cna_crt_effect_demo`) build; `CRTEffectTests` and
`DepthEffectTests` pass; `git range-diff` original↔adapted for each lane; every adapted commit
GPG-verifies; `git log` over the batch contains zero attribution hits.

### Batch 1 — GL family and small single-interface lanes · **5 lanes, 130 commits** · **✅ COMPLETE 2026-08-05 · 5 of 5**

`stub` (**integrated, adapted, merge `99ae7d11`**) →
`opengles1` (**integrated, adapted, merge `df6b7cc6`**) →
`opengl4` (**integrated, adapted, merge `bc29a976`**) →
`opengl1` (**integrated, adapted, merge `c0876fca`**) →
`opengl2` (**integrated, adapted, merge `9e6d62ed`**)

> **`opengl2` LANDED 2026-08-05 by ADAPTATION and closes Batch 1 at five of five.** The HISTORY
> CLEAN classification re-verified exactly (40/40 maintainer PGP, zero trailers, zero
> attribution) — and **nine** commit bodies still carried session narrative, two of them caught
> only by a multiline-aware sweep across line wraps (a line-based grep misses `this\nsession`);
> plan-section citations like "(plans/plan_opengl2.md session 13)" were kept as factual references.
> **The probe found 17 errors, 14 distinct drifts** — the familiar stale-fork set plus two the
> head never adopted because they are the lane's own interface additions
> (`GetDefaultViewportRect` behind its real Letterbox/Overscan/Stretch modes, and
> **`GraphicsCapability::Instancing`**, growing the enum to eleven). Growing the enum obligated
> four truthful arms elsewhere: OPENGL4 true (its no-default switch would have answered false
> while genuinely instancing), OPENGLES1 false (its surviving `default: return true` would have
> claimed instancing again — the ES1 hazard, fourth appearance), OPENGL1 false (made explicit),
> Software false (default-true but its instanced draw is the throwing base refusal). Every other
> identity's existing shape already answers truthfully — measured, not assumed.
>
> **The campaign's first genuine new production finding: both RenderTarget2D round trips
> rendered vertically flipped** (GetData AND sampled-RT-to-screen — the post-processing round
> trip), masked by the lane's orientation-insensitive RT assertions and caught by the newly
> armed shared wireframe pixel oracle, whose "edge BC missing" reading led to a frame dump and
> two probe measurements. Fixed in-lane as FNA's own convention: render-time clip-Y flip while
> a 2D target is bound, `glFrontFace` winding compensation, direct viewport/scissor/readback
> row mapping, cube faces deliberately excluded. **Arming shared pixel oracles at integration
> is what found it — three prior lanes' arming precedent paid off here.**
>
> Also the predicted stale-fork harness class (GFX-165, four presentation tests reading after a
> raw `SDL_SetWindowSize` — production proven correct), a software-base-vertex adoption (GL 2.1
> has no `glDrawElementsBaseVertex`; the lane's own comment documented the silent fallback the
> head's offset folding would have triggered), and the **sixth registration union** (all 26
> identities kept exact token counts, `OPENGL2` added as the 27th). §1.1's declaration guard is
> satisfied **by translation** rather than refusal: this backend name-binds genuinely custom
> declarations from their own elements (its tested Task-1080 capability), so the refusal-style
> guard would have deleted working draws.
>
> Validated on the real llvmpipe `4.5 (Compatibility Profile)` context (`:101`; the backend's
> own path is GL 2.1 entry points + GLSL 1.10 by construction — zero `#version` directives):
> **48/48** lane suites; `CnaTests` **5737 · 5730 · 6 skipped · 1 failed** (the networking
> flake, now **2/6 in isolation** — the stabilization watch item has a worsening trend);
> ASan/UBSan over eleven suites: **zero findings**, all leaks `libGLX_mesa`-rooted,
> `detect_leaks=0` control all-green. EasyGL principal control at the merged head recorded on
> the lane card. Full record: **`integration/lanes/opengl2.md`**.

> **`opengl1` LANDED 2026-08-05 by ADAPTATION — the batch's first HISTORY CLEAN lane, and the
> classification survived object-level re-verification exactly** (31/31 maintainer-PGP,
> Robert-authored and -committed, zero trailers, zero attribution hits). What a direct merge
> still failed on: **three commit bodies carry session narrative** ("this session", "an
> independent adversarial-review fork of tonight's earlier commits", "explicit user go-ahead")
> — minimally reworded at replay under the `opengl4` precedent — and the content is 896 commits
> stale: **the compile probe found 11 errors, 10 distinct drifts** (the ES1 set: two pure
> virtuals making both classes abstract, four `void→bool` readbacks, `preserveContents`,
> `BlendWriteState`, the fog vector — plus the lane's own `ITextureCubeBackend::ShareCpuPixels`
> hook the head never adopted, restored by its own replayed commit). `fc0dd2a2` cost: **zero**
> (no instancing anywhere); `alphaTest` verified byte-identical fork→head.
>
> **The capability hazard broke its streak here — three lanes for four.** The fork-era switch
> has no default case and a trailing `return false`, so the two post-fork members already
> answered a truthful false by shape; made explicit as the exhaustive ten-member GL4-convention
> switch anyway.
>
> **The fog inversion is the ES1 math verbatim** (recover scale by projecting `fogVector.xyz`
> onto the modelview eye-Z row, `w` yields the scalars; `{0,0,0,1}` → fully fogged), now pinned
> by a **three-pair oracle** — before-ramp, exact mid-ramp ~50/50, degenerate — each with its
> own expected value, because monotonicity alone cannot tell a wrong sign from a right one.
>
> **New failure class for the campaign: the lane's own harnesses collided with post-fork
> device contracts.** 35/38 on first run; each failure mechanism-diagnosed, production code
> proven correct in all three: REMED-GFX-081 (SpriteBatch::Begin's FNA-faithful
> CullCounterClockwise persists after End(), silently culling quads authored under a one-time
> CullNone — instrumented to zero-fragments-with-perfect-state before the test was touched),
> GFX-165 (GetBackBufferData validates against PresentationParameters, which a raw
> SDL_SetWindowSize deliberately does not update), and a **control-proven** display regression
> (the sandbox's GLX no longer exposes swap control at all — a CNA-independent raw-SDL probe
> refuses interval 0 and 1 alike; the vsync half of that test is now an honest skip).
> **Expect this class on every remaining stale-fork lane: the framework's XNA-faithfulness
> grew while the lane's tests slept.**
>
> The registration union was needed a **fifth** time (six files, token-verified: all 25
> pre-existing identities exact, `OPENGL1` +13 tokens). Validated on the real llvmpipe
> `4.5 (Compatibility Profile)` context — reported as the driver's identity, not the backend's
> API level, with the fixed-function path proven by construction (zero shader entry points):
> **38/38** lane suites; `CnaTests` **5737 · 5692 · 44 skipped · 1 failed** (the known
> networking flake); ASan/UBSan over nine suites: **zero findings**, all leaks
> `libGLX_mesa`-rooted. Full record: **`integration/lanes/opengl1.md`**.

> **`opengl4` LANDED 2026-08-05 by ADAPTATION — the history half alone was disqualifying.**
> All 28 commits are authored and committed under a non-human identity and all 28 are SSH-signed
> by the campaign's known non-maintainer key (0 PGP, 0 genuinely unsigned — the inventory row
> re-verified at the object level); the first 8 carry both prohibited trailers. Four per-session
> `NEXT.md` status-summary commits were **OMITTED with justification** (session narrative;
> `plans/plan_opengl4.md` carries the technical continuity); the other 24 replayed, 32 of 41 files
> byte-identical at the replay boundary, 0 missing.
>
> **Content: the compile probe found 23 errors across 13 drifts** — the `opengles1` set (bool
> readbacks, pure-virtual `SetVertexDeclaration`/`SetRenderTargets`, `preserveContents`,
> `BlendWriteState`) **plus two this lane paid first**: the `fc0dd2a2` unified instanced
> transport (its GL4-33 instancing read the removed `instanceVb`) and the FNA fog vector across
> ten GLSL programs — consumed directly (no inversion; this is a shader backend), skinned
> programs dotting the POST-skin position.
>
> **The capability hazard recurred exactly as predicted:** `SupportsCapability` was never
> overridden, inheriting `true` for members the fork predates. Now an exhaustive ten-member
> switch with **no default case** (a future member is a `-Wswitch` warning, not a wrong answer);
> `MultiStreamVertexInput` false with the shared negative oracles passing,
> `AnisotropicFiltering` from the driver-granted ceiling. **The registration union was needed a
> fourth time** — six files, token-verified, all 24 pre-existing identities intact.
>
> Validated on a real `OpenGL 4.5 (Core Profile)` context (llvmpipe, `:101`): **25/25** lane
> suites; `CnaTests` **5737 · 5730 passed · 6 skipped · 1 failed** (a transient x11-connection
> blip, different victim each run, 3/3 in isolation); principal EasyGL control at the merged
> head **5912 · 5905 · 6 · 1** (the known networking flake) — zero regressions vs the Batch-0
> baseline. ASan/UBSan over nine suites: zero findings, leaks control-classified to
> Mesa/harness. WireFrame is now pixel-oracle-proven in the shared suite. Full record:
> **`integration/lanes/opengl4.md`**.

> **`opengles1` LANDED 2026-08-05 by ADAPTATION — it failed the direct-merge conditions on BOTH
> halves**, which is why history and content were checked independently.
>
> **History.** 24 of 26 commits carry the maintainer's PGP signature with zero trailers. The other
> **2 are SSH-signed by a non-maintainer key**, authored *and* committed under a non-human identity
> with two prohibited attribution trailers each — and they are the lane's **first two commits**, so
> all 24 clean commits descend from them. Zero genuinely unsigned. `%G?` said `N` for both; the
> `gpg.ssh.allowedSignersFile` error firing exactly twice is the real tell, and `git cat-file -p`
> settled it — the `ext` lesson holding a second time.
>
> **Content.** Forked at `ac3aaaeb`, **835 commits behind**. Source inspection found the two
> now-pure virtuals; **the compile probe found four more that no signature grep could have** —
> `GetData`/`SetData` changing return type from `void` to `bool` on four interfaces — plus
> `CreateRenderTargetCube`'s new `preserveContents`, `ApplyBlendState`'s new `BlendWriteState`, and
> `GpuDrawParams`' scalar `fogStart`/`fogEnd` becoming the FNA fog vector. `IGraphicsBackend.hpp`
> grew **1023 → 1788 lines** across the gap. **A probe is not optional on a stale-fork lane; it is
> the only thing that sees a changed return type.**
>
> **The fog change was the one with real semantics.** A fixed-function pipeline has no dot product
> to evaluate a fog vector with, so `glFog` needs the scalars back. They are **recovered exactly**
> by inverting the vector against the same `world*view` matrix the draw loads immediately before —
> and proven by three distinct `FogStart`/`FogEnd` pairs each producing their own correct result on
> a real driver, the degenerate `FogStart == FogEnd` among them.
>
> **One independent production defect, found by the drift rather than introduced by it.**
> `GraphicsCapability` grew from **8 members to 10** after the fork, and `SupportsCapability` ends
> in `default: return true` — so `Texture3D` and `MultiStreamVertexInput` were both answered with a
> confident, wrong `true`. `Texture3DUnsupportedBackendTest`, which exists precisely to catch this,
> **skipped**, because the false claim silenced its own detector. **Every remaining lane forks from
> a base the head has outgrown; check any catch-all `default` against the current enum.**
>
> **`TextureCube` readback was implemented, not declared absent** — the backend genuinely owns cube
> pixels, so inheriting "cannot read a cube face" was untrue. Level 0 reads back exactly; above it
> is a real boundary, since `GL_OES_framebuffer_object` requires an attachment's level to be 0.
>
> **§1.1 decided on evidence:** the declaration guard **applies** (this backend dispatches by
> stride, exactly the case it exists for) and is wired into all four draw routes, header-only;
> truthful `WireFrame` was already satisfied and genuinely implemented.
>
> **The registration union was needed for the third time** — taking the incoming side of
> `BackendSelection.cmake` would have deleted `STUB`, `FREEDIRECT`, `DX1/2/5/6/7/8` and `D3D10`, and
> the lane's own `DX3` token means *free-direct*, not the head's real DirectX 3.
>
> **Validation on a real `OpenGL ES-CM 1.1` driver** (Mesa 25.0.7 softpipe, `:101`), not a desktop
> GL fallback: **63/63** across all seven lane harnesses; `CnaTests` **5733 run · 5689 passed · 87
> skipped**. Failures went **22 → 1**, the last being a two-process networking test that passes 3/3
> in isolation. Full record: **`integration/lanes/opengles1.md`**.

> **`stub` LANDED 2026-08-04 by ADAPTATION — the direct-merge classification did not survive
> contact.** Every *history* claim behind that classification re-verified true: 5 own commits, 5/5
> maintainer-authored **and** committed, 5/5 maintainer PGP-signed, zero attribution, zero merges,
> zero WIP, `GpuDrawParams` cost zero. The lane needed no history work whatsoever.
>
> **Its content did.** Forked from `develop` at `ac3aaaeb` and 827 commits behind, it predates two
> `IGraphicsBackend` members that have since become **pure virtual** —
> `IVertexBufferBackend::SetVertexDeclaration` and `IGraphicsBackend::SetRenderTargets`. Both are
> pure *by design*, so a new backend cannot inherit a silently-wrong default. A compiler probe, not
> an inference, showed `StubVertexBufferBackend` and `StubGraphicsBackend` are **abstract** against
> the current tree: a direct merge would have produced a non-compiling integration head. Conditions
> 7, 8 and 10 fail; 1–6, 9, 11, 12 hold.
>
> Adapted in the `dxold` shape: 5 originals replayed (**4 of 5 byte-identical by `range-diff`**),
> one bounded interface-adaptation commit `b7d472d7`, one validation-driven test-contract commit
> `c29ef117`, merged as **`99ae7d11`** (signed, `--no-ff`).
>
> **The §1.1 obligation was decided, not waved through.** Truthful `WireFrame` reporting was already
> satisfied and is *stricter* than Headless's inherited `true`; the declaration-fidelity guard has no
> subject on a backend with no native vertex layout (Headless's precedent, measured); and the
> "refuse polygon topologies" half does not apply to a backend with no pixel route, because refusing
> would break the lane's own contract that a `Game` loop completes without throwing.
>
> **The predicted "registration boilerplate" conflict needed a union, not a resolution.** Taking the
> incoming side of `cmake/BackendSelection.cmake` would have deleted `dxold`'s eight backends and
> reverted the `DX3 → FREEDIRECT` rename — and the lane's own `DX3` token means *free-direct*, a
> different backend from the head's real DirectX 3. Third time this campaign that a "low-conflict"
> lane's shared file required a union.
>
> Validation: `Stub_Smoke` 7/7 **with no display present**, `CnaTests` **5693/5737** under
> `CNA_GRAPHICS_BACKEND=STUB`. Full record: **`integration/lanes/stub.md`**.

Ascending interface footprint: `stub` and `opengles1` touch `GraphicsDevice.cpp` only; `opengl4` and
`opengl1` add `IGraphicsBackend.hpp`; `opengl2` adds `GraphicsCapability.hpp`. Landing them in that
order means each conflict is resolved against a base that already contains the simpler case.

**Validation:** each backend builds under its own `CNA_GRAPHICS_BACKEND` value; the GL-family test
labels pass under Xvfb; the declaration guard (§1.1) is present and its refusal path is exercised.

#### The order was re-derived after Batch 0, not assumed — and `stub` holds

Measured from refs against the **current** integration head `990d6b8a`, not carried forward.
`drift` counts the lane's own pre-existing files that have since changed at the integration head —
i.e. the conflict surface the adaptation must actually resolve:

| Lane | Own | Files | New | **Drift** | Shared ifaces | History class (object-level) |
|---|---|---|---|---|---|---|
| **`stub`** | **5** | **15** | 6 | **8** | GD | **HISTORY CLEAN** — 5/5 Robert-authored, 5/5 maintainer **PGP** |
| `opengles1` | 26 | 23 | 16 | 6 | GD | mixed — 24 PGP, **2 SSH** |
| `opengl4` | 28 | 41 | 31 | 9 | GD IGB | total cleanup — **0 PGP, 28 SSH** |
| `opengl1` | 31 | 43 | 32 | 11 | GD IGB | HISTORY CLEAN — 31/31 PGP |
| `opengl2` | 40 | 63 | 52 | 11 | GD IGB GC | HISTORY CLEAN — 40/40 PGP |

**`stub` remains the right opener**, on four measured grounds:

1. **It is the only lane in the batch needing no history work at all.** 5 of 5 commits are
   Robert-authored, Robert-committed and carry the maintainer's PGP signature — the `gltf` class.
   Whether it is a **direct-merge** candidate rather than an adaptation is the first question its
   lane card must answer against the nine direct-merge conditions.
2. **`GpuDrawParams` cost is zero — newly measured, not inherited.** None of the four
   `fc0dd2a2`-removed fields (`instanceVb`, `instanceVertexOffset`, `instanceFrequency`,
   `vertexBufferOffset`) appears anywhere in its diff. §4.2 of the inventory named only
   `depthcrt`/`dxold`/`ext`/`gltf` as zero-cost because `stub` had not been measured; it is now a
   fifth.
3. **Smallest by every size measure** — 5 commits, 15 files, +685/−9, a 166-line header and a
   49-line source file.
4. **It is the batch's registration-boilerplate archetype.** Its eight drifted files are precisely
   the shared registration surface every remaining GL lane must also edit, so resolving them once on
   the cheapest possible lane is what makes the next four cheaper.

`opengles1` has a marginally smaller drift count (6 vs 8) but five times the commits and a mixed
history — a worse first lane on every other axis. **The proposed order is unchanged.**

#### What `dxold` did to `stub`'s conflict surface

Landing 8 backends added registrations to the same files `stub` edits. Of `stub`'s 8 drifted files,
**5 drifted specifically because of `dxold`** (unchanged at the checkpoint, changed at the head):
`CMakeLists.txt`, `README.md`, `include/CNA/GraphicsBackendType.hpp`,
`tests/CNA/GraphicsBackendTypeTests.cpp`,
`tests/Microsoft/Xna/Framework/GraphicsBackendCompileDefinitionTests.cpp`. The other three
(`.gitignore`, `cmake/BackendSelection.cmake`,
`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`) had already drifted at the checkpoint.
This is **C4 behaving exactly as predicted** — cumulative textual drift on the shared registration
surface — and it is an argument for taking `stub` now rather than later.

`docs/graphics-backend-feature-matrix.md` is `stub`'s one pre-existing file with **zero** drift; it
is byte-identical across `develop`, the checkpoint and the head.

#### Known adaptation scope for `stub`, established read-only

| Item | State |
|---|---|
| Head | `a35651e8` — `feature/stub` local **and** `origin/feature/stub`, identical |
| Archive tag | `archive/preintegration/stub-20260804` → `a35651e8`, **verifies good**, unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked, not audit-stacked; 827 behind the head |
| Own commits | **5**, linear, **0 merges**, 0 WIP/fixup |
| Dependencies | **none beyond the base** — no SDL video, no GPU library, no new third party. `docs/stub-backend.md` states this explicitly |
| Test evidence | `Stub_Smoke` (7 checks, label `Stub`, 30 s timeout) via `cna_register_backend_test`, which **still exists** at the head in `cmake/TestHelpers.cmake`. Lane-recorded 2026-07-19: full `CnaTests` 5413/5423 with 4 sensor skips and 6 documented failures |
| **§1.1 obligation — must be resolved, not assumed** | `stub` adds a backend, so the post-audit obligations apply. Its last commit is already *"`SupportsCapability` should return false, not inherit default true"*, which is the `REMED-GFX-209` truthfulness question in embryo. A backend reporting `WireFrame=false` must **refuse polygon topologies before queueing** rather than silently no-op, and the declaration-fidelity guard must be reasoned about explicitly for a backend whose draw routes are all no-ops. **Decide and record it; do not wave it through because nothing renders.** |
| Likely conflict work | the 8 drifted files above, all registration boilerplate |

**Model recommendation for `stub`: Fable.** The lane is small, clean and mechanical — 5 signed
commits, 15 files, no history recreation, and conflicts confined to registration boilerplate whose
correct resolution `dxold` has just demonstrated on the same files. Nothing here turns on contested
design. **Reserve Opus for `opengl4`**, which is 28/28 non-maintainer-signed total history
recreation across two shared interfaces — and, before that, for the §1.1 capability decision if
`stub`'s no-op draw routes turn out to make the WireFrame-refusal contract genuinely ambiguous.

### Batch 2 — audit-stacked, rebase-first · **2 lanes, 23 commits** · ✅ **COMPLETE 2026-08-06 · 2 of 2**

`wicked` (**integrated, merge `683a00a5`**) → `magnum` (**integrated, merge `e7d46c4c`**)

> **`magnum` LANDED 2026-08-06 by ADAPTATION as merge `e7d46c4c` — the eleventh lane — and closes
> Batch 2 at two of two.** The total-recreation classification re-verified exactly (13/13
> non-human author+committer, 13/13 SSH-signed by the campaign's one foreign key, both trailers
> throughout); replayed as 19 signed commits (13 TRANSFERRED 1:1, 31/45 files byte-identical at
> the replay boundary, zero omissions). **The compile probe found zero drift** — the second lane
> after `wicked` to fork past the whole stale-fork set — and the registration union (the eighth)
> kept all 28 identities token-exact while adding the 29th. The lane's own re-gate of the flat
> "no wireframe" assertion had been superseded by REMED-GFX-209 at the head and resolved to
> exactly HEAD; MAGNUM instead joined the wireframe pixel oracle's measured set. Its
> `preferPerPixelLighting` comment correction was re-measured before replay: eight backends
> honour the field at this head, not the five the lane's text named.
>
> **First-ever build and execution.** Corrade+Magnum pins proven = upstream master tips (MIT,
> fresh `~/deps` clones reproduce them; `FETCHCONTENT_SOURCE_DIR_*` offline route verified);
> device on a real GL 4.5-core context (llvmpipe, `:101`); 8/8 dedicated pixel suites; the
> WICKED-78 teardown class measured absent (4/4 lifecycle legs); the §1.1 obligations proven at
> runtime by a 17/17 probe (custom-declaration refusal with the target unmutated against a
> lighting control, no over-refusal, unlisted-stride silent drop converted to a refusal,
> exhaustive 11-member capability switch).
>
> **Validation found two real defects, both fixed in-lane.** The adaptation's own guard compared
> split multi-stream declarations in the wrong space — the armed shared oracles caught it on
> their first run (9 failures → 9/9). And `MAGNUM-65`, a production `sizeof(T)`-fold sprite-flush
> heap overread carried since the lane's first commit: Corrade's typed-pointer
> `ArrayView<const void>` constructor scales by `sizeof(T)` and the flush passed byte counts —
> green everywhere (GL stored the oversized copy, draws read the real prefix) until radeonsi's
> allocator faulted it deterministically and ASan flagged it on the first sanitized flush.
> **A defect that renders correctly is exactly what pixel oracles cannot see; the sanitizer leg
> is what caught it.**
>
> Official corpus at the fixed content: **5843 · 5835 · 2 failed · 6 truthful skips · 0 aborts**,
> both failures control-classified pre-existing (networking Outcome C; the wall-clock audio class,
> 2/12 shuffling control failures on the pre-Magnum principal binary). ASan+UBSan over all eight
> suites: **zero findings post-fix**, leaks 100 % `libGLX_mesa`-rooted, `detect_leaks=0` controls
> green. Full record: **`integration/lanes/magnum.md`**.

> **Batch 2 stabilized 2026-08-06 — checkpoint decision BLOCKED, no tag.** Both lanes'
> provenance re-verified clean, zero Batch 2 integration regressions on every instrument
> (Wicked 5780-corpus, Magnum 5843-corpus, EasyGL 6212-ctest + the 5913-case continuity
> instrument), sanitizer gates clean, `REMED-GFX-222` discovered and resolved
> in-stabilization — and the stabilization's own deeper-than-corpus probe found **`WICKED-80`**
> (`Texture3D` staged transfers corrupt dimension-dependent tail rows, `plans/plan_wicked.md`), an open
> production defect that blocks the tag under the same literal criterion `REMED-GFX-220` blocked
> Batch 1's first decision. Full record: **`integration/BATCH_2_STABILIZATION.md`**.

> **`wicked` LANDED 2026-08-05 by ADAPTATION as merge `683a00a5` — the tenth lane — after its two
> blockers were repaired in-lane the same day they had blocked it.** `WICKED-77`: the instanced
> route carries each stream's whole public `VertexOffset` in the stream table (the ordinary routes
> fold it into `baseVertex`), and the single-geometry-stream binding dropped it — fixed at the
> bind, with a five-case regression whose pre-fix failure names the exact mechanism. `WICKED-78`:
> **both teardown mechanisms are upstream at pin `27c0df16`** — the destructor never destroys its
> three null images (VMA asserts on every never-rendering device) and never frees its pooled
> command lists (any drawing device leaked its whole `VkInstance`/`VkDevice`/allocator, masking
> the assertion) — fixed by a second carried patch, `wicked-device-teardown.patch`, alongside the
> SDL3 one, isolated with a CNA-free reproducer before any fix was written.
>
> **The first FULL corpus run this unblocked found two more:** `WICKED-79` (staged texture uploads
> smeared at widths not dividing the buffer-copy row alignment — upstream repacks initial data
> tightly, `CopyTexture` consumes aligned mapped pitches; fixed by writing the staging texture
> through its own mapped layout plus a submit per staged upload) and a corpus-composition break
> (the lane's backend-local test directory was globbed into every other backend's `CnaTests` —
> caught by the principal control, excluded by the glob file's own convention). Three shared
> contract tables also gained their WICKED arms (compile-definitions count, a FOURTH instanced
> multi-stream shape asserting the backend's exact one-instance-stream refusal, RTCube SetData
> acceptance).
>
> Validated at the merged content: corpus **5780 · 5774 · 0 failed · 6 truthful skips**, no abort;
> dedicated suites 14/14 + 6/6 + 5/5 + smoke; ASan+UBSan (vptr kept via upstream's
> `WICKED_ENABLE_RTTI`) **zero CNA-originating findings**, all leaks `libvulkan_lvp`-rooted with
> `detect_leaks=0` controls; principal EasyGL **5913/5907/0/6 — exactly the Batch 1 baseline**.
> Full record: **`integration/lanes/wicked.md`** §15.

> **`wicked` was adapted, validated and NOT merged.** The adaptation itself was the cheapest of the
> campaign: 10 commits replayed, the compile probe found **zero** interface drift (the only lane so
> far to need none — it forked from `feature/audit` *after* every stale-fork change landed), the
> dependency proved reproducible (Wicked Engine **MIT**, pin `27c0df16` = upstream master tip, SDL3
> patch applies clean from a fresh clone), and the registration union kept all 27 identities while
> adding the 28th.
>
> **Its content is what failed.** First execution — the backend had never been built or run —
> produced a real Vulkan device and 22 compiled shaders, then **two independent HIGH production
> defects**: `WICKED-77` (instanced draws ignore the geometry `VertexOffset`; the identical data
> renders correctly through the ordinary indexed route) and `WICKED-78` (`GraphicsDevice` teardown
> leaves GPU allocations live, tripping Wicked's VMA assertion and **aborting the process** —
> deterministic on a single device, and therefore making the shared `CnaTests` corpus unrunnable
> under this backend at all).
>
> **Recommend Group G / rework**, per `integration/lanes/wicked.md` criterion 8. Everything is
> preserved: branch `adapt/wicked` (12 signed commits), worktree `cnaintegration-wicked`, the
> `cmake-build-wicked` tree, `~/deps/WickedEngine`.
>
> **Two corrections this lane forces on the paragraph below.** The rebase figure is a
> **behind-count, not a commit count of work**: `wicked` has **10** own commits, `magnum` **13**.
> And 24 is stale — measured from `2338b44f`, the gap to the *current* integration head is **230**,
> not 24, because Batch 0 and Batch 1 landed 205 commits after it was written. `magnum`'s card
> repeats the same "rebase 24 commits" phrasing and is **left untouched here** rather than widening
> this lane; it is handed to the Batch 2 checkpoint.

C2 applies: each needs a rebase onto the checkpoint **before** adaptation. Both already
contain `fc0dd2a2`, so they pay no `GpuDrawParams` cost — the only lanes in the inventory that do
not. `wicked` touches none of the three interfaces; `magnum` touches two. Both are 100 %
Claude-authored and unsigned: total history recreation.

**Both are `NEEDS VALIDATION`, not ready.** Neither has ever been checked out, built or tested. This
batch's first task is establishing whether they build at all, and it may end by moving one or both
to Group G. Their small size is not evidence of completeness.

**Validation:** rebase produces no semantic conflict; both third-party dependencies resolve
(`cmake/ThirdPartyMagnum.cmake`, `cmake/ThirdPartyWicked.cmake` + its SDL3 patch); each backend
builds and its smoke test runs.

### Batch 3 — medium backends · **2 lanes, 102 commits** · `sokol` ✅ · `diligent` — **1 of 2**

`sokol` (**integrated 2026-08-07, merge `37066e45`**) → `diligent`

`sokol` touches `GraphicsDevice.cpp` only; `diligent` adds `IGraphicsBackend.hpp`. Both are mixed
histories (23/37 and 37/65 Claude-authored and unsigned).

**Validation:** per-backend build and smoke test; declaration guard present; capability reporting
truthful per §1.1.

> **`sokol` LANDED 2026-08-07 and opens Batch 3** — adapted on `adapt/sokol` as **44** signed
> commits (37 replayed 1:1, one stream-array adaptation, two §1.1 obligations, three
> validation-driven fixes and one docs commit), merged as **`37066e45`** (signed, `--no-ff`, merged
> tree byte-identical to the adaptation). It adds the **thirtieth** public identity, `SOKOL` —
> sokol_gfx on a desktop OpenGL 4.1-core context, dependency fetched at a pinned zlib-licensed
> commit and never vendored, with no carried patch.
>
> Two corrections this lane makes to the paragraph above. Its history is **not** "unsigned": the 23
> Claude-authored commits are SSH-signed by the campaign's known non-maintainer key, exactly the
> class `magnum` and `wicked` turned out to be — the inventory's "unsigned" wording describes the
> absence of a *maintainer* signature, not the absence of a signature. And its readiness was
> **UNKNOWN, not unbuilt**: unlike `wicked` and `magnum`, a pre-adaptation build at the lane's own
> fork point reproduced its recorded results exactly (48 checks, 0 failures) before any adaptation
> began, which is what made every later measurement attributable.
>
> §1.1's three obligations were all paid at adaptation, and one of them changed an answer:
> `WireFrame` was corrected from `false` to `true` on the shared pixel oracle's own reading. The
> lane's `false` came from reading the flag as "native polygon-mode support"; REMED-GFX-209 defines
> it by what a caller observes. It deliberately did not copy EasyGL's `false`, which this
> repository already records as the one report known to be wrong (`REMED-GFX-219`).
>
> Full record: **`integration/lanes/sokol.md`**.

### Batch 4 — cross-repository `feature/gl` · **1 lane, 28 commits + 21 external** · ✅ **COMPLETE 2026-08-07**

> **`gl` LANDED 2026-08-07 as merge `0a51f8647` and Batch 4 is CLOSED — checkpoint
> `integration/checkpoint-batch4-20260807` taken (READY).** All nine §7.4 steps ran in order under
> direct owner instruction. The externals reached their develops as **trailer-stripped replays**
> (MetaGL `c964e736`, EasyGL `9b831dee`; trees byte-identical to the as-authored heads): the
> inventory's HISTORY CLEAN rows missed `Co-authored-by: Junie` trailers on 15/16 and 5/5 commits,
> and the cleanliness guarantee is owner-scoped to the newly integrated ranges (published ancestry
> with historical Claude trailers deliberately not rewritten; a global rewrite is a separate
> owner-controlled operation). Public identities went 32 → **35** (EASYGL withdrawn; OPENGLES,
> OPENGL33, WEBGL1, WEBGL2 added; EasyGL internal per inventory §7.0). GLB-38 done — no
> `easy-glrvc` reference survives a configure. REMED-GFX-219 resolved in-lane (WireFrame true,
> oracle-backed); REMED-GFX-224 open and visible; new pre-existing findings `REMED-CORE-015`,
> `REMED-CONTENT-010`. Corpora 5906·5900·0 on both native profiles; the 236/241 instrument (now
> 293) at 292/293 on both, single failure documented pre-existing. Full records:
> **`integration/lanes/gl.md`**, **`integration/BATCH_4_STABILIZATION.md`**.

Gated by **C1**. Execute the nine-step sequence in inventory §7.4 exactly, in order. The
cross-repository **merges** — steps 4, 5 and 6 — and `GLB-38` (step 7) are **owner-only**;
`plans/plan_glbackends.md` records that decision independently and it has not been lifted.

**The lane adds four public backends, not five.** They are **OpenGL ES 3, OpenGL 3, WebGL 1 and
WebGL 2**. **EasyGL is internal and hidden** — a support library, never a user-selectable CNA
backend. Do not count or expose it as one (inventory §7.0).

**Steps 1–2 are cleanup, not a decision.** The `easy-glrvc` worktree's uncommitted
`CMakeLists.txt` redirect is **temporary local build configuration** — not feature work, and **not
provenance that must be preserved**. Restore or remove it so the worktree is clean (`git restore` /
`git checkout --`; **never `git stash`**), then create the missing signed archive tag for the
completed EasyGL `rvc` head. The earlier "discard or commit — the owner's call" framing was wrong and
is corrected in inventory §7.5: the only open provenance gap in the campaign is blocked on a one-line
cleanup, not on a judgment call.

`feature/gl` itself is `MESSAGE CLEANUP REQUIRED` — clean authorship and signatures, but five commit
bodies carry process narrative that must not survive.

**Validation:** MetaGL `develop` validated standalone; EasyGL `develop` validated against the new
MetaGL `develop`; `GLB-38` repoint verified by a configure that no longer references `../easy-glrvc`;
CNA `feature/gl` builds and its 236/241 suite runs.

### Batch 5 — high-conflict, all-interface lanes · **3 lanes, 121 commits** · ✅ **COMPLETE 3 of 3; checkpoint TAKEN after required retake**

`glide` → `gdi` → `html-dom`

> **Glide acceptance record:** `glide` landed 2026-08-08 as signed merge `677f4c59`; merged tree equals
> `adapt/glide` @ `e891e105`.** The original 32-commit branch remains unchanged at `2f9b47e1` and
> its annotated archive remains valid. Batch 5 is not checkpoint-eligible: `gdi` and `html-dom`
> remain. No checkpoint was created and no second member was begun. Technical validation is green.
> Reconciliation records the historical helper's bare `cmake --build --parallel` as a bounded-
> parallelism violation whose actual maximum is unprovable, but classifies it **B**: that operation
> supplied only historical-baseline SDL/configure evidence, while later monitored `-j4` work
> independently supplied all final gates. Glide remains technically accepted. Full record:
> `integration/lanes/glide.md`. That acceptance record preceded GDI.
>
> **`gdi` LANDED 2026-08-08 as signed merge `ba5fa601`; merged tree equals `adapt/gdi` @
> `625f4ad5`.** The original 34-commit branch remains unchanged at `adc9cc2a`; its sole signed
> annotated archive still peels to that exact head. The adaptation has 43 signed linear commits:
> 34 chronological replays plus nine follow-ups. All 34 replayed commits map 1:1 by range-diff
> (18 `=`, 16 `!`) with matching author/name/email/date/subject metadata, no omission, and no
> reorder. Current interfaces remained authoritative through the A–G classification recorded in
> `integration/lanes/gdi.md`; this lane has real shared GraphicsDevice, capability, Software-2D,
> SpriteBatch, RenderTarget, blend/effect, build, and registration changes and is not classified as
> conflict-free or backend-local-only.
>
> GDI is the Windows-only private CPU Software-2D core presented through classic Win32
> `SetDIBitsToDevice`/`StretchDIBits`, with no fallback renderer. Its true capabilities are exactly
> StencilBuffer, WireFrame, and MSAA. Historical and current focused matrices pass 19/19 through
> Wine; current evidence is x64 MinGW GCC 14 plus Wine 10/Xvfb. PE32 planners pass 12/12; focused
> native Software sanitizers and the EasyGL, DX3, Sokol, Diligent, Skia, and Glide controls pass
> within their stated runtime/compile-only boundaries. Physical Windows and native MSVC were not
> run. `REMED-GFX-229` through `-233`, `REMED-BUILD-017/-018`, and GDI-054 lifetime hardening are
> resolved for their automated scope; no unresolved supported-path GDI defect remains. Batch 5 is
> not checkpoint-eligible because HTML DOM remains. No checkpoint/tag was created. Next is HTML DOM.
>
> **`html-dom` LANDED 2026-08-08 as signed merge `24bf4786`; merged tree equals
> `adapt/html-dom` @ `a32977f3`.** The original 55-commit branch remains unchanged at `8e4e4293` and
> its sole signed annotated archive still peels to that exact head. The adaptation has 50 signed
> linear commits: 49 chronological meaningful replays plus one post-audit stabilization commit.
> Six Canvas-only commits and the Canvas-only hunk of one mixed commit were deliberately omitted;
> a scoped comparison at the historical replay boundary is byte-identical for every HTML DOM
> implementation/build/test/doc path. Range-diff accounts for every original commit and omission.
>
> `HTML_DOM` is a genuine Emscripten-only 2D public backend: pooled browser `<div>` sprites and CSS
> composite the backbuffer, private Canvas2D surfaces implement the bounded `RenderTarget2D` path,
> and handwritten `EM_JS` owns the bridge. It is not an alias or fallback. Current host contracts
> pass 57/57 and 57/57 with linked ASan/UBSan. OPENGLES/EasyGL, GDI, Glide, and focused
> Diligent/Skia/Sokol controls pass within their recorded runtime/build boundaries. The host lacks
> Emscripten and Node, so the adapted browser target was not rebuilt or run; historical real-browser
> results remain explicitly historical. `HTMLDOM-121/-122/-123` are resolved and no supported-path
> HTML DOM defect remains. Full record: `integration/lanes/html-dom.md`.
>
> Batch 5 is now technically complete and stabilized at 3/3, with exactly 18/21 lanes integrated.
> Its checkpoint is nevertheless **BLOCKED** by the still-open HIGH/P1
> `REMED-CONTENT-007/-008` gate stated below. No Batch 5 tag was created and no nineteenth lane
> began. Full decision: `integration/BATCH_5_STABILIZATION.md`.
>
> **Required retake 2026-08-08 — READY.** The paragraph above is the preserved first decision.
> `REMED-CONTENT-007/-008` are now DONE, the same-pattern audit's `REMED-CONTENT-011` is DONE, and
> final integration HEAD `c805fd73` passes the focused Content, sanitizer, and Batch 5 continuity
> controls. Local signed annotated tag `integration/checkpoint-batch5-20260808` (tag object
> `307c9ad5`) peels to `c805fd73` and verifies Good. Nothing was pushed and no nineteenth lane began.

All three touch `GraphicsCapability.hpp`; `glide` and `gdi` touch all three files. Ordered ascending
by size. `glide` and `gdi` are **HISTORY CLEAN** and signed — no attribution cleanup is needed,
which is exactly why they should absorb the first all-three-interface conflicts rather than a lane
that also needs message/identity repair. Both required chronological replay onto their current
integration bases; author/date/subject metadata was preserved and every recreated commit was signed.

Before integration, `html-dom` was the inventory's only lane with **three** history classes at once
(21 maintainer-PGP, 17 maintainer-SSH, 17 prohibited-author commits). Its historical worktree's
pre-existing ` M third_party/SDL` proved to be a clean submodule checkout-pointer mismatch, not
uncommitted source work; it was preserved untouched while a separate clean adaptation worktree was
used.

**Historical first validation result:** accepted Glide/GDI behavior remained intact; HTML DOM's truthful host/browser
boundary was green; the OPENGLES/EasyGL principal continuity and required focused all-interface
controls passed; no backend's `WireFrame` claim regressed. The initial absence of the checkpoint was
solely the pre-existing §6 security gate, not a lane-stabilization failure. The retake result is
recorded immediately above.

### Batch 6 / Group G — **COMPLETE 4/4** · technical READY · checkpoint TAKEN · **4 lanes, 356 commits**

The authoritative membership remains four lanes. Skia was integrated early by direct instruction;
Direct2D was then explicitly and boundedly unfrozen, stabilized, and integrated on 2026-08-08.
The owner then authorized LLGL, which was stabilized and integrated on 2026-08-09. Metal completed
the group later that day under the repository's explicit source-continuity/no-Mac policy.

| Lane | Own | Status / disposition |
|---|---|---|
| `direct2d` | 48 | ✅ **INTEGRATED** — bounded owner unfreeze; exact frozen recount was 96/128 incomplete, not the stale 88. Supported-path defects were fixed or changed to tested rejection; remaining rows are native/external evidence or nonblocking coverage/performance/process work. `adapt/direct2d` `1b740d96`; signed merge `7af760be`; lane card records the freeze-reason dispositions and 4/4 Wine gate |
| `llgl` | 68 | ✅ **INTEGRATED** — the i686 `__int128` record is classification A: a real sharp-runtime limitation reached by Glide's x86 ABI probe, but no LLGL i686/Windows contract or route exists. Linux/X11 x86_64 LLGL OpenGL is the supported path; 145/137/0/8 LLGL CTest, 5210/5203/7 full units, 9/9 sanitizers, EasyGL and Direct2D controls pass. `adapt/llgl` `c74fbaeb`; signed merge `4ac696c7`; see `integration/lanes/llgl.md` |
| `metal` | 99 | ✅ **INTEGRATED** — 88 retained chronological replays + six post-audit commits on `adapt/metal` `e2ffe729`; signed merge `012b158e`. Genuine macOS-only native Metal identity with no renderer fallback. Current portable evidence is 206/207 plus 206/206 sanitizer, with EasyGL and LLGL controls green. Historical Apple evidence (136/143) is explicitly not inherited; adapted Apple compile/runtime remains external. Known-wrong readback/MSAA and unsupported MRT/query/custom-effect/multistream/instancing paths are disabled deterministically; see `integration/lanes/metal.md` |
| `skia` | 141 | ✅ **INTEGRATED** — `adapt/skia` `a071e1e2`, signed merge `1381ff93`; 172/172 dedicated suite and cross-backend controls passed. The Ganesh subpath remains paused by owner decision (`SKIA-163`) without changing the accepted raster identity; see `integration/lanes/skia.md` |

**Group status after Metal:** Skia + Direct2D + LLGL + Metal are integrated. Group G is 4/4 and
the inventory is 21/21 with zero pending lanes. Technical Batch 6 stabilization is READY, the fresh
retake defined in §5 passed, and the signed local checkpoint is TAKEN. Group and checkpoint
completion do not themselves claim final campaign or `develop` readiness.

---

## 4. First integration lane — **`feature/depthcrt`** · ✅ **INTEGRATED 2026-08-04**

> **Outcome.** Adapted on `adapt/depthcrt` and merged into `integration/post-audit-phase1` as
> `61bd1a1b` (signed, `--no-ff`). Five adapted commits `88244b3a`, `3299c211`, `b9b63809`,
> `0998acc4`, `3cca0b19` — all GPG-signed, all authored **and** committed by Robert Vokac, all
> trailers stripped, zero attribution hits. `f05e07c8` dropped as superseded by `REMED-BUILD-005`
> after direct comparison. `git range-diff` shows **zero patch-content divergence** — the five
> replayed patches are byte-identical to the originals. Full record, test matrix, sanitizer results
> and residual classification: **`integration/lanes/depthcrt.md`**.
>
> Two corrections to the plan below, both recorded on the lane card: `NOXNA.md` needed **four** rows
> (`N26`–`N29`), not two; and the proposed commit **ordering** was not used, because reordering the
> demo commit would have forced patch content to move between commits and broken the provenance
> criterion this same section sets. Chronological order was used instead.
>
> The original refs and `archive/preintegration/depthcrt-20260804` are unchanged, and nothing was
> pushed.

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/depthcrt` = `refs/remotes/origin/feature/depthcrt` |
| Head | `f4804469a6c14fac6215965794ba6786fc6c5b48` |
| Archive tag | `archive/preintegration/depthcrt-20260804`, signed, verified |
| Fork point | `ac3aaaeb` (`origin/develop`) — develop-forked, not audit-stacked |
| Own commits / files | **6 / 14** |
| Diff shape | **+1529, −0** — purely additive |
| Shared interfaces | **none** |
| `GpuDrawParams` cost | **zero** — grep over its changed files finds none of the four removed fields |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED (total)** — 6/6 authored *and* committed by `Claude <noreply@anthropic.com>`, 6/6 unsigned, `Co-Authored-By: Claude Sonnet 5` + `Claude-Session:` trailers throughout |
| Conflict class | **LOW** |
| Dependencies | none |

### Why this lane and not another

- **It cannot damage the common interfaces**, because it touches none of them. 11 of its 14 files do
  not exist at the checkpoint at all.
- **It is real production code, not documentation.** `ext` and `gltf` are each one docs-only commit
  changing one file — they would validate nothing but the commit ceremony. `depthcrt` adds two NOXNA
  post-process effects (`CNA::Graphics::DepthEffect`, `CNA::Graphics::CRTEffect`), their enums,
  their unit tests and two demo harnesses. A build and a test run mean something.
- **It exercises the entire history policy on the smallest possible sample.** All six commits need
  author rewriting, trailer stripping and GPG signing. That machinery must be proven before it is
  pointed at `skia`'s 141 commits or `llgl`'s 68.
- **It has no dependency**, external or internal, and it is not `feature/gl`.
- **Its verification story is honest.** `CRTEffect.hpp`'s own commit body records that the
  curvature/vignette-via-`vTexCoord` limitation *"was caught and fixed during manual verification,
  not assumed correct from the shader math alone"* — the lane arrives with its own known boundary
  documented.

### Exact adaptation scope

**Files (14):** 11 new, 3 pre-existing.

| Status | Path |
|---|---|
| new | `include/CNA/Graphics/DepthEffect.hpp`, `DepthEffectMode.hpp`, `DitherMode.hpp` |
| new | `include/CNA/Graphics/CRTEffect.hpp`, `CRTMaskType.hpp` |
| new | `src/CNA/Graphics/DepthEffect.cpp`, `src/CNA/Graphics/CRTEffect.cpp` |
| new | `tests/CNA/Graphics/DepthEffectTests.cpp`, `tests/CNA/Graphics/CRTEffectTests.cpp` |
| new | `examples/depth_effect_demo_test.cpp`, `examples/crt_effect_demo_test.cpp` |
| **exists** | `NOXNA.md` — append entries N28/N29 only |
| **exists** | `cmake/Examples.cmake` — add the two demo targets |
| **exists** | `cmake/Harnesses.cmake` — **see the drop below** |

**One commit must be dropped, not adapted.** `f05e07c8`
(`fix(build): link SDL3 to the audio-mixer-destroy standalone harnesses`, +2 lines in
`cmake/Harnesses.cmake`) is **superseded**: the checkpoint already contains `REMED-BUILD-005`, which
fixes the same harness by the same means and documents why transitive `SDL3::SDL3` propagation
cannot be relied on under cross-compile toolchains. Carrying `f05e07c8` forward would re-apply a fix
that is already present in a better form. It is also unrelated to CRT/Depth work — it was bundled
into this branch incidentally.

**Proposed recomposition — 5 clean commits from 6 original:**

| # | Subject | Original commits |
|---|---|---|
| 1 | `feat(NOXNA): add DepthEffect colour-depth-reduction post-process` | `5c4ebf06` |
| 2 | `feat(NOXNA): add ordered Bayer dithering to DepthEffect` | `c580b3d7` |
| 3 | `feat(NOXNA): add Palette256/Palette16 nearest-colour modes to DepthEffect` | `b1525cf4` |
| 4 | `feat(NOXNA): add DepthEffect manual verification demo` | `e84f0c05` |
| 5 | `feat(NOXNA): add CRTEffect post-process (scanlines, RGB mask, curvature, vignette)` | `f4804469` |
| — | *dropped — superseded by `REMED-BUILD-005`* | `f05e07c8` |

Each adapted commit: authored and committed by the configured human maintainer identity, GPG-signed,
with every `Co-Authored-By:` and `Claude-Session:` trailer removed and the technical body preserved.
No third-party author may be invented; no AI attribution may appear.

### Test matrix

| Check | Command / target | Expected |
|---|---|---|
| Library build | `cmake --build cmake-build-debug --target CNA -j8` | clean |
| Unit tests | `CnaTests` → `DepthEffectTest.*`, `CRTEffectTest.*` | all pass; `CRTEffectTest` is 9 cases, 27 total across `CRTEffectTest`/`DepthEffectTest`/`ShaderEffectTest` per the original commit's own record |
| Demo harnesses | `cna_depth_effect_demo`, `cna_crt_effect_demo` | build; run under Xvfb with `SDL_VIDEODRIVER=x11` |
| No regression | full `CnaTests` | no new `FAILED` — grep the **complete** log, never a truncated tail |
| Provenance | `git range-diff archive/preintegration/depthcrt-20260804...<adapted>` | differences confined to author, committer, trailers, signature, and the dropped `f05e07c8` |
| Attribution | `git log <base>..<head>` searched for the banned tokens | zero hits |
| Signatures | `git log --format='%G?'` over the range | every commit good |

### Completion criteria

1. All five adapted commits on `integration/post-audit-phase1`, GPG-signed, human-authored.
2. `f05e07c8`'s omission recorded in the lane's integration note with its supersession reason.
3. Build and both test suites green; full log verified, not tail-sampled.
4. `range-diff` produced and attached to the lane card.
5. `archive/preintegration/depthcrt-20260804` still points at `f4804469` and
   `refs/heads/feature/depthcrt` is **unmodified**.
6. Zero attribution hits over the adapted range.

### The next adaptation task

**Batches 0, 1 and 2 are all CLOSED; Batch 3 is OPEN at 1 of 2.** Batch 0's four lanes (`depthcrt` §4, `gltf` §4.1, `ext`
§4.2, `dxold` `integration/lanes/dxold.md`), Batch 1's five (`stub`, `opengles1`, `opengl4`,
`opengl1`, `opengl2`), Batch 2's two (`wicked`, `magnum`) and Batch 3's first (`sokol`,
`integration/lanes/sokol.md`) are integrated — **twelve of the 21 logical lanes, 9 pending**. Batch 1 was stabilized on 2026-08-05
(`integration/BATCH_1_STABILIZATION.md`); **Batch 2 was stabilized on 2026-08-06 and its
checkpoint decision is BLOCKED** on exactly one open defect, `WICKED-80` — full record
`integration/BATCH_2_STABILIZATION.md`; every other gate passed, `REMED-GFX-222` was discovered
and resolved in-stabilization, and no checkpoint tag was created.

`WICKED-80` was resolved and the Batch 2 checkpoint retaken as ACCEPTED
(`integration/BATCH_2_STABILIZATION.md` §12/§13), and Batch 3 then opened with `sokol`. The
recommended next action is the **`diligent` lane** — Batch 3's second and last. The Batch 3
checkpoint belongs after it, not after `sokol` alone.

What `depthcrt` proved about the process, for whoever runs the next lane:

- **`git cherry-pick -n` + a fresh signed commit** is the whole re-authoring mechanism. It preserves
  the patch exactly, drops the original author/committer/trailers, and costs nothing to audit.
- **Replay in chronological order.** Any reordering forces hunks to migrate between commits and
  destroys the range-diff's value as evidence.
- **Diff the lane against the checkpoint before replaying**, per shared file. `depthcrt` would
  otherwise have silently reverted `REMED-BUILD-002` in `cmake/Examples.cmake`; the 3-way merge got
  it right, but only a post-hoc diff against the original head proved it.
- **Classify every test failure with a control run on the checkpoint build.** Both of `depthcrt`'s
  failures looked alarming and were pre-existing; the control took two minutes and settled it.
- **Check the lane's config actually exists in some build tree.** All 26 pre-existing trees are
  `CNA_NOXNA=OFF`; a lane that is entirely `#ifdef CNA_NOXNA` cannot be validated by any of them.

---

## 4.1 Second integration lane — **`feature/gltf`** · ✅ **INTEGRATED 2026-08-04** · **DIRECT MERGE**

> **Outcome.** Merged into `integration/post-audit-phase1` as **`722a2f5a`** (signed, `--no-ff`,
> parents `61bd1a1b` + `86ada7a7`). **No adaptation branch was created and no commit was recreated** —
> the lane's single commit `86ada7a7` is preserved as the same object and is now an ancestor of the
> integration branch. Full record: **`integration/lanes/gltf.md`**.

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/gltf` = `refs/remotes/origin/feature/gltf` |
| Head | `86ada7a7bdc7c8e76fff536be4f6c1f5bff3df43` |
| Archive tag | `archive/preintegration/gltf-20260804`, signed, verified, unchanged |
| Merge base | `32639a13` — audit-stacked, and already an ancestor of the integration branch |
| Own commits / files | **1 / 1** · `+451, −0` — one root-level document, `gltfissues.md` |
| History class | **HISTORY CLEAN** — authored *and* committed by Robert Vokac, GPG-signed, empty body, empty trailer set |
| Path taken | **DIRECT MERGE** |

### Why direct merge, and why that is not inconsistent with `depthcrt`

All nine direct-merge conditions held, so policy P1/P2 was satisfied at zero cost: the history was
already what adaptation exists to produce. `depthcrt` was adapted because it was
`AUTHOR/TRAILER CLEANUP REQUIRED` on 6 of 6 commits with 0 of 6 signed. **Different history class,
different treatment** — which is exactly what §3 says Batch 0 is for.

Note that `INTEGRATION_HISTORY_POLICY.md` §4 F5 ("no merge commits during adaptation") is scoped to
adaptation, whose purpose is to keep `range-diff` meaningful. Nothing was replayed here, so there is
no range-diff to protect; **object identity is the stronger losslessness proof** and it was used
instead.

### What this lane does and does not add

It adds a **dated analysis of open glTF import defects** — discarded node transforms, lost
`baseColorFactor`, a map-gated PBR selection condition, ignored `KHR_materials_transmission`, plus
sampler, multi-UV, rigid-animation, variant and sRGB losses.

**It implements none of them.** The document's *Recommended Repair Order* (P0–P2) and its twelve
*Missing Regression Tests* are proposals; none exists at the integration head. This merge closes no
ticket and adds no glTF capability.

### Staleness, measured rather than assumed

The document pins its own baseline in its body (analysis date 2026-07-28, commit `32639a13`). Of the
nine source files it cites, **eight are byte-identical** between that baseline and the integration
head, so its findings still describe current behaviour with their line numbers intact. The ninth,
`EasyGLGraphicsBackend.cpp`, was rewritten by the remediation campaign: the PBR shader line cited as
`4121` now sits at `4948`, while the quoted expression is unchanged.

**That citation was deliberately left as written.** Rewriting it would make a document that declares
itself a 2026-07-28 analysis of `32639a13` cite a line that did not exist then. The drift is recorded
on the lane card instead.

---

## 4.2 Third integration lane — **`feature/ext`** · ✅ **INTEGRATED 2026-08-04** · **ADAPTED**

> **Outcome.** Adapted on `adapt/ext` as **one** commit `c6a28036` — re-authored to
> `Robert Vokac <robertvokac@robertvokac.com>`, both prohibited trailers stripped, GPG-signed — and
> merged into `integration/post-audit-phase1` as **`8a374b9f`** (signed, `--no-ff`). Full record:
> **`integration/lanes/ext.md`**.

| Field | Value |
|---|---|
| Ref | **remote-only** `refs/remotes/origin/feature/ext` — no local branch |
| Head | `05ab5d3d002945c603fc28f2a5a23f8027773d63` |
| Archive tag | `archive/preintegration/ext-20260804`, signed, verified, unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked |
| Own commits / files | **1 / 1** · `NOXNA.md`, `+568, −241` |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED (total)** — 1/1 |
| Path taken | **ADAPTATION** |

### What it is

`NOXNA.md` rewritten as the authoritative extended-graphics design. Its central contribution is a
boundary that had never been written down: the **always-compiled `NOXNA`/`*EXT` marker convention**
in `Microsoft::Xna::Framework::Graphics` versus the **`CNA_NOXNA`-gated `CNA::Graphics` engine
layer**. It then records what already ships, corrects four stale claims, specifies the remaining
classes/enums/backend virtuals, and renumbers the backlog.

**Documentation only.** One file, no build input, no compiled source — verified by tree-hash equality
outside `NOXNA.md`. It implements nothing and closes no ticket. **Every testable claim it makes was
measured against the integration head and holds** (missing `NOXNA.hpp`, absent capability enums,
`RenderPipelineSettings` with no consumer, HDR `SurfaceFormat`s present, PBR/glTF/morph/Draco
shipped).

### Two things worth carrying forward

**1. A one-file lane is not a zero-conflict lane.** `ext` renumbers the same `NOXNA.md` backlog table
that `depthcrt` had appended `N26`–`N29` to, and its rewrite was authored against a base predating
them. Resolving in favour of the incoming side would have **deleted four rows of already-integrated
work**. The four rows were preserved verbatim; the renumbering happens to leave `N26`–`N29` free, so
they kept both their numbers and their position. `depthcrt`'s own lesson — *diff the lane against the
checkpoint before replaying, per shared file* — is what caught it.

**2. `%G?` cannot tell "unsigned" from "SSH-signed and uncheckable".** `05ab5d3d` is **not** unsigned
as the inventory records: it carries an SSH signature from a non-maintainer key, and `%G?` still
reports `N`. The required action was unchanged, but `opengl4`/`magnum`/`wicked` should be re-derived
with `git cat-file -p` rather than from the inventory's signature row.

### One consequence deliberately left open

The renumbering **invalidates four cross-references** from files outside the lane —
`include/CNA/Graphics/PbrMaterial.hpp:19` and `noxna_devices.md:93` (`N11`),
`docs/surface-format-support.md:184,220` (`N20`), and `plans/plan_postaudit.md:1572-74`
(`N50`/`N51`/`N52` plus an old section number). Two further citations live under `audit/`, which is
frozen. `DitherMode.hpp:14`'s `N70` still resolves correctly.

**Not fixed in the lane** — repairing them means editing four files outside a one-file lane and
touching `audit/`. **Owner: the Batch 0 stabilization checkpoint.**

---

## 5. Stabilization checkpoints

| After | Checkpoint | Signed tag |
|---|---|---|
| Batch 0, lanes 1–3 | **Intermediate.** Process proven: history policy, signing and range-diff reporting validated across all three history classes | `integration/checkpoint-batch0-20260804` → `e0332214` ✅ **TAKEN 2026-08-04** |
| Batch 0, complete | **Final.** All four lanes integrated — `dxold`'s eight Historical backends and the owner-ordered FREEDIRECT/DX3 naming transition included | `integration/checkpoint-batch0-complete-20260804` → `990d6b8a` ✅ **TAKEN 2026-08-04** · `integration/BATCH_0_COMPLETE.md` |

> **✅ DONE 2026-08-04 — see `integration/BATCH_0_STABILIZATION.md`.** Every item in the scope below
> was completed:
>
> - **full build + `CnaTests` on the integration HEAD** — 5912 run · 5906 passed · 6 skipped ·
>   **0 failed**, against `61bd1a1b`'s 5904/13/**2**. Zero integration regressions;
> - **the `NOXNA.md` cross-reference repair** — all four live non-`audit` citations repaired
>   semantically; the three `audit/` citations deliberately left untouched by owner decision, since
>   they are dated evidence of what the header said when it was audited;
> - **`XnbContainerFuzzTest`** — reproduced and read rather than accepted. The exit record's
>   `REMED-GFX-DECL-GUARD` attribution is **wrong**; the throw is `System::ArgumentException` from
>   `VertexBuffer::SetData`. Resolved as a fuzz-contract oracle completion, test-only, with an
>   injected-exception negative control proving the oracle is still not a catch-all;
> - **a signed checkpoint tag** — `integration/checkpoint-batch0-20260804`.
>
> One thing the plan above did not anticipate: the same NOXNA citations on `feature/audit` are
> **correct there**, because that branch still carries the pre-`ext` `NOXNA.md`. The repair is
> integration-branch-only.
>
> `dxold` — 28 commits, 225 files — is Batch 0's last lane and belongs **after** this checkpoint.
| Batch 1 | GL family landed; declaration guard verified across five backends | `integration/checkpoint-batch1-<date>` |
| Batch 3 | Majority of medium backends landed; capability matrix regenerated | `integration/checkpoint-batch3-<date>` |
| Batch 5 | All non-deferred lanes landed; technical matrix green; mandatory Content gate closed on retake | `integration/checkpoint-batch5-20260808` → `c805fd73` ✅ **TAKEN 2026-08-08** (tag object `307c9ad5`) |
| Batch 6 | Group G complete at 4/4; conservative Metal source-continuity contract and cross-backend controls green; clean/signature/invariant retake passed | `integration/checkpoint-batch6-20260809` → `012b158e` ✅ **TAKEN 2026-08-09** (tag object `8d347c93`) |

**Batch 6 final checkpoint decision, 2026-08-09: COMPLETE.** The initial technical decision was
READY subject to one fresh retake. That retake passed. Signed annotated tag
`integration/checkpoint-batch6-20260809`, exact message `CNA integration Batch 6 checkpoint`, was
created once without force as tag object `8d347c933a3da3c39f22711e40e80cf7a29c4682` and peels to
`012b158eb8246ce267887acbd4fc7a2468d89e52`. `git tag -v` exits 0 with a Good signature from Robert
Vokac under fingerprint `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. The tag is local only and
was not pushed. `integration/BATCH_6_STABILIZATION.md` preserves the initial READY decision and the
completed retake. This checkpoint is a Batch 6 integration milestone, not a final campaign or
`develop` readiness declaration.

**`REMED-CONTENT-007` / `-008` must be closed before the Batch-5 checkpoint**, and before any
public security-clean claim. See §6.

**Batch 5 decision, 2026-08-08:** all three lanes are accepted and technical stabilization passes,
but both mandatory findings remain HIGH/P1 OPEN. Therefore no
`integration/checkpoint-batch5-20260808` tag was created. The exactly bounded next task is to close
both path-containment findings together, then retake the checkpoint decision; no later graphics
lane has begun.

**Batch 5 required retake, later 2026-08-08: READY.** The decision above remains the historical
first attempt. Both mandatory findings are DONE with independent caller coverage; the bounded
same-pattern audit's `REMED-CONTENT-011` is also DONE; the final integration tree passes 46/46
focused containment, 116/116 relevant Content/Song/Video, 78/78 Glide portable, and 57/57 exact
HTML DOM host controls with linked ASan/UBSan and leak detection. All three lane acceptances and
signatures remain intact, 18/21 lanes are integrated, no nineteenth lane began, and no other
mandatory blocker exists. Local signed annotated tag `integration/checkpoint-batch5-20260808`
(object `307c9ad511015c64ce55184cdf0d5ebd7b1cb575`) peels to
`c805fd737f4321568fba378e8d1b8fe5b5270666` and verifies Good. Nothing was pushed. Full retake:
`integration/BATCH_5_STABILIZATION.md` §7.

---

## 6. `REMED-CONTENT-007` / `REMED-CONTENT-008` — CLOSED by required Batch 5 retake

**Current status: DONE 2026-08-08.** At the first Batch 5 decision these were HIGH/P1 OPEN and
outside that graphics-lane session; the original findings and placement analysis below are
preserved as historical gate evidence. Signed planning test/fix commits `569beedd`/`062ca70c` and
signed integration equivalents `2d795473`/`c805fd73` now enforce the shared component-aware
contract. `remediation/REMEDIATION_PROGRESS.md` is the authoritative technical closure record.

- **Original `REMED-CONTENT-007` finding** — `SongContentTypeReader.cpp` and
  `VideoContentTypeReader.cpp` each defined a private `ResolveRelativeFilePath()` with **no
  containment check**, fed directly by the `.xnb`'s own embedded filename. Both now use the shared
  primitive before media construction and after extension probing.
- **Original `REMED-CONTENT-008` finding** — `ContentManager.cpp` contained **zero** calls to
  `IsDisallowedAbsolutePath` / `ResolveContainedPath` / `PathContainment`, while joining eight
  manifest-supplied path fields onto the content root raw (`:560`, `:779-780`, `:1364`, `:2167`,
  `:2267`, `:2269`, `:2312`). The neighbouring `sourceFile` field **is** hardened via
  `CnjSourceFile.hpp`, which made the omission visible. All affected callers now validate before
  reads or recursive loads.

The required bounded same-pattern audit found additional genuine omissions and assigned
`REMED-CONTENT-011`; it closed in the same bounded fix. There is no remaining category-D hit.

**Both are non-blocking for the phase-1 checkpoint that already exists** — they fall outside its
declared blocker classes (`REMEDIATION_EXIT.md` §2.1, §4.4). That remains true and is not revisited
here.

**Both were required before the Batch 5 checkpoint or any public security-clean claim. That
requirement is now fulfilled.**

**Historical placement analysis.** They were best run as a **parallel safety lane during Batch 0–1** rather than waiting
for a post-integration batch: the fix is mechanical (the helper and the correct pattern both already
exist), it touches `Content/` only, and `Content/` is touched by **none** of the 21 integration
lanes — so it cannot conflict with the campaign. The alternative placement is the first
post-integration stabilization batch.

**Historical conditional integration rule.** If any lane selected for integration touched `ContentManager`,
`ContentReader`, the XNB type readers, or external resource resolution, then
`REMED-CONTENT-007`/`-008` became **hard blockers for that lane** and had to be closed before it landed.

Measured across all 21 lanes from their own fork points:

- **No lane touches any file the two findings live in** — not `ContentManager.cpp`, not
  `ContentReader.cpp`, not `SongContentTypeReader.cpp`, not `VideoContentTypeReader.cpp`, not
  `PathContainment.hpp`. **No lane is currently a hard blocker.**
- **Five lanes touch Content-adjacent files** and must be re-checked at adaptation time rather than
  waved through on the strength of this paragraph:

| Lane | File | Assessment |
|---|---|---|
| `skia` | `src/CNA/Internal/Xnb/Texture2DContentTypeReader.cpp` (**production**) | Mip-chain and existing-instance validation only. **Inspected: the change contains no path, `ReadString`, `fs::` or resolution logic.** Does not interact with either finding — but it is the only *production* content-reader change in the inventory, so re-verify it when `skia` is scheduled |
| `html-dom` | `tests/CNA/Internal/Xnb/Texture3DTextureCubeContentTypeReaderTests.cpp`, `tests/…/Content/CnjCapabilityMatrixTests.cpp` | tests only |
| `gdi` | same two test files | tests only |
| `sokol` | same two test files | tests only |
| `diligent` | `tests/…/Content/CnjEffectTests.cpp`, `CnjStockEffectTests.cpp` | tests only |

**`feature/gltf` — re-checked at integration and confirmed not a blocker.** Its single commit adds
one Markdown file and changes no code at all, so it touches `ContentManager`, `ContentReader`, the
XNB type readers and external resource resolution not at all. The document *discusses* the glTF
import path, but discussing it introduces no path-resolution code. See `integration/lanes/gltf.md`.
