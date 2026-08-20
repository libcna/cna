# Lane card — `dxold` · **FOURTH INTEGRATION LANE (closes Batch 0)** · **ADAPTED**

| Field | Value |
|---|---|
| Logical lane | `dxold` |
| Refs | `refs/heads/feature/dxold` and `refs/remotes/origin/feature/dxold` — **identical**, and **unmodified by the integration** |
| Head | `36289bb2eec7470fac53c2ff517181fe3ecf9af2` |
| Archive tag | **`archive/preintegration/dxold-20260804`** → `36289bb2` · annotated · GPG-signed · verifies good · local only · **unchanged** |
| Merge base with the integration branch | `ac3aaaeb` (`origin/develop`) — **develop-forked**, not audit-stacked |
| Own commits / files | **28 / 225** · diff `+48431, −16` |
| Behind the integration head at adaptation time | 791 commits |
| Subsystem | the **legacy DirectX backend family** — eight new `CNA::Internal::Backends` implementations plus their plans, docs, spikes, examples, CTest suites and Wine tooling |
| Shared interfaces | **none of the three** (no `GraphicsDevice.cpp`, no `IGraphicsBackend.hpp`, no `GraphicsCapability.hpp` edits) — but 15 shared files are modified (cmake registration, `GraphicsBackendType.hpp`, 4 test files, docs) |
| `GpuDrawParams` cost | **zero** — none of the four `fc0dd2a2`-removed fields appears anywhere in the lane (re-verified by grep) |
| Dependencies | MinGW-w64 system import libraries (`ddraw`, `dxguid`, `d3d10`, `dxgi`, `d3dcompiler`); DXVK's `d3d8.dll.a` for DX8 (system package path, overridable `CNA_DX8_DXVK_LIB`); Wine at runtime. **No vendored code, no downloads, no absolute developer-only paths** |
| Conflict class | **LOW** as recorded — confirmed for code; the real conflict surface was 6 shared files whose lane hunks were already superseded at the base |
| Development status | DEVELOPMENT COMPLETE (per `plans/plan_dxold.md`, all 8 backends spiked, implemented, CTest-verified 2026-07-20/21) |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED — partial (3/28)** + **MESSAGE CLEANUP (3/25)** — see below |
| Integration readiness | **ADAPTED AND MERGED — see §Adaptation record** |

## What the lane actually adds — Phase 1 scope, established from the tree

**Eight new public graphics backends**, all Route B (real MinGW-w64 Windows headers + genuine
era-correct COM interfaces + Wine/DXVK translation; **no `free-direct` anywhere in the family**):

| Backend | CMake value | API surface | 3D | Delivery |
|---|---|---|---|---|
| DX1 | `DX1` | DirectDraw v1 only (`IDirectDraw`/`IDirectDrawSurface`, never v2+) | none — throws (DirectX 1 has no Direct3D) | Wine builtin ddraw |
| DX2 | `DX2` | DirectDraw v1 + `IDirect3D2`/`IDirect3DDevice2::DrawPrimitive` | real geometry/Z/one-texture/blend/WireFrame + CPU lighting | Wine builtin ddraw |
| DX3 (real) | `DX30` → **`DX3` after the renames** | `IDirectDraw2` + the same `IDirect3DDevice2` 3D chain | as DX2 (verbatim port) | Wine builtin ddraw |
| DX5 | `DX5` | `IDirectDraw4`/`IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`, FVF `DrawPrimitive` | as DX2 + `Clear2` depth clears | Wine builtin ddraw |
| DX6 | `DX6` | same COM set as DX5 (no new interface exists at this era) | + real stencil (`DDPF_STENCILBUFFER` + `D3DRENDERSTATE_STENCIL*`) | Wine builtin ddraw |
| DX7 | `DX7` | `IDirectDraw7`/`IDirect3D7`/`IDirect3DDevice7` (`DirectDrawCreateEx`), viewport object removed | as DX6, `SetTextureStageState` texturing | Wine builtin ddraw |
| DX8 | `DX8` | `IDirect3D8` (no DirectDraw at all), GPU-quad SpriteBatch | fixed-function only; real GPU aniso | **DXVK D8VK** (`d3d8.dll.a` link + prefix DLL) |
| D3D10 | `D3D10` | `ID3D10Device`, real HLSL `vs_4_0`/`ps_4_0`, state objects, real MRT | vertex-colour 3D (owner-confirmed v1 scope) | Wine builtin d3d10 → **DXVK d3d10core** |

