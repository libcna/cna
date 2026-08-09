# HTML DOM integration lane — accepted 2026-08-08

> **Status: ACCEPTED.** HTML DOM is the eighteenth integrated logical lane and the third of three
> Batch 5 members. Signed merge `24bf4786af1ff6b1cf86640e85a22f76c7315818` has first parent
> `ba5fa60166bef2214a4c08b64d50570d1120b7b9`, second parent
> `a32977f397da0c667a4162ee73f9d2363e4981d2`, and a tree byte-identical to the second parent.
> Batch 5 is complete at 3 of 3, but its checkpoint is **BLOCKED** by the pre-existing mandatory
> `REMED-CONTENT-007/-008` gate in `INTEGRATION_ORDER.md` §6. No checkpoint tag was created and no
> nineteenth lane began.
>
> The current host has neither Emscripten nor Node, so this acceptance combines the original
> lane's recorded real-browser evidence with a clean native host-contract rebuild, linked
> ASan/UBSan, deterministic native-selection rejection, and cross-backend controls. It is not a
> claim that the adapted browser binary was rebuilt or executed in this session.

## 1. Identity, implementation, and boundary

`CNA_GRAPHICS_BACKEND=HTML_DOM` is a distinct public, Emscripten-only, 2D backend identity. It is
not an alias for Canvas, WebGL, EasyGL, Software, or Stub, and there is no fallback renderer:

```text
GraphicsDevice / SpriteBatch
        -> HtmlDomGraphicsBackend
        -> handwritten EM_JS bridge
        -> pooled <div> sprite elements + CSS compositing (backbuffer)
        -> private Canvas2D OffscreenCanvas surfaces (RenderTarget2D only)
        -> SDL/Emscripten canvas as the host/layout anchor
```

The backbuffer is an actual browser DOM subtree, not a canvas rasterization disguised as an HTML
backend. Canvas2D is private to offscreen render targets and image preparation. JavaScript glue is
handwritten in `EM_JS` bodies; Emscripten supplies the wasm/DOM bridge and SDL supplies the browser
window/canvas integration. A native `HTML_DOM` selection fails at configure time with an explicit
Emscripten-only diagnostic.

The supported public surface is texture/SpriteBatch-oriented 2D plus `RenderTarget2D` in tightly
bounded form. Vertex/index resources, primitive draws, cube/3D textures, cube render targets,
queries, programmable effects, depth/stencil, MSAA, MRT, wireframe, and unsupported state variants
reject deterministically.

Public backend identities moved token-exact **37 → 38**, adding only `HTML_DOM`. The enum, public
string, build selector, macro, factory, target registration, and Emscripten platform gate all name
the same implementation.

## 2. Provenance and history

| Field | Result |
|---|---|
| Original branch | `claude/html-dom-cna-backend-xefzwf` at `8e4e42935a0962bd5eb6178fe4698334075f94ee` |
| Fork/base | `f5645c646d32d77e0d06eb8d5e729c73df768fa6` |
| Historical contribution | 55 linear commits; 50 changed files; 21 maintainer-PGP, 17 maintainer-SSH and 17 prohibited-author commits in the original history |
| Archive | sole signed annotated tag `archive/preintegration/html-dom-20260804`, tag object `cb73cb0b541ffdd3ded44e16df278cacb813e087`, peeling exactly to the original head |
| Adaptation base | accepted GDI merge `ba5fa60166bef2214a4c08b64d50570d1120b7b9` |
| Adaptation head | `a32977f397da0c667a4162ee73f9d2363e4981d2` |
| Adapted history | 50 signed linear commits: 49 chronological historical replays and one post-audit stabilization commit |
| Merge | signed `--no-ff` merge `24bf4786af1ff6b1cf86640e85a22f76c7315818` |
| Signing identity | Robert Vokac, key `FB9CE8E20AADA55F`; every recreated commit and the merge verify cryptographically good |

Six pure Canvas commits were intentionally omitted because they do not implement HTML DOM:
`7f6eb955`, `6af0d1fa`, `f2b8f069`, `dafd3ab6`, `8db1cff5`, and `982e7a63`. The HTML DOM hunk of
mixed commit `d4756990` was replayed; its unrelated Canvas hunk was omitted. Every other meaningful
commit was replayed in chronological order with original author name/email, author date, technical
subject, and intent preserved, while prohibited attribution and nontechnical process trailers were
removed and every recreated object was GPG-signed.

