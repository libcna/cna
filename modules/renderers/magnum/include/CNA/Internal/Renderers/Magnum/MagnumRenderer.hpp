// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "MagnumBuffers.hpp"
#include "MagnumRenderTargets.hpp"
#include "MagnumStateMapping.hpp"
#include "MagnumStockShaders.hpp"
#include "MagnumTextures.hpp"

#include <array>
#include <memory>

namespace Magnum::Platform { class GLContext; }

namespace CNA::Internal::Renderers::Magnum
{
    /**
     * @brief CNA's graphics renderer expressed through Magnum's typed OpenGL wrappers.
     *
     * The platform owns the window and supplies the native GL context service. Magnum is given
     * that already-current context through `Platform::GLContext`; every resource, state change
     * and draw then goes through `Magnum::GL`.
     */
    class MagnumRenderer : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates a platform GL context and initializes Magnum against it.
         *
         * @param args Platform surface, GL service and renderer settings.
         * @throw CNA::Platform::PlatformException if the surface or context is unavailable.
         * @throw std::runtime_error if Magnum cannot initialize against the current context.
         */
        explicit MagnumRenderer(const GraphicsRendererCreateArgs& args);
        ~MagnumRenderer() override;

        /**
         * @brief Reports whether a feature is genuinely available on this renderer.
         *
         * @param capability Feature to query.
         * @return True when the feature is supported.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Clears the colour of the active destination.
         *
         * @param r Red, 0..1.
         * @param g Green, 0..1.
         * @param b Blue, 0..1.
         * @param a Alpha, 0..1.
         */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Presents the back buffer. */
        void Present() override;
        /**
         * @brief Requested vertical retrace interval.
         *
         * @param interval 0 immediate, 1 vsync, 2 half refresh rate.
         */
        void SetSwapInterval(int interval) override;
        /**
         * @brief Logical (game-coordinate) size of the presentation surface.
         *
         * @param width  Receives the logical width.
         * @param height Receives the logical height.
         */
        void GetViewportSize(int& width, int& height) override;
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        /** @brief Refreshes drawable size and display scale for the same platform window. */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        /**
         * @brief Sets the virtual (game-logic) resolution the renderer presents at.
         *
         * @param width  Virtual width, or 0 for none.
         * @param height Virtual height, or 0 for none.
         */
        void SetVirtualResolution(int width, int height) override;
        /**
         * @brief Sets the presentation/scaling policy.
         *
         * @param mode Raw `CnaPresentationMode` ordinal.
         */
        void SetPresentationMode(int mode) override;
        /** @brief Applied back-buffer sample count, or 0 when the back buffer is single-sampled. */
        [[nodiscard]] int GetMultiSampleCount() const override
        {
            return sampleCount_ > 1 ? sampleCount_ : 0;
        }
        /**
         * @brief Converts a physical window point into logical game coordinates.
         *
         * @param windowX Window-space X.
         * @param windowY Window-space Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when a virtual resolution is set and the conversion applies.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        /**
         * @brief Converts a logical game point into physical window coordinates.
         *
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X.
         * @param windowY Receives the window-space Y.
         * @return True when a virtual resolution is set and the conversion applies.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;
        /**
         * @brief Creates a 2D texture from CPU pixels.
         *
         * @param data Source image.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        /** @brief Creates a SpriteBatch renderer bound to this graphics renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        /** @brief Creates a real GPU occlusion query. */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        /**
         * @brief Creates a 2D render target.
         *
         * @param w                 Width in pixels.
         * @param h                 Height in pixels.
         * @param depthFormat       Raw `DepthFormat` ordinal.
         * @param preserveContents  Whether the previous contents survive a rebind; a GL colour
         *                          attachment is never implicitly discarded, so this is satisfied
         *                          by construction.
         * @param mipMap            Whether a mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount  Requested sample count.
         * @return The new render target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;
        /**
         * @brief Creates a cube-map render target.
         *
         * @param size             Edge length of each face in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents See `CreateRenderTarget2D`.
         * @param mipMap           Whether a mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count.
         * @return The new render target renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;
        /**
         * @brief Creates a volume texture.
         *
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether a mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return The new volume texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        /**
         * @brief Creates a cube-map texture.
         *
         * @param size          Edge length of each face in pixels.
         * @param mipMap        Whether a mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return The new cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        /**
         * @brief Compiles a `ShaderEffect`'s GLSL into its own program.
         *
         * @param vertSrc Vertex shader source.
         * @param fragSrc Fragment shader source.
         * @return The new effect renderer, valid only if compilation succeeded.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        /**
         * @brief Directs subsequent draws at a single 2D render target, or at the back buffer.
         *
         * @param rt Target to bind, or nullptr to return to the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /**
         * @brief Directs subsequent draws at one face of a cube-map render target.
         *
         * @param rt   Target to bind, or nullptr to return to the back buffer.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        /**
         * @brief Directs subsequent draws at an ordered set of render targets.
         *
         * @param renderTargets Normalized descriptors, in slot order.
         * @param count         Live entries in @p renderTargets; 0 returns to the back buffer.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        /**
         * @brief Creates a vertex buffer.
         *
         * @param vertex_capacity Vertex elements the buffer was declared with.
         * @return The new vertex buffer renderer.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /**
         * @brief Creates a 16-bit index buffer.
         *
         * @param index_capacity Indices the buffer was declared with.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        /**
         * @brief Creates a 32-bit index buffer.
         *
         * @param index_capacity Indices the buffer was declared with.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;
        /**
         * @brief Reads a rectangle of the back buffer back to the CPU, top row first.
         *
         * @param x      Left edge in pixels.
         * @param y      Top edge in pixels.
         * @param w      Width in pixels.
         * @param h      Height in pixels.
         * @param pixels Destination for tightly packed RGBA8 rows.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Applies the blend stage.
         *
         * @param colorSrcBlend  Raw `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for alpha.
         * @param writeState     Colour write masks and coverage mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        /**
         * @brief Applies the depth/stencil stage.
         *
         * @param depthEnable         Whether depth testing is on.
         * @param depthWriteEnable    Whether depth writes are on.
         * @param depthFunc           Raw `CompareFunction` ordinal for depth.
         * @param stencilEnable       Whether stencil testing is on.
         * @param stencilFunc         Raw `CompareFunction` ordinal for the front face.
         * @param stencilPass         Raw `StencilOperation` for a passing front-face fragment.
         * @param stencilFail         Raw `StencilOperation` for a failing front-face stencil test.
         * @param stencilDepthFail    Raw `StencilOperation` for a failing front-face depth test.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether the counter-clockwise parameters below apply.
         * @param ccwStencilFunc      Raw `CompareFunction` ordinal for the back face.
         * @param ccwStencilPass      Raw `StencilOperation` for a passing back-face fragment.
         * @param ccwStencilFail      Raw `StencilOperation` for a failing back-face stencil test.
         * @param ccwStencilDepthFail Raw `StencilOperation` for a failing back-face depth test.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        /**
         * @brief Applies the rasterizer stage.
         *
         * @param cullMode            Raw `CullMode` ordinal (None=0, CullClockwiseFace=1,
         *                            CullCounterClockwiseFace=2).
         * @param fillMode            Raw `FillMode` ordinal (Solid=0, WireFrame=1).
         * @param scissorTestEnable   Whether the scissor test is on.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        /**
         * @brief Records the sampler state a slot's texture is configured with at bind time.
         *
         * @param slot          Sampler slot.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy `SamplerState.MaxAnisotropy`.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        /**
         * @brief Sets the constant blend colour.
         *
         * @param r Red, 0..1.
         * @param g Green, 0..1.
         * @param b Blue, 0..1.
         * @param a Alpha, 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;
        /**
         * @brief Sets the stencil reference value without changing the rest of the stencil state.
         *
         * @param value Reference value.
         */
        void SetReferenceStencil(int value) override;
        /**
         * @brief Sets the scissor rectangle, converting from XNA's top-left origin.
         *
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;
        /**
         * @brief Sets the viewport, converting from XNA's top-left origin.
         *
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Near depth range value.
         * @param maxDepth Far depth range value.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Clears colour and depth of the active destination.
         *
         * @param r     Red, 0..1.
         * @param g     Green, 0..1.
         * @param b     Blue, 0..1.
         * @param a     Alpha, 0..1.
         * @param depth Depth value.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /**
         * @brief Clears depth only.
         *
         * @param depth Depth value.
         */
        void ClearDepth(float depth) override;
        /**
         * @brief Clears stencil only.
         *
         * @param stencil Stencil value.
         */
        void ClearStencil(int stencil) override;
        /**
         * @brief Clears depth and stencil.
         *
         * @param depth   Depth value.
         * @param stencil Stencil value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /**
         * @brief Clears colour and stencil.
         *
         * @param r       Red, 0..1.
         * @param g       Green, 0..1.
         * @param b       Blue, 0..1.
         * @param a       Alpha, 0..1.
         * @param stencil Stencil value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /**
         * @brief Clears colour, depth and stencil.
         *
         * @param r       Red, 0..1.
         * @param g       Green, 0..1.
         * @param b       Blue, 0..1.
         * @param a       Alpha, 0..1.
         * @param depth   Depth value.
         * @param stencil Stencil value.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a,
                                       float depth, int stencil) override;
        /**
         * @brief Enables or disables depth testing.
         *
         * @param enabled Whether depth testing is on.
         */
        void SetDepthTestEnabled(bool enabled) override;
        /**
         * @brief Enables or disables blending.
         *
         * @param enabled Whether blending is on.
         */
        void SetBlendEnabled(bool enabled) override;
        /**
         * @brief Enables or disables depth writes.
         *
         * @param enabled Whether depth writes are on.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Draws a non-indexed, colour-only primitive batch.
         *
         * @param vb             Vertex buffer to draw from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive type.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Draws an indexed, colour-only primitive batch.
         *
         * @param vb             Vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive type.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Draws a non-indexed batch with full effect parameters.
         *
         * @param vb             Vertex buffer to draw from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive type.
         * @param primitiveCount Number of primitives.
         * @param params         Effect parameters for this draw.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /**
         * @brief Draws an indexed batch with full effect parameters.
         *
         * @param vb             Vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive type.
         * @param primitiveCount Number of primitives.
         * @param params         Effect parameters for this draw.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        /**
         * @brief Draws an indexed batch several times with per-instance streams.
         *
         * @param vb             Per-vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive type.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances.
         * @param params         Effect parameters, including the bound vertex streams.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;

        /**
         * @brief Physical size of the window's drawable surface.
         *
         * @param width  Receives the physical width.
         * @param height Receives the physical height.
         */
        void GetPhysicalSize(int& width, int& height) const;
        /**
         * @brief Logical (game-coordinate) size derived from the presentation mode.
         *
         * @param width  Receives the logical width.
         * @param height Receives the logical height.
         */
        void GetLogicalSize(int& width, int& height) const;
        /**
         * @brief Extent of the bound 2D render target, if one is bound.
         *
         * @param width  Receives the target's width.
         * @param height Receives the target's height.
         * @return True when a single 2D target or a multi-target set is bound.
         */
        [[nodiscard]] bool GetCurrentRenderTarget2DSize(int& width, int& height) const;
        /** @brief The viewport most recently applied, in GL (bottom-left origin) coordinates. */
        [[nodiscard]] Mg::Range2Di GetCurrentViewport() const { return currentViewport_; }
        /**
         * @brief Applies the given viewport in GL coordinates and records it.
         *
         * @param viewport Viewport rectangle with a bottom-left origin.
         */
        void ApplyViewport(const Mg::Range2Di& viewport);
        /**
         * @brief Binds a texture into a slot and configures it with that slot's sampler state.
         *
         * @param slot    Texture unit.
         * @param texture Texture to bind; ignored when null.
         */
        void BindTextureToSlot(int slot, const ITextureRenderer* texture);
        /**
         * @brief Binds a cube map into a slot and configures it with that slot's sampler state.
         *
         * @param slot    Texture unit.
         * @param texture Cube map to bind; ignored when null.
         */
        void BindTextureCubeToSlot(int slot, const ITextureCubeRenderer* texture);
        /** @brief The lazily compiled stock program cache, shared with the SpriteBatch renderer. */
        [[nodiscard]] MagnumStockShaderCache& GetStockShaders() { return *stockShaders_; }
        /** @brief The framebuffer draws currently go to. */
        [[nodiscard]] Mg::GL::AbstractFramebuffer& GetActiveFramebuffer() const;

