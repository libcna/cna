// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/GlPresentationSurfaceState.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Common.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Resources.hpp"
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
#include "mojoshader.h"
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
namespace Microsoft::Xna::Framework::Graphics
{
    class TextureCollection;
}
#endif

namespace CNA::Internal::Renderers::OpenGL4
{
    class OpenGL4Renderer;
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
    class OpenGL4CompiledEffect;
#endif

    /**
     * @brief One linked GL program, compiled from a vertex/fragment source pair.
     *
     * plans/plan_opengl4.md GL4-1: OpenGL4 is a desktop OpenGL 4.x core-profile renderer family of
     * its own -- its own context request, its own loader, its own draw code. Since
     * plans/plan_opengl4_modern_graphics.md GL4-0008 it compiles the same stock-effect GLSL as
     * EasyGL (CNA/Internal/Renderers/Common/GlStockShaderSources.hpp), adapted to desktop core by
     * @ref AdaptGlslEs300ForDesktopCore.
     */
    class OpenGL4RawProgram
    {
    public:
        OpenGL4RawProgram() = default;
        ~OpenGL4RawProgram();

        OpenGL4RawProgram(const OpenGL4RawProgram&) = delete;
        OpenGL4RawProgram& operator=(const OpenGL4RawProgram&) = delete;
        OpenGL4RawProgram(OpenGL4RawProgram&&) noexcept;
        OpenGL4RawProgram& operator=(OpenGL4RawProgram&&) noexcept;

        /**
         * @brief Compiles and links a vertex/fragment pair exactly as given.
         *
         * @param vertSrc Vertex shader source.
         * @param fragSrc Fragment shader source.
         * @return True on success; @ref GetError then names the stage and the driver's log.
         */
        bool Compile(const std::string& vertSrc, const std::string& fragSrc);
        /** @brief Makes this program current. */
        void Use() const;
        /** @brief Returns whether the program linked. */
        [[nodiscard]] bool IsValid() const { return program_ != 0; }
        /** @brief Returns the last compile or link diagnostic. */
        [[nodiscard]] const std::string& GetError() const { return error_; }
        /**
         * @brief Looks up a uniform.
         *
         * @param name Uniform name.
         * @return Its location, or -1 when the program does not declare it.
         */
        [[nodiscard]] int UniformLocation(const char* name) const;
        /** @brief Returns the GL program name. */
        [[nodiscard]] unsigned int Handle() const { return program_; }
        /**
         * @brief Attribute locations 0..31 whose declared GLSL type is an integer (`int`, `ivecN`,
         *        `uint`, `uvecN`).
         *
         * GL4-0023: such an input must be fed through glVertexAttribIPointer, or the shader reads
         * undefined values.
         *
         * @return A bit per location.
         */
        [[nodiscard]] std::uint32_t IntegerAttributeMask() const { return integerAttributeMask_; }
        /** @brief Deletes the program now; its context must be current. */
        void Reset() { Destroy(); }
        /** @brief Forgets the program without any GL call: its context is already gone. */
        void Abandon() noexcept { program_ = 0; }

    private:
        void Destroy();

        unsigned int program_ = 0;
        std::string error_;
        std::uint32_t integerAttributeMask_ = 0;
    };

    /**
     * @brief `OpenGL4`-backed custom `ShaderEffect` program: a thin wrapper around one program.
     *
     * GLSL ES 3.00 source (the CNAEXT engine layer's authored profile) is adapted to desktop core
     * before compilation; desktop GLSL passes through unchanged.
     */
    class OpenGL4EffectRenderer final : public IEffectRenderer, public OpenGL4ContextResource
    {
    public:
        OpenGL4EffectRenderer() = default;
        ~OpenGL4EffectRenderer() override;

        OpenGL4EffectRenderer(const OpenGL4EffectRenderer&) = delete;
        OpenGL4EffectRenderer& operator=(const OpenGL4EffectRenderer&) = delete;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override;
        [[nodiscard]] std::string GetCompileError() const override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void SetUniformVec3Array(const char* name, const float* values, int count) override;
        void SetUniformMat4Array(const char* name, const float* matrices, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /** @brief Returns the program, so a SpriteBatch flush binds the one the setters wrote to. */
        [[nodiscard]] OpenGL4RawProgram& GetProgram() { return program_; }

    private:
        /**
         * @brief Makes the program current so a uniform setter writes to it.
         *
         * glUniform writes to the CURRENT program, not the one whose location was looked up; a
         * setter called before the effect is applied would otherwise write into another program.
         */
        void MakeProgramCurrent();
        /**
         * @brief Locates an array uniform by its bare name or by its first element.
         *
         * @param name Array name.
         * @return Location, or -1.
         */
        [[nodiscard]] int ArrayUniformLocation(const char* name) const;

        OpenGL4RawProgram program_;
        float rtFlipV_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        bool rtFlipVUploaded_ = false;
    };

    /**
     * @brief `OpenGL4`-backed occlusion query: a real `GL_SAMPLES_PASSED` query object.
     *
     * Desktop GL reports an exact passed-sample count, which is XNA's own desktop
     * `OcclusionQuery.PixelCount()` semantics.
     */
    class OpenGL4OcclusionQueryRenderer final : public IOcclusionQueryRenderer,
                                                public OpenGL4ContextResource
    {
    public:
        OpenGL4OcclusionQueryRenderer();
        ~OpenGL4OcclusionQueryRenderer() override;

        OpenGL4OcclusionQueryRenderer(const OpenGL4OcclusionQueryRenderer&) = delete;
        OpenGL4OcclusionQueryRenderer& operator=(const OpenGL4OcclusionQueryRenderer&) = delete;

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int PixelCount() const override;
        [[nodiscard]] bool PixelCountIsPreciseEXT() const noexcept override { return true; }

    private:
        unsigned int query_ = 0;
        bool issued_ = false;
        mutable bool resultCached_ = false;
        mutable int cachedResult_ = 0;
    };