The complete range-diff maps all 49 carried commits and accounts for exactly the six omissions.
At the end of the historical replay, a scoped comparison of every HTML DOM source, header, build,
test, example, script, plan, and backend-document file is byte-identical to the archived feature
tree. The final stabilization commit then deliberately changes 37 files (+494/-144) to align that
implementation with current post-audit contracts.

## 3. Historical worktree disposition

The historical worktree was preserved untouched. Its superproject reports ` M third_party/SDL`
because the index records SDL commit `cbe3fbe9` while the submodule worktree is checked out at
`591b584b`. The submodule itself has no modified or untracked files: this is a checkout-pointer
mismatch, not uncommitted source work. Nothing was reset, restored, updated, stashed, or cleaned.

A separate clean detached worktree at the original feature head, with its recorded submodule
checkout, supplied historical comparisons and baseline probes. All adaptation writes occurred only
in the dedicated clean `adapt/html-dom` worktree.

## 4. Replay and conflict classification

The replay required four conflict stops containing **11 file-conflict resolutions**: eight initial
registration/build/interface files, one stale test file, one shared SpriteBatch file, and one
capability-enum file.

| Historical change class | Resolution |
|---|---|
| A — still required | Genuine `HTML_DOM` identity, Emscripten selector/factory/gate, DOM/CSS implementation, private Canvas2D render targets, browser harness, tests, examples, plans, and documentation |
| B — independently present | Current post-audit resource factories, cache authority, draw-parameter shape, state validation, capability routing, and backend registrations were retained |
| C — superseded | Stale shared-interface snapshots and obsolete test registration were not copied wholesale |
| D — semantic adaptation | Added current unsupported-resource guards, dynamic additive capability, Immediate-mode signalling, checked pitch/address-space conversion, current render-target validation, and capability-switch coverage |
| E — tests | Historical browser oracles retained; native host-contract and sanitizer coverage added; stale expectations adapted to the current public contract |
| F — build/docs | Emscripten-only target/gate, explicit host-test option, documentation, plan, and CI registration retained or corrected |
| G — unrelated/not carried | Six Canvas-only commits and the Canvas-only hunk of the one mixed commit |

Current integration files remained authoritative. No old all-interface file was copied wholesale.

## 5. Shared production surface

The lane is intentionally high-conflict and is not backend-local-only. Shared production changes
are limited and directly justified:

- public `HTML_DOM` registration and the Emscripten-only build/factory path;
- appended `GraphicsCapability::AdditiveBlending`, with every current backend's capability switch
  swept and updated so the answer describes CNA behavior rather than API theory;
- additive `IGraphicsBackend::SetImmediateMode(bool)` with a default no-op, called by shared
  `SpriteBatch` once per `Begin()` so HTML DOM can realize Immediate timing without changing other
  backends' established behavior; and
- exclusion of two standalone Win32 Glide ABI programs from the recursively globbed `CnaTests`
  translation-unit list (`HTMLDOM-123`).

No shared `Texture2D` cache code changed. `REMED-GFX-223` authority/isolation semantics remain
intact; `REMED-GFX-224` remains the same EasyGL-only MEDIUM/OPEN issue. Accepted Glide and GDI
production semantics were not reopened or weakened.

## 6. Capability truth

| Capability | HTML DOM result |
|---|---|
| `AdditiveBlending` | **dynamic** — true only when the browser reports real CSS `mix-blend-mode: plus-lighter`; it does not imply arbitrary blend-state support |
| `ThreeD` | false |
| `DepthStencilBuffer` | false |
| `StencilBuffer` | false |
| `MultiSampleAntiAliasing` | false |
| `MultipleRenderTargets` | false |
| `AnisotropicFiltering` | false |
| `OcclusionQuery` | false |
| `CustomEffects` | false |
| `Texture3D` | false |
| `Instancing` | false |
| `MultiStreamVertexInput` | false |
| `WireFrame` | false |
| PBR | not applicable; no current capability-enum member and no programmable 3D/effect path |

The capability answers are implemented and host-contract tested; the dynamic browser-positive
answer is supported by the historical real-browser suite, not re-executed on the current host.

## 7. DOM, CSS, and lifetime result

- The backend owns a viewport wrapper and logical root over the SDL canvas, uses collision-checked
  unique IDs, reference-counts multiple devices sharing the same browser surface, preserves and
  restores canvas visibility, and removes the exact resize listener during final teardown.