    private:
        void BindDefaultFramebuffer();
        void FinalizeCurrentTargets();
        void ApplyColorWriteMasks();
        void EnsureDefaultPbrMaps();
        void BindPbrMap(MagnumProgram& program, const char* uniformName, int slot,
                        const ITextureRenderer* texture, Mg::GL::Texture2D& fallback,
                        float& flipOut);
        void ForceAllColorWriteMasks();
        [[nodiscard]] bool HasRestrictedColorWriteMask() const;
        Mg::Vector2i DestinationExtent() const;
        void DrawGeneric(const IVertexBufferRenderer& vb,
                         const IIndexBufferRenderer* ib,
                         const Matrix& world, const Matrix& view, const Matrix& projection,
                         PrimitiveType primitive, int primitiveCount,
                         int instanceCount, const GpuDrawParams& params);
        MagnumProgram* SelectProgram(std::size_t stride, const GpuDrawParams& params);
        void BindDrawParams(MagnumProgram& program,
                            const Matrix& world, const Matrix& view, const Matrix& projection,
                            const GpuDrawParams& params, bool instanced,
                            std::size_t strideInBytes);

        // These precede Magnum and renderer GL resources so both contexts outlive those objects.
        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        PlatformGlSurfaceState surface_;
        std::unique_ptr<::Magnum::Platform::GLContext> magnumContext_;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int sampleCount_ = 1;
        int swapInterval_ = 1;

