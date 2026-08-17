#pragma once

namespace CNA
{
    /**
     * @brief Identifies a graphics feature whose support genuinely varies across CNA renderers
     *        (and, for some entries, across devices/drivers within the same renderer).
     *
     * Query via GraphicsDevice::SupportsCapability()/IGraphicsRenderer::SupportsCapability()
     * before relying on the corresponding feature, instead of calling it and handling the
     * resulting exception. Every entry here maps to a real, already-documented gap somewhere in
     * this codebase (see each value's own comment) -- this is not a speculative capability list.
     */
    enum class GraphicsCapability
    {
        /**
         * @brief The 3D pipeline as a whole (vertex/index buffers, 3D draw calls, depth/stencil
         * clears and state). Several renderers, including the native 2D renderer, Canvas, GDI, and the
         * Skia raster renderer, are intentionally 2D-only and lack this entirely. Query the selected
         * renderer rather than inferring support from its name. GDI's separate 2D stencil-mask
         * extension does not imply a 3D pipeline.
         */
        ThreeD,

        /** @brief A complete real depth/stencil attachment on the active target. */
        DepthStencilBuffer,

        /** @brief Multi-sample anti-aliasing (any sample count above 1). */
        MultiSampleAntiAliasing,

        /** @brief More than one simultaneous render target (MRT). */
        MultipleRenderTargets,

        /** @brief Anisotropic texture filtering (device/driver-dependent on some renderers). */
        AnisotropicFiltering,

        /** @brief RasterizerState.FillMode = FillMode::WireFrame. */
        WireFrame,

        /** @brief Real GPU occlusion queries (OcclusionQuery.Begin/End/PixelCount). */
        OcclusionQuery,

        /** @brief A custom (non-stock) Effect passed to SpriteBatch.Begin(). */
        CustomEffects,

        /**
         * @brief Real volume (3D) texture storage -- Texture3D::SetData()/GetData() actually
         * persist and retrieve pixel data, not just validate arguments. This capability describes
         * storage only and never promises shader sampling: Skia reports it true for bounded CPU
         * transfer/readback storage while keeping its 3D and custom-effect capabilities false, even
         * though Skia separately offers a narrow, opt-in shader-sampling extension
         * (`cnaSampleCubeEXT`/`cnaSampleVolumeEXT`, `docs/skia-cube-volume-sampling-contract.md`)
         * that this flag does not represent and that does not imply general/stock 3D or effect
         * support. Headless has no real GPU resource of any kind by design; Software's Texture3D
         * support is an explicit, documented v1 scope boundary (`plan_software.md` Boundaries) --
         * both currently leave `IGraphicsRenderer::CreateTexture3D()` at its shared default (returns
         * `nullptr`), which previously let `Texture3D::SetData()`/`GetData()` silently no-op
         * instead of failing cleanly (REMED-CONTENT-004).
         */
        Texture3D,

        /**
         * @brief More than one `VertexBufferBinding` of the same input rate, on any draw route --
         * a `VertexDeclaration` whose elements are split across several bound buffers
         * (REMED-GFX-201), or several per-instance streams at their own frequencies
         * (REMED-GFX-202).
         *
         * XNA 4.0 allows both on every draw route, and CNA's shared layer carries the complete
         * binding set to the renderer boundary as `GpuDrawParams::vertexStreams` -- identically for
         * `DrawPrimitives`, `DrawIndexedPrimitives` and `DrawInstancedPrimitives`, exactly as FNA's
         * one `PrepareVertexBindingArray` does. Whether a renderer can then express the combination
         * natively is a real per-renderer gap: CNA's renderers derive their native input elements
         * from a single byte stride and bind exactly one per-instance buffer, so a renderer that has
         * not yet been taught to re-slot those elements across several bindings would silently
         * render from a subset of the bound streams. Such a renderer reports false here and
         * `GraphicsDevice` rejects the draw before native submission instead.
         *
         * The classic shapes need no capability at all and are unaffected: one per-vertex stream,
         * and one per-vertex stream plus one per-instance stream.
         *
         * The default is false: a newly added renderer must make an explicit decision to claim
         * this, exactly like `IVertexBufferRenderer::SetVertexDeclaration` being a required
         * override.
         */
        MultiStreamVertexInput,

        /** @brief Hardware instancing (GraphicsDevice.DrawInstancedPrimitives). Device/driver-
         *  dependent on renderers that implement it via an optional GL/Vulkan extension rather
         *  than an unconditional core feature. */
        Instancing,

