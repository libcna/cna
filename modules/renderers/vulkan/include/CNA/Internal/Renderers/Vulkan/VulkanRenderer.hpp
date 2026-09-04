#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Vulkan/VulkanCompiledEffect.hpp"
#endif
#include "CNA/Internal/Renderers/Common/PlatformVulkanRendererState.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace CNA::Internal::Renderers::Vulkan
{
    class VulkanRenderer;            // forward
    class VulkanRenderTargetRenderer;        // forward
    class VulkanRenderTargetCubeRenderer;    // forward

    // -------------------------------------------------------------------------
    // Vertex types (internal to the Vulkan renderer)
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
        /// VulkanRenderTargetRenderer/VulkanRenderTargetCubeRenderer with their own instance's
        /// picked format (see PickDepthFormat()). Backbuffer draws don't go through a
        /// VulkanRTSource at all, so callers use depthFormat_ directly for that case.
        virtual VkFormat      GetDepthFormat()               const { return VK_FORMAT_UNDEFINED; }
        /// Task 878: regenerate this RT's mip chain (no-op unless the concrete RT actually owns
        /// mip levels beyond 0). Called by RecordCommandBuffer right after this RT's render pass
        /// ends, once per frame it was actually rendered into.
        virtual void          MaybeGenerateMips(VkCommandBuffer /*cb*/) {}
        /// REMED-GFX-129: true when this source's render pass begins its colour attachments with
        /// VK_ATTACHMENT_LOAD_OP_CLEAR, which is the only condition under which a LEADING Clear()
        /// may be folded into the load action instead of being recorded as an ordered
        /// vkCmdClearAttachments. Default true: the MSAA and MRT render passes are
        /// DiscardContents-shaped unconditionally. Overridden where RenderTargetUsage decides.
        virtual bool          ColorLoadOpIsClearEXT()      const { return true; }
        /**
         * REMED-GFX-142: which depth/stencil storage this source renders into, as an identity.
         *
         * Colour is per-source -- a RenderTarget2D owns its image, a cube face owns its layer --
         * but depth/stencil is per TARGET: FNA's RenderTargetCube allocates exactly one
         * glDepthStencilBuffer for the whole cube and CNA mirrors that, so all six FaceProxies
         * share one depth image. REMED-GFX-074's single-target readback flush filters recorded
         * segments by `seg.rt == onlyRT`, which is right for colour and wrong for that shared
         * depth: reading face B replayed only face B's bind cycles, so face A's depth writes were
         * still pending and face B's own earlier depth clear was recorded AFTER them. The filter
         * asks this instead, so every face of one cube flushes together and the shared buffer sees
         * the public command order. Default `this` -- a source with no shared depth is its own
         * group, and the filter degenerates to the pointer comparison it always was.
         */
        virtual const void*   DepthStencilOwnerEXT()      const { return this; }
        virtual ~VulkanRTSource() = default;
    };

    /**
     * @brief REMED-GFX-166: one deferred render-pass DESTINATION, owned independently of its wrapper.
     *
     * A `VulkanRTSource*` stored in the deferred queues used to be the render-target RENDERER object,
     * which the public wrapper owns and destroys. That made a legal public sequence -- render into a
     * target, sample it onto the backbuffer, drop the target, let the frame present -- leave dangling
     * pointers behind, and REMED-GFX-074 answered that by DISCARDING the dying target's queued work.
     * The discard is what REMED-GFX-166 measured: the work being discarded is the PRODUCER, so the
     * still-queued consumer went on to sample an image nothing had rendered into.
     *
     * A destination is therefore no longer the wrapper's object. It is this: a separately allocated,
     * IMMUTABLE value describing the pass, `shared_ptr`-owned by both the render target and every
     * deferred command queued against it. Nothing is discarded, nothing dangles, and the object's
     * address is a stable identity for as long as any command names it -- so a later target cannot
     * alias a dead one by landing on the same heap address.
     *
     * Every field is fixed at the owning target's construction (see the render-target constructors:
     * the framebuffer, the render pass, the extent and the mip chain are all created there and never
     * replaced), so a copy taken then stays correct even after `ReleaseVulkanResources()` has handed
     * the handles to the retirement queue -- which is exactly the window this fix has to survive.
     * The native handles themselves are kept valid across it by REMED-GFX-075's frame-generation
     * retirement, which already covers both the CPU record window and in-flight GPU reads.
     */
    struct VulkanTargetPassEXT final : public VulkanRTSource
    {
        /** @brief The framebuffer this pass renders into (already the MSAA one where engaged). */
        VkFramebuffer framebuffer   = VK_NULL_HANDLE;
        /** @brief The render pass this framebuffer was built against. */
        VkRenderPass  renderPass    = VK_NULL_HANDLE;
        /** @brief Render-area width in texels. */
        int           width         = 0;
        /** @brief Render-area height in texels. */
        int           height        = 0;
        /** @brief True when `framebuffer` carries multisample colour plus single-sample resolves. */
        bool          msaa          = false;
        /** @brief This target's own depth format, VK_FORMAT_UNDEFINED for no depth attachment. */
        VkFormat      depthFormat   = VK_FORMAT_UNDEFINED;
        /** @brief Whether `renderPass` begins its colour attachments with LOAD_OP_CLEAR. */
        bool          loadOpIsClear = true;
        /** @brief Shared depth/stencil group identity (REMED-GFX-142); null = this pass alone. */
        const void*   depthGroup    = nullptr;
        /** @brief The colour image whose mip chain MaybeGenerateMips regenerates. */
        VkImage       mipImage      = VK_NULL_HANDLE;
        /** @brief Mip levels `mipImage` owns; 1 disables regeneration. */
        int           mipLevels     = 1;
        /** @brief Array layer of `mipImage` this pass owns (a cube face index, else 0). */
        uint32_t      mipLayer      = 0;

        /** @brief @copydoc VulkanRTSource::GetFramebuffer */
        VkFramebuffer GetFramebuffer()          const override { return framebuffer; }
        /** @brief @copydoc VulkanRTSource::GetRenderPass */
        VkRenderPass  GetRenderPass()           const override { return renderPass; }
        /** @brief @copydoc VulkanRTSource::GetWidth */
        int           GetWidth()                const override { return width; }
        /** @brief @copydoc VulkanRTSource::GetHeight */
        int           GetHeight()               const override { return height; }
        /** @brief @copydoc VulkanRTSource::GetColorAttachmentCount */
        uint32_t      GetColorAttachmentCount() const override { return 1; }
        /** @brief @copydoc VulkanRTSource::WantsMsaa */
        bool          WantsMsaa()               const override { return msaa; }
        /** @brief @copydoc VulkanRTSource::GetDepthFormat */
        VkFormat      GetDepthFormat()          const override { return depthFormat; }
        /** @brief @copydoc VulkanRTSource::ColorLoadOpIsClearEXT */
        bool          ColorLoadOpIsClearEXT()   const override { return loadOpIsClear; }
        /** @brief @copydoc VulkanRTSource::DepthStencilOwnerEXT */
        const void*   DepthStencilOwnerEXT()    const override
        {
            return depthGroup != nullptr ? depthGroup : static_cast<const void*>(this);
        }
        /** @brief @copydoc VulkanRTSource::MaybeGenerateMips */
        void          MaybeGenerateMips(VkCommandBuffer cb) override;
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
    // IVulkanVolumeSamplable -- the Texture3D counterpart of the two above.
    //
    // plans/plan_fx.md FX-110. Kept as its own interface rather than folded into
    // IVulkanSamplable because a VkImageView carries its own view type: a
    // VK_IMAGE_VIEW_TYPE_3D view bound where the shader declared sampler2D is
    // undefined behaviour, so the three kinds must stay distinguishable by type
    // rather than by convention.
    // -------------------------------------------------------------------------

    struct IVulkanVolumeSamplable
    {
        virtual ~IVulkanVolumeSamplable() = default;
        virtual VkImageView GetVkVolumeImageView() const = 0;
    };

    // -------------------------------------------------------------------------
    // VulkanTextureRenderer
    // -------------------------------------------------------------------------

    class VulkanTextureRenderer : public ITextureRenderer, public IVulkanSamplable
    {
    public:
        explicit VulkanTextureRenderer(const ImageData& data, VulkanRenderer* owner);
        ~VulkanTextureRenderer() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }

        VkDescriptorSet GetDescriptorSet()       const { return descriptorSet_; }
        VkImageView     GetImageView()           const { return imageView_; }
        VkDescriptorSet GetVkDescriptorSet()     const override { return descriptorSet_; }
        VkImageView     GetVkImageView()         const override { return imageView_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        // Task 925 (split from Task 867): real GPU upload for level>0, mirroring
        // VulkanTexture3DRenderer::SetData's established staging-buffer pattern (Task 864).
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

    private:
        // Task 925: transitions exactly ONE mip level's layout -- the shared
        // VulkanRenderer::TransitionImageLayout always barriers level 0 regardless of
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
        VulkanRenderer* owner_      = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanRenderTargetRenderer
    // -------------------------------------------------------------------------

    class VulkanRenderTargetRenderer : public IRenderTargetRenderer, public IVulkanSamplable
    {
    public:
        // Task 911: `depthFormat` (raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal)
        // gives this instance true per-RT DepthStencilFormat fidelity -- a real, distinct
        // VkFormat picked via PickDepthFormat() (or no depth attachment at all for
        // DepthFormat::None), independent of the backbuffer's own depthFormat_ and of every
        // other render target. Each distinct depthVkFormat_ gets its own render pass (see
        // VulkanRenderer::GetOrCreateRTRenderPass()/GetOrCreateRTRenderPassMsaa()) and its
        // own pipeline cache entries (DepthStencilKeyParams no longer needs a depth-format
        // dimension since depthCompareOp/stencil ops are independent of the buffer's exact
        // format -- but the render pass itself is, since Vulkan pipeline/render-pass
        // compatibility requires an exact attachment-format match).
        VulkanRenderTargetRenderer(int w, int h, int depthFormat, bool preserveContents,
                                   VulkanRenderer* owner, int requestedMultiSampleCount = 0,
                                   bool mipMap = false);
        ~VulkanRenderTargetRenderer() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief REMED-GFX-166: this target's deferred-pass destination, shared with every command.
         *
         * The deferred queues own a share of this, so the pass outlives the public wrapper for as
         * long as any queued command still renders into it. Also the target's identity in
         * REMED-GFX-151's render-to-texture dependency graph and in REMED-GFX-074's readback flush.
         *
         * @return The shared destination description; never null for a constructed target.
         */
        CNAEXT [[nodiscard]] const std::shared_ptr<VulkanTargetPassEXT>& PassEXT() const { return pass_; }
        // Task 878/879: true once this instance actually engaged MSAA (msaaFramebuffer_ created).
        bool            WantsMsaa()                const { return msaaFramebuffer_ != VK_NULL_HANDLE; }
        // Real, renderer-clamped applied MultiSampleCount (0 if MSAA wasn't engaged — see the
        // "piggyback on the renderer's own sampleCount_" scope decision in plans/plan_graphics.md).
        int             GetMultiSampleCount()      const override { return appliedMultiSampleCount_; }
        VkDescriptorSet GetDescriptorSet()         const { return descriptorSet_; }
        VkImageView     GetColorView()             const { return colorView_; }
        // REMED-GFX-095 test diagnostics: the texture/resolve view and the transient
        // multisample view owned by this target. These are read-only renderer-internal
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

        // REMED-GFX-074: real GPU readback of this render target's colour image so that
        // RenderTarget2D::GetData() observes prior sprite/3D rendering into it even BEFORE
        // Present() runs. Vulkan defers all draw work to a single Present-time record, so this
        // first flushes any deferred passes queued into this target (FlushDeferredRenderTarget),
        // then copies colorImage_ back via a host-visible staging buffer -- mirroring
        // VulkanTexture3DRenderer::GetData's pattern, plus the swapchain BGRA->RGBA channel swap
        // (the RT colour image uses swapchainFormat_). Pre-fix this was the unimplemented base
        // no-op, so a RenderTarget2D read back before Present returned all-zeros.
        //
        // REMED-GFX-127: returns true only once the staging copy has filled the whole requested
        // region; false when this target has no usable colour image (torn-down owner/device) so the
        // shared layer rejects the read instead of converting its own zeroed scratch buffer.
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
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
        VulkanRenderer*  owner_        = nullptr;
        // REMED-GFX-166: the destination the deferred queues name, built once at the end of the
        // constructor from the immutable handles above. Shared, so a command queued against this
        // target keeps the pass alive past this wrapper's destruction instead of being discarded.
        std::shared_ptr<VulkanTargetPassEXT> pass_;
    };

    // -------------------------------------------------------------------------
    // VulkanEffectRenderer (Task 119 — SPIR-V custom Effect for Vulkan)
    // -------------------------------------------------------------------------

    struct BlendKeyParams;

    class VulkanEffectRenderer : public IEffectRenderer
    {
    public:
        explicit VulkanEffectRenderer(VulkanRenderer* owner);
        ~VulkanEffectRenderer() override;

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
        VulkanRenderer* owner_;
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
    // REMED-GFX-071: also captured (by value) into VulkanSpriteBatchRenderer::BatchSnapshot so the
    // 2D sprite pipeline honors SpriteBatch.Begin()'s BlendState, mirroring the 3D Pending3DDraw
    // path -- hence defined here, ahead of the sprite renderer that stores it.
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
    // VulkanSpriteBatchRenderer
    // -------------------------------------------------------------------------

    class VulkanSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        explicit VulkanSpriteBatchRenderer(VulkanRenderer* renderer);
        ~VulkanSpriteBatchRenderer() override = default;

        void Begin() override;
        void End()   override;

        void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }

#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        /**
         * @brief Records whether SpriteBatch is in Immediate sort mode.
         *
         * plans/plan_fx.md FX-102. Only the compiled-effect route reads it: XNA applies an Effect's
         * passes when the batch FLUSHES, which in Immediate mode is once per Draw and in every
         * other mode is once per contiguous same-texture run at End().
         * @param immediate True for SpriteSortMode.Immediate.
         */
        void SetImmediateMode(bool immediate) override { immediateMode_ = immediate; }
#endif

        // REMED-GFX-012 fix: previously unoverridden, so SpriteBatch.Begin(transformMatrix) fell
        // through to IGraphicsRenderer::SetTransformMatrix()'s shared no-op default and the
        // transform was silently discarded on Vulkan specifically (the only affected renderer of
        // 14). Stored here and applied CPU-side per vertex in Draw() (see the .cpp), mirroring
        // D3D11SpriteBatchRenderer's design -- same raw-pixel-space Sprite2DVertex/viewportSize
        // shader contract, so there is no projection-matrix uniform to fold it into GPU-side.
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }

        // Task 665 fix: previously unoverridden (silent no-op via the base class's default
        // empty bodies). Stores pending values applied via VulkanRenderer::
        // ApplySamplerState(0, ...) (Task 118's existing per-slot VkSampler cache) at flush
        // time, mirroring EasyGLSpriteBatchRenderer's exact pendingFilter_/pendingAddressU_/
        // pendingAddressV_ pattern.
        void SetSamplerFilter(int textureFilter) override { pendingFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            pendingAddressU_ = addressU;
            pendingAddressV_ = addressV;
        }

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& dest, const Rectangle& src,
                  const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects,
                  float layerDepth) override;

        struct DrawCall { VkDescriptorSet descSet; uint32_t firstIndex; uint32_t indexCount; };

        // A fully independent, frame-lifetime record of one Begin()/End() cycle's geometry.
        // Pushed onto VulkanRenderer::activeBatches_ at End() (not Begin()) so that a
        // 2nd Begin()/Draw()/End() cycle on the same SpriteBatch within one frame can never
        // clobber the 1st cycle's already-completed data before RecordCommandBuffer() harvests
        // it at Present() (Task 664 fix — see plans/plan_graphics.md).
        struct BatchSnapshot
        {
            std::vector<Sprite2DVertex> vertices;
            std::vector<uint16_t>       indices;
            std::vector<DrawCall>       draws;
            // REMED-GFX-075: a custom Effect is captured by VALUE (its VkPipeline/VkPipelineLayout
            // handles + a copy of its 128-byte push-constant block) at End(), NOT by pointer, so a
            // custom Effect disposed after SpriteBatch.End() but before the deferred record can no
            // longer be dereferenced at Present time. The pipeline/layout handles themselves are
            // kept alive past this batch's consumption by the effect renderer's retirement queue.
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
            // frame-global value the renderer applied once per frame at backbuffer-pass begin.
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
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        /// plans/plan_fx.md FX-102: one sprite held back until the batch flushes, so a whole run of
        /// same-texture sprites can be replayed once per pass rather than each sprite once per
        /// pass. Everything a quad needs is captured by value; nothing here outlives the flush.
        struct PendingCompiledSpriteEXT
        {
            const ITextureRenderer* texture = nullptr;
            Rectangle destination;
            Rectangle source;
            Color color;
            float rotation = 0.0f;
            Vector2 origin;
            SpriteEffects effects = SpriteEffects::None;
        };
        std::vector<PendingCompiledSpriteEXT> pendingCompiledSprites_;
        bool immediateMode_ = false;
        /// Applies the current technique's passes over the whole pending run, pass-major, and
        /// queues each sprite's quad once per pass. Mirrors FNA's SpriteBatch.FlushBatch.
        void FlushPendingCompiledSpritesEXT();
        /// Builds one sprite's six vertices and hands them to the compiled draw route.
        void QueueCompiledSpriteEXT(const PendingCompiledSpriteEXT& sprite,
                                    ICompiledEffectRuntime* runtime);
#endif
        VulkanRenderer*           renderer_             = nullptr;
        bool                             active_              = false;
        Effect*                          customEffect_        = nullptr;
        VulkanEffectRenderer*             customEffectRenderer_ = nullptr;
        // REMED-GFX-012: SpriteBatch.Begin(transformMatrix), applied per vertex in Draw(). Always
        // (re-)set by SpriteBatch::Begin() before this renderer's Begin(), so it is never stale;
        // defaults to Identity for the no-transform overload.
        Matrix                           transform_           = Matrix::getIdentityProperty();
        std::vector<Sprite2DVertex>      vertices_;
        std::vector<uint16_t>            indices_;
        std::vector<DrawCall>            draws_;
        const IVulkanSamplable*          currentTexture_      = nullptr;
        uint32_t                         batchFirstIndex_     = 0;
        // REMED-GFX-166: a share of the destination pass, captured at Begin() and held until the
        // batch is recorded -- the target may be unbound AND destroyed in between.
        std::shared_ptr<VulkanRTSource>  activeRT_;
        // REMED-GFX-140: the logical render-pass segment active at Begin(), captured with
        // activeRT_ and for the same reason -- the batch belongs to the bind cycle that opened
        // it, even if End() happens after the target was unbound.
        uint64_t                         activeSegment_       = 0;

        // Task 665 fix: pending SamplerState, applied at flush time (mirrors EasyGL's exact
        // field names/defaults — SamplerState.LinearClamp is SpriteBatch's own real default).
        int                              pendingFilter_       = 0; // TextureFilter::Linear
        int                              pendingAddressU_     = 1; // TextureAddressMode::Clamp
        int                              pendingAddressV_     = 1; // TextureAddressMode::Clamp

        void FlushTexture();
    };

    // -------------------------------------------------------------------------
    // VulkanVertexBufferRenderer
    // -------------------------------------------------------------------------

    class VulkanVertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        explicit VulkanVertexBufferRenderer(int vertex_capacity, VulkanRenderer* owner);
        ~VulkanVertexBufferRenderer() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // REMED-GFX-DECL-GUARD: this renderer still infers its native input layout from the byte
        // stride (REMED-GFX-217), but the declaration is no longer discarded -- it is remembered
        // so a draw can refuse a declaration that layout would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        int  GetVertexCount() const override { return vertexCount_; }

        VkBuffer    GetBuffer()    const { return buffer_; }
        int         GetCapacity()  const { return capacity_; }
        const void* GetMappedPtr() const { return mappedPtr_; }
        std::size_t GetStride()    const { return stride_; }
        /// The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        const CNA::Internal::Graphics::DeclaredVertexLayout& GetDeclarationEXT() const
        {
            return declaration_;
        }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

        /// Bytes currently mapped at `mappedPtr_`. Never smaller than what a legal upload or a
        /// legal draw of this buffer touches -- see EnsureByteCapacity.
        VkDeviceSize GetAllocatedBytesEXT() const { return allocatedBytes_; }

    private:
        /// plan_vulkan.md VULKAN-130: grow the host-visible allocation to hold `needed` bytes.
        ///
        /// The constructor cannot size the allocation, because `CreateVertexBuffer` is handed a
        /// vertex COUNT and the stride only arrives with the first `SetData`. It therefore
        /// reserves a 64-byte-per-vertex guess, and this repairs it the moment a wider layout
        /// appears -- the renderer's own pipeline-key table already recognises strides 68, 76 and
        /// 80, so the guess is genuinely too small for layouts this renderer draws.
        ///
        /// Discarding the old handles needs no fence and no device wait: this VkBuffer is a
        /// host-visible CPU-side store that no command buffer ever binds. Every draw route copies
        /// the bytes out of `mappedPtr_` into its own deferred record (`Pending3DDraw::vbData`),
        /// so nothing but this object can name the handle. Keep that true, or this needs the
        /// retirement queue.
        void EnsureByteCapacity(VkDeviceSize needed);

        VkBuffer                buffer_      = VK_NULL_HANDLE;
        VkDeviceMemory          memory_      = VK_NULL_HANDLE;
        void*                   mappedPtr_   = nullptr;
        int                     capacity_    = 0;
        int                     vertexCount_ = 0;
        std::size_t             stride_      = 0;
        VkDeviceSize            allocatedBytes_ = 0;
        VulkanRenderer*  owner_       = nullptr;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    // -------------------------------------------------------------------------
    // VulkanIndexBufferRenderer
    // -------------------------------------------------------------------------

    class VulkanIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        explicit VulkanIndexBufferRenderer(int index_capacity, bool thirtyTwoBit,
                                          VulkanRenderer* owner);
        ~VulkanIndexBufferRenderer() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        int  GetIndexCount()  const override { return indexCount_; }
        bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        VkBuffer    GetBuffer()    const { return buffer_; }
        const void* GetMappedPtr() const { return mappedPtr_; }

        void ReleaseVulkanResources();
        void DisconnectOwner() { owner_ = nullptr; }

        /// Bytes currently mapped at `mappedPtr_`, i.e. `capacity_` indices of this buffer's own
        /// element width. plan_vulkan.md VULKAN-131.
        VkDeviceSize GetAllocatedBytesEXT() const { return allocatedBytes_; }

    private:
        /// plan_vulkan.md VULKAN-131: refuse an upload larger than the mapping, by name.
        ///
        /// Unlike the vertex buffer's twin this GROWS nothing, and the difference is deliberate.
        /// A vertex buffer is allocated from a stride guess and a wider real stride is a legal
        /// thing for a caller to have; an index buffer is allocated from the element width it was
        /// created with, so the only ways past its end are more indices than its capacity or a
        /// width that is not this buffer's. Both are caller errors, and widening the allocation
        /// would make the second one draw from misread bytes rather than fail.
        void RequireByteCapacity(VkDeviceSize needed, const char* what) const;

        VkBuffer                buffer_        = VK_NULL_HANDLE;
        VkDeviceMemory          memory_        = VK_NULL_HANDLE;
        void*                   mappedPtr_     = nullptr;
        int                     capacity_      = 0;
        int                     indexCount_    = 0;
        bool                    thirtyTwoBit_  = false;
        VkDeviceSize            allocatedBytes_ = 0;
        VulkanRenderer*  owner_         = nullptr;
    };

    // -------------------------------------------------------------------------
    // VulkanTexture3DRenderer
    // -------------------------------------------------------------------------

    class VulkanTexture3DRenderer : public ITexture3DRenderer, public IVulkanVolumeSamplable
    {
    public:
        /** @brief The VK_IMAGE_VIEW_TYPE_3D view this volume is sampled through. */
        [[nodiscard]] VkImageView GetVkVolumeImageView() const override { return imageView_; }

        VulkanTexture3DRenderer(VulkanRenderer* owner, int w, int h, int depth, bool mipMap);
        ~VulkanTexture3DRenderer() override;

        /// REMED-GFX-135: true only once the host-visible staging buffer has been filled and its
        /// copy submitted and waited on; false when this texture has no usable image, the staging
        /// buffer could not be mapped, or the level/box/source length is out of range. The staging
        /// copy happens inside this call, so the caller's memory is never retained past it.
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: true only once the staging copy has filled the whole requested box;
        /// false when this texture has no usable image (torn-down owner/device) or the staging
        /// buffer could not be mapped, so the shared layer rejects the read instead of converting
        /// its own zeroed scratch buffer into a fabricated volume.
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;

    private:
        VulkanRenderer* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int width_ = 0, height_ = 0, depth_ = 0;
        int levelCount_ = 1;
    };

    // -------------------------------------------------------------------------
    // VulkanTextureCubeRenderer
    // -------------------------------------------------------------------------

    class VulkanTextureCubeRenderer : public ITextureCubeRenderer, public IVulkanCubeSamplable
    {
    public:
        VulkanTextureCubeRenderer(VulkanRenderer* owner, int size, bool mipMap);
        ~VulkanTextureCubeRenderer() override;

        /// REMED-GFX-135: same explicit completion contract as VulkanTexture3DRenderer::SetData.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: same explicit completion contract as VulkanTexture3DRenderer::GetData --
        /// true only once the staging copy has filled the whole requested face rectangle.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the Vulkan cube image view for sampling. */
        [[nodiscard]] VkImageView GetImageView()        const { return imageView_; }
        [[nodiscard]] VkImageView GetVkCubeImageView()  const override { return imageView_; }

    private:
        VulkanRenderer* owner_ = nullptr;
        VkImage        image_     = VK_NULL_HANDLE;
        VkDeviceMemory memory_    = VK_NULL_HANDLE;
        VkImageView    imageView_ = VK_NULL_HANDLE;
        int size_ = 0;
        int levelCount_ = 1;
    };

    // -------------------------------------------------------------------------
    // VulkanOcclusionQueryRenderer
    // -------------------------------------------------------------------------

    class VulkanOcclusionQueryRenderer : public IOcclusionQueryRenderer
    {
        // Task 447/854: RecordCommandBuffer() drives pool_/resetThisFrame_/recordedThisFrame_
        // directly (real vkCmdResetQueryPool/vkCmdBeginQuery/vkCmdEndQuery recording lives there,
        // alongside every other draw-dispatch/render-pass concern), mirroring this file's
        // existing one-directional friend idiom (VulkanRenderer already befriends this
        // class the other way, for owner_ access).
        friend class VulkanRenderer;

    public:
        explicit VulkanOcclusionQueryRenderer(VulkanRenderer* owner);
        ~VulkanOcclusionQueryRenderer() override;

        void Begin() override;
        void End()   override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;

        /**
         * @brief Whether `PixelCount()` is a real tally rather than "any samples passed".
         *
         * plan_vulkan.md VULKAN-370. A Vulkan occlusion query is only required to produce an exact
         * count when the device's `occlusionQueryPrecise` feature is enabled **and** the query is
         * begun with `VK_QUERY_CONTROL_PRECISE_BIT`; without both, an implementation may return any
         * non-zero value once a single sample passes. Inheriting the shared `true` default was
         * therefore a claim this renderer had not earned, and the lensflare idiom -- `PixelCount()`
         * divided by an area -- is exactly what it would have broken.
         *
         * @return True when this device enabled precise occlusion queries.
         */
        [[nodiscard]] bool PixelCountIsPreciseEXT() const noexcept override;

    private:
        VulkanRenderer*  owner_      = nullptr;
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
    // VulkanRenderTargetCubeRenderer
    // -------------------------------------------------------------------------

    class VulkanRenderTargetCubeRenderer : public IRenderTargetCubeRenderer,
                                          public IVulkanCubeSamplable
    {
        friend class VulkanMRTProxy;

    public:
        // Task 911: `depthFormat` (raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal)
        // gives this instance true per-RT DepthStencilFormat fidelity, mirroring
        // VulkanRenderTargetRenderer's identical constructor-comment fix.
        // REMED-GFX-136: `preserveContents` is the public RenderTargetCube's own
        // RenderTargetUsage, arriving here for the first time. It selects this instance's face
        // render pass exactly the way VulkanRenderTargetRenderer has always selected its own
        // (GetOrCreateRTRenderPass(depthFmt, !preserveContents)): LOAD_OP_LOAD with
        // initialLayout SHADER_READ_ONLY_OPTIMAL when true, LOAD_OP_CLEAR with initialLayout
        // UNDEFINED when false. This class previously hardcoded the discard variant with a
        // comment saying it had "no preserveContents concept", which was true only because the
        // creation call could not carry one.
        // REMED-GFX-141: it now selects the MULTISAMPLED face render pass too. msaaColorImage_ is
        // a six-layer image with one per-face view (it used to be one single-layer attachment
        // shared by all six) and GetOrCreateRTRenderPassMsaa() has gained the LOAD variant it
        // never had, so a multisampled cube face is preserved across bind cycles exactly like a
        // single-sample one.
        VulkanRenderTargetCubeRenderer(VulkanRenderer* owner, int size, int depthFormat,
                                       bool preserveContents = false,
                                       bool mipMap = false, int requestedMultiSampleCount = 0);
        ~VulkanRenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        /// Task 903: real applied MSAA sample count (0 if MSAA wasn't engaged), mirroring
        /// VulkanRenderTargetRenderer::GetMultiSampleCount()'s identical Task 878/879 pattern.
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        [[nodiscard]] VkImageView GetFaceResolveViewEXT(int face) const
        {
            return face >= 0 && face < 6
                ? faceViews_[static_cast<std::size_t>(face)]
                : VK_NULL_HANDLE;
        }
        /// REMED-GFX-151: the key identifying this cube as ONE render target in the readback
        /// flush's dependency graph. Deliberately the same value all six face passes answer from
        /// `DepthStencilOwnerEXT()` (REMED-GFX-142), so a draw sampling the cube and the six cycles
        /// that fill its faces resolve to the same group.
        [[nodiscard]] const void* RenderTargetGroupEXT() const
        {
            return static_cast<const void*>(image_);
        }
        /// REMED-GFX-141: this cube's per-face multisample colour view (CNAEXT -- test/diagnostics).
        /// There are six now, one per array layer of `msaaColorImage_`, where there used to be one
        /// shared by every face. VK_NULL_HANDLE when this target did not engage MSAA.
        [[nodiscard]] VkImageView GetMsaaColorViewEXT(int face) const
        {
            return face >= 0 && face < 6
                ? msaaColorViews_[static_cast<std::size_t>(face)]
                : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkImageView GetDepthViewEXT() const { return depthView_; }
        [[nodiscard]] VkFormat GetDepthFormatEXT() const { return depthVkFormat_; }
        [[nodiscard]] VkSampleCountFlagBits GetColorSampleCountEXT() const
        {
            return static_cast<VkSampleCountFlagBits>(
                appliedMultiSampleCount_ > 1 ? appliedMultiSampleCount_ : 1);
        }

        /**
         * @brief Reads a RENDERED cube face's mip level back to the CPU.
         *
         * REMED-GFX-134. `VulkanTextureCubeRenderer::GetData`'s mechanism -- transition just this
         * face layer to TRANSFER_SRC_OPTIMAL, copy it into a host-visible staging buffer, restore
         * the layout -- with the two things a RENDERED face additionally needs:
         * `FlushDeferredRenderTarget` on this face's exact immutable pass first (REMED-GFX-074 and
         * REMED-GFX-194, so both single-target and MRT draws still queued for Present are read
         * after their matching producer, not before), and the BGRA->RGBA correction
         * `VulkanRenderTargetRenderer::GetData` already applies because a render target's colour
         * image carries the swapchain format rather than a plain texture's fixed RGBA8.
         *
         * No row flip: Vulkan's framebuffer origin is top-left, so a rendered face is already
         * stored top-row-first, exactly like this renderer's plain-cube upload.
         *
         * MSAA resolves into this same image at `vkCmdEndRenderPass` through the render pass's own
         * `pResolveAttachments`, and a mip chain is regenerated into these same layers by
         * `VulkanTargetPassEXT::MaybeGenerateMips`, so both are read here without a separate resolve step.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True once the whole region was written; false for an out-of-range
         *         face/level/region or a staging allocation this device refused.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        // IVulkanCubeSamplable — returns a VK_IMAGE_VIEW_TYPE_CUBE view over all 6 faces.
        [[nodiscard]] VkImageView GetVkCubeImageView() const override { return cubeView_; }

    private:
        VulkanRenderer*     owner_     = nullptr;
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
        /**
         * REMED-GFX-141: ONE multisampled image with SIX array layers and one per-layer view per
         * face, resolved into that face's own `faceViews_[face]`/`image_` layer at
         * `vkCmdEndRenderPass` via the MSAA render pass's `pResolveAttachments`.
         *
         * Task 903 allocated a single-layer image here and pointed all six face framebuffers at it,
         * on `depthImage_`'s "only one face is ever rendered into at a time" reasoning. That holds
         * while a face is being PRODUCED and breaks the moment one is RELOADED: a multisample image
         * carries no face identity, so a `PreserveContents` face rebound for a partial update read
         * back whichever face was rendered last -- or, since `GetOrCreateRTRenderPassMsaa` had no
         * `LOAD` variant at all, the clear colour. Six layers matches what D3D11/D3D12 have always
         * allocated. Memory cost is `size * size * samples * 4 * 6` bytes per cube target, paid
         * once at construction; there is no per-bind allocation and no full-face copy.
         *
         * `TRANSIENT_ATTACHMENT` is only requested for a DISCARDING target -- a preserving one must
         * genuinely keep its samples between passes, which is what "transient" says it will not.
         */
        VkImage                       msaaColorImage_  = VK_NULL_HANDLE;
        VkDeviceMemory                msaaColorMemory_ = VK_NULL_HANDLE;
        std::array<VkImageView, 6>    msaaColorViews_  = {};
        std::array<VkFramebuffer, 6>  msaaFramebuffers_ = {};
        // REMED-GFX-166: six independently owned face destinations, replacing the value-member
        // value-member FaceProxy array. All six carry the same `depthGroup` (this cube's colour image), which is
        // exactly the shared depth/stencil identity REMED-GFX-142 established, so a readback still
        // flushes the whole cube as one group.
        std::array<std::shared_ptr<VulkanTargetPassEXT>, 6> facePasses_;
        int                        size_      = 0;
        int                        levelCount_ = 1;
        int                        appliedMultiSampleCount_ = 0;
        /// REMED-GFX-136: this target's own RenderTargetUsage, collapsed by
        /// RenderTargetUsagePreservesContentsEXT(). Read once, in the constructor, to pick the
        /// face render pass; nothing mutates it afterwards, so a pass can never be built against a
        /// value that changed later.
        bool                       preserveContents_ = false;
    };

    // -------------------------------------------------------------------------
    // VulkanMRTProxy — combines N VulkanRenderTargetRenderer into one RT source
    // -------------------------------------------------------------------------

    class VulkanMRTProxy : public VulkanRTSource
    {
    public:
        VulkanMRTProxy(VulkanRenderer* owner,
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
        /** @brief Regenerates every mipmapped colour attachment finalized by this MRT pass. */
        void          MaybeGenerateMips(VkCommandBuffer cb) override;

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
        /**
         * @brief Finds the public MRT slot that produces an exact target pass and mip chain.
         *
         * @param targetPass The immutable 2D or cube-face destination being read.
         * @param requestedMipLevel The valid mip level requested by the readback.
         * @return The public colour-attachment slot, or -1 when this binding cycle is unrelated.
         */
        [[nodiscard]] int FindColorAttachmentSlotEXT(
            const VulkanTargetPassEXT& targetPass, int requestedMipLevel) const
        {
            if (requestedMipLevel < 0 || requestedMipLevel >= targetPass.mipLevels) return -1;
            for (std::size_t i = 0; i < colorTargetPasses_.size(); ++i)
                if (colorTargetPasses_[i].get() == &targetPass)
                    return static_cast<int>(i);
            return -1;
        }

    private:
        VulkanRenderer* owner_       = nullptr;
        VkRenderPass           renderPass_  = VK_NULL_HANDLE;
        VkFramebuffer          framebuffer_ = VK_NULL_HANDLE;
        int                    width_       = 0;
        int                    height_      = 0;
        uint32_t               colorCount_  = 0;
        VkSampleCountFlagBits  colorSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
        std::vector<VkImageView> colorAttachments_;
        std::vector<VkImageView> resolveAttachments_;
        std::vector<VkImageView> framebufferAttachments_;
        // REMED-GFX-190: immutable per-binding destinations, retained in public attachment order.
        // Besides supplying the exact image/layer/level metadata for post-pass mip generation,
        // shared ownership keeps that metadata valid when a bound target is disposed before the
        // deferred MRT segment records (REMED-GFX-166's lifetime rule).
        std::vector<std::shared_ptr<VulkanTargetPassEXT>> colorTargetPasses_;
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
    // VulkanRenderer
    // -------------------------------------------------------------------------

    class VulkanRenderer : public IGraphicsRenderer
    {
        friend class VulkanTextureRenderer;
        friend class VulkanVertexBufferRenderer;
        friend class VulkanIndexBufferRenderer;
        friend class VulkanSpriteBatchRenderer;
        friend class VulkanEffectRenderer;
        friend class VulkanRenderTargetRenderer;
        friend class VulkanOcclusionQueryRenderer;
        friend class VulkanTexture3DRenderer;
        friend class VulkanTextureCubeRenderer;
        friend class VulkanRenderTargetCubeRenderer;
        friend class VulkanMRTProxy;

    public:
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        /**
         * @brief Whether this renderer runs compiled XNA Effect Framework bytecode.
         *
         * plans/plan_fx.md FX-065. True: `VulkanCompiledEffect` translates the effect through CNA's own
         * MojoShader SPIR-V backend -- there is no `mojoshader_vulkan.c` -- and this renderer
         * builds a pipeline from the linked SPIR-V pair, binds the four descriptor sets the SPIR-V
         * profile fixes (0 = vertex samplers, 1 = vertex uniforms, 2 = pixel samplers, 3 = pixel
         * uniforms), and replays the draw from a per-frame uniform ring at `Present()`.
         *
         * The two things it still refuses by name, rather than rendering wrongly, are vertex-stage
         * sampling (FX-109) and a `Texture3D` bound to a pixel sampler (FX-110).
         * @return true.
         */
        [[nodiscard]] bool SupportsCompiledEffects() const override { return true; }

        /**
         * @brief Creates a compiled-effect runtime for this device.
         *
         * @param effectCode Compiled effect bytes.
         * @param effectCodeBytes Number of bytes at @p effectCode.
         * @return The runtime.
         */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /**
         * @brief CNAEXT. Returns this renderer's shared MojoShader effect-backend state.
         *
         * plans/plan_fx.md FX-065. One per renderer, exactly as `MOJOSHADER_glContext` and
         * `MOJOSHADER_sdlContext` are for the other two backends -- which is why the constant
         * register files it holds are shared, and why a deferred draw snapshots its uniforms.
         * @return The context; never null once the device exists.
         */
        CNAEXT [[nodiscard]] VulkanMojoShaderContextEXT* GetMojoShaderContextEXT();

        /**
         * @brief CNAEXT. Whether this renderer created @p texture, whatever its dimension.
         *
         * plans/plan_fx.md FX-065/FX-110. `ITextureRenderer`, `ITexture3DRenderer` and
         * `ITextureCubeRenderer` are three unrelated interfaces, so this cannot be answered by one
         * pointer cast; whether a DRAW can bind the texture is a separate question decided against
         * the shader's own declared sampler dimension.
         * @param texture Public texture to test.
         * @return True if this renderer owns it.
         */
        CNAEXT [[nodiscard]] bool OwnsSampleableTextureEXT(
            Microsoft::Xna::Framework::Graphics::Texture* texture) const;
#endif

        explicit VulkanRenderer(const GraphicsRendererCreateArgs& args);
        ~VulkanRenderer() override;

        /**
         * @brief Whether this renderer supports @p capability on the device it selected.
         *
         * plan_vulkan.md VULKAN-020: every member of `CNA::GraphicsCapability` has its own arm,
         * and the switch has no `default:`. Appending a member therefore stops this target
         * compiling (`-Werror=switch`) rather than silently claiming the new feature -- which is
         * what the previous `default: return true` did, and what `GraphicsCapability`'s own
         * documentation names as the reason several entries had to be made derived.
         *
         * @param capability The feature to ask about.
         * @return Its support on the selected device; false for a value that is not an enumerator.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief The shader dialect a custom `ShaderEffect` must be written in here.
         *
         * plan_vulkan.md VULKAN-250. `VulkanEffectRenderer::CompileProgram` accepts nothing but
         * SPIR-V words, so leaving this at the shared `Unknown` default made the one query that
         * exists to stop an application guessing answer "guess".
         *
         * `GlslVulkan` is the closest honest value the enumeration currently has -- and it is not
         * a precise one. See `VULKAN-264`: IGL's Vulkan backend reports the SAME enumerator and
         * takes GLSL **source**, while this renderer takes compiled **bytecode**, so the answer
         * still does not tell a caller which of the two to send. Narrowing that needs a new
         * enumerator, which is a C-ABI change and belongs with `plans/plan_binding.md`.
         *
         * @return `ShaderDialectEXT::GlslVulkan`.
         */
        [[nodiscard]] CNA::Internal::Renderers::ShaderDialectEXT
            GetShaderDialectEXT() const override;

        /**
         * @brief CNAEXT. Whether a `RenderTarget2D` of @p format can really be created here.
         *
         * plan_vulkan.md VULKAN-020. Mirrors `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT`,
         * including its `Defer` arm, so this renderer's own answer for the float render-target
         * capabilities cannot disagree with the one a game gets through `GraphicsDevice`.
         *
         * Takes an ordinal rather than the enum for the reason `IGraphicsRenderer` states about
         * itself: this layer is deliberately not coupled to `SurfaceFormat`.
         *
         * @param surfaceFormatOrdinal The `SurfaceFormat` ordinal to ask about.
         * @return True if a render target of that format is created rather than substituted.
         */
        CNAEXT [[nodiscard]] bool SupportsRenderTargetSurfaceFormatEXT(
            int surfaceFormatOrdinal) const;

        /**
         * @brief CNAEXT. XNA's Direct3D 9 pixel-centre correction, as a clip-space translation.
         *
         * plan_vulkan.md VULKAN-097. XNA 4.0 addresses pixel CENTRES with integer coordinates;
         * Vulkan (like OpenGL and Direct3D 10+) addresses pixel corners. Without a correction the
         * screen-space triangle `(x,y),(x+1,y),(x,y+1)` that XNA covers one pixel for lands
         * entirely on an excluded fill edge and disappears -- which is exactly what
         * `xna_pixel_center_contract_test.cpp` measured on the real XNA runtime and what this
         * renderer used to fail.
         *
         * Post-multiply it into the world x view x projection product, XNA row-vector order, so it
         * becomes `clip.xy += offset * clip.w`. The offset is the same slightly-under-half-pixel
         * displacement EasyGL and Wine use, so the pixel centre stays inside the triangle under
         * Direct3D's top-left fill rule; the margin is the whole design.
         *
         * Empty (identity) when the destination is multisampled: the correction is a GEOMETRY
         * translation and that is only equivalent to what it means at one sample per pixel
         * (REMED-GFX-235, and this renderer inherits the reasoning rather than the code).
         *
         * @return The clip-space translation, or identity where it must not be applied.
         */
        CNAEXT [[nodiscard]] Microsoft::Xna::Framework::Matrix XnaPixelCenterCorrectionEXT() const;

        /**
         * @brief CNAEXT. Refuses a PBR draw whose stride no PBR pipeline can express, at the DRAW.
         *
         * plan_vulkan.md VULKAN-346. `GetOrCreatePipelinePbr3D` and its skinned twin raise this
         * refusal, and they are called from `RecordCommandBuffer` -- at `Present()`, long after the
         * call that made the mistake. A caller therefore saw its draw accepted, and the exception
         * arrived from a frame boundary it could no longer associate with anything, escaping
         * whatever recovery it had around the draw itself. Asking the same question where the draw
         * is queued makes the refusal reach the caller at the point of the mistake, which is what
         * section 6.3 of the plan requires of every unsupported combination.
         *
         * @param stride Byte stride of the vertex record.
         * @param skinned True for the SkinnedPbrEffect family.
         * @throws std::runtime_error naming the strides that family accepts.
         */
        void RequirePbrStrideEXT(std::size_t stride, bool skinned) const;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int)       override {}

        /**
         * @brief Applies a runtime swap-interval change by rebuilding the swapchain.
         *
         * plan_vulkan.md VULKAN-332. `swapInterval_` used to be read once, at swapchain creation,
         * so `GraphicsDeviceManager.SynchronizeWithVerticalRetrace` + `ApplyChanges()` -- which
         * routes through `GraphicsDevice::Reset` and does reach `SetSwapInterval` -- changed
         * nothing here. `IGraphicsRenderer`'s own comment that Vulkan "cannot change VSync at
         * runtime" described that implementation, not the API: `CreateSwapchain` already picks the
         * present mode from `swapInterval_`, and this renderer already rebuilds its swapchain on
         * every resize, so the mechanism was present and simply never invoked.
         *
         * A request equal to the current interval rebuilds nothing.
         *
         * @param interval 0 = immediate, 1 = vsync, 2 = half-rate.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief The swap interval last requested of this renderer.
         *
         * plan_vulkan.md VULKAN-333. `REMED-GFX-243` added this getter because a setter alone
         * cannot tell "CNA never forwarded the request" from "the driver declined it", and a vsync
         * test that cannot tell them apart defends nothing. The default `-1` means "this renderer
         * does not record what it was asked for", which was the honest answer while
         * `SetSwapInterval` was the inherited no-op and is no longer one.
         *
         * This is CNA's own recorded request, not a driver query, so it is answerable anywhere --
         * including where the surface cannot honour it.
         *
         * @return The interval last passed to SetSwapInterval, or the one this renderer was
         *         constructed with if none has been.
         */
        [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }


        /**
         * @brief CNAEXT. The `VkPresentModeKHR` the live swapchain was actually created with.
         *
         * plan_vulkan.md VULKAN-332. The interval is the request; this is what the device gave,
         * and the two are not the same claim -- `VK_PRESENT_MODE_IMMEDIATE_KHR` may simply not be
         * offered. A regression that asserted only the recorded interval would pass on a renderer
         * that recorded it and rebuilt nothing.
         *
         * @return The present mode of the current swapchain.
         */
        CNAEXT [[nodiscard]] VkPresentModeKHR GetAppliedPresentModeEXT() const noexcept
        {
            return appliedPresentMode_;
        }

        /**
         * @brief CNAEXT. Whether this surface offers a present mode that does not wait for vblank.
         *
         * plan_vulkan.md VULKAN-332. `VK_PRESENT_MODE_FIFO_KHR` is the only mode Vulkan guarantees,
         * so "the mode is still FIFO with vsync off" is a correct answer on some surfaces and a
         * broken renderer on others. Without this a regression has to guess which, and guessing
         * means an escape hatch that excuses the defect -- measured while writing the one for this
         * task: with `SetSwapInterval` reverted to its no-op the test still passed, because the
         * lenient branch accepted FIFO.
         *
         * @return True if IMMEDIATE or MAILBOX is available on this surface.
         */
        CNAEXT [[nodiscard]] bool SupportsUnsynchronisedPresentModeEXT() const noexcept
        {
            return unsynchronisedPresentModeAvailable_;
        }
        // Task 902: real in-place backbuffer MSAA reconfiguration, wired from
        // GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling actually reaches
        // the renderer. Deliberately scoped to the backbuffer only -- already-live RenderTarget2D/
        // RenderTargetCube instances keep whatever MultiSampleCount they engaged at their own
        // construction time (see VulkanRenderTargetRenderer's own sampleCount_ capture); a
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
        /// REMED-GFX-212 diagnostic, mirroring the WebGPU renderer's own accessor of the same name.
        /// `BasicEffect.VertexColorEnabled` is NOT part of this cache's key -- it travels in the
        /// 128-byte push constant, so toggling it must reuse the variant an otherwise-identical
        /// draw already built, while a geometry declaration that carries a COLOR0 element must not
        /// share a pipeline with one that does not.
        CNAEXT [[nodiscard]] std::size_t GetInstancedPipelineCacheSizeEXT() const noexcept
        {
            return pipelinesInstanced3D_.size();
        }
        // REMED-GFX-095: live MRT construction/pipeline diagnostics used by the dedicated
        // regression to distinguish a requested multisample target from a silent 1x pass.
        [[nodiscard]] const VulkanMRTProxy* GetCurrentMRTProxyEXT() const noexcept
        {
            return mrtProxy_.get();
        }
        /**
         * @brief Returns the exact pending MRT binding cycles and slots selected by the last read.
         *
         * Each pair is `(segment id, public colour-attachment slot)`. The list is empty for a
         * direct single-target producer or when the requested target had no pending MRT producer.
         *
         * @return Read-only dependency-selection diagnostics for REMED-GFX-194 tests.
         */
        CNAEXT [[nodiscard]] const std::vector<std::pair<uint64_t, uint32_t>>&
        GetLastMrtReadbackMatchesEXT() const noexcept
        {
            return lastMrtReadbackMatchesEXT_;
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
         * @brief CNAEXT. Total host-visible bytes this renderer has mapped for live vertex buffers.
         *
         * plan_vulkan.md VULKAN-130. `CreateVertexBuffer` is handed a vertex count, never a
         * stride, so the allocation starts as a 64-byte-per-vertex guess and is widened by the
         * first upload that needs more. The guess is not a bound and must never be treated as one:
         * this renderer's own pipeline-key table recognises strides 68, 76 and 80. Exposed so a
         * regression can assert the widening happened, rather than inferring it from whether the
         * process survived writing past its own mapping.
         *
         * @return Sum of the mapped sizes of every live `VulkanVertexBufferRenderer`.
         */
        CNAEXT [[nodiscard]] VkDeviceSize GetLiveVertexBufferBytesEXT() const noexcept;

        /**
         * @brief CNAEXT. Total host-visible bytes this renderer has mapped for live index buffers.
         *
         * plan_vulkan.md VULKAN-131. The counterpart of GetLiveVertexBufferBytesEXT(), and the
         * reason it reads differently: an index buffer's allocation is exact from the start
         * (`capacity` indices of the width it was created with), so a regression asserts that it
         * stays exact and that an upload which would exceed it is refused by name.
         *
         * @return Sum of the mapped sizes of every live `VulkanIndexBufferRenderer`.
         */
        CNAEXT [[nodiscard]] VkDeviceSize GetLiveIndexBufferBytesEXT() const noexcept;

        /**
         * @brief CNAEXT. How many descriptor pools back the single-sampler descriptor cache.
         *
         * plan_vulkan.md VULKAN-390. One until a game exceeds `MaxDescriptorSets` distinct live
         * (image view, sampler) pairs, then one more per overflow. Exposed so a regression can
         * prove the chaining actually happened rather than inferring it from the pixels being
         * right -- which is exactly what the old silent white-texture fallback made them not be.
         *
         * @return The pool count; at least 1 once the renderer is initialised.
         */
        CNAEXT [[nodiscard]] std::size_t GetTexSamplerDescriptorPoolCountEXT() const noexcept
        {
            return 1u + texSamplerOverflowPools_.size();
        }

        /**
         * @brief Returns every warning/error emitted by the active Vulkan validation messenger.
         *
         * Used by native validation regressions to turn asynchronous debug output into a
         * deterministic test failure while retaining the complete layer message.
         *
         * @return Validation messages captured since renderer construction.
         */
        [[nodiscard]] const std::vector<std::string>& GetValidationMessagesEXT() const noexcept
        {
            return validationMessages_;
        }

        /**
         * @brief Returns the layer message-ID name for each captured validation message.
         *
         * Parallel to GetValidationMessagesEXT(): entry i is the pMessageIdName the layer supplied
         * for message i, e.g. "SYNC-HAZARD-WRITE-AFTER-READ" or a VUID. Lets a regression classify
         * by message class rather than by matching a diagnostic sentence.
         *
         * @return Message-ID names captured since renderer construction.
         */
        CNAEXT [[nodiscard]] const std::vector<std::string>& GetValidationMessageIdNamesEXT() const noexcept
        {
            return validationMessageIdNames_;
        }

        /**
         * @brief Requests Khronos synchronization validation on the next instance created.
         *
         * Chains VkValidationFeaturesEXT into VkInstanceCreateInfo, so a regression can enable the
         * synchronization checks in-process instead of depending on VK_LAYER_SETTINGS_PATH or on an
         * environment variable a runner might drop. Must be called before the renderer is
         * constructed, and has no effect when the Khronos layer is unavailable.
         *
         * @param enabled True to request synchronization validation.
         */
        CNAEXT static void SetSyncValidationEnabledEXT(bool enabled) noexcept;

        /**
         * @brief Test-only. Makes the next @p count single-sampler descriptor allocations fail.
         *
         * plan_vulkan.md VULKAN-390. The exhaustion arm cannot be reached by volume on either
         * driver measured here: `vkAllocateDescriptorSets` keeps succeeding past the pool's
         * `maxSets`, which the specification permits (running out is a runtime error the
         * implementation *may* report, not a usage violation the validation layer will flag), and
         * 4000 simultaneously live pairs still did not trigger it. Shrinking the pool does not help
         * for the same reason. Without an injected failure a regression on this path would pass
         * while executing none of it.
         *
         * Each failure makes `GetOrCreateTexSamplerDescSet` chain another pool, exactly as a real
         * `VK_ERROR_OUT_OF_POOL_MEMORY` would. A count large enough to outlast the fresh pool's own
         * first allocation reaches the named-refusal arm instead.
         *
         * @param count How many allocations to fail; 0 disables injection.
         */
        CNAEXT static void SetTexSamplerDescriptorAllocationFailuresForTestEXT(
            std::uint32_t count) noexcept;

        /**
         * @brief Reports whether the Khronos validation layer is actually active.
         *
         * False once CreateInstance() has found the layer missing, so a validation regression can
         * fail loudly instead of reporting a meaningless zero message count.
         *
         * @return True when the layer was found and the debug messenger was installed.
         */
        CNAEXT [[nodiscard]] static bool IsValidationActiveEXT() noexcept;

        /** @brief Number of vkAcquireNextImageKHR calls that returned an image. */
        CNAEXT [[nodiscard]] uint64_t GetAcquireCountEXT() const noexcept { return acquireCountEXT_; }
        /** @brief Number of per-frame vkQueueSubmit calls made by SubmitFrame. */
        CNAEXT [[nodiscard]] uint64_t GetFrameSubmitCountEXT() const noexcept { return frameSubmitCountEXT_; }
        /** @brief Number of vkQueuePresentKHR calls, deferred presents included. */
        CNAEXT [[nodiscard]] uint64_t GetPresentCountEXT() const noexcept { return presentCountEXT_; }
        /** @brief Number of frame-fence waits, including the deferred-readback hold. */
        CNAEXT [[nodiscard]] uint64_t GetFrameFenceWaitCountEXT() const noexcept { return frameFenceWaitCountEXT_; }
        /** @brief Number of completed swapchain recreations. */
        CNAEXT [[nodiscard]] uint64_t GetSwapchainRecreateCountEXT() const noexcept { return swapchainRecreateCountEXT_; }
        /** @brief Count of distinct swapchain image indices an acquire has returned. */
        CNAEXT [[nodiscard]] int GetDistinctAcquiredImageCountEXT() const noexcept;
        /** @brief Count of distinct frame slots a submit has used. */
        CNAEXT [[nodiscard]] int GetUsedFrameSlotCountEXT() const noexcept;
        /** @brief Number of images in the current swapchain. */
        CNAEXT [[nodiscard]] int GetSwapchainImageCountEXT() const noexcept
        {
            return static_cast<int>(swapchainImages_.size());
        }
        /** @brief Number of frame slots the renderer cycles through. */
        CNAEXT [[nodiscard]] static constexpr int GetFramesInFlightEXT() noexcept { return MaxFramesInFlight; }
        /** @brief Live image-available semaphores; must stay equal to the frame-slot count. */
        CNAEXT [[nodiscard]] int GetImageAvailableSemaphoreCountEXT() const noexcept
        {
            return static_cast<int>(imageAvailableSemaphores_.size());
        }
        /** @brief Live render-finished semaphores; must stay equal to the frame-slot count. */
        CNAEXT [[nodiscard]] int GetRenderFinishedSemaphoreCountEXT() const noexcept
        {
            return static_cast<int>(renderFinishedSemaphores_.size());
        }
        /** @brief Live frame fences; must stay equal to the frame-slot count. */
        CNAEXT [[nodiscard]] int GetFrameFenceCountEXT() const noexcept
        {
            return static_cast<int>(inFlightFences_.size());
        }
        /** @brief Live frame command buffers; must stay equal to the frame-slot count. */
        CNAEXT [[nodiscard]] int GetFrameCommandBufferCountEXT() const noexcept
        {
            return static_cast<int>(commandBuffers_.size());
        }


        std::unique_ptr<ITextureRenderer>         CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer>     CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer>    CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void                                     SetRenderTarget2D(IRenderTargetRenderer* rt) override;

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
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer>  CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        // ---- Extended 3D draws (textured + lit) ----
        void DrawPrimitivesEx(const IVertexBufferRenderer&,
                              const Matrix&, const Matrix&, const Matrix&,
                              PrimitiveType, int, const GpuDrawParams&) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                     const Matrix&, const Matrix&, const Matrix&,
                                     PrimitiveType, int, const GpuDrawParams&) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
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
        std::unique_ptr<IVertexBufferRenderer>  CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer>   CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer>   CreateIndexBuffer32(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer&,
                                   const Matrix&, const Matrix&, const Matrix&,
                                   PrimitiveType, int) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer&,
                                          const IIndexBufferRenderer&,
                                          const Matrix&, const Matrix&, const Matrix&,
                                          PrimitiveType, int) override;

    private:
        // --- Constants ---
        static constexpr int      MaxFramesInFlight = 2;
        static constexpr uint32_t MaxSpriteVertices = 8192;
        static constexpr uint32_t MaxSpriteIndices  = MaxSpriteVertices / 4 * 6;
        static constexpr uint32_t MaxDescriptorSets = 512;

        // --- Core Vulkan objects (lifetime = renderer) ---
        RendererSurfaceInfo surfaceInfo_;
        CNA::Platform::IPlatformVulkanSurface* platformSurfaceService_ = nullptr;
        VkInstance       instance_       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        std::vector<std::string> validationMessages_;
        /// REMED-GFX-144: pMessageIdName per entry of validationMessages_, same order and size.
        std::vector<std::string> validationMessageIdNames_;
        std::unique_ptr<PlatformVulkanSurfaceOwner> platformSurface_;
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

        // REMED-GFX-144: bounded frame/swapchain instrumentation. A synchronization regression that
        // can only read pixels proves nothing about the model that produced them, and one that can
        // only parse a validation sentence breaks whenever the layer rewords it. These counters let
        // a test assert the model itself: one acquire, one frame submit and one present per rendered
        // frame; every frame slot and every swapchain image genuinely re-entered; and no per-frame
        // growth in the objects that carry synchronization.
        uint64_t acquireCountEXT_          = 0;
        uint64_t frameSubmitCountEXT_      = 0;
        uint64_t presentCountEXT_          = 0;
        uint64_t frameFenceWaitCountEXT_   = 0;
        uint64_t swapchainRecreateCountEXT_ = 0;
        /// Bit i set once swapchain image index i has been returned by an acquire.
        uint32_t acquiredImageMaskEXT_     = 0;
        /// Bit i set once frame slot i has been used by a submit.
        uint32_t usedFrameSlotMaskEXT_     = 0;

        // --- MSAA state (set at construction, fixed for renderer lifetime) ---
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
        std::unordered_map<VkFormat, VkRenderPass> rtRenderPassMsaaByDepthFmt_;  // 3-attachment MSAA color/resolve/depth, LOAD_OP_CLEAR
        /// REMED-GFX-141: the MSAA render pass's missing LOAD variant. Its multisample colour
        /// attachment uses LOAD_OP_LOAD + STORE_OP_STORE and stays in COLOR_ATTACHMENT_OPTIMAL
        /// across passes, so a PreserveContents target's own multisample samples survive a bind
        /// cycle instead of being cleared and thrown away. Same depth-format keying, same
        /// per-format sharing, and render-pass-compatible with the clear variant (load/store ops
        /// and layouts take no part in compatibility), so no pipeline cache key changed.
        std::unordered_map<VkFormat, VkRenderPass> rtRenderPassMsaaLoadByDepthFmt_;
        std::vector<VkFramebuffer> swapchainFramebuffers_;

        /**
         * @brief REMED-GFX-143: swapchain render passes that can be ENTERED MORE THAN ONCE a frame.
         *
         * The backbuffer used to get exactly one pass per command buffer, so `renderPass_` /
         * `renderPassMsaa_` could hardcode "clear everything on entry, store only what is
         * presented". A frame that interleaves backbuffer and render-target cycles needs a second,
         * third ... entry into the SAME acquired swapchain image, and those must LOAD what the
         * previous entry stored instead of clearing it, and STORE what a following entry will load.
         * Variants are keyed on the four bits that decide it -- see SwapchainPassKey -- and the
         * all-clear/no-store combination is answered with `renderPass_`/`renderPassMsaa_`
         * themselves, so a frame with one backbuffer cycle creates no variant at all and behaves
         * byte-for-byte as before.
         *
         * Every variant is render-pass-COMPATIBLE with `renderPass_`/`renderPassMsaa_`: identical
         * attachment formats, sample counts, subpass shape and (deliberately byte-identical, see
         * Task 905 in GetOrCreateRTRenderPass) subpass dependencies, differing only in load/store
         * operations and initial layouts, which compatibility ignores. Existing pipelines and the
         * existing per-image VkFramebuffers are therefore reused unchanged -- no pipeline and no
         * framebuffer is ever created per segment.
         */
        struct SwapchainPassKey {
            bool msaa       = false;  ///< The 3-attachment MSAA colour/resolve/depth shape.
            bool loadColor  = false;  ///< Colour LOAD (a previous backbuffer segment stored it).
            bool loadDepth  = false;  ///< Depth LOAD.
            bool loadStencil = false; ///< Stencil LOAD.
            bool storeForNext = false;///< Another backbuffer segment follows and may LOAD this one.

            [[nodiscard]] uint32_t Bits() const
            {
                return (msaa ? 1u : 0u) | (loadColor ? 2u : 0u) | (loadDepth ? 4u : 0u) |
                       (loadStencil ? 8u : 0u) | (storeForNext ? 16u : 0u);
            }
        };
        std::unordered_map<uint32_t, VkRenderPass> swapchainPassVariants_;
        /**
         * @brief Returns the swapchain render pass matching @p key, creating it on first use.
         *
         * @param key Which load/store shape this backbuffer segment needs.
         * @return A render pass compatible with renderPass_/renderPassMsaa_.
         */
        VkRenderPass GetOrCreateSwapchainRenderPass(const SwapchainPassKey& key);
        /// Destroys every cached swapchain pass variant (swapchain recreation / shutdown).
        void DestroySwapchainPassVariants();

        /// plan_vulkan.md VULKAN-332: the present mode CreateSwapchain settled on, recorded so a
        /// regression can tell a real rebuild from a recorded request.
        VkPresentModeKHR appliedPresentMode_ = VK_PRESENT_MODE_FIFO_KHR;
        /// plan_vulkan.md VULKAN-332: whether the surface offers IMMEDIATE or MAILBOX at all,
        /// so a regression can tell "correctly stayed FIFO" from "never applied the request".
        bool unsynchronisedPresentModeAvailable_ = false;

        // --- Depth buffer (recreated with swapchain) ---
        VkFormat        depthFormat_    = VK_FORMAT_UNDEFINED;
        VkImage         depthImage_     = VK_NULL_HANDLE;
        VkDeviceMemory  depthMemory_    = VK_NULL_HANDLE;
        VkImageView     depthImageView_ = VK_NULL_HANDLE;

        // --- Sampler cache: one VkSampler per unique SamplerState ---
        // plans/plan_fx.md FX-091: every field XNA's SamplerState carries that this renderer can express
        // is part of the key. Leaving MaxMipLevel, the LOD bias or AddressW out of it -- as this
        // key did before -- means two genuinely different sampler states collapse onto one
        // VkSampler, and whichever was created first silently wins.
        struct SamplerStateKey {
            int filter = 0;
            int addressU = 0;
            int addressV = 0;
            int addressW = 0;
            int maxAnisotropy = 4;
            int maxMipLevel = 0;
            float lodBias = 0.0f;
            [[nodiscard]] auto AsTuple() const noexcept {
                // The bias is compared by bit pattern rather than by value, so that two distinct
                // floats can never compare equivalent through a strict-weak ordering on a NaN.
                return std::tuple(filter, addressU, addressV, addressW, maxAnisotropy, maxMipLevel,
                                  std::bit_cast<std::uint32_t>(lodBias));
            }
            bool operator<(const SamplerStateKey& o) const noexcept {
                return AsTuple() < o.AsTuple();
            }
        };
        std::map<SamplerStateKey, VkSampler>                         samplerCache_;
        /// Returns the cached VkSampler for one XNA sampler state, creating it on first use.
        /// Shared by the per-slot ApplySampler* setters (which then own a slot) and by the
        /// compiled-effect route (which owns none -- an Effect's sampler_state block belongs to the
        /// pass, not to a device slot).
        [[nodiscard]] VkSampler GetOrCreateSamplerEXT(const SamplerStateKey& key);
        /// Re-derives slot `slot`'s VkSampler from its whole recorded state.
        void RebuildSlotSamplerEXT(int slot);
        /// GraphicsDevice.SamplerStates[slot] as last selected, whole. Each ApplySampler* setter
        /// updates its own fields here and rebuilds the slot's sampler from all of them, so a
        /// later setter cannot drop what an earlier one established.
        SamplerStateKey                                               samplerSlotState_[16] = {};
        VkSampler                                                     slotSamplers_[16] = {};
        /// plan_vulkan.md VULKAN-390: a cached single-sampler descriptor set together with the
        /// pool it came from. The pool used to be implicit -- there was only one -- and a set
        /// freed against the wrong pool is undefined behaviour, so chaining pools makes the
        /// owner part of the entry rather than something a caller has to remember.
        struct TexSamplerDescSetEXT {
            VkDescriptorSet  set  = VK_NULL_HANDLE;
            VkDescriptorPool pool = VK_NULL_HANDLE;
        };
        std::map<std::pair<VkImageView,VkSampler>, TexSamplerDescSetEXT>  texSamplerDescSets_;
        /// VULKAN-390: pools chained after `descriptorPool_` fills. Each holds MaxDescriptorSets
        /// more combined-image-sampler sets. Empty until a game genuinely exceeds the first pool.
        std::vector<VkDescriptorPool> texSamplerOverflowPools_;
        /// VULKAN-390: allocate one single-sampler set, chaining a new pool when the current ones
        /// are full. Throws a named exception if the device refuses; never substitutes a resource.
        void AllocateTexSamplerDescSetEXT(VkDescriptorSet& outSet, VkDescriptorPool& outPool);
        bool anisotropySupported_ = false;
        float maxSamplerAnisotropy_ = 1.f;
        bool independentBlendSupported_ = false;
        /// plan_vulkan.md VULKAN-370: whether `occlusionQueryPrecise` was enabled on this device.
        /// Decides both the `VK_QUERY_CONTROL_PRECISE_BIT` this renderer passes to
        /// `vkCmdBeginQuery` and the answer `PixelCountIsPreciseEXT()` gives.
        bool occlusionQueryPreciseSupported_ = false;
        /// plan_vulkan.md VULKAN-020: the SELECTED physical device's limits, cached where the
        /// selection is made. `SupportsCapability` answers `MultipleRenderTargets` and (VULKAN-021)
        /// `MultiSampleAntiAliasing` from these rather than from a constant.
        VkPhysicalDeviceLimits deviceLimits_{};
        /// plan_vulkan.md VULKAN-097: clip-space multiplier for XNA's slightly-less-than-half-
        /// pixel centre correction. 63/64 in clip space is 63/128 of a viewport pixel, because
        /// clip [-1,1] spans the viewport. Reduced at device creation if the device's
        /// `subPixelPrecisionBits` cannot represent it below half a pixel -- rounding back UP to
        /// exactly half would put the 1x1 triangle back on the excluded edge, which is the same
        /// trap EasyGL hit on WebGL's four subpixel bits.
        float xnaPixelCenterScale_ = 63.0f / 64.0f;

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
        // Padded to the max sampled-view count of any effect (PbrEffect/SkinnedPbrEffect: 7).
        static constexpr std::size_t kMaxEffectSampledViews = 7;
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
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // --- Compiled XNA effects (plans/plan_fx.md FX-065) ---
        //
        // MojoShader's SPIR-V profile puts its descriptors in four FIXED sets: 0 = vertex-stage
        // samplers, 1 = the vertex uniform block, 2 = pixel-stage samplers, 3 = the pixel uniform
        // block. Every pipeline layout here declares all four even when a shader uses none of a
        // given set, because vkCreatePipelineLayout needs a layout object at each index.
        //
        // The two uniform blocks are bound as UNIFORM_BUFFER_DYNAMIC over one ring buffer per
        // frame, exactly like the fog/skinned bundles above: that keeps a descriptor set's CONTENT
        // independent of which draw is using it, so a set can be cached by its sampler views alone
        // while the per-draw uniform bytes travel as a dynamic offset.
        static constexpr uint32_t kCompiledEffectUBOStride    = 4096;  // 256 float4 registers
        static constexpr uint32_t kCompiledEffectUBODrawsPerChunk = 256;
        VkDescriptorSetLayout compiledEffectEmptySetLayout_  = VK_NULL_HANDLE;
        VkDescriptorSetLayout compiledEffectUniformSetLayoutVS_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout compiledEffectUniformSetLayoutPS_ = VK_NULL_HANDLE;
        VkDescriptorPool      compiledEffectDescriptorPool_  = VK_NULL_HANDLE;
        /// Pixel-sampler set layouts, keyed by the exact set of sampler register indices a shader
        /// declares -- two effects that sample the same registers share one layout and one pipeline
        /// layout, and two that do not cannot be handed each other's.
        std::unordered_map<std::uint64_t, VkDescriptorSetLayout> compiledEffectSamplerSetLayouts_;
        std::unordered_map<std::uint64_t, VkPipelineLayout>      compiledEffectPipelineLayouts_;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> compiledEffectPipelines_;
        std::array<std::unordered_map<std::uint64_t, VkDescriptorSet>,
                   MaxFramesInFlight>                  compiledEffectSamplerSets_;
        /// One slab of `kCompiledEffectUBODrawsPerChunk` uniform slices per stage, with the two
        /// descriptor sets that name it. Chunks are appended on demand rather than the ring being
        /// a fixed size: a compiled sprite effect makes one draw per sprite per pass, so a frame
        /// drawing a few hundred sprites is ordinary rather than exceptional, and wrapping a fixed
        /// ring would hand two draws the same slice while refusing outright would fail a frame a
        /// game is entitled to.
        struct CompiledEffectUniformChunkEXT
        {
            VkBuffer        vsBuffer = VK_NULL_HANDLE;
            VkDeviceMemory  vsMemory = VK_NULL_HANDLE;
            void*           vsPtr    = nullptr;
            VkDescriptorSet vsSet    = VK_NULL_HANDLE;
            VkBuffer        psBuffer = VK_NULL_HANDLE;
            VkDeviceMemory  psMemory = VK_NULL_HANDLE;
            void*           psPtr    = nullptr;
            VkDescriptorSet psSet    = VK_NULL_HANDLE;
        };
        std::array<std::vector<CompiledEffectUniformChunkEXT>, MaxFramesInFlight>
            compiledEffectUBOChunks_;
        std::array<std::uint32_t, MaxFramesInFlight> compiledEffectUBOCursor_ = {};
        /// Returns chunk @p chunkIndex of frame @p frameIdx, creating every chunk up to it.
        CompiledEffectUniformChunkEXT& EnsureCompiledEffectUniformChunkEXT(uint32_t frameIdx,
                                                                          std::size_t chunkIndex);
        std::unique_ptr<VulkanMojoShaderContextEXT>   mojoShaderContext_;

        void EnsureCompiledEffectResourcesEXT();
        [[nodiscard]] VkPipelineLayout GetOrCreateCompiledEffectPipelineLayoutEXT(
            const std::vector<std::uint32_t>& pixelSamplerBindings);
#endif
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

        // PbrEffect resources (stride 48: VertexPositionNormalTangentTexture, or stride 60 with
        // the importer-appended TextureCoordinate1 channel).
        // 7 combined image samplers (baseColor@0, normalMap@1, metallicRoughnessMap@2,
        // emissiveMap@3, occlusionMap@4, specular strength@6, specular colour@7) plus one dynamic
        // UBO (PbrParams@5, world/lights1-2/emissive/
        // eyePos/metallic-roughness/fog -- everything FillExtPushConst's 128-byte PC has no room
        // for), mirroring descriptorSetLayoutSkinned_'s sampler+dynamic-UBO shape.
        VkDescriptorSetLayout descriptorSetLayoutPbr_ = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPoolPbr_      = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayoutPbr3D_    = VK_NULL_HANDLE;
        std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>             pipelinesPbr3D_;
        std::array<std::unordered_map<uint64_t, EffectDescSetEntry>,
                   MaxFramesInFlight>                        pbrDescSets_;
        static constexpr uint32_t kPbrUBOStride   = 512; // 496 bytes used (124 floats), padded to 512
        static constexpr uint32_t kPbrUBOMaxDraws = 512;
        std::array<VkBuffer,       MaxFramesInFlight> pbrUBO_    = {};
        std::array<VkDeviceMemory, MaxFramesInFlight> pbrUBOMem_ = {};
        std::array<void*,          MaxFramesInFlight> pbrUBOPtr_ = {};

        // SkinnedPbrEffect resources (PBR + skinning combo, stride 68, or stride 76 with the
        // importer-appended TextureCoordinate1 channel). Same 7 samplers as descriptorSetLayoutPbr_
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
        static constexpr uint32_t kPbrSkinnedUBOStride   = 512; // same 496-byte PbrParams block
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
        // EasyGLRenderer::EnsureDefaultFlatNormalTexture()'s own fallback semantics.
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
        std::vector<VulkanTextureRenderer*>       liveTextures_;
        std::vector<VulkanVertexBufferRenderer*>  liveVertexBuffers_;
        std::vector<VulkanIndexBufferRenderer*>   liveIndexBuffers_;
        std::vector<VulkanRenderTargetRenderer*>  liveRenderTargets_;

        // --- MRT proxy (one active binding; old bindings retire through the frame fence) ---
        // REMED-GFX-166: shared, so a deferred entry queued against this proxy keeps it alive
        // after SetRenderTargets() has moved on (see the retirement-list removal below).
        std::shared_ptr<VulkanMRTProxy>          mrtProxy_;
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
            // REMED-GFX-166: a SHARE of the destination pass, not a borrowed pointer into the
            // public wrapper's renderer object. Holding it is what lets the wrapper be destroyed
            // between this draw and the frame that replays it.
            std::shared_ptr<VulkanRTSource> rt; // null = backbuffer
            std::size_t             stride = 16;  // vertex stride in bytes
            VkDescriptorSet         descSet = VK_NULL_HANDLE; // texture (or null)
            // Task 899: true for BasicEffect draws with no alpha-test/dual-tex/env-map/skinned/
            // lit-textured override (stride 16/20/24) -- routes to the new fog-capable
            // colored3d/textured3d/colored_textured3d bundle. Left false (default) by the legacy
            // no-GpuDrawParams DrawColoredPrimitives()/DrawIndexedColoredPrimitives() path, which
            // still falls through to the original zero-descriptor-set colored3d pipeline.
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
            /// plans/plan_fx.md FX-065: everything a compiled-effect draw needs, captured at queue time.
            /// The uniform bytes and sampler views in particular CANNOT be read back at record
            /// time: the constant register files are shared by every effect this renderer owns, and
            /// a later ApplyPass would have overwritten them.
            struct CompiledEffectEXT
            {
                /// Linked modules, attribute layout and reflected samplers for the applied pass.
                VulkanCompiledEffect::LinkedPassEXT pass;
                /// Packed vertex-stage uniform block, empty when the shader declares none.
                std::vector<std::uint8_t> vertexUniforms;
                /// Packed pixel-stage uniform block, empty when the shader declares none.
                std::vector<std::uint8_t> pixelUniforms;
                /// One image view per reflected pixel sampler, in the same order.
                std::vector<VkImageView> samplerViews;
                /// The sampler register index each of those views binds to.
                std::vector<std::uint32_t> samplerBindings;
                /// The VkSampler for each of those views, built from the pass's own sampler_state
                /// block when it assigns one and from the device's slot state when it does not.
                /// Samplers are cached for the renderer's lifetime, so these stay valid; the views
                /// belong to the sampled textures and carry the same lifetime assumption every
                /// other deferred draw family here already makes of its own `descSet`.
                std::vector<VkSampler> samplers;
            };
            bool                    useCompiledEffect = false;
            CompiledEffectEXT       compiledEffect;
#endif
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
            VkDescriptorSet         pbrDescSet        = VK_NULL_HANDLE; // 7-sampler set
            // PbrParams UBO layout (floats), matching pbr3d.vert/frag.glsl's and
            // pbr3d_skinned.vert/frag.glsl's own struct exactly: [0..15]=light1/2 dir+diffuse (4
            // vec4), [16..31]=world mat4, [32..35]=eyePos+metallicFactor, [36..39]=emissive+
            // roughnessFactor, [40..43]=fogColor+weightsPerVertex, [44..47]=fogVector,
            // [48..51]=alphaTest, [52..55]=normal scale + occlusion strength + padding,
            // [56..59]=base/emissive decode + output-encode flags + padding,
            // [60..63]=unclamped dielectric F0 RGB + specular factor,
            // [64..103]=ten core affine texture-transform rows,
            // [104..119]=four specular affine texture-transform rows,
            // [120]=seven-bit texture-coordinate-set mask, [121..123]=deterministic padding.
            // 124 floats = 496 bytes; the dynamic UBO stride is padded to 512 bytes.
            float                   pbrUboData[124]   = {};
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
            VulkanOcclusionQueryRenderer* occlusionQuery = nullptr;
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
            // REMED-GFX-140: the logical render-pass segment (bind cycle) this draw was issued in,
            // captured by PushPending3DDraw from currentSegment_. Two draws with the same `rt` but
            // different `segment` belong to two different native render passes.
            uint64_t                segment = 0;
            // REMED-GFX-129: this draw's position in the whole frame's public command stream (see
            // PendingBatch::order). Stamped by PushPending3DDraw, the single choke point.
            uint64_t                order   = 0;
        };
        std::vector<Pending3DDraw>  pending3D_;
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        [[nodiscard]] VkPipeline GetOrCreateCompiledEffectPipelineEXT(
            const Pending3DDraw& draw, uint32_t colorAttachmentCount, bool msaa,
            VkFormat targetDepthFmt);
        [[nodiscard]] VkDescriptorSet GetOrCreateCompiledEffectSamplerSetEXT(
            uint32_t frameIdx, VkDescriptorSetLayout layout,
            const std::vector<std::uint32_t>& bindings,
            const std::vector<VkImageView>& views,
            const std::vector<VkSampler>& samplers);
        void RecordCompiledEffectDrawEXT(VkCommandBuffer cb, const Pending3DDraw& draw,
                                         uint32_t frameIdx);
        /// plans/plan_fx.md FX-065: capture everything the deferred replay of a compiled-effect draw
        /// needs, at the moment the draw is issued. Throws rather than dropping to a stock shader
        /// if any of it cannot be captured faithfully.
        void PrepareCompiledEffectDrawEXT(Pending3DDraw& d, const IVertexBufferRenderer& vb,
                                          const GpuDrawParams& params);
        /// The same capture, for geometry this renderer generates itself (SpriteBatch quads) and
        /// so describes with its own fixed declaration rather than a caller-supplied one.
        void PrepareCompiledEffectDrawEXT(
            Pending3DDraw& d,
            const std::vector<VulkanCompiledEffect::CompiledVertexStreamEXT>& streams,
            ICompiledEffectRuntime* runtime);
        /// Queues one SpriteBatch quad to be drawn with a compiled Effect, as a deferred 3D draw
        /// rather than through the stock sprite pipeline -- the compiled pass owns the whole
        /// program, including the projection the stock sprite shader would otherwise apply.
        CNAEXT void QueueCompiledEffectSpriteEXT(const Sprite2DVertex (&quad)[6],
                                                 VkImageView spriteView,
                                                 ICompiledEffectRuntime* runtime);
        std::map<std::uint64_t, VkShaderModule> compiledEffectShaderModules_;

    public:
        /**
         * @brief CNAEXT. Returns a VkShaderModule over exactly these SPIR-V bytes.
         *
         * plans/plan_fx.md FX-065. Owned by the renderer and keyed by the SPIR-V's own content, for two
         * reasons that are both correctness rather than economy. A draw is recorded at `Present()`
         * long after its pass was applied, so a module owned by the effect -- and destroyed the
         * next time that shader is re-linked, which `MOJOSHADER_linkSPIRVShaders` forces because it
         * patches the SPIR-V in place -- would be a dangling handle by then. And the pipeline cache
         * keys on the module handle, so a handle that can be destroyed and its value reused would
         * let a later pipeline lookup hit an entry built from different code.
         *
         * @param code SPIR-V words.
         * @param codeBytes Number of bytes at @p code.
         * @return The module; valid until the device is torn down.
         */
        CNAEXT [[nodiscard]] VkShaderModule GetOrCreateCompiledEffectShaderModuleEXT(
            const void* code, std::size_t codeBytes);

    private:
#endif
        // Task 447/854: pushes d onto pending3D_ after tagging it with the currently-active
        // OcclusionQuery (if any) -- the single choke point every DrawXPrimitives() call site
        // routes through, so query correlation doesn't need repeating at each of the 6 push
        // sites individually.
        void PushPending3DDraw(Pending3DDraw&& d);
        // Task 447/854: set by VulkanOcclusionQueryRenderer::Begin(), cleared by End() (only if
        // still pointing at itself, mirroring Bgfx's activeOcclusionQuery_ convention). Nested
        // Begin() calls are not supported by XNA's own OcclusionQuery API (one query in flight
        // at a time is the real, documented XNA usage pattern), so a single raw pointer (not a
        // stack) is sufficient.
        VulkanOcclusionQueryRenderer* activeOcclusionQuery_ = nullptr;
        // One independent per-Begin/End-cycle snapshot, its target RT (nullptr = backbuffer) and
        // the logical render-pass segment it was issued in. Owns each snapshot outright (Task 664
        // fix) so that a 2nd Begin()/End() on the same SpriteBatch object within one frame cannot
        // clobber the 1st cycle's data — each snapshot is pushed at End(), independent heap
        // storage, harvested (and destroyed via activeBatches_.clear()) once per frame in
        // RecordCommandBuffer(). REMED-GFX-140 added `segment`: without it two batches with the
        // same `rt` were indistinguishable, which is exactly what let two bind cycles merge.
        struct PendingBatch {
            std::unique_ptr<VulkanSpriteBatchRenderer::BatchSnapshot> snapshot;
            /// REMED-GFX-166: a share of the destination pass -- see Pending3DDraw::rt.
            std::shared_ptr<VulkanRTSource> rt;
            uint64_t        segment = 0;
            // REMED-GFX-129: this batch's position in the whole frame's public command stream, so
            // RecordCommandBuffer can place an ordered Clear() exactly where the game issued it.
            uint64_t        order   = 0;
        };
        std::vector<PendingBatch> activeBatches_;

        // Task 875: a render target explicitly `Clear()`-ed with no accompanying draw call still
        // needs its render pass recorded. `RecordCommandBuffer`'s segment list was previously built
        // purely from `activeBatches_`/`pending3D_` (both draw-call-populated), so a real, XNA-legal
        // "SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);" pattern with no draw in
        // between never got its render pass recorded at all — the target's colour image stayed
        // at VK_IMAGE_LAYOUT_UNDEFINED forever.
        //
        // REMED-GFX-140 turned the bare pointer list into a per-segment record that also CARRIES
        // the clear values. Before, every pass in a flush began with the frame-global clearR_/
        // clearDepth_/clearStencil_ as they stood at record time, so the last Clear() of the frame
        // supplied the colour for every earlier pass too. A clear now belongs to the bind cycle
        // that issued it.
        //
        // REMED-GFX-129 made it ONE RECORD PER PUBLIC Clear() CALL rather than one per bind cycle.
        // The previous shape merged every Clear() of a cycle into a single record because the only
        // way a clear could reach the GPU was the pass load op, and a pass has exactly one -- which
        // is precisely the defect: the merged record could not express "draw, then clear", could
        // not express two different clears, and on a PreserveContents target (LOAD_OP_LOAD) could
        // not be delivered at all. Each record now also carries `order`, its position in the whole
        // frame's public command stream, so RecordCommandBuffer can replay it between exactly the
        // draws it belongs between.
        struct PendingClear {
            uint64_t        segment = 0;
            /// REMED-GFX-166: a share of the destination pass -- see Pending3DDraw::rt.
            std::shared_ptr<VulkanRTSource> rt;
            float           r = 0.f, g = 0.f, b = 0.f, a = 1.f;
            float           depth   = 1.0f;
            int             stencil = 0;
            // Which aspects this Clear() actually asked for. REMED-GFX-143 introduced them for the
            // BACKBUFFER, where one segment may clear colour while loading the depth an earlier
            // segment wrote; REMED-GFX-129 made them decide the vkCmdClearAttachments aspect mask
            // and the colour-attachment list for EVERY target, so a depth-only Clear() no longer
            // takes the colour load action the target's usage had already chosen.
            bool            wantColor   = false;
            bool            wantDepth   = false;
            bool            wantStencil = false;
            // REMED-GFX-129: position in the frame's public command stream (see PendingBatch).
            uint64_t        order   = 0;
        };
        std::vector<PendingClear> pendingClears_;
        /**
         * @brief Records one public Clear() call. REMED-GFX-129/140/143, Task 875.
         *
         * @param color   The caller cleared the colour target.
         * @param depth   The caller cleared the depth buffer.
         * @param stencil The caller cleared the stencil buffer.
         */
        void NoteRenderTargetClearEXT(bool color, bool depth, bool stencil);
        /**
         * @brief REMED-GFX-129: records one ordered clear as a real vkCmdClearAttachments command.
         *
         * Must be called inside an active render pass. Emits one VkClearAttachment per bound colour
         * attachment when the request names ClearOptions::Target, plus one combined depth/stencil
         * entry whose aspect mask is intersected with what @p depthFmt actually owns -- asking for
         * the stencil aspect of a depth-only format is invalid usage, not a no-op.
         *
         * @param cb                   The command buffer currently recording the render pass.
         * @param clear                The public Clear() call being replayed.
         * @param colorAttachmentCount Colour attachments in the active subpass.
         * @param depthFmt             The depth attachment's format, VK_FORMAT_UNDEFINED if none.
         * @param width                Render-area width in texels.
         * @param height               Render-area height in texels.
         */
        void RecordOrderedClearEXT(VkCommandBuffer cb, const PendingClear& clear,
                                   uint32_t colorAttachmentCount, VkFormat depthFmt,
                                   uint32_t width, uint32_t height) const;

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
        // MRT proxies are retired as whole objects: SetRenderTargets() replaces the live proxy
        // while its queued render work legitimately remains, so the old proxy is kept alive here
        // until the consuming frame drains, instead of being destroyed out from under those entries.
        //
        // REMED-GFX-166 made every deferred destination SHARED, so the queued entries co-own the
        // proxy as well -- and this list is deliberately KEPT rather than replaced by that. Unlike a
        // VulkanTargetPassEXT, which owns nothing and is safe to drop the instant the queues are
        // cleared, a proxy OWNS its VkFramebuffer, and the queues are cleared inside
        // RecordCommandBuffer -- before the command buffer that references that framebuffer is
        // submitted. The two gates answer the two different questions: this list is the GPU one
        // (generations must drain), the shared entries are the CPU one (the record must happen).
        std::vector<std::pair<uint64_t, std::shared_ptr<VulkanMRTProxy>>> retiredMrtProxies_;
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
        CNAEXT [[nodiscard]] std::size_t TotalEffectDescSetEntriesForTests() const;
        // REMED-GFX-076: read-only test introspection -- number of effect-cache entries that
        // reference the given VkImageView handle value (cast to uint64_t). Proves a specific view's
        // entries are gone after its resource dies, and detects a later resource reusing the handle.
        CNAEXT [[nodiscard]] std::size_t EffectDescSetEntriesForViewInTests(uint64_t rawImageViewHandle) const;
    private:
        // REMED-GFX-075: free every retirement bucket whose consuming frame's fence has certainly
        // completed (generation + MaxFramesInFlight < frameGeneration_); `force` frees all of them
        // (used at teardown, after a full device wait). Run once per frame from SubmitFrame().
        void ProcessRetiredResources(bool force);
        // REMED-GFX-075: detach a dying OcclusionQuery from every pending 3D draw (nulls the borrowed
        // pointer, keeping the draw) and from activeOcclusionQuery_, so RecordCommandBuffer never
        // dereferences the freed query wrapper. The VkQueryPool itself is retired separately.
        void PurgeDeferredQuery(VulkanOcclusionQueryRenderer* q);

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
        // REMED-GFX-166: shared with every command enqueued while it is bound, so the pass a
        // command names cannot be destroyed under it.
        std::shared_ptr<VulkanRTSource> currentRT_;
        // REMED-GFX-129: monotonic position in the frame's public command stream. Every deferred
        // entry -- sprite batch, 3D draw and Clear() -- is stamped with the next value, so a
        // segment's contents can be replayed in exactly the order the game issued them instead of
        // "every clear, then every draw". Never reset within a record; it only has to be
        // MONOTONIC, and pendingClears_/activeBatches_/pending3D_ are all cleared together.
        uint64_t                   commandOrder_ = 0;
        /// REMED-GFX-129: allocates the next public-command-stream position.
        uint64_t NextCommandOrderEXT() { return ++commandOrder_; }
        // REMED-GFX-140: identifies the logical render pass every deferred entry belongs to. One
        // public bind/unbind cycle == one segment: the counter advances on EVERY assignment of
        // currentRT_ (see BeginRenderPassSegmentEXT), so binding the SAME target again after an
        // unbind starts a new segment rather than joining the previous one. RecordCommandBuffer
        // replays segments in ascending id order, each in its own
        // vkCmdBeginRenderPass/vkCmdEndRenderPass, which is what makes the second cycle's load
        // action, clear values, viewport and scissor apply to the second cycle only. It is a
        // grouping key, never a cache key -- render passes and framebuffers stay keyed on
        // compatibility (depth format, attachment identity), so many segments share one
        // VkRenderPass and one VkFramebuffer.
        //
        // REMED-GFX-143: this includes the BACKBUFFER. A segment whose rt is nullptr is a
        // backbuffer bind cycle and is recorded in the same ascending-id stream as the off-screen
        // ones, so `bind t; draw; unbind; draw to the backbuffer; bind t; draw; unbind; draw to the
        // backbuffer` records four passes in that order instead of two target passes followed by
        // one swapchain pass holding both backbuffer draws.
        uint64_t                   currentSegment_ = 0;
        /// Advances the segment counter and binds @p rt (nullptr = backbuffer). REMED-GFX-140.
        void BeginRenderPassSegmentEXT(std::shared_ptr<VulkanRTSource> rt);
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

        // Custom effect set by VulkanEffectRenderer::Bind() (Task 119)
        VulkanEffectRenderer* activeCustomEffect_ = nullptr;

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
        // Task 911: MSAA counterpart. REMED-GFX-141 gave it the LOAD_OP_LOAD variant it never had:
        // `discardContents` false keeps the multisample colour attachment's own samples across
        // passes (LOAD/STORE, COLOR_ATTACHMENT_OPTIMAL in and out) instead of clearing them and
        // discarding them at every pass end. The two variants are render-pass-compatible, so the
        // same pipelines serve both.
        VkRenderPass GetOrCreateRTRenderPassMsaa(VkFormat depthFmt, bool discardContents);
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
        /// REMED-GFX-169: `sampler2D`/`samplerCube` are the SamplerStates of slots 0 and 1, the
        /// same slot convention GetOrCreateDualTexDescSet already uses and EasyGL's texture units
        /// confirm. Both participate in the cache key, so the same view pair under two different
        /// samplers cannot share one descriptor set.
        VkDescriptorSet GetOrCreateEnvMapDescSet(uint32_t frameIdx,
                                                  VkImageView view2D, VkImageView viewCube,
                                                  VkSampler sampler2D, VkSampler samplerCube);
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
        /// REMED-GFX-169: @p sampler is slot 0's SamplerState, keyed as well as written.
        VkDescriptorSet GetOrCreateSkinnedDescSet(uint32_t frameIdx, VkImageView view2D,
                                                   VkSampler sampler);
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
        // Metallic-roughness BRDF ported from EasyGLRenderer::EnsurePbrProgram()/
        // EnsurePbrSkinnedProgram() unchanged; only the resource-binding plumbing differs.
        void       EnsurePbrResources();
        /// REMED-GFX-169: @p samplers are the SamplerStates of slots 0..6, one per material map.
        /// All seven participate in the cache key.
        VkDescriptorSet GetOrCreatePbrDescSet(uint32_t frameIdx, VkImageView baseColor,
                                               VkImageView normalMap, VkImageView metallicRoughness,
                                               VkImageView emissive, VkImageView occlusion,
                                               VkImageView specular, VkImageView specularColor,
                                               const VkSampler (&samplers)[7]);
        VkPipeline GetOrCreatePipelinePbr3D(std::size_t stride, VkPrimitiveTopology,
                                             bool depthTest, bool depthWrite,
                                             bool blend, int cullMode,
                                             uint32_t colorAttachmentCount, bool wireframe,
                                             bool msaa, const DepthStencilKeyParams& dsParams = {},
                                         const BlendKeyParams& blendParams = {},
                                         VkFormat targetDepthFmt = VK_FORMAT_UNDEFINED);
        void       EnsurePbrSkinnedResources();
        /// REMED-GFX-169: as GetOrCreatePbrDescSet, slots 0..6.
        VkDescriptorSet GetOrCreatePbrSkinnedDescSet(uint32_t frameIdx, VkImageView baseColor,
                                                      VkImageView normalMap, VkImageView metallicRoughness,
                                                      VkImageView emissive, VkImageView occlusion,
                                                      VkImageView specular, VkImageView specularColor,
                                                      const VkSampler (&samplers)[7]);
        VkPipeline GetOrCreatePipelinePbrSkinned3D(std::size_t stride, VkPrimitiveTopology,
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
        // Fills the 124-float PbrParams UBO layout shared by pbr3d.vert/frag.glsl and
        // pbr3d_skinned.vert/frag.glsl (see Pending3DDraw::pbrUboData's own layout comment).
        // weightsPerVertex is only meaningful for the pbr+skinned combo (stride 68); pass 0 for
        // the unskinned PbrEffect path (stride 48), where it's unused.
        void       FillPbrUboData(float (&out)[124], const GpuDrawParams& p, float weightsPerVertex);
        // BasicEffect lit-textured path (Task 897) — DirectionalLight1/2 + EmissiveColor,
        // forwarded via a small UBO (set=0,binding=1) alongside the unchanged 128-byte PC
        // (set=0,binding=0 stays the texture sampler; PC content unchanged from FillExtPushConst).
        void       EnsureLitTexturedResources();
        /// REMED-GFX-169: @p sampler is slot 0's SamplerState, keyed as well as written.
        VkDescriptorSet GetOrCreateLitTexturedDescSet(uint32_t frameIdx, VkImageView view2D,
                                                       VkSampler sampler);
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
        /// REMED-GFX-169: @p sampler is slot 0's SamplerState, keyed as well as written. This is
        /// the bundle EVERY plain textured BasicEffect draw uses (it is fog-CAPABLE, not
        /// fog-enabled), so it is the most-travelled of the corrected paths.
        VkDescriptorSet GetOrCreateFogTex3DDescSet(uint32_t frameIdx, VkImageView view2D,
                                                    VkSampler sampler);
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

        /**
         * @brief Selects the mip clamp and LOD bias of GraphicsDevice.SamplerStates[slot].
         *
         * @param slot Sampler slot.
         * @param maxMipLevel XNA MaxMipLevel: the most detailed level the sampler may select.
         * @param lodBias XNA MipMapLevelOfDetailBias, added to the computed level of detail.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Selects the third address mode of GraphicsDevice.SamplerStates[slot].
         *
         * @param slot Sampler slot.
         * @param addressW XNA TextureAddressMode: 0 = Wrap, 1 = Clamp, 2 = Mirror.
         */
        void ApplySamplerAddressW(int slot, int addressW) override;
        VkDescriptorSet GetOrCreateTexSamplerDescSet(VkImageView view, VkSampler sampler);

        /// REMED-GFX-169: slots 0..6 as one array, for the two PBR descriptor builders whose
        /// seven material maps occupy those slots.
        /// A struct return keeps the array a value at the call site rather than a raw pointer
        /// into member storage that a later ApplySamplerState could mutate.
        struct PbrSlotSamplersEXTResult { VkSampler s[7]; };
        [[nodiscard]] PbrSlotSamplersEXTResult PbrSlotSamplersRawEXT() const
        {
            return { { slotSamplers_[0], slotSamplers_[1], slotSamplers_[2],
                       slotSamplers_[3], slotSamplers_[4], slotSamplers_[5],
                       slotSamplers_[6] } };
        }

        // --- Custom Effect / SPIR-V loading (Task 119) ---
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        // ---- Swapchain lifecycle ----
        void RecreateSwapchain();
        void CleanupSwapchain();

        // ---- Frame recording ----
        // REMED-GFX-074: Full records the whole deferred frame (all render-target passes + the
        // backbuffer pass) and clears every pending queue, as before. RenderTargetsOnly records the
        // off-screen passes needed by a mid-frame GetData readback flush into a transient command
        // buffer -- it skips the backbuffer pass entirely (no swapchain image needed, so
        // `imageIndex` is unused), leaving the backbuffer's pending work intact for the eventual
        // real Present().
        //
        // REMED-GFX-151: "needed by" used to mean `onlyRT`'s own segments and nothing else, which
        // silently assumed a target's content depends only on draws issued INTO it. It also depends
        // on every render target it SAMPLES: the canonical XNA render-to-texture sequence renders
        // into A, unbinds it and draws with A into B, and the game then reads B. Filtering the
        // segment list down to B dropped A's producer pass, so B's consumer pass sampled an image
        // that had never been rendered into, and A's pass was recorded later, at Present -- after
        // the read that was supposed to observe it. Only an intervening `A.GetData()` repaired it,
        // by running this same flush for A first; GetData was an accidental execution barrier.
        //
        // `flushSegments` is the exact set of bind cycles this record must replay, computed by
        // FlushDeferredRenderTarget as the transitive closure of "the target being read, plus every
        // render target its cycles sample, plus theirs". Replay is in ascending segment id, i.e.
        // exactly the public order Present would have used. Nothing is over-synchronized: no wait,
        // no submit per target switch, no readback and no extra barrier are introduced, because each
        // render target's render pass already ends in SHADER_READ_ONLY_OPTIMAL with a
        // COLOR_ATTACHMENT_OUTPUT/COLOR_ATTACHMENT_WRITE -> FRAGMENT_SHADER|TRANSFER /
        // SHADER_READ|TRANSFER_READ exit dependency. Consumption uses the identical predicate, so
        // Present replays nothing this record already emitted and drops nothing it did not.
        // Ignored when mode is Full.
        enum class RecordMode { Full, RenderTargetsOnly };
        void RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex,
                                 RecordMode mode = RecordMode::Full,
                                 VulkanRTSource* onlyRT = nullptr,
                                 const std::vector<uint64_t>* flushSegments = nullptr);

        // REMED-GFX-151: the render-to-texture dependency graph for the frame being accumulated --
        // (bind cycle, group of a render target that cycle SAMPLES). Populated at enqueue time,
        // where the source is known and provably alive, and consumed/cleared with the bind cycles
        // themselves, so it never outlives the pending queues it describes. `group` is always a
        // `VulkanRTSource::DepthStencilOwnerEXT()` value, the same key the flush and the recorder
        // use for "one target", so a dependency and the work satisfying it cannot drift apart.
        std::vector<std::pair<uint64_t, const void*>> segmentSampledGroups_;
        // REMED-GFX-194 test diagnostics: exact pending proxy cycles and public attachment slots
        // selected for the latest 2D/cube target read. Never participates in selection or replay.
        std::vector<std::pair<uint64_t, uint32_t>> lastMrtReadbackMatchesEXT_;
        /** @brief The render-target group a sampled texture belongs to, or nullptr if ordinary. */
        static const void* SampledRenderTargetGroupEXT(const ITextureRenderer* tex);
        /** @brief Cube overload of SampledRenderTargetGroupEXT. */
        static const void* SampledRenderTargetGroupEXT(const ITextureCubeRenderer* cube);
        /** @brief Records that bind cycle @p segment samples render-target group @p group. */
        void NoteSampledRenderTargetGroupEXT(uint64_t segment, const void* group);
        /** @brief SpriteBatch entry point: notes @p tex if it is a render target. */
        void NoteSampledRenderTargetEXT(uint64_t segment, const IVulkanSamplable* tex);
        /** @brief Notes every render target a 3D draw samples, across all GpuDrawParams slots. */
        void NoteSampledSourcesEXT(const GpuDrawParams& params);

        // REMED-GFX-166 removed PurgeDeferredWorkForTarget. It existed only because a queued
        // entry's destination was a borrowed pointer into the dying wrapper's renderer object; the
        // entries are now co-owners of an independent VulkanTargetPassEXT, so nothing dangles and
        // nothing has to be thrown away. Its stated justification -- "a destroyed target is
        // unobservable" -- was false: the discarded work is the PRODUCER, and the surviving
        // consumer draw that samples the target is exactly what observes it (REMED-GFX-166).

        /**
         * @brief REMED-GFX-166: trace one resource's public disposal against the deferred queues.
         *
         * Records what the renderer object was, which native handles it is about to hand to the
         * retirement queue, the current frame generation, and -- the part that cannot be seen from
         * C++ object lifetime alone -- how much already-queued work still names it, as a
         * destination and (via REMED-GFX-151's dependency graph) as a sampled source. Emits
         * nothing unless CNA_VULKAN_LIFETIME_TRACE is set.
         *
         * @param kind    Short resource-kind tag, e.g. "rt2d".
         * @param renderer The renderer object being disposed.
         * @param dest    Its destination identity as the deferred queues hold it (the
         *                `VulkanRTSource`, which for a multiply-inheriting renderer is NOT the same
         *                address as @p renderer), or nullptr for a resource that is never a
         *                destination.
         * @param image   Its VkImage, for correlation with the retirement and free lines.
         * @param view    Its sampled VkImageView.
         * @param fb      Its VkFramebuffer, VK_NULL_HANDLE for a non-destination resource.
         */
        CNAEXT void TraceTargetDisposalEXT(const char* kind, const void* renderer, const void* dest,
                                         VkImage image, VkImageView view, VkFramebuffer fb) const;

        // REMED-GFX-074: if `rt` has pending deferred work, record + submit ONLY its off-screen
        // pass now (no present, no swapchain, no frame-bookkeeping advance) so its colour image
        // holds the rendered result before a GetData readback, then drop the consumed entries so
        // Present() never replays them (no double-render). No-op if nothing is queued for `rt`.
        // REMED-GFX-194: `mrtAttachmentPass` is the exact immutable 2D or cube-face destination
        // being read. Each MRT proxy retains those pass objects in public attachment order, so the
        // lookup distinguishes resource, cube face, mip chain, attachment slot and binding cycle
        // without falling back to parent-cube or resolve-view identity.
        void FlushDeferredRenderTarget(VulkanRTSource* rt,
                                       const VulkanTargetPassEXT* mrtAttachmentPass,
                                       int requestedMipLevel);

        // Submits one frame (render + optional deferred readback copy). When deferSwap is
        // true the swapchain image is acquired, rendered and the GPU is waited on, but
        // vkQueuePresentKHR is NOT issued — letting ReadBackbuffer read the staging buffer
        // BEFORE the image is handed to the presentation engine. FinishDeferredPresent()
        // then presents the held image. Returns false if the swapchain was recreated.
        bool SubmitFrame(bool deferSwap);
        void FinishDeferredPresent();
        uint32_t deferredPresentImageIndex_ = 0;
        bool     hasDeferredPresent_        = false;

        // ---- Memory / resource helpers (also used by texture/buffer renderers) ----
        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
        void     CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& buf, VkDeviceMemory& mem, void** mapped = nullptr);
        VkCommandBuffer BeginOneTimeCommands();
        void            EndOneTimeCommands(VkCommandBuffer cb);
        void TransitionImageLayout(VkImage img, VkImageLayout from, VkImageLayout to,
                                   uint32_t baseMipLevel = 0);
        void CopyBufferToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h);
        VkShaderModule  CreateShaderModule(const uint32_t* spv, size_t byteSize);

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT*,
            void*);
    };
}