    /**
     * @brief `OpenGL4`-backed vertex buffer: one VBO and the VAO its declaration is bound into.
     *
     * The VAO's default layout comes from the caller's `VertexDeclaration` (location N = element
     * N), or from the built-in record shapes when no declaration was supplied. A stock-effect draw
     * rebinds the locations to the program's semantics for the draw and restores this layout
     * afterwards (plans/plan_opengl4_modern_graphics.md GL4-0013).
     */
    class OpenGL4VertexBufferRenderer final : public IVertexBufferRenderer,
                                              public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates the buffer objects.
         *
         * @param vertexCapacity The VertexBuffer's fixed VertexCount.
         */
        explicit OpenGL4VertexBufferRenderer(int vertexCapacity);
        ~OpenGL4VertexBufferRenderer() override;

        OpenGL4VertexBufferRenderer(const OpenGL4VertexBufferRenderer&) = delete;
        OpenGL4VertexBufferRenderer& operator=(const OpenGL4VertexBufferRenderer&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the element list of the remembered declaration (empty when none). */
        CNAEXT [[nodiscard]] const std::vector<VertexElement>& GetDeclarationElements() const
        {
            return declarationElements_;
        }
        /** @brief Returns the stride of the last upload. */
        CNAEXT [[nodiscard]] std::size_t GetStride() const { return strideInBytes_; }
        /** @brief Returns the GL vertex array object name. */
        CNAEXT [[nodiscard]] unsigned int VaoHandle() const { return vao_; }
        /** @brief Returns the GL buffer object name. */
        CNAEXT [[nodiscard]] unsigned int VboHandle() const { return vbo_; }

        /**
         * @brief (Re)builds the VAO's default layout for @p stride.
         *
         * @param stride Record stride in bytes.
         * @throws System::NotSupportedException For an unknown stride with no declaration.
         */
        void ApplyLayout(std::size_t stride);

    private:
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options);

        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t strideInBytes_ = 0;
        bool gpuAllocated_ = false;
        std::vector<VertexElement> declarationElements_;
    };

