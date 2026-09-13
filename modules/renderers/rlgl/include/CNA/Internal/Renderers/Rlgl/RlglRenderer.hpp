// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class TextureCollection;
}

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglCompiledEffect;
    class RlglResourceLifetime;
    class RlglThreadContextLeaseControl;

    namespace Detail
    {
        class RlglSpriteBatchRenderer;
    }

    namespace Bridge
    {
        struct CompiledEffectDrawResources;
        struct PrimitivePipeline;
        struct RenderTargetStorage;
    }

    /** @brief Observable stages of the RLGL native-context recovery transaction. */
    enum class RlglContextRecoveryState
    {
        /** @brief The renderer owns a complete native context and accepts draw work. */
        Available,
        /** @brief Native identities were invalidated after a reported context loss. */
        Lost,
        /** @brief A replacement context and renderer state are being constructed. */
        Recreating,
        /** @brief The latest recreation attempt failed and drawing remains closed. */
        Unavailable
    };

    /** @brief Context-recovery facts exposed to focused RLGL validation. */
    struct RlglContextRecoverySnapshot
    {
        /** @brief Current availability stage. */
        RlglContextRecoveryState state = RlglContextRecoveryState::Available;
        /** @brief Number of successfully initialized native contexts, including the first. */
        std::size_t contextGeneration = 0;
        /** @brief Whether standalone rlgl is initialized in the current context. */
        bool rlglInitialized = false;
        /** @brief Number of sampler slots whose native objects must survive recreation. */
        std::size_t realizedSamplers = 0;
        /** @brief Number of those sampler slots that currently own replacement objects. */
        std::size_t liveSamplers = 0;
        /** @brief Current native sampler identities by slot. */
        std::array<unsigned int, 16> samplerIds{};
        /** @brief Number of compiled vertex-sampler slots that must survive recreation. */
        std::size_t realizedVertexSamplers = 0;
        /** @brief Number of compiled vertex-sampler slots owning a replacement object. */
        std::size_t liveVertexSamplers = 0;
        /** @brief Current native compiled vertex-sampler identities by logical slot. */
        std::array<unsigned int, 4> vertexSamplerIds{};
        /** @brief Whether the lazy stock primitive pipeline has been realized. */
        bool primitivePipelineRealized = false;
        /** @brief Whether the current context owns the realized stock primitive pipeline. */
        bool primitivePipelineLive = false;
        /** @brief Whether the optional compiled-draw VAO set has been realized. */
        bool compiledDrawResourcesRealized = false;
        /** @brief Whether the optional compiled-draw VAO set is live now. */
        bool compiledDrawResourcesLive = false;
        /** @brief Whether the optional MojoShader GL context has been realized. */
        bool mojoShaderContextRealized = false;
        /** @brief Whether the optional MojoShader GL context is live now. */
        bool mojoShaderContextLive = false;
        /** @brief XNA SurfaceFormat ordinal matching the actual default framebuffer. */
        int appliedBackBufferFormat = 0;
        /** @brief XNA DepthFormat ordinal matching the granted depth/stencil planes. */
        int appliedDepthStencilFormat = 0;
        /** @brief Multisample count applied to the renderer-owned backbuffer. */
        int appliedMultiSampleCount = 0;
        /** @brief Whether the platform granted robust-access, lose-context-on-reset semantics. */
        bool robustContext = false;
        /** @brief Whether a native graphics-reset status entry point is available. */
        bool nativeLossPollingAvailable = false;
        /** @brief Number of ordinary GraphicsDevice presentation Reset calls observed. */
        std::size_t presentationResets = 0;
        /** @brief Number of native losses detected without the explicit debug-loss entry point. */
        std::size_t detectedNativeLosses = 0;
        /** @brief Stable diagnostic from the latest failed recreation attempt. */
        std::string unavailableReason;
    };

    /** @brief Renderer-local work counters used by the RLGL performance campaign. */
    struct RlglPerformanceSnapshot
    {
        /** @brief Native draw submissions issued by the measured renderer paths. */
        std::uint64_t drawCalls = 0;
        /** @brief Complete pipeline or sampler state applications requested. */
        std::uint64_t stateApplications = 0;
        /** @brief Shader-program bind calls issued for measured draws. */
        std::uint64_t programBinds = 0;
        /** @brief Texture-object bind calls issued by measured resource paths. */
        std::uint64_t textureBinds = 0;
        /** @brief Vertex or index buffer uploads issued after the counter reset. */
        std::uint64_t bufferUploads = 0;
        /** @brief Explicit framebuffer bind transitions issued after the counter reset. */
        std::uint64_t framebufferTransitions = 0;
        /** @brief Requests to drain rlgl's immediate-mode batch before CNA-owned work. */
        std::uint64_t flushRequests = 0;
        /** @brief Requested drains that contained vertices and performed a real batch flush. */
        std::uint64_t batchFlushes = 0;
        /** @brief Uniform uploads that survived the stock-pipeline value cache. */
        std::uint64_t uniformUploads = 0;
        /** @brief Native vertex-attribute state changes that survived the VAO cache. */
        std::uint64_t vertexAttributeChanges = 0;
        /** @brief Fixed-capacity attribute scratch lists built for submitted draws. */
        std::uint64_t attributeScratchBuilds = 0;
        /** @brief Heap allocations made by attribute scratch construction. */
        std::uint64_t attributeHeapAllocations = 0;
        /** @brief Vertex semantic searches performed while preparing draw attributes. */
        std::uint64_t semanticLookups = 0;
        /** @brief Public Matrix values converted to native column-major arrays. */
        std::uint64_t matrixConversions = 0;
    };

    /**
     * @brief Standalone-rlgl renderer device using a CNA-owned OpenGL 3.3 core context.
     *
     * This class owns no windowing or raylib framework state. The platform creates and swaps the
     * context, while the renderer initializes exactly one process-global rlgl instance inside it.
     */
    class RlglRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates an RLGL device on the supplied CNA presentation surface.
         *
         * @param args Platform surface, GL service, presentation, and multisample request.
         */
        explicit RlglRenderer(const GraphicsRendererCreateArgs& args);

        /** @brief Shuts down rlgl before releasing the CNA-owned OpenGL context. */
        ~RlglRenderer() override;

        /**
         * @brief Serializes a complete operation while owning this renderer's GL context.
         * @param release Selects whether this renderer's own prior binding is restored or released.
         * @return A token that releases the calling thread's context ownership when destroyed.
         */
        [[nodiscard]] std::unique_ptr<IRendererThreadContextLease>
            AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease release =
                    RendererThreadContextLeaseRelease::RestorePreviousBinding) override;

        /**
         * @brief Returns this device's resource lifetime for focused ownership validation.
         * @return Shared lifetime state that remains observable after device shutdown.
         */
        [[nodiscard]] std::shared_ptr<RlglResourceLifetime>
            GetResourceLifetimeForTesting() const noexcept;

        /**
         * @brief Selects whether subsequently created resources retain context-recovery state.
         * @param enabled True to register future resources for recovery.
         */
        void SetContextRecoveryEnabled(bool enabled) override;

        /**
         * @brief Reports whether the native context transaction currently permits drawing.
         * @return True only while the renderer is fully available.
         */
        [[nodiscard]] bool CanBeginDrawEXT() const override;

        /** @brief Simulates native context loss and raises the renderer device-lost event. */
        void DebugSimulateContextLoss() override;

        /** @brief Attempts context recreation and raises resetting/reset events in order. */
        void DebugRestoreContext() override;

        /**
         * @brief Captures the current context-recovery state for focused validation.
         * @return State, generation, realized core objects, and any failure diagnostic.
         */
        [[nodiscard]] RlglContextRecoverySnapshot
            GetContextRecoverySnapshotForTesting() const;

        /** @brief Resets renderer-local work counters without invalidating warmed caches. */
        void ResetPerformanceCountersForTesting();

        /**
         * @brief Captures renderer work performed since the latest counter reset.
         * @return Draw, state, binding, upload, flush, allocation, conversion, and lookup counts.
         */
        [[nodiscard]] RlglPerformanceSnapshot GetPerformanceSnapshotForTesting() const;

        /** @brief RLGL devices cannot be copied because each owns a GL context lifecycle. */
        RlglRenderer(const RlglRenderer&) = delete;

        /** @brief RLGL devices cannot be copy-assigned. */
        RlglRenderer& operator=(const RlglRenderer&) = delete;

        /**
         * @brief Clears the active color buffer.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         */
        void Clear(float r, float g, float b, float a) override;

        /** @brief Presents the current back buffer through CNA's platform context service. */
        void Present() override;

        /**
         * @brief Returns the logical viewport extent exposed to game code.
         *
         * @param width Receives the logical width.
         * @param height Receives the logical height.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Returns the physical drawable rectangle for the current presentation mode.
         *
         * @param x Receives the physical left edge.
         * @param y Receives the physical top edge.
         * @param width Receives the physical width.
         * @param height Receives the physical height.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;

        /**
         * @brief Refreshes the CNA-owned drawable size and scale snapshot.
         *
         * @param surface Updated surface information for the same stable window.
         */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief Updates the logical resolution used by presentation scaling.
         *
         * @param width Logical width, or zero to use the physical width.
         * @param height Logical height, or zero to use the physical height.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation scaling policy.
         *
         * @param mode A CnaPresentationMode ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Applies a requested swap interval through the platform GL service.
         *
         * @param interval Number of retraces, with zero selecting immediate presentation.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief Returns the swap interval most recently requested by CNA.
         *
         * @return The requested interval, whether or not the driver accepted it.
         */
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }

        /**
         * @brief Returns the multisample count applied to the renderer-owned back buffer.
         *
         * @return The applied sample count, or zero when multisampling is disabled.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Maps a presentation multisample request to the renderer-applied count.
         *
         * @param requestedMultiSampleCount The requested sample count.
         * @return The count actually applied to this device's back buffer.
         */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(
            int requestedMultiSampleCount) const override;

        /**
         * @brief Recreates renderer-owned backbuffer storage for a new sample request.
         * @param requestedMultiSampleCount Requested new sample count.
         * @return The device-clamped sample count that was applied.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;

        /**
         * @brief Records an ordinary presentation Reset without treating it as native context loss.
         * @param backBufferFormat Normalized requested backbuffer format ordinal.
         * @param depthStencilFormat Normalized requested depth/stencil format ordinal.
         * @param isFullScreen Whether the presentation request is fullscreen.
         */
        void UpdatePresentationFormatEXT(
            int backBufferFormat, int depthStencilFormat, bool isFullScreen) override;

        /**
         * @brief Returns the SurfaceFormat matching the actual context framebuffer.
         * @param requestedFormat Requested format retained only for interface parity.
         * @return Applied SurfaceFormat ordinal.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(
            int requestedFormat) const override;

        /**
         * @brief Returns the DepthFormat matching the actual context depth/stencil planes.
         * @param requestedFormat Requested format retained only for interface parity.
         * @return Applied DepthFormat ordinal.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(
            int requestedFormat) const override;

        /**
         * @brief Reports whether the created back buffer has both depth and stencil planes.
         *
         * @return True only when both planes were granted.
         */
        [[nodiscard]] bool SupportsDepthStencil() const override;