- Sprite `<div>` elements are pooled per scissor region. Unused elements are hidden; excess above a
  settled high-water mark is physically removed after 180 stable `Present()` frames. Scissor
  regions are capped at 16 with LRU eviction and are removed with the final device subtree.
- Texture and render-target browser objects live in `Module.cnaDomTextures`. Texture variants are
  owner-scoped, hit-promoted LRU entries capped at 256, invalidated by repeated `SetData`, and
  deleted on texture destruction. A bound render target clears its browser ID on destruction.
- Styles are inline/object-owned; no accumulating global style element or event handler is left.
  The recorded dispose, memory, churn, two-device, cache-shrink, and pool-regrowth browser oracles
  cover this ownership model. Native ASan cannot prove browser/JS lifetime, so those browser tests
  remain the applicable evidence.

No designed DOM accumulation remains across repeated devices or settled frames within the tested
scope.

## 8. Coordinates, layout, and image orientation

HTML DOM uses CNA's top-left convention. Logical sprite coordinates map to CSS pixels; viewport
offsets, scissor regions, z-order, transform origin, flip/rotation, and image orientation are
explicitly encoded. The logical root stays at backbuffer size; per-batch viewport translation and
per-region clipping prevent a later batch from retroactively moving or clipping an earlier one.
All `CnaPresentationMode` scale/offset rules, resize, letterbox rejection in coordinate transforms,
and Canvas2D render-target viewport/scissor paths have historical pixel or structural coverage.

There is no direct `devicePixelRatio` query in the backend. It uses the SDL browser canvas's CSS
`offsetWidth/offsetHeight` as the physical layout boundary while SDL/Emscripten owns the drawable
backing store. The original browser evidence therefore establishes the tested browser/layout
configuration (effectively DPR 1), not an independent DPR>1 guarantee.

## 9. Colour, alpha, and blending

The backend accepts only the four standard blend presets and rejects custom blend factors,
functions, write masks, and sample masks. Tint RGB uses cached browser image variants and tint alpha
uses CSS opacity on the DOM path. `AlphaBlend` is pixel-exact in the recorded browser suite;
`Opaque` is exact on the Canvas2D render-target path but has the documented DOM alpha/compositor
boundary. `NonPremultiplied` and `Additive` are colour-correct in their supported paths, while
translucent output alpha retains the documented CSS-compositor divergence. Additive fidelity is
guarded by the dynamic capability above. Zero alpha, destination transparency, tinted alpha,
premultiplied render-target round trips, and nontrivial translucent cases are explicitly covered.

There is no claim of arbitrary XNA blend equivalence through CSS.

## 10. Texture, image, cache, and readback result

- Supported ordinary textures are one-level RGBA8. Uploads accept tight or padded source rows,
  copy row-by-row, reject short pitch/spans transactionally, and use checked width/row/total
  arithmetic suitable for the wasm32 address space (`HTMLDOM-122`).
- Browser representation is a Canvas/ImageData-backed texture registry with lazily generated PNG
  image variants for DOM draws. Repeated `SetData` invalidates the correct owner variants rather
  than reusing stale images.
- `RenderTarget2D` is supported only for `SurfaceFormat::Color`, level zero, no mipmap,
  depth/stencil, or MSAA. It uses private Canvas2D and supports readback. Live DOM backbuffer
  readback is explicitly unsupported and throws because browsers do not rasterize a DOM subtree
  into readable pixels for this implementation.
- Clamp, Wrap, Point/Linear, symmetric Mirror, and non-Mirror mixed axes are implemented within the
  documented boundary. Mirror mixed with a different axis mode, anisotropic/mipmap/min-mag-split
  filters, and other inexpressible combinations reject.

No shared Texture2D authority change was made. Cache-isolation controls for `REMED-GFX-223` pass.

## 11. SpriteBatch, ordering, effects, and states

Shared SpriteBatch performs requested sorting; DOM document order and per-flush z-order realize the
result without silent reordering. Deferred mode snapshots state at batch flush. Immediate mode now
flushes each draw under the state active for that draw, verified historically with interleaved
scissor rectangles; the new shared hook is exception-safe and a no-op for other backends. Per-batch
viewport and scissor regions preserve earlier/later overlap order, region eviction is
non-destructive, and unused sprites do not survive visibly into the next frame.

There is no arbitrary shader stage. The fixed texture/tint/transform/SpriteFont path and the
documented standard blend/sampler/rasterizer subset work. A custom `Effect`, depth/stencil use,
wireframe/cull/depth-bias state, unsupported render-target options, or other inexpressible state
throws at the backend boundary rather than silently approximating through Canvas or WebGL.