    /** @brief `OpenGL4`-backed index buffer, 16- or 32-bit. */
    class OpenGL4IndexBufferRenderer final : public IIndexBufferRenderer,
                                             public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates the buffer object.
         *
         * @param indexCapacity The IndexBuffer's fixed IndexCount.
         * @param thirtyTwoBit True for `IndexElementSize.ThirtyTwoBits`.
         */
        OpenGL4IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit);
        ~OpenGL4IndexBufferRenderer() override;

        OpenGL4IndexBufferRenderer(const OpenGL4IndexBufferRenderer&) = delete;
        OpenGL4IndexBufferRenderer& operator=(const OpenGL4IndexBufferRenderer&) = delete;

        void SetData16(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32(const void* data, int index_count) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the GL buffer object name. */
        CNAEXT [[nodiscard]] unsigned int IboHandle() const { return ibo_; }
        /**
         * @brief Returns the uploaded index bytes.
         *
         * Kept because a negative `baseVertex` is folded into the index values on the CPU
         * (see OpenGL4Renderer::BindNegativeBaseVertexIndices).
         */
        CNAEXT [[nodiscard]] const std::vector<std::uint8_t>& GetCpuBytes() const { return cpuData_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t elementSize,
                    SetDataOptions options);

        unsigned int ibo_ = 0;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        bool gpuAllocated_ = false;
        std::vector<std::uint8_t> cpuData_;
    };

    /**
     * @brief `OpenGL4`-backed `SpriteBatch` renderer.
     *
     * CPU-generated quads with layer depth, the device's own blend/depth/rasterizer state, the
     * device Viewport, render-target orientation and XNA's 2 048-sprite submission ceiling
     * (plans/plan_opengl4_modern_graphics.md GL4-0014).
     */
    class OpenGL4SpriteBatchRenderer final : public ISpriteBatchRenderer,
                                             public OpenGL4ContextResource
    {
    public:
        /** @brief One sprite vertex: position (with layer depth), UV and colour. */
        struct Vertex { float x, y, z, u, v, r, g, b, a; };

        /**
         * @brief Creates the batch's program, VAO and dynamic buffers.
         *
         * @param owner The renderer whose viewport and sampler state the batch uses.
         */
        explicit OpenGL4SpriteBatchRenderer(OpenGL4Renderer& owner);
        ~OpenGL4SpriteBatchRenderer() override;

        OpenGL4SpriteBatchRenderer(const OpenGL4SpriteBatchRenderer&) = delete;
        OpenGL4SpriteBatchRenderer& operator=(const OpenGL4SpriteBatchRenderer&) = delete;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { pendingFilter_ = textureFilter; }
        void SetSamplerMaxAnisotropy(int maxAnisotropy) override { pendingMaxAnisotropy_ = maxAnisotropy; }
        void SetSamplerMipState(int maxMipLevel, float lodBias) override
        {
            pendingMaxMipLevel_ = maxMipLevel;
            pendingLodBias_ = lodBias;
        }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            pendingAddressU_ = addressU;
            pendingAddressV_ = addressV;
        }
        void SetSamplerState(int textureFilter, int addressU, int addressV, int addressW,
                             int maxAnisotropy, int maxMipLevel, float lodBias) override;
        void SetImmediateMode(bool immediate) override { immediateMode_ = immediate; }
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
        void Draw(const ITextureRenderer& texture,
                  float destinationX, float destinationY,
                  float destinationWidth, float destinationHeight,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        // XNA and FNA keep the native UInt16-indexed SpriteBatch buffers at 2 048 sprites. The
        // public queue may be larger; one submission may not.
        static constexpr std::size_t kMaxSpritesPerBatch = 2048;
        static constexpr std::size_t kMaxVerticesPerBatch = kMaxSpritesPerBatch * 4;

        void FlushBatch();
        void ResolveCurrentTextureRowOrder();
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
        /// plans/plan_fx.md FX-118: whether the batch being built flushes through the compiled
        /// route, which corrects a render target's row order per sampler slot instead of in the
        /// sprite's own V. The route is fixed for the whole batch: SetCustomEffect() flushes.
        [[nodiscard]] bool BatchFlushesThroughCompiledEffect() const;
        /// plans/plan_fx.md FX-080: the compiled-Effect half of FlushBatch(). Separate because it
        /// shares nothing with the stock/ShaderEffect route and keeps that route unchanged.
        void FlushBatchWithCompiledEffect();
        /// Applies the embedded XNA SpriteEffect's pass so a custom pass that assigns only a pixel
        /// shader inherits its vertex shader and MatrixTransform, exactly as Direct3D does.
        void ApplyCompiledSpriteVertexShader(int logicalWidth, int logicalHeight);
#endif

        OpenGL4Renderer* owner_ = nullptr;
        OpenGL4RawProgram program_;
        int projectionLocation_ = -1;
        int channelMaskLocation_ = -1;
        int channelFillLocation_ = -1;
        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        unsigned int ibo_ = 0;
        bool begun_ = false;
        bool immediateMode_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Effect* customEffect_ = nullptr;
        const ITextureRenderer* currentTexture_ = nullptr;
        bool currentTextureBottomUp_ = false;
        int pendingFilter_ = 0;
        int pendingAddressU_ = 1;
        int pendingAddressV_ = 1;
        int pendingAddressW_ = -1;
        int pendingMaxAnisotropy_ = 4;
        int pendingMaxMipLevel_ = 0;
        float pendingLodBias_ = 0.0f;
        std::vector<Vertex> pendingVertices_;
        std::vector<std::uint16_t> pendingIndices_;
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
        /// The embedded XNA SpriteEffect, created on the first compiled-effect flush so a batch that
        /// never meets a compiled Effect never touches MojoShader.
        std::unique_ptr<OpenGL4CompiledEffect> spriteCompiledEffect_;
        std::uint32_t spriteMatrixParameterIndex_ = 0;
        /// plans/plan_fx.md FX-120: the compiled route's own geometry, retained rather than created
        /// per flush, because the shared compiled-effect vertex array object records them.
        std::unique_ptr<IVertexBufferRenderer> compiledSpriteVertexBuffer_;
        std::unique_ptr<IIndexBufferRenderer> compiledSpriteIndexBuffer_;
#endif
    };

    /**
     * @brief One stock-effect program and every uniform location the shared corpus declares.
     *
     * The field set, and the name each resolves, is EasyGL's Prog3D: the programs are the same
     * source (GlStockShaderSources.hpp), and EasyGL resolves every uniform each one declares
     * (plans/plan_opengl4_modern_graphics.md GL4-0008), so resolving the whole name set for every
     * program is exactly equivalent. A location a program does not declare stays -1 and is skipped.
     */
    struct OpenGL4StockProgram
    {
        OpenGL4RawProgram prog;
        bool ready = false;
        int loc_wvp = -1;
        int loc_normalmat = -1;
        int loc_world = -1;
        int loc_diffuse = -1;
        int loc_ambient = -1;
        int loc_lighting_enabled = -1;
        int loc_l0dir = -1;
        int loc_l0diff = -1;
        int loc_l1dir = -1;
        int loc_l1diff = -1;
        int loc_l2dir = -1;
        int loc_l2diff = -1;
        int loc_l0spec = -1;
        int loc_l1spec = -1;
        int loc_l2spec = -1;
        int loc_specularcolor = -1;
        int loc_specularpower = -1;
        int loc_texture = -1;
        int loc_texture2 = -1;
        int loc_shadowmap = -1;
        int loc_lightviewproj = -1;
        int loc_shadows_on = -1;
        int loc_shadow_bias = -1;
        int loc_shadow_texel = -1;
        int loc_shadow_pcf = -1;
        int loc_cascade_count = -1;
        int loc_cascade_mats = -1;
        int loc_cascade_splits = -1;
        int loc_cascade_viewz = -1;
        int loc_cascade_blend = -1;
        int loc_cascade_debug = -1;
        int loc_punctual_kind = -1;
        int loc_punctual_pos = -1;
        int loc_punctual_dir = -1;
        int loc_punctual_diff = -1;
        int loc_punctual_range = -1;
        int loc_punctual_cosin = -1;
        int loc_punctual_cosout = -1;
        int loc_punctual_bias = -1;
        int loc_punctual_hasmap = -1;
        int loc_punctual_cube = -1;
        int loc_punctual_map = -1;
        int loc_punctual_vp = -1;
        int loc_punctual_texel = -1;
        int loc_ibl_enabled = -1;
        int loc_ibl_irradiance = -1;
        int loc_ibl_specular = -1;
        int loc_ibl_brdf = -1;
        int loc_ibl_mipcount = -1;
        int loc_ibl_intensity = -1;
        int loc_envmap = -1;
        int loc_envmap_amount = -1;
        int loc_envmap_spec = -1;
        int loc_fresnel_enabled = -1;
        int loc_fresnel_factor = -1;
        int loc_emissive = -1;
        int loc_eyepos = -1;
        int loc_bones = -1;
        int loc_weightsPerVertex = -1;
        int loc_alphatest = -1;
        int loc_fog_vector = -1;
        int loc_fog_color = -1;
        int loc_vertexcolor = -1;
        int loc_pbr_normalmap = -1;
        int loc_pbr_mr = -1;
        int loc_pbr_emissivemap = -1;
        int loc_pbr_occlusionmap = -1;
        int loc_pbr_specularmap = -1;
        int loc_pbr_specularcolormap = -1;
        int loc_pbr_metallic = -1;
        int loc_pbr_roughness = -1;
        int loc_pbr_dielectric_fresnel = -1;
        int loc_pbr_specular_fresnel_inputs = -1;
        int loc_pbr_srgb = -1;
        int loc_pbr_normalscale = -1;
        int loc_pbr_occlstrength = -1;
        int loc_pbr_texcoordsets = -1;
        int loc_pbr_occlusiontexcoordset = -1;
        int loc_pbr_specular_texcoordsets = -1;
        std::array<int, 10> loc_pbr_texture_transform_rows{
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
        std::array<int, 4> loc_pbr_specular_texture_transform_rows{-1, -1, -1, -1};
        int loc_rt_flip_v = -1;
        int loc_rt_flip_v_hi = -1;
        int loc_instanced = -1;

        /** @brief Resolves every uniform location from the linked program. */
        void Resolve();
    };

    /** @brief Desktop OpenGL 4.x core-profile `IGraphicsRenderer` implementation. */
    class OpenGL4Renderer final : public IGraphicsRenderer
    {
    public:
        explicit OpenGL4Renderer(const GraphicsRendererCreateArgs& args);
        ~OpenGL4Renderer() override;

        OpenGL4Renderer(const OpenGL4Renderer&) = delete;
        OpenGL4Renderer& operator=(const OpenGL4Renderer&) = delete;

        [[nodiscard]] std::unique_ptr<IRendererThreadContextLease>
        AcquireThreadContextLeaseEXT(
            RendererThreadContextLeaseRelease release =
                RendererThreadContextLeaseRelease::RestorePreviousBinding) override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        void UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                         bool isFullScreen) override;
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override
        {
            // The window framebuffer is RGBA8 whatever the game asked for, exactly as EasyGL's.
            (void)requestedFormat;
            return 0;
        }
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(int requestedMultiSampleCount) const override
        {
            (void)requestedMultiSampleCount;
            return GetMultiSampleCount();
        }
        [[nodiscard]] int GetMultiSampleCount() const override
        {
            return sampleCount_ > 1 ? sampleCount_ : 0;
        }
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /**
         * @brief Answers every current GraphicsCapability member explicitly.
         *
         * The switch has no default case, so a future member surfaces as a compiler warning here
         * instead of a confident wrong answer inherited from the base default.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Returns desktop GLSL; OpenGL4 compiles supplied source in its live core context. */
        [[nodiscard]] ShaderDialectEXT GetShaderDialectEXT() const override
        {
            return ShaderDialectEXT::GlslDesktop;
        }
        /**
         * @brief Reports the explicit shader payloads the OpenGL4 paths consume.
         *
         * @param language `CNA::ShaderLanguageEXT` ordinal.
         * @param stage `CNA::ShaderStageEXT` ordinal.
         * @return True for desktop GLSL vertex and fragment payloads, and compute payloads where
         *         SupportsComputeShadersEXT() holds.
         */
        [[nodiscard]] bool SupportsShaderLanguageEXT(int language, int stage) const override;
        /** @brief Returns true because custom-effect source is compiled and executed by OpenGL. */
        [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const override { return true; }
        /** @brief Returns true: a Texture3D bound to a custom effect is a real GL_TEXTURE_3D sampler input. */
        [[nodiscard]] bool SupportsTexture3DSamplingEXT() const override { return true; }

        // --- Modern CNAEXT surface (Workstream B; OpenGL4Modern.cpp) -------------------------

        /**
         * @brief Whether compute programs, storage buffers and dispatch are implemented here.
         *
         * @return True on a 4.3+ core context with native compute and shader-storage buffers:
         *         every desktop compute payload CNA ships is `#version 430 core`.
         */
        [[nodiscard]] bool SupportsComputeShadersEXT() const override;
        /**
         * @brief Whether a Texture2D can be bound to a compute program as an image.
         *
         * @return True with compute and native image load/store; desktop GL needs no immutable
         *         storage for it.
         */
        [[nodiscard]] bool SupportsComputeImageBindingEXT() const override;
        /**
         * @brief Maximum work groups of one dispatch along an axis.
         *
         * @param axis 0, 1 or 2.
         * @return `GL_MAX_COMPUTE_WORK_GROUP_COUNT`, or 0 without compute or for another axis.
         */
        [[nodiscard]] int GetMaxComputeWorkGroupCountEXT(int axis) const override;
        /**
         * @brief Maximum local size along an axis.
         *
         * @param axis 0, 1 or 2.
         * @return `GL_MAX_COMPUTE_WORK_GROUP_SIZE`, or 0 without compute or for another axis.
         */
        [[nodiscard]] int GetMaxComputeWorkGroupSizeEXT(int axis) const override;
        /** @brief Returns `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS`, or 0 without compute. */
        [[nodiscard]] int GetMaxComputeWorkGroupInvocationsEXT() const override;
        /** @brief Returns `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS`, or 0 without compute. */
        [[nodiscard]] int GetMaxVertexShaderStorageBlocksEXT() const override;
        /** @brief Returns `GL_MAX_SHADER_STORAGE_BLOCK_SIZE`, or 0 without compute. */
        [[nodiscard]] std::uint64_t GetMaxStorageBufferBytesEXT() const override;
        /** @brief Returns `GL_MAX_UNIFORM_BLOCK_SIZE`; uniform blocks are core in every 4.x. */
        [[nodiscard]] std::uint64_t GetMaxUniformBufferBytesEXT() const override;
        /** @brief Returns the storage blocks one compute program may bind, or 0 without compute. */
        [[nodiscard]] int GetMaxComputeStorageBufferBindingsEXT() const override;
        /** @brief Returns the smallest per-stage texture-unit limit of the stages this renderer runs. */
        [[nodiscard]] int GetMaxSampledTexturesPerShaderStageEXT() const override;
        /** @brief Returns the compute image-unit limit, or 0 without compute image binding. */
        [[nodiscard]] int GetMaxStorageImagesPerShaderStageEXT() const override;
        /** @brief Returns the per-vertex stream ceiling: every stream is its own GL buffer. */
        [[nodiscard]] int GetMaxVertexInputBindingsEXT() const override;
        /** @brief Returns `GL_MAX_VERTEX_ATTRIBS`. */
        [[nodiscard]] int GetMaxVertexInputAttributesEXT() const override;
        /** @brief Returns the colour attachments one draw writes (the MRT ceiling, at most four). */
        [[nodiscard]] int GetMaxColorAttachmentsEXT() const override;
        /** @brief Returns `GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT`, or 0 without compute. */
        [[nodiscard]] std::uint64_t GetMinStorageBufferOffsetAlignmentEXT() const override;
        /** @brief Returns `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`. */
        [[nodiscard]] std::uint64_t GetMinUniformBufferOffsetAlignmentEXT() const override;
        /**
         * @brief Compiles a desktop GLSL compute program.
         *
         * @param computeSrc The source.
         * @return The program -- also when it failed to build, so its log reaches the caller --
         *         or null without compute.
         */
        std::unique_ptr<IComputeShaderRenderer> CreateComputeShader(
            const std::string& computeSrc) override;
        /**
         * @brief Allocates a storage buffer with the legacy all-purpose usage.
         *
         * @param byteSize Bytes to allocate.
         * @return The buffer, or null without compute or for a zero size.
         */
        std::unique_ptr<IStorageBufferRenderer> CreateStorageBuffer(std::size_t byteSize) override;
        /**
         * @brief Allocates a buffer for exactly the declared roles.
         *
         * @param byteSize Bytes to allocate.
         * @param usage `CNA::Graphics::StorageBufferUsage` bits.
         * @param cpuAccess `CNA::Graphics::StorageBufferCpuAccess` bits.
         * @return The buffer, or null when a declared role is not available here.
         */
        std::unique_ptr<IStorageBufferRenderer> CreateStorageBufferEXT(
            std::size_t byteSize, std::uint32_t usage, std::uint32_t cpuAccess) override;
        /**
         * @brief Dispatches a compute program with its recorded bindings.
         *
         * @param shader A program created by this renderer.
         * @param groupsX Work groups along X.
         * @param groupsY Work groups along Y.
         * @param groupsZ Work groups along Z.
         */
        void DispatchCompute(IComputeShaderRenderer* shader, int groupsX, int groupsY,
                             int groupsZ) override;
        /**
         * @brief Issues exactly the requested `glMemoryBarrier` bits.
         *
         * @param barrierBits `CNA::GraphicsMemoryBarrier` bits.
         */
        void MemoryBarrierEXT(int barrierBits) override;
        /**
         * @brief Binds a storage buffer for the next draw's shaders to read.
         *
         * @param binding Shader-storage block binding.
         * @param buffer A buffer created by this renderer.
         */
        void BindStorageBufferForDrawEXT(int binding,
                                         const IStorageBufferRenderer& buffer) override;
        /**
         * @brief Whether draw arguments can be read from a GPU buffer.
         *
         * @return The native fact: `glDrawArraysIndirect`/`glDrawElementsIndirect` are core 4.0,
         *         so every context this renderer accepts has them once the entry points resolve.
         */
        [[nodiscard]] bool SupportsIndirectDrawEXT() const override;
        /**
         * @brief Whether an instanced draw can begin at a caller-selected instance.
         *
         * @return True where `glDrawElementsInstancedBaseVertexBaseInstance` (core 4.2) resolved.
         */
        [[nodiscard]] bool SupportsBaseInstanceDrawingEXT() const override;
        /**
         * @brief Whether GPU time can be measured.
         *
         * @return True where timestamp queries resolved and the driver reports a non-zero
         *         `GL_TIMESTAMP` counter width.
         */
        [[nodiscard]] bool SupportsGpuTimerEXT() const override;
        /**
         * @brief Creates a timestamp-pair GPU timer.
         *
         * @return The timer, or null without GPU timers.
         */
        std::unique_ptr<IGpuTimerRenderer> CreateGpuTimerEXT() override;
        /** @brief Returns 1000: a `GL_TIMESTAMP` tick is one nanosecond; 0 without timers. */
        [[nodiscard]] std::uint64_t GetTimestampPeriodPicosecondsEXT() const override;
        /**
         * @brief Inserts a debug marker into the command stream (`glDebugMessageInsert`).
         *
         * @param marker The marker text; null is ignored, as is a context without KHR_debug.
         */
        void SetStringMarkerEXT(const char* marker) override;
        /** @brief CNAEXT. Whether the GL debug callback is installed (Debug builds by default). */
        CNAEXT [[nodiscard]] bool IsDebugOutputEnabledEXT() const noexcept
        {
            return debugOutputEnabled_;
        }
        /**
         * @brief Draws with its vertex and instance counts read from @p argumentBuffer.
         *
         * @param vb The first bound vertex buffer.
         * @param world World matrix for the stock programs.
         * @param view View matrix for the stock programs.
         * @param projection Projection matrix for the stock programs.
         * @param primitive Primitive topology.
         * @param argumentBuffer Buffer holding the four-word `IndirectDrawArguments` record.
         * @param argumentByteOffset Byte offset of the record.
         * @param params Draw state, streams and effect.
         * @throws System::NotSupportedException for a compiled XNA effect, whose passes carry the
         *         primitive count this route reads from GPU memory.
         */
        void DrawPrimitivesIndirectEXT(const IVertexBufferRenderer& vb, const Matrix& world,
                                       const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive,
                                       const IStorageBufferRenderer& argumentBuffer,
                                       int argumentByteOffset,
                                       const GpuDrawParams& params) override;
        /**
         * @brief Indexed counterpart of @ref DrawPrimitivesIndirectEXT (five-word record).
         *
         * @param vb The first bound vertex buffer.
         * @param ib The bound index buffer.
         * @param world World matrix for the stock programs.
         * @param view View matrix for the stock programs.
         * @param projection Projection matrix for the stock programs.
         * @param primitive Primitive topology.
         * @param argumentBuffer Buffer holding the `IndirectDrawIndexedArguments` record.
         * @param argumentByteOffset Byte offset of the record.
         * @param params Draw state, streams and effect.
         * @throws System::NotSupportedException for a compiled XNA effect.
         */
        void DrawIndexedPrimitivesIndirectEXT(const IVertexBufferRenderer& vb,
                                              const IIndexBufferRenderer& ib,
                                              const Matrix& world, const Matrix& view,
                                              const Matrix& projection, PrimitiveType primitive,
                                              const IStorageBufferRenderer& argumentBuffer,
                                              int argumentByteOffset,
                                              const GpuDrawParams& params) override;
        /**
         * @brief Returns false: every buffered draw is a native GL draw from a bound buffer object.
         *
         * XNA forwards draw ranges to the native API unvalidated, and GL never reads host memory
         * for them, so the managed compatibility guard is not needed (EasyGL's same answer).
         */
        [[nodiscard]] bool RequiresManagedBufferedDrawRangeValidationEXT() const noexcept override
        {
            return false;
        }

        /**
         * @brief Returns independently probed native modern-GL facts for this live context.
         *
         * These facts do not advertise the corresponding public CNA contracts on their own.
         *
         * @return Capabilities discovered immediately after context creation.
         */
        CNAEXT [[nodiscard]] const GL4::ModernCapabilities& GetModernCapabilitiesEXT() const
        {
            return modernCapabilities_;
        }

        // Resource creation. The texture/render-target/format members are defined in
        // OpenGL4Formats.cpp beside the format tables they depend on.
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] RendererFormatVerdict ClassifyTextureCubeFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] RendererFormatVerdict ClassifyTexture3DFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(int surfaceFormat) const override;
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override;
        [[nodiscard]] bool SupportsHalfFloatTextureLinearFilteringEXT() const override;

        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                       int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void ApplyRasterizerMultiSampleState(bool enabled) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;
        void ApplySamplerAddressW(int slot, int addressW) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /** @brief The stock program families, chosen from the effect state, never the stride. */
        enum class StockProgramShape
        {
            Colored, Textured, ColoredTextured, Lit, LitVertexLit, DualTextured,
            DualTexturedColored, EnvMapped, Skinned, SkinnedVertexLit, Pbr, PbrSkinned
        };

        // --- Helpers the SpriteBatch renderer uses -------------------------------------------

        /** @brief Physical drawable size. */
        CNAEXT void GetPhysicalSize(int& width, int& height) const;
        /** @brief Logical (virtual) size, honouring the presentation mode. */
        CNAEXT void GetLogicalSize(int& width, int& height) const;
        /**
         * @brief Reports the bound render target's extent.
         *
         * A cube face counts: a SpriteBatch drawn into one must be projected onto the face, not
         * onto the window (plans/plan_opengl4_modern_graphics.md GL4-0014).
         *
         * @param width Receives the width when a RenderTarget2D, cube face or MRT set is bound.
         * @param height Receives the height when a RenderTarget2D, cube face or MRT set is bound.
         * @return False, leaving the outputs untouched, when the back buffer is bound.
         */
        CNAEXT [[nodiscard]] bool GetBoundRenderTargetSize(int& width, int& height) const;
        /** @brief Programs the GL viewport and records it (the only glViewport writer). */
        CNAEXT void SetGlViewport(int x, int y, int width, int height);
        /** @brief Reads the recorded GL viewport. */
        CNAEXT void GetGlViewport(int& x, int& y, int& width, int& height) const;
        /** @brief Whether the last SetViewport() was the default (presentation) viewport. */
        CNAEXT [[nodiscard]] bool ViewportIsDefaultEXT() const { return viewportIsDefault_; }
        /**
         * @brief Installs the stencil tuple a primitive topology needs.
         *
         * Direct3D 9 applies the counter-clockwise stencil tuple only to triangles.
         *
         * @param primitive The primitive type about to be drawn.
         */
        CNAEXT void ApplyStencilPrimitiveTopology(PrimitiveType primitive);

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
        // --- Compiled XNA effects (plans/plan_opengl4_modern_graphics.md GL4-0020) -------------
        // Defined in OpenGL4CompiledEffects.cpp. The route is EasyGL's desktop-profile one
        // (plans/plan_fx.md FX-062, FX-080, FX-082, FX-083, FX-088, FX-099, FX-118, FX-128).

        /**
         * @brief Parses a compiled XNA effect for this device.
         *
         * @param effectCode Compiled effect bytes.
         * @param effectCodeBytes Number of bytes at @p effectCode.
         * @return The runtime.
         * @throws std::runtime_error if MojoShader has no context for this device or rejects the
         *         bytes.
         */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /**
         * @brief True: this renderer executes compiled XNA Effect Framework bytecode.
         *
         * Every draw route recognises a compiled effect -- ordinary, indexed, instanced,
         * multi-stream and SpriteBatch -- and a pass's declared `sampler_state` block reaches the
         * GPU. A texture whose dimension does not match the shader's sampler is refused by name.
         *
         * @return true.
         */
        [[nodiscard]] bool SupportsCompiledEffects() const override { return true; }

        /**
         * @brief CNAEXT. Returns this device's MojoShader context, creating it on first use.
         *
         * MojoShader allows one context per GL context, so it is owned here rather than by each
         * effect. It is asked for `MOJOSHADER_PROFILE_GLSL120` explicitly, never for
         * `MOJOSHADER_glBestProfile`, whose `glspirv` choice cannot finalise a pixel-only pass
         * (plans/plan_fx.md FX-128).
         *
         * @return The context, or null if it could not be created.
         */
        CNAEXT [[nodiscard]] MOJOSHADER_glContext* GetMojoShaderContextEXT();

        /**
         * @brief CNAEXT. One vertex stream a compiled-effect draw may read attributes from.
         *
         * A compiled effect's vertex shader declares arbitrary semantics, and XNA lets any of them
         * come from any bound `VertexBufferBinding`; each stream therefore carries its own buffer,
         * stride, starting byte offset and instance frequency (plans/plan_fx.md FX-082).
         */
        struct CompiledEffectStreamEXT
        {
            /** @brief Buffer holding this stream's records; its declaration names the semantics. */
            const OpenGL4VertexBufferRenderer* buffer = nullptr;
            /** @brief Bytes between consecutive records inside this buffer. */
            std::size_t stride = 0;
            /** @brief Byte offset of this stream's first record (its public VertexOffset). */
            std::size_t baseByteOffset = 0;
            /** @brief `InstanceFrequency`; 0 means the stream advances once per vertex. */
            unsigned int instanceFrequency = 0;
            /**
             * @brief Renderer-neutral binding metadata carrying XNA/FNA's effective usage-index
             *        remap, or null for an internal single-stream draw.
             */
            const GpuVertexStreamBinding* binding = nullptr;
        };

        /**
         * @brief CNAEXT. Binds a compiled effect's applied-pass program, vertex attributes, sampler
         *        textures and sampler state, and pushes its uniforms -- everything a compiled draw
         *        needs immediately before the GL draw call.
         *
         * The caller must have bound `EnsureCompiledEffectVaoEXT()` first: MojoShader's OpenGL
         * adapter tracks enabled attribute arrays in its own context state, so every compiled draw
         * goes through one and the same vertex array object.
         *
         * @param streams Bound streams, in public binding-slot order.
         * @param streamCount Number of entries in @p streams; must be at least one.
         * @param runtime The applied compiled effect.
         * @param spriteBatchSlotZeroTexture When non-null, the texture that takes sampler slot 0
         *        regardless of the effect -- SpriteBatch's own rule. Null for ordinary draws.
         * @param deviceTextures When non-null, the owning device's authoritative texture slots.
         * @param deviceSamplerStates When non-null, the matching authoritative sampler states.
         * @param deviceVertexTextures When non-null, the four public vertex texture slots.
         * @param deviceVertexSamplerStates The matching authoritative vertex sampler states.
         * @throws std::runtime_error if the applied pass bound no shader pair, or @p runtime was
         *         not created by this renderer.
         * @throws System::NotSupportedException if no stream supplies an input the vertex shader
         *         consumes, or a reflected sampler has an incompatible texture bound.
         */
        CNAEXT void BindCompiledEffectForDrawEXT(
            const CompiledEffectStreamEXT* streams, std::size_t streamCount,
            ICompiledEffectRuntime& runtime,
            const ITextureRenderer* spriteBatchSlotZeroTexture = nullptr,
            const Microsoft::Xna::Framework::Graphics::TextureCollection* deviceTextures = nullptr,
            const Microsoft::Xna::Framework::Graphics::SamplerStateCollection*
                deviceSamplerStates = nullptr,
            const Microsoft::Xna::Framework::Graphics::TextureCollection*
                deviceVertexTextures = nullptr,
            const Microsoft::Xna::Framework::Graphics::SamplerStateCollection*
                deviceVertexSamplerStates = nullptr);

        /**
         * @brief CNAEXT. The one vertex array object every compiled-effect draw binds.
         *
         * MojoShader's OpenGL adapter remembers which attribute arrays it enabled in its own
         * context state, not per VAO, so routing compiled draws through each vertex buffer's own
         * VAO would both desynchronise that belief and overwrite the layout `ApplyLayout()`
         * installed there (plans/plan_fx.md FX-082).
         *
         * @return The compiled-effect vertex array name, created on first use.
         */
        CNAEXT [[nodiscard]] unsigned int EnsureCompiledEffectVaoEXT();

        /**
         * @brief CNAEXT. Returns a row-order-corrected copy of @p source for a compiled sampler.
         *
         * This renderer never flips geometry for a framebuffer object, so a render target's colour
         * texture stores its rows bottom-up relative to an uploaded `Texture2D`. The stock
         * programs correct that at sampling time (`uRtFlipV`, REMED-GFX-147), but a compiled
         * Effect's GLSL is generated by MojoShader and cannot be given the correction -- so the
         * pixels are corrected instead: the source's colour texture is blitted, Y reversed, into
         * this slot's own copy, and the copy is bound (plans/plan_fx.md FX-099). The copy keeps
         * the source's storage format, its mip chain and its Direct3D 9 channel expansion.
         *
         * @param slot Native texture unit the copy is for; each owns its own copy so two render
         *        targets sampled by one pass cannot overwrite each other.
         * @param source The render target being sampled. Must not be the current draw target.
         * @return The GL name of the corrected copy.
         * @throws System::NotSupportedException if @p slot is out of range, @p source is being
         *         drawn into, or its SurfaceFormat has no render-target storage.
         */
        CNAEXT [[nodiscard]] unsigned int AcquireCompiledEffectFlippedSourceEXT(
            int slot, const OpenGL4RenderTargetRenderer& source);
