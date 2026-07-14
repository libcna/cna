# Direct3D 9 Graphics Backend — Implementation Plan

> **Status (2026-07-14): AUTHORIZED FOR IMPLEMENTATION.** The project owner approved implementation
> through Phase D9-13 (Phase D9-11 "custom `ShaderEffect`" stays ask-first per its own row; Phase
> D9-14 needs real Windows hardware and is out of scope for this dev environment). Both Phase D9-0
> existence gates were spiked and BOTH PASSED. What has been proven, on this machine, with real runs:
>
> - **`D9-1` ✅ — Microsoft's own XNA 4.0 stock-effect shaders compile.** All **66/66** entry points
>   across all 6 effects, from Microsoft's **unmodified** `.fx` sources, to real D3D9 bytecode, using
>   the real Microsoft `d3dcompiler_47.dll` (Wine's builtin cannot — it fails outright on an ordinary
>   alpha-test ternary). The 72-bone skinning shader comes out at **2,160 bytes** where Wine's
>   compiler produced a 1.4 MB unrolled blob.
> - **`D9-73` 🟨 — and 61 of those 66 are byte-identical to the bytecode Microsoft actually shipped.**
>   The 5 that differ are all `PixelLighting` variants, and it is a compiler-*version* difference, not
>   a flag (six flag combinations tested, none matched). Microsoft's originals for those 5 are sitting
>   in the `.fxb`. This row is DECIDED — see design decision 4.
> - **`D9-A1`/`D9-A2` ✅ — real XNA 4.0 runs on this machine and renders.** `winetricks dotnet40
>   xna40` into a dedicated 32-bit prefix; a ~50-line `BasicEffect` reference app, compiled by the
>   in-prefix `csc.exe` (no Visual Studio, no XNA Game Studio, no content pipeline, no Windows box),
>   renders a triangle and dumps a PNG whose background is **exactly `Color.CornflowerBlue`
>   (100,149,237)**. **The oracle this whole plan depends on exists.**
>
> **Phase D9-0 is cleared, and implementation is now authorized.**
>
> **RESOLVED 2026-07-14 — "The `IGraphicsBackend` boundary problem" (below) is no longer blocking.**
> The project owner approved this document's own recommendation: an additive extension to
> `GraphicsBackendCreateArgs` (new optional fields: `BackBufferFormat`, `DepthStencilFormat`,
> `IsFullScreen`, `PresentInterval`, `GraphicsProfile`) plus a narrow backend→`GraphicsDevice`
> device-event notification channel (for `DeviceLost`/`DeviceResetting`/`DeviceReset`), both
> no-op/ignorable for the other nine backends. `D9-30`/`D9-32`/`D9-33`/`D9-34` are unblocked. The
> section below is kept as-is for its factual analysis of the problem; treat its "recommendation, not
> a decision" framing as superseded by this approval.
>
> A second readiness audit (2026-07-14, independent agent, adversarial) found the boundary
> contradiction above, and also found and fixed: a stale 64-vs-66 shader count, a self-contradicting
> D9-73 row, two additional undocumented `ShaderIndex` inputs (`AlphaTestEffect.isEqNe`,
> `EnvironmentMapEffect.specularEnabled`), a fact/fiction gap in D9-11's "override everything"
> instruction, an unspecified shader-compile Wine prefix (no prefix currently has both the real
> compiler and DXVK), and a missing CMake site (7 sites need editing, not 3). All now corrected in
> place below. A companion fact-check audit verified every concrete path/line-number/source claim in
> this plan against the actual repo and found the plan otherwise accurate.
>
> **⚠ Read "CNA's divergences from XNA 4.0" below before authorizing anything.** Taking XNA seriously
> as the specification has already, with zero backend code written, surfaced **six confirmed
> divergences between CNA and XNA 4.0** — the worst being that CNA ignores `PreferPerPixelLighting`
> and lights **per-pixel always**, where XNA's default is **per-vertex**, and **CNA has no per-vertex
> lighting shader on any of its nine backends.** These are not D3D9's bugs; they are CNA's, they are
> present everywhere, and **while they stand, "indistinguishable from XNA 4.0" is unreachable as a
> product goal.** They must be fixed — and that section states exactly what fixing them will cost.
>
> **Goal, stated by the project owner and binding on every task in this plan:**
> *a CNA game running on the Direct3D 9 backend should be indistinguishable from the same game
> running on the original XNA 4.0 runtime.* Not "close enough." Not "feature-equivalent."
> **Indistinguishable.** That is a much stronger bar than any other CNA backend has ever been held
> to, and it changes what "done" means: a task is not done when it renders plausibly, it is done when
> its output matches XNA's, verified against XNA itself (Phase D9-A).
>
> This plan is explicitly **not** a coverage/parity plan. Where authenticity and parity conflict,
> authenticity wins, and the conflict gets written down.

---

## Cold start — read this first if you are picking this up with no prior context

**You are not starting from zero. Phase D9-0 is already done, and its working code exists.**
Do not rewrite it from this document's prose.

**1. Read these, in this order, before writing anything.**

| What | Where | Why |
|------|-------|-----|
| `CLAUDE.md` | repo root | Binding. XNA API fidelity rules, `NOXNA`, SharpRuntime aliases, Doxygen requirements, commit discipline. Overrides your defaults. |
| `CHECKLIST.md` | repo root | Binding. The per-file porting checklist `CLAUDE.md` mandates for **every** ported file. Not optional, not deferrable. |
| **this file** | — | The what and the why. Especially "CNA's divergences from XNA 4.0" and the Boundaries at the end. |
| `dx9-spike/README.md` | sibling of this file | **The proven Phase D9-0 artifacts.** A working shader compiler, a working `.fxb` verification oracle, a working real-XNA reference app, and the Wine prefixes they need. All of it has actually run. |
| `plan_dx.md` | repo root | The D3D11/D3D12 plan. **Not your plan** — but the conventions it established (CTest shape, DXVK gating, mutation-verification discipline, `ComPtr`, MinGW cross-build loop) are the ones you inherit. Read its "Design decisions" and its Boundaries. |
| `docs/d3d11-backend.md` | repo | What a finished backend's docs look like here. |

**2. The reference sources you will be working against are on disk, not on the internet.**

- **Microsoft's own XNA 4.0 stock effect HLSL** (MS-PL, the heart of this whole plan):
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/HLSL/`
- **Microsoft's shipped bytecode** (your verification oracle, not a source of shipped bytes —
  design decision 4): `.../StockEffects/FXB/`
- **The C# that computes XNA's `ShaderIndex`** (you transcribe this, you do not invent it):
  `.../StockEffects/BasicEffect.cs` and its five siblings.
- **`.../StockEffects/EffectHelpers.cs`** — do not skip this one. It computes the *values* XNA
  actually uploads into Microsoft's constant registers (packed `FogVector`, `EmissiveColor =
  (emissive + ambientLight·diffuse)·alpha`, `WorldInverseTranspose`, eye position from the inverted
  view matrix). `GpuDrawParams` carries CNA's own, differently-cooked versions of some of these
  (see Divergence 6) — `D9-82` cannot upload a correct register value without reading this file
  first and reconciling the two.

**3. The Wine prefixes already exist. Using the wrong one will waste you a day.**

- `~/.wine-cna-d3d9-spike` — has the **real Microsoft `d3dcompiler_47.dll`**. Compile shaders here.
- `~/.wine-cna-xna40` — has **real XNA 4.0** (win32, .NET 4.0, all assemblies, an in-prefix `csc.exe`).
- `~/.wine-cna-d3d11` — **do not touch.** It is what the existing D3D11/D3D12 CTests run against, and
  its `d3dcompiler_47.dll` is Wine's builtin, which **cannot compile SM2/SM3 shaders at all**. Shader
  work done there fails in ways that look like your bug rather than the compiler's.

See `dx9-spike/README.md` for exactly what is in each and how they were built. `programs.md` in the
repo documents only the D3D11 prefix — closing that gap is part of `D9-130`.

**4. If you are working in a parallel worktree (`cna_dx9` on `feature/dx9`) while another agent works
on D3D11/D3D12 in `cna_graphics` on `feature/graphics`, these rules keep the eventual merge boring:**

- **UPDATED 2026-07-14 by the project owner: `NEXT.md` in the `feature/dx9`/`cnadx9` worktree is now
  the DX9 status file, not off-limits — but note the correction below, made the same day, to why.**
  **CORRECTION (later same day):** the first version of this note claimed `feature/graphics` was
  "done and merged into `develop`," which is **not accurate** — `cna_graphics` on `feature/graphics`
  is a separate, independent clone (its own `.git`, same GitHub remote) with a live session still
  actively committing there (confirmed via `ps`/`git log` 2026-07-14: `05ccd6ff` exists on
  `feature/graphics` and is not yet in `develop`). The real reason repurposing `NEXT.md` here is safe
  is simpler and does not depend on the other branch's status at all: **`cnadx9` and `cna_graphics`
  are separate physical directories/clones**, so there is no live file-level race on `NEXT.md`'s bytes
  between the two sessions regardless of either one's progress — reconciliation still happens once,
  by hand, at actual merge time, exactly as the original rule below already said. The project owner
  has simply decided, as owner, that *this* worktree's own copy should be DX9-scoped now rather than
  waiting for that merge. Keep `NEXT.md` in *this* worktree scoped to D3D9 status only (mirroring how
  `plan_dx.md`/`NEXT.md` related for D3D11/D3D12); point to `plan_dx.md`/`plan_graphics.md` for other
  backends' history instead of duplicating it — and remember `feature/graphics` is still moving, so
  that history is not final yet either. The original rule text is kept below for historical context
  only:
  ~~**Never touch `NEXT.md`.** It gets ~500 commits a week from the other agent. Keep all your status in
  *this* file. `NEXT.md` is reconciled once, by hand, at merge time.~~
- **`GpuDrawParams`, `D3DCommon/`, `D3D11/`, `D3D12/`: still never touch.** Cross-cutting/out of this
  plan's scope regardless of worktree state (see Boundaries and "CNA's divergences from XNA 4.0").
  **`IGraphicsBackend.hpp` (and `GraphicsBackendCreateArgs`): touch is now narrowly authorized** —
  see the RESOLVED note under "The `IGraphicsBackend` boundary problem" above. Additive only: the
  agreed new `GraphicsBackendCreateArgs` fields and the one new device-event notification channel,
  nothing else.
- Do the `CMakeLists.txt` wiring **early, once, and purely additively** (new `elseif()` blocks *after*
  D3D12's; add `D3D9` to the existing `OR`-conditions). After that, touch it only to register tests.
  It sees ~200 commits a week — every extra edit is a merge conflict you are choosing to have.
- Write a **new** `scripts/run-wine-dxvk9.sh` rather than editing the shared
  `scripts/run-wine-dxvk.sh`.
- **Defer all doc-registry edits** (`README.md`, `docs/graphics-backend-feature-matrix.md`,
  `THIRD_PARTY_NOTICES.md`) to **one** final commit. One conflict-prone commit beats fifty.
- Rebase onto `feature/graphics` regularly, not once at the end.

**5. Neither the worktree nor its branch exist yet — verified 2026-07-14 (`git worktree list` shows
only `cna_graphics` on `feature/graphics`; `git branch -a` has no `feature/dx9`).** Whoever starts
implementation creates both, from `feature/graphics` (not `master` — `master` is ~2,361 commits
behind and effectively empty), as the literal first action, before task `D9-10`:

```bash
cd cna_graphics  # the existing repo, wherever it's checked out
git worktree add ../cna_dx9 -b feature/dx9 feature/graphics
```

Then move this file and `dx9-spike/`'s contents into the new `cna_dx9` working tree (repo root next
to `plan_dx.md` for this file; `src/CNA/Internal/Backends/D3D9/shaders/` and `tools/xna-oracle/` per
the file-location table above) and commit them there. They currently live outside any repo only
because this plan was written and spiked while another agent held `cna_graphics`'s working tree on
`feature/graphics`.

---

## The decisive finding: Microsoft's own XNA shaders are already on this machine

Before this plan was rewritten, the assumed shader strategy was "port CNA's D3D11 HLSL down to Shader
Model 3." **That is no longer necessary, and would in fact be wrong.** Verified 2026-07-14 in the
project's own authoritative reference tree
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/`):

- **The original XNA 4.0 stock effect HLSL sources are there**, published by Microsoft as the "Stock
  Effects" sample (`http://xbox.create.msdn.com/en-US/education/catalog/sample/stock_effects`), and
  FNA's own `README` in that folder states they are included "almost exactly as it was published by
  Microsoft." The files: `BasicEffect.fx`, `AlphaTestEffect.fx`, `DualTextureEffect.fx`,
  `EnvironmentMapEffect.fx`, `SkinnedEffect.fx`, `SpriteEffect.fx`, plus the shared headers
  `Macros.fxh`, `Common.fxh`, `Lighting.fxh`, `Structures.fxh`.
- **They carry a native Direct3D 9 code path.** `Macros.fxh` branches on `SM4`; its `#else` branch —
  the DX9 branch — is written in D3D9's own idiom: `#define _vs(r) : register(vs, r)`,
  `#define SAMPLE_TEXTURE(Name, texCoord) tex2D(Name##Sampler, texCoord)`, `sampler_state { Texture =
  (Name); }`. These shaders were *designed* to compile for D3D9. CNA would not be porting them to
  D3D9; it would be compiling them the way they were originally meant to be compiled.
- **They carry Microsoft's own D3D9 constant-register layout, in the source.** `BasicEffect.fx`:
  `float4 DiffuseColor _vs(c0) _ps(c1)`, `float4x4 WorldViewProj _vs(c15)`,
  `float4x4 World _vs(c19)`, `float3x3 WorldInverseTranspose _vs(c23)`. `SkinnedEffect.fx`:
  `float4x3 Bones[SKINNED_EFFECT_MAX_BONES] _vs(c26)`. There is no register layout to design — it is
  given.
- **They target `vs_2_0`/`ps_2_0`**, not Shader Model 3. Every `compile` statement across all six
  effects is `compile vs_2_0 …` / `compile ps_2_0 …` (66 entry points, enumerated). This retires the
  biggest risk the previous draft of this plan carried.
- **They are MS-PL licensed** — the same license CNA itself ships under (`// SPDX-License-Identifier:
  MS-PL` is mandated at the top of every CNA file by `CLAUDE.md`). Vendoring them into CNA is
  license-clean.
- The compiled `.fxb` binaries are there too (`BasicEffect.fxb`, 28 KB, etc.), and FNA parses them
  with MojoShader (`lib/FNA3D/MojoShader`). **This plan does not use the `.fxb` files** — see design
  decision 4 — but their existence is a useful independent oracle.

**What this means.** The single largest authenticity risk in a D3D9 backend — "will our
reimplementation of BasicEffect's lighting math produce the same pixels XNA did?" — evaporates,
because the shaders would not be a reimplementation. They would be Microsoft's, compiled from
Microsoft's source, bound at Microsoft's registers, selected by Microsoft's shader-permutation
algorithm. **This is the strongest possible position for the goal the project owner has set**, and it
reshapes the plan around one organizing principle:

