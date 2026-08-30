# CNA renderer expansion candidates

Date: 2026-08-13

> **THIS DOCUMENT IS A CANDIDATE CATALOG, NOT AUTHORIZATION TO START WORK.**
>
> Nothing listed here may be implemented because it appears in this file. Exactly like
> `FUTURE.md`, every identity below requires a **fresh explicit owner instruction**, its own plan
> file, and its own acceptance criteria before a single line is written. The catalog exists so that
> when the owner does authorize the next renderer, the option space is already surveyed, deduplicated
> against the live registry, and screened against the project's own "no alias identities" rule.

Authoritative companions:

- `docs/renderer-registry.md` — the **live** public identities (canonical; this file never
  overrides it).
- `FUTURE.md` §"Phase 2 — renderer expansion" — the already-planned, owner-visible additions.
- `docs/graphics-renderer-feature-matrix.md` — what the established renderers actually prove.

---

## 1. What CNA supports today

**41 public renderer identities**, mechanically verified by `scripts/check_renderer_identities.py`
against `modules/core/include/CNA/GraphicsRendererType.hpp` and `cmake/RendererSelection.cmake`
(`OK: 41 public renderer identities preserved in both registries`). Selected at configure time via
`-DCNA_GRAPHICS_RENDERER=<selector>`; implementations live in `modules/renderers/<family>/`.

The 48 (this table's own count, pre-existing drift from the registry's true 50 -- IGL/PIXIJS are
also live but not yet reflected below) map to concrete factories, because the five GL profiles
(`OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`) share the internal EasyGL implementation
while keeping distinct public contracts (context, shader profile, platform).