Registration is the repository's own link-time pattern — each backend defines
`CNA::Internal::Backends::CreateGraphicsBackend()`, selected by `CNA_GRAPHICS_BACKEND`; that is
why `GraphicsDevice.cpp` needed no edit, and why none of the three shared interfaces is touched.

**No internal helper is exposed as a public backend; no alias counts twice.** The family's 2D CPU
compositor is ported source, not a shared library. The public backend delta is **+8**.

Not part of the lane, verified rather than assumed: no `IGraphicsBackend.hpp` change, no
`GraphicsDevice.cpp` change, no `Content/` path-resolution change (`REMED-CONTENT-007`/`-008`
re-checked — the lane touches none of the files they live in), no `feature/direct2d` overlap, no
existing D3D9/D3D11/D3D12 code modified (`Backends/D3D9|D3D11|D3D12` untouched by the lane; only
their sibling docs mention them).

## The owner-ordered naming transition, executed inside this lane

Mid-integration (2026-08-04) the project owner instructed, live: *`DX3` becomes `FREEDIRECT`,
then `DX30` becomes `DX3`.* This executes the transition `plans/plan_dxold.md` had recorded as
"owner-authorized, not yet executed" since the lane was authored (the real DirectX 3 backend
shipped under the temporary `DX30` name precisely because the `free-direct`-backed backend owned
`DX3`). The owner's spelling **`FREEDIRECT`** supersedes the plan's earlier `FREE_DIRECT` sketch.
Executed as two dedicated commits after the faithful replay (`8a1e801e`, `dd4806f0`); task IDs
(`DX3-*`, `X*`, `DX30-*`) are kept verbatim as historical identifiers; `audit/` and
`remediation/` untouched; dated logs keep the names that were true when written.

**Public backend identity after this lane**: `FREEDIRECT` (the renamed free-direct backend — a
rename, not an addition) and the eight new backends `DX1 DX2 DX3 DX5 DX6 DX7 DX8 D3D10`. The
DirectX 3 generation is covered by two distinct public implementations: `DX3` (real
Microsoft/Wine) and `FREEDIRECT` (reimplementation library) — two contracts, not aliases.

## Original commit inventory and mapping

28 original commits `ac3aaaeb..36289bb2`, all linear, zero merges, zero WIP/fixup.

Metadata classes (object-level, `git cat-file -p`, not `%G?` alone — the `ext` lesson):

- **3 commits** (`c6f08fe6`, `fb18ec19`, `9145bebb` — the `docs/directx-legacy-backends-analysis.md`
  series) authored **and** committed by `Claude <noreply@anthropic.com>`, `Co-Authored-By: Claude
  Opus 4.8` + `Claude-Session:` trailers, **SSH-signed by the non-maintainer key** (`%G?` says `N`;
  the object carries a `gpgsig -----BEGIN SSH SIGNATURE-----`) → re-authored under policy A2,
  trailers stripped, GPG-signed.
- **25 commits** authored and committed by Robert Vokac, GPG-signed (`U`) → replayed with authorship
  and author dates preserved (A1), re-signed as replayed objects. Three of them carried "this
  session's …" process narration in their bodies (`437b66f2`, `e4a024c2`, `448223ce`) — rewritten
  minimally under §2.2, technical content preserved.