> **Wherever XNA's original artifact exists and can be used directly, use it. Reimplement only what
> genuinely cannot be reused.**

---

## Two consequences that reverse earlier assumptions

**1. Shader Model 2.0, not 3.0, is the authentic stock-effect target.** The previous draft of this
plan agonized over SM3.0's 256 vertex-register limit and declared `SkinnedEffect`'s 72 bones a forced
deviation. That was wrong on both counts. Microsoft already solved it: `float4x3 Bones[72] _vs(c26)`
= 216 registers at c26..c241, comfortably inside even SM2's budget, and it is *not a deviation* — it
is the original layout. And SM2.0 is what every stock effect compiles to. **Shader Model 3.0 is
therefore relevant only to (a) custom user `ShaderEffect`s under the `HiDef` profile and (b) the
device caps floor — not to the stock effect set at all.**

**2. Plain Direct3D 9 with `D3DPOOL_MANAGED`, not D3D9Ex.** The previous draft recommended D3D9Ex to
escape the device-lost state machine. Under the indistinguishability bar, that recommendation
inverts. XNA 4.0 used plain D3D9 — and CNA's own `GraphicsDevice` already proves it, because it
already declares XNA 4.0's device-lost event surface (verified 2026-07-14 in
`include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`):

```cpp
System::EventHandler<System::EventArgs> DeviceLost;
System::EventHandler<System::EventArgs> DeviceReset;
System::EventHandler<System::EventArgs> DeviceResetting;
```

Those three events exist in CNA's public API **because they exist in XNA 4.0's public API, and they
exist in XNA 4.0's public API because XNA ran on plain Direct3D 9.** On EasyGL, Vulkan, D3D11, and
every other CNA backend they are dead code that can never fire — there is no device to lose.
**The D3D9 backend is the only backend in this project that can ever fire them authentically**, and
under the indistinguishability bar that is not an inconvenience to engineer around with D3D9Ex; it is
a deliverable (`D9-34`). Choosing D3D9Ex would mean deliberately building the one backend that
*could* reproduce XNA's device-lost behavior, and then having it not do so.

The cost is real and must be accepted with open eyes: the `D3DERR_DEVICELOST` →
`TestCooperativeLevel()` → release-all-`D3DPOOL_DEFAULT` → `Reset()` → recreate dance is the classic
source of hard-to-reproduce bugs in hand-rolled D3D9 backends, and DXVK will rarely trigger it
naturally, so it lands substantially on `D9-140` (real Windows). The compensation is that
`D3DPOOL_MANAGED` — which D3D9Ex forbids — makes user resources survive `Reset()` automatically (which
is exactly *why* XNA 4.0 could promise that resources survive a device reset) **and** makes them
CPU-lockable, which turns the previous draft's fiddliest task (`Texture2D::GetData()` via a
`StretchRect`/`GetRenderTargetData` staging dance) into a plain `LockRect`.

Authenticity, here, is not merely the more faithful choice. It is also the simpler one everywhere
except device-lost itself.

---

## What "indistinguishable" concretely means, and how it gets proven

A claim this strong needs an oracle, not an opinion. **Phase D9-A builds one: the original XNA 4.0
runtime, executing, on this machine.** XNA 4.0 runs under Wine (it needs .NET Framework 4.0 + the XNA
4.0 Redistributable; `winetricks` — present at `/usr/bin/winetricks` — has both `dotnet40` and `xna40`
verbs). The harness renders the *same scene* twice — once through real XNA 4.0, once through CNA/D3D9
— and diffs the framebuffers.

That oracle then becomes the **acceptance criterion for every rendering task in this plan.** A task
is not "done because the triangle looks right"; it is done because its pixels match XNA's. This is a
categorically stronger standard than the cross-backend-comparison discipline CNA uses elsewhere, and
it is the only standard that actually tests the goal.

It also resolves, cleanly, the methodological problem that dominated the previous draft.
`CLAUDE.md` requires every method body to be verified line-by-line against the FNA equivalent, and
FNA has no D3D9 driver — so the previous draft had to construct an awkward three-link port chain and
ask for a sign-off to deviate. **That is no longer needed.** The reference for this backend is not FNA
and not a port chain: it is XNA itself, in two forms — Microsoft's shader sources for the shaders, and
the running XNA 4.0 runtime for the behavior. Both are *more* authoritative than FNA, not less. The
`CLAUDE.md` rule is satisfied in spirit and exceeded in practice.

---

## CNA's divergences from XNA 4.0 — confirmed, and they MUST be fixed

Before a single line of D3D9 backend code has been written, simply *taking XNA seriously as the
specification* has already surfaced real, confirmed behavioral divergences between CNA and XNA 4.0.
They are listed here because of a hard logical consequence:

> **While these divergences stand, "indistinguishable from XNA 4.0" is unreachable — not just for the
> D3D9 backend, but for CNA as a product.** They are not D3D9's problems. They are CNA's, and they are
> present on all nine existing backends. The D3D9 backend is merely the first thing in this project's
> history capable of *seeing* them.

**This plan does not fix them** (see Boundaries — they are cross-cutting and belong to the project
owner and to `plan_graphics.md`, not to a backend plan). But it must name them, size them, and state
plainly what fixing them will cost, because "we'll get to it" is not a strategy when the whole goal
depends on it.

### Divergence 1 — `PreferPerPixelLighting` is ignored, and CNA lights the *opposite* of XNA's default

**Status: confirmed by direct source inspection, 2026-07-14. This is the most consequential one.**

What XNA does. `BasicEffect.PreferPerPixelLighting` and `SkinnedEffect.PreferPerPixelLighting` are
public XNA 4.0 properties that select **where lighting is evaluated**, and in XNA they do not toggle a
uniform — they select an entirely **different compiled shader**:

