# CNA renderer expansion candidates

Date: 2026-08-13. Rewritten 2026-09-17 after the renderer-set curation.

> **THIS DOCUMENT IS RESEARCH, NOT A ROADMAP.**
>
> Nothing listed here is planned, committed, scheduled, or authorized, and nothing may be
> implemented because it appears in this file. Every identity below would require a **fresh
> explicit owner instruction**, its own plan file, and its own acceptance criteria before a single
> line is written.
>
> **CNA intentionally maintains a curated renderer set.** On 2026-09-17 twenty-five identities were
> retired precisely because breadth had stopped paying for itself
> (`plans/plan_renderer_cleanup.md`). A renderer earns a place only when it provides meaningful
> platform coverage, compatibility value, architectural value, or a capability the existing set does
> not reasonably cover. **Renderer count is not a goal**, and no total in this document is a target.
> Several entries below were in fact implemented and then retired; they are kept, marked, as the
> record of what that cost.

Authoritative companions:

- `docs/renderer-registry.md` — the **live** public identities (canonical; this file never
  overrides it).
- `docs/removed-renderers.md` — the retired identities and their permanently reserved ABI values.
- `docs/graphics-renderer-feature-matrix.md` — what the established renderers actually prove.

---

## 1. What CNA supports today

**25 public renderer identities**, mechanically verified by `scripts/check_renderer_identities.py`
against `modules/core/include/CNA/GraphicsRendererType.hpp`, `cmake/RendererIdentities.cmake`, the
runtime registry and the C ABI. Selected at configure time via `-DCNA_GRAPHICS_RENDERER=<selector>`;
implementations live in `modules/renderers/<family>/`. Twenty-five former identities were retired on
2026-09-17 (`plans/plan_renderer_cleanup.md`, `docs/removed-renderers.md`).

The five GL profiles (`OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`) share the internal
EasyGL implementation while keeping distinct public contracts (context, shader profile, platform).