#endif

    private:
        friend class OpenGL4SpriteBatchRenderer;

        // Context and presentation.
        void EnsureCallingThreadContext();
        void CreateMsaaBuffers(int w, int h);
        void DestroyMsaaBuffers();
        void BindDefaultFramebuffer();
        void ResolveMsaa();
        void EnableDebugOutput();

        // Draw path (OpenGL4StockDraw.cpp).
        static StockProgramShape SelectStockProgramShape(const GpuDrawParams& params);
        OpenGL4StockProgram& SelectProgram(std::size_t stride, const GpuDrawParams& params);
        void EnsureStockProgram(OpenGL4StockProgram& program, StockProgramShape shape, bool dualUv);
        void RequireDeclarationFitsStockProgram(const std::vector<VertexElement>& declaredElements,
                                                std::size_t stride,
                                                const GpuDrawParams& params) const;
        bool ConfigureDeclarationForStockProgram(OpenGL4VertexBufferRenderer& buffer,
                                                 std::size_t stride,
                                                 const GpuDrawParams& params);
        void RestoreDeclarationLayout(OpenGL4VertexBufferRenderer& buffer);
        void BindDrawParams(OpenGL4StockProgram& p, const Matrix& world, const Matrix& view,
                            const Matrix& projection, const GpuDrawParams& params);
        void BindNegativeBaseVertexIndices(const OpenGL4IndexBufferRenderer& ib, int startIndex,
                                           int indexCount, int baseVertex);
        void DrawIndexedWithBaseVertexFallback(const OpenGL4IndexBufferRenderer& ib,
                                               GLenum primitive, int indexCount, GLenum indexType,
                                               const void* indexOffset, int startIndex,
                                               int baseVertex, bool instanced, int instanceCount,
                                               int firstInstance = 0);
        void IssueIndirectDrawEXT(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view,
                                  const Matrix& projection, PrimitiveType primitive,
                                  const IStorageBufferRenderer& argumentBuffer,
                                  int argumentByteOffset, const GpuDrawParams& params);
        void EnsureDefaultTexture(unsigned int& texture, const std::uint8_t rgba[4]);
        void EnsureDefaultBlackCubeTexture();
        void BindDefaultTexture(unsigned int texture, int unit, unsigned int target);

        // State.
        void ApplyCurrentColorWriteMasks();
        void ForceAllColorWriteMasks();
        [[nodiscard]] bool HasRestrictedActiveColorWriteMask() const;
        bool DisableScissorForClear();
        void RestoreScissorAfterClear(bool wasEnabled);
        void RestoreWriteMasksAfterClear(bool depth, bool stencil);
        void ApplyCurrentDepthStencilAvailability();
        void ApplyCurrentDepthBias();
        void FinalizeCurrentMRT();

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
        friend class OpenGL4CompiledEffect;
        // Compiled-effect draw routes (OpenGL4CompiledEffects.cpp); the Draw*Ex entry points
        // dispatch here before any stock-program validation, because a compiled effect's vertex
        // layout is validated against the applied pass's own shader reflection instead.
        void DrawCompiledPrimitivesEXT(const IVertexBufferRenderer& vb, PrimitiveType primitive,
                                       int primitiveCount, const GpuDrawParams& params);
        void DrawCompiledIndexedPrimitivesEXT(const IVertexBufferRenderer& vb,
                                              const IIndexBufferRenderer& ib,
                                              PrimitiveType primitive, int primitiveCount,
                                              bool instanced, int instanceCount,
                                              const GpuDrawParams& params);
        void RegisterCompiledEffectEXT(OpenGL4CompiledEffect* effect);
        void UnregisterCompiledEffectEXT(OpenGL4CompiledEffect* effect);
        /// MojoShader's OpenGL adapter keeps its current context in process-global state; every
        /// entry that reaches it names this device's context first, so two devices cannot cross.
        void MakeMojoShaderContextCurrentEXT() const;
        /// Teardown: releases every live compiled effect's native state, the MojoShader context
        /// and the compiled route's GL objects while this renderer's GL context is still current.
        void ReleaseCompiledEffectResourcesEXT();

        // XNA's four HiDef vertex samplers occupy native units 16..19, after the sixteen pixel
        // samplers: MojoShader's MOJOSHADER_XNA4_VERTEX_TEXTURES layout. GraphicsDevice applies
        // only 0..15; the compiled route is the only writer of the last four.
        static constexpr int kMaxSamplerSlots = 20;