        static constexpr int kMaxSamplerSlots = 16;
        std::array<MagnumSamplerState, kMaxSamplerSlots> samplers_{};
        std::array<int, 4> colorWriteMasks_{15, 15, 15, 15};
        int referenceStencil_ = 0;
        int stencilReadMask_ = 0xFF;
        int stencilFunction_ = 0;
        /// Tracked so a Clear can force the write on and then restore what the game asked for --
        /// XNA's Clear ignores DepthStencilState's write enables, but the state itself must survive.
        bool depthWriteEnabled_ = true;
        int stencilWriteMask_ = 0xFF;
        bool wireframe_ = false;

        Mg::Range2Di currentViewport_{};
        std::shared_ptr<MagnumBoundTarget> bound_ = std::make_shared<MagnumBoundTarget>();
        /// Reusable framebuffer for an ordered multi-target set; every bind replaces all of its
        /// attachments, so target identity is never part of any cached pipeline state.
        std::unique_ptr<Mg::GL::Framebuffer> mrtFramebuffer_;
        int maxMrtTargets_ = 1;

        /// Held indirectly so the destructor can release the programs it owns BEFORE tearing the
        /// GL context down. Magnum's GL object destructors consult the current context, so a
        /// program outliving the context aborts rather than quietly leaking a handle.
        std::unique_ptr<MagnumStockShaderCache> stockShaders_ =
            std::make_unique<MagnumStockShaderCache>();
        /**
         * Stand-ins for the PbrEffect maps a material does not supply. The shader samples all six
         * unconditionally -- branching per map would cost more than a 1x1 fetch -- so an absent one
         * must resolve to its own neutral value: opaque white for metallic-roughness, emissive and
         * occlusion and both specular-extension maps, and a flat +Z normal for the normal map.
         */
        std::unique_ptr<Mg::GL::Texture2D> defaultWhiteTexture_;
        std::unique_ptr<Mg::GL::Texture2D> defaultFlatNormalTexture_;
    };
}