#if defined(CNA_RLGL_COMPILED_EFFECTS)
        /**
         * @brief Creates the renderer-local runtime for classic XNA Effect Framework bytecode.
         * @param effectCode Effect Framework bytes.
         * @param effectCodeBytes Number of bytes at @p effectCode.
         * @return Parsed and GL-compiled device runtime.
         */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /**
         * @brief Reports that the option-on renderer executes ordinary compiled-effect draws.
         * @return True in a `CNA_RLGL_COMPILED_EFFECTS=ON` build.
         */
        [[nodiscard]] bool SupportsCompiledEffects() const override { return true; }
#endif

        /**
         * @brief Reports whether the created back buffer has a depth plane.
         *
         * @return True when the driver granted at least one depth bit.
         */
        [[nodiscard]] bool SupportsDepthBuffer() const override { return depthBits_ > 0; }

        /**
         * @brief Reports whether the created back buffer has a stencil plane.
         *
         * @return True when the driver granted at least one stencil bit.
         */
        [[nodiscard]] bool SupportsStencilBuffer() const override { return stencilBits_ > 0; }

        /**
         * @brief Answers implemented renderer capabilities without inferring them from GL support.
         *
         * @param capability Capability to query.
         * @return True only for a corresponding complete CNA path with focused validation.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Returns the source dialect accepted by RLGL ShaderEffect programs.
         * @return Desktop OpenGL GLSL for the fixed GL 3.3 core context.
         */
        [[nodiscard]] ShaderDialectEXT GetShaderDialectEXT() const override
        {
            return ShaderDialectEXT::GlslDesktop;
        }

        /**
         * @brief Reports the exact language/stage pairs compiled by RLGL ShaderEffect.
         * @param language Raw `CNA::ShaderLanguageEXT` ordinal.
         * @param stage Raw `CNA::ShaderStageEXT` ordinal.
         * @return True only for desktop GLSL vertex and fragment stages.
         */
        [[nodiscard]] bool SupportsShaderLanguageEXT(
            int language, int stage) const override;

        /** @brief Returns true because RLGL compiles and executes supplied GLSL source. */
        [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const override { return true; }

        /**
         * @brief Reads an RGBA8 region of the current back buffer in top-left row order.
         *
         * @param x Left edge in game/backbuffer coordinates.
         * @param y Top edge in game/backbuffer coordinates.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param pixels Destination holding at least w * h * 4 bytes.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Creates a sampled two-dimensional texture.
         *
         * @param data Texture dimensions and initial pixels.
         * @return Renderer-owned texture record.
         * @throws std::runtime_error When the requested format has not passed its RLGL gate.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a sampled RGBA8 volume texture.
         * @param w Level-zero width.
         * @param h Level-zero height.
         * @param depth Level-zero depth.
         * @param mipMap Whether to allocate the complete width/height-derived mip chain.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal; currently Color only.
         * @return Renderer-owned volume resource.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Creates a sampled cube texture with six independently writable faces.
         * @param size Width and height of every face.
         * @param mipMap Whether to allocate the complete mip chain.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Renderer-owned cube texture record.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Compiles one caller-supplied desktop GLSL ShaderEffect pair.
         * @param vertSrc Vertex-stage GLSL source.
         * @param fragSrc Fragment-stage GLSL source.
         * @return RLGL-owned program with uniform, texture, draw, and recovery state.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(
            const std::string& vertSrc, const std::string& fragSrc) override;

        /**
         * @brief Returns the current OpenGL context's maximum two-dimensional texture edge.
         * @return The `GL_MAX_TEXTURE_SIZE` value measured after rlgl initialization.
         */
        [[nodiscard]] int GetMaxTextureDimension() const override { return maxTextureSize_; }

        /**
         * @brief Applies XNA's cube-size ceiling to the live GL texture limit.
         * @param graphicsProfile Raw XNA GraphicsProfile ordinal.
         * @return At most 512 for Reach and 4096 for HiDef.
         */
        [[nodiscard]] int GetMaxCubeSizeForProfileEXT(
            int graphicsProfile) const override;

        /**
         * @brief Returns the live OpenGL volume-texture extent limit.
         * @param graphicsProfile Raw XNA GraphicsProfile ordinal.
         * @return The `GL_MAX_3D_TEXTURE_SIZE` value for either supported profile.
         */
        [[nodiscard]] int GetMaxVolumeExtentForProfileEXT(
            int graphicsProfile) const override;

        /**
         * @brief Classifies Texture2D formats whose complete storage path has passed validation.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return Supported only for implemented exact layouts; Unsupported otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Classifies formats with a complete RLGL cube storage and transfer path.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Supported for Color and the three DXT block formats; Unsupported otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyTextureCubeFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Classifies formats with a complete RLGL volume storage and transfer path.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Supported for Color and Unsupported for every other format.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyTexture3DFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Probes exact classic-XNA two-dimensional target renderability.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return Supported only when the live context completes the exact mapped framebuffer.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Probes exact classic-XNA cube-target renderability.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Supported only when an exact cube-face framebuffer completes.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetCubeFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Prevents Color transfers from reinterpreting signed-normalized formats.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return Unsupported for signed-normalized layouts and Defer otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Reports DXT formats transferred as native compressed blocks.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return True for DXT1/3/5; the resource selects native storage or software decode.
         */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Reports exact DXT block transfer support for plain cube textures.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return True for Dxt1, Dxt3, and Dxt5.
         */
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Keeps supported DXT content compressed through CNA's loaders.
         * @return True; loaders retain DXT blocks until the renderer chooses native or fallback storage.
         */
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override;

        /** @brief Returns true because source and compiled shaders sample real RLGL volumes. */
        [[nodiscard]] bool SupportsTexture3DSamplingEXT() const override { return true; }

        /**
         * @brief Creates the renderer-owned SpriteBatch implementation.
         *
         * @return A CNA-scheduled low-level rlgl SpriteBatch renderer.
         */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Creates a precise OpenGL sample-count query.
         * @return Renderer-owned classic XNA occlusion query.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief Creates a framebuffer-backed RenderTarget2D with optional multisampling.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether contents survive target switches.
         * @param mipMap Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested sample count, clamped to the live device limit.
         * @return Renderer-owned target resource.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;

        /**
         * @brief Creates a format-explicit framebuffer-backed RenderTarget2D.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether contents survive target switches.
         * @param mipMap Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Renderer-owned target resource.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief Creates a Color cube render target.
         * @param size Width and height of all faces.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether contents survive face switches.
         * @param mipMap Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @return Renderer-owned cube target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;

        /**
         * @brief Creates an exact format-explicit cube render target.
         * @param size Width and height of all faces.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether contents survive face switches.
         * @param mipMap Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal.
         * @return Renderer-owned cube target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief Applies XNA's profile ceiling to the measured native MRT limit.
         * @param graphicsProfile Raw XNA GraphicsProfile ordinal.
         * @return One for Reach, otherwise up to four supported native attachments.
         */
        [[nodiscard]] int GetMaxRenderTargetsForProfileEXT(
            int graphicsProfile) const override;

        /**
         * @brief Returns the logical extent used by the current SpriteBatch viewport.
         * @param width Receives the viewport-local logical width.
         * @param height Receives the viewport-local logical height.
         */
        void GetSpriteBatchViewportSize(int& width, int& height);

        /**
         * @brief Applies XNA filter, U/V addressing, and anisotropy to a texture slot.
         * @param slot Texture unit index.
         * @param filter Raw `TextureFilter` ordinal.
         * @param addressU Raw U `TextureAddressMode` ordinal.
         * @param addressV Raw V `TextureAddressMode` ordinal.
         * @param maxAnisotropy Requested maximum anisotropy.
         */
        void ApplySamplerState(
            int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        /**
         * @brief Applies XNA mip-level selection and bias to a texture slot.
         * @param slot Texture unit index.
         * @param maxMipLevel Most detailed selectable mip level.
         * @param lodBias Level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Applies XNA W addressing to a texture slot.
         * @param slot Texture unit index.
         * @param addressW Raw W `TextureAddressMode` ordinal.
         */
        void ApplySamplerAddressW(int slot, int addressW) override;

        /**
         * @brief Selects one RenderTarget2D or restores the default framebuffer.
         *
         * @param renderTargets Ordered target bindings, or nullptr for the back buffer.
         * @param count Number of bindings; zero restores the back buffer.
         * @throws System::NotSupportedException For cube faces or multiple targets.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /**
         * @brief Clears color and depth planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears the depth plane.
         *
         * @param depth Depth clear value.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears the stencil plane.
         *
         * @param stencil Stencil clear value.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil planes in one operation.
         *
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears color and stencil planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param stencil Stencil clear value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears color, depth, and stencil planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearColorDepthAndStencil(
            float r, float g, float b, float a, float depth, int stencil) override;

        /**
         * @brief Enables or disables depth testing through rlgl.
         *
         * @param enabled True to enable depth testing.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables color blending through rlgl.
         *
         * @param enabled True to enable blending.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes through rlgl.
         *
         * @param enabled True to enable depth writes.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Applies complete XNA blend factors, equations, write masks, and sample mask.
         * @param colorSrcBlend Raw color source `Blend` ordinal.
         * @param alphaSrcBlend Raw alpha source `Blend` ordinal.
         * @param colorDstBlend Raw color destination `Blend` ordinal.
         * @param alphaDstBlend Raw alpha destination `Blend` ordinal.
         * @param colorBlendFunc Raw color `BlendFunction` ordinal.
         * @param alphaBlendFunc Raw alpha `BlendFunction` ordinal.
         * @param writeState Per-target channel masks and multisample mask.
         */
        void ApplyBlendState(
            int colorSrcBlend, int alphaSrcBlend,
            int colorDstBlend, int alphaDstBlend,
            int colorBlendFunc, int alphaBlendFunc,
            const BlendWriteState& writeState) override;

        /**
         * @brief Applies the constant color used by BlendFactor modes.
         * @param r Red component.
         * @param g Green component.
         * @param b Blue component.
         * @param a Alpha component.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Applies complete XNA depth, stencil, and two-sided stencil state.
         * @param depthEnable Whether depth testing is enabled.
         * @param depthWriteEnable Whether depth writes are enabled.
         * @param depthFunc Raw depth `CompareFunction` ordinal.
         * @param stencilEnable Whether stencil testing is enabled.
         * @param stencilFunc Raw front-face stencil comparison.
         * @param stencilPass Raw front-face depth-pass operation.
         * @param stencilFail Raw front-face stencil-fail operation.
         * @param stencilDepthFail Raw front-face depth-fail operation.
         * @param stencilMask Stencil comparison mask.
         * @param stencilWriteMask Stencil write mask.
         * @param referenceStencil Stencil reference.
         * @param twoSidedStencilMode Whether front and back faces differ.
         * @param ccwStencilFunc Raw back-face comparison.
         * @param ccwStencilPass Raw back-face depth-pass operation.
         * @param ccwStencilFail Raw back-face stencil-fail operation.
         * @param ccwStencilDepthFail Raw back-face depth-fail operation.
         */
        void ApplyDepthStencilState(
            bool depthEnable, bool depthWriteEnable, int depthFunc,
            bool stencilEnable, int stencilFunc,
            int stencilPass, int stencilFail, int stencilDepthFail,
            int stencilMask, int stencilWriteMask, int referenceStencil,
            bool twoSidedStencilMode, int ccwStencilFunc, int ccwStencilPass,
            int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Changes the active stencil reference without reassigning the state object.
         * @param value New stencil reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Applies culling, fill, scissor-enable, and depth-bias state.
         * @param cullMode Raw `CullMode` ordinal.
         * @param fillMode Raw `FillMode` ordinal.
         * @param scissorTestEnable Whether scissor testing is enabled.
         * @param depthBias XNA normalized constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(
            int cullMode, int fillMode, bool scissorTestEnable,
            float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;

        /**
         * @brief Creates a vertex buffer.
         *
         * @param vertexCapacity Maximum vertex count.
         * @return A declaration-aware fixed-capacity rlgl buffer resource.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         *
         * @param indexCapacity Maximum index count.
         * @return A fixed-capacity rlgl element-buffer resource.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         *
         * @param indexCapacity Maximum index count.
         * @return A fixed-capacity rlgl element-buffer resource.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;

        /**
         * @brief Draws non-indexed colored primitives.
         *
         * @param vb Vertex buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(
            const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
            const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Draws indexed colored primitives.
         *
         * @param vb Vertex buffer.
         * @param ib Index buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Draws the currently supported unlit stock-effect subset.
         * @param vb Vertex buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params Effect values and first-vertex selection.
         */
        void DrawPrimitivesEx(
            const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
            const Matrix& projection, PrimitiveType primitive, int primitiveCount,
            const GpuDrawParams& params) override;

        /**
         * @brief Draws the currently supported indexed unlit stock-effect subset.
         * @param vb Vertex buffer.
         * @param ib Index buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params Effect values and index/base-vertex selection.
         */
        void DrawIndexedPrimitivesEx(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount,
            const GpuDrawParams& params) override;

        /**
         * @brief Draws indexed stock-effect geometry using per-instance vertex streams.
         * @param vb Primary per-vertex buffer.
         * @param ib Index buffer.
         * @param world Effect world transform.
         * @param view Effect view transform.
         * @param projection Effect projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount Number of instances.
         * @param params Effect values, stream bindings, and indexed draw range.
         */
        void DrawInstancedPrimitivesEx(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount, int instanceCount,
            const GpuDrawParams& params) override;

        /**
         * @brief Applies a physical GL viewport and depth range.
         *
         * @param x Left edge in top-left-origin drawable coordinates.
         * @param y Top edge in top-left-origin drawable coordinates.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param minDepth Minimum depth value.
         * @param maxDepth Maximum depth value.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Applies the scissor rectangle in top-left-origin drawable coordinates.
         *
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

    private:
        friend class RlglCompiledEffect;
        friend class Detail::RlglSpriteBatchRenderer;

        struct SamplerRecord
        {
            unsigned int id = 0;
            int filter = 0;
            int addressU = 0;
            int addressV = 0;
            int addressW = 0;
            int maxAnisotropy = 4;
            int maxMipLevel = 0;
            float lodBias = 0.0f;
            bool realized = false;
            bool nativeApplied = false;
        };

        struct StencilRecord
        {
            bool stateApplied = false;
            bool depthEnableAssigned = false;
            bool depthWriteAssigned = false;
            bool depthEnable = false;
            bool depthWriteEnable = false;
            int depthFunction = 0;
            bool enabled = false;
            bool twoSided = false;
            int function = 0;
            int counterClockwiseFunction = 0;
            int readMask = 0;
            int writeMask = 0;
            int reference = 0;
            int pass = 0;
            int fail = 0;
            int depthFail = 0;
            int counterClockwisePass = 0;
            int counterClockwiseFail = 0;
            int counterClockwiseDepthFail = 0;
            bool referenceAssigned = false;
        };

        struct BlendRecord
        {
            bool stateApplied = false;
            int colorSource = 0;
            int alphaSource = 0;
            int colorDestination = 0;
            int alphaDestination = 0;
            int colorFunction = 0;
            int alphaFunction = 0;
            BlendWriteState writeState{};
            bool factorApplied = false;
            std::array<float, 4> factor{{1.0f, 1.0f, 1.0f, 1.0f}};
            bool enabledApplied = false;
            bool enabled = false;
        };

        struct ViewportRecord
        {
            bool applied = false;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            float minDepth = 0.0f;
            float maxDepth = 1.0f;
        };

        struct ScissorRecord
        {
            bool applied = false;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        void CreateContext(int requestedMultiSampleCount);
        void RefreshContextAttributes();
        [[nodiscard]] std::string InitializeContextState();
        void TransitionToContextLost(const std::string& reason, bool nativeDetection);
        void ThrowIfNativeContextWasReset();
        void InvalidateRendererNativeState() noexcept;
        void DestroyRendererNativeState() noexcept;
        void RestoreRendererNativeState();
        void RestoreCurrentRenderTargets();
        void ReapplyDeviceState();
        void NotifyDeviceEvent(RendererDeviceEvent event);
        void GetPhysicalSize(int& width, int& height) const;
        void GetLogicalSize(int& width, int& height) const;
        void RecreateBackbufferStorage(int width, int height);
        void BindBackbuffer();
        void ResolveBackbufferToDefault();
        SamplerRecord& GetSamplerRecord(int slot);
        void ApplySamplerRecord(int slot, SamplerRecord& sampler);
        Bridge::PrimitivePipeline& GetPrimitivePipeline();
        [[nodiscard]] int GetCurrentSampleCount() const;
        void ApplyCurrentRasterizerState();
        void FinalizeCurrentRenderTargets();
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        SamplerRecord& GetCompiledEffectVertexSamplerRecord(int slot);
        void ApplyCompiledEffectVertexSamplerRecord(int slot, SamplerRecord& sampler);
        Bridge::CompiledEffectDrawResources& GetCompiledEffectDrawResources();
        void DrawCompiledEffectGeometry(
            const IVertexBufferRenderer& vertexBuffer,
            const IIndexBufferRenderer* indexBuffer,
            PrimitiveType primitive, int elementCount,
            int firstVertex, int startIndex, int baseVertex,
            const GpuDrawParams& params,
            const ITextureRenderer* spriteBatchSlotZeroTexture = nullptr,
            const Microsoft::Xna::Framework::Graphics::TextureCollection*
                spriteBatchTextures = nullptr);
        [[nodiscard]] void* GetMojoShaderContext();
        void MakeCompiledEffectContextCurrent();
        void DestroyCompiledEffectContext() noexcept;
#endif

        PlatformGlSurfaceState surface_;
        CNA::Platform::IPlatformGlContext* platformGlService_ = nullptr;
        std::shared_ptr<PlatformGlContextOwner> platformContext_;
        std::shared_ptr<RlglThreadContextLeaseControl> threadContextLeaseControl_;
        std::shared_ptr<RlglResourceLifetime> resourceLifetime_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::Letterbox;
        int swapInterval_ = 1;
        int requestedMultiSampleCount_ = 0;
        int multiSampleCount_ = 0;
        int depthBits_ = 0;
        int stencilBits_ = 0;
        int backBufferFormat_ = 0;
        int depthStencilFormat_ = 0;
        bool robustContext_ = false;
        bool nativeLossPollingAvailable_ = false;
        int maxTextureSize_ = 0;
        int maxTexture3DSize_ = 0;
        int maxSamplerSlots_ = 0;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        int maxVertexSamplerSlots_ = 0;
#endif
        int maxRenderTargets_ = 1;
        int maxRenderTargetSamples_ = 0;
        std::unique_ptr<Bridge::RenderTargetStorage> backbufferStorage_;
        float maxSamplerAnisotropy_ = 1.0f;
        std::array<SamplerRecord, 16> samplers_{};
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        std::array<SamplerRecord, 4> vertexSamplers_{};
#endif
        BlendRecord blend_{};
        StencilRecord stencil_{};
        ViewportRecord viewport_{};
        ScissorRecord scissor_{};
        int currentViewportWidth_ = 0;
        int currentViewportHeight_ = 0;
        bool viewportIsDefault_ = true;
        std::array<IRenderTargetRenderer*, 4> currentRenderTargets_{};
        std::array<IRenderTargetCubeRenderer*, 4> currentRenderTargetCubes_{};
        std::array<int, 4> currentRenderTargetCubeFaces_{{-1, -1, -1, -1}};
        int currentRenderTargetCount_ = 0;
        unsigned int mrtFramebuffer_ = 0;
        int currentRenderTargetWidth_ = 0;
        int currentRenderTargetHeight_ = 0;
        int currentTargetDepthBits_ = 0;
        int rasterizerCullMode_ = 2;
        int rasterizerFillMode_ = 0;
        bool rasterizerScissorTestEnabled_ = false;
        float rasterizerDepthBias_ = 0.0f;
        float rasterizerSlopeScaleDepthBias_ = 0.0f;
        bool rasterizerStateApplied_ = false;
        bool restorePrimitivePipeline_ = false;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        bool restoreCompiledDrawResources_ = false;
        bool restoreMojoShaderContext_ = false;
#endif
        std::function<void(RendererDeviceEvent)> deviceEventCallback_;
        std::atomic<RlglContextRecoveryState> contextRecoveryState_{
            RlglContextRecoveryState::Available};
        std::size_t contextGeneration_ = 0;
        std::atomic<std::size_t> presentationResets_{0};
        std::atomic<std::size_t> detectedNativeLosses_{0};
        std::string unavailableReason_;
        bool lifecycleClaimed_ = false;
        bool rlglInitialized_ = false;
        bool registered_ = false;
        std::unique_ptr<Bridge::PrimitivePipeline> primitivePipeline_;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        std::unique_ptr<Bridge::CompiledEffectDrawResources> compiledEffectDrawResources_;
        void* mojoShaderContext_ = nullptr;
#endif
    };
}