        /**
         * @brief A real stencil plane usable independently of depth.
         *
         * This is separate from DepthStencilBuffer so a 2D renderer such as GDI can advertise its
         * CPU stencil-mask extension without falsely claiming a depth attachment or 3D pipeline.
         * Appended to preserve the numeric values of the existing capability entries.
         */
        StencilBuffer,

        /**
         * @brief `BlendState.Additive` uses the renderer's documented additive colour path, rather
         * than silently degrading to normal alpha blending. This flag does not widen that
         * renderer's documented alpha-channel contract or imply arbitrary custom BlendState
         * support. HTML_DOM support is browser-version-dependent
         * (plan_html_dom.md HTMLDOM-117):
         * on an engine without `plus-lighter`, the CSS value is simply ignored before any CNA code
         * can observe it, and `Additive` silently renders as ordinary source-over blending instead
         * -- no exception, a different visual result. Query this before relying on genuine additive
         * compositing. Each renderer reports the fidelity of its CNA implementation, not merely
         * whether its underlying graphics API could theoretically express additive blending.
         */
        AdditiveBlending,

        /**
         * @brief XNA/FNA Direct3D 9 Effect Framework bytecode, including reflected parameters,
         * techniques, passes, shaders, samplers, and pass state.
         *
         * This is intentionally separate from CustomEffects, which describes CNAEXT
         * ShaderEffect's caller-supplied source-pair contract. A renderer may support either
         * format independently. Appended to preserve every existing numeric capability value.
         */
        CompiledEffects,

        /**
         * @brief 32-bit-per-channel floating-point colour render targets -- a `RenderTarget2D` (or
         * `RenderTargetCube`) created with `SurfaceFormat::Single`, `Vector2` or `Vector4` really
         * stores unclamped float values, instead of the 8-bit `Color` target every renderer creates
         * today regardless of the requested format.
         *
         * This is the foundation of the CNAEXT engine layer's HDR pipeline (`plan_modern.md`
         * Phase 1): without it a scene rendered to an off-screen target is clamped to [0,1] before
         * tonemapping ever runs, which defeats the purpose of tonemapping. It is reported separately
         * from `HalfFloatRenderTargets` because 16-bit float targets are far more widely available
         * than 32-bit ones -- notably on GLES 3.0 devices, where `GL_EXT_color_buffer_half_float`
         * is common and full float colour buffers are not.
         *
         * A renderer reports true only when it actually creates the requested float format through
         * `CreateRenderTarget2DEXT()`; the shared default of that factory ignores the format and
         * produces a `Color` target, so a renderer that has not been taught float formats must
         * leave this false rather than let a caller believe values above 1.0 survive.
         *
         * @note This entry and `HalfFloatRenderTargets` are **derived**: a renderer opts in by
         * reporting the individual formats it can really create (`plan_modern.md` MOD-104's
         * `IGraphicsRenderer::SupportsRenderTargetFormat()`), not by adding a case to its own
         * `SupportsCapability()` override. Many renderer overrides end in `default: return true`,
         * so answering a brand-new capability there is opt-out rather than opt-in -- the wrong
         * direction for a promise this specific.
         */
        FloatRenderTargets,

        /**
         * @brief 16-bit-per-channel (half) floating-point colour render targets --
         * `SurfaceFormat::HalfSingle`, `HalfVector2`, `HalfVector4` and `HdrBlendable`.
         *
         * The practical HDR format: half the bandwidth and memory of a 32-bit float target, enough
         * range and precision for scene-referred colour, and blendable on far more hardware. The
         * CNAEXT engine layer's HDR scene target prefers `HdrBlendable` (an alias of `HalfVector4`
         * in CNA) and falls back to `Color` with a one-time log when neither float capability is
         * present. See `FloatRenderTargets` for why the two are separate entries.
         */
        HalfFloatRenderTargets,

        /**
         * @brief Linear (and mip) filtering when *sampling* a half-float colour texture, as opposed
         * to merely rendering into one.
         *
         * The two are separate hardware features and separate GL extensions: a context can render
         * to `RGBA16F` and still only sample it with `NEAREST`. Bloom is where that bites -- its
         * down/upsample chain is built on hardware-filtered half-resolution reads, and without
         * linear filtering it must fall back to more taps at more cost for a worse result. The
         * `CNA::Graphics` passes query this and document which fallback they take.
         *
         * Reported for the half-float formats (`HalfSingle`/`HalfVector2`/`HalfVector4`/
         * `HdrBlendable`), which are what the engine layer's targets use; 32-bit float filtering is
         * rarer still and no CNA pass depends on it.
         */
        HalfFloatTextureLinearFiltering
    };
} // CNA
