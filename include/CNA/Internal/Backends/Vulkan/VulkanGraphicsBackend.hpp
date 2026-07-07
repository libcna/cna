#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace CNA::Internal::Backends::Vulkan
{
    class VulkanGraphicsBackend;            // forward
    class VulkanRenderTargetBackend;        // forward
    class VulkanRenderTargetCubeBackend;    // forward

    // -------------------------------------------------------------------------
    // Vertex types (internal to the Vulkan backend)
    // -------------------------------------------------------------------------

    struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };

    // -------------------------------------------------------------------------
    // VulkanRTSource — minimal interface used by RecordCommandBuffer
    // -------------------------------------------------------------------------

    struct VulkanRTSource {
        virtual VkFramebuffer GetFramebuffer()             const = 0;
        virtual VkRenderPass  GetRenderPass()              const = 0;
        virtual int           GetWidth()                   const = 0;
        virtual int           GetHeight()                  const = 0;
        virtual uint32_t      GetColorAttachmentCount()    const = 0;
        /// True if this RT source's actual bound framebuffer/render pass this frame is the
        /// 3-attachment MSAA variant (MSAA color + resolve + MSAA depth) rather than the plain
        /// single-sample one. Default false; overridden by VulkanRenderTargetBackend when it
        /// actually engaged MSAA (Task 878/879 — see the "piggyback on the backend's own
        /// sampleCount_" scope decision in plan_graphics.md).
        virtual bool          WantsMsaa()                   const { return false; }
        /// Task 878: regenerate this RT's mip chain (no-op unless the concrete RT actually owns
        /// mip levels beyond 0). Called by RecordCommandBuffer right after this RT's render pass
        /// ends, once per frame it was actually rendered into.
        virtual void          MaybeGenerateMips(VkCommandBuffer /*cb*/) {}
        virtual ~VulkanRTSource() = default;
    };

    // -------------------------------------------------------------------------
    // IVulkanSamplable — common interface for any Vulkan object that can be
    // bound as a sampled texture (regular Texture2D and RenderTarget2D).
    // -------------------------------------------------------------------------

    struct IVulkanSamplable
    {
        virtual ~IVulkanSamplable() = default;
        virtual VkDescriptorSet GetVkDescriptorSet() const = 0;
        virtual VkImageView     GetVkImageView()     const = 0;
    };

    // -------------------------------------------------------------------------
    // IVulkanCubeSamplable — common interface for Vulkan objects that can be
    // sampled as a cube map (TextureCube and RenderTargetCube).
    // -------------------------------------------------------------------------

    struct IVulkanCubeSamplable
    {
        virtual ~IVulkanCubeSamplable() = default;
        virtual VkImageView GetVkCubeImageView() const = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanTextureBackend
    // -------------------------------------------------------------------------

    class VulkanTextureBackend : public ITextureBackend, public IVulkanSamplable
    {
    public:
        explicit VulkanTextureBackend(const ImageData& data, VulkanGraphicsBackend* owner);
        ~VulkanTextureBackend() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        VkDescriptorSet GetDescriptorSet()       const { return descriptorSet_; }
        VkImageView     GetImageView()           const { return imageView_; }
        VkDescriptorSet GetVkDescriptorSet()     const override { return descriptorSet_; }
        VkImageView     GetVkImageView()         const override { return imageView_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;

    private:
        int                 width_         = 0;
        int                 height_        = 0;
        VkImage             image_         = VK_NULL_HANDLE;
        VkDeviceMemory      memory_        = VK_NULL_HANDLE;
        VkImageView         imageView_     = VK_NULL_HANDLE;
        VkDescriptorSet     descriptorSet_ = VK_NULL_HANDLE;
        VulkanGraphicsBackend* owner_      = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanRenderTargetBackend
    // -------------------------------------------------------------------------

    class VulkanRenderTargetBackend : public IRenderTargetBackend, public VulkanRTSource,
                                      public IVulkanSamplable
    {
    public:
        // Task 877: `depthFormat` (raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal)
        // is accepted for interface uniformity with the other 2 backends but not yet acted upon
        // here — every Vulkan render target always allocates a combined depth+stencil buffer
        // using the device-wide depthFormat_ (see VulkanGraphicsBackend::FindDepthFormat()),
        // the same format shared by the backbuffer and every other render target so that
        // pipelines can be reused across renderPass_/rtRenderPass_/rtRenderPassMsaa_ (Vulkan
        // render-pass-compatibility rules require matching attachment formats). Varying the
        // depth/stencil format per render target would need a depth-format-keyed render
        // pass/pipeline cache across every GetOrCreatePipelineXXX call site — a genuine
        // architectural change, tracked as Task 911 rather than attempted opportunistically here.
        VulkanRenderTargetBackend(int w, int h, int depthFormat, bool preserveContents,
                                   VulkanGraphicsBackend* owner, int requestedMultiSampleCount = 0,
                                   bool mipMap = false);
        ~VulkanRenderTargetBackend() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

        VkFramebuffer   GetFramebuffer()          const override;
        VkRenderPass    GetRenderPass()            const override;
        uint32_t        GetColorAttachmentCount()  const override { return 1; }
        // Task 878/879: true once this instance actually engaged MSAA (msaaFramebuffer_ created).
        bool            WantsMsaa()                const override { return msaaFramebuffer_ != VK_NULL_HANDLE; }
        // Real, backend-clamped applied MultiSampleCount (0 if MSAA wasn't engaged — see the
        // "piggyback on the backend's own sampleCount_" scope decision in plan_graphics.md).
        int             GetMultiSampleCount()      const override { return appliedMultiSampleCount_; }
        VkDescriptorSet GetDescriptorSet()         const { return descriptorSet_; }
        VkImageView     GetColorView()             const { return colorView_; }
        VkImageView     GetDepthView()             const { return depthView_; }
        VkDescriptorSet GetVkDescriptorSet()       const override { return descriptorSet_; }
        // Task 878: the full-mip-range view, so mip filtering works when this RT is sampled as
        // an ordinary texture (on-the-fly descriptor sets built by RecordCommandBuffer's texture
        // dispatch), not just via GetDescriptorSet()'s own precreated one.
        VkImageView     GetVkImageView()           const override { return colorSampleView_; }

        // Task 878: regenerates the full mip chain (levels 1..levelCount_-1) from level 0's
        // just-rendered content via a vkCmdBlitImage cascade, mirroring EasyGL's
        // glGenerateMipmap-on-unbind behavior (Task 336) / FNA3D's OPENGL_ResolveTarget. Called
        // once per frame this RT was actually rendered into, right after its render pass ends
        // (see VulkanGraphicsBackend::RecordCommandBuffer). No-op when mipMap was false.
        void MaybeGenerateMips(VkCommandBuffer cb) override;

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        int                     width_            = 0;
        int                     height_           = 0;
        bool                    preserveContents_ = false;
        // Task 878: number of mip levels colorImage_ actually owns (1 when mipMap was false).
        int                     levelCount_   = 1;
        VkImage                 colorImage_   = VK_NULL_HANDLE;
        VkDeviceMemory          colorMemory_  = VK_NULL_HANDLE;
        VkImageView             colorView_    = VK_NULL_HANDLE; ///< mip 0 only — framebuffer color attachment.
        VkImageView             colorSampleView_ = VK_NULL_HANDLE; ///< all levelCount_ levels — descriptor/sampling view.
        VkImage                 depthImage_   = VK_NULL_HANDLE;
        VkDeviceMemory          depthMemory_  = VK_NULL_HANDLE;
        VkImageView             depthView_    = VK_NULL_HANDLE;
        VkFramebuffer           framebuffer_  = VK_NULL_HANDLE;
        // --- MSAA resources (Task 878/879), only created when MSAA was actually engaged: an
        // MSAA color image (attached, never sampled directly) resolved automatically into
        // colorImage_ at vkCmdEndRenderPass, plus a dedicated 3-attachment framebuffer against
        // the owner's shared rtRenderPassMsaa_. depthImage_/depthView_ above are reused in-place
        // as the MSAA depth attachment (promoted to owner_->sampleCount_ samples) rather than
        // duplicated, since depthView_ is never sampled externally by anything in this codebase.
        VkImage                 msaaColorImage_  = VK_NULL_HANDLE;
        VkDeviceMemory          msaaColorMemory_ = VK_NULL_HANDLE;
        VkImageView             msaaColorView_   = VK_NULL_HANDLE;
        VkFramebuffer           msaaFramebuffer_ = VK_NULL_HANDLE;
        int                     appliedMultiSampleCount_ = 0;
        VkDescriptorSet         descriptorSet_ = VK_NULL_HANDLE;
        VulkanGraphicsBackend*  owner_        = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanEffectBackend (Task 119 — SPIR-V custom Effect for Vulkan)
    // -------------------------------------------------------------------------

    class VulkanEffectBackend : public IEffectBackend
    {
    public:
        explicit VulkanEffectBackend(VulkanGraphicsBackend* owner);
        ~VulkanEffectBackend() override;

        // vertSpv / fragSpv are raw SPIR-V bytecode stored in std::string.
        bool CompileProgram(const std::string& vertSpv, const std::string& fragSpv) override;
        void Bind()   override;
        void Unbind() override;
        [[nodiscard]] bool        IsValid()        const override;
        [[nodiscard]] std::string GetCompileError() const override;

        // Named-uniform helpers map to fixed push-constant slots (see contract below).
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;

        VkPipeline       GetPipeline()       const { return pipeline_;       }
        VkPipelineLayout GetPipelineLayout() const { return pipelineLayout_; }
        // Returns pointer to 128-byte push-constant staging area (floats 2..31 = user uniforms).
        const float*     GetPushConst()      const { return pushConst_;      }

    private:
        VulkanGraphicsBackend* owner_;
        VkShaderModule   vertModule_     = VK_NULL_HANDLE;
        VkShaderModule   fragModule_     = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline       pipeline_       = VK_NULL_HANDLE;
        std::string      compileError_;
        // Push-constant staging: 32 floats = 128 bytes.
        // GLSL std140 layout (push_constant block) requires mat4 alignment=16, so
        // after vec2 vpSize (8 bytes) there are 8 bytes of padding before mat4.
        // [0..1]   = vpSize     (bytes  0- 7) — set by sprite batch at draw time.
        // [2..3]   = padding    (bytes  8-15) — unused, zero.
        // [4..19]  = uMatrix    (bytes 16-79) — set via SetUniformMat4.
        // [20..23] = uColor     (bytes 80-95) — set via SetUniformVec4 / Vec3 / Vec2.
        // [24..31] = uFloats×8  (bytes 96-127)— set via SetUniformFloat / Int.
        float            pushConst_[32]  = {};
    };

    // -------------------------------------------------------------------------
    // VulkanSpriteBatchBackend
    // -------------------------------------------------------------------------

    class VulkanSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        explicit VulkanSpriteBatchBackend(VulkanGraphicsBackend* backend);
        ~VulkanSpriteBatchBackend() override = default;

        void Begin() override;
        void End()   override;

        void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }

        // Task 665 fix: previously unoverridden (silent no-op via the base class's default
        // empty bodies). Stores pending values applied via VulkanGraphicsBackend::
        // ApplySamplerState(0, ...) (Task 118's existing per-slot VkSampler cache) at flush
        // time, mirroring EasyGLSpriteBatchBackend's exact pendingFilter_/pendingAddressU_/
        // pendingAddressV_ pattern.
        void SetSamplerFilter(int textureFilter) override { pendingFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            pendingAddressU_ = addressU;
            pendingAddressV_ = addressV;
        }

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects,
                  float layerDepth) override;

        struct DrawCall { VkDescriptorSet descSet; uint32_t firstIndex; uint32_t indexCount; };

        // A fully independent, frame-lifetime record of one Begin()/End() cycle's geometry.
        // Pushed onto VulkanGraphicsBackend::activeBatches_ at End() (not Begin()) so that a
        // 2nd Begin()/Draw()/End() cycle on the same SpriteBatch within one frame can never
        // clobber the 1st cycle's already-completed data before RecordCommandBuffer() harvests
        // it at Present() (Task 664 fix — see plan_graphics.md).
        struct BatchSnapshot
        {
            std::vector<Sprite2DVertex> vertices;
            std::vector<uint16_t>       indices;
            std::vector<DrawCall>       draws;
            VulkanEffectBackend*        customEffectBackend = nullptr;
        };

    private:
        VulkanGraphicsBackend*           backend_             = nullptr;
        bool                             active_              = false;
        Effect*                          customEffect_        = nullptr;
        VulkanEffectBackend*             customEffectBackend_ = nullptr;
        std::vector<Sprite2DVertex>      vertices_;
        std::vector<uint16_t>            indices_;
        std::vector<DrawCall>            draws_;
        const IVulkanSamplable*          currentTexture_      = nullptr;
        uint32_t                         batchFirstIndex_     = 0;
        VulkanRTSource*                  activeRT_            = nullptr;

        // Task 665 fix: pending SamplerState, applied at flush time (mirrors EasyGL's exact
        // field names/defaults — SamplerState.LinearClamp is SpriteBatch's own real default).
        int                              pendingFilter_       = 0; // TextureFilter::Linear
        int                              pendingAddressU_     = 1; // TextureAddressMode::Clamp
        int                              pendingAddressV_     = 1; // TextureAddressMode::Clamp

        void FlushTexture();
    };

    // -------------------------------------------------------------------------
    // VulkanVertexBufferBackend
    // -------------------------------------------------------------------------

    class VulkanVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        explicit VulkanVertexBufferBackend(int vertex_capacity, VulkanGraphicsBackend* owner);
        ~VulkanVertexBufferBackend() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        int  GetVertexCount() const override { return vertexCount_; }

        VkBuffer    GetBuffer()    const { return buffer_; }
        int         GetCapacity()  const { return capacity_; }
        const void* GetMappedPtr() const { return mappedPtr_; }
        std::size_t GetStride()    const { return stride_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        VkBuffer                buffer_      = VK_NULL_HANDLE;
        VkDeviceMemory          memory_      = VK_NULL_HANDLE;
        void*                   mappedPtr_   = nullptr;
        int                     capacity_    = 0;
        int                     vertexCount_ = 0;
        std::size_t             stride_      = 0;
        VulkanGraphicsBackend*  owner_       = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanIndexBufferBackend
    // -------------------------------------------------------------------------

    class VulkanIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        explicit VulkanIndexBufferBackend(int index_capacity, bool thirtyTwoBit,
                                          VulkanGraphicsBackend* owner);
        ~VulkanIndexBufferBackend() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        int  GetIndexCount()  const override { return indexCount_; }
        bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        VkBuffer    GetBuffer()    const { return buffer_; }
        const void* GetMappedPtr() const { return mappedPtr_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

    private:
        VkBuffer                buffer_        = VK_NULL_HANDLE;
        VkDeviceMemory          memory_        = VK_NULL_HANDLE;
        void*                   mappedPtr_     = nullptr;
        int                     capacity_      = 0;
        int                     indexCount_    = 0;
        bool                    thirtyTwoBit_  = false;
        VulkanGraphicsBackend*  owner_         = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanTexture3DBackend
    // -------------------------------------------------------------------------

    class VulkanTexture3DBackend : public ITexture3DBackend
    {
    public:
        VulkanTexture3DBackend(VulkanGraphicsBackend* owner, int w, int h, int depth);
        ~VulkanTexture3DBackend() override;

        void SetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     const void* data, int dataLength) override;

    private:
        VulkanGraphicsBackend* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int width_ = 0, height_ = 0, depth_ = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanTextureCubeBackend
    // -------------------------------------------------------------------------

    class VulkanTextureCubeBackend : public ITextureCubeBackend, public IVulkanCubeSamplable
    {
    public:
        VulkanTextureCubeBackend(VulkanGraphicsBackend* owner, int size);
        ~VulkanTextureCubeBackend() override;

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;

        /** @brief Returns the Vulkan cube image view for sampling. */
        [[nodiscard]] VkImageView GetImageView()        const { return imageView_; }
        [[nodiscard]] VkImageView GetVkCubeImageView()  const override { return imageView_; }

    private:
        VulkanGraphicsBackend* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int size_ = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanOcclusionQueryBackend
    // -------------------------------------------------------------------------

    class VulkanOcclusionQueryBackend : public IOcclusionQueryBackend
    {
    public:
        explicit VulkanOcclusionQueryBackend(VulkanGraphicsBackend* owner);
        ~VulkanOcclusionQueryBackend() override;

        void Begin() override;
        void End()   override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;

    private:
        VulkanGraphicsBackend*  owner_      = nullptr;
        VkQueryPool             pool_       = VK_NULL_HANDLE;
        bool                    ended_      = false;
        mutable int             pixelCount_ = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanRenderTargetCubeBackend
    // -------------------------------------------------------------------------

    class VulkanRenderTargetCubeBackend : public IRenderTargetCubeBackend,
                                          public IVulkanCubeSamplable
    {
    public:
        VulkanRenderTargetCubeBackend(VulkanGraphicsBackend* owner, int size, bool mipMap = false);
        ~VulkanRenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;

        // IVulkanCubeSamplable — returns a VK_IMAGE_VIEW_TYPE_CUBE view over all 6 faces.
        [[nodiscard]] VkImageView GetVkCubeImageView() const override { return cubeView_; }

    private:
        // Task 907: per-face proxy also knows how to regenerate its OWN layer's mip chain
        // (levels 0..levelCount-1 of the shared 6-layer `image_`, layer = faceIndex) via a
        // vkCmdBlitImage cascade, mirroring VulkanRenderTargetBackend::MaybeGenerateMips (Task 878).
        struct FaceProxy : public VulkanRTSource {
            VkFramebuffer framebuffer  = VK_NULL_HANDLE;
            VkRenderPass  renderPass   = VK_NULL_HANDLE;
            int           size         = 0;
            VkImage       image        = VK_NULL_HANDLE;
            int           levelCount   = 1;
            int           faceIndex    = 0;
            VkFramebuffer GetFramebuffer()          const override { return framebuffer; }
            VkRenderPass  GetRenderPass()            const override { return renderPass; }
            int GetWidth()                          const override { return size; }
            int GetHeight()                         const override { return size; }
            uint32_t GetColorAttachmentCount()      const override { return 1; }
            void MaybeGenerateMips(VkCommandBuffer cb) override;
        };

        VulkanGraphicsBackend*     owner_     = nullptr;
        VkImage                    image_     = VK_NULL_HANDLE;
        VkDeviceMemory             memory_    = VK_NULL_HANDLE;
        VkImageView                cubeView_  = VK_NULL_HANDLE;   ///< Full-cube view for sampling.
        std::array<VkImageView, 6> faceViews_ = {};
        VkImage                    depthImage_  = VK_NULL_HANDLE;
        VkDeviceMemory             depthMemory_ = VK_NULL_HANDLE;
        VkImageView                depthView_   = VK_NULL_HANDLE;
        std::array<VkFramebuffer, 6> framebuffers_ = {};
        std::array<FaceProxy, 6>     faceProxies_;
        int                        size_      = 0;
        int                        levelCount_ = 1;
    };

    // -------------------------------------------------------------------------
    // VulkanMRTProxy — combines N VulkanRenderTargetBackend into one RT source
    // -------------------------------------------------------------------------

    class VulkanMRTProxy : public VulkanRTSource
    {
    public:
        VulkanMRTProxy(VulkanGraphicsBackend* owner,
                       VulkanRenderTargetBackend* const* rts, uint32_t count);
        ~VulkanMRTProxy() override;

        VkFramebuffer GetFramebuffer()          const override { return framebuffer_; }
        VkRenderPass  GetRenderPass()            const override { return renderPass_; }
        int           GetWidth()                const override { return width_; }
        int           GetHeight()               const override { return height_; }
        uint32_t      GetColorAttachmentCount() const override { return colorCount_; }

    private:
        VulkanGraphicsBackend* owner_       = nullptr;
        VkRenderPass           renderPass_  = VK_NULL_HANDLE;
        VkFramebuffer          framebuffer_ = VK_NULL_HANDLE;
        int                    width_       = 0;
        int                    height_      = 0;
        uint32_t               colorCount_  = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanGraphicsBackend
    // -------------------------------------------------------------------------

    class VulkanGraphicsBackend : public IGraphicsBackend
    {
        friend class VulkanTextureBackend;
        friend class VulkanVertexBufferBackend;
        friend class VulkanIndexBufferBackend;
        friend class VulkanSpriteBatchBackend;
        friend class VulkanEffectBackend;
        friend class VulkanRenderTargetBackend;
        friend class VulkanOcclusionQueryBackend;
        friend class VulkanTexture3DBackend;
        friend class VulkanTextureCubeBackend;
        friend class VulkanRenderTargetCubeBackend;
        friend class VulkanMRTProxy;

    public:
        explicit VulkanGraphicsBackend(SDL_Window* window, int multiSampleCount = 1, int swapInterval = 1);
        ~VulkanGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int)       override {}

        SDL_Window*  GetWindowInternal()   const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend>         CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend>     CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend>    CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void                                     SetRenderTarget2D(IRenderTargetBackend* rt) override;

        // ---- Graphics state: IMPLEMENTED ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DBackend>  CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;

        // ---- Extended 3D draws (textured + lit) ----
        void DrawPrimitivesEx(const IVertexBufferBackend&,
                              const Matrix&, const Matrix&, const Matrix&,
                              PrimitiveType, int, const GpuDrawParams&) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                     const Matrix&, const Matrix&, const Matrix&,
                                     PrimitiveType, int, const GpuDrawParams&) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                       const Matrix&, const Matrix&, const Matrix&,
                                       PrimitiveType, int, int, const GpuDrawParams&) override;

        void ClearColorAndDepth(float, float, float, float, float) override;
        void ClearDepth(float) override;
        void SetDepthTestEnabled(bool)  override;
        void SetBlendEnabled(bool)      override;
        void SetDepthWriteEnabled(bool) override;
        void SetStringMarkerEXT(const char* marker) override;
        std::unique_ptr<IVertexBufferBackend>  CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend>   CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend>   CreateIndexBuffer32(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend&,
                                   const Matrix&, const Matrix&, const Matrix&,
                                   PrimitiveType, int) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                          const IIndexBufferBackend&,
                                          const Matrix&, const Matrix&, const Matrix&,
                                          PrimitiveType, int) override;

    private:
        // --- Constants ---
        static constexpr int      MaxFramesInFlight = 2;
        static constexpr uint32_t MaxSpriteVertices = 32768;
        static constexpr uint32_t MaxSpriteIndices  = MaxSpriteVertices / 4 * 6;
        static constexpr uint32_t MaxDescriptorSets = 512;

        // --- Core Vulkan objects (lifetime = backend) ---
        SDL_Window*      window_         = nullptr;
        VkInstance       instance_       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice         device_         = VK_NULL_HANDLE;

        uint32_t graphicsQueueFamily_ = 0;
        uint32_t presentQueueFamily_  = 0;
        VkQueue  graphicsQueue_       = VK_NULL_HANDLE;
        VkQueue  presentQueue_        = VK_NULL_HANDLE;

        VkCommandPool                commandPool_    = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;

        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence>     inFlightFences_;

        // --- MSAA state (set at construction, fixed for backend lifetime) ---
        VkSampleCountFlagBits    sampleCount_     = VK_SAMPLE_COUNT_1_BIT;
        VkRenderPass             renderPassMsaa_  = VK_NULL_HANDLE;  // 3-attachment MSAA pass; null when sampleCount_==1
        VkImage                  msaaColorImage_  = VK_NULL_HANDLE;
        VkDeviceMemory           msaaColorMemory_ = VK_NULL_HANDLE;
        VkImageView              msaaColorView_   = VK_NULL_HANDLE;
        VkPipeline               pipeline2DMsaa_  = VK_NULL_HANDLE;

        // --- Swap interval (set at construction; Vulkan requires swapchain recreation to change) ---
        int swapInterval_ = 1;

        // --- Swapchain (recreated on resize) ---
        VkSwapchainKHR           swapchain_       = VK_NULL_HANDLE;
        VkFormat                 swapchainFormat_ = VK_FORMAT_UNDEFINED;
        VkExtent2D               swapchainExtent_ = {};
        std::vector<VkImage>     swapchainImages_;
        std::vector<VkImageView> swapchainImageViews_;

        // --- Render pass (permanent) + framebuffers (per swapchain image) ---
        VkRenderPass               renderPass_       = VK_NULL_HANDLE;
        VkRenderPass               rtRenderPass_     = VK_NULL_HANDLE;  // LOAD_OP_CLEAR, color → SHADER_READ_ONLY_OPTIMAL
        VkRenderPass               rtRenderPassLoad_ = VK_NULL_HANDLE;  // LOAD_OP_LOAD,  color → SHADER_READ_ONLY_OPTIMAL
        // Task 878/879: shared 3-attachment (MSAA color/resolve/MSAA depth) RT render pass, lazily
        // created the first time a VulkanRenderTargetBackend actually engages MSAA. Reused by every
        // MSAA-enabled RT instance's own per-instance framebuffer (mirrors rtRenderPass_'s existing
        // lazy-shared-singleton pattern). DiscardContents-shaped only — see VulkanRenderTargetBackend
        // constructor comment for why PreserveContents+MSAA doesn't get its own LOAD_OP_LOAD variant.
        VkRenderPass               rtRenderPassMsaa_ = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchainFramebuffers_;

        // --- Depth buffer (recreated with swapchain) ---
        VkFormat        depthFormat_    = VK_FORMAT_UNDEFINED;
        VkImage         depthImage_     = VK_NULL_HANDLE;
        VkDeviceMemory  depthMemory_    = VK_NULL_HANDLE;
        VkImageView     depthImageView_ = VK_NULL_HANDLE;

        // --- Sampler cache: one VkSampler per unique (filter,addrU,addrV,aniso) tuple ---
        struct SamplerStateKey {
            int filter, addressU, addressV, maxAnisotropy;
            bool operator<(const SamplerStateKey& o) const noexcept {
                if (filter     != o.filter)     return filter     < o.filter;
                if (addressU   != o.addressU)   return addressU   < o.addressU;
                if (addressV   != o.addressV)   return addressV   < o.addressV;
                return maxAnisotropy < o.maxAnisotropy;
            }
        };
        std::map<SamplerStateKey, VkSampler>                         samplerCache_;
        VkSampler                                                     slotSamplers_[16] = {};
        std::map<std::pair<VkImageView,VkSampler>, VkDescriptorSet>  texSamplerDescSets_;
        bool anisotropySupported_ = false;
        float maxSamplerAnisotropy_ = 1.f;

        // --- Pipeline resources (permanent) ---
        VkSampler             defaultSampler_        = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_   = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool_        = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout2D_      = VK_NULL_HANDLE;
        VkPipeline            pipeline2D_            = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout3D_      = VK_NULL_HANDLE;
        std::unordered_map<uint32_t, VkPipeline>             pipelines3D_;
        VkPipelineLayout      pipelineLayoutExt3D_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutAlphaTest3D_ = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesAlphaTest3D_;
        VkDescriptorSetLayout descriptorSetLayout2Tex_     = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool2Tex_          = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutDualTex3D_     = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesDualTex3D_;
        // Task 899: per-frame cache (was a single flat map) -- binding=2's fog UBO now makes the
        // descriptor set's buffer binding frame-specific, mirroring litTexturedDescSets_/skinnedDescSets_.
        std::array<std::unordered_map<uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                        dualTexDescSets_;
        // Task 899: per-frame UBO ring buffer for DualTextureEffect fog (binding=2, dynamic).
        static constexpr uint32_t kDualTexFogUBOStride   = 256;
        static constexpr uint32_t kDualTexFogUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> dualTexFogUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> dualTexFogUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> dualTexFogUBOPtr_ = {};
        // EnvironmentMapEffect resources
        VkDescriptorSetLayout descriptorSetLayoutEnvMap_   = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolEnvMap_        = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutEnvMap3D_      = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesEnvMap3D_;
        // Per-frame descriptor set cache: key = hash(view2D, viewCube)
        std::array<std::unordered_map<uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                        envMapDescSets_;
        // Per-frame UBO ring buffer for env map FS params (world+eye+lighting)
        static constexpr uint32_t kEnvMapUBOStride   = 256; // 96 bytes used, padded to 256
        static constexpr uint32_t kEnvMapUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> envMapUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> envMapUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> envMapUBOPtr_ = {};
        // Default 1×1 white cube image for fallback when env map texture is null
        VkImage               defaultWhiteCubeImage_  = VK_NULL_HANDLE;
        VkDeviceMemory        defaultWhiteCubeMem_    = VK_NULL_HANDLE;
        VkImageView           defaultWhiteCubeView_   = VK_NULL_HANDLE;
        // BasicEffect lit-textured resources (Task 897: DirectionalLight1/2 + EmissiveColor)
        VkDescriptorSetLayout descriptorSetLayoutLitTextured_ = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolLitTextured_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutLitTextured3D_    = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesLitTextured3D_;
        std::array<std::unordered_map<uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                        litTexturedDescSets_;
        // Per-frame UBO ring buffer for light1/light2/emissive (5×vec4 = 80 bytes used, padded to 256)
        static constexpr uint32_t kLitTexturedUBOStride   = 256;
        static constexpr uint32_t kLitTexturedUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> litTexturedUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> litTexturedUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> litTexturedUBOPtr_ = {};
        // Task 899: BasicEffect fog bundle shared by colored3d (stride 16) / textured3d
        // (stride 20) / colored_textured3d (stride 24) -- all three read the same fully-packed
        // 128-byte FillExtPushConst() layout (zero spare bytes for fog), so fog is forwarded via
        // one small dedicated dynamic UBO instead, mirroring descriptorSetLayoutLitTextured_'s
        // shape (sampler@0 + dynamic UBO@1). colored3d's own shaders never read binding=0 (no
        // texture sampling), but the layout still declares it so all three pipelines can share
        // one descriptor-set-layout/pool/UBO/cache bundle (a default white texture is bound at
        // binding=0 for colored3d draws, same fallback every other pipeline already uses).
        VkDescriptorSetLayout descriptorSetLayoutFogTex3D_ = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolFogTex3D_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutFogTex3D_      = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesFogColored3D_;
        std::unordered_map<uint64_t, VkPipeline>             pipelinesFogTex3D_; // textured+coloredTextured, keyed by stride
        std::array<std::unordered_map<uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                        fogTex3DDescSets_;
        static constexpr uint32_t kFogTex3DUBOStride   = 256;
        static constexpr uint32_t kFogTex3DUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> fogTex3DUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> fogTex3DUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> fogTex3DUBOPtr_ = {};
        // SkinnedEffect resources
        VkDescriptorSetLayout descriptorSetLayoutSkinned_  = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolSkinned_       = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutSkinned3D_     = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkPipeline>              pipelinesSkinned3D_;
        std::array<std::unordered_map<uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                         skinnedDescSets_;
        // Per-frame bone matrix UBO ring buffer (4608 bytes/draw × 32 draws max)
        static constexpr uint32_t kSkinnedUBOStride   = 4608; // 72×64, multiple of 256
        static constexpr uint32_t kSkinnedUBOMaxDraws = 32;
        std::array<VkBuffer,       MaxFramesInFlight> skinnedUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> skinnedUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> skinnedUBOPtr_ = {};
        // Task 899: SkinnedEffect fog -- separate small dynamic UBO at binding=2 (BoneBlock@1
        // has zero spare capacity, so fog cannot be packed into it).
        static constexpr uint32_t kSkinnedFogUBOStride   = 256;
        static constexpr uint32_t kSkinnedFogUBOMaxDraws = 32;
        std::array<VkBuffer,       MaxFramesInFlight> skinnedFogUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> skinnedFogUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> skinnedFogUBOPtr_ = {};
        // --- Instanced 3D pipeline (Task 111) ---
        // Uses pipelineLayoutExt3D_ (128-byte PC: [0..15]=VP, [16..31]=ext params).
        // Vertex binding=0: per-vertex VERTEX rate; binding=1: per-instance INSTANCE rate (stride=64).
        std::unordered_map<uint64_t, VkPipeline> pipelinesInstanced3D_;

        // Default 1×1 white texture used when DrawPrimitivesEx has no texture bound.
        VkImage               defaultWhiteImage_     = VK_NULL_HANDLE;
        VkDeviceMemory        defaultWhiteMemory_    = VK_NULL_HANDLE;
        VkImageView           defaultWhiteView_      = VK_NULL_HANDLE;
        VkDescriptorSet       defaultWhiteDescSet_   = VK_NULL_HANDLE;

        // --- Sprite batch GPU buffers (host-visible, one per frame-in-flight) ---
        std::array<VkBuffer,       MaxFramesInFlight> spriteVB_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> spriteVBMem_ = {};
        std::array<void*,          MaxFramesInFlight> spriteVBPtr_ = {};
        std::array<VkBuffer,       MaxFramesInFlight> spriteIB_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> spriteIBMem_ = {};
        std::array<void*,          MaxFramesInFlight> spriteIBPtr_ = {};

        // --- Lifetime tracking for externally-owned Vulkan resources ---
        std::vector<VulkanTextureBackend*>       liveTextures_;
        std::vector<VulkanVertexBufferBackend*>  liveVertexBuffers_;
        std::vector<VulkanIndexBufferBackend*>   liveIndexBuffers_;
        std::vector<VulkanRenderTargetBackend*>  liveRenderTargets_;

        // --- MRT proxy (owned here; valid for one SetRenderTargets call) ---
        std::unique_ptr<VulkanMRTProxy>          mrtProxy_;

        // --- MRT render pass cache (keyed by color attachment count) ---
        std::unordered_map<uint32_t, VkRenderPass> mrtRenderPasses_;

        // --- Per-frame 3D dynamic geometry buffers ---
        // Vertex/index data is copied to CPU at draw time, then uploaded here after fence wait.
        static constexpr VkDeviceSize kFrame3DVBSize    = 4 * 1024 * 1024;  // 4 MB per-vertex
        static constexpr VkDeviceSize kFrame3DIBSize    = 1 * 1024 * 1024;  // 1 MB index
        static constexpr VkDeviceSize kFrame3DInstVBSize = 1 * 1024 * 1024; // 1 MB per-instance
        std::array<VkBuffer,       MaxFramesInFlight> frame3DVB_       = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> frame3DVBMem_    = {};
        std::array<void*,          MaxFramesInFlight> frame3DVBPtr_    = {};
        std::array<VkBuffer,       MaxFramesInFlight> frame3DIB_       = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> frame3DIBMem_    = {};
        std::array<void*,          MaxFramesInFlight> frame3DIBPtr_    = {};
        std::array<VkBuffer,       MaxFramesInFlight> frame3DInstVB_   = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> frame3DInstVBMem_= {};
        std::array<void*,          MaxFramesInFlight> frame3DInstVBPtr_= {};
        bool frame3DInstBuffersAllocated_ = false;

        // --- Per-frame accumulated draw data (cleared in RecordCommandBuffer) ---
        struct Pending3DDraw {
            std::vector<uint8_t>    vbData;       // copied vertex bytes
            std::vector<uint8_t>    ibData;       // copied index bytes, empty = non-indexed
            VkPrimitiveTopology     topology;
            uint32_t                drawCount;    // vertex or index count to submit
            float                   pushConst[32] = {}; // 128 bytes: [0..15]=MVP, [16..31]=ext params
            bool                    depthTest;
            bool                    depthWrite;
            bool                    blend;
            int                     cullMode;     // XNA CullMode: 0=None, 1=CW, 2=CCW
            VkIndexType             indexType;    // VK_INDEX_TYPE_UINT16 or UINT32
            VulkanRTSource*         rt = nullptr; // nullptr = backbuffer
            std::size_t             stride = 16;  // vertex stride in bytes
            VkDescriptorSet         descSet = VK_NULL_HANDLE; // texture (or null)
            // Task 899: true for BasicEffect draws with no alpha-test/dual-tex/env-map/skinned/
            // lit-textured override (stride 16/20/24) -- routes to the new fog-capable
            // colored3d/textured3d/colored_textured3d bundle. Left false (default) by the legacy
            // no-GpuDrawParams DrawColoredPrimitives()/DrawIndexedColoredPrimitives() path, which
            // still falls through to the original zero-descriptor-set colored3d pipeline.
            bool                    useFogTex3D       = false;
            float                   fogTex3DUboData[8] = {}; // vec4 fogColorEnabled + vec4 fogStartEnd
            VkDescriptorSet         fogTex3DDescSet   = VK_NULL_HANDLE;
            bool                    useAlphaTest      = false; // true = AlphaTest3D pipeline
            bool                    useDualTexture    = false; // true = DualTex3D pipeline
            VkDescriptorSet         dualTexDescSet    = VK_NULL_HANDLE; // 2-sampler set
            float                   dualTexFogUboData[8] = {}; // vec4 fogColorEnabled + vec4 fogStartEnd (Task 899)
            bool                    useEnvMap         = false; // true = EnvMap3D pipeline
            float                   envMapPC[32]      = {};    // push consts: [0..15]=mvp, [16..31]=world
            // 8×vec4 = 128 bytes for env map UBO (6 original + fogColorEnabled/fogStartEnd,
            // Task 899's own noted cheap-leftover: env_map3d's EnvMapParams UBO already has
            // ~160 spare bytes past the original 96 used).
            float                   envMapUboData[32] = {};
            VkDescriptorSet         envMapDescSet     = VK_NULL_HANDLE;
            bool                    useSkinned        = false; // true = Skinned3D pipeline
            std::vector<float>      boneMatrices;              // up to 72 mat4s = 1152 floats
            VkDescriptorSet         skinnedDescSet    = VK_NULL_HANDLE;
            float                   skinnedFogUboData[8] = {}; // vec4 fogColorEnabled + vec4 fogStartEnd (Task 899)
            bool                    useLitTextured    = false; // true = LitTextured3D pipeline (Task 897)
            // Layout (floats): [0..19]=light1/2 dir+diffuse+emissive (5 vec4, Task 897),
            // [20..35]=world mat4, [36..39]=eyePos, [40..51]=light0/1/2 specular (3 vec4),
            // [52..55]=specularColor+specularPower (Task 886/898), [56..59]=fogColor+fogEnabled,
            // [60..63]=fogStart+fogEnd+unused (Task 888). 256 bytes total.
            float                   litUboData[64]    = {};
            VkDescriptorSet         litTexturedDescSet = VK_NULL_HANDLE;
            int32_t                 baseVertex        = 0;     // vertexOffset for vkCmdDrawIndexed
            bool                    useInstanced      = false; // true = Instanced3D pipeline
            std::vector<uint8_t>    instVbData;                // per-instance bytes (instanceCount × stride)
            std::size_t             instVbStride      = 64;    // bytes per instance (default = mat4)
            uint32_t                instanceCount     = 1;     // number of instances
            bool                    wireframe         = false; // true = VK_POLYGON_MODE_LINE
            float                   depthBias         = 0.0f;  // XNA DepthBias (vkCmdSetDepthBias constant)
            float                   slopeScaleDepthBias = 0.0f; // XNA SlopeScaleDepthBias (slope factor)
            // Debug marker (SetStringMarkerEXT) — if true, vbData is empty and this entry
            // emits vkCmdInsertDebugUtilsLabelEXT instead of a draw call.
            bool                    isMarker          = false;
            std::string             markerLabel;
        };
        std::vector<Pending3DDraw>  pending3D_;
        // pair: (an independent per-Begin/End-cycle snapshot, target RT) where RT=nullptr means
        // backbuffer. Owns each snapshot outright (Task 664 fix) so that a 2nd Begin()/End() on
        // the same SpriteBatch object within one frame cannot clobber the 1st cycle's data —
        // each snapshot is pushed at End(), independent heap storage, harvested (and destroyed
        // via activeBatches_.clear()) once per frame in RecordCommandBuffer().
        std::vector<std::pair<std::unique_ptr<VulkanSpriteBatchBackend::BatchSnapshot>, VulkanRTSource*>> activeBatches_;

        // Cached vkCmdInsertDebugUtilsLabelEXT — loaded once after device creation, nullptr if unsupported.
        PFN_vkCmdInsertDebugUtilsLabelEXT pfnCmdInsertDebugLabel_ = nullptr;

        // --- Virtual (game) resolution for 2D NDC mapping ---
        int virtualWidth_  = 0;
        int virtualHeight_ = 0;

        // --- Frame state ---
        uint32_t currentFrame_            = 0;
        uint32_t lastPresentedImageIndex_ = 0;
        float    clearR_ = 0.f, clearG_ = 0.f, clearB_ = 0.f, clearA_ = 1.f;
        bool     initialized_       = false;
        VulkanRTSource*            currentRT_ = nullptr;
        bool     depthTestEnabled_  = true;
        bool     depthWriteEnabled_ = true;
        bool     blendEnabled_      = false;
        int      cullMode_          = 0;  // XNA CullMode: 0=None, 1=CW, 2=CCW

        bool frame3DBuffersAllocated_ = false;

        // ScissorRectangle state (Task 57)
        bool     scissorEnabled_            = false;
        bool     fillModeWireframe_         = false; // current XNA FillMode::WireFrame state
        bool     fillModeNonSolidSupported_ = false; // VkPhysicalDeviceFeatures.fillModeNonSolid
        float    depthBias_                 = 0.0f;  // XNA RasterizerState.DepthBias
        float    slopeScaleDepthBias_       = 0.0f;  // XNA RasterizerState.SlopeScaleDepthBias
        int32_t  scissorX_ = 0, scissorY_ = 0;
        uint32_t scissorW_ = 0, scissorH_ = 0;

        // Viewport state (Task 880) -- storage-only; consumed at command-buffer-record
        // time via vkCmdSetViewport (mirrors scissorX_/Y_/W_/H_'s identical pattern).
        int32_t  viewportX_ = 0, viewportY_ = 0;
        uint32_t viewportW_ = 0, viewportH_ = 0;
        float    viewportMinDepth_ = 0.0f, viewportMaxDepth_ = 1.0f;
        bool     viewportSet_      = false;

        // BlendFactor state (Task 63)
        float blendFactorR_ = 1.f, blendFactorG_ = 1.f,
              blendFactorB_ = 1.f, blendFactorA_ = 1.f;

        // Deferred readback: copy swapchain image to staging BEFORE vkQueuePresentKHR
        // so the presentation engine never races with the CPU read.
        bool         readbackPending_    = false;
        int          readbackX_          = 0;
        int          readbackY_          = 0;
        int          readbackW_          = 0;
        int          readbackH_          = 0;
        VkBuffer     readbackStagingBuf_ = VK_NULL_HANDLE;
        VkDeviceMemory readbackStagingMem_ = VK_NULL_HANDLE;
        int          readbackAllocW_     = 0;  // last allocated staging width
        int          readbackAllocH_     = 0;  // last allocated staging height
        // True when readbackStagingBuf_ holds a full, current copy of the backbuffer.
        // Lets multiple GetBackBufferData() reads of one frame be served from the cache
        // without re-presenting (which would re-render an empty frame). Invalidated by Clear*.
        bool         readbackStagingValid_ = false;

        // Custom effect set by VulkanEffectBackend::Bind() (Task 119)
        VulkanEffectBackend* activeCustomEffect_ = nullptr;

        // ---- Init helpers ----
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateRTRenderPass();
        // Task 878/879: lazily-created 3-attachment (MSAA color/resolve/MSAA depth) RT render
        // pass, shared by every MSAA-enabled RenderTarget2D. See rtRenderPassMsaa_ comment.
        void CreateRTRenderPassMsaa();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapchain();
        void CreateImageViews();
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateCommandPool();
        void AllocateCommandBuffers();
        void CreateSyncObjects();
        void CreateSampler();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void CreatePipeline2D();
        VkFormat   FindDepthFormat() const;
        void       CreateDepthResources();
        void       CleanupDepthResources();
        VkPipeline GetOrCreatePipeline3D(VkPrimitiveTopology, bool depthTest, bool depthWrite,
                                         bool blend, int cullMode,
                                         uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                         bool msaa = false);
        VkPipeline GetOrCreatePipelineAlphaTest3D(std::size_t stride, VkPrimitiveTopology,
                                                   bool depthTest, bool depthWrite,
                                                   bool blend, int cullMode,
                                                   uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                   bool msaa = false);
        void       EnsureDualTexResources();
        VkDescriptorSet GetOrCreateDualTexDescSet(uint32_t frameIdx, VkImageView view0, VkImageView view1,
                                                    VkSampler sampler0, VkSampler sampler1);
        VkPipeline GetOrCreatePipelineDualTex3D(VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                bool msaa = false);
        // EnvironmentMapEffect
        void       EnsureEnvMapResources();
        VkDescriptorSet GetOrCreateEnvMapDescSet(uint32_t frameIdx,
                                                  VkImageView view2D, VkImageView viewCube);
        VkPipeline GetOrCreatePipelineEnvMap3D(VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                bool msaa = false);
        void       FillEnvMapPushConst(float (&pc)[32], const Matrix& wvp, const Matrix& world);
        // SkinnedEffect
        void       EnsureSkinnedResources();
        VkDescriptorSet GetOrCreateSkinnedDescSet(uint32_t frameIdx, VkImageView view2D);
        VkPipeline GetOrCreatePipelineSkinned3D(VkPrimitiveTopology,
                                                 bool depthTest, bool depthWrite,
                                                 bool blend, int cullMode,
                                                 uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                 bool msaa = false);
        void       EnsureDefaultWhiteTexture();
        void       FillExtPushConst(float (&pc)[32], const Matrix& wvp, const GpuDrawParams& p);
        void       FillAlphaTestPushConst(float (&pc)[32], const Matrix& wvp, const GpuDrawParams& p);
        // BasicEffect lit-textured path (Task 897) — DirectionalLight1/2 + EmissiveColor,
        // forwarded via a small UBO (set=0,binding=1) alongside the unchanged 128-byte PC
        // (set=0,binding=0 stays the texture sampler; PC content unchanged from FillExtPushConst).
        void       EnsureLitTexturedResources();
        VkDescriptorSet GetOrCreateLitTexturedDescSet(uint32_t frameIdx, VkImageView view2D);
        VkPipeline GetOrCreatePipelineLitTextured3D(VkPrimitiveTopology,
                                                     bool depthTest, bool depthWrite,
                                                     bool blend, int cullMode,
                                                     uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                     bool msaa = false);
        // BasicEffect fog bundle (Task 899) — shared by colored3d/textured3d/colored_textured3d.
        void       EnsureFogTex3DResources();
        VkDescriptorSet GetOrCreateFogTex3DDescSet(uint32_t frameIdx, VkImageView view2D);
        VkPipeline GetOrCreatePipelineFogColored3D(VkPrimitiveTopology,
                                                    bool depthTest, bool depthWrite,
                                                    bool blend, int cullMode,
                                                    uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                    bool msaa = false);
        VkPipeline GetOrCreatePipelineFogTex3D(std::size_t stride, VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                bool msaa = false);
        // --- Instanced 3D pipeline ---
        VkPipeline GetOrCreatePipelineInstanced3D(std::size_t pvStride, VkPrimitiveTopology,
                                                   bool depthTest, bool depthWrite,
                                                   bool blend, int cullMode,
                                                   uint32_t colorAttachmentCount = 1, bool wireframe = false,
                                                   bool msaa = false);
        void FillInstancedPushConst(float (&pc)[32], const Matrix& view, const Matrix& proj,
                                    const GpuDrawParams& p);
        void CreateFrame3DInstBuffers();
        void EnsureFrame3DInstBuffers();

        void CreateMsaaColorResources();
        void CleanupMsaaColorResources();
        void CreateRenderPassMsaa();
        void CreatePipeline2DMsaa();

        void CreateSpriteBuffers();
        void CreateFrame3DBuffers();
        void EnsureFrame3DBuffers();
        VkRenderPass GetOrCreateMRTRenderPass(uint32_t colorAttachmentCount);

        // --- Per-slot SamplerState (Task 118) ---
        void ApplySamplerState(int slot, int filter,
                               int addressU, int addressV,
                               int maxAnisotropy) override;
        VkDescriptorSet GetOrCreateTexSamplerDescSet(VkImageView view, VkSampler sampler);

        // --- Custom Effect / SPIR-V loading (Task 119) ---
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        // ---- Swapchain lifecycle ----
        void RecreateSwapchain();
        void CleanupSwapchain();

        // ---- Frame recording ----
        void RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);

        // Submits one frame (render + optional deferred readback copy). When deferSwap is
        // true the swapchain image is acquired, rendered and the GPU is waited on, but
        // vkQueuePresentKHR is NOT issued — letting ReadBackbuffer read the staging buffer
        // BEFORE the image is handed to the presentation engine. FinishDeferredPresent()
        // then presents the held image. Returns false if the swapchain was recreated.
        bool SubmitFrame(bool deferSwap);
        void FinishDeferredPresent();
        uint32_t deferredPresentImageIndex_ = 0;
        bool     hasDeferredPresent_        = false;

        // ---- Memory / resource helpers (also used by texture/buffer backends) ----
        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
        void     CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& buf, VkDeviceMemory& mem, void** mapped = nullptr);
        VkCommandBuffer BeginOneTimeCommands();
        void            EndOneTimeCommands(VkCommandBuffer cb);
        void TransitionImageLayout(VkImage img, VkImageLayout from, VkImageLayout to);
        void CopyBufferToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h);
        VkShaderModule  CreateShaderModule(const uint32_t* spv, size_t byteSize);

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT*,
            void*);
    };
}