- `false` (**XNA's default**) → `VSBasicVertexLighting*` / `VSBasicOneLight*`: lighting is computed
  **per vertex**, in the vertex shader, and Gouraud-interpolated across the triangle.
- `true` → `VSBasicPixelLighting*` + `PSBasicPixelLighting*`: lighting is computed **per pixel**, in
  the pixel shader.

This is why `BasicEffect.cs`'s shader-index computation reads `if (preferPerPixelLighting)
shaderIndex += 24; else if (oneLight) shaderIndex += 16; else shaderIndex += 8;` — three distinct
shader families, chosen at draw time.

What CNA does. Both of the following are true, and together they are worse than either alone:

1. **CNA declares, defaults, stores, and clones the property — and then never reads it.**
   `BasicEffect::getPreferPerPixelLightingProperty()` / `setPreferPerPixelLightingProperty()` are
   implemented, `preferPerPixelLighting_ = false` matches XNA's default, and the field is even copied
   in the clone constructor. But `BasicEffect::FillGpuDrawParams()` never touches it, and
   `GpuDrawParams` has no field for it. **It cannot reach any backend.** Same for `SkinnedEffect`.
2. **CNA's only lit shader variant computes lighting in the FRAGMENT shader.** `lit_textured3d`'s
   pixel shader does the full Blinn-Phong evaluation per pixel — `normalize(N)`, three `dot(N, -L)`
   diffuse terms, three half-vector `pow()` specular terms.

So CNA does not ignore the flag by falling back to XNA's default. **CNA ignores the flag by always
rendering per-pixel — the exact opposite of XNA's default.** And the deeper problem underneath:

> **CNA has no per-vertex lighting shader at all. Not on EasyGL, not on Vulkan, not on Bgfx, not on
> WebGPU, not on D3D11, not on D3D12, not on Software. The rendering mode that virtually every real
> XNA game actually shipped with does not exist anywhere in this codebase.**

Why it is visible, not academic. Per-vertex and per-pixel lighting differ most exactly where users
look: **specular highlights** smear, band, or disappear entirely under Gouraud interpolation on
low-poly geometry, and are smooth and round under per-pixel evaluation. Coarse meshes show
**faceting** under per-vertex lighting and appear smooth under per-pixel. Every CNA game today renders
XNA-default lighting *better than XNA rendered it* — which, under this plan's goal, is precisely a
divergence, not a bonus.

### Divergence 2 — `oneLight` is inferred, not known

XNA's `oneLight` is a real boolean ("only `DirectionalLight0` is enabled") and it selects a distinct,
cheaper shader family (`shaderIndex += 16`). CNA's `GpuDrawParams` does not carry it; a backend can
only *infer* it from `light1Diffuse`/`light2Diffuse`/`light1Specular`/`light2Specular` all being zero.
That inference **misfires for a light that is genuinely enabled but set to black** — a legal XNA state
that would select the three-light shader in XNA and the one-light shader in CNA. The pixel result is
likely identical (black contributes nothing), so this is the mildest divergence here — but it is a
structural one, and it must be measured against the oracle rather than waved away.

### Divergence 3 — `GraphicsProfile` is decorative

`GraphicsAdapter::IsProfileSupported()` is literally `return true;`.
`QueryRenderTargetFormat()` / `QueryBackBufferFormat()` accept a `GraphicsProfile` parameter and
`(void)`-discard it. **CNA therefore never enforces a single `Reach` restriction**, and an XNA game
targeting `Reach` — which in XNA would be refused an NPOT-wrapped texture, an over-large texture, a
`HiDef`-only format, or hardware instancing — silently gets all of them. CNA cannot currently *fail*
the way XNA failed. Phase D9-10 fixes this on D3D9, where the query is native; on the other backends
it is genuinely unfixable-in-principle (there is no `D3DCAPS9` to consult), which is why this plan
forbids faking it there.

### Divergence 4 — `SpriteBatch` has never modeled XNA's D3D9 texel convention

XNA's `SpriteBatch` ran on Direct3D 9, whose texel centers sit at integer coordinates (D3D10+ and
OpenGL put them at pixel centers). XNA compensated in the `MatrixTransform` it fed `SpriteEffect`.
CNA's `SpriteBatch.cpp` contains **no half-pixel handling whatsoever** (verified: zero occurrences of
`0.5f` / `halfPixel`). For CNA's modern backends this is *correct* — there is nothing to compensate
for. But it means CNA's sprite sampling has never been checked against the convention XNA-era content
and XNA-era tutorials were authored against, and a pixel-perfect 2D XNA game ported to CNA may sample
one texel off in ways nobody has ever measured. `D9-91` will measure it, on D3D9, against the oracle.

### Divergence 5 — CNA branches at runtime where XNA branched at compile time

XNA compiles one shader per feature combination (20 vertex × 10 pixel variants for `BasicEffect`
alone) and selects with `ShaderIndex`. CNA compiles ~10 monolithic "über-shader" variants and passes
`lightingEnabled` / `textureEnabled` / `vertexColorEnabled` / `fogEnabled` as uniforms that the shader
branches on. The *math* should be the same, so this is expected to be benign — but "expected to be
benign" is a hypothesis, and this plan's entire method is to stop accepting those. It is also the
reason CNA structurally *cannot* reach some XNA permutations (Divergence 1 being the proof). Measure
it; do not assume it.

### Divergence 6 — self-declared "numerically equivalent" deviations that were never verified

`GpuDrawParams`' own doc comments admit at least one: CNA folds `AmbientLightColor` into the
`DiffuseColor` multiply rather than using FNA/XNA's pre-baked "ambient+emissive" shader uniform,
described in-source as a *"numerically equivalent net result."* That may well be true. It has never
been checked against XNA. **Under the indistinguishability bar, every "numerically equivalent" claim in
this codebase is an untested assertion**, and the oracle (`D9-A`) is the first tool capable of
promoting them to facts — or demoting them to bugs. `D9-A6` should sweep for them deliberately.

### Summary

| # | Divergence | Confirmed? | Fixable on other backends? | Severity |
|---|-----------|-----------|---------------------------|----------|
| 1 | `PreferPerPixelLighting` ignored; CNA always per-pixel, XNA defaults per-vertex; **no per-vertex lighting shader exists anywhere in CNA** | ✅ source-verified | Yes — but it is real work | **High — visible on every lit scene** |
| 2 | `oneLight` inferred from zeroed colors, not carried | ✅ source-verified | Yes, cheaply | Low |
| 3 | `GraphicsProfile` never enforced (`IsProfileSupported()` = `return true;`) | ✅ source-verified | **No** — no caps to consult | Medium (behavioral, not visual) |
| 4 | `SpriteBatch` half-texel convention never modeled or measured | ✅ source-verified | N/A (correct as-is on modern APIs) | Unknown until measured |
| 5 | Runtime uniform branching vs. XNA's compile-time shader permutations | ✅ structural | Not worth "fixing" if pixels match | Unknown until measured |
| 6 | Unverified "numerically equivalent" shader-math deviations | ✅ self-declared in-source | Yes, if any prove wrong | Unknown until measured |

## Consequences of fixing these — read this before authorizing anything

The project owner has asked for indistinguishability, so these fixes are **not optional**. But they
are also not free, and the costs must be understood before the work starts, not discovered halfway
through.

**1. `GpuDrawParams` and `IGraphicsBackend` will have to change — the thing every backend plan in this
project has so far worked hard to avoid.** Divergence 1 needs at minimum a
`preferPerPixelLighting` flag (and Divergence 2 an explicit `oneLight`) added to `GpuDrawParams`. That
struct is consumed by **nine** existing backends, and would become ten once D3D9 lands. This is
exactly the cross-cutting change `plan_dx.md`,
`plan_webgpu.md`, `plan_headless.md`, and this plan's own Boundaries all forbid a backend author from
making unilaterally — which is why it must be a deliberate, owner-approved, `plan_graphics.md`-level
task, not a line item smuggled into a D3D9 phase.

**2. Every GPU backend needs a new shader variant it does not currently have.** A per-vertex lighting
path means a new vertex+fragment shader pair (or a new über-shader branch, if that is judged
acceptable) for **EasyGL (GLSL), Vulkan (GLSL→SPIR-V), Bgfx (its own shader toolchain), WebGPU (WGSL),
D3D11 (HLSL SM5), D3D12 (HLSL SM5)**, plus a **CPU per-vertex lighting path in the Software
rasterizer**. That is roughly 12–14 new shader files across four different shading languages, plus the
variant-dispatch, input-layout, pipeline-state and PSO plumbing each backend needs to select them.
**D3D9 is the only backend where this is free** — Microsoft's own `.fx` files already contain both
families.

**3. Existing pixel tests will start failing, and those failures will be CORRECT.** Once
`PreferPerPixelLighting`'s default is honored, every test that renders a lit scene produces **different
pixels** — flatter, faceted, with weaker or absent specular. CNA's suite has hundreds of such
assertions across `CnaTests` and the per-backend CTests. **Every lit-scene baseline must be
regenerated**, and the team must resist the overwhelming instinct to "fix" the new output back to the
old, prettier one. This is the single biggest practical risk in the whole endeavour: a correctness fix
that makes the product look worse is very easy to accidentally revert.

**4. Games and samples will visibly change — and users will call it a regression.** `cna-samples` and
any downstream CNA game will render with per-vertex lighting by default after the fix. Specular
highlights on low-poly models will lose their smooth round shape; coarse meshes will show faceting.
**This is XNA-correct.** It should be announced as such, loudly, in the release notes, alongside the
one-line escape hatch (`effect.PreferPerPixelLighting = true`) that restores the old look for anyone
who prefers it — which, notably, is exactly the same escape hatch XNA offered.

**5. Performance will improve, slightly.** Per-vertex lighting is strictly cheaper than per-pixel. This
is the one unambiguously good side effect.

**6. Docs, `AUDIT.md`, and the feature matrix all move.** `PreferPerPixelLighting` currently reads as
implemented (the property exists and compiles). It is not. Every place that claims `BasicEffect` /
`SkinnedEffect` completeness needs revisiting — and honestly, so does the confidence level attached to
every other "✅ implemented" row that has never been checked against XNA itself.

### Sequencing — the one thing that must not be gotten wrong

**Build the oracle first (`D9-A`). Then measure. Then fix. Then re-measure.**

Fixing Divergence 1 without the oracle means writing a per-vertex lighting shader for seven backends
and having **no way to tell whether the Gouraud math actually matches XNA's** — which would replace a
known, measured divergence with an unknown, unmeasured one, and would feel like progress. That is the
worst available outcome, and it is the default one if the fixes are started out of enthusiasm before
`D9-A` exists.

`D9-A6` (running the oracle corpus against CNA's *other* backends) is the task that converts this
section from an argument into a dataset. It should run early, and its output should be the input to
whatever `plan_graphics.md` task the project owner eventually opens.

---

## The `IGraphicsBackend` boundary problem — UNRESOLVED, blocks Phase D9-3

> **RESOLVED 2026-07-14 by the project owner: approved as recommended below** — the additive
> `GraphicsBackendCreateArgs` extension and the narrow device-event notification channel are both
> authorized. `D9-30`/`D9-32`/`D9-33`/`D9-34` are unblocked. The analysis below is kept for the
> record; its closing "do not let this recommendation be read as authorization" line no longer
> applies — it has been read as exactly that, explicitly, by the project owner.

**This is a genuine contradiction inside this plan, found by an independent readiness audit
2026-07-14, and it is not this document's call to resolve on its own.** Two of this plan's own
headline deliverables require exactly the thing its own Boundaries forbid.

**The facts, verified directly against the code:**

1. `GraphicsBackendCreateArgs` (`IGraphicsBackend.hpp`) has exactly **7 fields**: `window`,
   `virtualWidth`, `virtualHeight`, `presentationMode`, `contextRecoveryEnabled`,
   `multiSampleCount`, `swapInterval`. There is no `BackBufferFormat`, `DepthStencilFormat`,
   `IsFullScreen`, `PresentInterval`, or `GraphicsProfile`. **D9-30** needs a real
   `D3DPRESENT_PARAMETERS`, which needs those values from the game's actual
   `PresentationParameters` — the backend is never given them. D3D11's precedent is to hardcode
   (`DXGI_FORMAT_R8G8B8A8_UNORM`, `DXGI_FORMAT_D24_UNORM_S8_UINT`) — acceptable for D3D11 (parity is
   not its goal), a direct violation of indistinguishability for D3D9 (this plan's only goal).
2. `IGraphicsBackend` has **no channel for a backend to notify `GraphicsDevice` of anything, ever.**
   The one existing "context recovery" mechanism is one-directional and test-only:
   `GraphicsDevice` can *command* a backend (`SetContextRecoveryEnabled`,
   `DebugSimulateContextLoss`/`DebugRestoreContext`), but the backend has no way to *report* a real,
   driver-triggered event back up. `DeviceLost.Raise(...)` is called **nowhere** in the codebase.
   **D9-34** — the task this plan calls "the single most XNA-authentic subsystem in CNA" — cannot
   be built without adding exactly such a channel.

**The contradiction:** this plan's own Boundaries state "Do not extend `GpuDrawParams` or
`IGraphicsBackend` on your own authority" and cold-start §4 says "Never touch `IGraphicsBackend.hpp`
… or `GpuDrawParams`." Taken literally, D9-30 (in the form D9-34 needs) and D9-34 itself **cannot be
built at all.** Taken as "authenticity is the stated exception," they can — but nothing in this plan
says that exception exists, and a backend author should not decide that for themselves.

**This document's recommendation, not a decision:** `GraphicsBackendCreateArgs`'s own doc comment
already says *"Currently minimal, but allows for easier extension"* — it was designed to grow. Adding
fields to it (back-buffer/depth format, fullscreen, `GraphicsProfile`) is a small, additive,
non-breaking change every existing backend can simply ignore. A device-lost notification channel is
a slightly bigger, but still narrow and additive, one (e.g. a
`std::function<void(BackendDeviceEvent)>` passed in `GraphicsBackendCreateArgs`, called by whichever
backend actually needs it — nine of ten backends would never call it). Neither requires touching
`GpuDrawParams` or the draw-call surface at all; both are narrowly scoped to device
creation/lifecycle, not rendering. That said, **this is exactly the kind of ten-backend-touching
decision this plan's Boundaries reserve for the project owner** — do not let this recommendation be
read as authorization.

~~**Until this is decided:** Phase D9-3 cannot proceed past `D9-31` (`Clear`/`Present`/
`ReadBackbuffer`, which need no new channel). `D9-30`/`D9-32`/`D9-33`/`D9-34` are blocked, and so —
transitively — is everything downstream that depends on a fully-initialized device with correct
formats.~~ **No longer applies — see the RESOLVED note above.**

---

## Design decisions

1. **One new backend value, `CNA_GRAPHICS_BACKEND=D3D9`** → target `cna_backend_graphics_d3d9`,
   define `CNA_BACKEND_D3D9`, Windows-only `FATAL_ERROR` gate (extend the existing `D3D11`/`D3D12`
   check at `CMakeLists.txt:153`). Standard pattern, no novelty.

2. **Plain Direct3D 9 (`Direct3DCreate9`), NOT D3D9Ex. `D3DPOOL_MANAGED` for user resources,
   `D3DPOOL_DEFAULT` only for render targets and dynamic buffers.** Rationale above. The full
   device-lost lifecycle is implemented, and **CNA's existing `DeviceLost`/`DeviceResetting`/
   `DeviceReset` events are fired at XNA's own points in it** (`D9-34`). This decision is reversible
   only by the project owner; do not switch to D3D9Ex mid-implementation because device-lost turned
   out to be painful — that pain is the deliverable.

3. **Vendor Microsoft's Stock Effects sources into CNA; do not port them, do not rewrite them, do not
   "improve" them.** Copy `HLSL/*.fx` + `HLSL/*.fxh` verbatim from the FNA tree into
   `src/CNA/Internal/Backends/D3D9/shaders/xna/`, preserving Microsoft's copyright headers, the
   `LICENSE` (MS-PL), and a `README` naming the provenance. Add an entry to
   `THIRD_PARTY_NOTICES.md`. **Not one line of the `.fx`/`.fxh` files is edited** — if something
   doesn't compile, the fix goes in the compile step (`D9-71`) or in an added `#define`, never in
   Microsoft's source. A modified original is no longer an original, and every future reader must be
   able to `diff` these files against the FNA tree and get zero output.

4. **Never use the D3D9 Effect framework. Get the shader bytecode either by compiling the `.fx` entry
   points directly, or by lifting Microsoft's own bytes out of the `.fxb` — `D9-73` has run, and the
   answer now needs a project-owner decision.** The `.fx` files' `technique`/`pass`/`VertexShader
   VSArray[]` blocks require the D3DX Effect runtime (`d3dx9_43.dll`), a deprecated redistributable
   this project will not ship (design decision 9). That much is settled. What is *not* settled is
   where the bytes come from:
   - **Compiling the `.fx` works, and works beautifully.** Each of the 66 entry points compiles
     individually (`D3DCompile(src, …, "VSBasic", "vs_2_0")` + an `ID3DInclude` handler for the
     `.fxh` files), from Microsoft's **unmodified** source — the real fxc simply ignores the
     Effect-framework tail in `/T vs_2_0` mode, so no preprocessing is needed at all (`D9-1`).
   - **But 5 of the 66 do not reproduce Microsoft's shipped bytecode**, and `.fxb` already contains
     the originals (`D9-73`: 61/66 byte-identical instruction streams; the 5 misses are all
     `PixelLighting` vertex variants, and are a *compiler-version* difference, not a flag one).

   **DECIDED 2026-07-14 by the project owner: CNA compiles the shaders itself.** The `.fx` sources are
   the input; the `.fxb` is kept strictly as a **verification oracle** (`D9-73`), never as a source of
   shipped bytes. Rationale: CNA owns its build, the shader pipeline stays uniform with the project's
   existing `spirv_shaders.hpp`/`hlsl_shaders.hpp` precedent, and a checked-in binary blob lifted from
   a third-party tree is a dependency CNA does not need.

   **The obligation this creates must not be dropped:** the 5 `PixelLighting` variants that do *not*
   reproduce Microsoft's bytes have to be **proven** equivalent against the `D9-A` oracle, shader by
   shader, at an exact-match threshold. Under this plan's stated goal, an unverified "should be the
   same" is not acceptable. The other 61 need no such proof — they *are* Microsoft's instruction
   stream, byte for byte.

   Either way, **the technique's `VSArray`/`PSArray`/`VSIndices`/`PSIndices`/`ShaderIndex` machinery is
   replicated in C++** (`D9-80`) — which a D3D9 backend must do regardless, and which FNA's own `*.cs`
   files already spell out.

5. **Shader targets are Microsoft's own: `vs_2_0`/`ps_2_0` for every stock effect.** Do not "upgrade"
   them to `vs_3_0`/`ps_3_0` because the hardware supports it — that would change the compiler's
   codegen, its precision behavior, and its instruction selection, which is precisely the class of
   difference the indistinguishability bar exists to prevent. Shader Model 3.0 enters this plan in
   exactly two places: the `HiDef` capability floor (`D9-100`) and custom user `ShaderEffect`s
   (`D9-111`).

6. **Constant registers come from Microsoft's `_vs(c#)`/`_ps(c#)` annotations, not from a
   CNA-designed layout.** `D9-72` transcribes them into one authoritative C++ table and
   `static_assert`s it. The previous draft's plan to hand-assign registers is deleted. Where
   `D3DCommon/D3DConstantBuffers.hpp`'s existing POD structs happen to match Microsoft's layout,
   reuse them; where they don't, **Microsoft's layout wins** and D3D9 gets its own structs. Do not
   bend Microsoft's registers to fit CNA's existing structs — that has the causality backwards.

7. **The XNA shader-permutation model is replicated exactly.** `BasicEffect.fx` declares
   `VertexShader VSArray[20]`, `PixelShader PSArray[10]`, `int VSIndices[32]`, `int PSIndices[32]`,
   and selects with `int ShaderIndex`. `BasicEffect.cs` computes that index (verified, lines
   486–511):

   ```
   shaderIndex = 0
   if (!fogEnabled)        shaderIndex += 1
   if (vertexColorEnabled) shaderIndex += 2
   if (textureEnabled)     shaderIndex += 4
   if (lightingEnabled) {
       if (preferPerPixelLighting) shaderIndex += 24
       else if (oneLight)          shaderIndex += 16
       else                        shaderIndex += 8
   }
   ```

   This — and the equivalent in `AlphaTestEffect.cs`/`DualTextureEffect.cs`/
   `EnvironmentMapEffect.cs`/`SkinnedEffect.cs` — is ported verbatim into the D3D9 backend's shader
   dispatch (`D9-80`). It is **not** re-derived from CNA's own `GpuDrawParams` variant-selection
   logic, which is a CNA invention and is not XNA's.

8. **`GpuDrawParams` is the input to the ShaderIndex function, and `IGraphicsBackend` is NOT extended
   to make that easier — but the gaps this exposes are reported, not silently papered over.**
   A real gap already found, before any code is written: **XNA's `ShaderIndex` needs
   `preferPerPixelLighting`, and `GpuDrawParams` does not carry it.** CNA's `BasicEffect` *has* the
   property (`getPreferPerPixelLightingProperty()`/`setPreferPerPixelLightingProperty()` are
   implemented and the field is stored and even copied in the clone constructor — verified in
   `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`), but it is **never plumbed into
   `GpuDrawParams`**, which means *no CNA backend honors it today* and every CNA backend silently
   renders per-vertex lighting when an XNA game asked for per-pixel. That is a real, pre-existing
   fidelity bug that this backend is the first thing to expose — exactly what the authenticity path is
   for. A second, softer one: XNA's `oneLight` flag is a genuine "only DirectionalLight0 is enabled"
   boolean, whereas the D3D9 backend can only *infer* it from `GpuDrawParams`' zeroed
   `light1Diffuse`/`light2Diffuse`, which misfires for a light legitimately set to black.
   **`D9-81` reports these; it does not unilaterally fix them.** Extending `GpuDrawParams` touches
   nine other backends and is a project-owner decision (see Boundaries).

9. **No D3DX, ever.** `libd3dx9_*.a` is present in the MinGW tree, but linking it creates a runtime
   dependency on `d3dx9_43.dll`, a deprecated redistributable absent from a stock Windows install.
   This rules out `ID3DXConstantTable` (use CTAB parsing, `D9-110`) and the Effect framework (design
   decision 4).

10. **The SpriteBatch half-pixel offset is applied in the `MatrixTransform` constant, on the CPU —
    because that is where XNA applied it.** Confirmed by reading `SpriteEffect.fx` in full: its vertex
    shader is a bare `position = mul(position, MatrixTransform);` with **no offset in the shader**, so
    XNA's D3D9 half-texel correction must have been baked into the matrix CPU-side. CNA's own
    `SpriteBatch.cpp` contains no half-pixel handling at all (verified: no `0.5f`/`halfPixel`
    occurrences), which is correct for its D3D10+/GL backends and wrong for D3D9. The offset therefore
    lives in `D3D9SpriteBatchBackend`'s matrix construction, at exactly one site, with a comment naming
    the convention and a pixel test that fails if it is removed. **Never absorb it into a widened test
    tolerance** — that would mask real sampling bugs for the life of the backend.

11. **Render state, not state objects.** D3D9 has no state objects; `ApplyBlendState`/
    `ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState` become `SetRenderState(D3DRS_*)`
    / `SetSamplerState(D3DSAMP_*)` sequences. Do **not** port `D3D11StateObjectCache`/`D3D11SamplerCache`
    — there is nothing to cache. Add a redundancy filter only if profiling shows it matters.

12. **`D3DCommon` is not expanded.** D3D9 shares a vendor with D3D11/D3D12, not a model: `D3DFORMAT`
    is a different enum space from `DXGI_FORMAT`, and D3D9 has no state objects at all. D3D9 gets its
    own `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations`, reusing `D3DCommon`'s *test
    rigor*, not its code. (`D3DConstantBuffers.hpp` is the one reusable artifact — verified to include
    only `<cstddef>` — but see design decision 6: it is reused only where Microsoft's register layout
    happens to agree with it.)

13. **MRT is capped by `D3DCAPS9::NumSimultaneousRTs` (≤ 4), all targets same bit depth, no
    independent blending.** `IGraphicsBackend::SetRenderTargets()`'s default silently degrades to a
    single target — the exact invisible-capability trap `plan_dx.md`'s own boundaries section flagged.
    Over-request throws a named error; it does not degrade.

14. **Native `HWND` via SDL3's Win32 property**, feeding `D3DPRESENT_PARAMETERS::hDeviceWindow`. Same
    as `plan_dx.md` design decision 7. No new windowing code.

15. **COM lifetime: `Microsoft::WRL::ComPtr<T>`**, already proven on this exact MinGW-w64 toolchain by
    `plan_dx.md`'s `DX-6` (including the leak-prone `ReleaseAndGetAddressOf()` pattern). No bare
    `Release()` call sites. Inherited, not re-litigated.

16. **Link set: expect `d3d9` alone, plus `SDL3::SDL3`.** `D9-2` confirms empirically the way `DX-1`
    did. `d3dcompiler` goes only on the offline shader tool and, later, the custom-`ShaderEffect`
    target — never on the main backend, so the stock pipeline keeps zero runtime shader-compiler
    dependency.

17. **The DXVK gate applies here.** `scripts/run-wine-dxvk.sh`'s `DX-85` assertion (fail the run if no
    `DXVK: <version>` line appeared, so a silent WineD3D fallback cannot quietly invalidate every
    pixel test) must cover D3D9 runs, verified with a deliberately-broken-prefix negative test — a gate
    that has never been *seen* to fail is not a gate.

---

## Development environment

Already in place. Verified 2026-07-14:

- **MinGW-w64**: `d3d9.h`, `d3d9caps.h`, `d3d9types.h`, `libd3d9.a` all present. (`libd3dx9_*.a` also
  present — do not link, design decision 9.)
- **Wine + DXVK D3D9**: DXVK's `d3d9.dll.so` ships in `/usr/lib/dxvk/wine64/`, and the project's
  existing `~/.wine-cna-d3d11` prefix **already** symlinks `system32/d3d9.dll` to it with
  `"d3d9"="native"` in the registry — a side effect of `DX-2`'s `dxvk-setup install`. `plan_dx.md`'s
  entire Phase DX1 equivalent is therefore already done for D3D9; no new `programs.md` section needed.
- **`winetricks`** present at `/usr/bin/winetricks` — needed for `d3dcompiler_47` (`D9-1`) and for the
  XNA runtime itself (`D9-A1`).

**What Wine+DXVK cannot authenticate, and why it matters more here than it did for D3D11.** Two of
this plan's headline deliverables are *specifically* the things DXVK cannot model faithfully:
`D3DCAPS9` (DXVK synthesizes them; they are not an XNA-era driver's) and device-lost (DXVK rarely
loses the device the way a real Windows driver does on alt-tab/RDP/lock-screen). Phase D9-10's and
`D9-34`'s results are **provisional** until `D9-140` runs on real hardware, and the docs must say so
rather than shipping DXVK's caps as if they were authentic.

---

## Definition of done, and test conventions — filled in after a readiness audit found neither existed

**No phase before D9-12 had a stated exit condition**, which an independent readiness-audit agent
correctly flagged as a nine-phase blind flight (nothing runnable to check progress against until
Phase D9-12). Fixed with one rule, borrowed from D3D11's own actual history rather than invented:

> **`D3D9_Smoke` starts in Phase D9-3 (as soon as `D9-31`'s `Clear`/`Present`/`ReadBackbuffer` work)
> and grows one check per task from there — exactly how `D3D11_Smoke` grew 3→13→18→29→...→69 across
> `plan_dx.md`'s own Phases DX4 through DX10.** A phase in *this* plan is done when its own tasks'
> checks are green in `D3D9_Smoke`/`D3D9_Common`, not when the code merely compiles. `D9-122`
> (registering the suite formally) is about formalizing what already exists by then, not creating it
> from nothing at the end.

**Test file convention: mirror the existing backends' actual precedent, not `CLAUDE.md`'s generic
Tests section.** `CLAUDE.md` describes Google Test under `tests/`; every existing backend CTest is
instead a standalone `main()` under `examples/` (`examples/d3d11_smoke_test.cpp`, hand-counted
"checks", no gtest). This is a real, if quiet, divergence from `CLAUDE.md` that every prior backend
plan has already made — `D3D9_Smoke` follows it: `examples/d3d9_smoke_test.cpp`, registered in
`CMakeLists.txt` via the same `cna_d3d11_test`/`cna_d3d11_ctest_command`-style macros (mirror them,
do not invent new ones), `LABELS "D3D9"`. `CHECKLIST.md`'s Tests section is about porting `.cs`
files and does not apply to backend-internal smoke tests.

**File locations, settled so nobody has to guess:**

| Artifact | Location |
|---|---|
| `D3D9GraphicsBackend.{hpp,cpp}` and siblings | `include\|src/CNA/Internal/Backends/D3D9/` (repo convention, unambiguous) |
| `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations` | same, top-level in `D3D9/` (design decision 3 — not `D3DCommon/`) |
| `D3D9ShaderRegisters.hpp` (D9-72) | `include/CNA/Internal/Backends/D3D9/shaders/` — it is a small, hand-authored table, not generated output, but it belongs next to the shader pipeline it documents |
| Vendored `.fx`/`.fxh` + `fxc_tool.cpp`/`compile_shaders_sm2.py` + generated `d3d9_shaders.hpp` | `src/CNA/Internal/Backends/D3D9/shaders/` (source) and `.../shaders/xna/` (the vendored Microsoft files specifically, design decision 3) |
| `dx9-spike/xna-oracle/Oracle.cs` (the D9-A2 reference app) | `tools/xna-oracle/` — it is a build/verification tool consumed by developers and CI, not a CNA-namespace test of ported XNA behavior, so `tests/` (reserved for `CHECKLIST.md`-style per-class unit tests) is the wrong fit |
| `NotYetImplemented()` helper (D9-11) | `include/CNA/Internal/Backends/Common/NotYetImplemented.hpp` (new — currently duplicated implicitly by being D3D12-private only) |

---

## Execution order

| # | Phase | Gate |
|---|-------|------|
| 1 | **D9-0 — Feasibility spikes** | **HARD GATE** |
| 2 | **D9-A — The XNA 4.0 oracle** | **Blocks every rendering acceptance criterion** |
| 3 | D9-1 — CMake + skeleton | |
| 4 | D9-2 — Mapping layer | |
| 5 | D9-3 — Device, present, device-lost + XNA events | |
| 6 | D9-4 — Buffers | |
| 7 | D9-5 — Textures, render targets, readback | |
| 8 | D9-6 — Render states | |
| 9 | D9-7 — Microsoft's stock effects: vendor, compile, embed | |
| 10 | D9-8 — XNA shader dispatch + Microsoft's registers | |
| 11 | D9-9 — SpriteBatch | |
| 12 | D9-10 — `GraphicsProfile` made real | |
| 13 | D9-11 — Custom `ShaderEffect` | optional — ask |
| 14 | D9-12 — The indistinguishability suite | the real acceptance test |
| 15 | D9-13 — Docs | |
| 16 | D9-14 — Real Windows verification | `needs_human` |

---

## Phase D9-0 — Feasibility spikes (HARD GATE)

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-1 | **Prove a working SM2.0 shader compiler.** | ✅ | **CLOSED 2026-07-14 — the gate is passed, decisively.** Wine's builtin `d3dcompiler_47.dll` (vkd3d-shader backed) genuinely cannot do this: a `ps_3_0` probe with an ordinary alpha-test ternary died with `E5017: Aborting due to not yet implemented feature: SM1 non-float expression`, and a `vs_3_0` probe that *did* compile emitted a **1.4 MB** blob (fully unrolled — no relative addressing). The **real Microsoft `d3dcompiler_47.dll`** (installed via `winetricks -q d3dcompiler_47` into a dedicated `~/.wine-cna-d3d9-spike` prefix — 4,346,120 bytes, native override, vs. Wine's 1,093,743-byte builtin) fixes it completely: **all 66 entry points across all 6 stock effects compile, 0 failures**, from Microsoft's **unmodified** `.fx` sources. Three findings worth carrying into Phase D9-7: (1) **No source stripping is needed** — the real fxc silently ignores the Effect-framework tail (`VertexShader VSArray[]`, `VSIndices`, `Technique`) when invoked with `/T vs_2_0`, so `D9-71` compiles Microsoft's files *verbatim*, with no preprocessing step at all. (2) An `ID3DInclude` handler resolving `Macros.fxh`/`Common.fxh`/`Lighting.fxh`/`Structures.fxh` relative to the `.fx` file's own directory is the only tooling needed. (3) Output is genuine, compact D3D9 bytecode — version tokens verified (`00 02 fe ff` = vs_2_0, `00 02 ff ff` = ps_2_0), and the critical `VSSkinnedVertexLightingFourBones` (the 72-bone `float4x3` array) is **2,160 bytes**, i.e. relative addressing works — a 670× reduction from Wine's unrolled blob. Largest shader in the entire set: 2,160 bytes. |
| D9-2 | Confirm the minimum link set empirically — expect `d3d9` alone (design decision 16). Spike: `Direct3DCreate9` + `GetDeviceCaps` + `CreateDevice`, `x86_64-w64-mingw32-g++ -std=c++23 spike.cpp -ld3d9`. | ✅ | **CLOSED 2026-07-14.** `x86_64-w64-mingw32-g++ -std=c++23 spike.cpp -o spike.exe -ld3d9 -static-libgcc -static-libstdc++` links cleanly with **`d3d9` alone** — no `dxguid` needed (confirmed: the spike calls `Direct3DCreate9`/`GetDeviceCaps`/`CreateDevice` directly via the returned COM interface, never `QueryInterface` via an `IID_*` global, and `nm` on the resulting binary shows no `IID_` symbols at all). Also ran successfully end-to-end under `~/.wine-cna-d3d11` (DXVK 2.6.0): `CreateDevice` returned a real device. Confirms `plan_dx.md` `DX-1`'s own finding transfers unchanged to D3D9. |
| D9-3 | Prove the Wine+DXVK **D3D9** loop end-to-end: device + swap chain, clear to a known color, present, read back, assert the pixel, confirm DXVK (not WineD3D) handled it. Log the full `D3DCAPS9` (`VertexShaderVersion`, `PixelShaderVersion`, `NumSimultaneousRTs`, `MaxTextureWidth/Height`, `TextureCaps` NPOT bits, `MaxVertexIndex`). | ✅ | **CLOSED 2026-07-14.** Real windowed device (64×64, `D3DFMT_A8R8G8B8`) → `Clear(ARGB(255,10,20,30))` → `Present()` → `GetBackBuffer` → `CreateOffscreenPlainSurface(D3DPOOL_SYSTEMMEM)` → `GetRenderTargetData` → `LockRect` → pixel(0,0) read back as **exactly R=10 G=20 B=30 A=255** — an exact match, through `~/.wine-cna-d3d11`'s DXVK path (`DXVK: 2.6.0` confirmed in the log, real `AMD Radeon 780M (RADV PHOENIX)` device). `D3DCAPS9` dump: `VertexShaderVersion=0xfffe0300` (vs_3_0), `PixelShaderVersion=0xffff0300` (ps_3_0), `NumSimultaneousRTs=4`, `MaxTextureWidth/Height=16384`, `TextureCaps`: **`POW2=0`, `NONPOW2CONDITIONAL=0`** (DXVK reports unconditional full NPOT support — a synthesized-caps data point for `D9-10`/`D9-56`, since real XNA-era hardware commonly had a POW2 or NPOT-conditional restriction; flag this as provisional, not authentic, per the plan's own DXVK-caps caveat), `MaxVertexIndex=16777215` (`0xFFFFFF`, confirms 32-bit index buffers are usable, feeds `D9-41`). |
| D9-4 | Confirm `D3DPOOL_MANAGED` textures are genuinely `LockRect`-readable under DXVK (design decision 2's payoff for `Texture2D::GetData()`), and that `Reset()` genuinely preserves them. | ✅ | **CLOSED 2026-07-14 — confirmed exactly as design decision 2 predicted.** A `D3DPOOL_MANAGED` `IDirect3DTexture9` was `LockRect`-written with a known byte pattern, read back correctly pre-`Reset()`, then the device was `Reset()` with the **same** `D3DPRESENT_PARAMETERS` — **without releasing the MANAGED texture first** — and `Reset()` succeeded. Read the texture back again afterward: **byte-identical to what was written, no re-upload needed.** This confirms `Texture2D::GetData()` (`D9-52`) can be a plain `LockRect`, not the `StretchRect`/`GetRenderTargetData`/`SYSTEMMEM` dance design decision 2 flagged as the fallback if this had failed. |
| D9-5 | **Write a new `scripts/run-wine-dxvk9.sh`** — do not edit the shared `scripts/run-wine-dxvk.sh` (cold-start §4's shared-file rule applies here too, and takes precedence over any earlier wording in this plan that said "extend"). Copy `run-wine-dxvk.sh`'s structure (`DX-3`) and its `DX-85` DXVK-marker gate verbatim, adjusted only for D3D9's own env-var names (do not reuse `CNA_D3D11_*` names — introduce `CNA_D3D9_WINEPREFIX` / `CNA_D3D9_ALLOW_WINED3D` / `CNA_D3D9_SKIP_DXVK_GATE`, mirroring the shape, not the identifiers). Prove the gate fires with a deliberately-broken-prefix negative test. | ✅ | **CLOSED 2026-07-14.** New `scripts/run-wine-dxvk9.sh`, defaulting `CNA_D3D9_WINEPREFIX` to `~/.wine-cna-d3d11` (per the "Development environment" section above — that prefix's own `dxvk-setup install`, run for D3D11, already wires `d3d9.dll` to DXVK too). **Positive case**: `D9-3`'s spike run through the wrapper passes, gate sees `DXVK: 2.6.0` in the log, exits 0. **Negative case**: a freshly-`wineboot --init`'d prefix with no DXVK install run through the same wrapper — the D3D9 device silently falls back to WineD3D (readback pixel came back all-zero, a real, different symptom of the non-DXVK path), no `DXVK: <version>` line appears, and the gate correctly fires, exiting 3 with the documented error message. The gate is real, not a theoretical placeholder. |

---

## Phase D9-A — The XNA 4.0 oracle (blocks every rendering acceptance criterion)

**This phase is what makes "indistinguishable" a testable claim rather than an aspiration.** It stands
up the original XNA 4.0 runtime on this machine and builds a differential harness against it. Nothing
in Phases D9-7 through D9-12 can honestly be marked ✅ without it.

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-A1 | Stand up real XNA 4.0 under Wine. | ✅ | **CLOSED 2026-07-14 — real XNA 4.0 runs and renders on this machine.** A dedicated 32-bit prefix (`WINEARCH=win32 WINEPREFIX=~/.wine-cna-xna40`), `winetricks -q dotnet40` (rc=0) then `winetricks -q xna40` (rc=0). All ten `Microsoft.Xna.Framework.*.dll` assemblies land in the GAC (`Framework`, `Game`, `Graphics`, `Xact`, `Video`, `Storage`, `Net`, `GamerServices`, `Input.Touch`, `Avatar`), and `csc.exe` is present at `Microsoft.NET/Framework/v4.0.30319/`, so the reference app can be built **inside the prefix** — no Visual Studio 2010, no XNA Game Studio, no Windows machine needed. |
| D9-A2 | Build a minimal XNA 4.0 reference app **without the content pipeline**. | ✅ | **CLOSED 2026-07-14 — proven end-to-end.** `Oracle.cs` (≈50 lines: `Game` + `GraphicsDeviceManager` at `GraphicsProfile.HiDef` → `RenderTarget2D` → `Clear(CornflowerBlue)` → a `BasicEffect` `VertexPositionColor` triangle via `DrawUserPrimitives` → `RenderTarget2D.SaveAsPng` → `Exit()`) compiles with the in-prefix `csc.exe` against the GAC assemblies and **runs**: `XNA-ORACLE-OK profile=HiDef adapter=ATI Radeon HD 5600 Series`, producing a real 256×256 PNG. Pixels verified: corner = `(100,149,237)` — **exactly `Color.CornflowerBlue`** — and the triangle's interior shows genuine Gouraud interpolation (`(69,118,69)` at centre, `(1,253,1)` near the apex). Avoiding the content pipeline is what makes this tractable — it is the only part of XNA that genuinely needs the old Visual Studio tooling, and nothing this plan compares (stock effects, `SpriteBatch`, render targets, formats) requires it. **Note the reported adapter is Wine's spoofed string, not the real GPU — see `D9-A4`.** |
| D9-A3 | Build the **byte-for-byte equivalent CNA app** (same scene, same numbers, same clear color, same camera, same vertex data) running on `CNA_GRAPHICS_BACKEND=D3D9`, dumping a PNG the same way. | ⬜ | The scenes must be authored once, in a shared, declarative form, and rendered by both — not hand-transcribed twice, or the harness will drift and start comparing two different scenes. |
| D9-A4 | `scripts/xna-diff.py` — run both, diff the PNGs, report max per-channel delta, count of differing pixels, and a visual diff image. Define the pass threshold **explicitly and defend it**: exact match where achievable; where not, a documented, justified tolerance with the *reason* named, never a tolerance widened until the test passes. | ⬜ | The threshold discipline is the whole ballgame. A tolerance chosen to make a red test green is how an authenticity project quietly becomes a parity project. **Critical methodological requirement, found by `D9-A2`'s own first run: both sides must execute on the SAME Direct3D 9 implementation.** The XNA prefix as built has no DXVK, so real XNA ran on **WineD3D** (it reported the adapter as `ATI Radeon HD 5600 Series` — WineD3D's spoofed string, not this machine's actual AMD Radeon 780M), while CNA/D3D9 would run on **DXVK**. Diffing those two would measure `CNA+DXVK` against `XNA+WineD3D` and silently attribute *driver* differences to CNA. **Fix: install DXVK into the XNA prefix too** (`dxvk-setup install`, the same command `DX-2` used), so both sides go through DXVK's D3D9 → Vulkan path and the only remaining variable is CNA itself. Optionally also run the whole corpus a second time with both sides on WineD3D — agreement across both drivers is a much stronger result than agreement on one. |
| D9-A5 | A scene corpus, growing with the plan: flat clear; a `colored3d` triangle; textured quad; `BasicEffect` with 1 and 3 lights, fog on/off, vertex colors on/off, per-vertex **and per-pixel** lighting; `AlphaTestEffect` all compare functions; `DualTextureEffect`; `EnvironmentMapEffect` (fresnel on/off); `SkinnedEffect` (1/2/4 bones per vertex); `SpriteBatch` (rotation, scale, source rect, origin, all `SpriteEffects`, all `SpriteSortMode`s); render targets; every `SurfaceFormat` that both support. | ⬜ | Each subsequent phase adds its own scenes here. This corpus *is* the definition of "indistinguishable" for this project — it is the deliverable that outlives the backend. |
| D9-A6 | **Run the corpus against CNA's OTHER backends too**, and record the deltas. | ⬜ | Free, and extremely valuable: it measures, for the first time, how far EasyGL/Vulkan/D3D11 actually are from real XNA. The `preferPerPixelLighting` gap (design decision 8) predicts at least one guaranteed hit. Expect this task to open real `plan_graphics.md` bugs — that is a success, not scope creep, but **log them and move on**; do not fix them inside this plan. |

---

## Phase D9-1 — CMake integration and skeleton

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-10 | **All seven `CMakeLists.txt` sites that mention `"D3D12"` need a `"D3D9"` sibling added next to it** (found by `grep -n '"D3D12"' CMakeLists.txt`; verified 2026-07-14, do not trust a shorter list from an earlier draft of this plan): (1) line 95, the cache `STRINGS` property; (2) line 139, `list(APPEND _cna_enabled_backends "D3D12")` → add the `D3D9` equivalent; (3) line 153, the Windows-only `FATAL_ERROR` gate's `OR` chain; (4) line 248, the backend-dir/target `elseif()` block (`BACKEND_DIR`, `BACKEND_TARGET=cna_backend_graphics_d3d9`, `CNA_BACKEND_D3D9`); (5) line 288, a second Windows-only-related `OR` chain; (6) line 325, the link-libraries `elseif()` (expect just `d3d9` + `SDL3::SDL3`, design decision 16); (7) line 392, a third `OR` chain. Do this **as one commit, early, purely additively** (cold-start §4) — it is the one CMake change every later task depends on, and `CMakeLists.txt` gets ~200 commits/week from parallel work on this project, so one clean pass beats several. | ✅ | **CLOSED 2026-07-14 — with one real correction to this row's own site (5).** Re-`grep`ped fresh (line numbers had NOT drifted, still exactly 95/139/153/248/288/325/392) but **site (5), line 288, is NOT "a second Windows-only-related OR chain" as this row claimed** — it is the `D3DCommon`-shared-core conditional (`if(... D3D11 OR ... D3D12)` gating `add_library(cna_backend_graphics_d3dcommon ...)`). Adding `D3D9` there would have wired D3D9 into building/linking `D3DCommon` (with `d3d11`/`dxgi`!), directly contradicting design decision 12 ("`D3DCommon` is not expanded"). **Left untouched on purpose**, with a new comment at that site explaining why, so a future reader doesn't "helpfully" add it back. The other 6 sites got their `D3D9` sibling exactly as described: cache `STRINGS`/`option(CNA_BACKEND_D3D9 ...)`/`_cna_enabled_backends` list/the `FATAL_ERROR` gate/the backend-dir-target `elseif()`/the link-libraries `elseif()` (`d3d9` + `SDL3::SDL3` only, no `d3dcompiler`, no `cna_backend_graphics_d3dcommon`). Site (7), line 392 (the `CNA` circular-link `OR` chain, for `SpriteBatch::Begin(effect)`'s callback into `Effect::Apply()`) was **deliberately left out of the `D3D9` `OR` chain too** — nothing in D3D9 calls back into a CNA-defined symbol yet (that's `D9-112`, Phase D9-11, ask-first) — with its own explanatory comment. Verified: `cmake -DCNA_GRAPHICS_BACKEND=D3D9 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake` configures clean. |
| D9-11 | `D3D9GraphicsBackend` skeleton implementing `IGraphicsBackend`. **Override every method that has a *silently empty* default body** (`{}` — there are 10 of these, e.g. `SetSwapInterval`, `SetBlendFactor`, `SetScissorRect`; grep `IGraphicsBackend.hpp` for `{}` on a `virtual` line to enumerate them) **and throw `NotYetImplemented()` from each**, so a missing capability is a loud, explicit failure rather than an invisible no-op. Methods that are already pure-virtual (22 of them) must be implemented for real or the class won't compile — no throwing stub needed, the compiler enforces it. Methods with an already-*throwing* default (~25 of them) may simply not be overridden yet; inheriting "throws NotYetImplemented" is fine, it is inheriting *silence* that is the trap. This is **not** "override all 57" — D3D11's own skeleton (`D3D11GraphicsBackend.hpp`, 46 overrides of 57) is the actual precedent to match, not a stricter reading of this row. `NotYetImplemented()` currently exists only as a `D3D12GraphicsBackend`-private static helper (`D3D12GraphicsBackend.hpp`/`.cpp`) — **lift it into a small shared header** (e.g. `include/CNA/Internal/Backends/Common/NotYetImplemented.hpp`) as part of this task, since D3D9 needs the identical helper and duplicating it is worse than sharing four lines of code. It throws `std::runtime_error` (confirm against D3D12's actual implementation before assuming). | ✅ | **CLOSED 2026-07-14.** Confirmed by direct `grep`: exactly 22 pure-virtuals, exactly 10 silently-empty-default (`SetSwapInterval`/`SetRenderTarget2D`/`SetBlendFactor`/`SetReferenceStencil`/`SetScissorRect`/`SetViewport`/`SetContextRecoveryEnabled`/`SetStringMarkerEXT`/`DebugSimulateContextLoss`/`DebugRestoreContext`), matching this row's counts exactly. New shared `include/CNA/Internal/Backends/Common/NotYetImplemented.hpp` (a free function, not a per-class static, since two backends now need it — `D3D12GraphicsBackend`'s own private copy is left untouched, out of scope). New `D3D9GraphicsBackend.{hpp,cpp}`: the 22 pure virtuals are either real (trivial bookkeeping — `GetWindowInternal`/`GetRendererInternal`/`GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`, no device needed) or throw `NotYetImplemented()` naming the follow-up task (`Clear`/`Present`/all 5 `Clear*` combos/`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`CreateTexture`/`CreateSpriteBatch`); all 10 silently-empty ones now throw too. The other ~25 (already-throwing defaults) are left un-overridden, per this row's own instruction. Verified real, not just compiling: a standalone runtime check (constructs the backend with `window=nullptr`, confirms `GetViewportSize()`/`GetWindowInternal()`/`GetRendererInternal()`/`SetVirtualResolution()` work for real, then confirms 6 representative methods — `Clear`/`Present`/`SetScissorRect`/`SetSwapInterval`/`SetBlendFactor`/`DebugSimulateContextLoss` — each throw `std::runtime_error` with the expected message) ran clean under `~/.wine-cna-d3d11`. `CNA` and `cna_backend_graphics_d3d9` both build clean (zero warnings from the new files themselves; `IGraphicsBackend.hpp`'s own pre-existing `-Wunused-parameter` warnings on unrelated base-class default bodies are unaffected by this change and appear regardless of which backend includes it). **CORRECTION found during `D9-30`/`D9-6`, 2026-07-14: this row's own "10" count and its suggested `grep 'virtual.*{}$'` method both missed 4 more silently-empty virtuals** — `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState` also default to `{}`, but their signatures span multiple lines, so the single-line grep pattern doesn't find them. Real consequence: these 4 were left silently inheriting the empty default after this task's own original pass, undetected until `GraphicsDevice`'s constructor (which unconditionally pushes `BlendState`/`DepthStencilState`/`RasterizerState` defaults, Task 896/955) crashed against a *different* stub (`SetViewport`, single-line, correctly caught) and, once that was fixed, immediately hit these multi-line ones next. All 4 are now handled: the first 3 are genuinely real (forced in early by this same discovery, see `D9-60`/`D9-61`/`D9-62`'s own rows); `ApplySamplerState` now throws `NotYetImplemented()` like the original 10, closing the gap this row's method missed. |
| D9-12 | Audit `GraphicsDevice.cpp`'s `#ifdef CNA_BACKEND_*` sites; add `D3D9` where genuinely needed. | ✅ | **CLOSED 2026-07-14 — zero changes needed, confirmed per site.** `grep -n "CNA_BACKEND_" GraphicsDevice.cpp` finds 5 sites: (1) `#ifdef CNA_BACKEND_BGFX` (Bgfx-only header include) — N/A to D3D9; (2)/(3)/(4) `getBackendWindowFlags()`'s `CNA_BACKEND_EASYGL`/`VULKAN`/`BGFX` branches, adding `SDL_WINDOW_OPENGL`/`SDL_WINDOW_VULKAN` — **D3D11/D3D12 already have zero branches here today**, confirming a native-Win32-window D3D backend needs neither flag; D3D9 needs none either, same reasoning (design decision 14: native `HWND` via SDL3's Win32 property, no new windowing code); (5) the constructor's `#if !defined(CNA_BACKEND_HEADLESS) && !defined(CNA_BACKEND_SOFTWARE)` SDL-video-init guard and `createOrAttachWindow()`'s matching `#if defined(HEADLESS) \|\| defined(SOFTWARE)` — both are already negative conditionals that correctly include D3D9 in the "real window" path purely by NOT being Headless/Software, with no D3D9-specific edit required. |

---

## Phase D9-2 — Mapping layer

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-20 | `D3D9FormatMapping` — `SurfaceFormat`/`DepthFormat` → `D3DFORMAT`. | ✅ | **CLOSED 2026-07-14.** `SurfaceFormatToD3D9`/`DepthFormatToD3D9`, `include/src/CNA/Internal/Backends/D3D9/D3D9FormatMapping.{hpp,cpp}`. Verified against Microsoft's own published D3D9→D3D10/DXGI legacy-format table (not derived by loose analogy) — the two genuinely non-obvious findings: **`SurfaceFormat::Color` → `D3DFMT_A8B8G8R8`, not `D3DFMT_A8R8G8B8`** (D3D9's channel-order naming reads MSB→LSB, the reverse of DXGI's LSB→MSB convention — the *same* R,G,B,A memory layout D3D11 maps to `DXGI_FORMAT_R8G8B8A8_UNORM` is `A8B8G8R8` in D3D9's naming, and `ColorBgraEXT` gets the `A8R8G8B8` name instead); and **`Rgba1010102` → `D3DFMT_A2B10G10R10`, not the superficially-similar `D3DFMT_A2R10G10B10`** (Microsoft's own table lists `A2R10G10B10` as having no DXGI equivalent at all — its alpha bits sit in a different position than `DXGI_FORMAT_R10G10B10A2_UNORM`'s; `A2B10G10R10` is the one whose bits actually match, derived from first principles, not name resemblance). `Bc7EXT`/`Bc7SrgbEXT` correctly return `D3DFMT_UNKNOWN` (BC7 postdates D3D9 entirely). `ColorSrgbEXT`/`Dxt5SrgbEXT` intentionally return the *same* `D3DFORMAT` as their non-sRGB counterpart, documented as a real, D3D9-specific architectural difference — D3D9 has no distinct sRGB format constants, sRGB decode is a per-sampler render state (`D3DSAMP_SRGBTEXTURE`), owed to `D9-63`. `DepthFormat::Depth24` correctly gets its own real D3D9 depth-only format (`D3DFMT_D24X8`), unlike D3D11 (no such format, folds into the combined D24S8). |
| D9-21 | `D3D9StateMapping` — `Blend`/`BlendFunction`/`CompareFunction`/`CullMode`/`FillMode`/`TextureAddressMode`/`TextureFilter`/`StencilOperation` → `D3DBLEND`/`D3DBLENDOP`/`D3DCMPFUNC`/`D3DCULL`/`D3DFILLMODE`/`D3DTEXTUREADDRESS`/`D3DTEXTUREFILTERTYPE`/`D3DSTENCILOP`. | 🟨 | **Table CLOSED 2026-07-14; the `D3DCULL` pixel-proof this row itself demands is NOT done yet — honestly deferred, not skipped.** `include/src/CNA/Internal/Backends/D3D9/D3D9StateMapping.{hpp,cpp}`. All 7 enums map directly (verified against `d3d9types.h` on this machine, not assumed) — `CullClockwiseFace`→`D3DCULL_CW`/`CullCounterClockwiseFace`→`D3DCULL_CCW` on the documented assumption that D3D9, like D3D11, defines clockwise-as-front by default (no per-device winding-flip render state exists in either API to second-guess). **`TextureFilter` needed its own struct, `D3D9FilterTriple` — not a single enum.** D3D9 has no composed `D3D11_FILTER`-style value; min/mag/mip are three independent `D3DSAMP_MINFILTER`/`MAGFILTER`/`MIPFILTER` render states, so `TextureFilterToD3D9()` returns a `{min,mag,mip}` triple decomposed mechanically from XNA's own min/mag/mip-modeled enumerator names (same rationale `D3DCommon::TextureFilterToD3D11`'s own doc comment already gives) — `TextureFilter::Anisotropic` decomposes to `{ANISOTROPIC,ANISOTROPIC,LINEAR}` since `D3DTEXF_ANISOTROPIC` is not a legal `D3DSAMP_MIPFILTER` value. **This row's own warning — "pixel-test `CullClockwiseFace`/`CullCounterClockwiseFace` against the XNA oracle before trusting this table" — has NOT been done**: Phase D9-A's oracle-comparable draw path doesn't exist until `D9-82`/`D9-84`. The mapping is structurally sound (same reasoning D3D11's own, already-oracle-independent, verified convention uses) but not yet pixel-proven; treat as provisional until `D9-84` covers it, same honesty discipline the DXVK-caps caveats elsewhere in this plan already use. |
| D9-22 | `D3D9VertexDeclarations` — stride-keyed `D3DVERTEXELEMENT9` arrays. | ✅ | **CLOSED 2026-07-14.** `VertexElementsForStrideD3D9()`, `include/src/CNA/Internal/Backends/D3D9/D3D9VertexDeclarations.{hpp,cpp}` — same 5 stride-keyed layouts (16/20/24/32/52) and byte offsets `D3DCommon::InputElementsForStride()` already establishes, transcribed into `D3DVERTEXELEMENT9`/`D3DDECLTYPE`/`D3DDECLUSAGE` vocabulary, each array pre-terminated with the `D3DDECL_END()` sentinel (verified by an explicit test check, not just asserted) so it can be passed directly to `CreateVertexDeclaration()` with no separate count parameter. `COLOR` uses `D3DDECLTYPE_D3DCOLOR` (XNA's own historical D3D9 convention: a packed value the vertex shader auto-unswizzles to normalized RGBA); `BLENDINDICES` uses `D3DDECLTYPE_UBYTE4` (raw integer, not normalized, matching D3D11's `R8G8B8A8_UINT` choice). The stride-32 `sprite2d`/`VertexPositionNormalTexture` collision `DX-70` found is noted in this file's own doc comment as a `D9-90` concern, not re-litigated here. |
| D9-23 | `D3D9_Common` CTest for all three tables, mutation-verified. | ✅ | **CLOSED 2026-07-14.** New `examples/d3d9_common_test.cpp` (28 checks) registered in `CMakeLists.txt` mirroring `D3D11_Common`'s own macros (`cna_d3d9_test`/`cna_d3d9_ctest_command`), run through the new `scripts/run-wine-dxvk9.sh` with `CNA_D3D9_SKIP_DXVK_GATE=1` (this binary opens no device, by design). **28/28 PASS**, confirmed both via direct execution and `ctest --test-dir cmake-build-d3d9 -R D3D9`. Mutation-verified: flipped `CullMode::CullClockwiseFace`'s mapping to `D3DCULL_CCW`, rebuilt, confirmed exactly the 1 expected check failed (27/28) and no others, reverted, reconfirmed 28/28 — same discipline `DX-11-fmt`/`DX-12-state`/`DX-84` used. |

---

## Phase D9-3 — Device, present, and XNA's device-lost lifecycle

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-30 | `Direct3DCreate9`, `GetDeviceCaps`, `CreateDevice` with `D3DPRESENT_PARAMETERS`. | ✅ | **CLOSED 2026-07-14.** Implemented the approved additive `GraphicsBackendCreateArgs` extension for real: `backBufferFormat`/`depthStencilFormat`/`isFullScreen`/`graphicsProfile` (raw ordinals) + a `BackendDeviceEvent`-typed `deviceEventCallback` (`D9-34`'s own channel, wired now since it's one field), all populated by `GraphicsDevice::createBackend()` from the game's real `PresentationParameters`/`GraphicsProfile`. `D3D9GraphicsBackend::CreateDeviceResources()`: real `Direct3DCreate9`, a preliminary `GetDeviceCaps` to pick hardware-vs-software vertex processing (`D3DDEVCAPS_HWTRANSFORMANDLIGHT`), then `CreateDevice` with a real `D3DPRESENT_PARAMETERS` built from those fields. **Real, hardware-confirmed finding along the way**: D3D9's swap-chain back buffer is restricted to a small set of DISPLAY-compatible `D3DFORMAT`s — DXVK genuinely rejected `D3DFMT_A8B8G8R8` (`SurfaceFormat::Color`'s own `D9-20` mapping, correct for a *texture*) with `D3D9DeviceEx::ResetSwapChain: Unsupported backbuffer format`. Fixed with a back-buffer-specific `BackBufferDisplayFormatForD3D9()` substitution (`A8B8G8R8` → its display-compatible `A8R8G8B8` sibling; `ReadBackbuffer()`, D9-31, already handles both byte orders) — a real D3D9 API restriction, not a DXVK quirk, documented in place. **A second real gap found empirically, beyond the originally-scoped extension**: `GraphicsDevice::Reset()` never forwarded updated back-buffer/depth-stencil/fullscreen settings to an *already-constructed* backend (only `SetVirtualResolution`/`ApplyMultiSampleCount` were re-pushed) — this matters because `Game` commonly constructs its `GraphicsDevice` (and backend) with *default* `PresentationParameters` before `GraphicsDeviceManager.ApplyChanges()` ever runs its *real* preferred settings. Fixed with one more small additive `IGraphicsBackend` method, `UpdatePresentationFormatEXT(backBufferFormat, depthStencilFormat, isFullScreen)` (empty default, every other backend ignores it unchanged), called from `GraphicsDevice::Reset()` alongside the pre-existing two. Verified: `CNA`/EasyGL still build clean and 34 EasyGL gtest+CTest checks (`GraphicsDeviceDefaultStateTest`/`GraphicsDeviceInformationTest`/`IGraphicsDeviceManagerTest`/`EasyGL_DeviceResetEvents`/`EasyGL_BackbufferResize`/`EasyGL_RealWindowResize`/`EasyGL_RenderTarget_ViewportScissorReset`/`EasyGL_ViewportResetAfterResize`) still pass with zero regressions. |
| D9-31 | `Clear()` + **all six combo `Clear*` variants** (`ClearColorAndDepth`, `ClearDepth`, `ClearStencil`, `ClearDepthAndStencil`, `ClearColorAndStencil`, `ClearColorDepthAndStencil`) + `Present()` + `ReadBackbuffer()`. | ✅ | **CLOSED 2026-07-14 — all 6 variants pixel-verified in `D3D9_Smoke`, this row's own bar met.** `Clear`/all 6 combos map to one `IDirect3DDevice9::Clear()` call each with the right `D3DCLEAR_*` flag combination (simpler than D3D11's two-call split, since D3D9's `Clear()` natively combines color+depth+stencil); `HasDepthBuffer`/`HasStencilBuffer` gate the depth/stencil flags so a request against a depth-less device silently no-ops (matches `SupportsDepthStencil()`'s masking contract) rather than passing an invalid flag to the driver. `ReadBackbuffer()`: `GetBackBuffer`→`CreateOffscreenPlainSurface(D3DPOOL_SYSTEMMEM)`→`GetRenderTargetData`→`LockRect`, handling both `A8B8G8R8` (direct copy, already R,G,B,A) and `A8R8G8B8` (R/B swap) back-buffer byte orders; other formats throw a named not-yet-implemented (no general D3DFORMAT→RGBA8 converter exists yet, not needed by anything built so far). **Proof, since no real draw path exists until `D9-82`:** the 4 variants with a color component get an exact-color pixel readback; the 3 depth/stencil-only variants (`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`) are proven by confirming the color buffer from a *prior* `Clear()` is genuinely UNCHANGED (a real bug — e.g. accidentally including `D3DCLEAR_TARGET` — would fail this) plus a direct `GetDepthStencilSurface()`/`GetDesc()` check that a real, correctly-sized/formatted depth-stencil surface backs the device; a second, raw (non-`Game`) backend built with `DepthFormat::None` separately proves `GetDepthStencilSurface()` genuinely fails and `ClearDepth`/`ClearStencil` silently no-op there. `D3D9_Smoke`: 9/9 main checks + 3/3 no-depth-buffer checks = 12/12. |
| D9-32 | Enforce the profile floor at construction: reject a device below the requested `GraphicsProfile`'s caps with a specific diagnostic, not a deferred shader-creation failure. | 🟨 | **CLOSED for the shader-model floor, 2026-07-14 — the FULL Reach/HiDef capability table stays `D9-100`'s job, not invented here.** `CreateDeviceResources()` checks `GraphicsProfile::HiDef` against the real `D3DCAPS9.VertexShaderVersion`/`PixelShaderVersion` (must meet `vs_3_0`/`ps_3_0`, XNA's own documented HiDef floor) and throws the real XNA `NoSuitableGraphicsDeviceException` (not a generic `CreateDevice` failure) if not met — a specific, named diagnostic, exactly this row's own bar. `Reach` has no floor to check on this machine (every real D3D9 device already exceeds `vs_2_0`/`ps_2_0`). **Honest asymmetry, not a hidden gap**: only the *positive* path is proven (`D3D9_Smoke` Check K — `HiDef` construction succeeds since this real GPU's caps exceed the floor) — the *rejection* path cannot be exercised on real, already-SM3-capable hardware under this dev loop; same "needs real constrained hardware" caveat this plan already applies elsewhere (`D9-105`/`D9-140`). Texture size limits, NPOT restrictions, instancing, MRT count, render-target/back-buffer format whitelists, and MSAA levels remain `D9-100`'s full table, per this row's own "Feeds Phase D9-10" note. |
| D9-33 | Window resize. | ✅ | **CLOSED 2026-07-14 (mechanism + dedicated test).** D3D9 has no `ResizeBuffers` — `EnsureDeviceSize()` (called lazily from `Present()`, exactly mirroring `D3D11GraphicsBackend::EnsureSwapChainSize()`'s own timing) detects an SDL-window pixel-size change (or a `presentationDirty_` flag set by `UpdatePresentationFormatEXT()`, see `D9-30`) and does a real `IDirect3DDevice9::Reset()` with updated `D3DPRESENT_PARAMETERS`, then restores the viewport (`Reset()` clears device render-state defaults). New `D3D9_Smoke` Check L (mirroring D3D11's own `DX-83`): a real `GraphicsDeviceManager` resize 64×64→96×80, polled across up to 30 frames (Wine/Xvfb's own async window-manager resize delivery, same discipline this project's other backends' resize tests already use) — confirms the viewport genuinely reflects 96×80, a post-resize `Clear()`+readback matches at BOTH the origin and near the new far edge (proving the back buffer/depth-stencil/viewport are truly the new size, not stale/clamped), and `PresentationParameters` reflects 96×80 too. All 3 checks passed on the first real run. Device-lost recovery ("design it once, use it twice") is `D9-34`'s own remaining half, not re-litigated here. |
| D9-34 | **XNA's device-lost lifecycle, for real.** `TestCooperativeLevel()` → `D3DERR_DEVICELOST` (fire `DeviceLost`) → wait for `D3DERR_DEVICENOTRESET` → release all `D3DPOOL_DEFAULT` resources (fire `DeviceResetting`) → `Reset()` → recreate them (fire `DeviceReset`). `D3DPOOL_MANAGED` user resources survive untouched — which is *why* XNA 4.0 could promise resources survive a reset. | ⬜ | **BLOCKED — not merely high-risk, currently unbuildable. See "The `IGraphicsBackend` boundary problem" above and do not start until it is answered.** Verified 2026-07-14: `IGraphicsBackend` (57 virtuals) has no device-lost query, no notify-callback, no event hook of any kind. The one existing "context recovery" mechanism (`SetContextRecoveryEnabled`, `DebugSimulateContextLoss`/`DebugRestoreContext`) is **one-directional, `GraphicsDevice`-calls-into-`backend` only** — it lets a test *command* a backend to simulate loss, it gives the backend no way to *report* a real, driver-triggered loss back up. `DeviceLost.Raise(...)` does not appear anywhere in the codebase today; only `DeviceResetting`/`DeviceReset` fire, and only from the app-initiated `GraphicsDevice::Reset()` path (`GraphicsDevice.cpp`), never from an async device-loss detection. Building this task as specified requires a new backend→device notification channel, which means touching `IGraphicsBackend` and/or `GraphicsDevice.cpp`'s backend-facing surface — exactly what this plan's Boundaries forbid a backend author from doing unilaterally. This is the plan's own core contradiction: it asks for the one thing its own rules forbid building. **Do not resolve this by silently picking an interpretation — it is now surfaced explicitly for the project owner (see the boundary-problem section).** Once resolved: verify the event *order and payload* against real XNA (`D9-A`), not just that they fire. Under DXVK the device is unlikely to be lost naturally — if the path cannot be forced deterministically in this dev loop, mark 🟨 and hand the real verification to `D9-140`. |

---

## Phase D9-4 — Buffers

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-40 | `D3D9VertexBufferBackend` (`IDirect3DVertexBuffer9`, `Lock`/`Unlock`; `SetDataOptions` → `D3DLOCK_DISCARD`/`D3DLOCK_NOOVERWRITE`). | ⬜ | XNA's `SetDataOptions` maps 1:1 onto D3D9's lock flags — a good sanity check that the port is on the right track. If a mapping feels forced here, something upstream is wrong. |
| D9-41 | `D3D9IndexBufferBackend` — 16-bit and 32-bit (`D3DFMT_INDEX16`/`INDEX32`), `CreateIndexBuffer32()` explicitly overridden. | ⬜ | D3D11 silently inherited a 16-bit-only default (`DX-31` caught it). Don't repeat it. `D3DCAPS9::MaxVertexIndex` gates 32-bit — query it, and feed the result into Phase D9-10 (it is a `Reach`/`HiDef` discriminator). |
| D9-42 | Byte-exact round-trip tests for both. | ⬜ | |

---

## Phase D9-5 — Textures, render targets, readback

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-50 | `D3D9TextureBackend` (`IDirect3DTexture9`, `D3DPOOL_MANAGED`), mip levels, sub-rect `SetData`. | ⬜ | |
| D9-51 | `D3D9TextureCubeBackend` (`IDirect3DCubeTexture9`), `D3D9Texture3DBackend` (`IDirect3DVolumeTexture9`). | ⬜ | Volume textures are a `D3DCAPS9` capability — query, don't assume. |
| D9-52 | `GetData()` for 2D/cube/3D via `LockRect` on the MANAGED copy (design decision 2's payoff, contingent on `D9-4`). | ⬜ | If `D9-4` found MANAGED unlockable under DXVK, this reverts to the `StretchRect`→`GetRenderTargetData`→`SYSTEMMEM` dance and grows substantially. Unsupported formats throw a named error and are listed in the docs. |
| D9-53 | `D3D9RenderTargetBackend` / `D3D9RenderTargetCubeBackend` (`D3DUSAGE_RENDERTARGET`, `D3DPOOL_DEFAULT`, depth-stencil surface, MSAA via `CheckDeviceMultiSampleType`). | ⬜ | These are `D3DPOOL_DEFAULT` → they are exactly what `D9-34`'s device-lost path must release and recreate. Wire them into it as they land, not afterwards. |
| D9-54 | MRT via `SetRenderTarget(i, surface)`, capped at `NumSimultaneousRTs`, same-bit-depth check; over-request throws (design decision 13). | ⬜ | |
| D9-55 | `D3D9OcclusionQueryBackend` (`D3DQUERYTYPE_OCCLUSION`). | ⬜ | |
| D9-56 | NPOT handling driven by `D3DPTEXTURECAPS_POW2` / `NONPOW2CONDITIONAL`. | ⬜ | **Authenticity, not a limitation to hide.** XNA's `Reach` profile forbids wrapping on NPOT textures *because of this cap*. Surface it; feed it into Phase D9-10. |

---

## Phase D9-6 — Render states

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-60 | `ApplyBlendState` / `SetBlendFactor` (`D3DRS_SEPARATEALPHABLENDENABLE`, `D3DRS_BLENDFACTOR`, `D3DRS_COLORWRITEENABLE`). | 🟨 | **CLOSED for everything `IGraphicsBackend::ApplyBlendState()`'s own signature carries, 2026-07-14 — forced in early, see `D9-30`'s row.** `D3DRS_ALPHABLENDENABLE`/`SEPARATEALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND`/`BLENDOP`/`SRCBLENDALPHA`/`DESTBLENDALPHA`/`BLENDOPALPHA` all real, via the `D9-21` `BlendToD3D9`/`BlendFunctionToD3D9` tables. `SetBlendFactor` → `D3DRS_BLENDFACTOR`. **`D3DRS_COLORWRITEENABLE` genuinely NOT touched** — not a corner cut specific to D3D9: `IGraphicsBackend::ApplyBlendState()`'s own parameter list has no `ColorWriteChannels`-equivalent field at all, and D3D11 doesn't implement it either (verified: zero `COLORWRITEENABLE`/`ColorWriteChannels` hits in `D3D11GraphicsBackend.cpp`) — a pre-existing, cross-backend interface gap, not a new D3D9 one. Behavioral pixel proof needs a real draw (`D9-82`); this closes the render-state-push half only, same honest bar Check O set for D3D11's own equivalent row. |
| D9-61 | `ApplyDepthStencilState` / `SetReferenceStencil` (two-sided stencil via `D3DRS_TWOSIDEDSTENCILMODE`). | ✅ | **CLOSED 2026-07-14 — forced in early, see `D9-30`'s row.** Confirms this row's own prediction: XNA's `DepthStencilState` mapped onto D3D9 render states with no forced/awkward translation (`D3DRS_ZENABLE`/`ZWRITEENABLE`/`ZFUNC`/`STENCILENABLE`/`STENCILFUNC`/`STENCILPASS`/`STENCILFAIL`/`STENCILZFAIL`/`STENCILMASK`/`STENCILWRITEMASK`/`STENCILREF`/`TWOSIDEDSTENCILMODE`/`CCW_STENCILFUNC`/`CCW_STENCILPASS`/`CCW_STENCILFAIL`/`CCW_STENCILZFAIL`, via the `D9-21` `CompareFunctionToD3D9`/`StencilOperationToD3D9` tables). `SetReferenceStencil` → `D3DRS_STENCILREF` standalone (Task 870/319 parity, same as D3D11). Behavioral pixel proof needs a real draw (`D9-82`), same bar as `D9-60`. |
| D9-62 | `ApplyRasterizerState` / `SetScissorRect` / `SetViewport`. | 🟨 | **CLOSED 2026-07-14 — forced in early (`SetViewport` even more urgently than the others: `GraphicsDevice`'s own constructor calls it unconditionally via `UpdateViewportFromWindow()`, so a real device could not even finish constructing without it).** `ApplyRasterizerState` → `D3DRS_CULLMODE`/`FILLMODE`/`SCISSORTESTENABLE`/`DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` via the `D9-21` `CullModeToD3D9`/`FillModeToD3D9` tables; `SetScissorRect` → `IDirect3DDevice9::SetScissorRect`; `SetViewport` → `IDirect3DDevice9::SetViewport`. **This row's own predicted finding confirmed**: D3D9's `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` are floats, and XNA's own float `DepthBias`/`SlopeScaleDepthBias` map through with **no unit conversion** (unlike D3D11, which needs float→`INT` rounding) — `SetRenderState()` still takes a `DWORD` parameter for these two, so the float bits are reinterpreted (`std::bit_cast`), not numerically converted. 🟨 because the oracle-verification this row itself asks for ("verify against the oracle") is not done — `D9-84`'s job, needs a real draw first. `D3DCULL`'s own pixel-proof is `D9-21`'s still-open obligation, not re-litigated here. |
| D9-63 | `ApplySamplerState` per slot (`D3DSAMP_MINFILTER`/`MAGFILTER`/`MIPFILTER`/`ADDRESSU`/`ADDRESSV`/`MAXANISOTROPY`/`SRGBTEXTURE`), 16 slots. | ⬜ | Genuinely deferred (unlike D9-60/61/62, nothing forced this one in early) — no texture/sampler work exists yet for it to apply to. A `NotYetImplemented()` override was added as part of this session's audit (see `D9-11`'s updated row: this method's `{}` default is multi-line, so it was missed by the original single-line `grep` and was still silently empty until now). |
| D9-64 | Reuse the backend-agnostic `easygl_blendstate_*`/`easygl_depthstencilstate_*`/`easygl_rasterizerstate_*` CTest sources verbatim, as Vulkan and D3D11 already do. | ⬜ | Free cross-backend pixel proof — but note it proves *cross-backend consistency*, not XNA fidelity. The oracle (`D9-A`) is what proves the latter. |

---

## Phase D9-7 — Microsoft's stock effects: vendor, compile, embed

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-70 | **Vendor** `BasicEffect.fx`, `AlphaTestEffect.fx`, `DualTextureEffect.fx`, `EnvironmentMapEffect.fx`, `SkinnedEffect.fx`, `SpriteEffect.fx`, `Macros.fxh`, `Common.fxh`, `Lighting.fxh`, `Structures.fxh` verbatim from the FNA tree into `src/CNA/Internal/Backends/D3D9/shaders/xna/`, with Microsoft's copyright headers, the MS-PL `LICENSE`, and a provenance `README`. Add a `THIRD_PARTY_NOTICES.md` entry. | ⬜ | **Not one line edited** (design decision 3). Add a CI/script check that `diff`s them against the FNA tree and fails on any delta — the "don't touch the originals" rule must be enforced mechanically, not by good intentions. |
| D9-71 | Offline compile: reuse `dx9-spike/fxc_tool.cpp`'s `ID3DInclude` handler (already proven — do not rewrite it) + `D3DCompile` per entry point at Microsoft's own `vs_2_0`/`ps_2_0` targets → checked-in `d3d9_shaders.hpp`. The entry-point list is **extracted from the `.fx` files' own `compile` statements** (`grep -o "compile [vp]s_2_0 [A-Za-z0-9_]*" *.fx \| sort -u`), not hand-maintained. **Locked-in compile flags: `D3DCOMPILE_OPTIMIZATION_LEVEL3`, no other flags** — this is the exact configuration `D9-1`/`D9-73` proved gives 61/66 exact Microsoft matches; do not change it without re-running `compare_against_fxb.py` and updating `D9-73`'s numbers. Output header lives at `src/CNA/Internal/Backends/D3D9/shaders/d3d9_shaders.hpp`, namespace `CNA::Internal::Backends::D3D9::Shaders` (D3D9 is explicitly not part of `D3DCommon`, design decision 12 — this is its own namespace, not `D3DCommon::Shaders`). Array-naming convention: `k<EffectName>_<EntryPointName>` (e.g. `kBasicEffect_VSBasic`, `kSkinnedEffect_VSSkinnedPixelLightingFourBones`) — mechanical, derived directly from Microsoft's own names, not invented. | ⬜ | 66 entry points across 6 effects (verified count — see `D9-1`'s closing note; do not trust "64" if it appears anywhere else in older notes). Uses the `D9-1`-proven compiler, run in `~/.wine-cna-d3d9-spike` (the **only** prefix with the real Microsoft `d3dcompiler_47.dll` — see `dx9-spike/README.md`). **This prefix has no DXVK, and that is fine here**: compiling a shader never opens a D3D9 device, so `run-wine-dxvk.sh`'s DXVK-marker gate would reject this run for an unrelated reason — do not route this step through that script (`D9-5`'s new `run-wine-dxvk9.sh` is for runtime device tests, not for shader compilation). Invoke `fxc_tool.exe` directly through a bare `wine` call in the compile script. Not part of any CMake target — run by hand after a vendor refresh, same as the other two shader pipelines. |
| D9-72 | Transcribe Microsoft's `_vs(c#)`/`_ps(c#)` register annotations into one authoritative `D3D9ShaderRegisters.hpp` table; `static_assert` the C++ upload structs against it. | ⬜ | The layout is *given*, not designed (design decision 6). Where `D3DConstantBuffers.hpp` agrees, reuse it; where it disagrees, Microsoft wins and D3D9 gets its own struct. |
| D9-73 | **Cross-check against Microsoft's shipped `.fxb` bytecode. DECIDED 2026-07-14 by the project owner: CNA compiles the shaders itself from Microsoft's `.fx` sources; the `.fxb` is a verification oracle only, never a source of shipped bytes.** Consequence, and it is a real obligation, not a footnote: **the 5 divergent `PixelLighting` variants must be *proven* equivalent to Microsoft's originals against the `D9-A` oracle** (a dedicated scene per shader, exact-match threshold) — "presumably equivalent" is exactly the hand-wave this plan's goal forbids. If any of the 5 cannot be proven equivalent, escalate; do not quietly accept a delta. | 🟨 | **Run 2026-07-14. Result: 61 of our 66 compiled shaders are byte-identical to the bytecode Microsoft actually shipped** (comparing instruction streams — i.e. after stripping the CTAB/comment tokens, which legitimately differ because they embed the compiler version and source path). That is an extraordinarily strong signal: our compile pipeline reproduces Microsoft's own output almost exactly. **The 5 that differ are all `PixelLighting` vertex-shader variants** (`VSBasicPixelLighting`, `VSBasicPixelLightingTx`, `VSSkinnedPixelLighting{One,Two,Four}Bones`). The cause is **not** a compile flag — six flag combinations were tried (`OPTIMIZATION_LEVEL0/1/2/3`, `SKIP_OPTIMIZATION`, `AVOID_FLOW_CONTROL`) and none produced a match — it is a **compiler-version difference**: Microsoft built these with the XNA-era fxc (D3DCompiler_43, June 2010 SDK); we have `d3dcompiler_47` (2013+). **This row's decision is closed, not open** (design decision 4): CNA compiles its own shaders from Microsoft's `.fx` sources; the `.fxb` is a verification oracle only, never a source of shipped bytes. `compare_against_fxb.py` (checked into `dx9-spike/`, and destined for `src/CNA/Internal/Backends/D3D9/shaders/` alongside the compile pipeline) stays wired in as a standing regression check — re-run it if the compiler version or flags ever change, since it is the only thing that would notice a silent drop below 61/66. The 5 divergent shaders carry the open obligation from the row above: **proven** equivalent against `D9-A`, not assumed. |
| D9-74 | `D3D9ShaderCache` — `CreateVertexShader`/`CreatePixelShader` per entry point from the embedded bytecode; test creates all 66 through a live device. | ⬜ | **This is the first task in the plan that needs a D3D9 device actually running under DXVK, and no existing prefix provides that plus the real compiler at once** (`~/.wine-cna-d3d9-spike` has the real compiler, no DXVK; `~/.wine-cna-d3d11` has DXVK, no usable compiler). Two options, pick one before starting: (a) install DXVK into `~/.wine-cna-d3d9-spike` too (`dxvk-setup install`, same command `plan_dx.md` `DX-2` used) — the compiled bytecode is embedded in a checked-in header by this point, so the compiler DLL and the runtime device no longer need to coexist in the same *step*, only the same *machine*; or (b) create a fourth, purpose-built runtime prefix. Recommendation: (a) — one fewer prefix to document. Update `dx9-spike/README.md`'s prefix table either way. |

---

## Phase D9-8 — XNA shader dispatch

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-80 | Replicate the XNA shader-permutation model exactly: the `VSIndices[32]`/`PSIndices[32]` tables (transcribed from the `.fx` sources) and the `ShaderIndex` computation (ported from `BasicEffect.cs`/`AlphaTestEffect.cs`/`DualTextureEffect.cs`/`EnvironmentMapEffect.cs`/`SkinnedEffect.cs`, all present in the FNA tree). | ⬜ | Design decision 7. This is a **transcription task, not a design task** — if you find yourself inventing a selection rule, you have gone off the rails. `SkinnedEffect`'s one/two/four-bones permutations map directly onto `GpuDrawParams::weightsPerVertex` (which Task 895 added — a happy accident that turns out to be XNA's own model). **`DualTextureEffect`'s `ShaderIndex` needs only fog + vertex-color, both already in `GpuDrawParams` — it is fully transcribable today, with no gap.** `BasicEffect`/`SkinnedEffect`/`AlphaTestEffect`/`EnvironmentMapEffect` each have at least one gap — see `D9-81`, which is now known to be four effects wide, not two. |
| D9-81 | **Audit `GpuDrawParams` against XNA's real `ShaderIndex` inputs and report the gaps — ALL FOUR, not just the two originally found.** | ⬜ | **A real, confirmed, cross-cutting fidelity bug, verified 2026-07-14, and it is bigger than first described:** <br><br>**(1) `PreferPerPixelLighting` (`BasicEffect`, `SkinnedEffect`) — the headline bug.** Both effects declare it, default it to `false` (matching XNA), and **store** it — but `FillGpuDrawParams()` never reads it, so it never reaches any backend. Separately, CNA's *only* lit shader variant (`lit_textured3d`) computes full Blinn-Phong lighting **in the fragment shader**, always. **So CNA does not ignore the flag by rendering per-vertex; it ignores the flag by rendering per-pixel, always — the opposite of XNA's default.** XNA with `PreferPerPixelLighting = false` (what nearly every XNA game ships) selects `VSBasicVertexLighting*` and lights **per-vertex**, Gouraud-interpolated. **CNA has no per-vertex lighting shader at all, on any of its nine backends.** Visible exactly where per-vertex lighting differs most: specular highlights smear/band/vanish under Gouraud on low-poly geometry, and coarse meshes show faceting. Every CNA game today renders lighting *better* than XNA did — under this plan's goal, that is precisely a divergence, not a bonus.<br><br>**(2) `oneLight` (`BasicEffect`, `SkinnedEffect`) — a real boolean, only inferable in CNA.** XNA's `oneLight` means "only `DirectionalLight0` is enabled." CNA can only *infer* it from `GpuDrawParams`' zeroed `light1Diffuse`/`light2Diffuse`, which misfires for a light legitimately enabled but set to black (a legal XNA state). Likely a no-visible-difference case (black contributes nothing), but it is a structural gap, not a proven-safe one.<br><br>**(3) `AlphaTestEffect`'s `isEqNe` — NOT previously listed in this plan, found by a later audit.** XNA's `AlphaTestEffect.ShaderIndex` branches on whether `AlphaFunction` is `Equal`/`NotEqual` (`isEqNe`) versus every other `CompareFunction`. `GpuDrawParams` carries no `CompareFunction` at all — only a CNA-invented `alphaTest[4] = {refVal, tolerance, passWeight, failWeight}` float encoding. Recovering `isEqNe` from that encoding means **inferring** it (plausibly `tolerance > 0`), which is exactly the kind of invented selection rule `D9-80` says means "you have gone off the rails." **Without a decision here, `AlphaTestEffect` cannot select its correct XNA shader at all — this is not cosmetic, it is a hard blocker for that one effect.**<br><br>**(4) `EnvironmentMapEffect`'s `specularEnabled` — NOT previously listed in this plan, found by a later audit.** Same shape of problem as (3): a real `ShaderIndex` input with no `GpuDrawParams` field, only inferable from `envMapSpecular[3] != 0`, with the same "legitimately zero" misfire risk as `oneLight` — except here it gates which **shader** is selected, not just a boolean passed to one shader, so an inference mistake is not silently harmless the way it likely is for `oneLight`.<br><br>**Report; do not unilaterally fix any of the four.** Fixing (1)/(2) means extending `GpuDrawParams` and, for (1), authoring a new shader variant on seven other backends. Fixing (3)/(4) means extending `GpuDrawParams` with a `CompareFunction`-shaped field and a `specularEnabled` boolean respectively — smaller than (1), but still cross-cutting, still not this plan's call. **On D3D9 itself, all four are free** — Microsoft's own shaders already contain every permutation; the only missing piece is the C++ code to select among them, which needs the real inputs, not inferred ones. If (3)/(4) are not resolved before `D9-80` needs to dispatch `AlphaTestEffect`/`EnvironmentMapEffect`, escalate rather than shipping a guessed inference silently. |
| D9-82 | Upload constants at Microsoft's registers via `SetVertexShaderConstantF`/`SetPixelShaderConstantF`; implement `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`. | ⬜ | Expect the `D3DCULL` winding trap (`D9-21`) to bite on the first triangle. Budget for it. |
| D9-83 | `DrawInstancedPrimitivesEx` via `SetStreamSourceFreq` (`D3DSTREAMSOURCE_INDEXEDDATA`/`INSTANCEDATA`), indexed-only. | ⬜ | D3D9 hardware instancing only works with indexed draws — and `IGraphicsBackend::DrawInstancedPrimitivesEx()` is **already** indexed-only. No interface change needed. Note: XNA 4.0's own instancing is `HiDef`-only — enforce that (Phase D9-10). |
| D9-84 | **Every draw path validated against the oracle** (`D9-A5` scenes), not against another CNA backend. | ⬜ | This is where the plan's bar actually gets applied. A `lit_textured3d`-equivalent that "looks lit" is not done. |

---

## Phase D9-9 — SpriteBatch

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-90 | `D3D9SpriteBatchBackend` driving Microsoft's own `SpriteEffect` (`SpriteVertexShader`/`SpritePixelShader`) — quad batching, dest/source rect, origin, rotation, scale, `SpriteEffects` flip. | ⬜ | The shader is Microsoft's; only the quad construction and the `MatrixTransform` are CNA's. |
| D9-91 | **The half-pixel offset**, baked into `MatrixTransform` on the CPU (design decision 10 — `SpriteEffect.fx` has no offset in the shader, so XNA must have put it in the matrix). One site, named comment, and a pixel test that fails if it is removed. | ⬜ | Verify the exact offset **against the oracle**, don't reason it out from first principles — a half-texel argued from theory and a half-texel measured against XNA are not the same thing, and only one of them is the goal. |
| D9-92 | Sampler filter/address-mode wiring (`Wrap`/`Mirror`/`Clamp`) with discriminating probe pixels. | ⬜ | D3D11's `DX-131`/`DX-133` shipped these stored-but-inert at first. Wire them for real here. |
| D9-93 | Tested through the **public `SpriteBatch`/`Texture2D` API**, not the raw backend interface, and diffed against the oracle across all `SpriteSortMode`s. | ⬜ | |

---

## Phase D9-10 — `GraphicsProfile` made real

XNA 4.0's `Reach` and `HiDef` are **named subsets of Direct3D 9 device caps**. Today in CNA that
heritage is entirely lost: `GraphicsAdapter::IsProfileSupported()` is literally `return true;`, and
`QueryRenderTargetFormat()`/`QueryBackBufferFormat()` take a `GraphicsProfile` and `(void)`-discard it
(verified in `src/Microsoft/Xna/Framework/Graphics/GraphicsAdapter.cpp`). On D3D11/Vulkan/EasyGL there
is no honest way to fix that — there is no `D3DCAPS9` to consult, and any implementation would be a
hardcoded table pretending to be a capability query. **On D3D9 the query is native.**

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-100 | Map XNA 4.0's published `Reach`/`HiDef` capability floors onto `D3DCAPS9` fields (shader versions, max texture size, NPOT flags, `NumSimultaneousRTs`, max primitive count, `MaxVertexIndex`, render-target/back-buffer format whitelists, MSAA levels, instancing). One documented table, citing XNA's own profile documentation. | ⬜ | A research task as much as a coding one. |
| D9-101 | Implement `IsProfileSupported()` for real on D3D9, replacing `return true;`, against the live device's caps. | ⬜ | **Backend-local.** The other nine backends keep their honest `return true;` — see Boundaries. |
| D9-102 | Implement `QueryRenderTargetFormat()`/`QueryBackBufferFormat()` for real via `CheckDeviceFormat`/`CheckDeviceMultiSampleType`, honoring the profile's format whitelist and returning XNA's documented fallback when a format is refused. | ⬜ | Diff the fallback chain against the oracle — XNA's fallback behavior is documented, but documented and actual are not always the same thing. |
| D9-103 | Enforce the profile at resource creation: a `HiDef`-only format/size/feature requested on a `Reach` device throws the XNA-correct exception. | ⬜ | **The first time CNA can fail the way XNA failed.** |
| D9-104 | Tests, including at least one `Reach`-illegal request (e.g. wrapping on an NPOT texture, `D9-56`) that must be refused, and the `HiDef` equivalent that must be allowed. | ⬜ | |
| D9-105 | Document the caveat honestly: under Wine+DXVK, `D3DCAPS9` is **synthesized by DXVK**, not reported by an XNA-era driver. The profile logic is real; the caps it reads in this dev loop are not authentic. Provisional until `D9-140`. | ⬜ | Do not let the docs imply otherwise. |

---

## Phase D9-11 — Custom `ShaderEffect` (optional — ask before starting)

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-110 | CTAB parser: name → register mappings from D3D9 shader bytecode (design decision 9 — no `ID3DXConstantTable`). | ⬜ | ~150–250 LOC, documented format, **confirmed present** in this environment's SM output (the `vs_3_0` spike's blob carried a real `CTAB` section). Unit-test against a shader with known constants before wiring it to anything. |
| D9-111 | `D3D9EffectBackend` — runtime `D3DCompile()`, SM2.0 under `Reach` / SM3.0 under `HiDef` (this is where SM3 legitimately enters the plan), `SetUniform*` by name via `D9-110`. Links `d3dcompiler` on this target only. | ⬜ | Reintroduces the `d3dcompiler_47.dll` runtime dependency **for custom shaders only** — the stock pipeline stays dependency-free. Same conscious trade-off as D3D11's `DX-58`. |
| D9-112 | `SpriteBatch::Begin(effect)` wiring. | ⬜ | |

---

## Phase D9-12 — The indistinguishability suite

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-120 | Promote `D9-A`'s corpus to a real CTest (`D3D9_XNA_Diff`), running the full scene set against the checked-in XNA reference images, with the `D9-A4` thresholds enforced. | ⬜ | Reference images checked in (regenerating them requires the XNA prefix; the CTest must not). |
| D9-121 | A written, honest **divergence report**: every scene where CNA/D3D9 does not match XNA exactly, with the delta, the root cause, and whether it is fixable. | ⬜ | **This document is the plan's real deliverable.** "Indistinguishable" is a claim; this is the evidence. A short list is a triumph; a long list is still a triumph, because nobody has ever measured it before. What would *not* be a triumph is an empty list produced by loose thresholds. |
| D9-122 | `D3D9_Smoke` + `D3D9_Common` CTests + the 4 reused state tests (`D9-64`), mutation-verified. | ⬜ | |
| D9-123 | `CnaTests` under `CNA_GRAPHICS_BACKEND=D3D9`. Note D3D11 **cannot** build `CnaTests` today (~10 test files call POSIX-only `::setenv()`, `NEXT.md` §4); D3D9 will hit the identical wall. Either fix it once for all MinGW backends, or record the same honest gap. | ⬜ | Fixing it once benefits D3D11 and D3D12 too — worth proposing as a separate task. **Do not silently skip and report green.** |

---

## Phase D9-13/14 — Docs, real hardware

| ID | Task | Status | Notes |
|----|------|--------|-------|
| D9-130 | `docs/d3d9-backend.md` + a `D3D9` column in `docs/graphics-backend-feature-matrix.md` + a `README.md` build section. Lead with what this backend is *for* (XNA authenticity), the fact that it runs **Microsoft's own shaders**, and the divergence report — not with a feature checklist. | ⬜ | |
| D9-140 | **Real Windows hardware verification.** Non-negotiably: (a) real device-lost via alt-tab/RDP/lock-screen, with the XNA event order verified (`D9-34`); (b) real `D3DCAPS9` from Intel/AMD/NVIDIA drivers — Phase D9-10's authenticity claim is provisional without this; (c) exclusive fullscreen; (d) ideally, the `D9-A` oracle re-run on real Windows against real XNA, which is the definitive result. | ⬜ | `needs_human`. Same status as `DX-90`/`DX-114`. |

---

## What this backend will NOT do

- **No compute, no ray tracing, no texture arrays** — Direct3D 9. This is a feature of the plan, not a
  gap in it: XNA had none of those either.
- **MRT ≤ 4, same bit depth, no independent blending.**
- **Custom shaders are SM2.0 (`Reach`) / SM3.0 (`HiDef`)** — exactly XNA's own ceiling.
- **NPOT textures are cap-gated**, and on a `Reach`-class device may not support wrapping. **This is
  XNA-authentic behavior, not a defect.**

If a task's acceptance criterion requires crossing one of these lines, the task is wrong, not the
backend.

---

## Boundaries (stop and ask, don't improvise)

- **Do not start any implementation task without the project owner's explicit go-ahead.** Phase D9-0's
  spikes are closed; nothing else is authorized.
- **`D9-73` is DECIDED (2026-07-14): CNA compiles the shaders itself from Microsoft's `.fx`; the
  `.fxb` is an oracle, not a source of bytes.** The obligation that comes with it is binding: the 5
  `PixelLighting` variants that diverge from Microsoft's shipped bytecode must be **proven** equivalent
  against the `D9-A` oracle. Do not let that quietly become an assumption.
- **The six CNA-vs-XNA divergences must be fixed, but NOT from inside this plan.** See "CNA's
  divergences from XNA 4.0" above. They are cross-cutting (`GpuDrawParams` + a new shader variant on
  seven backends), they will break existing pixel-test baselines *correctly*, and they belong in
  `plan_graphics.md` under the project owner's authority. From inside this plan: **measure them with
  the oracle, document them, propose the fix — and wait.** A backend author who "just adds the flag
  while they're in there" has made a ten-backend decision on their own, which is precisely what every
  backend plan in this project forbids.
- **Do not start fixing Divergence 1 before the oracle exists.** Writing a per-vertex lighting shader
  for seven backends with no way to check it against XNA replaces a *measured* divergence with an
  *unmeasured* one, while feeling like progress. Oracle first. Always.
- **Never substitute CNA's own ported shaders for Microsoft's** and call the result authentic. That
  would defeat the entire purpose of this plan.
- **Do not edit Microsoft's `.fx`/`.fxh` files.** Ever. For any reason. `D9-70` adds a mechanical check
  that enforces this; do not disable it.
- **Do not "upgrade" the stock effects to `vs_3_0`/`ps_3_0`** because the hardware allows it (design
  decision 5). Different codegen is a different result.
- **Do not extend `GpuDrawParams` or `IGraphicsBackend` on your own authority** — not even for
  `preferPerPixelLighting` (`D9-81`), which is a genuine, confirmed fidelity bug. It touches nine other
  backends and is the project owner's call. **Report it, prove it with the oracle, propose it, wait.**
- **Do not implement a fake `D3DCAPS9`-equivalent for the other nine backends** to make
  `IsProfileSupported()` look "consistent." Its honest `return true;` on a modern API is *correct*; the
  entire value of Phase D9-10 is that exactly one backend can answer that question truthfully.
- **Do not switch to D3D9Ex** because device-lost turned out to be painful (design decision 2). That
  pain is the deliverable. If it becomes genuinely intractable, that is a finding to escalate, not a
  decision to make.
- **Do not widen an oracle tolerance to turn a red test green** (`D9-A4`). That single act converts
  this from an authenticity project into a parity project, silently, and nobody will notice for months.
- **Do not fix the `plan_graphics.md` bugs that `D9-A6` will surface.** Log them, move on. This plan
  builds the instrument; it does not also perform every surgery the instrument reveals.
- **Do not claim indistinguishability from Wine+DXVK results alone.** DXVK synthesizes `D3DCAPS9` and
  rarely loses the device — the two things this plan is proudest of are precisely the two this dev loop
  cannot authenticate. `D9-140` is a real gate.
- **Do not link D3DX** (design decision 9), and do not link `d3dcompiler` into the main backend target
  (design decision 16).