## 12. Baseline, build, runtime, sanitizers, and controls

### Historical baseline

The clean original head deterministically rejects a native intended configuration because HTML DOM
requires Emscripten. Its five HTML DOM translation units pass clean host syntax probes. The original
lane records real browser execution under an Emscripten/browser harness: smoke **69/69** plus its
screenshot checks, pixel **35/35** plus screenshot checks, stress **10/10**, dispose **17/17**,
host integration **2/2**, memory **6/6**, and GTest **54/54**. The records name local emsdk 6.0.5
and CI pin 6.0.3. Those results are preserved as historical evidence; they were not silently
relabelled as a current adapted-browser run.

### Current adaptation

| Instrument | Result and boundary |
|---|---|
| Toolchain discovery | no `emcc`, `em++`, `emcmake`, Node, npm, or npx; Chrome 151 and Xvfb exist, but the repository browser harness cannot build/run without the absent SDK/runtime |
| Native selection | deterministic Emscripten-only configure rejection; no fallback |
| HTML DOM host contracts | **57/57** current adapted implementation tests |
| ASan/UBSan | runtimes proven linked (`libasan.so.8`, `libubsan.so.1`); **57/57**, leak detection enabled, zero CNA ASan/UBSan/OOB/UAF/stale-wrapper/conversion-buffer reports |
| OPENGLES/EasyGL principal control | 110 selected across 17 suites: **109 pass + 1 intentional WireFrame-capability skip** under Xvfb |
| `REMED-GFX-223` | `CnjCacheIsolationTest` 2/2 and Texture2D cache controls 8/8 included in the green principal run |
| GDI | seven focused current x64 MinGW/Wine/Xvfb executables exit 0: 2D regression, colour matrix, public stencil, public API, applied state, unsupported paths, exact 4x MSAA |
| Glide | current unit/capability suites pass; standalone i686 fake-DLL ABI loader builds and exits 0 under Wine; full i686 backend remains externally blocked by the accepted sharp-runtime `__int128` limit |
| Diligent / Skia / Sokol | each changed capability translation unit passes its backend-specific compile probe; no runtime claim |

A deliberately attempted `SDL_VIDEODRIVER=dummy` EasyGL run failed because that driver cannot
provide OpenGL; the immediate Xvfb rerun passed and is the accepted result. A broad Diligent build
was stopped by signalling only its exact session-owned Ninja process after 217/904 dependency
steps; the required changed-source control was replaced by the focused successful compile probe.
Neither event indicates a CNA regression.

All compilation used explicit numeric bounds of at most four jobs. The only nested SDL bootstrap
helper was inspected and uses `--parallel 2`; no argument-less parallel helper ran. The session
maximum was **4**, `-j8` was never reached, and all work remained within the campaign's ≤8 bound.

## 13. Findings and Batch 5 result

| ID | State | Resolution |
|---|---|---|
| `HTMLDOM-121` | RESOLVED | Truthful deterministic unsupported resource/state/factory boundary and capability reporting |
| `HTMLDOM-122` | RESOLVED | Pitch-aware row packing plus checked allocation/source-span arithmetic |
| `HTMLDOM-123` | RESOLVED | Exclude standalone Win32 Glide ABI programs from recursive cross-platform `CnaTests` sources |

No unresolved supported-path HTML DOM finding remains. Existing findings were not absorbed or
renamed: `REMED-GFX-223` remains RESOLVED and green; `REMED-GFX-224` remains MEDIUM/OPEN;
`REMED-GFX-225` through `-233`, `REMED-BUILD-017/-018`, and GDI-054 retain their recorded resolved
states; `REMED-CORE-015` and `REMED-CONTENT-010` remain LOW/OPEN.

The logical inventory is now **18/21 integrated, 3 pending**. Batch 5 membership remains exactly
`glide` → `gdi` → `html-dom`; all three are accepted and stabilization is technically green. The
checkpoint decision is nevertheless **BLOCKED** because `INTEGRATION_ORDER.md` explicitly requires
the still-open HIGH/P1 `REMED-CONTENT-007/-008` path-containment findings to close before the Batch
5 checkpoint. No `integration/checkpoint-batch5-20260808` tag exists. Nothing was pushed,
`audit/` remains unchanged, and no nineteenth lane began.

**Only bounded next action:** close `REMED-CONTENT-007/-008` together as the existing Content path-
containment safety task, then retake this checkpoint decision. Do not begin that task from this
record.
