#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <map>
#include <string>
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
        /// True if this source's actual framebuffer/render pass uses multisample color
        /// attachments plus single-sample resolves. Default false; overridden by the concrete
        /// single-target, cube-face, and MRT sources when MSAA is genuinely engaged.
        virtual bool          WantsMsaa()                   const { return false; }
        /// Task 911: this RT's own real depth VkFormat (VK_FORMAT_UNDEFINED = no depth
        /// attachment at all, DepthFormat::None). Default VK_FORMAT_UNDEFINED; overridden by
        /// VulkanRenderTargetBackend/VulkanRenderTargetCubeBackend with their own instance's
        /// picked format (see PickDepthFormat()). Backbuffer draws don't go through a
        /// VulkanRTSource at all, so callers use depthFormat_ directly for that case.
        virtual VkFormat      GetDepthFormat()               const { return VK_FORMAT_UNDEFINED; }
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
        // Task 925 (split from Task 867): real GPU upload for level>0, mirroring
        // VulkanTexture3DBackend::SetData's established staging-buffer pattern (Task 864).
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

    private:
        // Task 925: transitions exactly ONE mip level's layout -- the shared
        // VulkanGraphicsBackend::TransitionImageLayout always barriers level 0 regardless of
        // which level is being copied. UpdatePixelsLevel needs the real target level barriered
        // instead, confirmed via live Vulkan validation-layer errors otherwise. Texture3D's
        // formerly identical mismatch was corrected by REMED-GFX-093.
        void TransitionLevelLayout(int level, VkImageLayout from, VkImageLayout to);

        int                 width_         = 0;
        int                 height_        = 0;
        int                 levelCount_    = 1;
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
        // Task 911: `depthFormat` (raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal)
        // gives this instance true per-RT DepthStencilFormat fidelity -- a real, distinct
        // VkFormat picked via PickDepthFormat() (or no depth attachment at all for
        // DepthFormat::None), independent of the backbuffer's own depthFormat_ and of every
        // other render target. Each distinct depthVkFormat_ gets its own render pass (see
        // VulkanGraphicsBackend::GetOrCreateRTRenderPass()/GetOrCreateRTRenderPassMsaa()) and its
        // own pipeline cache entries (DepthStencilKeyParams no longer needs a depth-format
        // dimension since depthCompareOp/stencil ops are independent of the buffer's exact
        // format -- but the render pass itself is, since Vulkan pipeline/render-pass
        // compatibility requires an exact attachment-format match).
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
        // Task 911: this instance's own real depth VkFormat (VK_FORMAT_UNDEFINED = no depth
        // attachment, DepthFormat::None).
        VkFormat        GetDepthFormat()            const override { return depthVkFormat_; }
        // Real, backend-clamped applied MultiSampleCount (0 if MSAA wasn't engaged — see the
        // "piggyback on the backend's own sampleCount_" scope decision in plan_graphics.md).
        int             GetMultiSampleCount()      const override { return appliedMultiSampleCount_; }
        VkDescriptorSet GetDescriptorSet()         const { return descriptorSet_; }
        VkImageView     GetColorView()             const { return colorView_; }
        // REMED-GFX-095 test diagnostics: the texture/resolve view and the transient
        // multisample view owned by this target. These are read-only backend-internal
        // handles; sampling must always continue to use colorSampleView_ below.
        VkImageView     GetResolveColorViewEXT()   const { return colorView_; }
        VkImageView     GetMsaaColorViewEXT()      const { return msaaColorView_; }
        VkSampleCountFlagBits GetColorSampleCountEXT() const
        {
            return static_cast<VkSampleCountFlagBits>(
                appliedMultiSampleCount_ > 1 ? appliedMultiSampleCount_ : 1);
        }
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

        // REMED-GFX-074: real GPU readback of this render target's colour image so that
        // RenderTarget2D::GetData() observes prior sprite/3D rendering into it even BEFORE
        // Present() runs. Vulkan defers all draw work to a single Present-time record, so this
        // first flushes any deferred passes queued into this target (FlushDeferredRenderTarget),
        // then copies colorImage_ back via a host-visible staging buffer -- mirroring
        // VulkanTexture3DBackend::GetData's pattern, plus the swapchain BGRA->RGBA channel swap
        // (the RT colour image uses swapchainFormat_). Pre-fix this was the unimplemented base
        // no-op, so a RenderTarget2D read back before Present returned all-zeros.
        void GetData(int level, int x, int y, int w, int h,
                     void* data, int dataLength) const override;

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
        // Task 911: VK_FORMAT_UNDEFINED means "no depth attachment at all" (DepthFormat::None);
        // depthImage_/depthMemory_/depthView_ stay VK_NULL_HANDLE in that case.
        VkFormat                depthVkFormat_ = VK_FORMAT_UNDEFINED;
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

    struct BlendKeyParams;

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

        VkPipeline GetOrCreatePipeline(uint32_t colorAttachmentCount,
                                       VkSampleCountFlagBits sampleCount,
                                       VkFormat depthFormat,
                                       bool blend,
                                       const BlendKeyParams& blendParams);
        VkPipelineLayout GetPipelineLayout() const { return pipelineLayout_; }
        // Returns pointer to 128-byte push-constant staging area (floats 2..31 = user uniforms).
        const float*     GetPushConst()      const { return pushConst_;      }

    private:
        VulkanGraphicsBackend* owner_;
        VkShaderModule   vertModule_     = VK_NULL_HANDLE;
        VkShaderModule   fragModule_     = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        struct PipelineVariantKey
        {
            uint32_t colorAttachmentCount = 1;
            uint32_t sampleCount = 1;
            int32_t depthFormat = 0;
            bool blend = false;
            uint32_t blendBits = 0;
            uint32_t colorWriteBits = 0;
            uint32_t sampleMask = 0xFFFFFFFFu;
            bool operator==(const PipelineVariantKey&) const noexcept = default;
        };
        struct PipelineVariantKeyHash
        {
            std::size_t operator()(const PipelineVariantKey& key) const noexcept
            {
                std::size_t h = std::hash<uint32_t>{}(key.colorAttachmentCount);
                h ^= std::hash<uint32_t>{}(key.sampleCount) + (h << 6) + (h >> 2);
                h ^= std::hash<int32_t>{}(key.depthFormat) + (h << 6) + (h >> 2);
                h ^= std::hash<bool>{}(key.blend) + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.blendBits) + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.colorWriteBits) + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.sampleMask) + (h << 6) + (h >> 2);
                return h;
            }
        };
        std::unordered_map<PipelineVariantKey, VkPipeline, PipelineVariantKeyHash> pipelines_;
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

    // Task 868: the real per-channel Blend/BlendFunction values a BlendState requests, mirrors
    // DepthStencilKeyParams -- fields baked into a pipeline at creation time (Vulkan has no
    // per-draw dynamic blend-equation state). Defaults match BlendState.Opaque's own values
    // (One/Zero, Add), though blendEnabled_ already gates Opaque out of blending entirely.
    // REMED-GFX-071: also captured (by value) into VulkanSpriteBatchBackend::BatchSnapshot so the
    // 2D sprite pipeline honors SpriteBatch.Begin()'s BlendState, mirroring the 3D Pending3DDraw
    // path -- hence defined here, ahead of the sprite backend that stores it.
    struct BlendKeyParams {
        int colorSrc  = 0; // Blend::One
        int colorDst  = 1; // Blend::Zero
        int alphaSrc  = 0; // Blend::One
        int alphaDst  = 1; // Blend::Zero
        int colorFunc = 0; // BlendFunction::Add
        int alphaFunc = 0; // BlendFunction::Add
        // REMED-GFX-077: BlendState.ColorWriteChannels per MRT slot 0..3 (bit0=R..bit3=A; the XNA
        // bit layout is identical to VK_COLOR_COMPONENT_*) + BlendState.MultiSampleMask. Both are
        // static Vulkan pipeline state, so both participate in the pipeline cache key. Defaults
        // match XNA (All ×4, 0xFFFFFFFF). Applied via FillBlendAttachmentState (colorWrite) and
        // VkPipelineMultisampleStateCreateInfo::pSampleMask (sampleMask).
        int colorWrite[4] = {15, 15, 15, 15};
        uint32_t sampleMask = 0xFFFFFFFFu;
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

        // REMED-GFX-012 fix: previously unoverridden, so SpriteBatch.Begin(transformMatrix) fell
        // through to IGraphicsBackend::SetTransformMatrix()'s shared no-op default and the
        // transform was silently discarded on Vulkan specifically (the only affected backend of
        // 14). Stored here and applied CPU-side per vertex in Draw() (see the .cpp), mirroring
        // D3D11SpriteBatchBackend's design -- same raw-pixel-space Sprite2DVertex/viewportSize
        // shader contract, so there is no projection-matrix uniform to fold it into GPU-side.
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }

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
            // REMED-GFX-075: a custom Effect is captured by VALUE (its VkPipeline/VkPipelineLayout
            // handles + a copy of its 128-byte push-constant block) at End(), NOT by pointer, so a
            // custom Effect disposed after SpriteBatch.End() but before the deferred record can no
            // longer be dereferenced at Present time. The pipeline/layout handles themselves are
            // kept alive past this batch's consumption by the effect backend's retirement queue.
            bool                        hasCustomEffect = false;
            VkPipeline                  customPipeline  = VK_NULL_HANDLE;
            VkPipelineLayout            customLayout    = VK_NULL_HANDLE;
            float                       customPushConst[32] = {};
            // REMED-GFX-013: scissor state captured at End() so a SpriteBatch filling a render
            // target is clipped correctly regardless of later frame-global scissor changes (e.g.
            // Task 338's ScissorRectangle reset on RT unbind). enabled==false or a zero-sized rect
            // means "no clip" (whole framebuffer), matching the backbuffer pass's own guard.
            bool                        scissorEnabled = false;
            int32_t                     scissorX = 0, scissorY = 0;
            uint32_t                    scissorW = 0, scissorH = 0;
            // REMED-GFX-062: viewport state captured at End() so a SpriteBatch filling a render
            // target honors the Viewport active for this batch, not the frame-global viewport left
            // over at Present() (which SetRenderTarget resets to the target's full size). set==false
            // or a zero-sized rect means "full target", matching the backbuffer pass's own guard.
            bool                        viewportSet = false;
            int32_t                     viewportX = 0, viewportY = 0;
            uint32_t                    viewportW = 0, viewportH = 0;
            float                       viewportMinDepth = 0.0f, viewportMaxDepth = 1.0f;
            // REMED-GFX-070: blend-constant (GraphicsDevice.BlendFactor) captured at End() so a
            // SpriteBatch filling a render target uses the constant active for this batch, not the
            // frame-global value the backend applied once per frame at backbuffer-pass begin.
            // Defaults to (1,1,1,1) = XNA Color::White so a batch that predates any SetBlendFactor
            // uses the correct XNA default. Replayed only when the batch's normalized blend
            // equation uses BlendFactor/InverseBlendFactor (REMED-GFX-091).
            float                       blendFactorR = 1.0f, blendFactorG = 1.0f,
                                        blendFactorB = 1.0f, blendFactorA = 1.0f;
            // REMED-GFX-071: the batch's full BlendState -- blendEnable + the six per-channel
            // Blend/BlendFunction values -- captured at End() so drawSpritesFor() selects a 2D
            // sprite pipeline whose colour-attachment blend equation matches SpriteBatch.Begin()'s
            // BlendState instead of the pre-fix hardcoded alpha-blend. Captured BY VALUE (not a
            // BlendState*) so a state changed/destroyed after End() but before the deferred Present
            // record cannot dangle (mirrors GFX-075's by-value effect capture and the 3D path's
            // Pending3DDraw.blendParams). Defaults to Opaque (blendEnabled=false); every real batch
            // overwrites these from the device state SpriteBatch.Begin() always (re-)applies.
            bool                        blendEnabled = false;
            BlendKeyParams              blendParams;
        };

    private:
        VulkanGraphicsBackend*           backend_             = nullptr;
        bool                             active_              = false;
        Effect*                          customEffect_        = nullptr;
        VulkanEffectBackend*             customEffectBackend_ = nullptr;
        // REMED-GFX-012: SpriteBatch.Begin(transformMatrix), applied per vertex in Draw(). Always
        // (re-)set by SpriteBatch::Begin() before this backend's Begin(), so it is never stale;
        // defaults to Identity for the no-transform overload.
        Matrix                           transform_           = Matrix::getIdentityProperty();
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
        void SetVertexDeclaration(const VertexDeclaration&) override {}
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
        VulkanTexture3DBackend(VulkanGraphicsBackend* owner, int w, int h, int depth, bool mipMap);
        ~VulkanTexture3DBackend() override;

        void SetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     const void* data, int dataLength) override;
        void GetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     void* data, int dataLength) const override;

    private:
        VulkanGraphicsBackend* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int width_ = 0, height_ = 0, depth_ = 0;
        int levelCount_ = 1;
    };

    // -------------------------------------------------------------------------
    // VulkanTextureCubeBackend
    // -------------------------------------------------------------------------

    class VulkanTextureCubeBackend : public ITextureCubeBackend, public IVulkanCubeSamplable
    {
    public:
        VulkanTextureCubeBackend(VulkanGraphicsBackend* owner, int size, bool mipMap);
        ~VulkanTextureCubeBackend() override;

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;
        void GetData(int face, int level, int x, int y, int w, int h,
                     void* data, int dataLength) const override;

        /** @brief Returns the Vulkan cube image view for sampling. */
        [[nodiscard]] VkImageView GetImageView()        const { return imageView_; }
        [[nodiscard]] VkImageView GetVkCubeImageView()  const override { return imageView_; }

    private:
        VulkanGraphicsBackend* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int size_ = 0;
        int levelCount_ = 1;
    };

    // -------------------------------------------------------------------------
    // VulkanOcclusionQueryBackend
    // -------------------------------------------------------------------------

    class VulkanOcclusionQueryBackend : public IOcclusionQueryBackend
    {
        // Task 447/854: RecordCommandBuffer() drives pool_/resetThisFrame_/recordedThisFrame_
        // directly (real vkCmdResetQueryPool/vkCmdBeginQuery/vkCmdEndQuery recording lives there,
        // alongside every other draw-dispatch/render-pass concern), mirroring this file's
        // existing one-directional friend idiom (VulkanGraphicsBackend already befriends this
        // class the other way, for owner_ access).
        friend class VulkanGraphicsBackend;

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
        // Task 447/854: per-frame recording bookkeeping, driven entirely by RecordCommandBuffer().
        // recordedThisFrame_ is reset to false at the top of every RecordCommandBuffer() call (for
        // every query actually tagged on a pending draw that frame) and flips true once this
        // query's first contiguous run of draws has been wrapped in a real vkCmdBeginQuery/
        // vkCmdEndQuery pair -- implementing the approved "allow multiple draws within one render
        // pass, reject additional spans across a render-pass boundary (or a non-contiguous 2nd run
        // within the same pass)" policy: only the first run this query appears in each frame is
        // ever actually recorded on the GPU.
        bool                    recordedThisFrame_ = false;
    };

    // -------------------------------------------------------------------------
    // VulkanRenderTargetCubeBackend
    // -------------------------------------------------------------------------

    class VulkanRenderTargetCubeBackend : public IRenderTargetCubeBackend,
                                          public IVulkanCubeSamplable
    {
    public:
        // Task 911: `depthFormat` (raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal)
        // gives this instance true per-RT DepthStencilFormat fidelity, mirroring
        // VulkanRenderTargetBackend's identical constructor-comment fix.
        VulkanRenderTargetCubeBackend(VulkanGraphicsBackend* owner, int size, int depthFormat,
                                       bool mipMap = false, int requestedMultiSampleCount = 0);
        ~VulkanRenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        /// Task 903: real applied MSAA sample count (0 if MSAA wasn't engaged), mirroring
        /// VulkanRenderTargetBackend::GetMultiSampleCount()'s identical Task 878/879 pattern.
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        [[nodiscard]] VkImageView GetFaceResolveViewEXT(int face) const
        {
            return face >= 0 && face < 6
                ? faceViews_[static_cast<std::size_t>(face)]
                : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkImageView GetMsaaColorViewEXT() const { return msaaColorView_; }
        [[nodiscard]] VkImageView GetDepthViewEXT() const { return depthView_; }
        [[nodiscard]] VkFormat GetDepthFormatEXT() const { return depthVkFormat_; }
        [[nodiscard]] VkSampleCountFlagBits GetColorSampleCountEXT() const
        {
            return static_cast<VkSampleCountFlagBits>(
                appliedMultiSampleCount_ > 1 ? appliedMultiSampleCount_ : 1);
        }

        // IVulkanCubeSamplable — returns a VK_IMAGE_VIEW_TYPE_CUBE view over all 6 faces.
        [[nodiscard]] VkImageView GetVkCubeImageView() const override { return cubeView_; }

    private:
        // Task 907: per-face proxy also knows how to regenerate its OWN layer's mip chain
        // (levels 0..levelCount-1 of the shared 6-layer `image_`, layer = faceIndex) via a
        // vkCmdBlitImage cascade, mirroring VulkanRenderTargetBackend::MaybeGenerateMips (Task 878).
        // Task 903: also knows how to report/serve its own MSAA framebuffer + render pass, when
        // this cube engaged MSAA -- mirrors VulkanRenderTargetBackend's msaaFramebuffer_ pattern.
        struct FaceProxy : public VulkanRTSource {
            VkFramebuffer framebuffer     = VK_NULL_HANDLE;
            VkRenderPass  renderPass      = VK_NULL_HANDLE;
            VkFramebuffer msaaFramebuffer = VK_NULL_HANDLE;
            VkRenderPass  msaaRenderPass  = VK_NULL_HANDLE;
            int           size         = 0;
            VkImage       image        = VK_NULL_HANDLE;
            int           levelCount   = 1;
            int           faceIndex    = 0;
            /// Task 911: this cube's own real depth VkFormat (VK_FORMAT_UNDEFINED = no depth
            /// attachment, DepthFormat::None), mirrored from the owning
            /// VulkanRenderTargetCubeBackend's depthVkFormat_.
            VkFormat      depthFormat  = VK_FORMAT_UNDEFINED;
            VkFramebuffer GetFramebuffer()          const override { return (msaaFramebuffer != VK_NULL_HANDLE) ? msaaFramebuffer : framebuffer; }
            VkRenderPass  GetRenderPass()            const override { return (msaaFramebuffer != VK_NULL_HANDLE) ? msaaRenderPass : renderPass; }
            int GetWidth()                          const override { return size; }
            int GetHeight()                         const override { return size; }
            uint32_t GetColorAttachmentCount()      const override { return 1; }
            bool WantsMsaa()                        const override { return msaaFramebuffer != VK_NULL_HANDLE; }
            VkFormat GetDepthFormat()               const override { return depthFormat; }
            void MaybeGenerateMips(VkCommandBuffer cb) override;
        };

        VulkanGraphicsBackend*     owner_     = nullptr;
        VkImage                    image_     = VK_NULL_HANDLE;
        VkDeviceMemory             memory_    = VK_NULL_HANDLE;
        VkImageView                cubeView_  = VK_NULL_HANDLE;   ///< Full-cube view for sampling.
        std::array<VkImageView, 6> faceViews_ = {};
        /// Task 911: this cube's own real depth VkFormat (VK_FORMAT_UNDEFINED = no depth
        /// attachment at all, DepthFormat::None), picked independently of the backbuffer's own
        /// depthFormat_ -- see PickDepthFormat().
        VkFormat                   depthVkFormat_ = VK_FORMAT_UNDEFINED;
        VkImage                    depthImage_  = VK_NULL_HANDLE;
        VkDeviceMemory             depthMemory_ = VK_NULL_HANDLE;
        VkImageView                depthView_   = VK_NULL_HANDLE;
        std::array<VkFramebuffer, 6> framebuffers_ = {};
        // Task 903: one shared MSAA color image, reused across all 6 faces (mirrors depthImage_'s
        // existing shared-across-faces pattern) since only one face is ever rendered into at a
        // time -- TRANSIENT_ATTACHMENT only, resolved into that face's own faceViews_[face]/image_
        // layer at vkCmdEndRenderPass via rtRenderPassMsaa_'s pResolveAttachments.
        VkImage                       msaaColorImage_  = VK_NULL_HANDLE;
        VkDeviceMemory                msaaColorMemory_ = VK_NULL_HANDLE;
        VkImageView                   msaaColorView_   = VK_NULL_HANDLE;
        std::array<VkFramebuffer, 6>  msaaFramebuffers_ = {};
        std::array<FaceProxy, 6>     faceProxies_;
        int                        size_      = 0;
        int                        levelCount_ = 1;
        int                        appliedMultiSampleCount_ = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanMRTProxy — combines N VulkanRenderTargetBackend into one RT source
    // -------------------------------------------------------------------------

    class VulkanMRTProxy : public VulkanRTSource
    {
    public:
        VulkanMRTProxy(VulkanGraphicsBackend* owner,
                       const RenderTargetBindingDescriptor* renderTargets,
                       uint32_t count);
        ~VulkanMRTProxy() override;

        VkFramebuffer GetFramebuffer()          const override { return framebuffer_; }
        VkRenderPass  GetRenderPass()            const override { return renderPass_; }
        int           GetWidth()                const override { return width_; }
        int           GetHeight()               const override { return height_; }
        uint32_t      GetColorAttachmentCount() const override { return colorCount_; }
        bool          WantsMsaa()               const override
        {
            return colorSampleCount_ > VK_SAMPLE_COUNT_1_BIT;
        }
        // XNA/FNA shares binding 0's depth attachment across the active MRT set.
        VkFormat      GetDepthFormat()           const override { return depthFormat_; }

        // REMED-GFX-095 read-only structural diagnostics. Keeping the actual framebuffer
        // vector makes each live attachment/view association directly testable; Vulkan has
        // no API for querying a VkFramebuffer's creation-time attachment list afterward.
        [[nodiscard]] VkSampleCountFlagBits GetColorSampleCountEXT() const
        {
            return colorSampleCount_;
        }
        [[nodiscard]] uint32_t GetFramebufferAttachmentCountEXT() const
        {
            return static_cast<uint32_t>(framebufferAttachments_.size());
        }
        [[nodiscard]] VkImageView GetFramebufferAttachmentViewEXT(uint32_t index) const
        {
            return index < framebufferAttachments_.size()
                ? framebufferAttachments_[index]
                : VK_NULL_HANDLE;
        }
        [[nodiscard]] uint32_t GetResolveAttachmentCountEXT() const
        {
            return static_cast<uint32_t>(resolveAttachments_.size());
        }
        [[nodiscard]] VkImageView GetColorAttachmentViewEXT(uint32_t index) const
        {
            return index < colorAttachments_.size() ? colorAttachments_[index] : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkImageView GetResolveAttachmentViewEXT(uint32_t index) const
        {
            return index < resolveAttachments_.size() ? resolveAttachments_[index] : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkSampleCountFlagBits GetDepthSampleCountEXT() const
        {
            return depthView_ != VK_NULL_HANDLE ? colorSampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        }
        [[nodiscard]] bool ContainsResolveTargetEXT(
            const VulkanRenderTargetBackend& target) const
        {
            return std::find(resolveTargetViews_.begin(), resolveTargetViews_.end(),
                             target.GetResolveColorViewEXT()) != resolveTargetViews_.end();
        }

    private:
        VulkanGraphicsBackend* owner_       = nullptr;
        VkRenderPass           renderPass_  = VK_NULL_HANDLE;
        VkFramebuffer          framebuffer_ = VK_NULL_HANDLE;
        int                    width_       = 0;
        int                    height_      = 0;
        uint32_t               colorCount_  = 0;
        VkSampleCountFlagBits  colorSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
        std::vector<VkImageView> colorAttachments_;
        std::vector<VkImageView> resolveAttachments_;
        std::vector<VkImageView> framebufferAttachments_;
        std::vector<VkImageView> resolveTargetViews_;
        VkFormat                depthFormat_ = VK_FORMAT_UNDEFINED;
        // Borrowed from binding 0. Render-target retirement keeps it alive until frame completion.
        VkImageView             depthView_ = VK_NULL_HANDLE;
    };

    // Task 870: bundles every DepthStencilState field that -- unlike stencil reference/compare
    // mask/write mask, which are true Vulkan dynamic state (vkCmdSetStencil*, no new pipeline
    // needed) -- is baked into a VkPipeline at creation time (depthCompareOp and the front/back
    // VkStencilOpState blocks), so must be part of every 3D pipeline's cache key.

    // Task 868: pipeline caches now key on (existing uint64_t topology/depth/stencil/etc. bits,
    // packed blend bits) -- a plain uint64_t ran out of free bit-width once the full 6-value
    // Blend/BlendFunction state needed representing (the existing key already uses up through
    // ~bit 52 once a depth VkFormat is folded in via FoldDepthFormatIntoKey), so blend state gets
    // its own uint32_t half instead of being crammed into the same 64 bits. std::pair<uint64_t,
    // uint32_t> has correct default operator== (member-wise); only a hash functor is needed.
    // REMED-GFX-077: the color-write mask (16 bits, 4×4 for MRT slots 0..3) and the 32-bit sample
    // mask are static Vulkan pipeline state that must be keyed losslessly, but don't fit the former
    // (uint64_t, uint32_t) budget. Extended to a 4-field key: `a`/`b` are the original depth-folded
    // key and packed blend factors/functions; `cw` packs the four colour-write masks; `sm` is the
    // sample mask. The default (cw=0x5555... no — All(15)×4 = 0xFFFF-low16, sm=0xFFFFFFFF) is a
    // fixed contribution, so default draws still collapse to one pipeline (no cache fragmentation).
    struct PipelineKey {
        uint64_t a = 0;
        uint32_t b = 0;
        uint32_t cw = 0;
        uint32_t sm = 0xFFFFFFFFu;
        bool operator==(const PipelineKey&) const noexcept = default;
    };
    struct PipelineKeyHash {
        std::size_t operator()(const PipelineKey& k) const noexcept
        {
            std::size_t h = std::hash<uint64_t>{}(k.a);
            h ^= std::hash<uint32_t>{}(k.b) * 0x9E3779B97F4A7C15ull;
            h ^= (std::hash<uint32_t>{}(k.cw) + 0x165667B19E3779F9ull + (h << 6) + (h >> 2));
            h ^= (std::hash<uint32_t>{}(k.sm) + 0x27D4EB2F165667C5ull + (h << 6) + (h >> 2));
            return h;
        }
    };

    struct DepthStencilKeyParams {
        int  depthFunc            = 3;      // CompareFunction::LessEqual (XNA DepthStencilState.Default)
        bool stencilEnable        = false;
        int  stencilFunc          = 0;      // CompareFunction::Always
        int  stencilFail          = 0;      // StencilOperation::Keep
        int  stencilDepthFail     = 0;
        int  stencilPass          = 0;
        bool twoSidedStencilMode  = false;
        int  ccwStencilFunc       = 0;
        int  ccwStencilFail       = 0;
        int  ccwStencilDepthFail  = 0;
        int  ccwStencilPass       = 0;
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

        // AnisotropicFiltering/WireFrame reflect real, already-cached device feature queries
        // (anisotropySupported_/fillModeNonSolidSupported_, both set once at device creation).
        // Everything else CNA::GraphicsCapability currently enumerates is genuinely supported
        // here, so falls through to the shared default (true).
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int)       override {}
        // Task 902: real in-place backbuffer MSAA reconfiguration, wired from
        // GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling actually reaches
        // the backend. Deliberately scoped to the backbuffer only -- already-live RenderTarget2D/
        // RenderTargetCube instances keep whatever MultiSampleCount they engaged at their own
        // construction time (see VulkanRenderTargetBackend's own sampleCount_ capture); a
        // cascading invalidation of live render targets is tracked separately as a follow-up.
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        [[nodiscard]] int GetMultiSampleCount() const override;
        // REMED-GFX-091 test diagnostic: total graphics-pipeline cache entries. BlendFactor's
        // RGBA value is dynamic and must never increase this count; only static state does.
        [[nodiscard]] std::size_t GetGraphicsPipelineCacheEntryCountEXT() const noexcept
        {
            return pipelines2DByDepthFmt_.size() + pipelines2DMsaaByDepthFmt_.size()
                + pipelines3D_.size() + pipelinesAlphaTest3D_.size()
                + pipelinesDualTex3D_.size() + pipelinesEnvMap3D_.size()
                + pipelinesLitTextured3D_.size() + pipelinesLitTextured3DVertexLit_.size()
                + pipelinesFogColored3D_.size() + pipelinesFogTex3D_.size()
                + pipelinesSkinned3D_.size() + pipelinesSkinned3DVertexLit_.size()
                + pipelinesPbr3D_.size() + pipelinesPbrSkinned3D_.size()
                + pipelinesInstanced3D_.size();
        }
        // REMED-GFX-095: live MRT construction/pipeline diagnostics used by the dedicated
        // regression to distinguish a requested multisample target from a silent 1x pass.
        [[nodiscard]] const VulkanMRTProxy* GetCurrentMRTProxyEXT() const noexcept
        {
            return mrtProxy_.get();
        }
        [[nodiscard]] VkSampleCountFlagBits GetLastMRTPipelineSampleCountEXT() const noexcept
        {
            return lastMrtPipelineSampleCountEXT_;
        }
        [[nodiscard]] uint32_t GetLastMRTPipelineColorCountEXT() const noexcept
        {
            return lastMrtPipelineColorCountEXT_;
        }
        /**
         * @brief Returns every warning/error emitted by the active Vulkan validation messenger.
         *
         * Used by native validation regressions to turn asynchronous debug output into a
         * deterministic test failure while retaining the complete layer message.
         *
         * @return Validation messages captured since backend construction.
         */
        [[nodiscard]] const std::vector<std::string>& GetValidationMessagesEXT() const noexcept
        {
            return validationMessages_;
        }

        SDL_Window*  GetWindowInternal()   const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend>         CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend>     CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend>    CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void                                     SetRenderTarget2D(IRenderTargetBackend* rt) override;

        // ---- Graphics state: IMPLEMENTED ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
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
        // Task 870/319: GraphicsDevice.ReferenceStencil applied standalone (not just as part of
        // a full DepthStencilState re-application) -- updates the same referenceStencil_ member
        // ApplyDepthStencilState writes, taking effect on the next draw's
        // vkCmdSetStencilReference call (see draw3DFor).
        void SetReferenceStencil(int value) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DBackend>  CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

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
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
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
        std::vector<std::string> validationMessages_;
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
        // Task 911: depth-format-keyed (VK_FORMAT_UNDEFINED = no depth attachment), lazily
        // populated via GetOrCreatePipeline2DMsaa() -- mirrors the 3D pipeline caches' own
        // per-target-depth-format fidelity, since the 2D sprite pipeline's render pass must still
        // exactly attachment-format-match whichever target it draws into even though it never
        // itself reads/writes the depth attachment (VkPipelineDepthStencilStateCreateInfo has
        // depthTestEnable=depthWriteEnable=VK_FALSE always).
        // REMED-GFX-071: now keyed by (depth-format-folded uint64 + blendEnabled bit, PackBlendBits)
        // -- the full PipelineKey shape the 3D caches use -- so a distinct SpriteBatch BlendState
        // gets its own VkPipeline (with FillBlendAttachmentState-derived factors) instead of one
        // hardcoded alpha-blend pipeline per depth format. The BlendFactor *value* stays dynamic
        // (VK_DYNAMIC_STATE_BLEND_CONSTANTS, GFX-070) and is NOT in this key -- only PackBlendBits'
        // factor/function *enums* are, so changing GraphicsDevice.BlendFactor never fragments it.
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines2DMsaaByDepthFmt_;

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
        // Task 911: RT render passes are now keyed by real depth VkFormat (VK_FORMAT_UNDEFINED =
        // no depth attachment at all) instead of one hardcoded device-wide format shared by every
        // render target — each RenderTarget2D/RenderTargetCube gets true per-instance
        // DepthStencilFormat fidelity. Lazily populated via GetOrCreateRTRenderPass()/
        // GetOrCreateRTRenderPassMsaa(); a format's entry is shared by every RT requesting that
        // same format (render-pass "compatibility" requires an exact attachment-format match, so
        // a distinct format genuinely needs its own render pass, but RTs sharing a format safely
        // share one pass + one pipeline, mirroring the pre-existing single-format reuse pattern).
        std::unordered_map<VkFormat, VkRenderPass> rtRenderPassByDepthFmt_;      // LOAD_OP_CLEAR, color → SHADER_READ_ONLY_OPTIMAL
        std::unordered_map<VkFormat, VkRenderPass> rtRenderPassLoadByDepthFmt_;  // LOAD_OP_LOAD,  color → SHADER_READ_ONLY_OPTIMAL
        std::unordered_map<VkFormat, VkRenderPass> rtRenderPassMsaaByDepthFmt_;  // 3-attachment MSAA color/resolve/depth
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
        bool independentBlendSupported_ = false;

        // REMED-GFX-076: a cached effect descriptor set together with the sampled VkImageViews it
        // was written against. The seven per-frame effect descriptor caches below key on a *hash* of
        // raw VkImageView handle values and persist across frames with no per-view free path. A view
        // handle is recyclable once its view is destroyed (GFX-075 retirement only *defers* the free
        // past the consuming frame's fence -- it does not keep the value reserved forever), so a
        // stale hash-keyed entry could later be handed to a different resource that reuses the same
        // VkImageView value, sampling the destroyed image. Recording each entry's referencing views
        // lets EvictSampledViewFromCaches() drop (and fence-retire) every entry a dying view
        // participates in -- exactly as texSamplerDescSets_ is already evicted per (view,sampler)
        // key -- closing the reuse-aliasing window and giving these caches a bounded free path.
        // Padded to the max sampled-view count of any effect (PbrEffect/SkinnedPbrEffect: 5).
        static constexpr std::size_t kMaxEffectSampledViews = 5;
        struct EffectDescSetEntry {
            VkDescriptorSet                                  set = VK_NULL_HANDLE;
            std::array<VkImageView, kMaxEffectSampledViews>  views{}; // VK_NULL_HANDLE-padded
        };
        using EffectDescSetCache =
            std::array<std::unordered_map<uint64_t, EffectDescSetEntry>, MaxFramesInFlight>;

        // --- Pipeline resources (permanent) ---
        VkSampler             defaultSampler_        = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_   = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool_        = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout2D_      = VK_NULL_HANDLE;
        // Task 911: depth-format-keyed, mirrors pipelines2DMsaaByDepthFmt_ above.
        // REMED-GFX-071: same (depth-format + blend) PipelineKey as the MSAA map above.
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines2DByDepthFmt_;
        VkPipelineLayout      pipelineLayout3D_      = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelines3D_;
        VkPipelineLayout      pipelineLayoutExt3D_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutAlphaTest3D_ = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesAlphaTest3D_;
        VkDescriptorSetLayout descriptorSetLayout2Tex_     = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool2Tex_          = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutDualTex3D_     = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesDualTex3D_;
        // Task 899: per-frame cache (was a single flat map) -- binding=2's fog UBO now makes the
        // descriptor set's buffer binding frame-specific, mirroring litTexturedDescSets_/skinnedDescSets_.
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
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
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesEnvMap3D_;
        // Per-frame descriptor set cache: key = hash(view2D, viewCube)
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
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
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesLitTextured3D_;
        // Task 1103: PreferPerPixelLighting=false (XNA's real default) sibling pipeline cache --
        // same descriptor set layout/pipeline layout as pipelinesLitTextured3D_ above, different
        // shader modules only (lighting moved into the vertex stage).
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesLitTextured3DVertexLit_;
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
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
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesFogColored3D_;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesFogTex3D_; // textured+coloredTextured, keyed by stride
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
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
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>              pipelinesSkinned3D_;
        // Task 1103: PreferPerPixelLighting=false (XNA's real default) sibling pipeline cache --
        // same descriptor set layout/pipeline layout as pipelinesSkinned3D_ above, different
        // shader modules only.
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>              pipelinesSkinned3DVertexLit_;
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
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
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelinesInstanced3D_;

        // PbrEffect resources (unskinned, stride 48: VertexPositionNormalTangentTexture).
        // 5 combined image samplers (baseColor@0, normalMap@1, metallicRoughnessMap@2,
        // emissiveMap@3, occlusionMap@4) + 1 dynamic UBO (PbrParams@5, world/lights1-2/emissive/
        // eyePos/metallic-roughness/fog -- everything FillExtPushConst's 128-byte PC has no room
        // for), mirroring descriptorSetLayoutSkinned_'s sampler+dynamic-UBO shape.
        VkDescriptorSetLayout descriptorSetLayoutPbr_ = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolPbr_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutPbr3D_    = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesPbr3D_;
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
                   MaxFramesInFlight>                        pbrDescSets_;
        static constexpr uint32_t kPbrUBOStride   = 256; // 192 bytes used (48 floats), padded to 256
        static constexpr uint32_t kPbrUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> pbrUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> pbrUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> pbrUBOPtr_ = {};

        // SkinnedPbrEffect resources (PBR + skinning combo, stride 68:
        // VertexPositionNormalTangentTextureSkinned). Same 5 samplers as descriptorSetLayoutPbr_
        // above, plus a dynamic bone-palette UBO (binding=5, same shape as
        // descriptorSetLayoutSkinned_'s own BoneBlock) and a PbrParams dynamic UBO at binding=6
        // (WeightsPerVertex packed alongside the fog vector, mirroring
        // skinned3d.vert.glsl's fog.eyePos_pad.w packing trick).
        VkDescriptorSetLayout descriptorSetLayoutPbrSkinned_ = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolPbrSkinned_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutPbrSkinned3D_    = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesPbrSkinned3D_;
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
                   MaxFramesInFlight>                        pbrSkinnedDescSets_;
        static constexpr uint32_t kPbrSkinnedBoneUBOStride   = 4608; // 72×64, multiple of 256
        static constexpr uint32_t kPbrSkinnedBoneUBOMaxDraws = 32;
        std::array<VkBuffer,       MaxFramesInFlight> pbrSkinnedBoneUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> pbrSkinnedBoneUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> pbrSkinnedBoneUBOPtr_ = {};
        static constexpr uint32_t kPbrSkinnedUBOStride   = 256; // 192 bytes used (48 floats), padded to 256
        static constexpr uint32_t kPbrSkinnedUBOMaxDraws = 32;
        std::array<VkBuffer,       MaxFramesInFlight> pbrSkinnedUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> pbrSkinnedUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> pbrSkinnedUBOPtr_ = {};

        // Default 1×1 white texture used when DrawPrimitivesEx has no texture bound.
        VkImage               defaultWhiteImage_     = VK_NULL_HANDLE;
        VkDeviceMemory        defaultWhiteMemory_    = VK_NULL_HANDLE;
        VkImageView           defaultWhiteView_      = VK_NULL_HANDLE;
        VkDescriptorSet       defaultWhiteDescSet_   = VK_NULL_HANDLE;

        // Default 1×1 "flat" tangent-space normal texture (128,128,255,255 -> decodes to
        // (0,0,1)) used when a PbrEffect/SkinnedPbrEffect draw has no NormalMap bound, mirroring
        // EasyGLGraphicsBackend::EnsureDefaultFlatNormalTexture()'s own fallback semantics.
        VkImage               defaultFlatNormalImage_  = VK_NULL_HANDLE;
        VkDeviceMemory        defaultFlatNormalMemory_ = VK_NULL_HANDLE;
        VkImageView            defaultFlatNormalView_  = VK_NULL_HANDLE;

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

        // --- MRT proxy (one active binding; old bindings retire through the frame fence) ---
        std::unique_ptr<VulkanMRTProxy>          mrtProxy_;
        VkSampleCountFlagBits lastMrtPipelineSampleCountEXT_ = VK_SAMPLE_COUNT_1_BIT;
        uint32_t              lastMrtPipelineColorCountEXT_ = 0;

        // --- MRT render pass cache (target count + samples + shared depth format) ---
        std::unordered_map<uint64_t, VkRenderPass> mrtRenderPasses_;

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
            BlendKeyParams          blendParams;  // Task 868: real per-channel blend factors/funcs
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
            float                   fogTex3DUboData[8] = {}; // vec4 fogColorEnabled + vec4 fogVector
            VkDescriptorSet         fogTex3DDescSet   = VK_NULL_HANDLE;
            bool                    useAlphaTest      = false; // true = AlphaTest3D pipeline
            bool                    useDualTexture    = false; // true = DualTex3D pipeline
            VkDescriptorSet         dualTexDescSet    = VK_NULL_HANDLE; // 2-sampler set
            float                   dualTexFogUboData[8] = {}; // vec4 fogColorEnabled + vec4 fogVector (Task 899)
            bool                    useEnvMap         = false; // true = EnvMap3D pipeline
            float                   envMapPC[32]      = {};    // push consts: [0..15]=mvp, [16..31]=world
            // 12×vec4 = 192 bytes for env map UBO (8 original [0..31], see Task 899's own
            // comment on the original 6 + fog pair, + 4 more for Task 890's
            // light1Dir_pad/light1Diff_pad/light2Dir_pad/light2Diff_pad at [32..47]).
            float                   envMapUboData[48] = {};
            VkDescriptorSet         envMapDescSet     = VK_NULL_HANDLE;
            bool                    useSkinned        = false; // true = Skinned3D pipeline
            std::vector<float>      boneMatrices;              // up to 72 mat4s = 1152 floats
            VkDescriptorSet         skinnedDescSet    = VK_NULL_HANDLE;
            // vec4 fogColorEnabled + vec4 fogVector (Task 899); [8..23] Task 893's
            // DirectionalLight1/2 dir+diffuse; [24..59] Task 894's World (mat4, 16 floats),
            // eyePosition_pad, specularColor_specularPower, light0/1/2Specular_pad (4 more vec4);
            // [60..63] REMED-GFX-008's emissiveColor vec4 (pre-folded emissive+ambient*diffuse).
            // 64 floats = 256 bytes = kSkinnedFogUBOStride exactly.
            float                   skinnedFogUboData[64] = {};
            bool                    usePbr            = false; // true = Pbr3D pipeline (unskinned)
            bool                    usePbrSkinned     = false; // true = PbrSkinned3D pipeline (combo)
            VkDescriptorSet         pbrDescSet        = VK_NULL_HANDLE; // 5-sampler set
            // PbrParams UBO layout (floats), matching pbr3d.vert/frag.glsl's and
            // pbr3d_skinned.vert/frag.glsl's own struct exactly: [0..15]=light1/2 dir+diffuse (4
            // vec4), [16..31]=world mat4, [32..35]=eyePos+metallicFactor, [36..39]=emissive+
            // roughnessFactor, [40..43]=fogColor+weightsPerVertex, [44..47]=fogVector. 48 floats =
            // 192 bytes, under kPbrUBOStride=256. usePbrSkinned also reuses boneMatrices above.
            float                   pbrUboData[48]    = {};
            bool                    useLitTextured    = false; // true = LitTextured3D pipeline (Task 897)
            // Task 1103: true = select the PreferPerPixelLighting=false (XNA's real default)
            // per-vertex-lit pipeline sibling instead of the (historically always-selected)
            // per-pixel-lit one, for whichever of useLitTextured/useSkinned is set. Only
            // meaningful when lightingEnabled is also true (see the enqueue sites).
            bool                    preferVertexLit   = false;
            // Layout (floats): [0..19]=light1/2 dir+diffuse+emissive (5 vec4, Task 897),
            // [20..35]=world mat4, [36..39]=eyePos, [40..51]=light0/1/2 specular (3 vec4),
            // [52..55]=specularColor+specularPower (Task 886/898), [56..59]=fogColor+fogEnabled,
            // [60..63]=fogVector. 256 bytes total.
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
            // Task 870: full DepthStencilState snapshot at draw-call time. depthFunc/stencil*
            // (everything baked into the pipeline) feed DepthStencilKeyParams in draw3DFor;
            // stencilReadMask/stencilWriteMask/referenceStencil are true Vulkan dynamic state,
            // applied directly via vkCmdSetStencilCompareMask/WriteMask/Reference per draw.
            DepthStencilKeyParams   dsParams;
            int                     stencilReadMask   = -1;  // all bits (0xFFFFFFFF, XNA default)
            int                     stencilWriteMask  = -1;
            int                     referenceStencil  = 0;
            // Debug marker (SetStringMarkerEXT) — if true, vbData is empty and this entry
            // emits vkCmdInsertDebugUtilsLabelEXT instead of a draw call.
            bool                    isMarker          = false;
            std::string             markerLabel;
            // Task 447/854: the OcclusionQuery active (via Begin()/End()) when this draw was
            // submitted, nullptr if none. Set uniformly by PushPending3DDraw() so every draw
            // call site gets query correlation for free. RecordCommandBuffer() wraps each
            // contiguous run of draws sharing the same non-null pointer, within a single
            // targetRT's render pass, in a real vkCmdBeginQuery/vkCmdEndQuery pair.
            VulkanOcclusionQueryBackend* occlusionQuery = nullptr;
            // REMED-GFX-013: scissor state snapshotted at enqueue time (PushPending3DDraw), so it
            // survives the frame-global scissor being overwritten before Present() records this
            // draw -- notably by Task 338's ScissorRectangle reset on render-target unbind.
            // scissorEnabled==false (or a zero-sized rect) means "no clip" (whole framebuffer),
            // matching the backbuffer pass's own long-standing guard.
            bool                    scissorEnabled = false;
            int32_t                 scissorX = 0, scissorY = 0;
            uint32_t                scissorW = 0, scissorH = 0;
            // REMED-GFX-062: viewport state snapshotted at enqueue time (PushPending3DDraw), so the
            // render-target pass honors the Viewport that was active while its RT was bound, not the
            // frame-global viewport left over at Present() -- notably the full-size reset
            // SetRenderTarget applies on RT bind/unbind (GraphicsDevice::
            // ResetViewportAndScissorForRenderTarget, the FNA-parity analog of Task 338's scissor
            // reset). viewportSet==false (or a zero-sized rect) means "full target", matching the
            // backbuffer pass's own long-standing guard.
            bool                    viewportSet = false;
            int32_t                 viewportX = 0, viewportY = 0;
            uint32_t                viewportW = 0, viewportH = 0;
            float                   viewportMinDepth = 0.0f, viewportMaxDepth = 1.0f;
            // REMED-GFX-070: blend-constant (GraphicsDevice.BlendFactor) snapshotted at enqueue time
            // (PushPending3DDraw), so each queued draw replays the constant that was active when it
            // was issued. Pre-fix, RT passes never set the blend constant at all, and multiple
            // BlendFactor values in one frame collapsed to the single record-time read (last-wins).
            // Defaults to (1,1,1,1) = XNA Color::White. Replayed per draw via
            // vkCmdSetBlendConstants only when the normalized blend equation uses
            // BlendFactor/InverseBlendFactor (VK_DYNAMIC_STATE_BLEND_CONSTANTS, REMED-GFX-091).
            float                   blendFactorR = 1.0f, blendFactorG = 1.0f,
                                    blendFactorB = 1.0f, blendFactorA = 1.0f;
        };
        std::vector<Pending3DDraw>  pending3D_;
        // Task 447/854: pushes d onto pending3D_ after tagging it with the currently-active
        // OcclusionQuery (if any) -- the single choke point every DrawXPrimitives() call site
        // routes through, so query correlation doesn't need repeating at each of the 6 push
        // sites individually.
        void PushPending3DDraw(Pending3DDraw&& d);
        // Task 447/854: set by VulkanOcclusionQueryBackend::Begin(), cleared by End() (only if
        // still pointing at itself, mirroring Bgfx's activeOcclusionQuery_ convention). Nested
        // Begin() calls are not supported by XNA's own OcclusionQuery API (one query in flight
        // at a time is the real, documented XNA usage pattern), so a single raw pointer (not a
        // stack) is sufficient.
        VulkanOcclusionQueryBackend* activeOcclusionQuery_ = nullptr;
        // pair: (an independent per-Begin/End-cycle snapshot, target RT) where RT=nullptr means
        // backbuffer. Owns each snapshot outright (Task 664 fix) so that a 2nd Begin()/End() on
        // the same SpriteBatch object within one frame cannot clobber the 1st cycle's data —
        // each snapshot is pushed at End(), independent heap storage, harvested (and destroyed
        // via activeBatches_.clear()) once per frame in RecordCommandBuffer().
        std::vector<std::pair<std::unique_ptr<VulkanSpriteBatchBackend::BatchSnapshot>, VulkanRTSource*>> activeBatches_;

        // Task 875: render targets explicitly `Clear()`-ed this frame with no accompanying draw
        // call. `RecordCommandBuffer`'s `usedRTs` list was previously built purely from
        // `activeBatches_`/`pending3D_` (both draw-call-populated), so a real, XNA-legal
        // "SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);" pattern with no draw in
        // between never got its render pass recorded at all — the target's colour image stayed
        // at VK_IMAGE_LAYOUT_UNDEFINED forever. `Clear()`/`ClearColorAndDepth()` push `currentRT_`
        // here (when a render target is bound) so `usedRTs` picks it up even with zero draws;
        // cleared alongside `activeBatches_`/`pending3D_` once per frame in `RecordCommandBuffer()`.
        std::vector<VulkanRTSource*> clearedRTs_;

        // REMED-GFX-075: deferred-resource retirement queue -- the generic ownership mechanism that
        // makes the whole-frame deferred renderer memory-safe against a SOURCE resource
        // (Texture2D/TextureCube/RenderTarget2D-as-sampler/custom Effect/OcclusionQuery) destroyed
        // after its draw was queued but before Present()/GetData records it. A resource's destructor
        // no longer frees its Vulkan handles immediately; it hands them here, tagged with the current
        // frameGeneration_. ProcessRetiredResources() frees a bucket only once MaxFramesInFlight
        // generations have elapsed past the frame that consumed the entries referencing it -- i.e.
        // after that frame's fence has certainly signalled -- covering BOTH the CPU record window
        // (a deferred entry still borrows the handle) and the GPU execution window (submitted work
        // still reads it), with no vkDeviceWaitIdle in the ordinary destruction path. This replaces
        // the previous per-destroy device stall and subsumes it (a strictly wider safety window).
        struct RetiredResources {
            uint64_t                       generation = 0;
            std::vector<VkImageView>       imageViews;
            std::vector<VkImage>           images;
            std::vector<VkDeviceMemory>    memories;
            std::vector<VkFramebuffer>     framebuffers;
            std::vector<VkPipeline>        pipelines;
            std::vector<VkPipelineLayout>  pipelineLayouts;
            std::vector<VkShaderModule>    shaderModules;
            std::vector<VkQueryPool>       queryPools;
            std::vector<VkDescriptorSet>   descriptorSets; // all allocated from descriptorPool_
            // REMED-GFX-076: effect descriptor sets evicted from the seven per-frame effect caches
            // when a sampled view they reference dies. Unlike `descriptorSets` (all from
            // descriptorPool_), each is freed from its OWN pool, so the pool is carried with the set.
            std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>> poolDescriptorSets;
        };
        std::vector<RetiredResources>                                    retiredResources_;
        // MRT proxies are retired as whole objects (they are VulkanRTSource DESTINATIONS referenced
        // by their pointer in the deferred queues): SetRenderTargets() replaces the live proxy while
        // its queued render work legitimately remains, so the old proxy is kept alive here until the
        // consuming frame drains, instead of being destroyed out from under those entries.
        std::vector<std::pair<uint64_t, std::unique_ptr<VulkanMRTProxy>>> retiredMrtProxies_;
        // Monotonic count of Full frame records submitted; the retirement generation clock.
        uint64_t frameGeneration_ = 0;

        // REMED-GFX-075: hand a bundle of Vulkan handles to the retirement queue (tags it with the
        // current frameGeneration_). Called from every user-destroyable resource's destructor.
        void RetireResources(RetiredResources&& r);
        // REMED-GFX-075: drop every texSamplerDescSets_ entry keyed on `view` (a dying sampled
        // texture/RT view), moving its cached VkDescriptorSet into `into` for retirement -- so a
        // later resource that happens to reuse the freed VkImageView handle value can never collide
        // with a stale cached descriptor set.
        void EvictSampledViewFromCaches(VkImageView view, RetiredResources& into);
        // REMED-GFX-076: erase (and fence-retire to `pool`) every entry in one effect descriptor-set
        // cache that references `view`, so a later resource reusing the freed VkImageView handle
        // value gets a fresh descriptor set rather than aliasing this (now-destroyed) one. Called
        // once per effect cache from EvictSampledViewFromCaches().
        void EvictViewFromEffectCache(EffectDescSetCache& caches, VkDescriptorPool pool,
                                      VkImageView view, RetiredResources& into);
    public:
        // REMED-GFX-076: read-only test introspection -- total live entries across all seven
        // per-frame effect descriptor-set caches. The resource-identity regression uses it to prove
        // a destroyed sampled resource's cached sets are evicted (count returns to baseline). No
        // effect on rendering.
        NOXNA [[nodiscard]] std::size_t TotalEffectDescSetEntriesForTests() const;
        // REMED-GFX-076: read-only test introspection -- number of effect-cache entries that
        // reference the given VkImageView handle value (cast to uint64_t). Proves a specific view's
        // entries are gone after its resource dies, and detects a later resource reusing the handle.
        NOXNA [[nodiscard]] std::size_t EffectDescSetEntriesForViewInTests(uint64_t rawImageViewHandle) const;
    private:
        // REMED-GFX-075: free every retirement bucket whose consuming frame's fence has certainly
        // completed (generation + MaxFramesInFlight < frameGeneration_); `force` frees all of them
        // (used at teardown, after a full device wait). Run once per frame from SubmitFrame().
        void ProcessRetiredResources(bool force);
        // REMED-GFX-075: detach a dying OcclusionQuery from every pending 3D draw (nulls the borrowed
        // pointer, keeping the draw) and from activeOcclusionQuery_, so RecordCommandBuffer never
        // dereferences the freed query wrapper. The VkQueryPool itself is retired separately.
        void PurgeDeferredQuery(VulkanOcclusionQueryBackend* q);

        // Cached vkCmdInsertDebugUtilsLabelEXT — loaded once after device creation, nullptr if unsupported.
        PFN_vkCmdInsertDebugUtilsLabelEXT pfnCmdInsertDebugLabel_ = nullptr;

        // --- Virtual (game) resolution for 2D NDC mapping ---
        int virtualWidth_  = 0;
        int virtualHeight_ = 0;

        // --- Frame state ---
        uint32_t currentFrame_            = 0;
        uint32_t lastPresentedImageIndex_ = 0;
        float    clearR_ = 0.f, clearG_ = 0.f, clearB_ = 0.f, clearA_ = 1.f;
        // Task 871: threaded into every render pass's VkClearValue.depthStencil.stencil field,
        // replacing a previously-hardcoded clear value of 0.
        int      clearStencil_ = 0;
        // Task 950: threaded into every render pass's VkClearValue.depthStencil.depth field,
        // replacing a previously-hardcoded clear value of 1.0f (mirrors clearStencil_ exactly).
        float    clearDepth_ = 1.0f;
        bool     initialized_       = false;
        VulkanRTSource*            currentRT_ = nullptr;
        bool     depthTestEnabled_  = true;
        bool     depthWriteEnabled_ = true;
        bool     blendEnabled_      = false;
        // Task 868: the real requested blend factors/functions, previously entirely discarded by
        // ApplyBlendState (only the enabled-or-not boolean above was ever kept).
        BlendKeyParams blendParams_;
        int      cullMode_          = 0;  // XNA CullMode: 0=None, 1=CW, 2=CCW
        // Task 870: full DepthStencilState, previously almost entirely dropped by
        // ApplyDepthStencilState (only depthTestEnabled_/depthWriteEnabled_ were ever stored).
        // dsParams_ mirrors DepthStencilKeyParams exactly (the fields baked into a pipeline at
        // creation time); the remaining 3 are true Vulkan dynamic state.
        DepthStencilKeyParams dsParams_;
        int      stencilReadMask_  = -1;  // all bits (0xFFFFFFFF, XNA DepthStencilState.Default)
        int      stencilWriteMask_ = -1;
        int      referenceStencil_ = 0;

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
        // Task 911: lazily creates (and caches) the RT render pass for a specific real depth
        // VkFormat -- VK_FORMAT_UNDEFINED means "no depth attachment at all" (DepthFormat::None).
        // discardContents selects LOAD_OP_CLEAR (false) vs LOAD_OP_LOAD (true); a pipeline
        // created against the discard variant is render-pass-compatible with the load variant
        // too (they differ only in loadOp/initialLayout, which compatibility ignores), so callers
        // that only need a pipeline's *reference* render pass should always pass false.
        VkRenderPass GetOrCreateRTRenderPass(VkFormat depthFmt, bool discardContents);
        // Task 911: MSAA counterpart -- DiscardContents-shaped only, mirrors the pre-existing
        // rtRenderPassMsaa_ scope decision (PreserveContents+MSAA was never given its own
        // LOAD_OP_LOAD variant).
        VkRenderPass GetOrCreateRTRenderPassMsaa(VkFormat depthFmt);
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
        // Task 911: lazily creates (and caches) the 2D sprite pipeline for a specific real depth
        // VkFormat -- VK_FORMAT_UNDEFINED means "no depth attachment" (DepthFormat::None), mirrors
        // GetOrCreateRTRenderPass()'s own depth-format-keyed caching. Uses PickRTPipelineRenderPass
        // for the reference render pass, same as every 3D pipeline creation function.
        // REMED-GFX-071: also parameterized by the batch's BlendState (blend enable + per-channel
        // factors/functions) so SpriteBatch.Begin()'s BlendState drives the colour-attachment blend
        // equation via FillBlendAttachmentState, instead of a hardcoded alpha-blend.
        VkPipeline GetOrCreatePipeline2D(VkFormat depthFmt, uint32_t colorAttachmentCount,
                                         bool blend, const BlendKeyParams& bp);
        VkFormat   FindDepthFormat() const;
        void       CreateDepthResources();
        void       CleanupDepthResources();
        VkPipeline GetOrCreatePipeline3D(VkPrimitiveTopology, bool depthTest, bool depthWrite,
                                         bool blend, int cullMode,
                                         uint32_t colorAttachmentCount, bool wireframe,
                                         bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        VkPipeline GetOrCreatePipelineAlphaTest3D(std::size_t stride, VkPrimitiveTopology,
                                                   bool depthTest, bool depthWrite,
                                                   bool blend, int cullMode,
                                                   uint32_t colorAttachmentCount, bool wireframe,
                                                   bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void       EnsureDualTexResources();
        VkDescriptorSet GetOrCreateDualTexDescSet(uint32_t frameIdx, VkImageView view0, VkImageView view1,
                                                    VkSampler sampler0, VkSampler sampler1);
        VkPipeline GetOrCreatePipelineDualTex3D(std::size_t stride, VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount, bool wireframe,
                                                bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // EnvironmentMapEffect
        void       EnsureEnvMapResources();
        VkDescriptorSet GetOrCreateEnvMapDescSet(uint32_t frameIdx,
                                                  VkImageView view2D, VkImageView viewCube);
        VkPipeline GetOrCreatePipelineEnvMap3D(VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount, bool wireframe,
                                                bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void       FillEnvMapPushConst(float (&pc)[32], const Matrix& wvp, const Matrix& world);
        // SkinnedEffect
        void       EnsureSkinnedResources();
        VkDescriptorSet GetOrCreateSkinnedDescSet(uint32_t frameIdx, VkImageView view2D);
        // `stride` selects the vertex layout/shader variant: 52 = VertexPositionNormalTextureSkinned
        // (no per-vertex color), 56 = the same layout with a per-vertex Color appended (CNB-67 /
        // SkinnedEffect::VertexColorEnabled) -- mirrors GetOrCreatePipelineDualTex3D's own
        // stride-selects-shader-variant convention.
        VkPipeline GetOrCreatePipelineSkinned3D(std::size_t stride, VkPrimitiveTopology,
                                                 bool depthTest, bool depthWrite,
                                                 bool blend, int cullMode,
                                                 uint32_t colorAttachmentCount, bool wireframe,
                                                 bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // Task 1103: PreferPerPixelLighting=false sibling of GetOrCreatePipelineSkinned3D above
        // (real per-vertex/Gouraud lighting, XNA's own default) — same signature/layout, different
        // shader modules and pipeline cache only.
        VkPipeline GetOrCreatePipelineSkinned3DVertexLit(std::size_t stride, VkPrimitiveTopology,
                                                 bool depthTest, bool depthWrite,
                                                 bool blend, int cullMode,
                                                 uint32_t colorAttachmentCount, bool wireframe,
                                                 bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // PbrEffect (unskinned, stride 48) / SkinnedPbrEffect (PBR + skinning combo, stride 68).
        // Metallic-roughness BRDF ported from EasyGLGraphicsBackend::EnsurePbrProgram()/
        // EnsurePbrSkinnedProgram() unchanged; only the resource-binding plumbing differs.
        void       EnsurePbrResources();
        VkDescriptorSet GetOrCreatePbrDescSet(uint32_t frameIdx, VkImageView baseColor,
                                               VkImageView normalMap, VkImageView metallicRoughness,
                                               VkImageView emissive, VkImageView occlusion);
        VkPipeline GetOrCreatePipelinePbr3D(VkPrimitiveTopology,
                                             bool depthTest, bool depthWrite,
                                             bool blend, int cullMode,
                                             uint32_t colorAttachmentCount, bool wireframe,
                                             bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void       EnsurePbrSkinnedResources();
        VkDescriptorSet GetOrCreatePbrSkinnedDescSet(uint32_t frameIdx, VkImageView baseColor,
                                                      VkImageView normalMap, VkImageView metallicRoughness,
                                                      VkImageView emissive, VkImageView occlusion);
        VkPipeline GetOrCreatePipelinePbrSkinned3D(VkPrimitiveTopology,
                                             bool depthTest, bool depthWrite,
                                             bool blend, int cullMode,
                                             uint32_t colorAttachmentCount, bool wireframe,
                                             bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void       EnsureDefaultWhiteTexture();
        void       EnsureDefaultFlatNormalTexture();
        void       FillExtPushConst(float (&pc)[32], const Matrix& wvp, const GpuDrawParams& p);
        void       FillAlphaTestPushConst(float (&pc)[32], const Matrix& wvp, const GpuDrawParams& p);
        // Fills the 48-float PbrParams UBO layout shared by pbr3d.vert/frag.glsl and
        // pbr3d_skinned.vert/frag.glsl (see Pending3DDraw::pbrUboData's own layout comment).
        // weightsPerVertex is only meaningful for the pbr+skinned combo (stride 68); pass 0 for
        // the unskinned PbrEffect path (stride 48), where it's unused.
        void       FillPbrUboData(float (&out)[48], const GpuDrawParams& p, float weightsPerVertex);
        // BasicEffect lit-textured path (Task 897) — DirectionalLight1/2 + EmissiveColor,
        // forwarded via a small UBO (set=0,binding=1) alongside the unchanged 128-byte PC
        // (set=0,binding=0 stays the texture sampler; PC content unchanged from FillExtPushConst).
        void       EnsureLitTexturedResources();
        VkDescriptorSet GetOrCreateLitTexturedDescSet(uint32_t frameIdx, VkImageView view2D);
        VkPipeline GetOrCreatePipelineLitTextured3D(VkPrimitiveTopology,
                                                     bool depthTest, bool depthWrite,
                                                     bool blend, int cullMode,
                                                     uint32_t colorAttachmentCount, bool wireframe,
                                                     bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // Task 1103: PreferPerPixelLighting=false sibling of GetOrCreatePipelineLitTextured3D
        // above (real per-vertex/Gouraud lighting, XNA's own default) — same signature/layout,
        // different shader modules and pipeline cache only.
        VkPipeline GetOrCreatePipelineLitTextured3DVertexLit(VkPrimitiveTopology,
                                                     bool depthTest, bool depthWrite,
                                                     bool blend, int cullMode,
                                                     uint32_t colorAttachmentCount, bool wireframe,
                                                     bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // BasicEffect fog bundle (Task 899) — shared by colored3d/textured3d/colored_textured3d.
        void       EnsureFogTex3DResources();
        VkDescriptorSet GetOrCreateFogTex3DDescSet(uint32_t frameIdx, VkImageView view2D);
        VkPipeline GetOrCreatePipelineFogColored3D(VkPrimitiveTopology,
                                                    bool depthTest, bool depthWrite,
                                                    bool blend, int cullMode,
                                                    uint32_t colorAttachmentCount, bool wireframe,
                                                    bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        VkPipeline GetOrCreatePipelineFogTex3D(std::size_t stride, VkPrimitiveTopology,
                                                bool depthTest, bool depthWrite,
                                                bool blend, int cullMode,
                                                uint32_t colorAttachmentCount, bool wireframe,
                                                bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        // --- Instanced 3D pipeline ---
        VkPipeline GetOrCreatePipelineInstanced3D(std::size_t pvStride, VkPrimitiveTopology,
                                                   bool depthTest, bool depthWrite,
                                                   bool blend, int cullMode,
                                                   uint32_t colorAttachmentCount, bool wireframe,
                                                   bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void FillInstancedPushConst(float (&pc)[32], const Matrix& view, const Matrix& proj,
                                    const GpuDrawParams& p);
        void CreateFrame3DInstBuffers();
        void EnsureFrame3DInstBuffers();

        void CreateMsaaColorResources();
        void CleanupMsaaColorResources();
        void CreateRenderPassMsaa();
        // Task 911: MSAA counterpart to GetOrCreatePipeline2D(), same depth-format-keyed caching.
        // REMED-GFX-071: also BlendState-parameterized, see GetOrCreatePipeline2D().
        VkPipeline GetOrCreatePipeline2DMsaa(VkFormat depthFmt, uint32_t colorAttachmentCount,
                                             bool blend, const BlendKeyParams& bp);

        void CreateSpriteBuffers();
        void CreateFrame3DBuffers();
        void EnsureFrame3DBuffers();
        VkRenderPass GetOrCreateMRTRenderPass(uint32_t colorAttachmentCount,
                                              VkSampleCountFlagBits sampleCount,
                                              VkFormat depthFormat);
        // The render-pass-selection decision shared by every 2D/custom/3D pipeline. MRT uses a
        // pass keyed by color count, sample count, and binding 0's depth format; single-target
        // draws reuse compatible backbuffer passes or a depth-format-keyed RT pass.
        VkRenderPass PickRTPipelineRenderPass(uint32_t colorAttachmentCount, bool msaa,
                                               VkFormat targetDepthFmt);

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
        // REMED-GFX-074: Full records the whole deferred frame (all render-target passes + the
        // backbuffer pass) and clears every pending queue, as before. RenderTargetsOnly records
        // ONLY `onlyRT`'s off-screen pass into a transient command buffer for a mid-frame GetData
        // readback flush -- it skips the backbuffer pass entirely (no swapchain image needed, so
        // `imageIndex` is unused) and consumes only that target's deferred entries, leaving every
        // other target's and the backbuffer's pending work intact for the eventual real Present().
        enum class RecordMode { Full, RenderTargetsOnly };
        void RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex,
                                 RecordMode mode = RecordMode::Full,
                                 VulkanRTSource* onlyRT = nullptr);

        // REMED-GFX-074: drop every deferred sprite-batch / 3D-draw / clear entry whose target is
        // `rt` from the pending queues, so a render target destroyed before Present() cannot leave
        // a dangling VulkanRTSource* for RecordCommandBuffer() to dereference. Called from each
        // render-target backend destructor. A destroyed target is unobservable (its GetData can no
        // longer be called, and sampling its freed image is already broken independently), so
        // discarding its unflushed draws is observationally identical to XNA's eager model where
        // those draws had already executed by the time the resource was disposed.
        void PurgeDeferredWorkForTarget(VulkanRTSource* rt);

        // REMED-GFX-074: if `rt` has pending deferred work, record + submit ONLY its off-screen
        // pass now (no present, no swapchain, no frame-bookkeeping advance) so its colour image
        // holds the rendered result before a GetData readback, then drop the consumed entries so
        // Present() never replays them (no double-render). No-op if nothing is queued for `rt`.
        void FlushDeferredRenderTarget(VulkanRTSource* rt);

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
