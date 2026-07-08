#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <memory>
#include <unordered_map>
#include "CNA/Internal/Graphics/ImageData.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Microsoft::Xna::Framework::Graphics { class Effect; }

namespace CNA::Internal::Backends
{
    using Color = Microsoft::Xna::Framework::Color;
    using Rectangle = Microsoft::Xna::Framework::Rectangle;
    using Vector2 = Microsoft::Xna::Framework::Vector2;
    using SpriteEffects = Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using PrimitiveType = Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Matrix = Microsoft::Xna::Framework::Matrix;
    using Effect = Microsoft::Xna::Framework::Graphics::Effect;
    using ImageData = CNA::Internal::Graphics::ImageData;
    using SetDataOptions = Microsoft::Xna::Framework::Graphics::SetDataOptions;

    /**
     * @brief Backend handle for a vertex buffer.
     *
     * Owned by `Microsoft::Xna::Framework::Graphics::VertexBuffer`. The
     * concrete type is backend-specific (e.g. an OpenGL VBO+VAO pair) and
     * intentionally hidden from the public XNA-like API.
     *
     * @note Status: IMPLEMENTED for VertexPositionColor (stride 16),
     *       VertexPositionTexture (20), VertexPositionColorTexture (24),
     *       and VertexPositionNormalTexture (32) on the EasyGL backend.
     */
    class IVertexBufferBackend
    {
    public:
        virtual ~IVertexBufferBackend() = default;
        /**
         * @brief Uploads `vertex_count` `VertexPositionColor` vertices.
         *
         * @param data         Pointer to a contiguous array of vertices,
         *                     each of size `stride_in_bytes`.
         * @param vertex_count Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         */
        virtual void SetData(const void* data,
                             int vertex_count,
                             std::size_t stride_in_bytes) = 0;

        /**
         * @brief Uploads vertex data with an explicit streaming hint.
         *
         * Backends may use @p options to select a more efficient upload path
         * (e.g. buffer orphaning for `Discard`, `glBufferSubData` for
         * `NoOverwrite`). The default implementation ignores @p options.
         *
         * @param data            Packed vertex data.
         * @param vertex_count    Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         * @param options         Streaming hint.
         */
        virtual void SetDataWithOptions(const void* data,
                                        int vertex_count,
                                        std::size_t stride_in_bytes,
                                        SetDataOptions options)
        {
            SetData(data, vertex_count, stride_in_bytes);
        }

        [[nodiscard]] virtual int GetVertexCount() const = 0;
    };

    /**
     * @brief Backend handle for a 16- or 32-bit index buffer.
     */
    class IIndexBufferBackend
    {
    public:
        virtual ~IIndexBufferBackend() = default;
        virtual void SetData16(const void* data, int index_count) = 0;
        virtual void SetData32(const void* data, int index_count)
        {
            throw std::runtime_error("SetData32 not supported by this backend");
        }

        /** @brief Uploads 16-bit index data with a streaming hint. Default ignores @p options. */
        virtual void SetData16WithOptions(const void* data, int index_count, SetDataOptions options)
        {
            SetData16(data, index_count);
        }

        /** @brief Uploads 32-bit index data with a streaming hint. Default ignores @p options. */
        virtual void SetData32WithOptions(const void* data, int index_count, SetDataOptions options)
        {
            SetData32(data, index_count);
        }

        [[nodiscard]] virtual int  GetIndexCount()   const = 0;
        [[nodiscard]] virtual bool IsThirtyTwoBit()  const { return false; }
    };

    /**
     * @brief Backend handle for a GPU occlusion query.
     *
     * On OpenGL ES 3.0 (EasyGL), uses GL_ANY_SAMPLES_PASSED — so PixelCount()
     * returns 0 (no visible samples) or 1 (at least one visible sample), not an
     * exact pixel count. This matches FNA's behaviour on GLES3.
     */
    class IOcclusionQueryBackend
    {
    public:
        virtual ~IOcclusionQueryBackend() = default;
        virtual void Begin() = 0;
        virtual void End()   = 0;
        [[nodiscard]] virtual bool IsComplete() const = 0;
        [[nodiscard]] virtual int  PixelCount() const = 0;
    };

