# Skia surface-mode decision

Status: accepted for the CPU-raster release (SKIA-5/6, SKIA-76–79)

## Decision

`CNA_GRAPHICS_BACKEND=SKIA` has one supported execution mode in this release: an RGBA8888 CPU
`SkSurface` presented through a backend-owned SDL renderer and streaming texture. It does not
attempt a Skia GPU context and therefore has no automatic GPU-to-raster fallback. Construction is
observable as `surface=raster`; a missing pinned artifact or failed presenter is a hard error.

If a later release funds an accelerated mode, Ganesh/OpenGL is the first integration candidate on
SDL/OpenGL platforms. It is a future candidate, not a current capability. Adding it requires a
successor plan and must reopen the surface, reset, parity, sanitizer, MSAA, and anisotropy gates.
No accelerated code or archive is linked by the current selection.

## Evaluated routes

| Route | Pinned-revision building blocks | Ownership/integration cost | Disposition |
|---|---|---|---|
| Raster `SkSurface` + SDL upload | Existing `SkSurfaces::Raster`, exact CPU readback, SDL streaming texture | Skia owns CPU pixels; the backend owns SDL renderer/texture; the caller retains the SDL window. No GPU context crosses the boundary. | Selected and fully tested |
| Ganesh/OpenGL default framebuffer | `GrDirectContexts::MakeGL`, `GrGLFramebufferInfo`, `GrBackendRenderTarget`, `SkSurfaces::WrapBackendRenderTarget`, `FlushAndSubmit` | Requires a separately pinned Ganesh/GL artifact, backend-owned `SDL_GLContext`, strict current-thread/context rules, top/bottom-origin handling, flush/swap/readback, resize rewrap, and context-loss recovery. | First future accelerated candidate; not enabled |
| Ganesh/Vulkan | Ganesh Vulkan direct context and backend render-target APIs | CNA/SDL and Skia would need one explicitly shared instance/device/queue, image ownership, layouts, synchronization, swapchain recreation, and readback policy. | Rejected as the first spike; materially broader than GL wrapping |
| Graphite Vulkan/Dawn/Metal | Graphite context/recorder and backend-texture surface APIs | Requires platform-specific device/queue ownership plus recorder submission and texture/swapchain synchronization. The pinned artifact disables all of these APIs. | Deferred; no advantage established for the first integration |

The comparison uses headers from pinned Skia revision
`ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`, not an assumed system Skia API. The validated CNA
adapter remains GNU/Clang ELF raster-only; this ADR does not claim a new supported OS or binary
package.

## Why raster is the release mode

- Every supported 2D contract already executes through the same `SkCanvas`/resource path and has
  exact readback evidence, including real-XNA oracles.
- The raster surface survives SDL presenter reconstruction without invalidating textures, targets,
  or snapshots; the 64-cycle sanitizer gate directly checks that ownership.
- A GPU path would add a second artifact ABI, context/device ownership model, sample/filter
  capability set, loss/recovery behavior, and pixel-variance policy. Shipping it without the
  required comparison suite would weaken rather than extend the verified backend.
- SDL may accelerate the final upload internally, but it receives a completed CPU image. That is
  presentation implementation, not a Ganesh/Graphite execution mode.

SKIA-6 is therefore conditionally not applicable to this release: SKIA-5 did not select an
accelerated mode for implementation. A future Ganesh/OpenGL task must still prove default-
framebuffer wrapping, clear, flush/submit, swap, readback, resize, and context loss before it may be
advertised. The current `Accelerated` CTest label intentionally contains zero tests.

## Selected raster MSAA policy

The selected raster surface has zero physical samples and reports
`GraphicsCapability::MultiSampleAntiAliasing=false`.

- Backbuffer requests 0, 1, 2, 4, and an oversized 4096 are accepted by `GraphicsDevice::Reset`
  and written back as the actual value 0.
- `RenderTarget2D` requests 0/1 apply 0. Requests 2, normalized 3, 4, and 4096 reject before
  allocation; no fake resolve is exposed.
- `RenderTargetCube` reports the applied value 0. No multisample mask is interpreted on a
  zero-sample surface.

`Skia_RenderTarget2D_MsaaPolicy` verifies the complete backbuffer/target matrix and proves the
backbuffer remains usable afterward. A future GPU mode must query its own supported sample counts,
test resolve/readback, and report a mode-specific capability; it may not inherit this zero-sample
result blindly.

## Selected raster anisotropy policy

The pinned raster route has no device anisotropy query or anisotropic footprint control and reports
`GraphicsCapability::AnisotropicFiltering=false`. For SpriteBatch's complete Texture2D chain,
`TextureFilter::Anisotropic` uses the documented `Linear` fallback, including Linear's mip
interpolation. `MaxAnisotropy` therefore does not change pixels. SKIA-129 implements all other
min/mag/mip ordinal combinations in bounded raster code; all 3D stock-effect sampling still
rejects at the common 3D boundary.

`Skia_Sampler_MipmapFilterPolicy` proves byte-identical complete-Linear output for Anisotropic,
including fractional mip interpolation, while the capability remains false. This distinguishes a
deliberate fallback from a claimed/clamped hardware feature.
A future GPU mode must probe its native maximum and replace this result only after a real
minification/LOD fixture passes.

## Reopening requirements

An accelerated successor must, before changing the default or any capability:

1. add a separately named pinned GN artifact and explicit construction-time mode selector;
2. make context/device/thread ownership and destruction order testable;
3. wrap and present the real backbuffer, including resize and loss/recovery;
4. run the current 2D XNA oracle and API-contract corpus in both modes;
5. add accelerated ASan/UBSan/lifetime coverage where the platform permits it; and
6. probe MSAA and anisotropy on the selected native API rather than copying raster policy.

Until all six gates pass, the release claim remains CPU-raster 2D only.

Gate 1 is now fully closed: SKIA-159 (`docs/skia-ganesh-artifact.md`) produced a separately pinned
Ganesh/OpenGL GN artifact and a `CNA::SkiaGanesh` CMake target, functionally verified below the API
(a real `GrDirectContexts::MakeGL()` context over a real SDL GL context); SKIA-160 added the
explicit construction-time mode selector (`CNA_SKIA_MODE`, `SkiaGaneshContext`) on top of it, with a
mode-specific diagnostic and no silent runtime fallback in either direction.

SKIA-161 makes real, but partial, progress on two further gates without closing either: gate 3
("wrap and present the real backbuffer, including resize and loss/recovery") now has a real,
pixel-proven default-framebuffer wrap, flush/submit, swap, readback, and caller-invoked resize
(`SkiaGaneshSurface`) -- but no loss/recovery, which remains SKIA-162's job. Gate 5 ("add
accelerated ASan/UBSan/lifetime coverage where the platform permits it") now has a real, permanent
sanitizer build (`cmake-build-skia-ganesh-asan`) exercising this surface-wrapping code -- but not
the full 2D XNA oracle/API-contract corpus gate 4 requires, since none of that corpus can run
through Ganesh yet (no `IGraphicsBackend` wraps it).

Gates 2, 4, and 6 remain fully untouched. `CNA_GRAPHICS_BACKEND=SKIA`'s default `RASTER` mode is
completely unaffected throughout; no `IGraphicsBackend` wraps the Ganesh artifact yet; and this note
does not itself change the release claim above.