| Class | Identities | Count |
|---|---|---:|
| No pixels (validation/no-op) | `HEADLESS`, `STUB` | 2 |
| 2D-oriented | `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `SKIA`, `BLEND2D`, `OPENVG`, `NANOVG`, `FREEDIRECT`, `DIRECTX1`, `DIRECT2D`, `GDI` | 12 |
| CPU 3D | `SOFTWARE`, `PORTABLEGL`, `TINYGL` | 3 |
| Legacy / fixed-function 3D | `OPENGLES1`, `OPENGL1`, `DIRECTX2`, `DIRECTX3`, `DIRECTX5`, `DIRECTX6`, `DIRECTX7`, `DIRECTX8`, `GLIDE` | 9 |
| Programmable / modern | `OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`, `OPENGL2`, `OPENGL4`, `VULKAN`, `WEBGPU`, `METAL`, `DIRECTX9`, `DIRECTX10`, `DIRECTX11`, `DIRECTX12`, `SDL_GPU` | 15 |
| Abstraction / engine RHI | `BGFX`, `MAGNUM`, `WICKED`, `SOKOL`, `DILIGENT`, `LLGL`, `FNA3D` | 7 |
| **Total** | | **48** |

Notes that must not be misstated anywhere: `WEBGPU`, `SOKOL`, `DILIGENT`, `LLGL` and `WICKED` are
experimental with bounded verified surfaces; `SKIA` is CPU-raster 2D only; the `ASCII` identity was
**removed** in favour of the renderer-neutral `CNA::Graphics::AsciiPostProcessEffect`.

## 2. Already planned, not yet started (from `FUTURE.md` Phase 2)

Eight identities are already on the roadmap and are **not** re-proposed here. They keep priority
over anything in §3 unless the owner says otherwise.

| Identity | Upstream | Class |
|---|---|---|
| `IGL` | facebook/igl | Abstraction RHI |
| `NVRHI` | NVIDIA NVRHI | Abstraction RHI |
| `KORE` | Kode/Kore (Kinc lineage) | Abstraction RHI |
| `METHANEKIT` | MethaneKit | Abstraction RHI |
| `LINAGX` | LinaGX | Abstraction RHI |
| `TEMPEST` | Tempest | Abstraction RHI |
| `THORVG` | ThorVG | 2D vector |
| `REACT_DOM` | React/DOM | Web DOM — only if it can truthfully satisfy a graphics contract |

Live 48 (this document's own table above) + these 8 = the current **56** roadmap ceiling per this
document's own count.

## 3. New candidates

**41 candidate identities** as first catalogued, none of which duplicated a live identity or a §2
entry. Two — **A2 `TINYGL`** and **A6 `NANOVG`** — have since been implemented and are now live
identities; both are left in place below, marked DELIVERED, so the catalog stays readable as a
record rather than silently shrinking. **39 remain open.** Each row states
the one thing it proves that no existing CNA identity proves — that column is the admission test,
because `docs/renderer-registry.md` forbids counting a conceptual alias of an existing identity.

Effort: **S** ≈ one focused lane, **M** ≈ a plan file of ~30–60 tasks, **L** ≈ comparable to the
Vulkan/Skia campaigns, **XL** ≈ needs hardware or a toolchain CNA does not have.
Licenses are as commonly reported upstream and **must be re-verified at spike time** — several are
copyleft or version-dependent and could be disqualifying.

### Tier A — first wave (Linux dev-loop buildable, CI-friendly, low dependency risk)

| # | Identity | Upstream / API | Class | Proves what nothing else proves | Gate | Effort | Risk | License |
|---:|---|---|---|---|---|---|---|---|
| A1 | `OSMESA` | Mesa `libOSMesa` | CPU GL 4.x | Real, spec-complete desktop GL executed with **zero GPU and zero display server** — the only way to run the full EasyGL/`OPENGL4` feature matrix in CI. `PORTABLEGL` is a partial GL 3.x reimplementation, not Mesa. | none (CPU) | S | low | MIT |
| A2 | `TINYGL` | C-Chads/tinygl (Bellard lineage) | CPU fixed-function | **DELIVERED 2026-08-13.** Fixed-function GL 1.x **on CPU**. `OPENGL1` needs a driver; `PORTABLEGL` is shader-era; `SOFTWARE` is CNA's own rasterizer. See `tinygl-renderer.md` / `../plans/plan_tinygl.md`. | none | S | low | zlib-style, acknowledgment required |
| A3 | `SWIFTSHADER` | google/swiftshader | CPU Vulkan | A conformant **Vulkan** API surface with no GPU — lets the `VULKAN` renderer's contract be regression-tested on GPU-less machines without claiming it as `VULKAN`. | none | M | med | Apache-2.0 |
| A4 | `CAIRO` | cairo | 2D vector | A third, independent 2D vector model next to `SKIA`/`BLEND2D` — and the only one with a *device-agnostic* backend set (image, X11, PDF, SVG) behind one API. | system pkg | M | med | LGPL-2.1 / MPL-1.1 — **verify** |
| A5 | `XLIB` | X11 `XImage`/MIT-SHM | 2D presentation | The Linux peer of `GDI`: CPU pixels pushed straight to a bare X server, no SDL, no GL, no toolkit. | Linux + X11 | S | low | MIT |
| A6 | `NANOVG` | memononen/nanovg | 2D vector on GL | **DELIVERED 2026-08-19.** Vector-first 2D on a real GPU context, driven through NanoVG's own compiled GLSL shader pipeline (GL2 backend) rather than fixed-function GL (`OPENVG`) or CPU raster (`SKIA`/`BLEND2D`). Genuinely supports `BlendState.Additive`, unlike `OPENVG`. See `nanovg-renderer.md` / `../plans/plan_nanovg.md`. | GL context | S | low | zlib |
| A7 | `FBDEV` | Linux `/dev/fb0` | 2D presentation | Rendering with no window system **and no GPU driver stack** at all — the minimum viable embedded/console target. | Linux | S | low | n/a (kernel ABI) |
| A8 | `DRM_KMS` | DRM/KMS dumb buffers | 2D presentation | Direct modeset + scanout ownership (kiosk/appliance), incl. real vsync/page-flip semantics `FBDEV` cannot express. | Linux + DRM | M | med | n/a (kernel ABI) |
| A9 | `OPENGLES32` | OpenGL ES 3.2 | Programmable | The GL family's 6th profile: **compute shaders, tessellation, geometry stage** — capabilities no current GL profile may truthfully report. | ES 3.2 driver | M | low | n/a |
| A10 | `SIXEL` | libsixel / DEC SIXEL | 2D presentation | **True pixels in a terminal.** Distinct from the removed `ASCII` identity in kind, not degree: that was glyph quantization (now a post-process effect), this is a real framebuffer transport. | sixel-capable TTY | S | low | MIT |

Tier A is deliberately the cheapest way to widen coverage: eight of the ten need no GPU, so they
also strengthen CI.

### Tier B — established libraries and engines (medium effort, dev-loop buildable)

| # | Identity | Upstream | Class | Proves what nothing else proves | Effort | Risk | License |
|---:|---|---|---|---|---|---|---|
| B1 | `RAYLIB` | raysan5/raylib (rlgl) | 2D + basic 3D | The most widely used "simple game library" as a CNA host; rlgl's batching model differs from every current path. | S | low | zlib |
| B2 | `SFML` | SFML `Graphics` | 2D | Classic RAII C++ 2D API as a renderer host; view/transform model unlike `SDL_RENDERER`. | S | low | zlib |
| B3 | `ALLEGRO` | Allegro 5 | 2D | Another mature 2D game library with its own bitmap/target model. | S | low | zlib-like |
| B4 | `OGRE` | Ogre-Next / Ogre 14 | Engine RenderSystem | A **scene-graph engine's** RenderSystem driven by an immediate XNA API — the hardest structural mismatch to prove, and the most valuable if it works. | L | high | MIT |
| B5 | `IRRLICHT` | Irrlicht | Engine, multi-driver | An engine that ships **its own software rasterizers alongside GL/D3D** — a single identity spanning both worlds. | M | med | zlib |
| B6 | `FILAMENT` | google/filament | PBR renderer | A physically-based renderer forced to reproduce XNA's fixed stock-effect lighting exactly; a strong fidelity probe. | L | high | Apache-2.0 |
| B7 | `THEFORGE` | ConfettiFX/The-Forge | RHI | A production cross-platform RHI with an explicit-descriptor model unlike bgfx/Diligent/LLGL. | L | med | Apache-2.0 |
| B8 | `NRI` | NVIDIA NRI | RHI | Thin, low-abstraction RHI — the opposite design point from `NVRHI` (§2), so both are defensible. | M | med | MIT |
| B9 | `HORDE3D` | Horde3D | Engine | Minimal forward/deferred engine; small enough to fully verify. | M | med | EPL |
| B10 | `RBFX` | rbfx (Urho3D lineage) | Engine | A living Urho3D descendant with its own RHI; different again from B4/B5. | L | med | MIT |
| B11 | `BSF` | bs::framework | Engine RHI | RenderBeast/ct::RenderAPI — modern C++ engine RHI. | L | high | MIT |
| B12 | `FALCOR` | NVIDIA Falcor | Research renderer | Render-graph-first architecture driven by immediate-mode XNA calls. | L | high | BSD-3 |
| B13 | `QPAINTER` | Qt `QPainter` | 2D | The only candidate whose 2D output can target widgets, images, printers and PDF through one painter API. | M | med | LGPL-3 / commercial — **verify** |
| B14 | `AGG` | Anti-Grain Geometry | CPU 2D | Scanline AA rasterization with sub-pixel accuracy semantics distinct from Skia/Blend2D. | M | med | **version-dependent (2.4 permissive, 2.5+ GPL) — verify** |
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
| C4 | `WEBGPU_WEB` | Browser WebGPU (Emscripten) | Programmable | Real **browser** WebGPU. `WEBGPU` today is native wgpu-native; the split mirrors `OPENGLES3` vs `WEBGL2` exactly and is therefore identity-worthy, not an alias. | Emscripten | M | med |
| C5 | `DCOMP` | DirectComposition / Windows.UI.Composition | Composition | Windows' visual-layer compositor — the desktop peer of `HTML_DOM`, retained-mode rather than immediate. | Windows | M | high |

### Tier D — retro / exotic (matches the existing `DIRECTX1`…`GLIDE` appetite)

| # | Identity | Upstream | Class | Note | Effort | Risk |
|---:|---|---|---|---|---|---|
| D1 | `GX` | devkitPro libogc (GameCube/Wii) | Fixed-function 3D | A real, still-maintained open homebrew toolchain; TEV stage model is a genuine XNA-mapping challenge. | XL | high |
| D2 | `DREAMCAST_PVR` | KallistiOS PowerVR | Tile-based 3D | Tile-deferred rendering — a pipeline shape CNA has never targeted. | XL | high |
| D3 | `PSP_GU` | PSPSDK `sceGu` | Fixed-function 3D | Handheld fixed-function with hard VRAM budgets. | XL | high |
| D4 | `VGA_DOS` | DJGPP + VGA 13h / VESA VBE | 2D presentation | The logical floor below `DIRECTX1`: real-mode-era direct framebuffer writes. | L | high |
| D5 | `N64_RDP` | libdragon | Fixed-function 3D | RDP combiner semantics; the most alien fixed-function model of the four. | XL | high |

Tier D entries are cheap in *concept* and expensive in *toolchain*. They are listed because the
repository already treats historical accuracy as a first-class goal (`dx1-spike/` … `dx10-spike/`,
`GLIDE`), not because they are recommended next.

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
| Skia Ganesh / Graphite | GPU backends of the existing `SKIA` identity (already a build flag). |
| EGL/GBM headless GL | A context-creation mode of the GL identities. |
| OffscreenCanvas / worker canvas | A presentation mode of `CANVAS`. |
| Canvas `ImageData` CPU path | A `SOFTWARE`-style sink for `CANVAS`, not an identity. |
| PNG/PPM frame dump | A **present sink** any renderer can gain; not a renderer. |
| SDL 1.2 | Superseded; CNA targets SDL3. |
| `DIRECTX4` | Never shipped publicly. The `DIRECTX1..12` sequence's gap is correct as-is. |
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
`<name>-spike/` directory (per `CLAUDE.md`), proving the underlying API can clear a screen and draw
one textured quad before any renderer code is written. The `dx9-spike/` precedent applies verbatim.

## 6. Suggested sequencing (if and when authorized)

A2 `TINYGL` was delivered first, on 2026-08-13. The rest of a defensible first wave is
**A1 `OSMESA`, A5 `XLIB`, A6 `NANOVG`, A10 `SIXEL`** — all
Tier S/low-risk, all Linux-dev-loop verifiable without a GPU, and three of them directly improve CI
coverage of *existing* renderers. `A9 OPENGLES32` is the highest-value non-trivial one, because it is
the first CNA identity that could truthfully report compute-shader capability.

## 7. Arithmetic

    50 live today (46 at first writing, + TINYGL, + IGL, + PIXIJS, + NANOVG)
     + 8 planned but unstarted (FUTURE.md Phase 2)               = 58
     + 39 candidates still open here                             = 97 theoretical ceiling

**97 is not a target and not a plan.** It is the size of the surveyed option space. The only number
that may ever be published as CNA's renderer count is the one
`scripts/check_renderer_identities.py` prints for the actual tree — today, **50**.