| # | Original | Adapted | Disposition |
|---|---|---|---|
| 1–3 | `c6f08fe6`, `fb18ec19`, `9145bebb` | `bc2e0de3`, `8e9fadb6`, `8527628f` | **TRANSFERRED** (re-authored, trailers stripped) |
| 4–7 | `3f10fad1`, `e56968ae`, `4d73b9f7`, `437b66f2` | `b039e421`, `4457f356`, `12c34afb`, `c4043a60` | **TRANSFERRED** (7 with message cleanup) |
| 8 | `1b8a8409` | `08700427` | **SPLIT: 2 hunks TRANSFERRED / 4 concerns ALREADY PRESENT** — Harnesses SDL3 lines, CnaLibrary+XnbBuiltInReaders VideoContentTypeReader exclusion/guard, UnitTests FFmpeg exclusions and MediaLibraryIndexTests Windows skip are all at the base via `REMED-BUILD-005`/`REMED-BUILD-013`; only the `CNA_ENABLE_NET=OFF` ENet test exclusion and the DX1 single-RT gate arm were still missing. Recorded in the adapted commit body |
| 9–10 | `e4a024c2`, `5fa68e65` | `985877d2`, `069b073c` | **TRANSFERRED** (9 message cleanup; 10 **hunk-adapted** — the base's own `Build locations & caching` CLAUDE.md section already covers most of the commit; only the spike-directory convention is genuinely new and is kept, retitled) |
| 11–19 | `af82a2f7` … `407abfe7` (DX2 sequence) | `38c70171` … `9f14e80c` | **TRANSFERRED** (byte-identical patches) |
| 20 | `448223ce` | `dbdff216` | **TRANSFERRED, one hunk ALREADY PRESENT** — the `gtest_discover_tests WORKING_DIRECTORY` fix is at the base as `REMED-BUILD-001` (identical code line); base comment kept |
| 21 | `f12e0c3a` | `a66c5d32` | **TRANSFERRED, one hunk SUPERSEDED** — the `DoesNotSupportWireFrame` edit targets a test `REMED-GFX-209` deleted; its intent (a true WireFrame report on DX2) is already asserted by the replacement's default arm |
| 22–27 | `e0929917`, `aba5509b`, `3fed7be6`, `d49e440b`, `dd1ec61e`, `d93c3de1` | `9a76dea7`, `60424c06`, `278b668a`, `87e5bd84`, `b83555ac`, `bf577c29` | **TRANSFERRED, same single superseded WireFrame-test hunk dropped in each** |
| 28 | `36289bb2` | `c0cad202` | **TRANSFERRED** (byte-identical) |
| — | *(new)* | `55e1269f` | interface adaptation to the post-audit contracts (below) |
| — | *(new)* | `8a1e801e` | owner rename DX3 → FREEDIRECT |
| — | *(new)* | `dd4806f0` | owner rename DX30 → DX3 |
| — | *(new)* | `acb085a8` | capability examples' direct `SetRenderTargets` calls to descriptor form |
| — | *(new)* | `d6a9bd32` | the two `GetData` overrides and one `rts[0]` the survey pattern missed on Dx8/D3D10 |
| — | *(new)* | `618afbcf` | the one compound CTest label the FREEDIRECT rename missed |
| — | *(new)* | `9256e606` | FreeDirect no3d oracle's REMED-CONTENT-004 completion (control-proven pre-existing) |

**No original commit is unaccounted for**: 27 TRANSFERRED (12 byte-identical, the rest with
recorded metadata/hunk adaptations), 1 SPLIT, 0 OMITTED. Every superseded hunk names its
superseding base change in the adapted commit's own body.

## Shared-interface adaptation (commit `55e1269f`)

The lane forked before the remediation campaign reshaped the backend contracts. Measured delta the
backends had to absorb (complete signature diff of `IGraphicsBackend.hpp` fork→head):

| Base change | Adaptation |
|---|---|
| `IVertexBufferBackend::SetVertexDeclaration(const VertexDeclaration&) = 0` (was an optional no-op over `std::vector<VertexElement>`) | every vertex-buffer backend stores the declaration via the shared header-only `DeclaredVertexLayout` |
| `SetRenderTargets(const RenderTargetBindingDescriptor*, int) = 0` (REMED-GFX-096; was `IRenderTargetBackend* const*` with a default) | 7 single-target backends keep their exact single-RT throw, add cube-face refusal, unwrap `GetRenderTarget2D()`; D3D10 keeps its real-MRT loop over descriptors |
| `ApplyBlendState(..., const BlendWriteState&)` (REMED-GFX-077, deliberately no default) | explicit decision recorded per backend: DX1–DX7 inexpressible at era (documented gap, the SDL_Renderer precedent); DX8/D3D10 native mask states exist, wiring deferred with each backend's other recorded deferrals |
| `ITextureBackend::GetData` → `[[nodiscard]] bool` | `Dx8TextureBackend` (the family's only override) returns true on success |
| `Texture3D` ctor now throws `System::NotSupportedException` on a capability-less backend (REMED-CONTENT-004) | `dx1_no3d_test` Check F asserts the typed throw (cubes keep degrade-gracefully — still true) |
| `REMED-GFX-209` WireFrame report contract | `WireFrameCapabilityReportIsThisBackendsOwn` gains a truthful-false DX1 arm; DX2..D3D10 satisfy the default true arm |
| **`REMED-GFX-DECL-GUARD`** (the post-audit obligation for stride-dispatching rasterizers) | the six stride-dispatching backends (DX2, DX3, DX5, DX6, DX7, DX8) call `RequireFaithfulVertexDeclaration` as the first statement of `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` with `UnlistedStrideLayout::BackendRefusesIt` — the same boundary and the same helper Software/D3D9/D3D11/D3D12 gained at the base. DX1 has no 3D route; D3D10 has no stride-dispatched Ex route |

No shared-interface file was modified; every adaptation is backend-local plus the two test files.
`GetMaxVertexStreams`/`GetMaxTextureDimension`/`MultiStreamVertexInput`/instancing all stay at the
base defaults — truthful for these backends (multi-stream reports false; instancing throws loudly).

## Dependencies and licensing

| Dependency | Kind | License | Provenance |
|---|---|---|---|
| `ddraw`, `dxguid`, `d3d10`, `dxgi`, `d3dcompiler` import libs + `ddraw.h`/`d3d.h`/`d3d8.h`/`d3d10.h` headers | MinGW-w64 system toolchain | MinGW-w64 runtime licensing (public-domain/ZPL-class), same as the shipping D3D9/D3D11/D3D12 backends | Debian package |
| DXVK `d3d8.dll.a` (DX8 link) + `d3d8.dll`/`d3d10core.dll`/`dxgi.dll` (runtime) | system package `/usr/lib/dxvk/`, overridable `CNA_DX8_DXVK_LIB` cache var with a clear configure-time error | zlib | Debian dxvk package |
| Wine ≥ builtin ddraw/d3d10 | runtime translation | LGPL | system wine-10.0 |
| SDL3 | already a project dependency (HWND source) | zlib | third_party submodule |

**No opaque binary is downloaded, no developer-machine-only SDK, no absolute local path in build
files** (the DXVK default path is a documented distro package location with an override). No
`EasyGL`/`MetaGL`/Direct2D dependency absorbed.

## Generated files and shaders

**No generated artifacts.** DX1–DX8 are fixed-function; D3D10's HLSL `vs_4_0`/`ps_4_0` sources are
inline strings compiled at runtime via `D3DCompile` (the D3D9/D3D11 precedent) — no offline
generator, no blobs, nothing to regenerate. `git diff --numstat` over the adapted range reports
zero binary files.

## Adaptation record — identity

| Field | Value |
|---|---|
| Integration base | `e0332214` (`integration/post-audit-phase1`, the Batch 0 stabilization checkpoint, tag `integration/checkpoint-batch0-20260804`) |
| Adaptation branch | **`adapt/dxold`** — created from `e0332214`, **retained** after merge |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-dxold` — **retained**, clean |
| Adapted head | **`9256e606`** — 35 commits, all GPG-signed (`U`), all authored **and** committed by Robert Vokac |
| Merge commit | **`990d6b8a`** — signed, true `--no-ff`, parents `e0332214` + `9256e606`; merged tree **byte-identical** to `adapt/dxold`'s tree |

The 35 commits are: 28 replayed originals (§mapping) + `55e1269f` (interface adaptation) +
`8a1e801e` (DX3→FREEDIRECT) + `dd4806f0` (DX30→DX3) + **four** bounded completion fixes found by
the validation matrix itself — `acb085a8` (the six capability examples' direct `SetRenderTargets`
calls to descriptor form), `d6a9bd32` (the two `GetData` overrides and one `rts[0]` the
adaptation's survey pattern missed on Dx8/D3D10), **`618afbcf`** (the one compound CTest label the
FREEDIRECT rename missed), and `9256e606` (the FreeDirect no3d oracle's REMED-CONTENT-004
completion, control-proven pre-existing). 28 + 1 + 2 + 4 = **35**.

> **Two count corrections, recorded at the Batch 0 closeout (2026-08-04).**
>
> 1. **This card's own enumeration was one short.** It named `three` completion fixes and listed 34
>    of the 35 commits; **`618afbcf` was missing** from both this paragraph and the mapping table.
>    The total 35 was always right, and `git rev-list --count e0332214..9256e606` confirms it.
>    Corrected in place — a lane card is living integration documentation.
> 2. **The merge commit `990d6b8a`'s body says "as 32 signed commits on `adapt/dxold`".** The
>    correct figure is **35**; the 32 excludes the three validation-driven fixes that followed it in
>    the draft, and its own inline enumeration accounts for 31. **The merge object is deliberately
>    left unmodified** — it is signed, it closes Batch 0, and amending published integration history
>    is forbidden. **The authoritative count is 35**, here and in `integration/BATCH_0_COMPLETE.md` §7.

## Losslessness chain

Byte-identity is proven at the replay boundary, then every subsequent transformation is a
reviewed, dedicated commit:

1. **Replay (28 commits):** all **210 added files byte-identical** to `36289bb2`
   (`git rev-parse` blob comparison, 210/210); the 15 modified shared files differ from the
   original head **only** by base content the adaptation deliberately keeps (REMED-BUILD-001/
   -005/-013 supersessions, the GFX-209 test rework, head drift) — each difference enumerated in
   §mapping and in the adapted commits' own bodies. `git range-diff` pairs 27 of 28 originals
   (12 `=` byte-identical, 15 `!` with metadata/recorded-hunk differences only); `1b8a8409`
   falls below the pairing threshold because 4 of its 6 concerns were already present, and its
   adapted form `08700427` carries exactly the two surviving hunks.
2. **Interface adaptation:** one commit, backend-local, every contract named.
3. **Renames:** two commits, identity-only, mechanical token classes + hand-reviewed narrative;
   task IDs preserved.

## Validation matrix — results

Builds: 10 configurations in the adaptation worktree (`cmake-build-dx1/dx2/dx3/dx5/dx6/dx7/dx8/
d3d10` MinGW cross + `cmake-build-freedirect` native + `cmake-build-d3d9` MinGW control), all
new (none existed for these configs anywhere), all ccache-launched, all built with explicit
targets (never `all`), max parallelism `-j8`. Wine runs on `:99`; native runs on `:101`; `:0`
never used; `WAYLAND_DISPLAY` unset by the wrappers.

| Gate | Result |
|---|---|
| DX1 dedicated suite (Wine ddraw) | **10/10** — twice: pre-rename and re-run post-rename |
| DX2 dedicated suite | **19/19** |
| DX3 dedicated suite (renamed identity, fresh Wine prefix auto-init) | **19/19** |
| DX5 dedicated suite | **19/19** |
| DX6 dedicated suite (incl. real stencil) | **20/20** |
| DX7 dedicated suite | **20/20** |
| DX8 dedicated suite (DXVK D8VK) | **20/20** |
| D3D10 dedicated suite (DXVK d3d10core, incl. real MRT test) | **10/10** |
| **Family total** | **137/137** |
| FREEDIRECT native suite (`:101`) | **19/20** — the one red is `FreeDirect_SpriteBatch`, reproduced **identically on the checkpoint's own pre-rename binaries** (zero-alpha general path + rotation-by-pi; the known pre-existing defect pair). `FreeDirect_No3D` was equally pre-existing-red and its oracle was completed (`9256e606`) |
| D3D9 modern-control cross-build | **exit 0** — CNA + backend + full `CnaTests` link under the D3D9 MinGW config after all renames/shared edits |
| Post-merge EasyGL principal suite (`cmake-build-noxna`, integration worktree, incremental) | **5912 ran · 5906 passed · 6 skipped · 0 failed** — the Batch 0 baseline exactly; complete log grepped, never tail-sampled. One earlier pass hit a single transient `SDL_InitSubSystem: x11 not available` fixture failure (`DrawUserIndexedPrimitivesArgumentGuardTest.VPC_32bit_NegativeCount_Throws`); the isolated rerun (22/22) and the clean full rerun classify it environmental/flaky, unrelated to any lane content |
| Whitespace / status | `git diff --check` clean; all worktrees clean |

Runtime identities, honestly stated: DX1–DX7 run against **Wine's builtin ddraw/d3d** (DXVK does
not translate DirectDraw); DX8 runs on **DXVK D8VK**; D3D10 runs on **Wine's builtin
d3d10/d3d10_1 forwarding to DXVK's d3d10core**; the underlying rasterizer on `:99` is llvmpipe.
None of this is native-Windows verification — the same Route-B caveat every existing D3D backend
carries.

**Sanitizer scope, honestly stated:** the eight new backends are MinGW/Wine targets — ASan/UBSan
are not available for them in this environment, the same classification the phase-1 checkpoint
applied to D3D9/D3D11/D3D12. The lane's native-code footprint after adaptation is identity-only
(enum header, renames) plus test files; the existing native ASan baseline (Batch 0) covers the
shared code, and the FreeDirect functional suite covers the renamed native backend. No new
native production logic was introduced that a sanitizer could exercise.

## Residuals

| Residual | Classification |
|---|---|
| `FreeDirect_SpriteBatch` 2 sub-checks (zero-alpha, rotation-by-pi) | **checkpoint residual**, control-proven on pre-rename binaries; the rotation half is a real production defect on the FreeDirect backend, previously recorded; deliberately not fixed by this lane |
| DX1 `CnaTests` full-suite reds (documented 48 at the lane's own DX1-88 run: 3D-content loads on a 2D-only backend, ungated capability asserts, FFmpeg-off media, Wine quirks) | **documented structural boundary** of 2D-only backends, identical class to FREEDIRECT/SDL_RENDERER/ASCII at the base; the lane's own docs (`docs/dx1-backend.md` §7a) carry the breakdown. Full CnaTests-under-Wine was not re-run per backend this session; the dedicated suites are each backend's own declared gate |
| `REMED-CONTENT-007`/`-008` | **OPEN HIGH/P1, carried forward** — re-checked: the lane touches none of the path-resolution files |

## New findings

**No new remediation ticket.** Everything encountered falls in the declared non-ticketable
classes: expected old-API adaptation (the interface deltas), original metadata cleanup, the
supersessions, and two pre-existing checkpoint residuals that were reproduced on controls and
documented (the FreeDirect no3d oracle was completed test-only, the SpriteBatch defect pair was
already known). No independent production defect was introduced or discovered beyond what the
base already carries.

## Completion criteria

| # | Criterion | State |
|---|---|---|
| 1 | Refs fetched; head/base/archive tag verified; originals never moved | ✅ `36289bb2` everywhere, tag verifies good, re-checked after merge |
| 2 | Every original commit classified and mapped; none lost | ✅ 27 TRANSFERRED / 1 SPLIT / 0 OMITTED |
| 3 | Actual public backend set established from the tree | ✅ +8 public backends; FREEDIRECT = rename, not addition |
| 4 | Dependencies/licenses reviewed | ✅ system MinGW + DXVK package, no vendored/opaque additions |
| 5 | Current graphics contracts preserved; obligations applied | ✅ decl-guard on all six stride-dispatching backends; truthful WireFrame arms; descriptor RT handoff; explicit BlendWriteState decisions |
| 6 | All adapted commits signed, attribution-clean | ✅ 35/35 `U`, zero prohibited tokens |
| 7 | Provenance: range-diff + mapping + losslessness chain | ✅ this card |
| 8 | Required builds/tests pass; runtimes honestly identified | ✅ 137/137 + 19/20-with-control + D3D9 control + post-merge suite |
| 9 | One signed `--no-ff` merge; prior ancestry intact; nothing pushed | ✅ `990d6b8a`; checkpoint/batch0/3 prior lanes verified ancestors; no push |
| 10 | Owner naming transition executed and documented | ✅ FREEDIRECT / DX3, task IDs preserved |

**Lane status: DONE. Batch 0 is complete — 4 of 4 lanes integrated.** `adapt/dxold` and its
worktree are retained for provenance review. Nothing was pushed.