#else
        static constexpr int kMaxSamplerSlots = 16;
#endif

        // Declared before every GL resource-owning member so it outlives those resources.
        std::shared_ptr<PlatformGlContextOwner> platformContext_;

        /** @brief Serialises a background content load against the frame (per-thread lease). */
        struct ThreadContextLeaseControl
        {
            std::shared_ptr<PlatformGlContextOwner> platformContext;
            std::recursive_mutex mutex;
        };
        std::shared_ptr<ThreadContextLeaseControl> threadContextLeaseControl_;

        GlPresentationSurfaceState surfaceState_;
        int swapInterval_ = 1;
        int sampleCount_ = 1;
        int backBufferDepthFormat_ = 3;
        int maxMrtTargets_ = 4;
        float xnaPixelCenterScale_ = 63.0f / 64.0f;
        bool debugOutputEnabled_ = false;

        /// REMED-GFX-168: shared with every render target so a target destroyed while bound can
        /// clear its own slot. Its own allocation, owned here, handed out as weak_ptr.
        std::shared_ptr<OpenGL4BoundTarget> bound_ = std::make_shared<OpenGL4BoundTarget>();
        unsigned int mrtFbo_ = 0;

        unsigned int msaaFbo_ = 0;
        unsigned int msaaColorRbo_ = 0;
        unsigned int msaaDepthRbo_ = 0;
        int msaaW_ = 0;
        int msaaH_ = 0;
        int msaaStorageDepthFormat_ = -1;

        // Recorded render state -- what a clear or a topology change must put back.
        bool depthEnabled_ = true;
        bool depthWriteEnabled_ = true;
        bool stencilEnabled_ = false;
        bool stencilTwoSided_ = false;
        bool stencilPrimitiveUsesTwoSided_ = false;
        int stencilFunc_ = 0;
        int stencilPass_ = 0;
        int stencilFail_ = 0;
        int stencilDepthFail_ = 0;
        int stencilCcwFunc_ = 0;
        int stencilCcwPass_ = 0;
        int stencilCcwFail_ = 0;
        int stencilCcwDepthFail_ = 0;
        int stencilReadMask_ = -1;
        int stencilWriteMask_ = -1;
        int referenceStencil_ = 0;
        std::array<int, 4> currentColorWriteMasks_{15, 15, 15, 15};
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;
        bool fillModeWireframe_ = false;

        // Viewport record (the only glViewport writer is SetGlViewport).
        struct GlViewportShadow
        {
            bool known = false;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };
        mutable GlViewportShadow glViewportShadow_;
        bool viewportIsDefault_ = true;
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;

        /// Driver-granted anisotropic-filtering ceiling (1 when the extension is absent).
        float maxAnisotropy_ = 1.0f;
        /// Native post-4.1 facts; public CNA feature promises remain separate overrides.
        GL4::ModernCapabilities modernCapabilities_;
        /// Format probe caches, owned by the format layer (OpenGL4Formats.cpp).
        mutable OpenGL4FormatSupport formatSupport_;

        // Stock programs.
        OpenGL4StockProgram progColored_;
        OpenGL4StockProgram progTextured_;
        OpenGL4StockProgram progColTextured_;
        OpenGL4StockProgram progLitTextured_;
        OpenGL4StockProgram progLitTexturedVertexLit_;
        OpenGL4StockProgram progDualTextured_;
        OpenGL4StockProgram progDualTexturedColored_;
        OpenGL4StockProgram progEnvMapped_;
        OpenGL4StockProgram progSkinned_;
        OpenGL4StockProgram progSkinnedVertexLit_;
        OpenGL4StockProgram progPbr_;
        OpenGL4StockProgram progPbrDualUv_;
        OpenGL4StockProgram progPbrSkinned_;
        OpenGL4StockProgram progPbrSkinnedDualUv_;

        unsigned int defaultWhiteTexture_ = 0;
        unsigned int defaultBlackTexture_ = 0;
        unsigned int defaultBlackCubeTexture_ = 0;
        unsigned int defaultFlatNormalTexture_ = 0;
        unsigned int negativeBaseVertexIbo_ = 0;
        std::vector<std::uint8_t> negativeBaseVertexScratch_;

        // Sampler objects, one per XNA sampler slot, each bound to its own texture unit.
        unsigned int samplers_[kMaxSamplerSlots] = {};
        /// Nearest/clamp sampler every sampled compute input reads through (GL4-0025).
        unsigned int computeSampler_ = 0;
        /// GL_TIMESTAMP counter width, asked on first use; -1 until then (GL4-0027).
        mutable int timestampCounterBits_ = -1;

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)
        /// One MojoShader GL context for this renderer's lifetime, created on first use.
        MOJOSHADER_glContext* mojoShaderContext_ = nullptr;
        /// Device-wide legacy Direct3D 9 texture-stage values consumed by TEXBEM/L and BEM.
        std::array<CompiledEffectLegacyBumpMapEnvState, 16> compiledLegacyBumpMapEnvs_{};
        /// The one array object every compiled draw binds (EnsureCompiledEffectVaoEXT).
        unsigned int compiledEffectVao_ = 0;
        /// One native unit's row-order-corrected copy of a render target being sampled.
        struct CompiledEffectFlippedSource
        {
            unsigned int texture = 0;
            unsigned int framebuffer = 0;
            int width = 0;
            int height = 0;
            int levelCount = 1;
            int surfaceFormat = -1;
        };
        std::array<CompiledEffectFlippedSource, kMaxSamplerSlots> compiledFlippedSources_{};
        /// Read framebuffer the sampled target's colour texture is attached to per blit, so the
        /// target's own framebuffers -- including a multisample one -- are never touched.
        unsigned int compiledFlipReadFbo_ = 0;
        /// Every live compiled effect of this device, released early if this renderer dies first.
        std::vector<OpenGL4CompiledEffect*> compiledEffects_;
#endif
    };
}