    /** @brief Backend interface for a cube map texture. */
    class ITextureCubeBackend
    {
    public:
        virtual ~ITextureCubeBackend() = default;
        /** @brief Uploads raw byte data to a sub-rectangle of a single cube face. */
        virtual void SetData(int face, int level, int x, int y, int w, int h,
                             const void* data, int dataLength) = 0;
        /** @brief Reads back raw RGBA8 pixels from a sub-rectangle of a single cube face. No-op by default. */
        virtual void GetData(int face, int level, int x, int y, int w, int h,
                             void* data, int dataLength) const {}
        /// Binds this cube map to the currently active GL texture unit. No-op on non-GL backends.
        virtual void BindGL() const {}
    };

    /** @brief Backend interface for a 3D (volume) texture. */
    class ITexture3DBackend
    {
    public:
        virtual ~ITexture3DBackend() = default;
        /** @brief Uploads raw byte data to a sub-volume of the given mip level. */
        virtual void SetData(int level, int x, int y, int z,
                             int w, int h, int depth,
                             const void* data, int dataLength) = 0;
        /** @brief Reads back raw RGBA8 pixels from a sub-volume of the given mip level. No-op by default. */
        virtual void GetData(int level, int x, int y, int z,
                             int w, int h, int depth,
                             void* data, int dataLength) const {}
    };