| Class | Identities | Count |
|---|---|---:|
| No pixels (validation/no-op) | `HEADLESS`, `STUB` | 2 |
| 2D-oriented | `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `FREEDIRECT`, `DIRECT2D`, `GDI` | 7 |
| CPU 3D | `SOFTWARE`, `PORTABLEGL` | 2 |
| Programmable / modern | `OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`, `OPENGL4`, `VULKAN`, `WEBGPU`, `METAL`, `DIRECTX9`, `DIRECTX11`, `DIRECTX12`, `SDL_GPU` | 13 |
| Abstraction layer | `FNA3D` | 1 |
| **Total** | | **25** |

Notes that must not be misstated anywhere: `WEBGPU` is experimental with a bounded verified
surface; the `ASCII` identity was **removed** in favour of the renderer-neutral
`CNA::Graphics::AsciiPostProcessEffect`.

## 2. The former "Phase 2" list (no longer a roadmap)

`misc/FUTURE.md` Phase 2 once carried eight identities as *planned expansion*. That phase was
**closed on 2026-09-17** with the curation, and these are now research entries with the same
standing as everything in §3 — no priority, no commitment, no schedule.

| Identity | Upstream | Class | Status |
|---|---|---|---|
| `IGL` | facebook/igl | Abstraction RHI | **Implemented 2026-09, retired 2026-09-17.** The one entry here that was actually built; see `docs/removed-renderers.md`. |
| `NVRHI` | NVIDIA NVRHI | Abstraction RHI | never started |
| `KORE` | Kode/Kore (Kinc lineage) | Abstraction RHI | never started |
| `METHANEKIT` | MethaneKit | Abstraction RHI | never started |
| `LINAGX` | LinaGX | Abstraction RHI | never started |
| `TEMPEST` | Tempest | Abstraction RHI | never started |
| `THORVG` | ThorVG | 2D vector | never started |
| `REACT_DOM` | React/DOM | Web DOM — only if it can truthfully satisfy a graphics contract | never started |

Six of the eight are abstraction RHIs. CNA has now retired four renderers of exactly that shape
(bgfx, Diligent, LLGL, IGL) after building them, which is the strongest available evidence that
*another* RHI wrapper is the least defensible kind of addition, not the most obvious one. Any future
proposal from this row has to answer that first.

## 3. New candidates

**41 candidate identities** as first catalogued, none of which duplicated a live identity at the
time. Three of them — **A2 `TINYGL`**, **A6 `NANOVG`** and **B1 `RLGL`** — were subsequently built,
and all three were **retired again on 2026-09-17**. They are left in place below, marked, because
that outcome is the most useful information this catalog contains: each one passed the "proves what
nothing else proves" test on paper and still did not earn its keep.

Each row states the one thing it proves that no existing CNA identity proves. That column is the
admission test, but it is now a **necessary and not a sufficient** condition — the 2026-09-17
curation retired identities that all satisfied it. Several rows argue against a renderer that no
longer exists; where that is so, the row is marked, because the comparison it drew is no longer
available.

Effort: **S** ≈ one focused lane, **M** ≈ a plan file of ~30–60 tasks, **L** ≈ comparable to the
Vulkan campaign, **XL** ≈ needs hardware or a toolchain CNA does not have.
Licenses are as commonly reported upstream and **must be re-verified at spike time** — several are
copyleft or version-dependent and could be disqualifying.

### Tier A — first wave (Linux dev-loop buildable, CI-friendly, low dependency risk)

| # | Identity | Upstream / API | Class | Proves what nothing else proves | Gate | Effort | Risk | License |
|---:|---|---|---|---|---|---|---|---|
| A1 | `OSMESA` | Mesa `libOSMesa` | CPU GL 4.x | Real, spec-complete desktop GL executed with **zero GPU and zero display server** — the only way to run the full EasyGL/`OPENGL4` feature matrix in CI. `PORTABLEGL` is a partial GL 3.x reimplementation, not Mesa. | none (CPU) | S | low | MIT |
| A2 | `TINYGL` | C-Chads/tinygl (Bellard lineage) | CPU fixed-function | **DELIVERED 2026-08-13, RETIRED 2026-09-17.** Fixed-function GL 1.x on CPU. Its argument rested on contrasts with `OPENGL1` (also retired) and `PORTABLEGL`; in practice its 1-bit colour-key transparency, absent stencil/scissor/render-targets and absent shaders left it unable to serve as a general CPU renderer, which `SOFTWARE` and `PORTABLEGL` already do. See `../plans/plan_tinygl.md`. | none | S | low | zlib-style, acknowledgment required |
| A3 | `SWIFTSHADER` | google/swiftshader | CPU Vulkan | A conformant **Vulkan** API surface with no GPU — lets the `VULKAN` renderer's contract be regression-tested on GPU-less machines without claiming it as `VULKAN`. (Weigh against §4's llvmpipe/lavapipe ruling: a software **driver** behind `VULKAN` is not an identity.) | none | M | med | Apache-2.0 |
| A4 | `CAIRO` | cairo | 2D vector | ⚠ Premise expired: the `SKIA` and `BLEND2D` identities it was meant to sit beside are both retired, so CNA has no CPU 2D vector identity for it to be a *third* of. What survives is the device-agnostic backend set (image, X11, PDF, SVG) behind one API. | system pkg | M | med | LGPL-2.1 / MPL-1.1 — **verify** |
| A5 | `XLIB` | X11 `XImage`/MIT-SHM | 2D presentation | The Linux peer of `GDI`: CPU pixels pushed straight to a bare X server, no SDL, no GL, no toolkit. | Linux + X11 | S | low | MIT |
| A6 | `NANOVG` | memononen/nanovg | 2D vector on GL | **DELIVERED 2026-08-19, RETIRED 2026-09-17.** Vector-first 2D on a real GPU context through NanoVG's own compiled GLSL pipeline. Its distinguishing comparisons were all against identities retired on the same day (`OPENVG`, `SKIA`, `BLEND2D`), which is itself the lesson: a candidate defined only by how it differs from other candidates does not establish that any of them were needed. See `../plans/plan_nanovg.md`. | GL context | S | low | zlib |
| A7 | `FBDEV` | Linux `/dev/fb0` | 2D presentation | Rendering with no window system **and no GPU driver stack** at all — the minimum viable embedded/console target. | Linux | S | low | n/a (kernel ABI) |
| A8 | `DRM_KMS` | DRM/KMS dumb buffers | 2D presentation | Direct modeset + scanout ownership (kiosk/appliance), incl. real vsync/page-flip semantics `FBDEV` cannot express. | Linux + DRM | M | med | n/a (kernel ABI) |
| A9 | `OPENGLES32` | OpenGL ES 3.2 | Programmable | The GL family's 6th profile: **compute shaders, tessellation, geometry stage** — capabilities no current GL profile may truthfully report. | ES 3.2 driver | M | low | n/a |
| A10 | `SIXEL` | libsixel / DEC SIXEL | 2D presentation | **True pixels in a terminal.** Distinct from the removed `ASCII` identity in kind, not degree: that was glyph quantization (now a post-process effect), this is a real framebuffer transport. The CPU-frame producer a terminal transport would attach to exists again: `SOFTWARE` sets `needsSurfacePresenter` and presents its backbuffer through `IPlatformSurfacePresenter` (`plans/plan_terminal_capi_repair.md` `TCR-2`), so this would be a second presenter behind the same contract rather than a gap to close first. | sixel-capable TTY | S | low | MIT |

Tier A is deliberately the cheapest way to widen coverage: eight of the ten need no GPU, so they
also strengthen CI.

### Tier B — established libraries and engines (medium effort, dev-loop buildable)

| # | Identity | Upstream | Class | Proves what nothing else proves | Effort | Risk | License |
|---:|---|---|---|---|---|---|---|
| B1 | `RLGL` | raysan5/raylib (`rlgl.h` only) | Low-level OpenGL abstraction | **STARTED 2026-09-12, RETIRED 2026-09-17 before completion.** Only its device/clear/readback/present/resize slice was ever runtime-validated; texture, draw, effect and render-target work never landed. The clearest case in this catalog of a renderer retired as unfinished rather than as inadequate. See `../plans/plan_rlgl.md`. | L | high | zlib |
| B2 | `SFML` | SFML `Graphics` | 2D | Classic RAII C++ 2D API as a renderer host; view/transform model unlike `SDL_RENDERER`. | S | low | zlib |
| B3 | `ALLEGRO` | Allegro 5 | 2D | Another mature 2D game library with its own bitmap/target model. | S | low | zlib-like |
| B4 | `OGRE` | Ogre-Next / Ogre 14 | Engine RenderSystem | A **scene-graph engine's** RenderSystem driven by an immediate XNA API — the hardest structural mismatch to prove, and the most valuable if it works. | L | high | MIT |
| B5 | `IRRLICHT` | Irrlicht | Engine, multi-driver | An engine that ships **its own software rasterizers alongside GL/D3D** — a single identity spanning both worlds. | M | med | zlib |
| B6 | `FILAMENT` | google/filament | PBR renderer | A physically-based renderer forced to reproduce XNA's fixed stock-effect lighting exactly; a strong fidelity probe. | L | high | Apache-2.0 |
| B7 | `THEFORGE` | ConfettiFX/The-Forge | RHI | A production cross-platform RHI with an explicit-descriptor model unlike bgfx/Diligent/LLGL — all three of which CNA has since built and retired (§2). | L | med | Apache-2.0 |
| B8 | `NRI` | NVIDIA NRI | RHI | Thin, low-abstraction RHI — the opposite design point from `NVRHI` (§2), so both are defensible. | M | med | MIT |
| B9 | `HORDE3D` | Horde3D | Engine | Minimal forward/deferred engine; small enough to fully verify. | M | med | EPL |
| B10 | `RBFX` | rbfx (Urho3D lineage) | Engine | A living Urho3D descendant with its own RHI; different again from B4/B5. | L | med | MIT |
| B11 | `BSF` | bs::framework | Engine RHI | RenderBeast/ct::RenderAPI — modern C++ engine RHI. | L | high | MIT |
| B12 | `FALCOR` | NVIDIA Falcor | Research renderer | Render-graph-first architecture driven by immediate-mode XNA calls. | L | high | BSD-3 |
| B13 | `QPAINTER` | Qt `QPainter` | 2D | The only candidate whose 2D output can target widgets, images, printers and PDF through one painter API. | M | med | LGPL-3 / commercial — **verify** |
| B14 | `AGG` | Anti-Grain Geometry | CPU 2D | Scanline AA rasterization with sub-pixel accuracy semantics; the Skia/Blend2D identities it was contrasted with are both retired. | M | med | **version-dependent (2.4 permissive, 2.5+ GPL) — verify** |
| B15 | `PIXMAN` | pixman | CPU compositing | Pure low-level composite/blit — the leanest possible truthful 2D contract. | S | low | MIT |
| B16 | `PLUTOVG` | plutovg / lunasvg lineage | CPU 2D vector | Tiny dependency-free vector rasterizer; a ThorVG counterweight with a much smaller surface. | S | low | MIT |
| B17 | `DIRECTFB` | DirectFB | Embedded 2D | Embedded Linux 2D acceleration layer with its own surface/layer model. | M | med | LGPL — **verify** |
| B18 | `WAYLAND_SHM` | Wayland `wl_shm` | 2D presentation | Native Wayland client presentation with no SDL, no GL, no XWayland. | M | med | MIT |
| B19 | `VNC` | RFB / LibVNCServer | Remote framebuffer | Output as a **network protocol**: headless hosts made visually inspectable, incl. by CI reviewers. | M | med | GPL-2 — **verify, likely disqualifying** |
| B20 | `PDF_VECTOR` | cairo-pdf / libharu | Document output | Frame output as a **resolution-independent vector document**, not a raster surface. Non-interactive by design. | M | med | varies |
| B21 | `XRENDER` | X11 XRender | Server-side 2D | Composition executed **by the X server**, not the client — a genuinely different execution locus from A5. | M | med | MIT |

### Tier C — platform-bound (need the platform; each also strengthens an under-covered OS)

| # | Identity | Upstream | Class | Proves what nothing else proves | Gate | Effort | Risk |
|---:|---|---|---|---|---|---|---|
| C1 | `QUARTZ2D` | CoreGraphics | 2D | macOS's `GDI`/`DIRECT2D` peer — today macOS has exactly one native identity (`METAL`). | macOS | M | med |
| C2 | `ANDROID_CANVAS` | `android.graphics.Canvas` via JNI | 2D | Android's own 2D stack; CNA currently reaches Android only through GL. | Android | M | high |
| C3 | `ANATIVEWINDOW` | NDK `ANativeWindow` | 2D presentation | Direct CPU buffer lock/post with no Java and no GL — the Android peer of `FBDEV`. | Android NDK | M | med |
| C4 | `WEBGPU_WEB` | Browser WebGPU (Emscripten) | Programmable | **WITHDRAWN 2026-09-04 — this row's premise is false.** It argued browser WebGPU was identity-worthy because `WEBGPU` was native-only. `WEBGPU` shipped its browser backend on 2026-08-26 (`WEBGPU-119`–`123`), through Emscripten's emdawnwebgpu port: 2D, the 3D `BasicEffect` path and every stock effect render in a real browser, byte-identical to native Vulkan. `docs/webgpu-renderer.md` states it is *"one renderer identity with two backends, not two renderers"*, which is §4's own Dawn ruling applied to the same question. Nothing to add. | — | — | — |
| C5 | `DCOMP` | DirectComposition / Windows.UI.Composition | Composition | Windows' visual-layer compositor — the desktop peer of `HTML_DOM`, retained-mode rather than immediate. | Windows | M | high |

### Tier D — retro / exotic

| # | Identity | Upstream | Class | Note | Effort | Risk |
|---:|---|---|---|---|---|---|
| D1 | `GX` | devkitPro libogc (GameCube/Wii) | Fixed-function 3D | A real, still-maintained open homebrew toolchain; TEV stage model is a genuine XNA-mapping challenge. | XL | high |
| D2 | `DREAMCAST_PVR` | KallistiOS PowerVR | Tile-based 3D | Tile-deferred rendering — a pipeline shape CNA has never targeted. | XL | high |
| D3 | `PSP_GU` | PSPSDK `sceGu` | Fixed-function 3D | Handheld fixed-function with hard VRAM budgets. | XL | high |
| D4 | `VGA_DOS` | DJGPP + VGA 13h / VESA VBE | 2D presentation | Real-mode-era direct framebuffer writes. It was pitched as the floor below `DIRECTX1`, an identity retired on 2026-09-17 along with the whole legacy DirectX series. | L | high |
| D5 | `N64_RDP` | libdragon | Fixed-function 3D | RDP combiner semantics; the most alien fixed-function model of the four. | XL | high |

Tier D entries are cheap in *concept* and expensive in *toolchain*, and the argument that once
supported them no longer holds. They were listed because the repository then carried a legacy
renderer series (`DIRECTX1`…`DIRECTX10`, `GLIDE`) that treated historical accuracy as a goal in
itself. **That entire series was retired on 2026-09-17**, so a Tier D proposal can no longer point
at a precedent inside CNA — it has to make the case from nothing. The retired renderers' findings
remain in their historical plans and `docs/removed-renderers.md`; the probe sources are available
through Git history rather than retained under `spikes/`.

## 4. Explicitly **not** new identities

These are frequently mistaken for renderers. Under `docs/renderer-registry.md`'s rules they are
implementation choices, profiles, or sinks — adding them as identities would inflate the count
dishonestly. Recorded here so the question does not have to be re-litigated.

| Thing | Correct treatment |
|---|---|
| ANGLE | A GLES **driver**; `OPENGLES2`/`OPENGLES3` already run on it. At most a documented runtime option. |
| llvmpipe / lavapipe | Software **drivers** behind `OPENGL*`/`VULKAN`. Note `SWIFTSHADER` (A3) is proposed as an identity only because it is a distinct, separately-selected stack — if that argument fails at spike time, it becomes an option too. |
| MoltenVK | A Vulkan implementation on Metal; `VULKAN` on macOS, not a new name. |
| Zink | GL-on-Vulkan driver. |
| Dawn | A second native WebGPU implementation → a **profile** of `WEBGPU`, like the GL profiles. |
| Skia Ganesh / Graphite | GPU backends of Skia, whose CNA identity was retired in 2026-08. Were Skia ever revisited, they would be backends of one identity, not two. |
| EGL/GBM headless GL | A context-creation mode of the GL identities. |
| OffscreenCanvas / worker canvas | A presentation mode of `CANVAS`. |
| Canvas `ImageData` CPU path | A `SOFTWARE`-style sink for `CANVAS`, not an identity. |
| PNG/PPM frame dump | A **present sink** any renderer can gain; not a renderer. |
| SDL 1.2 | Superseded; CNA targets SDL3. |
| `DIRECTX4` | Never shipped publicly. Moot since 2026-09-17: the legacy series was retired and CNA's Direct3D identities are now `DIRECTX9`, `DIRECTX11` and `DIRECTX12` only. |
| Terminal ASCII/glyph output | Already solved renderer-neutrally by `AsciiPostProcessEffect`. Only true-pixel terminal transports (A10) are identity-worthy. |

## 5. Requirements any candidate must satisfy before it counts

Unchanged from `FUTURE.md` §"Requirements for every new renderer", repeated because they are the
admission test, not paperwork:

1. Start from the current modular `develop`; implement against the modular renderer system from
   inception (`modules/renderers/<family>/`), never retrofitted.
2. Stay renderer-local; touch `IGraphicsRenderer` only where a common change is genuinely required
   **and** re-verified across the established renderers.
3. Truthful `GraphicsCapability` reporting — no capability claimed that is not implemented.
4. Deterministic rejection on unsupported paths; **no silent fallback** to another CNA renderer.
5. Permanent tests, registered in the module's own `tests/`.
6. Public CNA identity kept distinct from the internal native/RHI API it happens to use.
7. Its own `docs/<name>-renderer.md` capability boundary and `plan_<name>.md`.
8. Registered in **both** registries so `scripts/check_renderer_identities.py` recounts cleanly.

Recommended additional gate for everything in §3: an **existence-gate spike** first, in a repo-root
`spikes/<name>-spike/` directory (per `CLAUDE.md`), proving the underlying API can clear a screen
and draw one textured quad before any renderer code is written. The `spikes/dx9-spike/` precedent
applies while the renderer remains in the supported or candidate set.

## 6. Sequencing — withdrawn

This section previously recommended a first wave (A2 `TINYGL`, then A1 `OSMESA`, A5 `XLIB`,
A6 `NANOVG`, A10 `SIXEL`). Two of those five were built and then retired within five weeks, so the
recommendation is **withdrawn** rather than reordered: a ranking of candidates implies a queue, and
there is no queue.

What survives from it is one observation worth keeping, because it is about CNA rather than about
any candidate: the entries that would most help are the ones that improve testing of renderers CNA
**already has** — a GPU-less path for the existing GL identities (A1 `OSMESA`) or for `VULKAN`
(A3 `SWIFTSHADER`) — and those may well be better as build options behind an existing identity than
as identities of their own (§4). `A9 OPENGLES32` remains the only candidate that could truthfully
report a capability no current identity reports (compute, tessellation, geometry). None of this is
authorization.

## 7. Arithmetic

The earlier edition of this section added the live identities, the Phase 2 list and this catalog
into a "97 theoretical ceiling". **That arithmetic is deleted, not updated.** Summing a support
matrix with a research list produces a number that looks like an ambition, and CNA has since
established the opposite policy: the set is curated, and on 2026-09-17 it went *down* by 25.

The only renderer count that may be published anywhere is the one
`scripts/check_renderer_identities.py` prints for the actual tree — today, **25 public identities
over 21 implementation families**. Twenty-six identities are retired, their ABI values permanently
reserved (`docs/removed-renderers.md`).

For the record of what this catalog has actually produced since 2026-08-13: three candidates built
(`TINYGL`, `NANOVG`, `RLGL`), three retired, net zero.