    class ITextureBackend
    {
    public:
        virtual ~ITextureBackend() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Texture* GetNativeTexture() const = 0;
        /// Replaces full level-0 texture pixels in-place. stride = row bytes (width * 4 for RGBA).
        virtual void UpdatePixels(const uint8_t* rgba, int stride) {}
        /// Uploads a specific mip level. levelW/levelH are the dimensions at that level.
        virtual void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) {}
        /// Binds the underlying GL texture handle (no-op on non-GL backends).
        virtual void BindGL() const {}
        /// Shares a reference to the CPU pixel buffer owned by Texture2D::cpuPixels_.
        /// The backend stores this reference for OpenGL context-loss restoration instead
        /// of keeping its own duplicate copy of the pixel data.
        virtual void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
    };

    /// Backend handle for a 2D render target (off-screen FBO on EasyGL).
    class IRenderTargetBackend : public ITextureBackend
    {
    public:
        /// Bind the FBO so subsequent draws go to this render target.
        virtual void BindAsRenderTarget() = 0;
        /// Unbind and restore the default framebuffer (back buffer).
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the native GL color texture handle; returns 0 on non-GL backends.
        [[nodiscard]] virtual unsigned int GetColorGLHandle() const { return 0; }
        /// Returns the actual (device-clamped) multisample count this target was created
        /// with; 0 if none/not supported by this backend. Matches FNA's semantics where
        /// RenderTarget2D.MultiSampleCount reflects the real clamped value, not the raw
        /// constructor request (FNA3D_GetMaxMultiSampleCount).
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Returns whether this specific target instance actually has a real depth-stencil
        /// buffer backing it, as opposed to merely being requested via DepthFormat at
        /// construction time. Most backends honor whatever DepthFormat was requested, so the
        /// default mirrors that (via @p depthFormatWasRequested, computed by the caller from
        /// RenderTarget2D::getDepthStencilFormatProperty() != DepthFormat::None). SDL_Renderer's
        /// 2D-only render targets never allocate real depth-buffer storage regardless of what
        /// format was requested, and overrides this to always return false (Task 708).
        [[nodiscard]] virtual bool HasRealDepthBuffer(bool depthFormatWasRequested) const { return depthFormatWasRequested; }
    };

    /// Backend handle for a cube-map render target.
    /// Each face can be activated independently for rendering.
    /// Inherits ITextureCubeBackend so RenderTargetCube can share a single GPU image
    /// with its TextureCube base class (same pattern as IRenderTargetBackend : ITextureBackend).
    class IRenderTargetCubeBackend : public ITextureCubeBackend
    {
    public:
        virtual ~IRenderTargetCubeBackend() = default;
        /// Returns the width/height of each cube face in pixels.
        [[nodiscard]] virtual int GetSize() const = 0;
        /// Activates face @p face (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z) as the draw target.
        virtual void BindAsRenderTargetFace(int face) = 0;
        /// Unbind and restore the default framebuffer.
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the underlying GL texture handle so the cube map can be sampled.
        [[nodiscard]] virtual unsigned int GetGLHandle() const { return 0; }
        /// See IRenderTargetBackend::GetMultiSampleCount.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }

        // ITextureCubeBackend — render targets do not support CPU-side SetData; no-op by default.
        void SetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                     const void* /*data*/, int /*dataLength*/) override {}
    };

    /// Backend handle for a compiled shader program (vertex + fragment).
    /// Created via IGraphicsBackend::CreateEffectBackend().
    class IEffectBackend
    {
    public:
        virtual ~IEffectBackend() = default;
        /// Compiles the program from GLSL/HLSL/SPIR-V sources.
        /// Returns true on success; false if compilation fails (see GetCompileError()).
        virtual bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) = 0;
        /// Binds the program for subsequent draw calls.
        virtual void Bind() = 0;
        /// Unbinds the program (restores default shader).
        virtual void Unbind() = 0;
        /// Returns true if the program has been compiled successfully.
        [[nodiscard]] virtual bool IsValid() const = 0;
        /// Returns the last compilation error string, or empty if no error.
        [[nodiscard]] virtual std::string GetCompileError() const = 0;
        /// Sets a float uniform by name.
        virtual void SetUniformFloat(const char* name, float value) {}
        /// Sets an int uniform by name.
        virtual void SetUniformInt(const char* name, int value) {}
        /// Sets a vec2 uniform by name.
        virtual void SetUniformVec2(const char* name, float x, float y) {}
        /// Sets a vec3 uniform by name.
        virtual void SetUniformVec3(const char* name, float x, float y, float z) {}
        /// Sets a vec4 uniform by name.
        virtual void SetUniformVec4(const char* name, float x, float y, float z, float w) {}
        /// Sets a column-major 4×4 matrix uniform by name.
        virtual void SetUniformMat4(const char* name, const float* matrix) {}
    };

    class ISpriteBatchBackend
    {
    public:
        virtual ~ISpriteBatchBackend() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        /// Sets the transform matrix applied on top of the 2D ortho projection.
        /// Must be called before the first Draw of each Begin/End block.
        virtual void SetTransformMatrix(const Matrix& m) {}
        /// Sets a custom Effect to use for sprite rendering instead of the built-in sprite shader.
        /// Pass nullptr to restore the built-in shader. Must be called before Begin().
        virtual void SetCustomEffect(Effect* effect) {}
        /// Sets the texture filter mode applied to each Draw call.
        /// Passes the raw TextureFilter int value; 0=Linear, 1=Point/Nearest, others map to nearest.
        virtual void SetSamplerFilter(int /*textureFilter*/) {}
        /**
         * @brief Sets the texture address (wrap/clamp/mirror) mode applied to each Draw call.
         *
         * Default: no-op (backend keeps whatever wrap mode the texture was created with, i.e.
         * Clamp on EasyGL).
         *
         * @param addressU Raw TextureAddressMode int value for U (0=Wrap, 1=Clamp, 2=Mirror).
         * @param addressV Raw TextureAddressMode int value for V (0=Wrap, 1=Clamp, 2=Mirror).
         */
        virtual void SetSamplerAddressMode(int /*addressU*/, int /*addressV*/) {}
        virtual void Draw(const ITextureBackend& texture, float x, float y) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth) = 0;
    };

    /**
     * @brief Per-draw effect parameters forwarded from the XNA effect layer
     *        to the graphics backend.
     *
     * Populated via Effect::FillGpuDrawParams() before each draw call so the
     * backend can select and configure the appropriate shader variant.
     */
    struct GpuDrawParams
    {
        const ITextureBackend*     texture0 = nullptr;      ///< Texture unit 0 (diffuse), or null
        const ITextureBackend*     texture1 = nullptr;      ///< Texture unit 1 (DualTextureEffect second layer), or null
        const ITextureCubeBackend* envMap   = nullptr;      ///< Cube map for EnvironmentMapEffect, or null
        float diffuseColor[4]  = {1,1,1,1};                ///< RGBA 0..1
        float ambientColor[3]  = {0,0,0};                   ///< RGB 0..1 (BasicEffect path)
        float light0Dir[3]     = {0,-1,0};                  ///< World-space, pre-normalized
        float light0Diffuse[3] = {1,1,1};                   ///< RGB 0..1
        /// BasicEffect: DirectionalLight1's direction/diffuse, zeroed when disabled. World-space, pre-normalized.
        float light1Dir[3]     = {0,-1,0};
        float light1Diffuse[3] = {0,0,0};
        /// BasicEffect: DirectionalLight2's direction/diffuse, zeroed when disabled. World-space, pre-normalized.
        float light2Dir[3]     = {0,-1,0};
        float light2Diffuse[3] = {0,0,0};
        float worldColMajor[16] = {                         ///< Column-major world matrix
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /// Alpha-test parameters (x=refVal, y=tolerance, z=passWeight, w=failWeight).
        /// Shader evaluates: if ((y>0) ? (|a-x|<y) : (a<x)) ? z : w < 0 → discard.
        /// Default {0,0,1,1} = Always pass (never discard).
        float alphaTest[4]      = {0.0f, 0.0f, 1.0f, 1.0f};
        /// EnvironmentMapEffect: emissive+ambient combined, RGB 0..1.
        /// BasicEffect (lit path only): raw EmissiveColor*Alpha, added after the ambient/light
        /// sum is multiplied by DiffuseColor (CNA folds ambient into that multiply instead of
        /// FNA's pre-baked "ambient+emissive" shader uniform — numerically equivalent net result,
        /// see BasicEffect::FillGpuDrawParams()).
        float emissiveColor[3]  = {0,0,0};
        /// EnvironmentMapEffect: specular tint from env map, RGB 0..1.
        float envMapSpecular[3] = {0,0,0};
        /// EnvironmentMapEffect: camera world-space position for reflection vector.
        /// BasicEffect (lit path only): camera world-space position for specular half-vector.
        float eyePositionWorld[3] = {0,0,0};
        /// BasicEffect: DirectionalLight0/1/2's SpecularColor, zeroed when that light is disabled
        /// (mirrors the light*Diffuse zeroing — FNA's DirectionalLight.Enabled setter zeroes both).
        float light0Specular[3] = {0,0,0};
        float light1Specular[3] = {0,0,0};
        float light2Specular[3] = {0,0,0};
        /// BasicEffect: material SpecularColor, applied once to the summed per-light specular
        /// contribution (not per-light, unlike DiffuseColor which multiplies the light-sum then
        /// is applied once too — both are single material-level multiplies, matching FNA).
        float specularColor[3] = {1,1,1};
        /// BasicEffect: Blinn-Phong specular exponent.
        float specularPower = 16.0f;
        /// EnvironmentMapEffect: blend amount for the env map contribution [0,1].
        float envMapAmount = 0.0f;
        /// EnvironmentMapEffect: when true, the env-map blend factor is Fresnel-weighted
        /// (`pow(max(1-|dot(eye,normal)|,0),fresnelFactor)*envMapAmount`) instead of flat `envMapAmount`.
        bool fresnelEnabled = false;
        /// EnvironmentMapEffect: exponent for the Fresnel edge-weighting term above.
        float fresnelFactor = 1.0f;
        /// SkinnedEffect: column-major mat4 per bone (72 × 16 floats), zero-initialised.
        float boneTransforms[72 * 16] = {};
        /// SkinnedEffect: number of valid entries in boneTransforms (0 = none).
        int boneCount = 0;
        /// BasicEffect fog: when true the fog uniforms below are used.
        bool  fogEnabled      = false;
        /// BasicEffect fog: RGB blend colour.
        float fogColor[3]     = {0, 0, 0};
        /// BasicEffect fog: distance at which fog begins (eye-space Z).
        float fogStart        = 0.0f;
        /// BasicEffect fog: distance at which fog reaches full density (eye-space Z).
        float fogEnd          = 1.0f;
        bool textureEnabled      = false;
        bool vertexColorEnabled  = true;
        bool lightingEnabled     = false;
        /// When true the backend selects a two-sampler DualTexture shader variant.
        bool dualTexture         = false;
        /// When true the backend selects a cube-map env-mapping shader variant.
        bool envMapping          = false;
        /// When true the backend selects the skinning shader variant.
        bool skinned             = false;
        /// Number of instances to draw (1 = non-instanced).
        int instanceCount = 1;
        /// Per-instance vertex buffer backend pointer; cast to the concrete type inside the backend.
        /// Null when not instancing. Only valid for the duration of the DrawInstancedPrimitivesEx call.
        const IVertexBufferBackend* instanceVb = nullptr;
        /// First vertex index for non-indexed draws (maps to glDrawArrays `first` / vkCmdDraw `firstVertex`).
        int vertexStart = 0;
        /// First index in the IBO for indexed draws (maps to glDrawElements byte offset / vkCmdDrawIndexed `firstIndex`).
        int startIndex  = 0;
        /// Value added to each index before vertex fetch (maps to glDrawElementsBaseVertex / vkCmdDrawIndexed `vertexOffset`).
        int baseVertex  = 0;
    };

    class IGraphicsBackend
    {
    public:
        virtual ~IGraphicsBackend() = default;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Present() = 0;
        virtual void GetViewportSize(int& width, int& height) = 0;
        /// Updates the backend logical presentation size at runtime.
        /// Called by GraphicsDevice::SetVirtualResolution() when
        /// GraphicsDeviceManager::ApplyChanges() propagates a new
        /// PreferredBackBufferWidth/Height from the game.
        virtual void SetVirtualResolution(int width, int height) = 0;
        /// Updates the backend presentation/scaling mode at runtime.
        /// Called by GraphicsDevice when GraphicsDeviceManager::ApplyChanges() is used.
        virtual void SetPresentationMode(int mode) = 0;
        /// Updates the swap interval at runtime (0=immediate, 1=VSync, 2=half-rate).
        /// Backends that cannot change VSync at runtime (e.g. Vulkan) silently ignore this.
        virtual void SetSwapInterval(int /*interval*/) {}
        /// Task 902: reconfigures the backbuffer's MSAA sample count in place, called from
        /// GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling (and any other
        /// preference-driven MultiSampleCount change) actually reaches the backend instead of
        /// being silently ignored after construction. Returns the actual, device-clamped sample
        /// count applied (0 = no MSAA). Default: unsupported -- backends that cannot change this
        /// post-construction report back whatever GetMultiSampleCount() already is.
        virtual int ApplyMultiSampleCount(int /*requestedMultiSampleCount*/) { return GetMultiSampleCount(); }
        /// Returns the backbuffer's actual (device-clamped) MSAA sample count; 0 if none/unsupported.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Converts a point from physical window coordinates to logical (virtual)
        /// game coordinates. Returns true on success. Default: no-op (returns false).
        virtual bool TransformWindowToLogical(float windowX, float windowY,
                                              float& logX, float& logY) const { return false; }
        /// Converts a point from logical (virtual) game coordinates to physical window
        /// coordinates — the inverse of TransformWindowToLogical. Returns true on success.
        /// Default: no-op (returns false), i.e. window == logical (no scaling). Used by
        /// Mouse::SetPosition to place the OS cursor correctly on a scaled/letterboxed window.
        virtual bool TransformLogicalToWindow(float logX, float logY,
                                              float& windowX, float& windowY) const { return false; }
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Window* GetWindowInternal() const = 0;
        virtual SDL_Renderer* GetRendererInternal() const = 0;

        virtual std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() = 0;

        /// Reads the rendered backbuffer pixels for the given region into @p pixels (RGBA8).
        /// @p x, @p y are top-left in game coordinates; @p pixels must hold w*h*4 bytes.
        /// Default implementation throws — override in backends that support readback.
        virtual void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
        {
            throw std::runtime_error("ReadBackbuffer: not implemented in this backend");
        }

        /// Creates a backend occlusion query object. Returns nullptr on
        /// backends that do not support hardware occlusion queries.
        virtual std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() { return nullptr; }
        virtual std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) { return nullptr; }
        virtual std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) { return nullptr; }

        /// Creates an off-screen FBO-backed render target. Returns nullptr on
        /// backends that do not support render targets. `depthFormat` is the raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat (None=0, Depth16=1, Depth24=2,
        /// Depth24Stencil8=3), passed as `int` to avoid coupling this backend-agnostic header
        /// to the XNA namespace — mirrors CreateTexture3D/CreateTextureCube's `surfaceFormat`
        /// convention. EasyGL and Bgfx honor the exact requested format (None omits the
        /// depth/stencil attachment entirely); Vulkan always allocates a combined depth+stencil
        /// buffer using its device-wide format regardless of the exact value requested, since
        /// varying it per render target would require a depth-format-keyed render pass/pipeline
        /// cache (Vulkan render-pass-compatibility rules require matching attachment formats
        /// for the pipelines this backend currently shares across the backbuffer and every
        /// render target) — a real architectural change, tracked as Task 911 (Task 877).
        /// `mipMap` requests a full mip chain, auto-generated from level 0 when the target is
        /// unbound (matching FNA3D's OPENGL_ResolveTarget behavior) — all 3 backends implement
        /// this (Task 336/878/906). `multiSampleCount` requests a multisampled color (and depth,
        /// where honored) attachment, resolved into the sampleable texture when the target is
        /// unbound (same FNA3D resolve mechanism; all 3 backends implement this for
        /// RenderTarget2D — Task 337/878/879).
        virtual std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// Activates the given render target (binds its FBO). Pass nullptr to
        /// restore the default back buffer.
        virtual void SetRenderTarget2D(IRenderTargetBackend* rt) {}

        /// Creates a cube-map render target. Returns nullptr on backends that
        /// do not support cube map render targets. See CreateRenderTarget2D for `depthFormat`/
        /// `mipMap`/`multiSampleCount`.
        virtual std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// Compiles a shader program from GLSL/HLSL source strings.
        /// Returns nullptr on backends that do not support programmable shaders.
        virtual std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                                      const std::string& fragSrc)
        { return nullptr; }

        /// Activates a specific face of a cube-map render target for rendering.
        /// Pass nullptr to restore the default back buffer.
        virtual void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
        {
            if (rt) rt->BindAsRenderTargetFace(face);
            else SetRenderTarget2D(nullptr);
        }

        /// Activates multiple render targets for MRT. Default: binds the first
        /// target via SetRenderTarget2D; backends with MRT support should override.
        /// Pass nullptr / count=0 to restore the default back buffer.
        virtual void SetRenderTargets(IRenderTargetBackend* const* rts, int count)
        {
            SetRenderTarget2D(count > 0 ? rts[0] : nullptr);
        }

        // ---- Graphics state ----

        /// Applies a BlendState to the backend. Default: no-op.
        virtual void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                     int colorDstBlend, int alphaDstBlend,
                                     int colorBlendFunc, int alphaBlendFunc) {}

        /// Applies a DepthStencilState to the backend. Default: no-op.
        virtual void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                            int depthFunc,
                                            bool stencilEnable, int stencilFunc,
                                            int stencilPass, int stencilFail, int stencilDepthFail,
                                            int stencilMask, int stencilWriteMask, int referenceStencil,
                                            bool twoSidedStencilMode,
                                            int ccwStencilFunc, int ccwStencilPass,
                                            int ccwStencilFail, int ccwStencilDepthFail) {}

        /// Applies a RasterizerState to the backend. Default: no-op.
        /// @param cullMode            Raw CullMode int value.
        /// @param fillMode            Raw FillMode int value.
        /// @param scissorTestEnable   Whether the scissor test is enabled.
        /// @param depthBias           Constant depth bias (XNA DepthBias).
        /// @param slopeScaleDepthBias Slope-scaled depth bias (XNA SlopeScaleDepthBias).
        virtual void ApplyRasterizerState(int cullMode, int fillMode,
                                          bool scissorTestEnable,
                                          float depthBias = 0.0f,
                                          float slopeScaleDepthBias = 0.0f) {}

        /// Applies a SamplerState to the given texture slot. Default: no-op.
        /// @param slot         Texture unit index (0–15).
        /// @param filter       Raw TextureFilter int value.
        /// @param addressU     Raw TextureAddressMode int value for U.
        /// @param addressV     Raw TextureAddressMode int value for V.
        /// @param maxAnisotropy Maximum anisotropy level (1–16).
        virtual void ApplySamplerState(int slot, int filter,
                                       int addressU, int addressV,
                                       int maxAnisotropy) {}

        /// Sets the constant blend color used with the BlendFactor blend mode.
        /// Maps to glBlendColor on GL backends. Default: no-op.
        virtual void SetBlendFactor(float r, float g, float b, float a) {}

        /// Task 870/319: GraphicsDevice.ReferenceStencil is a real, independent device property
        /// (FNA3D_Get/SetReferenceStencil), analogous to BlendFactor above -- it must take effect
        /// immediately, standalone from a full DepthStencilState re-application. Default: no-op
        /// (backends that only apply ReferenceStencil as part of ApplyDepthStencilState's full
        /// state and don't yet cache it for standalone re-application silently ignore this).
        virtual void SetReferenceStencil(int /*value*/) {}

        /// Sets the scissor clip rectangle. Default: no-op.
        virtual void SetScissorRect(int x, int y, int w, int h) {}

        /// Sets the GPU viewport rectangle and depth range (Task 880). Default: no-op.
        virtual void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) {}

        // ---- 3D pipeline ----

        /**
         * @brief Clears color and depth buffers in a single call.
         *
         * @param r,g,b,a    Clear color in range 0..1.
         * @param depth      Depth value to clear with (0..1).
         */
        virtual void ClearColorAndDepth(float r, float g, float b, float a, float depth) = 0;
        virtual void ClearDepth(float depth) = 0;

        /**
         * @brief Enables or disables depth testing.
         *
         * @note Status: PARTIAL. Only the EasyGL backend honors this; other
         *       backends throw on first 3D usage.
         */
        virtual void SetDepthTestEnabled(bool enabled) = 0;
        virtual void SetBlendEnabled(bool enabled) = 0;
        virtual void SetDepthWriteEnabled(bool enabled) = 0;

        /**
         * @brief Creates a backend-specific vertex buffer for
         *        `VertexPositionColor` data.
         */
        virtual std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) = 0;

        /**
         * @brief Creates a backend-specific 16-bit index buffer.
         */
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) = 0;
        /// Creates a 32-bit index buffer. Default delegates to CreateIndexBuffer16 for
        /// backends that do not yet support 32-bit indices.
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity)
        {
            return CreateIndexBuffer16(index_capacity);
        }

        /**
         * @brief Draws colored primitives from `vb` using the supplied transform.
         *
         * The backend internally applies a basic colored-vertex shader
         * (equivalent to `BasicEffect` with `VertexColorEnabled = true`).
         *
         * @param vb            Vertex buffer to read from.
         * @param world,view,projection Per-draw transform matrices (XNA
         *                              row-major). The combined matrix
         *                              uploaded to the GPU is
         *                              `projection * view * world`.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives (NOT vertices).
         */
        virtual void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                           const Matrix& world,
                                           const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive,
                                           int primitiveCount) = 0;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         */
        virtual void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                  const IIndexBufferBackend& ib,
                                                  const Matrix& world,
                                                  const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive,
                                                  int primitiveCount) = 0;

        /**
         * @brief Effect-aware draw — selects the shader variant based on
         *        vertex layout (derived from stride) and @p params.
         *
         * Default implementation falls back to DrawColoredPrimitives so
         * backends that have not yet implemented this path still work.
         */
        virtual void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                      const Matrix& world,
                                      const Matrix& view,
                                      const Matrix& projection,
                                      PrimitiveType primitive,
                                      int primitiveCount,
                                      const GpuDrawParams& params)
        {
            DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
        }

        /**
         * @brief Indexed counterpart of `DrawPrimitivesEx`.
         */
        virtual void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                             const IIndexBufferBackend& ib,
                                             const Matrix& world,
                                             const Matrix& view,
                                             const Matrix& projection,
                                             PrimitiveType primitive,
                                             int primitiveCount,
                                             const GpuDrawParams& params)
        {
            DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
        }

        /// Instanced indexed draw — default throws on backends that don't support it.
        virtual void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                               const IIndexBufferBackend& ib,
                                               const Matrix& world,
                                               const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive,
                                               int primitiveCount,
                                               int instanceCount,
                                               const GpuDrawParams& params)
        {
            (void)vb; (void)ib; (void)world; (void)view; (void)projection;
            (void)primitive; (void)primitiveCount; (void)instanceCount; (void)params;
            throw std::runtime_error(
                "DrawInstancedPrimitives is not supported on this graphics backend.");
        }

        /// Disables context-loss recovery on the running backend.
        /// Safe to call after backend creation when no resources have been
        /// loaded yet (e.g. from Game1 constructor). Future Create* calls
        /// will skip registry registration and CPU shadow copies.
        virtual void SetContextRecoveryEnabled(bool /*enabled*/) {}

        // ---- Debug / testing ----

        /// Inserts a named GPU debug label into the command stream.
        /// Default implementation is a no-op; Vulkan backend overrides with
        /// vkCmdInsertDebugUtilsLabelEXT when VK_EXT_debug_utils is available.
        virtual void SetStringMarkerEXT(const char* /*marker*/) {}

        /// Simulates an OpenGL context loss.
        /// On Web (Emscripten): triggers WEBGL_lose_context.loseContext().
        /// On desktop: destroys the SDL GL context and immediately recreates it,
        /// forcing all GPU resources to be re-initialised.
        virtual void DebugSimulateContextLoss() {}

        /// Simulates an OpenGL context restore after a previous DebugSimulateContextLoss().
        /// On Web: triggers WEBGL_lose_context.restoreContext().
        /// On desktop: equivalent to DebugSimulateContextLoss() (destroy + recreate).
        virtual void DebugRestoreContext() {}

        // ---- Window → backend registry ----
        // Backends that implement TransformWindowToLogical register themselves here
        // so that SdlInputBridge can map physical mouse coordinates to logical ones
        // even for backends that have no SDL_Renderer (e.g. EasyGL).

        static void RegisterForWindow(SDL_Window* window, IGraphicsBackend* backend)
        {
            windowRegistry()[window] = backend;
        }
        static void UnregisterForWindow(SDL_Window* window)
        {
            windowRegistry().erase(window);
        }
        static IGraphicsBackend* GetForWindow(SDL_Window* window)
        {
            auto& reg = windowRegistry();
            auto it = reg.find(window);
            return it != reg.end() ? it->second : nullptr;
        }

    private:
        static std::unordered_map<SDL_Window*, IGraphicsBackend*>& windowRegistry()
        {
            static std::unordered_map<SDL_Window*, IGraphicsBackend*> reg;
            return reg;
        }
    };

    /**
     * @brief Presentation/scaling policy used when the virtual (game-logic)
     *        resolution differs from the physical surface size.
     *
     * Matches XNA/Windows Phone semantics:
     * - Letterbox            – scale = min(surfW/virtW, surfH/virtH); adds bars.
     * - Overscan             – scale = max(surfW/virtW, surfH/virtH); crops edges.
     * - Stretch              – stretches to fill without preserving aspect ratio.
     * - NativeBackBuffer     – no scaling; game draws at its requested size.
     * - FixedHeightDynamicWidth – keeps the game's preferred height as the
     *                            logical height and computes logical width from
     *                            the actual surface aspect ratio:
     *                              logicalW = round(outputW * preferredH / outputH)
     *                            Then applies LETTERBOX so the computed canvas
     *                            fills the surface perfectly (no bars, no crop).
     *                            This matches XNA/Windows Phone behaviour where
     *                            height=480 is fixed and wider devices simply
     *                            show more horizontal content.
     */
    enum class CnaPresentationMode
    {
        Letterbox = 0,
        Overscan = 1,
        Stretch = 2,
        NativeBackBuffer = 3,
        FixedHeightDynamicWidth = 4
    };

    /**
     * @brief Arguments for creating a graphics backend.
     * Currently minimal, but allows for easier extension.
     */
    struct GraphicsBackendCreateArgs
    {
        // TODO: SDL dependency should be abstracted later
        SDL_Window* window = nullptr;
        /// Virtual (game-logic) resolution the backend should present at.
        /// SDL_SetRenderLogicalPresentation will be set to this size so that
        /// the game always draws in its own coordinate space and the backend
        /// scales to the real surface automatically.
        /// 0 means "unset"; the backend should ignore logical presentation.
        int virtualWidth = 0;
        int virtualHeight = 0;
        /// Presentation/scaling policy. Default is FixedHeightDynamicWidth:
        /// keeps preferred height fixed and derives logical width from the
        /// actual surface aspect ratio, matching XNA/Windows Phone behaviour.
        CnaPresentationMode presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;
        /// When false, the EasyGL backend will not keep CPU-side copies of
        /// texture pixels or vertex/index data and will not register resources
        /// with the ResourceRegistry. This eliminates the per-texture CPU
        /// shadow copy overhead at the cost of making GL context-loss recovery
        /// impossible. Safe on desktop where context loss never occurs.
        bool contextRecoveryEnabled = true;
        /// Desired multisample count (1 = no MSAA, 4 = 4× MSAA, etc.).
        /// Backends that do not support MSAA silently clamp to 1.
        int multiSampleCount = 1;
        /// Swap interval for vertical sync.
        ///   0 = immediate (no VSync)
        ///   1 = wait for 1 vertical retrace (VSync, default)
        ///   2 = wait for 2 vertical retraces (half refresh rate)
        /// Corresponds to PresentInterval: Default/One→1, Two→2, Immediate→0.
        int swapInterval = 1;
    };

    // Factory function to be implemented by each backend
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args);
}
