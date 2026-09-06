#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Vulkan/VulkanCompiledEffect.hpp"
namespace {
    /// plans/plan_fx.md FX-112: the stream descriptor is spelled often enough in this file that the
    /// fully-qualified nested name is noise.
    using VkFxStreamEXT =
        CNA::Internal::Renderers::Vulkan::VulkanCompiledEffect::CompiledVertexStreamEXT;
}
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#endif
#include <bit>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

namespace CNA::Internal::Renderers::Vulkan
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;

    // -------------------------------------------------------------------------
    // REMED-GFX-166: native ownership trace
    //
    // A deferred renderer's lifetime defects are invisible from C++ object lifetime alone: a
    // wrapper can be long dead while its VkImage is legitimately still alive in the retirement
    // queue, and a draw can be recorded against a framebuffer whose contents nothing ever wrote.
    // Set CNA_VULKAN_LIFETIME_TRACE=1 to emit one line per ownership transition -- enqueue,
    // descriptor resolution, disposal request, retirement insertion, segment recording, draw
    // issue, submit, native free -- each carrying the public command order, the renderer object,
    // the VkImage/VkImageView/VkDescriptorSet involved and the frame generation. Off (and free
    // beyond one already-resolved bool test) unless the variable is set.
    // -------------------------------------------------------------------------
    namespace
    {
        CNA::Platform::VulkanInstanceHandle ToPlatformInstance(const VkInstance instance) noexcept
        {
            return reinterpret_cast<CNA::Platform::VulkanInstanceHandle>(instance);
        }

        template<typename TSurface>
        TSurface FromPlatformSurface(
            const CNA::Platform::VulkanSurfaceHandle surface) noexcept
        {
            if constexpr (std::is_pointer_v<TSurface>)
                return reinterpret_cast<TSurface>(static_cast<std::uintptr_t>(surface));
            else
                return static_cast<TSurface>(surface);
        }

        bool VulkanLifetimeTraceOnEXT()
        {
            static const bool on = [] {
                const char* v = std::getenv("CNA_VULKAN_LIFETIME_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
            return on;
        }

        void VkLifetimeTraceEXT(const char* fmt, ...)
        {
            if (!VulkanLifetimeTraceOnEXT()) return;
            std::fputs("[VKLT] ", stderr);
            va_list ap;
            va_start(ap, fmt);
            std::vfprintf(stderr, fmt, ap);
            va_end(ap);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }

        /// REMED-GFX-169: the sampler/descriptor trace. Set CNA_VULKAN_SAMPLER_TRACE=1 to emit one
        /// line per public sampler application and one per combined-image-sampler binding written
        /// into a descriptor set, each carrying the command family, the texture slot, the image
        /// view, the VkSampler actually written, the descriptor-set cache key, the descriptor set
        /// and whether the cache HIT. That chain is what distinguishes "the sampler never reached
        /// the descriptor" from "the descriptor was correct but a stale cache entry was reused":
        /// two draws with the same image view and different samplers must show different
        /// `sampler=` values AND different `key=` values. Off (and free beyond one already-resolved
        /// bool test) unless the variable is set.
        bool VulkanSamplerTraceOnEXT()
        {
            static const bool on = [] {
                const char* v = std::getenv("CNA_VULKAN_SAMPLER_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
            return on;
        }

        void VkSamplerTraceEXT(const char* fmt, ...)
        {
            if (!VulkanSamplerTraceOnEXT()) return;
            std::fputs("[VKST] ", stderr);
            va_list ap;
            va_start(ap, fmt);
            std::vfprintf(stderr, fmt, ap);
            va_end(ap);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }

        /// REMED-GFX-189: the render-target readback trace. Set CNA_VULKAN_TARGET_READBACK_TRACE=1
        /// to emit one line per public RenderTarget2D readback that reaches this renderer, carrying
        /// the target's public dimensions and DECLARED level count beside the level actually asked
        /// for, the native image and subresource selected to satisfy it, the copy extent, the
        /// staging size and the first texel handed back. Printing the declaration beside the
        /// request is the whole point: every individual handle, extent and staging size can be
        /// perfectly plausible while the requested level does not exist at all, which is exactly
        /// how an out-of-range read escaped detection here -- it returned normally, wrote the
        /// caller's buffer, and every number in the copy looked right on its own. Off (and free
        /// beyond one already-resolved bool test) unless the variable is set.
        bool VulkanTargetReadbackTraceOnEXT()
        {
            static const bool on = [] {
                const char* v = std::getenv("CNA_VULKAN_TARGET_READBACK_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
            return on;
        }

        void VkTargetReadbackTraceEXT(const char* fmt, ...)
        {
            if (!VulkanTargetReadbackTraceOnEXT()) return;
            std::fputs("[VKRB] ", stderr);
            va_list ap;
            va_start(ap, fmt);
            std::vfprintf(stderr, fmt, ap);
            va_end(ap);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }

        /// REMED-GFX-166: which deferred 3D command family a draw belongs to, for the trace.
        /// The flags are mutually exclusive by construction at every enqueue site; the order here
        /// mirrors the recorder's own pipeline-selection order so the two cannot disagree.
        template <typename D>
        const char* Pending3DFamilyEXT(const D& d)
        {
            if (d.isMarker)        return "Marker";
            if (d.usePbrSkinned)   return "PbrSkinned";
            if (d.usePbr)          return "Pbr";
            if (d.useSkinned)      return "Skinned";
            if (d.useEnvMap)       return "EnvMap";
            if (d.useDualTexture)  return "DualTexture";
            if (d.useAlphaTest)    return "AlphaTest";
            if (d.useLitTextured)  return "LitTextured";
            if (d.useInstanced)    return "Instanced";
            if (d.useFogTex3D)     return "BasicEffect";
            return "Colored";
        }

        /// Opaque handle value of any Vulkan object, for tracing on 32- and 64-bit handle builds.
        template <typename T>
        unsigned long long VkH(T handle)
        {
            if constexpr (std::is_pointer_v<T>)
                return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(handle));
            else
                return static_cast<unsigned long long>(handle);
        }
    }

    static const char* const kValidationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    static const char* const kDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

// Validation is desired in debug builds but requires the Khronos layer.
// Checked at runtime in CreateInstance(); flipped to false if unavailable.
#ifdef NDEBUG
    static bool sEnableValidation = false;
#else
    static bool sEnableValidation = true;
#endif

    // REMED-GFX-144: opt-in synchronization validation, off unless a regression asks for it. The
    // Khronos layer's synchronization checks are not part of the default validation set, which is
    // why the whole class went unmeasured until REMED-GFX-140; requesting them through
    // VkValidationFeaturesEXT keeps the regression self-contained rather than dependent on a
    // VK_LAYER_SETTINGS_PATH file or an environment variable a test runner may not forward.
    static bool sRequestSyncValidation = false;

    // plan_vulkan.md VULKAN-390, widened by VULKAN-391: how many more descriptor-set allocations a
    // test wants to fail. 0 in every production run. See
    // SetDescriptorAllocationFailuresForTestEXT for why an injected failure is the only way to
    // execute the chaining and refusal arms on the drivers here -- neither is reachable by volume.
    static std::uint32_t sDescriptorAllocFailuresToInject = 0;
    // VULKAN-391: how many allocations to let through FIRST. A single draw can allocate from more
    // than one pool -- the textured BasicEffect route takes a set from the shared
    // combined-image-sampler pool before it takes one from its own -- and that shared pool CHAINS
    // rather than refusing (VULKAN-390), so without a skip the injected failure is absorbed there
    // and the arm under test never runs. Measured: a test written without this asserted a refusal
    // and got a successful draw.
    static std::uint32_t sDescriptorAllocSkipsBeforeInjection = 0;
    // plan_vulkan.md VULKAN-161: how many more `vkCreateSampler` calls a test wants to fail. 0 in
    // every production run. Exhausting `maxSamplerAllocationCount` for real would mean creating
    // tens of thousands of samplers, which is a slow way to reach one branch.
    static std::uint32_t sSamplerCreationFailuresToInject = 0;
    // plan_vulkan.md VULKAN-026: how many more swapchain acquires a test wants to report
    // VK_ERROR_OUT_OF_DATE_KHR. 0 in every production run. The state is real and reachable --
    // GetBackBufferData's own comment calls it "common on first frame under Wayland/RADV" -- but
    // it arrives when the window manager decides, not when a test asks, so it cannot be driven
    // by a resize under Xvfb. Injected INSTEAD OF the acquire rather than after it, because a
    // successful acquire that is then reported out of date would leave the image-available
    // semaphore signalled with no waiter.
    static std::uint32_t sSwapchainOutOfDateToInject = 0;

    // VULKAN-390: one allocation attempt, with the test-only failure injection folded in so every
    // call site sees the same behaviour a real VK_ERROR_OUT_OF_POOL_MEMORY would produce.
    //
    // VULKAN-391 moved it here, ahead of every caller. It used to sit beside the one pool that
    // used it, which is exactly why the other ten sites called `vkAllocateDescriptorSets` directly
    // and none of their failure arms could be tested at all.
    static VkResult AllocateOneDescriptorSet(VkDevice device,
                                             const VkDescriptorSetAllocateInfo& info,
                                             VkDescriptorSet& out)
    {
        if (sDescriptorAllocFailuresToInject > 0) {
            if (sDescriptorAllocSkipsBeforeInjection > 0) {
                --sDescriptorAllocSkipsBeforeInjection;
            } else {
                --sDescriptorAllocFailuresToInject;
                return VK_ERROR_OUT_OF_POOL_MEMORY;
            }
        }
        return vkAllocateDescriptorSets(device, &info, &out);
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    // Task 878/879: numeric sample count corresponding to a VkSampleCountFlagBits, for reporting
    // IRenderTargetRenderer::GetMultiSampleCount()'s real applied value.
    // plan_vulkan.md VULKAN-162 (finding F-30). A sample mask selects among samples that EXIST, so
    // only its low `samples` bits can change what a pipeline rasterizes. Carrying all 32 into the
    // pipeline key made `BlendState::MultiSampleMask` -- a 32-bit integer the game sets -- an
    // unbounded axis of a cache that never evicts: 32 distinct masks measured as 30 distinct
    // pipelines, most of them identical in every observable way. Narrowed here, the axis is bounded
    // by the sample count: one value at 1x, sixteen at 4x.
    //
    // 32 and above is left alone rather than shifted: `1u << 32` is undefined, and no device this
    // renderer targets rasterizes with that many samples anyway.
    static std::uint32_t NarrowSampleMaskEXT(std::uint32_t mask, int samples) noexcept
    {
        if (samples <= 0 || samples >= 32) return mask;
        return mask & ((1u << samples) - 1u);
    }

    static int SampleCountToInt(VkSampleCountFlagBits s)
    {
        switch (s) {
            case VK_SAMPLE_COUNT_2_BIT:  return 2;
            case VK_SAMPLE_COUNT_4_BIT:  return 4;
            case VK_SAMPLE_COUNT_8_BIT:  return 8;
            case VK_SAMPLE_COUNT_16_BIT: return 16;
            case VK_SAMPLE_COUNT_32_BIT: return 32;
            case VK_SAMPLE_COUNT_64_BIT: return 64;
            default:                     return 0;
        }
    }

    // Task 911: maps a Microsoft::Xna::Framework::Graphics::DepthFormat to a real, distinct
    // VkFormat per render target instance (mirrors EasyGL's/Bgfx's MapDepthFormat, but with a
    // device-capability fallback chain since Vulkan -- unlike GL/bgfx -- doesn't guarantee every
    // one of these formats is actually supported for DEPTH_STENCIL_ATTACHMENT usage on every
    // device). DepthFormat::None is handled by the caller (no VkFormat / no attachment at all --
    // this function is only called when a real depth buffer was actually requested).
    /// plan_vulkan.md VULKAN-215: when true, PickDepthFormat pretends each request's PREFERRED
    /// format is unsupported and takes its fallback. Every device measured here honours Depth16
    /// and Depth24 verbatim, so without this there is no reachable case in which a render target's
    /// applied depth format differs from the requested one -- and a test for "the report shows the
    /// substitution" would be comparing a request with itself.
    static bool sDepthFormatPreferredUnsupportedForTest = false;

    // plan_vulkan.md VULKAN-170: forces ClassifySurfaceFormatEXT's `Unsupported` arm for one
    // SurfaceFormat ordinal. Without it that arm is unreachable here -- llvmpipe (and every
    // desktop driver) reports SAMPLED_IMAGE|TRANSFER_DST for R8G8B8A8_UNORM, so the only format
    // this renderer stores is also the only one it could ever refuse, and a test could never see
    // the refusal it is supposed to guarantee. Same instrument, and the same reason, as
    // SetDepthFormatPreferredUnsupportedForTestEXT: force a state the DEVICE owns.
    static int sSurfaceFormatUnsupportedForTest = -1;

    static VkFormat PickDepthFormat(VkPhysicalDevice pd, DepthFormat requested)
    {
        auto supports = [pd](VkFormat fmt) {
            if (sDepthFormatPreferredUnsupportedForTest &&
                (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_X8_D24_UNORM_PACK32))
                return false;
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(pd, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        };
        switch (requested) {
        case DepthFormat::Depth16:
            if (supports(VK_FORMAT_D16_UNORM)) return VK_FORMAT_D16_UNORM;
            break;
        case DepthFormat::Depth24:
            // X8_D24_UNORM_PACK32: real 24-bit depth, no stencil -- the closest Vulkan
            // equivalent to XNA's Depth24 (no-stencil) request.
            if (supports(VK_FORMAT_X8_D24_UNORM_PACK32)) return VK_FORMAT_X8_D24_UNORM_PACK32;
            break;
        case DepthFormat::Depth24Stencil8:
        case DepthFormat::None:
        default:
            break;
        }
        // Depth24Stencil8's primary candidates, and the fallback for Depth16/Depth24 when their
        // preferred no-stencil format isn't supported on this device (a combined format with
        // unused stencil bits is still a real, correct depth buffer for those two requests).
        for (VkFormat fmt : { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT }) {
            if (supports(fmt)) return fmt;
        }
        // Last resort: any real depth-capable format at all.
        for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM }) {
            if (supports(fmt)) return fmt;
        }
        throw std::runtime_error("Vulkan: no suitable depth format for requested DepthFormat");
    }

    namespace
    {
        /// REMED-GFX-142: declared here, defined next to its REMED-GFX-129 sibling further down --
        /// the render-target constructors need it for their up-front depth layout transition, which
        /// must cover the stencil aspect too when the picked format actually has one.
        bool VkDepthFormatHasStencil(VkFormat f);
    }

    static int VertexCountForPrimitives(PrimitiveType pt, int n)
    {
        switch (pt) {
        case PrimitiveType::TriangleList:  return n * 3;
        case PrimitiveType::TriangleStrip: return n + 2;
        case PrimitiveType::LineList:      return n * 2;
        case PrimitiveType::LineStrip:     return n + 1;
        case PrimitiveType::PointListEXT:  return n;
        }
        return 0;
    }

    static VkPrimitiveTopology ToVkTopology(PrimitiveType pt)
    {
        switch (pt) {
        case PrimitiveType::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveType::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveType::PointListEXT:  return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    // =========================================================================
    // VulkanTextureRenderer
    // =========================================================================

    VulkanTextureRenderer::VulkanTextureRenderer(const ImageData& data, VulkanRenderer* owner)
        : width_(data.width), height_(data.height),
          levelCount_(data.mipLevels > 0 ? data.mipLevels : 1),
          // plan_vulkan.md VULKAN-170 (finding F-11): remember the format this texture was created
          // with, so GetSurfaceFormatEXT() reports it instead of the shared `0` default. Nothing in
          // this renderer branches on it yet -- Color is the only format CreateTexture allocates --
          // but a texture that cannot say what it is cannot be asked, and EasyGL's own sampler path
          // asks exactly this question.
          surfaceFormat_(data.surfaceFormat),
          owner_(owner)
    {
        VkDevice dev = owner_->device_;

        // plan_vulkan.md VULKAN-170: the native storage comes from the ONE table, not from a
        // constant here. With Color the only entry this is byte-for-byte what the hardcoded
        // VK_FORMAT_R8G8B8A8_UNORM / *4 did; what changes is that a format added to the table is
        // allocated and uploaded correctly instead of being silently stored as RGBA8. The fallback
        // is unreachable through the public API -- Texture2D refuses a format this renderer defers
        // on before it gets here -- and is the old behaviour rather than a throw so that an
        // internal caller constructing an ImageData with a format ordinal nobody classified keeps
        // working exactly as it did.
        VulkanRenderer::VulkanSurfaceFormatStorageEXT storage{ VK_FORMAT_R8G8B8A8_UNORM, 4, 1 };
        owner_->MapSurfaceFormatToStorageEXT(data.surfaceFormat, storage);
        vkFormat_      = storage.format;
        bytesPerTexel_ = storage.bytesPerTexel;
        blockExtent_   = storage.blockExtent;   // VULKAN-172

        // --- Staging buffer ---
        // VULKAN-172: block-counted for a compressed format, texel-counted otherwise, through the
        // one helper both paths share.
        VkDeviceSize size = VulkanRenderer::LevelByteCountEXT(storage, data.width, data.height);
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem);

        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, data.pixels.data(), static_cast<size_t>(size));
        vkUnmapMemory(dev, stagingMem);

        // --- VkImage ---
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = vkFormat_;
        imgInfo.extent        = { static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height), 1 };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (image) failed");
        vkBindImageMemory(dev, image_, memory_, 0);

        // Transition UNDEFINED → TRANSFER_DST_OPTIMAL, copy, → SHADER_READ_ONLY (level 0 only --
        // the shared transition helper hardcodes a single-level range).
        //
        // plan_vulkan.md VULKAN-401: all three in ONE command buffer. They used to be three
        // separate one-time submissions, so creating a texture cost three full vkQueueWaitIdle
        // calls where the Vulkan idiom pays one -- VULKAN-396 measured 48 creations at 144
        // one-time commands and 40.5% of the workload's wall clock spent in those waits.
        // Ordering inside the buffer is exactly what the barriers express, so batching them
        // changes the number of waits and nothing else.
        {
            VkCommandBuffer cb = owner_->BeginOneTimeCommands();
            owner_->RecordImageLayoutTransition(cb, image_,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            owner_->RecordBufferToImageCopy(cb, stagingBuf, image_,
                static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height));
            owner_->RecordImageLayoutTransition(cb, image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            owner_->EndOneTimeCommands(cb);
        }

        // Clean up staging
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);

        // Task 925: levels 1..levelCount_-1 have no data yet (uploaded later via
        // UpdatePixelsLevel) but still need a defined layout -- mirrors
        // VulkanTexture3DRenderer's own construction-time full-range barrier (Task 864).
        if (levelCount_ > 1)
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                                 static_cast<uint32_t>(levelCount_ - 1), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        // --- VkImageView ---
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = vkFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = static_cast<uint32_t>(levelCount_);
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(dev, &viewInfo, nullptr, &imageView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (texture) failed");

        // --- Descriptor set ---
        VkDescriptorSetAllocateInfo dsInfo{};
        dsInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsInfo.descriptorPool     = owner_->descriptorPool_;
        dsInfo.descriptorSetCount = 1;
        dsInfo.pSetLayouts        = &owner_->descriptorSetLayout_;
        // VULKAN-391: through the shared helper, so this arm is reachable by the injection hook.
        if (AllocateOneDescriptorSet(dev, dsInfo, descriptorSet_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateDescriptorSets (texture) failed");

        VkDescriptorImageInfo imgDescInfo{};
        imgDescInfo.sampler     = owner_->defaultSampler_;
        imgDescInfo.imageView   = imageView_;
        imgDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet_;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgDescInfo;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    }

    void VulkanTextureRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        // REMED-GFX-075: a deferred sprite/3D draw queued before this texture's destruction still
        // borrows imageView_ (baked into a cached descriptor set) until the next Present/GetData
        // record consumes it. Retire the handles instead of freeing them now (no device stall); the
        // texSamplerDescSets_ cache entry is evicted immediately so a reused VkImageView handle can
        // never hit a stale set. Frees happen once the consuming frame's fence has completed.
        VulkanRenderer::RetiredResources r;
        owner_->EvictSampledViewFromCaches(imageView_, r);
        if (descriptorSet_ != VK_NULL_HANDLE) { r.descriptorSets.push_back(descriptorSet_); descriptorSet_ = VK_NULL_HANDLE; }
        if (imageView_     != VK_NULL_HANDLE) { r.imageViews.push_back(imageView_);         imageView_     = VK_NULL_HANDLE; }
        if (image_         != VK_NULL_HANDLE) { r.images.push_back(image_);                  image_         = VK_NULL_HANDLE; }
        if (memory_        != VK_NULL_HANDLE) { r.memories.push_back(memory_);               memory_        = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
    }

    void VulkanTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!owner_ || !owner_->device_ || !rgba) return;
        VkDevice dev = owner_->device_;
        // VULKAN-170: sized by the texture's own texel size, not by an assumed 4. The shared layer
        // hands SetData the format's real bytes-per-texel as the stride (Texture2D.cpp's
        // `levelW * bytesPerTexel`), so both ends now agree for any format in the table.
        VulkanRenderer::VulkanSurfaceFormatStorageEXT storage{ vkFormat_, bytesPerTexel_, blockExtent_ };
        VkDeviceSize size = VulkanRenderer::LevelByteCountEXT(storage, width_, height_);

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem);

        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
        // VULKAN-172: a compressed upload is always whole-level (the shared layer assembles the
        // level's blocks before calling), so its "row" is the whole payload and the stride branch
        // below collapses to the straight memcpy.
        const int rowBytes = blockExtent_ > 1
            ? static_cast<int>(size)
            : width_ * bytesPerTexel_;
        if (stride == rowBytes)
        {
            std::memcpy(mapped, rgba, static_cast<std::size_t>(size));
        }
        else
        {
            for (int y = 0; y < height_; ++y)
                std::memcpy(static_cast<uint8_t*>(mapped) + y * rowBytes,
                            rgba + y * stride, static_cast<std::size_t>(rowBytes));
        }
        vkUnmapMemory(dev, stagingMem);

        // plan_vulkan.md VULKAN-401: barrier, copy, barrier in ONE command buffer. These three
        // used to be three separate one-time submissions, so an upload cost three full
        // vkQueueWaitIdle calls where the Vulkan idiom pays one -- measured by VULKAN-396 at
        // 40.5% of an upload workload's wall clock. Ordering within the buffer is what the
        // barriers are for, so batching them changes nothing but the number of waits.
        {
            VkCommandBuffer cb = owner_->BeginOneTimeCommands();
            owner_->RecordImageLayoutTransition(cb, image_,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            owner_->RecordBufferToImageCopy(cb, stagingBuf, image_,
                static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
            owner_->RecordImageLayoutTransition(cb, image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            owner_->EndOneTimeCommands(cb);
        }

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    // Task 925: transitions exactly ONE mip level's layout -- the shared TransitionImageLayout
    // helper always barriers level 0 regardless of the level actually being copied. Reusing it
    // for level>0 barriers mip 0 while vkCmdCopyBufferToImage targets the real level, a genuine
    // mismatch confirmed by live Vulkan validation. Texture3D's formerly identical mismatch was
    // corrected by REMED-GFX-093.
    void VulkanTextureRenderer::TransitionLevelLayout(int level, VkImageLayout from, VkImageLayout to)
    {
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image_;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 1, 0, 1 };
        VkPipelineStageFlags srcStage, dstStage;
        if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else { // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        owner_->EndOneTimeCommands(cb);
    }

    // Task 925 (split from Task 867): real GPU upload for level>0, mirroring
    // VulkanTexture3DRenderer::SetData's established staging-buffer pattern (Task 864) --
    // previously the shared IGraphicsRenderer no-op default, silently discarding the caller's
    // mip-level data.
    void VulkanTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (!owner_ || !owner_->device_ || !rgba || level < 0 || level >= levelCount_) return;
        VkDevice dev = owner_->device_;
        VulkanRenderer::VulkanSurfaceFormatStorageEXT levelStorage{ vkFormat_, bytesPerTexel_,
                                                                     blockExtent_ };
        VkDeviceSize size = VulkanRenderer::LevelByteCountEXT(levelStorage, levelW, levelH);

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void* mapped = nullptr;
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        std::memcpy(mapped, rgba, static_cast<std::size_t>(size));

        TransitionLevelLayout(level,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { 0, 0, 0 };
        region.imageExtent      = { static_cast<uint32_t>(levelW), static_cast<uint32_t>(levelH), 1 };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        owner_->EndOneTimeCommands(cb);

        TransitionLevelLayout(level,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }

    VulkanTextureRenderer::~VulkanTextureRenderer()
    {
        if (owner_) owner_->TraceTargetDisposalEXT("tex2d", this, nullptr, image_, imageView_,
                                                    VK_NULL_HANDLE);
        if (owner_) {
            auto& list = owner_->liveTextures_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    // =========================================================================
    // VulkanRenderTargetRenderer
    // =========================================================================

    // Mirrors EasyGLRenderer.cpp's CalculateRenderTargetMipLevels / Texture2D.cpp's
    // CalculateMipLevels (Task 878).
    static int CalculateVulkanRTMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    VulkanRenderTargetRenderer::VulkanRenderTargetRenderer(int w, int h, int depthFormat,
                                                          bool preserveContents,
                                                          VulkanRenderer* owner,
                                                          int requestedMultiSampleCount,
                                                          bool mipMap)
        : width_(w), height_(h), preserveContents_(preserveContents), owner_(owner)
    {
        // Task 911: real per-instance DepthStencilFormat fidelity -- None means no depth
        // attachment at all; otherwise a real, distinct VkFormat picked for THIS instance,
        // independent of the backbuffer's own depthFormat_.
        const bool hasDepth = (static_cast<DepthFormat>(depthFormat) != DepthFormat::None);
        if (hasDepth)
            depthVkFormat_ = PickDepthFormat(owner_->physicalDevice_, static_cast<DepthFormat>(depthFormat));
        VkDevice dev = owner_->device_;
        const uint32_t uw = static_cast<uint32_t>(w);
        const uint32_t uh = static_cast<uint32_t>(h);
        levelCount_ = mipMap ? CalculateVulkanRTMipLevels(w, h) : 1;

        // Task 878/879: this RT engages real MSAA only if it was asked for AND the renderer
        // itself was constructed with backbuffer MSAA enabled (sampleCount_ > 1) -- see the
        // "piggyback on the renderer's own sampleCount_" scope decision in plans/plan_graphics.md.
        // Reusing the renderer's single already-lazily-created MSAA pipeline/render-pass
        // infrastructure avoids threading an independent numeric sample count through every
        // pipeline cache key. If the renderer has no MSAA infrastructure at all, a RT-only MSAA
        // request honestly reports MultiSampleCount == 0 (via appliedMultiSampleCount_ staying
        // 0 below) rather than silently no-oping.
        const bool wantsMsaa = requestedMultiSampleCount > 0 &&
                               owner_->sampleCount_ > VK_SAMPLE_COUNT_1_BIT;

        // --- Color image (must use swapchainFormat_ for pipeline compatibility) ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { uw, uh, 1 };
        colorInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        colorInfo.arrayLayers   = 1;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC/DST (Task 878): needed by MaybeGenerateMips' vkCmdBlitImage cascade when
        // levelCount_ > 1; harmless to always request even for non-mipmapped RTs.
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        colorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &colorInfo, nullptr, &colorImage_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImage (color) failed");

        VkMemoryRequirements colorReq;
        vkGetImageMemoryRequirements(dev, colorImage_, &colorReq);
        VkMemoryAllocateInfo colorAlloc{};
        colorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        colorAlloc.allocationSize  = colorReq.size;
        colorAlloc.memoryTypeIndex = owner_->FindMemoryType(colorReq.memoryTypeBits,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &colorAlloc, nullptr, &colorMemory_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetRenderer: vkAllocateMemory (color) failed");
        vkBindImageMemory(dev, colorImage_, colorMemory_, 0);

        VkImageViewCreateInfo colorView{};
        colorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorView.image    = colorImage_;
        colorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorView.format   = owner_->swapchainFormat_;
        colorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(dev, &colorView, nullptr, &colorView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImageView (color) failed");

        // Task 878: a second view over the *full* mip range, used only for sampling (the
        // framebuffer attachment above must stay mip-0-only). Identical range to colorView_
        // when levelCount_ == 1, so this is a no-op change for every pre-existing non-mipmapped
        // RT — sampling still only ever sees level 0.
        VkImageViewCreateInfo sampleView = colorView;
        sampleView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(levelCount_), 0, 1 };
        if (vkCreateImageView(dev, &sampleView, nullptr, &colorSampleView_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImageView (color sample) failed");

        // --- Depth image (Task 911: only created when a real depth format was requested --
        // DepthFormat::None correctly gets no depth attachment at all now, matching EasyGL/Bgfx's
        // existing behavior. Promoted in-place to MSAA samples when this RT engages MSAA --
        // depthView_ is never sampled externally by anything in this codebase, so there is no
        // separate single-sample depth-resolve path to keep in sync, unlike colorImage_). ---
        if (hasDepth)
        {
            VkImageCreateInfo depthInfo{};
            depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthInfo.imageType     = VK_IMAGE_TYPE_2D;
            depthInfo.format        = depthVkFormat_;
            depthInfo.extent        = { uw, uh, 1 };
            depthInfo.mipLevels     = 1;
            depthInfo.arrayLayers   = 1;
            depthInfo.samples       = wantsMsaa ? owner_->sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
            depthInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            depthInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &depthInfo, nullptr, &depthImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImage (depth) failed");

            VkMemoryRequirements depthReq;
            vkGetImageMemoryRequirements(dev, depthImage_, &depthReq);
            VkMemoryAllocateInfo depthAlloc{};
            depthAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            depthAlloc.allocationSize  = depthReq.size;
            depthAlloc.memoryTypeIndex = owner_->FindMemoryType(depthReq.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &depthAlloc, nullptr, &depthMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkAllocateMemory (depth) failed");
            vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

            VkImageViewCreateInfo depthView{};
            depthView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            depthView.image    = depthImage_;
            depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthView.format   = depthVkFormat_;
            depthView.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &depthView, nullptr, &depthView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImageView (depth) failed");
        }

        if (wantsMsaa)
        {
            // --- MSAA color image: the actual attached render target. TRANSIENT_ATTACHMENT
            // only (never sampled directly) -- resolved automatically into colorImage_ at
            // vkCmdEndRenderPass via the render pass's pResolveAttachments mechanism, mirroring
            // VulkanRenderer::CreateMsaaColorResources' backbuffer counterpart exactly. ---
            VkImageCreateInfo msaaColorInfo{};
            msaaColorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            msaaColorInfo.imageType     = VK_IMAGE_TYPE_2D;
            msaaColorInfo.format        = owner_->swapchainFormat_;
            msaaColorInfo.extent        = { uw, uh, 1 };
            msaaColorInfo.mipLevels     = 1;
            msaaColorInfo.arrayLayers   = 1;
            msaaColorInfo.samples       = owner_->sampleCount_;
            msaaColorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            msaaColorInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            msaaColorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            msaaColorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &msaaColorInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImage (MSAA color) failed");

            VkMemoryRequirements msaaColorReq;
            vkGetImageMemoryRequirements(dev, msaaColorImage_, &msaaColorReq);
            VkMemoryAllocateInfo msaaColorAlloc{};
            msaaColorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            msaaColorAlloc.allocationSize  = msaaColorReq.size;
            msaaColorAlloc.memoryTypeIndex = owner_->FindMemoryType(msaaColorReq.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &msaaColorAlloc, nullptr, &msaaColorMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkAllocateMemory (MSAA color) failed");
            vkBindImageMemory(dev, msaaColorImage_, msaaColorMemory_, 0);

            VkImageViewCreateInfo msaaColorView{};
            msaaColorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            msaaColorView.image    = msaaColorImage_;
            msaaColorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            msaaColorView.format   = owner_->swapchainFormat_;
            msaaColorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &msaaColorView, nullptr, &msaaColorView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateImageView (MSAA color) failed");
        }

        // --- Framebuffer (Task 911: render pass now selected/lazily-created per this
        // instance's own real depthVkFormat_, not a single renderer-wide shared one). ---
        if (wantsMsaa)
        {
            // att0=MSAA color, att1=resolve (colorImage_/colorView_), att2=MSAA depth (if hasDepth)
            // -- mirrors GetOrCreateRTRenderPassMsaa()'s attachment order exactly.
            // REMED-GFX-141 gave that call a `discardContents` parameter and a real LOAD variant.
            // The 2D leg deliberately keeps asking for the CLEAR one: this finding's subject is the
            // six-face aliasing a cube target suffers, and a single-surface target has no second
            // face whose samples it could load by mistake. That a multisampled PreserveContents
            // RenderTarget2D still discards on Vulkan is the SAME missing-load-variant half of the
            // root cause, but it is a distinct, separately recorded claim that needs its own
            // cross-renderer oracle -- not a symmetry edit made here without one.
            VkImageView fbAtts[] = { msaaColorView_, colorView_, depthView_ };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_,
                                                                        /*discardContents=*/true);
            fbInfo.attachmentCount = hasDepth ? 3u : 2u;
            fbInfo.pAttachments    = fbAtts;
            fbInfo.width           = uw;
            fbInfo.height          = uh;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &msaaFramebuffer_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateFramebuffer (MSAA) failed");
            appliedMultiSampleCount_ = SampleCountToInt(owner_->sampleCount_);
        }
        else
        {
            VkImageView fbAtts[] = { colorView_, depthView_ };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            // Pipelines only ever need a reference render pass built against the discard
            // variant (see GetOrCreateRTRenderPass()'s own comment: discard/load differ only in
            // loadOp/initialLayout, which don't affect compatibility) -- but THIS framebuffer
            // must be built against whichever variant this RT instance actually uses.
            fbInfo.renderPass      = owner_->GetOrCreateRTRenderPass(depthVkFormat_, !preserveContents_);
            fbInfo.attachmentCount = hasDepth ? 2u : 1u;
            fbInfo.pAttachments    = fbAtts;
            fbInfo.width           = uw;
            fbInfo.height          = uh;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetRenderer: vkCreateFramebuffer failed");
        }

        // Transition color image to SHADER_READ_ONLY_OPTIMAL (initial state before first RT use).
        // Task 878: covers *all* levelCount_ levels (not just level 0, unlike the shared
        // single-level TransitionImageLayout helper) since colorSampleView_ above already
        // exposes the full range to the descriptor below, which declares that whole range to be
        // in SHADER_READ_ONLY_OPTIMAL — levels 1..levelCount_-1 get real content the first time
        // this RT is rendered into and unbound (MaybeGenerateMips), same as EasyGL's own
        // eager-allocate-then-regenerate-on-unbind pattern (Task 336).
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = colorImage_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            // REMED-GFX-142: a PRESERVING target's RT render pass loads depth and stencil, and
            // VK_ATTACHMENT_LOAD_OP_LOAD declares initialLayout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            // -- which the depth image is not in until something puts it there. One transition
            // here, on the command buffer the colour image is already using, makes the FIRST bind
            // legal; every later one finds the layout the pass's own finalLayout left. A
            // discarding target keeps LOAD_OP_CLEAR with initialLayout UNDEFINED and needs none of
            // this, so nothing is submitted for it. Contents stay undefined either way until the
            // target is first drawn into or cleared -- the same "a brand-new preserving target has
            // no previous content" rule REMED-GFX-136 established for colour.
            if (depthImage_ != VK_NULL_HANDLE && preserveContents_)
            {
                VkImageMemoryBarrier depthBarrier{};
                depthBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                depthBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                depthBarrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.image               = depthImage_;
                depthBarrier.subresourceRange    = {
                    static_cast<VkImageAspectFlags>(
                        VK_IMAGE_ASPECT_DEPTH_BIT |
                        (VkDepthFormatHasStencil(depthVkFormat_) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u)),
                    0, 1, 0, 1 };
                depthBarrier.srcAccessMask       = 0;
                depthBarrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &depthBarrier);
            }
            owner_->EndOneTimeCommands(initCb);
        }

        // --- Descriptor set so the RT can be sampled as a texture ---
        VkDescriptorSetAllocateInfo dsInfo{};
        dsInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsInfo.descriptorPool     = owner_->descriptorPool_;
        dsInfo.descriptorSetCount = 1;
        dsInfo.pSetLayouts        = &owner_->descriptorSetLayout_;
        // VULKAN-391: through the shared helper, so this arm is reachable by the injection hook.
        if (AllocateOneDescriptorSet(dev, dsInfo, descriptorSet_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetRenderer: vkAllocateDescriptorSets failed");

        VkDescriptorImageInfo imgDesc{};
        imgDesc.sampler     = owner_->defaultSampler_;
        imgDesc.imageView   = colorSampleView_;
        imgDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet_;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgDesc;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        // REMED-GFX-166: the destination the deferred queues will name. Built from the handles
        // above, all of which are final at this point and never replaced -- so this description
        // stays correct after ReleaseVulkanResources() has handed those handles to the retirement
        // queue, which is the window a command outliving this wrapper has to be replayed in.
        pass_ = std::make_shared<VulkanTargetPassEXT>();
        pass_->framebuffer   = (msaaFramebuffer_ != VK_NULL_HANDLE) ? msaaFramebuffer_ : framebuffer_;
        pass_->renderPass    = (msaaFramebuffer_ != VK_NULL_HANDLE)
                               ? owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_,
                                                                     /*discardContents=*/true)
                               : owner_->GetOrCreateRTRenderPass(depthVkFormat_, !preserveContents_);
        pass_->width         = width_;
        pass_->height        = height_;
        pass_->msaa          = (msaaFramebuffer_ != VK_NULL_HANDLE);
        pass_->depthFormat   = depthVkFormat_;
        // REMED-GFX-129: a PreserveContents target's non-MSAA pass loads its colour, so a leading
        // Clear() cannot ride the load action there. The MSAA variant this target asks for is
        // DiscardContents-shaped for every usage (see the framebuffer branch above).
        pass_->loadOpIsClear = pass_->msaa || !preserveContents_;
        pass_->mipImage      = colorImage_;
        pass_->mipLevels     = levelCount_;
        pass_->mipLayer      = 0;

        owner_->liveRenderTargets_.push_back(this);
        VkLifetimeTraceEXT("rt2d.create      renderer=%p image=0x%llx sampleView=0x%llx "
                           "fb=0x%llx msaaFb=0x%llx descSet=0x%llx %dx%d levels=%d gen=%llu",
                           static_cast<const void*>(this), VkH(colorImage_), VkH(colorSampleView_),
                           VkH(framebuffer_), VkH(msaaFramebuffer_), VkH(descriptorSet_),
                           width_, height_, levelCount_,
                           static_cast<unsigned long long>(owner_->frameGeneration_));
        VkLifetimeTraceEXT("rt2d.pass        renderer=%p pass=%p fb=0x%llx renderPass=0x%llx",
                           static_cast<const void*>(this), static_cast<const void*>(pass_.get()),
                           VkH(pass_->framebuffer), VkH(pass_->renderPass));
    }

    void VulkanRenderTargetRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        // REMED-GFX-075: a render target destroyed while it is still queued as a sampled SOURCE in
        // another draw (its colorSampleView_ baked into that draw's descriptor set) must keep that
        // view alive until the consuming record. Retire every handle (no device stall) and evict the
        // sample-view cache entries; the frame-fence-gated free covers both the borrowed-but-not-yet
        // recorded window and any in-flight GPU read. (Destination-queued work into THIS target was
        // already dropped by ~VulkanRenderTargetRenderer's PurgeDeferredWorkForTarget, GFX-074.)
        VulkanRenderer::RetiredResources r;
        owner_->EvictSampledViewFromCaches(colorSampleView_, r);
        owner_->EvictSampledViewFromCaches(colorView_, r);
        if (descriptorSet_   != VK_NULL_HANDLE) { r.descriptorSets.push_back(descriptorSet_); descriptorSet_   = VK_NULL_HANDLE; }
        if (framebuffer_     != VK_NULL_HANDLE) { r.framebuffers.push_back(framebuffer_);     framebuffer_     = VK_NULL_HANDLE; }
        if (msaaFramebuffer_ != VK_NULL_HANDLE) { r.framebuffers.push_back(msaaFramebuffer_); msaaFramebuffer_ = VK_NULL_HANDLE; }
        if (colorView_       != VK_NULL_HANDLE) { r.imageViews.push_back(colorView_);         colorView_       = VK_NULL_HANDLE; }
        if (colorSampleView_ != VK_NULL_HANDLE) { r.imageViews.push_back(colorSampleView_);   colorSampleView_ = VK_NULL_HANDLE; }
        if (colorImage_      != VK_NULL_HANDLE) { r.images.push_back(colorImage_);            colorImage_      = VK_NULL_HANDLE; }
        if (colorMemory_     != VK_NULL_HANDLE) { r.memories.push_back(colorMemory_);         colorMemory_     = VK_NULL_HANDLE; }
        if (msaaColorView_   != VK_NULL_HANDLE) { r.imageViews.push_back(msaaColorView_);     msaaColorView_   = VK_NULL_HANDLE; }
        if (msaaColorImage_  != VK_NULL_HANDLE) { r.images.push_back(msaaColorImage_);        msaaColorImage_  = VK_NULL_HANDLE; }
        if (msaaColorMemory_ != VK_NULL_HANDLE) { r.memories.push_back(msaaColorMemory_);     msaaColorMemory_ = VK_NULL_HANDLE; }
        if (depthView_       != VK_NULL_HANDLE) { r.imageViews.push_back(depthView_);         depthView_       = VK_NULL_HANDLE; }
        if (depthImage_      != VK_NULL_HANDLE) { r.images.push_back(depthImage_);            depthImage_      = VK_NULL_HANDLE; }
        if (depthMemory_     != VK_NULL_HANDLE) { r.memories.push_back(depthMemory_);         depthMemory_     = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
        appliedMultiSampleCount_ = 0;
    }

    VulkanRenderTargetRenderer::~VulkanRenderTargetRenderer()
    {
        if (owner_) owner_->TraceTargetDisposalEXT("rt2d", this, pass_.get(), colorImage_,
                                                    colorSampleView_, framebuffer_);
        if (owner_) {
            // REMED-GFX-166: queued work into this target is NOT dropped. Every entry that names
            // pass_ owns a share of it, so the description and its handles stay valid until the
            // frame that replays them -- which is what makes the producer of a target sampled by a
            // still-queued consumer actually run (see PassEXT()).
            auto& list = owner_->liveRenderTargets_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
            if (owner_->currentRT_ == pass_) owner_->BeginRenderPassSegmentEXT(nullptr);
        }
        ReleaseVulkanResources();
    }

    void VulkanRenderTargetRenderer::BindAsRenderTarget()
    {
        if (owner_) owner_->BeginRenderPassSegmentEXT(pass_);
    }

    void VulkanRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (owner_ && owner_->currentRT_ == pass_) owner_->BeginRenderPassSegmentEXT(nullptr);
    }

    // Task 878/907: regenerate a target's mip chain from level 0's just-rendered (and, where MSAA
    // was engaged, just-resolved) content via a vkCmdBlitImage cascade -- the Vulkan equivalent of
    // EasyGL's glGenerateMipmap-on-unbind (Task 336) / FNA3D's OPENGL_ResolveTarget. Called from
    // RecordCommandBuffer right after this pass's render pass ends, so level 0 is already in
    // SHADER_READ_ONLY_OPTIMAL (the RT render pass's finalLayout) when this runs.
    //
    // REMED-GFX-166: one implementation for both destination kinds. RenderTarget2D owns layer 0 of
    // its own image and a RenderTargetCube face owns layer `mipLayer` of the cube's six-layer
    // image; the two previously identical cascades differed only in that subresource layer, so they
    // are the same code with `mipLayer` filled in, and neither can drift from the other.
    void VulkanTargetPassEXT::MaybeGenerateMips(VkCommandBuffer cb)
    {
        if (mipLevels <= 1 || mipImage == VK_NULL_HANDLE) return;
        const uint32_t layer = mipLayer;

        auto barrier = [&](uint32_t level, VkImageLayout oldL, VkImageLayout newL,
                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout           = oldL;
            b.newLayout           = newL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = mipImage;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, layer, 1 };
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        barrier(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        int srcW = width, srcH = height;
        for (int level = 1; level < mipLevels; ++level) {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            barrier(static_cast<uint32_t>(level),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level - 1), layer, 1 };
            blit.srcOffsets[1]  = { srcW, srcH, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), layer, 1 };
            blit.dstOffsets[1]  = { dstW, dstH, 1 };
            vkCmdBlitImage(cb, mipImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              mipImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              1, &blit, VK_FILTER_LINEAR);

            // level-1 is done being read from -- restore it to its steady-state layout.
            barrier(static_cast<uint32_t>(level - 1),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            if (level < mipLevels - 1) {
                // Becomes the source for the next iteration.
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else {
                // Last level -- done, restore to steady-state layout directly.
                barrier(static_cast<uint32_t>(level),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }

            srcW = dstW; srcH = dstH;
        }
    }

    // REMED-GFX-074: read this render target's colour image back to CPU memory. Because Vulkan
    // defers every draw to a Present-time record, first flush any sprite/3D work queued into this
    // target so colorImage_ actually holds the rendered result (FlushDeferredRenderTarget is a
    // no-op when nothing is pending -- e.g. after a previous Present already recorded the pass, in
    // which case the RT render pass's SHADER_READ_ONLY_OPTIMAL finalLayout / the constructor's
    // initial transition already left colorImage_ readable). Then copy the requested sub-rectangle
    // via a host-visible staging buffer, mirroring VulkanTexture3DRenderer::GetData, applying the
    // swapchain BGRA->RGBA swap since the RT colour image uses swapchainFormat_.
    bool VulkanRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        static unsigned long long readbackCallEXT = 0;
        const unsigned long long callIndex = ++readbackCallEXT;
        VkTargetReadbackTraceEXT("rt2d.read.enter  call=%llu renderer=%p target=%dx%d LevelCount=%d "
                                 "requestedLevel=%d region=(%d,%d %dx%d) dataLength=%d",
                                 callIndex, static_cast<const void*>(this), width_, height_,
                                 levelCount_, level, x, y, w, h, dataLength);

        // REMED-GFX-189: a level this target does not own must be refused HERE, before any native
        // call. The sibling routes on this very renderer already do exactly this --
        // VulkanTexture3DRenderer::GetData, VulkanTextureCubeRenderer::GetData and
        // VulkanRenderTargetCubeRenderer::GetData all test `level < 0 || level >= levelCount_` --
        // and the 2D target was the one route that never learned it.
        //
        // Unguarded, the request reached vkCmdCopyImageToBuffer with an out-of-range
        // imageSubresource.mipLevel, and the shared layer's own `max(1, base >> level)` clamp had
        // already turned the nonexistent level's dimensions into a plausible 1x1 rectangle, so the
        // copy was well-formed on every axis except the one that mattered. It then FABRICATED:
        // the caller got level 0's texel (0,0) back and a `true` return, which is the invented
        // content REMED-GFX-127/130 exist to forbid. For a large enough level the same call
        // SIGSEGVs inside the driver instead, uncatchably.
        //
        // This is an ARGUMENT error, not a missing capability, so it throws rather than reporting
        // false -- reporting false would raise System::NotSupportedException through the shared
        // layer and tell a caller their renderer cannot read targets, which is untrue and hides the
        // real mistake. `std::out_of_range` matches both the shared `Texture2D::GetData`, which
        // already raises it for the NEGATIVE end of exactly this range, and the sibling GPU
        // guard added in REMED-GFX-186.
        //
        // Placed ahead of the capability test below deliberately: REMED-GFX-162's precedence has
        // specific public argument errors answered with their own error before a storage or
        // capability decision can weaken them into a generic refusal.
        if (level < 0 || level >= levelCount_)
        {
            VkTargetReadbackTraceEXT("rt2d.read.reject call=%llu requestedLevel=%d LevelCount=%d "
                                     "nativeOps=0 (no subresource, no transition, no staging, no "
                                     "command buffer, no copy, no submit, no wait, no destination "
                                     "write)",
                                     callIndex, level, levelCount_);
            throw std::out_of_range(
                "CNA Vulkan: RenderTarget2D::GetData: mip level " + std::to_string(level) +
                " does not exist (this target has " + std::to_string(levelCount_) +
                (levelCount_ == 1 ? " level)" : " levels)"));
        }

        // REMED-GFX-127: reporting false here is what makes the shared layer raise the missing
        // capability instead of handing the caller its own zero-initialized scratch buffer.
        if (!owner_ || colorImage_ == VK_NULL_HANDLE || !data || dataLength <= 0) return false;

        owner_->FlushDeferredRenderTarget(pass_.get(), pass_.get(), level);

        VkDevice dev = owner_->device_;
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);

        // Every colorImage_ level is in SHADER_READ_ONLY_OPTIMAL outside a render pass (the RT
        // render pass finalLayout plus mip generation, or the constructor's init barrier for a
        // never-rendered target). Transition only the level this copy addresses.
        owner_->TransitionImageLayout(colorImage_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<uint32_t>(level));

        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        // The staging allocation as it stands BEFORE the copy. Printing it is what separates "the
        // copy read the wrong subresource" from "the copy wrote nothing and the caller was handed
        // whatever this fresh host-visible allocation already contained": if the pre- and post-copy
        // bytes are identical, no image content reached the caller at all.
        const auto* preSrc = static_cast<const uint8_t*>(mapped);
        VkTargetReadbackTraceEXT("rt2d.read.native call=%llu image=0x%llx mipLevel=%u baseLayer=%u "
                                 "layerCount=%u srcLayout=TRANSFER_SRC_OPTIMAL offset=(%d,%d,0) "
                                 "extent=(%u,%u,%u) stagingBytes=%d preCopyStaging=(%u,%u,%u,%u) "
                                 "cb=%p",
                                 callIndex,
                                 static_cast<unsigned long long>(
                                     reinterpret_cast<std::uintptr_t>(colorImage_)),
                                 region.imageSubresource.mipLevel,
                                 region.imageSubresource.baseArrayLayer,
                                 region.imageSubresource.layerCount,
                                 region.imageOffset.x, region.imageOffset.y,
                                 region.imageExtent.width, region.imageExtent.height,
                                 region.imageExtent.depth, dataLength,
                                 dataLength >= 4 ? static_cast<unsigned>(preSrc[0]) : 0u,
                                 dataLength >= 4 ? static_cast<unsigned>(preSrc[1]) : 0u,
                                 dataLength >= 4 ? static_cast<unsigned>(preSrc[2]) : 0u,
                                 dataLength >= 4 ? static_cast<unsigned>(preSrc[3]) : 0u,
                                 static_cast<void*>(cb));
        vkCmdCopyImageToBuffer(cb, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);
        owner_->EndOneTimeCommands(cb);

        owner_->TransitionImageLayout(colorImage_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            static_cast<uint32_t>(level));

        const bool isBGRA = (owner_->swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             owner_->swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB);
        auto*       dst = static_cast<uint8_t*>(data);
        const auto* src = static_cast<const uint8_t*>(mapped);
        const int   pixels = dataLength / 4;
        for (int i = 0; i < pixels; ++i) {
            const int o = i * 4;
            if (isBGRA) { dst[o+0] = src[o+2]; dst[o+1] = src[o+1]; dst[o+2] = src[o+0]; dst[o+3] = src[o+3]; }
            else        { dst[o+0] = src[o+0]; dst[o+1] = src[o+1]; dst[o+2] = src[o+2]; dst[o+3] = src[o+3]; }
        }

        VkTargetReadbackTraceEXT("rt2d.read.exit   call=%llu elementsWritten=%d "
                                 "firstTexel=(%u,%u,%u,%u) result=true",
                                 callIndex, pixels,
                                 pixels > 0 ? static_cast<unsigned>(dst[0]) : 0u,
                                 pixels > 0 ? static_cast<unsigned>(dst[1]) : 0u,
                                 pixels > 0 ? static_cast<unsigned>(dst[2]) : 0u,
                                 pixels > 0 ? static_cast<unsigned>(dst[3]) : 0u);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return true;
    }

    // =========================================================================
    // VulkanSpriteBatchRenderer
    // =========================================================================

    VulkanSpriteBatchRenderer::VulkanSpriteBatchRenderer(VulkanRenderer* renderer)
        : renderer_(renderer) {}

    void VulkanSpriteBatchRenderer::Begin()
    {
        if (active_) return;
        vertices_.clear();
        indices_.clear();
        draws_.clear();
        currentTexture_  = nullptr;
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        pendingCompiledSprites_.clear();
#endif
        batchFirstIndex_ = 0;
        activeRT_        = renderer_->currentRT_;
        activeSegment_   = renderer_->currentSegment_;
        active_ = true;
    }

    void VulkanSpriteBatchRenderer::FlushTexture()
    {
        if (!currentTexture_) return;
        uint32_t count = static_cast<uint32_t>(indices_.size()) - batchFirstIndex_;
        if (count == 0) return;
        // Task 665 fix: previously always used the texture's own pre-baked descriptor set
        // (currentTexture_->GetVkDescriptorSet(), built once at texture-load time with a fixed
        // default sampler), completely bypassing SetSamplerFilter/SetSamplerAddressMode. Apply
        // the pending SamplerState to slot 0 (Task 118's existing per-slot VkSampler cache) and
        // build a fresh descriptor set combining the texture's own image view with THAT sampler.
        renderer_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        // REMED-GFX-151: if this run of sprites samples a RENDER TARGET, the bind cycle being
        // recorded depends on that target's earlier cycles, and a mid-frame readback flush has to
        // replay them first. `activeSegment_` (not currentSegment_) is the cycle this batch belongs
        // to, matching the PendingBatch pushed at End().
        renderer_->NoteSampledRenderTargetEXT(activeSegment_, currentTexture_);
        VkDescriptorSet ds = renderer_->GetOrCreateTexSamplerDescSet(
            currentTexture_->GetVkImageView(), renderer_->slotSamplers_[0]);
        draws_.push_back({ ds, batchFirstIndex_, count });
        batchFirstIndex_ = static_cast<uint32_t>(indices_.size());
    }

    void VulkanSpriteBatchRenderer::End()
    {
        if (!active_) return;
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        if (!pendingCompiledSprites_.empty())
        {
            FlushPendingCompiledSpritesEXT();
            active_ = false;
            return;
        }
#endif
        renderer_->activeCustomEffect_ = nullptr;
        if (customEffect_) customEffect_->Apply(); // may set renderer_->activeCustomEffect_
        customEffectRenderer_ = renderer_->activeCustomEffect_;
        FlushTexture();
        active_ = false;

        VkPipeline preparedCustomPipeline = VK_NULL_HANDLE;
        if (customEffectRenderer_) {
            const uint32_t colorAttachmentCount =
                activeRT_ ? activeRT_->GetColorAttachmentCount() : 1u;
            const bool wantsMsaa = activeRT_
                ? activeRT_->WantsMsaa()
                : renderer_->sampleCount_ > VK_SAMPLE_COUNT_1_BIT;
            const VkSampleCountFlagBits samples = wantsMsaa
                ? renderer_->sampleCount_
                : VK_SAMPLE_COUNT_1_BIT;
            const VkFormat depthFormat = activeRT_
                ? activeRT_->GetDepthFormat()
                : renderer_->depthFormat_;
            preparedCustomPipeline = customEffectRenderer_->GetOrCreatePipeline(
                colorAttachmentCount, samples, depthFormat,
                renderer_->blendEnabled_, renderer_->blendParams_);
        }

        // Task 664 fix: move this cycle's geometry into its own independent, frame-lifetime
        // snapshot pushed onto renderer_->activeBatches_ NOW (at End(), not Begin()), so a 2nd
        // Begin()/Draw()/End() cycle on this same object later in the same frame starts from
        // freshly-cleared vertices_/indices_/draws_ without ever touching (and destroying) this
        // cycle's already-completed data. Previously activeBatches_ stored a raw `this` pointer
        // pushed at Begin() time, so a 2nd Begin() call's vertices_.clear() wiped out the 1st
        // cycle's data in place before RecordCommandBuffer() ever harvested it at Present() —
        // only the last batch's draws ever survived to be recorded.
        if (!vertices_.empty() && !draws_.empty())
        {
            auto snapshot = std::make_unique<BatchSnapshot>();
            snapshot->vertices            = std::move(vertices_);
            snapshot->indices             = std::move(indices_);
            snapshot->draws               = std::move(draws_);
            // REMED-GFX-075: capture the custom effect's pipeline/layout/push-constants BY VALUE now,
            // while it is guaranteed alive (Apply() just ran), so the deferred record never touches
            // the effect wrapper -- it may be disposed before Present. The pipeline handle stays
            // valid past this batch via the effect renderer's retirement queue.
            if (customEffectRenderer_ && preparedCustomPipeline != VK_NULL_HANDLE) {
                snapshot->hasCustomEffect = true;
                snapshot->customPipeline  = preparedCustomPipeline;
                snapshot->customLayout    = customEffectRenderer_->GetPipelineLayout();
                std::memcpy(snapshot->customPushConst, customEffectRenderer_->GetPushConst(),
                            sizeof(snapshot->customPushConst));
            }
            // REMED-GFX-013: capture the scissor active for this batch (see BatchSnapshot).
            snapshot->scissorEnabled = renderer_->scissorEnabled_;
            snapshot->scissorX = renderer_->scissorX_; snapshot->scissorY = renderer_->scissorY_;
            snapshot->scissorW = renderer_->scissorW_; snapshot->scissorH = renderer_->scissorH_;
            // REMED-GFX-062: capture the viewport active for this batch (see BatchSnapshot).
            snapshot->viewportSet = renderer_->viewportSet_;
            snapshot->viewportX = renderer_->viewportX_; snapshot->viewportY = renderer_->viewportY_;
            snapshot->viewportW = renderer_->viewportW_; snapshot->viewportH = renderer_->viewportH_;
            snapshot->viewportMinDepth = renderer_->viewportMinDepth_;
            snapshot->viewportMaxDepth = renderer_->viewportMaxDepth_;
            // REMED-GFX-070: capture the blend constant active for this batch (see BatchSnapshot).
            snapshot->blendFactorR = renderer_->blendFactorR_;
            snapshot->blendFactorG = renderer_->blendFactorG_;
            snapshot->blendFactorB = renderer_->blendFactorB_;
            snapshot->blendFactorA = renderer_->blendFactorA_;
            // REMED-GFX-071: capture this batch's BlendState (blend enable + the six per-channel
            // Blend/BlendFunction values) BY VALUE, so drawSpritesFor selects a 2D pipeline whose
            // blend equation matches SpriteBatch.Begin()'s BlendState. SpriteBatch.Begin() applied
            // it to the device (GraphicsDevice::setBlendStateProperty -> ApplyBlendState) before
            // this renderer's Begin(), so renderer_->blendEnabled_/blendParams_ are this batch's.
            snapshot->blendEnabled = renderer_->blendEnabled_;
            snapshot->blendParams  = renderer_->blendParams_;
            // REMED-GFX-129: `order` is taken HERE, at End(), which is where the batch enters the
            // frame's command stream -- an ordered Clear() issued between Begin() and End() would
            // belong before it, and SpriteBatch has no way to interleave with one anyway.
            renderer_->activeBatches_.push_back(
                { std::move(snapshot), activeRT_, activeSegment_,
                  renderer_->NextCommandOrderEXT() });
            VkLifetimeTraceEXT("enqueue.sprite   order=%llu family=SpriteBatch rt=%p seg=%llu "
                               "draws=%zu",
                               static_cast<unsigned long long>(renderer_->activeBatches_.back().order),
                               static_cast<const void*>(activeRT_.get()),
                               static_cast<unsigned long long>(activeSegment_),
                               renderer_->activeBatches_.back().snapshot->draws.size());
        }
    }

#if defined(CNA_VULKAN_COMPILED_EFFECTS)
    void VulkanSpriteBatchRenderer::FlushPendingCompiledSpritesEXT()
    {
        // plans/plan_fx.md FX-102. XNA runs a compiled Effect's passes at FLUSH granularity over a whole
        // contiguous run of same-texture sprites: FNA's SpriteBatch.FlushBatch splits the batch
        // into texture runs and DrawPrimitives wraps each run's draw in `foreach (pass)`. So the
        // submission order for two sprites sharing a texture and two passes is s0p0, s1p0, s0p1,
        // s1p1 -- pass-major within a run, not sprite-major. Under a non-opaque blend the two
        // orders produce visibly different pixels.
        ICompiledEffectRuntime* runtime =
            customEffect_ != nullptr ? customEffect_->GetCompiledRuntimePtr() : nullptr;
        Microsoft::Xna::Framework::Graphics::EffectTechnique* technique =
            customEffect_ != nullptr ? customEffect_->getCurrentTechniqueProperty() : nullptr;
        const int passCount =
            technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
        if (runtime == nullptr || passCount == 0)
        {
            pendingCompiledSprites_.clear();
            throw std::logic_error(
                "CNA Vulkan: a compiled Effect used with SpriteBatch must have a current "
                "technique with at least one pass.");
        }

        std::size_t runStart = 0;
        while (runStart < pendingCompiledSprites_.size())
        {
            std::size_t runEnd = runStart + 1;
            while (runEnd < pendingCompiledSprites_.size() &&
                   pendingCompiledSprites_[runEnd].texture ==
                       pendingCompiledSprites_[runStart].texture)
            {
                ++runEnd;
            }
            for (int pass = 0; pass < passCount; ++pass)
            {
                technique->getPassesProperty()[pass].Apply();
                for (std::size_t i = runStart; i < runEnd; ++i)
                    QueueCompiledSpriteEXT(pendingCompiledSprites_[i], runtime);
            }
            runStart = runEnd;
        }
        pendingCompiledSprites_.clear();
    }

    void VulkanSpriteBatchRenderer::QueueCompiledSpriteEXT(
        const PendingCompiledSpriteEXT& sprite, ICompiledEffectRuntime* runtime)
    {
        const ITextureRenderer& texture = *sprite.texture;
        const float tw = static_cast<float>(texture.GetWidth());
        const float th = static_cast<float>(texture.GetHeight());

        float u1 = static_cast<float>(sprite.source.X) / tw;
        float v1 = static_cast<float>(sprite.source.Y) / th;
        float u2 = static_cast<float>(sprite.source.X + sprite.source.Width) / tw;
        float v2 = static_cast<float>(sprite.source.Y + sprite.source.Height) / th;
        if (static_cast<int>(sprite.effects) & static_cast<int>(SpriteEffects::FlipHorizontally))
            std::swap(u1, u2);
        if (static_cast<int>(sprite.effects) & static_cast<int>(SpriteEffects::FlipVertically))
            std::swap(v1, v2);

        const float r = sprite.color.getRProperty() / 255.f;
        const float g = sprite.color.getGProperty() / 255.f;
        const float b = sprite.color.getBProperty() / 255.f;
        const float a = sprite.color.getAProperty() / 255.f;

        // Identical corner math to the stock path above, so a compiled sprite lands exactly where
        // the same sprite drawn without an Effect would.
        const float dx = static_cast<float>(sprite.destination.X);
        const float dy = static_cast<float>(sprite.destination.Y);
        const float dw = static_cast<float>(sprite.destination.Width);
        const float dh = static_cast<float>(sprite.destination.Height);
        const float sw = static_cast<float>(sprite.source.Width);
        const float sh = static_cast<float>(sprite.source.Height);
        const float ox = sprite.origin.X;
        const float oy = sprite.origin.Y;
        const float sx = dw / sw;
        const float sy = dh / sh;

        const float cosR = std::cos(sprite.rotation);
        const float sinR = std::sin(sprite.rotation);
        auto corner = [&](float lx, float ly) {
            const float px = (lx - ox) * sx;
            const float py = (ly - oy) * sy;
            return Vector2::Transform(
                Vector2(dx + px * cosR - py * sinR, dy + px * sinR + py * cosR), transform_);
        };
        const Vector2 c0 = corner(0.f, 0.f);
        const Vector2 c1 = corner(sw, 0.f);
        const Vector2 c2 = corner(sw, sh);
        const Vector2 c3 = corner(0.f, sh);

        // Six vertices rather than four plus indices: the compiled draw route is non-indexed, and
        // a quad is small enough that the duplication costs nothing worth an index buffer.
        const Sprite2DVertex quad[6] = {
            {c0.X, c0.Y, u1, v1, r, g, b, a},
            {c1.X, c1.Y, u2, v1, r, g, b, a},
            {c2.X, c2.Y, u2, v2, r, g, b, a},
            {c2.X, c2.Y, u2, v2, r, g, b, a},
            {c3.X, c3.Y, u1, v2, r, g, b, a},
            {c0.X, c0.Y, u1, v1, r, g, b, a},
        };
        const auto* samplable = dynamic_cast<const IVulkanSamplable*>(&texture);
        if (samplable == nullptr)
            throw std::runtime_error("Vulkan SpriteBatch: texture is not IVulkanSamplable");
        renderer_->QueueCompiledEffectSpriteEXT(quad, samplable->GetVkImageView(), runtime);
    }
#endif  // CNA_VULKAN_COMPILED_EFFECTS

    void VulkanSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        float w = static_cast<float>(texture.GetWidth());
        float h = static_cast<float>(texture.GetHeight());
        Rectangle dest(static_cast<int>(x), static_cast<int>(y),
                        static_cast<int>(w), static_cast<int>(h));
        Rectangle src(0, 0, static_cast<int>(w), static_cast<int>(h));
        Draw(texture, dest, src, Color(255, 255, 255, 255));
    }

    void VulkanSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& dest, const Rectangle& src,
                                        const Color& color)
    {
        Draw(texture, dest, src, color, 0.f, Vector2(0.f, 0.f), SpriteEffects::None, 0.f);
    }

    void VulkanSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& dest, const Rectangle& src,
                                        const Color& color, float rotation,
                                        const Vector2& origin, SpriteEffects effects,
                                        float /*layerDepth*/)
    {
        if (!active_) throw std::runtime_error("Vulkan SpriteBatch: Draw called outside Begin/End");

#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-071/FX-080: customEffect_ is either ShaderEffect-derived or compiled,
        // never both. A compiled Effect owns the whole program including the projection, so its
        // sprites leave the stock sprite pipeline entirely -- letting them fall through to it
        // would render the sprite with the stock shader and ignore the Effect silently.
        if (ICompiledEffectRuntime* compiledRuntime =
                customEffect_ != nullptr ? customEffect_->GetCompiledRuntimePtr() : nullptr)
        {
            const PendingCompiledSpriteEXT sprite{&texture, dest, src, color,
                                                  rotation, origin, effects};
            if (!immediateMode_)
            {
                // Deferred and every sorted mode: XNA applies the passes when the batch FLUSHES,
                // over whole texture runs, so the sprite is only recorded here.
                pendingCompiledSprites_.push_back(sprite);
                return;
            }
            // Immediate: XNA flushes this one sprite now, so its run IS this sprite and applying
            // the passes around it here produces the same order.
            Microsoft::Xna::Framework::Graphics::EffectTechnique* technique =
                customEffect_->getCurrentTechniqueProperty();
            const int passCount =
                technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
            if (passCount == 0)
            {
                throw std::logic_error(
                    "CNA Vulkan: a compiled Effect used with SpriteBatch must have a current "
                    "technique with at least one pass.");
            }
            for (int pass = 0; pass < passCount; ++pass)
            {
                technique->getPassesProperty()[pass].Apply();
                QueueCompiledSpriteEXT(sprite, compiledRuntime);
            }
            return;
        }
#else
        if (customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr)
        {
            throw System::NotSupportedException(
                "CNA Vulkan: this build has no compiled-effect support "
                "(CNA_VULKAN_COMPILED_EFFECTS is off); SpriteBatch would silently draw with the "
                "stock sprite shader instead of the Effect.");
        }
#endif

        const auto* samplable = dynamic_cast<const IVulkanSamplable*>(&texture);
        if (!samplable)
            throw std::runtime_error("Vulkan SpriteBatch: texture is not IVulkanSamplable");
        if (currentTexture_ != nullptr && currentTexture_ != samplable)
            FlushTexture();
        currentTexture_ = samplable;

        float tw = static_cast<float>(texture.GetWidth());
        float th = static_cast<float>(texture.GetHeight());

        // Task 665 fix: no [0,1] clamp here — matches FNA, which divides straight through with
        // no clamping (SpriteBatch.cs). A sourceRectangle that extends past the texture bounds
        // intentionally produces UVs outside [0,1], letting the bound SamplerState's
        // TextureAddressMode (Wrap/Mirror/Clamp) govern edge sampling — the classic XNA
        // scrolling/tiling-background technique. Clamping here (the pre-fix behaviour) silently
        // defeated Wrap/Mirror addressing entirely, regardless of which sampler was bound.
        float u1 = (float)src.X / tw;
        float v1 = (float)src.Y / th;
        float u2 = (float)(src.X + src.Width)  / tw;
        float v2 = (float)(src.Y + src.Height) / th;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally))
            std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically))
            std::swap(v1, v2);

        float r = color.getRProperty() / 255.f;
        float g = color.getGProperty() / 255.f;
        float b = color.getBProperty() / 255.f;
        float a = color.getAProperty() / 255.f;

        float dx = (float)dest.X,  dy = (float)dest.Y;
        float dw = (float)dest.Width, dh = (float)dest.Height;
        float sw = (float)src.Width,  sh = (float)src.Height;
        float ox = origin.X,       oy = origin.Y;
        float sx = dw / sw,        sy = dh / sh;

        float p0x = (0.f - ox) * sx, p0y = (0.f - oy) * sy;
        float p1x = (sw  - ox) * sx, p1y = (0.f - oy) * sy;
        float p2x = (sw  - ox) * sx, p2y = (sh  - oy) * sy;
        float p3x = (0.f - ox) * sx, p3y = (sh  - oy) * sy;

        float cosR = std::cos(rotation), sinR = std::sin(rotation);
        auto rot = [&](float px, float py, float& rx, float& ry) {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rot(p0x, p0y, v0x, v0y);
        rot(p1x, p1y, v1x, v1y);
        rot(p2x, p2y, v2x, v2y);
        rot(p3x, p3y, v3x, v3y);

        // REMED-GFX-012: apply SpriteBatch's transform matrix here, in pixel space, before upload.
        // sprite2d.vert.glsl maps a raw pixel-space Position straight to NDC via viewportSize with
        // no projection-matrix uniform, so this is the correct place -- mathematically equivalent
        // to XNA's combined = transformMatrix * orthographicProjection, evaluated CPU-side, exactly
        // as D3D11SpriteBatchRenderer does for the identical Sprite2DVertex/shader contract.
        // Previously omitted entirely (SetTransformMatrix was unoverridden), so this transform was
        // silently dropped on Vulkan.
        const Vector2 tv0 = Vector2::Transform(Vector2(v0x, v0y), transform_);
        const Vector2 tv1 = Vector2::Transform(Vector2(v1x, v1y), transform_);
        const Vector2 tv2 = Vector2::Transform(Vector2(v2x, v2y), transform_);
        const Vector2 tv3 = Vector2::Transform(Vector2(v3x, v3y), transform_);

        auto base = static_cast<uint16_t>(vertices_.size());
        vertices_.push_back({tv0.X, tv0.Y, u1, v1, r, g, b, a});
        vertices_.push_back({tv1.X, tv1.Y, u2, v1, r, g, b, a});
        vertices_.push_back({tv2.X, tv2.Y, u2, v2, r, g, b, a});
        vertices_.push_back({tv3.X, tv3.Y, u1, v2, r, g, b, a});

        indices_.insert(indices_.end(),
            {static_cast<uint16_t>(base),
             static_cast<uint16_t>(base + 1),
             static_cast<uint16_t>(base + 2),
             static_cast<uint16_t>(base + 2),
             static_cast<uint16_t>(base + 3),
             static_cast<uint16_t>(base)});
    }

    // =========================================================================
    // VulkanVertexBufferRenderer
    // =========================================================================

    VulkanVertexBufferRenderer::VulkanVertexBufferRenderer(int vertex_capacity,
                                                         VulkanRenderer* owner)
        : capacity_(vertex_capacity), owner_(owner)
    {
        // `CreateVertexBuffer` is handed a vertex COUNT; the stride arrives with the first
        // SetData. 64 bytes per vertex is the opening guess, not a bound -- VULKAN-130's
        // EnsureByteCapacity widens it as soon as a real stride says it is too small.
        // A 0-vertex capacity (e.g. an empty model part) must still produce a valid,
        // non-empty VkBuffer -- vkCreateBuffer/vkAllocateMemory with size 0 is invalid
        // per the Vulkan spec.
        VkDeviceSize size = std::max<VkDeviceSize>(1, static_cast<VkDeviceSize>(vertex_capacity) * 64);
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_, memory_, &mappedPtr_);
        allocatedBytes_ = size;
    }

    void VulkanVertexBufferRenderer::EnsureByteCapacity(VkDeviceSize needed)
    {
        if (needed <= allocatedBytes_) return;
        if (!owner_ || !owner_->device_)
            throw std::runtime_error(
                "The Vulkan renderer: cannot grow a vertex buffer whose device is gone.");

        VkDevice dev = owner_->device_;
        VkBuffer       oldBuffer = buffer_;
        VkDeviceMemory oldMemory = memory_;

        VkBuffer       grownBuffer = VK_NULL_HANDLE;
        VkDeviceMemory grownMemory = VK_NULL_HANDLE;
        void*          grownMapped = nullptr;
        owner_->CreateBuffer(needed,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            grownBuffer, grownMemory, &grownMapped);

        // Carry the bytes already uploaded across. A grow is triggered by an upload that is about
        // to overwrite them, but SetData is not the only reader of this mapping and a partially
        // filled buffer must not become garbage because it grew.
        if (mappedPtr_ && allocatedBytes_ > 0)
            std::memcpy(grownMapped, mappedPtr_, static_cast<std::size_t>(allocatedBytes_));

        buffer_         = grownBuffer;
        memory_         = grownMemory;
        mappedPtr_      = grownMapped;
        allocatedBytes_ = needed;

        // Safe without a fence: nothing outside this object holds the handle (see the header).
        if (oldBuffer != VK_NULL_HANDLE) vkDestroyBuffer(dev, oldBuffer, nullptr);
        if (oldMemory != VK_NULL_HANDLE) vkFreeMemory(dev, oldMemory, nullptr);
    }

    void VulkanVertexBufferRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        // plan_vulkan.md VULKAN-392 (F-13): this used to open with vkDeviceWaitIdle, which made
        // every DrawUserPrimitives call a full-device stall -- that route allocates and destroys a
        // throwaway VertexBuffer per call (GraphicsDevice.cpp:1753+). Retire the handles into the
        // same fence-gated queue textures and render targets already use, so the free happens once
        // the consuming frame's fence has completed and no thread waits for it.
        //
        // Retiring is strictly safer here than the stall it replaces, not merely cheaper: the
        // deferred replay in RecordCommandBuffer binds only renderer-owned per-frame buffers
        // (spriteVB_, frame3DVB_, frame3DInstVB_) and never this handle, because a queued draw
        // carries COPIED vertex bytes. The only command that ever names this buffer is the staging
        // copy inside SetData, which EndOneTimeCommands has already waited on.
        VulkanRenderer::RetiredResources r;
        if (buffer_ != VK_NULL_HANDLE) { r.buffers.push_back(buffer_);  buffer_ = VK_NULL_HANDLE; }
        if (memory_ != VK_NULL_HANDLE) { r.memories.push_back(memory_); memory_ = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
    }

    VulkanVertexBufferRenderer::~VulkanVertexBufferRenderer()
    {
        if (owner_) {
            auto& list = owner_->liveVertexBuffers_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    void VulkanVertexBufferRenderer::SetData(const void* data, int vertex_count,
                                            std::size_t stride_in_bytes)
    {
        vertexCount_ = vertex_count;
        stride_      = stride_in_bytes;
        if (vertex_count <= 0 || stride_in_bytes == 0) return;

        // VULKAN-130: reserve the WHOLE logical capacity at this stride, not just the bytes this
        // call writes. Every draw route copies out of this mapping at the caller's own
        // vertexStart/vertexCount, which the shared layer bounds by the buffer's capacity rather
        // than by the last upload -- so a mapping sized to one short upload would still be read
        // past its end by a legal draw.
        const VkDeviceSize span = static_cast<VkDeviceSize>(
            std::max(vertex_count, capacity_)) * static_cast<VkDeviceSize>(stride_in_bytes);
        EnsureByteCapacity(span);

        std::memcpy(mappedPtr_, data,
                    static_cast<std::size_t>(vertex_count) * stride_in_bytes);
    }

    // =========================================================================
    // VulkanIndexBufferRenderer
    // =========================================================================

    VulkanIndexBufferRenderer::VulkanIndexBufferRenderer(int index_capacity, bool thirtyTwoBit,
                                                       VulkanRenderer* owner)
        : capacity_(index_capacity), thirtyTwoBit_(thirtyTwoBit), owner_(owner)
    {
        const std::size_t elemSize = thirtyTwoBit ? sizeof(uint32_t) : sizeof(uint16_t);
        // A 0-index capacity (e.g. an empty model part) must still produce a valid,
        // non-empty VkBuffer -- vkCreateBuffer/vkAllocateMemory with size 0 is invalid
        // per the Vulkan spec.
        VkDeviceSize size = std::max<VkDeviceSize>(1, static_cast<VkDeviceSize>(index_capacity) * elemSize);
        owner_->CreateBuffer(size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_, memory_, &mappedPtr_);
        allocatedBytes_ = size;
    }

    void VulkanIndexBufferRenderer::RequireByteCapacity(VkDeviceSize needed, const char* what) const
    {
        if (needed <= allocatedBytes_) return;
        throw std::runtime_error(
            std::string("The Vulkan renderer: ") + what + " would write "
            + std::to_string(needed) + " bytes into an index buffer mapped at "
            + std::to_string(allocatedBytes_) + " bytes ("
            + std::to_string(capacity_) + " indices of "
            + std::to_string(thirtyTwoBit_ ? 4 : 2) + " bytes). The upload is refused rather than "
            "truncated or written past the mapping.");
    }

    void VulkanIndexBufferRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || !owner_->device_) return;
        // VULKAN-392: the index-buffer twin of the vertex-buffer path above, for the same reason
        // and with the same safety argument -- a queued draw carries copied index bytes.
        VulkanRenderer::RetiredResources r;
        if (buffer_ != VK_NULL_HANDLE) { r.buffers.push_back(buffer_);  buffer_ = VK_NULL_HANDLE; }
        if (memory_ != VK_NULL_HANDLE) { r.memories.push_back(memory_); memory_ = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
    }

    VulkanIndexBufferRenderer::~VulkanIndexBufferRenderer()
    {
        if (owner_) {
            auto& list = owner_->liveIndexBuffers_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    void VulkanIndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        if (index_count <= 0) { indexCount_ = index_count; return; }
        RequireByteCapacity(
            static_cast<VkDeviceSize>(index_count) * sizeof(uint16_t), "a 16-bit index upload");
        indexCount_ = index_count;
        std::memcpy(mappedPtr_, data, static_cast<size_t>(index_count) * sizeof(uint16_t));
    }

    void VulkanIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        if (index_count <= 0) { indexCount_ = index_count; return; }
        RequireByteCapacity(
            static_cast<VkDeviceSize>(index_count) * sizeof(uint32_t), "a 32-bit index upload");
        indexCount_ = index_count;
        std::memcpy(mappedPtr_, data, static_cast<size_t>(index_count) * sizeof(uint32_t));
    }

    // =========================================================================
    // VulkanRenderer — construction
    // =========================================================================

    static VkSampleCountFlagBits PickSampleCount(VkPhysicalDevice pd, int requested)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        VkSampleCountFlags avail = props.limits.framebufferColorSampleCounts
                                 & props.limits.framebufferDepthSampleCounts;
        const VkSampleCountFlagBits candidates[] = {
            VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT,  VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_1_BIT
        };
        const int counts[] = { 64, 32, 16, 8, 4, 2, 1 };
        for (int i = 0; i < 7; ++i) {
            if (counts[i] <= requested && (avail & candidates[i]))
                return candidates[i];
        }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    VulkanRenderer::VulkanRenderer(const GraphicsRendererCreateArgs& args)
        : surfaceInfo_(args.surface)
        , platformSurfaceService_(&RequirePlatformVulkanSurface(args.vulkanSurface, "VULKAN"))
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , swapInterval_(args.swapInterval)
    {
        if (surfaceInfo_.windowId == 0)
            throw std::runtime_error("VulkanRenderer: missing platform window id");
        if (!(surfaceInfo_.displayScale > 0.0f)) surfaceInfo_.displayScale = 1.0f;

        CreateInstance();
        if (sEnableValidation) SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        sampleCount_ = PickSampleCount(physicalDevice_, args.multiSampleCount);
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT)
            std::clog << "[Vulkan] MSAA: " << static_cast<int>(sampleCount_) << "x\n";
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
        CreateRenderPass();
        // Task 911: RT render passes are no longer eagerly created here -- they're now
        // depth-format-keyed and lazily created on first use via GetOrCreateRTRenderPass()/
        // GetOrCreateRTRenderPassMsaa() (see VulkanRenderTargetRenderer/VulkanRenderTargetCubeRenderer).
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) {
            CreateMsaaColorResources();
            CreateRenderPassMsaa();
        }
        CreateFramebuffers();
        CreateCommandPool();
        AllocateCommandBuffers();
        CreateSyncObjects();
        CreateSampler();
        CreateDescriptorSetLayout();
        CreateDescriptorPool();
        // Task 911: the 2D sprite pipeline(s) are now lazily created per real target depth format
        // on first use (see GetOrCreatePipeline2D()/GetOrCreatePipeline2DMsaa(), called from
        // drawSpritesFor) rather than eagerly here.
        CreateSpriteBuffers();
        initialized_ = true;
        IGraphicsRenderer::RegisterForWindow(surfaceInfo_.windowId, this);
        std::clog << "[Vulkan] Renderer initialised\n";
    }

    // =========================================================================
    // VulkanRenderer — destruction
    // =========================================================================

    bool VulkanRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // plan_vulkan.md VULKAN-020. Every member of CNA::GraphicsCapability is answered here, and
        // there is deliberately **no `default:` arm**. Two things follow, and both are the point:
        //
        //  * appending a member to the enum makes this switch incomplete, and the target is built
        //    with `-Werror=switch` (modules/renderers/vulkan/CMakeLists.txt), so the build stops
        //    instead of this renderer claiming a feature nobody taught it;
        //  * a value that is not an enumerator at all -- only reachable by casting an out-of-range
        //    int -- falls out of the switch to the `return false` below. Refused, never claimed.
        //
        // The previous `default: return true` is what this replaces. GraphicsCapability's own
        // documentation names that shape three times as the reason `ComputeShaders`,
        // `IndirectDraw` and the float render-target entries had to be made derived: a catch-all
        // true turns every future capability into an opt-out promise.
        switch (capability)
        {
            // ---- Answered from a real device property -------------------------------------
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return anisotropySupported_;
            case CNA::GraphicsCapability::WireFrame:
                return fillModeNonSolidSupported_;
            case CNA::GraphicsCapability::MultipleRenderTargets:
                // The renderer caps its own MRT set at FNA's MAX_RENDERTARGET_BINDINGS, but the
                // question here is whether more than one attachment is expressible at all.
                return deviceLimits_.maxColorAttachments > 1;
            case CNA::GraphicsCapability::DepthStencilBuffer:
                // FindDepthFormat() picks a device-wide depth format at construction and throws if
                // the device offers none, so this is a real attachment rather than an assumption.
                return depthFormat_ != VK_FORMAT_UNDEFINED;
            case CNA::GraphicsCapability::StencilBuffer:
                // Separate from the above on purpose: FindDepthFormat() prefers a stencil-capable
                // format but falls back to VK_FORMAT_D32_SFLOAT, and on a device that took that
                // fallback DepthStencilState.StencilEnable cannot work however correctly the
                // pipeline maps it. Answer from the format actually chosen.
                return depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT
                    || depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT
                    || depthFormat_ == VK_FORMAT_D16_UNORM_S8_UINT
                    || depthFormat_ == VK_FORMAT_S8_UINT;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            {
                // VULKAN-021: the same intersection PickSampleCount reads, asked the same way.
                // Colour AND depth, because every MSAA path here attaches both -- a device that
                // could multisample colour alone still could not run this renderer's MSAA render
                // pass, so claiming the capability from the colour mask alone would be a promise
                // ApplyMultiSampleCount then declines to keep.
                const VkSampleCountFlags both = deviceLimits_.framebufferColorSampleCounts
                                              & deviceLimits_.framebufferDepthSampleCounts;
                return (both & ~static_cast<VkSampleCountFlags>(VK_SAMPLE_COUNT_1_BIT)) != 0;
            }

            // ---- Answered from what this renderer implements -------------------------------
            case CNA::GraphicsCapability::ThreeD:
                // DrawPrimitivesEx / DrawIndexedPrimitivesEx / DrawInstancedPrimitivesEx, real
                // depth and stencil state, and a full 3D pipeline cache.
                return true;
            case CNA::GraphicsCapability::OcclusionQuery:
                // VK_QUERY_TYPE_OCCLUSION is core Vulkan 1.0 with no feature bit guarding its
                // existence. Whether the COUNT is exact is a different question with a different
                // feature behind it, answered by IOcclusionQueryRenderer::PixelCountIsPreciseEXT()
                // (VULKAN-370) -- a device may support the query and still not count.
                return true;
            case CNA::GraphicsCapability::CustomEffects:
                // CreateEffect() builds a pipeline from a caller-supplied SPIR-V pair. What that
                // acceptance does and does not promise is GetShaderDialectEXT()'s job to say
                // (VULKAN-250), and the surface's own gaps are VULKAN-251..VULKAN-255.
                return true;
            case CNA::GraphicsCapability::Texture3D:
                // VulkanTexture3DRenderer is real storage with a real SetData/GetData round-trip.
                return true;
            case CNA::GraphicsCapability::Instancing:
                // vkCmdDrawIndexed with a per-instance binding; core, no extension.
                return true;
            case CNA::GraphicsCapability::AdditiveBlending:
                // A real VkPipelineColorBlendAttachmentState carried in the pipeline key, not a
                // degradation to source-over.
                return true;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: not yet implemented here. Every 3D pipeline in this renderer bakes
                // a single VkVertexInputBindingDescription at binding 0 with combined-layout
                // attribute offsets, so a second per-vertex stream has no binding to reach and no
                // attribute to claim. Reported honestly so an ordinary multi-stream draw is
                // rejected before submission instead of rendering from stream 0 alone.
                return false;

            // ---- Derived: answered above this switch by GraphicsDevice ----------------------
            // A game never reaches these cases -- GraphicsDevice::SupportsCapability answers all
            // six from a renderer virtual before asking the renderer's switch. They are answered
            // here anyway, from the SAME virtual, so that a caller holding an IGraphicsRenderer
            // directly (the C ABI, a renderer test) cannot be told something the seam would deny.
            case CNA::GraphicsCapability::CompiledEffects:
                return SupportsCompiledEffects();
            case CNA::GraphicsCapability::ComputeShaders:
                return SupportsComputeShadersEXT();
            case CNA::GraphicsCapability::IndirectDraw:
                return SupportsIndirectDrawEXT();
            case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
                return SupportsHalfFloatTextureLinearFilteringEXT();
            case CNA::GraphicsCapability::FloatRenderTargets:
                return SupportsRenderTargetSurfaceFormatEXT(
                    static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Vector4));
            case CNA::GraphicsCapability::HalfFloatRenderTargets:
                return SupportsRenderTargetSurfaceFormatEXT(
                    static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::HdrBlendable));
        }
        // Not an enumerator. See the note above: refused, never claimed.
        return false;
    }

    CNA::Internal::Renderers::ShaderDialectEXT VulkanRenderer::GetShaderDialectEXT() const
    {
        // VULKAN-250. Not device-dependent and not build-dependent: this renderer has exactly one
        // custom-effect intake, and CompileProgram rejects anything that is not SPIR-V words.
        return CNA::Internal::Renderers::ShaderDialectEXT::GlslVulkan;
    }

    void VulkanRenderer::RequirePbrStrideEXT(std::size_t stride, bool skinned) const
    {
        // The same two conditions GetOrCreatePipelinePbr3D and GetOrCreatePipelinePbrSkinned3D
        // apply, and deliberately the same two messages: a caller that catches this at the draw
        // and a caller that used to catch it at Present must not have to tell them apart.
        if (skinned) {
            if (stride != 68 && stride != 76 && stride != 80)
                throw std::runtime_error("Vulkan SkinnedPbrEffect requires vertex stride 68, 76 or 80");
        } else {
            if (stride != 48 && stride != 60)
                throw std::runtime_error("Vulkan PbrEffect requires vertex stride 48 or 60");
        }
    }

    void VulkanRenderer::RequireLegacyColoredStrideEXT(std::size_t stride, const char* route) const
    {
        // VULKAN-155 (F-24). GetOrCreatePipeline3D -- the only pipeline these two entry points
        // reach -- hard-codes `bind{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX }` and Position@0 +
        // Color@12, while the entry points copy `vertexCount * GetStride()` bytes. A 20-byte
        // record was copied at 20 and read at 16, so vertex 1 came from the middle of vertex 0.
        // Measured before the fix: both entry points drew the clear colour, silently.
        //
        // Refused rather than converted. This pair is a colored-vertex convenience with no
        // production caller and no GpuDrawParams -- the declaration-driven layout every other
        // route now takes is not reachable from here -- and D3D9 refuses the same stride by name.
        // An honest refusal on a legacy surface is worth more than a binding path nothing calls.
        if (stride != 16)
            throw std::runtime_error(
                std::string("Vulkan ") + route + " requires a 16-byte VertexPositionColor record; "
                "this buffer's stride is " + std::to_string(stride) +
                ". Use DrawPrimitivesEx, whose layout comes from the VertexDeclaration.");
    }

    void VulkanRenderer::RequireBoneIndexFormatEXT() const
    {
        // VULKAN-151. The skinned shaders take BLENDINDICES as `vec4` so that one shader serves
        // both spellings XNA allows; a `Byte4` element reaches it through
        // VK_FORMAT_R8G8B8A8_USCALED, which Vulkan does not require a device to support for vertex
        // buffers. Measured supported on both drivers available here
        // (spikes/vulkan-vertex-format-spike/). Where it is not, the honest answer is a refusal
        // that names the format and the way round it, not a pipeline that fails to build.
        if (!uscaledVertexFormatSupported_)
            throw std::runtime_error(
                "Vulkan: this device cannot bind VK_FORMAT_R8G8B8A8_USCALED as a vertex attribute, "
                "so a Byte4-spelled BlendIndices cannot reach the skinned shaders. Supply a "
                "VertexDeclaration spelling BlendIndices as Vector4, which binds natively.");
    }

    void VulkanRenderer::RequireSkinnedStrideEXT(std::size_t stride) const
    {
        // VULKAN-156. GetOrCreatePipelineSkinned3D and its VertexLit sibling both reduce their
        // binding stride to `(stride == 56) ? 56 : 52`, so ANY other stride was silently bound as
        // 52. A 20-byte record then has vertex 1 fetched from byte 52 -- past the whole 120-byte
        // copy of a six-vertex quad -- and the draw rasterizes nothing while reporting success.
        // Refused by name instead, the way the PBR pair already refuses, because a family that
        // draws nothing and says it succeeded is the one outcome a caller cannot act on.
        if (stride != 52 && stride != 56)
            throw std::runtime_error(
                "Vulkan SkinnedEffect requires vertex stride 52 or 56, or a VertexDeclaration "
                "supplying every input of its shader");
    }

    Matrix VulkanRenderer::XnaPixelCenterCorrectionEXT(PrimitiveType primitive) const
    {
        // Filled primitives only. The correction compensates for Direct3D's top-left FILL rule by
        // moving the pixel centre just inside the primitive; a line or a point has no fill rule,
        // and the same shift only moves it off the pixels XNA lights. See the header for the
        // measurement.
        switch (primitive) {
            case PrimitiveType::TriangleList:
            case PrimitiveType::TriangleStrip:
                break;
            default:
                return Matrix::getIdentityProperty();
        }

        // The destination's own extent, in the same order the pass itself resolves it: an explicit
        // Viewport wins, then the bound render target, then the swapchain.
        int vpW = 0;
        int vpH = 0;
        if (viewportSet_ && viewportW_ > 0 && viewportH_ > 0) {
            vpW = static_cast<int>(viewportW_);
            vpH = static_cast<int>(viewportH_);
        } else if (currentRT_) {
            vpW = currentRT_->GetWidth();
            vpH = currentRT_->GetHeight();
        } else {
            vpW = static_cast<int>(swapchainExtent_.width);
            vpH = static_cast<int>(swapchainExtent_.height);
        }
        if (vpW <= 0 || vpH <= 0)
            return Matrix::getIdentityProperty();

        // REMED-GFX-235's rule, for REMED-GFX-235's reason. At one sample the correction decides
        // which side of the fill edge the pixel CENTRE lands on and the sub-half-pixel margin keeps
        // it covered; at four samples the outer sample positions sit a quarter of a pixel out,
        // inside that margin, so the same translation starts REMOVING coverage from the outermost
        // row and column. EasyGL measured exactly 1/2 and 1/4 of the expected colour there.
        if (currentRT_ && currentRT_->WantsMsaa())
            return Matrix::getIdentityProperty();

        // XNA row-vector order: post-multiplying the WVP by this gives clip.xy += offset * clip.w.
        // The Y sign is EasyGL's unchanged, and that is not a coincidence to be tidied away: this
        // renderer's vertex shaders negate clip.y themselves (`pos.y = -pos.y`, Vulkan NDC), and
        // that negation cancels the NDC-direction difference, so the same offset produces the same
        // half-pixel shift down-and-right on screen in both renderers.
        return Matrix::CreateTranslation(xnaPixelCenterScale_ / static_cast<float>(vpW),
                                         -xnaPixelCenterScale_ / static_cast<float>(vpH),
                                         0.0f);
    }

    // plan_vulkan.md VULKAN-170: the single table that says which public SurfaceFormat values this
    // renderer has native storage for, and in which VkFormat each one is stored.
    //
    // It is deliberately the ONLY place that answers that question, because two answers are how a
    // renderer ends up classifying a format it cannot allocate. ClassifySurfaceFormatEXT reads it
    // to decide its verdict and VulkanTextureRenderer reads it to create the image, so a format
    // added here is offered and allocated in one step, and a format missing from it is deferred to
    // the framework rule rather than half-supported.
    //
    // Every entry that follows Color is added by its own row -- VULKAN-172 (Dxt1/3/5),
    // VULKAN-173 (Bgr565/Bgra5551/Bgra4444), VULKAN-174 (NormalizedByte2/4) -- and the verdict is
    // never widened ahead of the storage.
    bool VulkanRenderer::MapSurfaceFormatToStorageEXT(
        int surfaceFormatOrdinal, VulkanRenderer::VulkanSurfaceFormatStorageEXT& out) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormatOrdinal))
        {
            case SurfaceFormat::Color:
                out = { VK_FORMAT_R8G8B8A8_UNORM, 4 };
                return true;
            // plan_vulkan.md VULKAN-173. The two packed 16-bit formats GraphicsProfile.Reach
            // permits whose Vulkan spelling is CORE 1.0, so no extension and no version bump is
            // needed to name them. Component order is the entire risk here and is not guessed:
            //   Bgra5551 is D3DFMT_A1R5G5B5 -- A in bit 15, R 10..14, G 5..9, B 0..4 -- which is
            //     VK_FORMAT_A1R5G5B5_UNORM_PACK16 field for field.
            //   Bgr565 is D3DFMT_R5G6B5 -- R 11..15, G 5..10, B 0..4 -- which is
            //     VK_FORMAT_R5G6B5_UNORM_PACK16 field for field.
            // XNA's names read the channels in little-endian MEMORY order, Vulkan's read them from
            // the high bit of the packed word down; that is why the two spellings look reversed
            // and describe the same 16 bits. `Vulkan_Packed16Format` asserts the drawn colour, not
            // the acceptance, because a swapped R and B survives every construction check.
            case SurfaceFormat::Bgr565:
                out = { VK_FORMAT_R5G6B5_UNORM_PACK16, 2 };
                return true;
            case SurfaceFormat::Bgra5551:
                out = { VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2 };
                return true;
            // plan_vulkan.md VULKAN-174. The two signed-normalized byte formats, XNA's bump-map
            // pair. Both are core Vulkan 1.0 and their component order is the plain one -- X in R,
            // Y in G, and for the four-channel form Z in B and W in A -- so unlike the packed 16-bit
            // pair there is no bit-field reasoning to get wrong. What IS different from every other
            // entry in this table is the RANGE: a SNORM texel samples to [-1, 1], not [0, 1], which
            // is why ClassifyColorTransferFormatEXT refuses a Color-shaped transfer for them below.
            case SurfaceFormat::NormalizedByte2:
                out = { VK_FORMAT_R8G8_SNORM, 2 };
                return true;
            case SurfaceFormat::NormalizedByte4:
                out = { VK_FORMAT_R8G8B8A8_SNORM, 4 };
                return true;
            // plan_vulkan.md VULKAN-179. The third packed 16-bit format, and the only entry in this
            // table that is CONDITIONAL: `VK_FORMAT_A4R4G4B4_UNORM_PACK16` has no core-1.1
            // spelling, so it may be named only on a device that offered `VK_EXT_4444_formats` and
            // whose `formatA4R4G4B4` feature was enabled at device creation. That is why this
            // function is a member rather than a static -- what this renderer stores is a fact
            // about the device it opened. `VK_FORMAT_B4G4R4A4_UNORM_PACK16` is core 1.0 but is a
            // DIFFERENT layout (B 12..15, G 8..11, R 4..7, A 0..3), not a fallback for
            // `D3DFMT_A4R4G4B4`'s A 12..15, R 8..11, G 4..7, B 0..3.
            case SurfaceFormat::Bgra4444:
                if (!formatA4R4G4B4Supported_) return false;
                out = { VK_FORMAT_A4R4G4B4_UNORM_PACK16, 2 };
                return true;
            // plan_vulkan.md VULKAN-172. The three block-compressed formats Reach permits. XNA's
            // DXT1/3/5 are S3TC, which Vulkan spells BC1/BC2/BC3, and the mapping is exact:
            // DXT1 -> BC1_RGBA (8 bytes per 4x4 block), DXT3 -> BC2 (16, explicit 4-bit alpha),
            // DXT5 -> BC3 (16, interpolated alpha). Conditional on
            // VkPhysicalDeviceFeatures.textureCompressionBC, which must be ENABLED at device
            // creation and not merely reported -- ClassifySurfaceFormatEXT's second gate then
            // still asks VkFormatProperties per format, because the feature promises the family
            // and the properties promise this member of it.
            //
            // The BC1 choice is BC1_RGBA rather than BC1_RGB: XNA's Dxt1 carries the 1-bit
            // punch-through alpha the block encoding itself defines, and BC1_RGB would discard it.
            case SurfaceFormat::Dxt1:
                if (!textureCompressionBCSupported_) return false;
                out = { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8, 4 };
                return true;
            case SurfaceFormat::Dxt3:
                if (!textureCompressionBCSupported_) return false;
                out = { VK_FORMAT_BC2_UNORM_BLOCK, 16, 4 };
                return true;
            case SurfaceFormat::Dxt5:
                if (!textureCompressionBCSupported_) return false;
                out = { VK_FORMAT_BC3_UNORM_BLOCK, 16, 4 };
                return true;
            default:
                return false;
        }
    }

    RendererFormatVerdict VulkanRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        // plan_vulkan.md VULKAN-170. Two gates, in this order, and the order is the point.
        //
        // 1. Storage. A format this renderer has no VkFormat for is `Defer`, never `Unsupported`:
        //    the framework's own rule (Texture::ValidateFormat) is what applies then, and saying
        //    `Unsupported` would be this renderer claiming to have judged a format it has never
        //    looked at.
        // 2. The device. For a format it DOES store, the answer comes from the physical device's
        //    real VkFormatProperties rather than from this table -- a build that maps a format is
        //    not a device that can sample it, and a driver that cannot is entitled to say so.
        //    SAMPLED_IMAGE and TRANSFER_DST are exactly what VulkanTextureRenderer needs: it
        //    creates every texture with USAGE_SAMPLED_BIT | USAGE_TRANSFER_DST_BIT in
        //    TILING_OPTIMAL, so anything less would classify a format vkCreateImage would refuse.
        VulkanSurfaceFormatStorageEXT storage{};
        if (!MapSurfaceFormatToStorageEXT(surfaceFormat, storage))
            return RendererFormatVerdict::Defer;
        if (surfaceFormat == sSurfaceFormatUnsupportedForTest)
            return RendererFormatVerdict::Unsupported;
        if (physicalDevice_ == VK_NULL_HANDLE)
            return RendererFormatVerdict::Defer;
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, storage.format, &props);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return (props.optimalTilingFeatures & required) == required
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict VulkanRenderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        // plan_vulkan.md VULKAN-174, and the same answer EasyGL gives for the same reason. The
        // framework's rule is "any format whose texel is a multiple of four bytes", which
        // NormalizedByte4 satisfies -- it is four bytes wide. But its four bytes are SIGNED and
        // sample to [-1, 1], so a `Color`-shaped GetData/SetData over them would read or write the
        // wrong values while looking perfectly well-formed. Refuse the transfer rather than let the
        // width decide. NormalizedByte2 is named alongside it because the pair stands or falls
        // together; the framework rule already excludes it on width, and saying so explicitly costs
        // nothing and stops a later widening of that rule from quietly admitting it.
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format == SurfaceFormat::NormalizedByte2 || format == SurfaceFormat::NormalizedByte4)
            return RendererFormatVerdict::Unsupported;
        // plan_vulkan.md VULKAN-172: and the same refusal for the block-compressed formats, for a
        // sharper version of the same reason. The framework's width rule reads Dxt1's texel size
        // as 8 and Dxt3/Dxt5's as 16 -- all multiples of four -- and would admit a `Color`-shaped
        // transfer over them. Those bytes are BLOCKS, not texels: a `Color*` upload would write
        // w*h*4 bytes into an image that holds ceil(w/4)*ceil(h/4)*8. Refuse rather than let a
        // width coincidence decide. **This diverges from EasyGL**, which defers here; the
        // divergence is deliberate and is recorded in plan_vulkan.md VULKAN-172, because EasyGL
        // stores these formats through a decode path where a Color transfer has a meaning and this
        // renderer stores the blocks themselves, where it has none.
        if (format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
            format == SurfaceFormat::Dxt5)
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Defer;
    }

    RendererFormatVerdict VulkanRenderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        // plan_vulkan.md VULKAN-171. Renderability is a strictly narrower question than
        // storability, and this renderer's answer is narrower still than the device's -- which is
        // the point of not reusing VULKAN-170's verdict here.
        //
        // `VulkanRenderTargetRenderer` and `VulkanRenderTargetCubeRenderer` create their colour
        // image in `owner_->swapchainFormat_`, unconditionally: the requested SurfaceFormat reaches
        // neither constructor. So the ONLY format this renderer genuinely renders into is `Color`,
        // and it is answered from the device's real `VkFormatProperties` for that swapchain format
        // rather than asserted -- `COLOR_ATTACHMENT_BIT` plus `SAMPLED_IMAGE_BIT`, because every
        // render target here is also created `USAGE_SAMPLED_BIT` so it can be read back and used
        // as a texture.
        //
        // Everything else is `Defer`, and that is deliberate rather than lazy. The nine formats
        // VULKAN-170's table now claims as TEXTURE storage -- the packed 16-bit trio, the
        // signed-normalized pair, the three block-compressed ones -- must NOT leak into this
        // answer: nothing renders into a BCn surface at all, and the other five would be silently
        // substituted for the swapchain format, which is exactly the "asked for one format, got
        // another" MOD-115 forbids. `Vulkan_SurfaceFormatClassification`'s render-target sweep is
        // what stops that leak happening by accident later.
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        if (static_cast<SurfaceFormat>(surfaceFormat) != SurfaceFormat::Color)
            return RendererFormatVerdict::Defer;
        if (physicalDevice_ == VK_NULL_HANDLE || swapchainFormat_ == VK_FORMAT_UNDEFINED)
            return RendererFormatVerdict::Defer;
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, swapchainFormat_, &props);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        return (props.optimalTilingFeatures & required) == required
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    bool VulkanRenderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        // plan_vulkan.md VULKAN-172: true for exactly what the storage table claims as blocks on
        // THIS device, so the transfer route a caller takes and the storage it lands in are one
        // answer rather than two that could drift. A device without textureCompressionBC claims
        // nothing here, and Texture2D then refuses the format at construction rather than
        // accepting blocks it has nowhere to put.
        VulkanSurfaceFormatStorageEXT storage{};
        return MapSurfaceFormatToStorageEXT(surfaceFormat, storage) && storage.blockExtent > 1;
    }

    bool VulkanRenderer::SupportsRenderTargetSurfaceFormatEXT(int surfaceFormatOrdinal) const
    {
        // Mirrors GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT exactly, including the
        // Defer arm's framework rule (Texture::ValidateFormat admits Color alone). It is repeated
        // rather than called because GraphicsDevice is above this layer; if that reduction ever
        // changes, this must change with it -- which is why the two float capability cases above
        // route through here instead of hard-coding an answer.
        switch (ClassifyRenderTargetFormatEXT(surfaceFormatOrdinal))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:   return true;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:  return false;
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:        break;
        }
        return surfaceFormatOrdinal == static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    }

    VulkanRenderer::~VulkanRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surfaceInfo_.windowId);
        if (device_ == VK_NULL_HANDLE) {
            surface_ = VK_NULL_HANDLE;
            platformSurface_.reset();
            if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr);                   instance_ = VK_NULL_HANDLE; }
            return;
        }

        // Step 1: wait for all in-flight GPU work to complete.
        DeviceWaitIdleEXT();

        // Step 1b: destroy every MRT framebuffer before releasing the render-target views it
        // borrows. The device is idle, so both current and frame-retired proxies are safe now.
        //
        // VULKAN-405: resetting the renderer's own two handles was not enough, and the object
        // tracker said so -- `VkFramebuffer … has not been destroyed` at vkDestroyDevice. A proxy
        // is reference-counted, and `currentRT_` holds a share for as long as the MRT is bound,
        // as do Pending3DDraw::rt / PendingBatch::rt / PendingClear::rt for every record the
        // frame never replayed. A process that binds an MRT and exits without presenting leaves
        // one of those holding the last share; it is destroyed with the renderer's members, which
        // happens AFTER vkDestroyDevice below, and ~VulkanMRTProxy then finds device_ already
        // null and returns without freeing anything. Worse, it reads `owner_->device_` out of a
        // VulkanRenderer whose destructor has already finished.
        //
        // Both halves are settled the way every other owned resource here settles them, and
        // without having to enumerate who holds a share: release the framebuffer explicitly while
        // the device is alive, then disconnect the owner so a surviving proxy never dereferences
        // it. `mrtProxy_` and `retiredMrtProxies_` together are every proxy that exists -- a proxy
        // reaches the second list precisely when it leaves the first.
        auto releaseProxy = [](VulkanMRTProxy* proxy) {
            if (proxy == nullptr) return;
            proxy->ReleaseVulkanResources();
            proxy->DisconnectOwner();
        };
        releaseProxy(mrtProxy_.get());
        for (auto& retired : retiredMrtProxies_) releaseProxy(retired.second.get());
        mrtProxy_.reset();
        retiredMrtProxies_.clear();

        // Step 2: destroy buffers and memory.
        // Externally-owned render targets, vertex/index buffers (C++ objects may outlive this destructor).
        for (auto* rt : liveRenderTargets_) { rt->ReleaseVulkanResources(); rt->DisconnectOwner(); }
        liveRenderTargets_.clear();
        for (auto* vb : liveVertexBuffers_) { vb->ReleaseVulkanResources(); vb->DisconnectOwner(); }
        liveVertexBuffers_.clear();
        for (auto* ib : liveIndexBuffers_)  { ib->ReleaseVulkanResources(); ib->DisconnectOwner(); }
        liveIndexBuffers_.clear();
        // Renderer-owned sprite buffers + per-frame 3D buffers.
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (spriteVB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, spriteVB_[i], nullptr);    spriteVB_[i]    = VK_NULL_HANDLE; }
            if (spriteVBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, spriteVBMem_[i], nullptr);    spriteVBMem_[i] = VK_NULL_HANDLE; }
            if (spriteIB_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, spriteIB_[i], nullptr);    spriteIB_[i]    = VK_NULL_HANDLE; }
            if (spriteIBMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, spriteIBMem_[i], nullptr);    spriteIBMem_[i] = VK_NULL_HANDLE; }
            if (frame3DVB_[i]       != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DVB_[i], nullptr);        frame3DVB_[i]       = VK_NULL_HANDLE; }
            if (frame3DVBMem_[i]    != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DVBMem_[i], nullptr);        frame3DVBMem_[i]    = VK_NULL_HANDLE; }
            if (frame3DIB_[i]       != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DIB_[i], nullptr);        frame3DIB_[i]       = VK_NULL_HANDLE; }
            if (frame3DIBMem_[i]    != VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DIBMem_[i], nullptr);        frame3DIBMem_[i]    = VK_NULL_HANDLE; }
            if (frame3DInstVB_[i]   != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame3DInstVB_[i], nullptr);    frame3DInstVB_[i]   = VK_NULL_HANDLE; }
            if (frame3DInstVBMem_[i]!= VK_NULL_HANDLE) { vkFreeMemory(device_, frame3DInstVBMem_[i], nullptr);   frame3DInstVBMem_[i]= VK_NULL_HANDLE; }
        }

        // Step 3: destroy image views, images, and memory.
        // Externally-owned textures.
        for (auto* tex : liveTextures_) { tex->ReleaseVulkanResources(); tex->DisconnectOwner(); }
        liveTextures_.clear();
        // VULKAN-407: the three classes that used to be in no list at all. A Texture3D, TextureCube
        // or RenderTargetCube can outlive its GraphicsDevice -- MetalResourceHealth's own
        // base-move test exists to document exactly that -- and until this loop existed such an
        // object reached its destructor after vkDestroyDevice, found device_ null and returned
        // having freed nothing, after reading a VulkanRenderer whose destructor had finished.
        for (auto* vol  : liveTexture3Ds_)        { vol->ReleaseVulkanResources();  vol->DisconnectOwner(); }
        liveTexture3Ds_.clear();
        for (auto* cube : liveTextureCubes_)      { cube->ReleaseVulkanResources(); cube->DisconnectOwner(); }
        liveTextureCubes_.clear();
        for (auto* rtc  : liveRenderTargetCubes_) { rtc->ReleaseVulkanResources();  rtc->DisconnectOwner(); }
        liveRenderTargetCubes_.clear();
        // REMED-GFX-075: force-free every retirement bucket now (device already idle from Step 1) --
        // including the handles the live-resource ReleaseVulkanResources() calls above just retired,
        // and any retired MRT proxy -- BEFORE descriptorPool_ is destroyed below, since retired
        // descriptor sets are freed from that pool.
        ProcessRetiredResources(true);
        // MSAA color buffer.
        CleanupMsaaColorResources();
        // Depth buffer.
        if (depthImageView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, depthImageView_, nullptr); depthImageView_ = VK_NULL_HANDLE; }
        if (depthImage_     != VK_NULL_HANDLE) { vkDestroyImage(device_, depthImage_, nullptr);         depthImage_     = VK_NULL_HANDLE; }
        if (depthMemory_    != VK_NULL_HANDLE) { vkFreeMemory(device_, depthMemory_, nullptr);           depthMemory_    = VK_NULL_HANDLE; }

        // Step 4: destroy descriptor resources.
        if (descriptorPool_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);           descriptorPool_      = VK_NULL_HANDLE; }
        // VULKAN-390: the chained overflow pools go with it, or their sets outlive their pool.
        for (VkDescriptorPool pool : texSamplerOverflowPools_)
            if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, pool, nullptr);
        texSamplerOverflowPools_.clear();
        if (descriptorSetLayout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr); descriptorSetLayout_ = VK_NULL_HANDLE; }
        texSamplerDescSets_.clear(); // descriptor sets freed with pools above
        for (auto& [k, entry] : samplerCache_)
            if (entry.sampler != VK_NULL_HANDLE) vkDestroySampler(device_, entry.sampler, nullptr);
        samplerCache_.clear();
        if (defaultSampler_      != VK_NULL_HANDLE) { vkDestroySampler(device_, defaultSampler_, nullptr);                  defaultSampler_      = VK_NULL_HANDLE; }

        // Step 5: destroy pipelines, render pass, and framebuffers.
        for (auto& [key, pipe] : pipelines3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelines3D_.clear();
        for (auto& [k, pipe] : pipelinesAlphaTest3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesAlphaTest3D_.clear();
        for (auto& [k, pipe] : pipelinesDualTex3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesDualTex3D_.clear();
        for (auto& cache : dualTexDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (dualTexFogUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, dualTexFogUBO_[i], nullptr);    dualTexFogUBO_[i]    = VK_NULL_HANDLE; }
            if (dualTexFogUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, dualTexFogUBOMem_[i], nullptr);   dualTexFogUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesEnvMap3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesEnvMap3D_.clear();
        for (auto& cache : envMapDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (envMapUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, envMapUBO_[i], nullptr);     envMapUBO_[i]    = VK_NULL_HANDLE; }
            if (envMapUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, envMapUBOMem_[i], nullptr);    envMapUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesLitTextured3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesLitTextured3D_.clear();
        for (auto& [k, pipe] : pipelinesLitTextured3DVertexLit_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesLitTextured3DVertexLit_.clear();
        for (auto& cache : litTexturedDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (litTexturedUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, litTexturedUBO_[i], nullptr);    litTexturedUBO_[i]    = VK_NULL_HANDLE; }
            if (litTexturedUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, litTexturedUBOMem_[i], nullptr);   litTexturedUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesFogColored3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesFogColored3D_.clear();
        for (auto& [k, pipe] : pipelinesFogTex3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesFogTex3D_.clear();
        for (auto& cache : fogTex3DDescSets_) cache.clear(); // freed with pool below
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (fogTex3DUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, fogTex3DUBO_[i], nullptr);    fogTex3DUBO_[i]    = VK_NULL_HANDLE; }
            if (fogTex3DUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, fogTex3DUBOMem_[i], nullptr);   fogTex3DUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& [k, pipe] : pipelinesSkinned3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesSkinned3D_.clear();
        for (auto& [k, pipe] : pipelinesSkinned3DVertexLit_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesSkinned3DVertexLit_.clear();
        for (auto& [k, pipe] : pipelinesInstanced3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesInstanced3D_.clear();
        for (auto& [k, pipe] : pipelinesPbr3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesPbr3D_.clear();
        for (auto& [k, pipe] : pipelinesPbrSkinned3D_)
            if (pipe != VK_NULL_HANDLE) { vkDestroyPipeline(device_, pipe, nullptr); pipe = VK_NULL_HANDLE; }
        pipelinesPbrSkinned3D_.clear();
        for (auto& cache : skinnedDescSets_) cache.clear();
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, skinnedUBO_[i], nullptr);    skinnedUBO_[i]    = VK_NULL_HANDLE; }
            if (skinnedUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, skinnedUBOMem_[i], nullptr);   skinnedUBOMem_[i] = VK_NULL_HANDLE; }
            if (skinnedFogUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, skinnedFogUBO_[i], nullptr);    skinnedFogUBO_[i]    = VK_NULL_HANDLE; }
            if (skinnedFogUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, skinnedFogUBOMem_[i], nullptr);   skinnedFogUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& cache : pbrDescSets_) cache.clear();
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (pbrUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, pbrUBO_[i], nullptr);    pbrUBO_[i]    = VK_NULL_HANDLE; }
            if (pbrUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, pbrUBOMem_[i], nullptr);   pbrUBOMem_[i] = VK_NULL_HANDLE; }
        }
        for (auto& cache : pbrSkinnedDescSets_) cache.clear();
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (pbrSkinnedBoneUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, pbrSkinnedBoneUBO_[i], nullptr);    pbrSkinnedBoneUBO_[i]    = VK_NULL_HANDLE; }
            if (pbrSkinnedBoneUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, pbrSkinnedBoneUBOMem_[i], nullptr);   pbrSkinnedBoneUBOMem_[i] = VK_NULL_HANDLE; }
            if (pbrSkinnedUBO_[i]    != VK_NULL_HANDLE) { vkDestroyBuffer(device_, pbrSkinnedUBO_[i], nullptr);    pbrSkinnedUBO_[i]    = VK_NULL_HANDLE; }
            if (pbrSkinnedUBOMem_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, pbrSkinnedUBOMem_[i], nullptr);   pbrSkinnedUBOMem_[i] = VK_NULL_HANDLE; }
        }
        // Default flat-normal fallback texture (no free of descriptorSet -- it's never bound as
        // its own standalone set, only as one of several samplers in a shared PBR descriptor set).
        if (defaultFlatNormalView_   != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultFlatNormalView_, nullptr);  defaultFlatNormalView_   = VK_NULL_HANDLE; }
        if (defaultFlatNormalImage_  != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultFlatNormalImage_, nullptr);     defaultFlatNormalImage_  = VK_NULL_HANDLE; }
        if (defaultFlatNormalMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultFlatNormalMemory_, nullptr);       defaultFlatNormalMemory_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteCubeView_, nullptr); defaultWhiteCubeView_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeImage_ != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteCubeImage_, nullptr);   defaultWhiteCubeImage_ = VK_NULL_HANDLE; }
        if (defaultWhiteCubeMem_  != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteCubeMem_, nullptr);       defaultWhiteCubeMem_  = VK_NULL_HANDLE; }
        // Default white texture (no free of descriptorSet — will be freed with the pool).
        if (defaultWhiteView_   != VK_NULL_HANDLE) { vkDestroyImageView(device_, defaultWhiteView_, nullptr);  defaultWhiteView_   = VK_NULL_HANDLE; }
        if (defaultWhiteImage_  != VK_NULL_HANDLE) { vkDestroyImage(device_, defaultWhiteImage_, nullptr);     defaultWhiteImage_  = VK_NULL_HANDLE; }
        if (defaultWhiteMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, defaultWhiteMemory_, nullptr);       defaultWhiteMemory_ = VK_NULL_HANDLE; }
        for (auto& [fmt, p] : pipelines2DMsaaByDepthFmt_) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
        pipelines2DMsaaByDepthFmt_.clear();
        for (auto& [fmt, p] : pipelines2DByDepthFmt_) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
        pipelines2DByDepthFmt_.clear();
        if (pipelineLayout3D_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout3D_, nullptr);       pipelineLayout3D_      = VK_NULL_HANDLE; }
        if (pipelineLayoutExt3D_        != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutExt3D_, nullptr);        pipelineLayoutExt3D_        = VK_NULL_HANDLE; }
        if (pipelineLayoutAlphaTest3D_  != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutAlphaTest3D_, nullptr);  pipelineLayoutAlphaTest3D_  = VK_NULL_HANDLE; }
        if (pipelineLayoutDualTex3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutDualTex3D_, nullptr);    pipelineLayoutDualTex3D_    = VK_NULL_HANDLE; }
        if (pipelineLayoutEnvMap3D_     != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutEnvMap3D_, nullptr);     pipelineLayoutEnvMap3D_     = VK_NULL_HANDLE; }
        if (descriptorPool2Tex_         != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPool2Tex_, nullptr);         descriptorPool2Tex_         = VK_NULL_HANDLE; }
        if (descriptorSetLayout2Tex_    != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayout2Tex_, nullptr); descriptorSetLayout2Tex_   = VK_NULL_HANDLE; }
        if (descriptorPoolEnvMap_       != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolEnvMap_, nullptr);       descriptorPoolEnvMap_       = VK_NULL_HANDLE; }
        if (descriptorSetLayoutEnvMap_  != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutEnvMap_, nullptr); descriptorSetLayoutEnvMap_ = VK_NULL_HANDLE; }
        if (pipelineLayoutSkinned3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutSkinned3D_, nullptr);    pipelineLayoutSkinned3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolSkinned_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolSkinned_, nullptr);      descriptorPoolSkinned_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutSkinned_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutSkinned_, nullptr); descriptorSetLayoutSkinned_ = VK_NULL_HANDLE; }
        if (pipelineLayoutPbr3D_        != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutPbr3D_, nullptr);        pipelineLayoutPbr3D_        = VK_NULL_HANDLE; }
        if (descriptorPoolPbr_          != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolPbr_, nullptr);          descriptorPoolPbr_          = VK_NULL_HANDLE; }
        if (descriptorSetLayoutPbr_     != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutPbr_, nullptr); descriptorSetLayoutPbr_     = VK_NULL_HANDLE; }
        if (pipelineLayoutPbrSkinned3D_        != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutPbrSkinned3D_, nullptr);        pipelineLayoutPbrSkinned3D_        = VK_NULL_HANDLE; }
        if (descriptorPoolPbrSkinned_          != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolPbrSkinned_, nullptr);          descriptorPoolPbrSkinned_          = VK_NULL_HANDLE; }
        if (descriptorSetLayoutPbrSkinned_     != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutPbrSkinned_, nullptr); descriptorSetLayoutPbrSkinned_     = VK_NULL_HANDLE; }
        if (pipelineLayoutLitTextured3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutLitTextured3D_, nullptr);    pipelineLayoutLitTextured3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolLitTextured_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolLitTextured_, nullptr);      descriptorPoolLitTextured_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutLitTextured_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutLitTextured_, nullptr); descriptorSetLayoutLitTextured_ = VK_NULL_HANDLE; }
        if (pipelineLayoutFogTex3D_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayoutFogTex3D_, nullptr);    pipelineLayoutFogTex3D_    = VK_NULL_HANDLE; }
        if (descriptorPoolFogTex3D_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPoolFogTex3D_, nullptr);      descriptorPoolFogTex3D_      = VK_NULL_HANDLE; }
        if (descriptorSetLayoutFogTex3D_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutFogTex3D_, nullptr); descriptorSetLayoutFogTex3D_ = VK_NULL_HANDLE; }
        if (pipelineLayout2D_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, pipelineLayout2D_, nullptr);       pipelineLayout2D_      = VK_NULL_HANDLE; }
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-065.
        for (auto& [k, module] : compiledEffectShaderModules_)
            if (module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module, nullptr);
        compiledEffectShaderModules_.clear();
        for (auto& [k, pipe] : compiledEffectPipelines_)
            if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipe, nullptr);
        compiledEffectPipelines_.clear();
        for (auto& [k, layout] : compiledEffectPipelineLayouts_)
            if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, layout, nullptr);
        compiledEffectPipelineLayouts_.clear();
        for (auto& [k, layout] : compiledEffectSamplerSetLayouts_)
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        compiledEffectSamplerSetLayouts_.clear();
        for (auto& cache : compiledEffectSamplerSets_) cache.clear(); // freed with the pool
        if (compiledEffectDescriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, compiledEffectDescriptorPool_, nullptr);
            compiledEffectDescriptorPool_ = VK_NULL_HANDLE;
        }
        if (compiledEffectUniformSetLayoutVS_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, compiledEffectUniformSetLayoutVS_, nullptr);
            compiledEffectUniformSetLayoutVS_ = VK_NULL_HANDLE;
        }
        if (compiledEffectUniformSetLayoutPS_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, compiledEffectUniformSetLayoutPS_, nullptr);
            compiledEffectUniformSetLayoutPS_ = VK_NULL_HANDLE;
        }
        if (compiledEffectEmptySetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, compiledEffectEmptySetLayout_, nullptr);
            compiledEffectEmptySetLayout_ = VK_NULL_HANDLE;
        }
        for (auto& chunks : compiledEffectUBOChunks_) {
            for (auto& chunk : chunks) {
                if (chunk.vsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, chunk.vsBuffer, nullptr);
                if (chunk.vsMemory != VK_NULL_HANDLE) vkFreeMemory(device_, chunk.vsMemory, nullptr);
                if (chunk.psBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, chunk.psBuffer, nullptr);
                if (chunk.psMemory != VK_NULL_HANDLE) vkFreeMemory(device_, chunk.psMemory, nullptr);
            }
            chunks.clear();
        }
#endif
        for (auto fb : swapchainFramebuffers_)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        if (renderPassMsaa_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPassMsaa_, nullptr); renderPassMsaa_ = VK_NULL_HANDLE; }
        // Task 911: RT render passes are now depth-format-keyed caches, not single members.
        for (auto& [fmt, rp] : rtRenderPassByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassByDepthFmt_.clear();
        for (auto& [fmt, rp] : rtRenderPassLoadByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassLoadByDepthFmt_.clear();
        for (auto& [fmt, rp] : rtRenderPassMsaaByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaByDepthFmt_.clear();
        // REMED-GFX-141: the MSAA load variant has the same lifetime as the MSAA clear variant.
        for (auto& [fmt, rp] : rtRenderPassMsaaLoadByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaLoadByDepthFmt_.clear();
        // REMED-GFX-143: the per-load/store swapchain variants share renderPass_'s lifetime.
        DestroySwapchainPassVariants();
        if (renderPass_       != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPass_,       nullptr); renderPass_       = VK_NULL_HANDLE; }
        for (auto& [n, rp] : mrtRenderPasses_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        mrtRenderPasses_.clear();

        // Sync objects (semaphores and fences).
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (i < (int)imageAvailableSemaphores_.size() && imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
                imageAvailableSemaphores_[i] = VK_NULL_HANDLE;
            }
            if (i < (int)renderFinishedSemaphores_.size() && renderFinishedSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
                renderFinishedSemaphores_[i] = VK_NULL_HANDLE;
            }
            if (i < (int)inFlightFences_.size() && inFlightFences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, inFlightFences_[i], nullptr);
                inFlightFences_[i] = VK_NULL_HANDLE;
            }
        }

        // Deferred readback staging buffer.
        if (readbackStagingBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, readbackStagingBuf_, nullptr); readbackStagingBuf_ = VK_NULL_HANDLE; }
        if (readbackStagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, readbackStagingMem_, nullptr);    readbackStagingMem_ = VK_NULL_HANDLE; }

        // Step 6: destroy command pool (before swapchain).
        if (commandPool_ != VK_NULL_HANDLE) { vkDestroyCommandPool(device_, commandPool_, nullptr); commandPool_ = VK_NULL_HANDLE; }

        // Step 7: destroy swapchain resources.
        for (auto iv : swapchainImageViews_)
            if (iv != VK_NULL_HANDLE) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_ != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }

        // Step 8: destroy device last among Vulkan device-level objects.
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;

        if (sEnableValidation && debugMessenger_ != VK_NULL_HANDLE) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn) { fn(instance_, debugMessenger_, nullptr); debugMessenger_ = VK_NULL_HANDLE; }
        }
        surface_ = VK_NULL_HANDLE;
        platformSurface_.reset();
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr);                   instance_ = VK_NULL_HANDLE; }
    }

    // =========================================================================
    // Instance / device / surface init
    // =========================================================================

    void VulkanRenderer::CreateInstance()
    {
        if (sEnableValidation) {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, nullptr);
            std::vector<VkLayerProperties> layers(n);
            vkEnumerateInstanceLayerProperties(&n, layers.data());
            for (const char* name : kValidationLayers) {
                bool found = false;
                for (const auto& l : layers) if (std::strcmp(l.layerName, name) == 0) { found = true; break; }
                if (!found) {
                    std::cerr << "[Vulkan] Validation layer '" << name
                              << "' not available - running without validation\n";
                    sEnableValidation = false;
                    break;
                }
            }
        }

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "CNA";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "CNA";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_1;

        const std::vector<std::string> platformExtensions =
            platformSurfaceService_->GetInstanceExtensions();
        std::vector<const char*> exts;
        exts.reserve(platformExtensions.size() + 3);
        for (const std::string& extension : platformExtensions)
            exts.push_back(extension.c_str());
        if (sEnableValidation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        // REMED-GFX-144: VK_EXT_validation_features is provided by the Khronos layer itself, so it
        // is requested only when that layer is really going in.
        const bool wantSyncValidation = sEnableValidation && sRequestSyncValidation;
        if (wantSyncValidation) exts.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

        // The layer caps repeats of one message id at ten by default, which would make a reported
        // hazard COUNT meaningless. VK_EXT_layer_settings lifts the cap in process; it is queried
        // from the layer rather than assumed, because requesting an absent instance extension makes
        // vkCreateInstance fail outright.
        bool haveLayerSettings = false;
        if (wantSyncValidation) {
            uint32_t en = 0;
            vkEnumerateInstanceExtensionProperties(kValidationLayers[0], &en, nullptr);
            std::vector<VkExtensionProperties> layerExts(en);
            vkEnumerateInstanceExtensionProperties(kValidationLayers[0], &en, layerExts.data());
            for (const auto& e : layerExts)
                if (std::strcmp(e.extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME) == 0) {
                    haveLayerSettings = true;
                    break;
                }
            if (haveLayerSettings) exts.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        }

        const VkValidationFeatureEnableEXT syncFeature[] = {
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
        };
        VkValidationFeaturesEXT vf{};
        vf.sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        vf.enabledValidationFeatureCount = static_cast<uint32_t>(std::size(syncFeature));
        vf.pEnabledValidationFeatures    = syncFeature;

        const VkBool32 kFalse = VK_FALSE;
        const VkLayerSettingEXT settings[] = {
            { kValidationLayers[0], "enable_message_limit",
              VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &kFalse },
        };
        VkLayerSettingsCreateInfoEXT lsci{};
        lsci.sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
        lsci.pNext        = &vf;
        lsci.settingCount = static_cast<uint32_t>(std::size(settings));
        lsci.pSettings    = settings;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        if (wantSyncValidation)
            ci.pNext = haveLayerSettings ? static_cast<const void*>(&lsci)
                                         : static_cast<const void*>(&vf);
        if (sEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            ci.ppEnabledLayerNames = kValidationLayers;
        }
        if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
    }

    void VulkanRenderer::SetSyncValidationEnabledEXT(bool enabled) noexcept
    {
        sRequestSyncValidation = enabled;
    }

    bool VulkanRenderer::IsValidationActiveEXT() noexcept
    {
        return sEnableValidation;
    }

    int VulkanRenderer::GetDistinctAcquiredImageCountEXT() const noexcept
    {
        return std::popcount(acquiredImageMaskEXT_);
    }

    int VulkanRenderer::GetUsedFrameSlotCountEXT() const noexcept
    {
        return std::popcount(usedFrameSlotMaskEXT_);
    }

    void VulkanRenderer::SetupDebugMessenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = DebugCallback;
        info.pUserData = this;
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (!fn || fn(instance_, &info, nullptr, &debugMessenger_) != VK_SUCCESS)
            std::cerr << "[Vulkan] Warning: could not set up validation debug messenger\n";
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* d, void* userData)
    {
        if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            auto* renderer = static_cast<VulkanRenderer*>(userData);
            if (renderer != nullptr && d != nullptr && d->pMessage != nullptr) {
                renderer->validationMessages_.emplace_back(d->pMessage);
                // REMED-GFX-144: pMessageIdName carries the layer's stable message name
                // ("SYNC-HAZARD-WRITE-AFTER-READ", "VUID-..."), which pMessage does not. Recording
                // it lets a regression classify by message CLASS instead of matching a diagnostic
                // sentence the layer is free to reword.
                renderer->validationMessageIdNames_.emplace_back(
                    d->pMessageIdName != nullptr ? d->pMessageIdName : "");
            }
            std::cerr << "[Vulkan Validation] "
                      << (d != nullptr && d->pMessage != nullptr ? d->pMessage : "") << '\n';
        }
        return VK_FALSE;
    }

    void VulkanRenderer::CreateSurface()
    {
        platformSurface_ = std::make_unique<PlatformVulkanSurfaceOwner>(
            *platformSurfaceService_, ToPlatformInstance(instance_), surfaceInfo_.windowId);
        surface_ = FromPlatformSurface<VkSurfaceKHR>(platformSurface_->Get());
    }

    void VulkanRenderer::PickPhysicalDevice()
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance_, &n, nullptr);
        if (n == 0) throw std::runtime_error("Vulkan: no GPU found");
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance_, &n, devs.data());

        for (auto dev : devs) {
            uint32_t ec = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &ec, nullptr);
            std::vector<VkExtensionProperties> exts(ec);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &ec, exts.data());
            bool hasSwap = false;
            for (const auto& e : exts) if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { hasSwap = true; break; }
            if (!hasSwap) continue;

            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qps(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qn, qps.data());
            std::optional<uint32_t> gfx, pres;
            for (uint32_t i = 0; i < qn; ++i) {
                if (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfx = i;
                VkBool32 ps = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &ps);
                if (ps) pres = i;
                if (gfx && pres) break;
            }
            if (!gfx || !pres) continue;

            uint32_t fc = 0, mc = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fc, nullptr);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &mc, nullptr);
            if (fc == 0 || mc == 0) continue;

            physicalDevice_      = dev;
            graphicsQueueFamily_ = *gfx;
            presentQueueFamily_  = *pres;
            break;
        }
        if (physicalDevice_ == VK_NULL_HANDLE)
            throw std::runtime_error("Vulkan: no suitable GPU");

        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(physicalDevice_, &p);
        // plan_vulkan.md VULKAN-020: SupportsCapability answers several members from the device's
        // own limits, and it must answer for the device this loop actually selected -- which is not
        // necessarily GPU 0. Cached here, next to the selection, rather than re-queried per call.
        deviceLimits_ = p.limits;
        // VULKAN-151: can this device bind Byte4 bone indices to the shaders' `vec4` input?
        // VK_FORMAT_R8G8B8A8_USCALED is not a mandatory vertex-buffer format, so ask rather than
        // assume. Measured YES on both drivers here (spikes/vulkan-vertex-format-spike/).
        {
            VkFormatProperties uscaled{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_R8G8B8A8_USCALED,
                                                &uscaled);
            uscaledVertexFormatSupported_ =
                (uscaled.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0;
        }
        // VULKAN-097: the pixel-centre correction must stay strictly BELOW half a pixel. A device
        // whose rasterizer cannot represent 63/128 rounds it back up to exactly half, which puts
        // XNA's 1x1 triangles on an excluded fill edge again -- the trap EasyGL hit on WebGL's four
        // subpixel bits. Vulkan guarantees at least 4 bits; the upper guard mirrors EasyGL's.
        const uint32_t subPixelBits = p.limits.subPixelPrecisionBits;
        if (subPixelBits > 1 && subPixelBits < 24) {
            const float representableBelowHalf =
                1.0f - std::ldexp(1.0f, 1 - static_cast<int>(subPixelBits));
            xnaPixelCenterScale_ = std::min(xnaPixelCenterScale_, representableBelowHalf);
        }
        std::clog << "[Vulkan] GPU: " << p.deviceName << '\n';
    }

    void VulkanRenderer::CreateLogicalDevice()
    {
        std::set<uint32_t> families = { graphicsQueueFamily_, presentQueueFamily_ };
        float prio = 1.f;
        std::vector<VkDeviceQueueCreateInfo> qis;
        for (uint32_t f : families) {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = f;
            qi.queueCount = 1;
            qi.pQueuePriorities = &prio;
            qis.push_back(qi);
        }
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
        VkPhysicalDeviceFeatures feat{};
        if (supported.fillModeNonSolid) {
            feat.fillModeNonSolid     = VK_TRUE;
            fillModeNonSolidSupported_ = true;
        }
        if (supported.samplerAnisotropy) {
            feat.samplerAnisotropy = VK_TRUE;
            anisotropySupported_   = true;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &props);
            maxSamplerAnisotropy_ = props.limits.maxSamplerAnisotropy;
        }
        if (supported.independentBlend) {
            feat.independentBlend = VK_TRUE;
            independentBlendSupported_ = true;
        }
        // VULKAN-370: without this feature AND VK_QUERY_CONTROL_PRECISE_BIT at vkCmdBeginQuery, an
        // occlusion query is only required to answer "some samples passed", not how many. XNA's
        // OcclusionQuery.PixelCount is a real tally, so ask for the real thing where the device
        // offers it and report the truth through PixelCountIsPreciseEXT() where it does not.
        if (supported.occlusionQueryPrecise) {
            feat.occlusionQueryPrecise = VK_TRUE;
            occlusionQueryPreciseSupported_ = true;
        }
        // plan_vulkan.md VULKAN-172: BCn formats may not be used at all unless this feature is
        // ENABLED, not merely reported -- an image created in VK_FORMAT_BC1_RGBA_UNORM_BLOCK on a
        // device where it was left false is invalid however encouraging VkFormatProperties looks.
        if (supported.textureCompressionBC) {
            feat.textureCompressionBC = VK_TRUE;
            textureCompressionBCSupported_ = true;
        }
        // plan_vulkan.md VULKAN-179: SurfaceFormat::Bgra4444 is D3DFMT_A4R4G4B4, whose exact
        // Vulkan spelling VK_FORMAT_A4R4G4B4_UNORM_PACK16 came with VK_EXT_4444_formats and is core
        // only in 1.3, while this renderer's instance asks for 1.1. Enabling the extension where
        // the device offers it is the narrow route: it leaves the instance version, the device
        // selection and every other renderer path untouched, and a device without the extension
        // simply keeps refusing the format by name rather than being handed a different layout.
        //
        // Both halves are required and are checked separately -- the extension must be present AND
        // its formatA4R4G4B4 feature must be reported, because an extension that is advertised but
        // whose feature is false permits nothing.
        std::vector<const char*> enabledDeviceExtensions(std::begin(kDeviceExtensions),
                                                         std::end(kDeviceExtensions));
        VkPhysicalDevice4444FormatsFeaturesEXT formats4444{};
        formats4444.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT;
        {
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> available(extCount);
            if (extCount > 0)
                vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount,
                                                     available.data());
            bool has4444 = false;
            for (const auto& e : available)
                if (std::strcmp(e.extensionName, VK_EXT_4444_FORMATS_EXTENSION_NAME) == 0)
                { has4444 = true; break; }
            if (has4444)
            {
                VkPhysicalDeviceFeatures2 probe{};
                probe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                probe.pNext = &formats4444;
                vkGetPhysicalDeviceFeatures2(physicalDevice_, &probe);
                if (formats4444.formatA4R4G4B4 == VK_TRUE)
                {
                    formats4444.formatA4B4G4R4 = VK_FALSE;   // not a format CNA names
                    enabledDeviceExtensions.push_back(VK_EXT_4444_FORMATS_EXTENSION_NAME);
                    formatA4R4G4B4Supported_ = true;
                }
            }
        }

        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = static_cast<uint32_t>(qis.size());
        ci.pQueueCreateInfos = qis.data();
        ci.pEnabledFeatures = &feat;
        if (formatA4R4G4B4Supported_) ci.pNext = &formats4444;
        ci.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtensions.size());
        ci.ppEnabledExtensionNames = enabledDeviceExtensions.data();
        if (sEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            ci.ppEnabledLayerNames = kValidationLayers;
        }
        if (vkCreateDevice(physicalDevice_, &ci, nullptr, &device_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed");
        vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, presentQueueFamily_,  0, &presentQueue_);
        pfnCmdInsertDebugLabel_ = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(device_, "vkCmdInsertDebugUtilsLabelEXT"));

        // Task 456: one-time startup capability dump. This renderer previously had NO startup log
        // at all (unlike several sibling renderers, which print something at initialization) --
        // a real, previously-undocumented gap on its own.
        {
            VkPhysicalDeviceProperties devProps{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &devProps);
            const int maxMsaa = SampleCountToInt(PickSampleCount(physicalDevice_, 64));
            // plan_vulkan.md VULKAN-402: std::clog, not std::cout. A startup diagnostic on stdout
            // is what GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout exists to
            // forbid -- stdout belongs to the program's output, and a host that pipes it gets this
            // line mixed into its data. This was the only std::cout in the renderer; every other
            // diagnostic here already used std::clog.
            std::clog << "CNA: Vulkan capabilities -- device=" << devProps.deviceName
                      << "; MSAA up to " << maxMsaa
                      << "x; MRT up to 4 targets (FNA MAX_RENDERTARGET_BINDINGS); "
                         "anisotropic filtering: "
                      << (anisotropySupported_
                              ? ("supported, max " + std::to_string(static_cast<int>(maxSamplerAnisotropy_)) + "x")
                              : std::string("NOT supported on this device"))
                      << "; wireframe fill mode: " << (fillModeNonSolidSupported_ ? "supported" : "NOT supported")
                      << "; independent MRT blend/write state: "
                      << (independentBlendSupported_ ? "supported" : "NOT supported")
                      << "; SurfaceFormat: Color only (Task 176)" << std::endl;
        }
    }

    // =========================================================================
    // Swapchain
    // =========================================================================

    void VulkanRenderer::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

        uint32_t fn = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fn, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fn);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fn, fmts.data());
        // FNA's SurfaceFormat.Color (the default backbuffer format) is linear, not sRGB — a
        // separate SurfaceFormat.ColorSrgbEXT exists for the gamma-encoded variant. An SRGB
        // swapchain format would apply an automatic linear-to-sRGB encode to every presented
        // pixel regardless of content, which XNA/FNA does not do by default. Prefer UNORM.
        VkSurfaceFormatKHR fmt = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { fmt = f; break; }

        uint32_t mn = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, nullptr);
        std::vector<VkPresentModeKHR> modes(mn);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &mn, modes.data());
        // Choose present mode based on swapInterval_:
        //   0 (Immediate) → prefer IMMEDIATE, fall back to MAILBOX, then FIFO
        //   2 (Two)       → prefer FIFO_RELAXED, fall back to FIFO
        //   1 (Default/One) → FIFO (guaranteed VSync, always available)
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (swapInterval_ == 0)
        {
            for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) { mode = m; break; }
            if (mode == VK_PRESENT_MODE_FIFO_KHR)
                for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { mode = m; break; }
        }
        else if (swapInterval_ == 2)
        {
            for (auto m : modes) if (m == VK_PRESENT_MODE_FIFO_RELAXED_KHR) { mode = m; break; }
        }
        // VULKAN-332: what the device actually gave, which is not always what was asked for, and
        // whether an unsynchronised mode was on offer at all -- FIFO is the only guaranteed one.
        appliedPresentMode_ = mode;
        unsynchronisedPresentModeAvailable_ = false;
        for (auto m : modes)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR || m == VK_PRESENT_MODE_MAILBOX_KHR)
                unsynchronisedPresentModeAvailable_ = true;

        VkExtent2D ext;
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            ext = caps.currentExtent;
        } else {
            const int w = surfaceInfo_.drawableSize.width;
            const int h = surfaceInfo_.drawableSize.height;
            ext.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
            ext.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface_;
        ci.minImageCount = imgCount;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        uint32_t qfi[] = { graphicsQueueFamily_, presentQueueFamily_ };
        if (graphicsQueueFamily_ != presentQueueFamily_) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = qfi;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = mode;
        ci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");

        swapchainFormat_ = fmt.format;
        swapchainExtent_ = ext;
    }

    void VulkanRenderer::CreateImageViews()
    {
        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
        swapchainImages_.resize(n);
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapchainImages_.data());
        swapchainImageViews_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo ci{};
            ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image    = swapchainImages_[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format   = swapchainFormat_;
            ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(device_, &ci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateImageView failed");
        }
    }

    void VulkanRenderer::CreateRenderPass()
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format  = swapchainFormat_;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAtt{};
        depthAtt.format  = depthFormat_;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pDepthStencilAttachment = &depthRef;

        // Use the same two subpass dependencies as rtRenderPass_ so that pipelines
        // created against renderPass_ are also compatible with rtRenderPass_.
        VkSubpassDependency renderPassDeps[2]{};
        // Entry: wait for previous shader reads (RT-as-texture) AND the previous frame's
        // depth-buffer writes before this frame clears/tests depth. The depth image is
        // shared across frames, so without the depth scope the loadOp CLEAR can race a
        // prior frame's depth writes, intermittently corrupting depth-tested draws.
        renderPassDeps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[0].dstSubpass      = 0;
        // REMED-GFX-144: COLOR_ATTACHMENT_OUTPUT leads this mask, and on the swapchain passes it is
        // load-bearing. vkCmdBeginRenderPass performs the attachment's automatic initialLayout
        // transition, which is a WRITE, and that transition is ordered only by THIS dependency: it
        // happens-after the availability operations of the first synchronization scope (srcStageMask)
        // and happens-before the visibility operations of the second (dstStageMask). SubmitFrame
        // waits the vkAcquireNextImageKHR semaphore with pWaitDstStageMask =
        // COLOR_ATTACHMENT_OUTPUT, so the wait blocks COLOR_ATTACHMENT_OUTPUT and every later stage
        // -- and nothing earlier. Without COLOR_ATTACHMENT_OUTPUT here, every stage in this
        // dependency's src scope (FRAGMENT_SHADER, EARLY/LATE_FRAGMENT_TESTS) precedes the waited
        // stage while dstStageMask still demands the transition complete before EARLY_FRAGMENT_TESTS,
        // so the transition was free to run BEFORE the acquire semaphore signalled -- writing an
        // image the presentation engine had not yet released. Khronos synchronization validation
        // reported it as one WRITE_AFTER_READ per acquire, vkCmdBeginRenderPass versus
        // vkAcquireNextImageKHR, prior_access SYNC_PRESENT_ENGINE_SYNCVAL_PRESENT_ACQUIRE_READ_SYNCVAL.
        // Widening a dependency's FIRST scope only adds ordering, so the same bit is added at all six
        // render-pass creation sites, keeping the byte-identical masks Task 905 requires; measured,
        // the three swapchain sites are the ones that clear the hazard.
        renderPassDeps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        renderPassDeps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        renderPassDeps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // REMED-GFX-143: COLOR_ATTACHMENT_READ was missing here and at every other render pass in
        // this renderer. A VK_ATTACHMENT_LOAD_OP_LOAD colour attachment IS a colour-attachment read,
        // and without the bit the entry dependency does not cover it, so synchronization validation
        // reports "attachment loadOp access is not synchronized with the attachment layout
        // transition" for every loading pass. The backbuffer's own LOAD variants (see
        // GetOrCreateSwapchainRenderPass) would otherwise multiply that pre-existing class rather
        // than merely inherit it, and the bit is added at ALL SIX sites so the byte-identical
        // dependency masks Task 905 requires for cross-pass pipeline compatibility stay identical.
        renderPassDeps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        renderPassDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes (and the finalLayout=PRESENT_SRC transition) visible to
        // subsequent fragment-shader reads (RT-as-texture) AND transfer reads (the deferred
        // GetBackBufferData copy reads the swapchain image at the TRANSFER stage).
        renderPassDeps[1].srcSubpass      = 0;
        renderPassDeps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        renderPassDeps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        renderPassDeps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            VK_PIPELINE_STAGE_TRANSFER_BIT;
        renderPassDeps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassDeps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        renderPassDeps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses   = &sub;
        ci.dependencyCount = 2; ci.pDependencies = renderPassDeps;
        if (vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
    }

    // REMED-GFX-143: see SwapchainPassKey in the header. The all-clear/no-store combination IS
    // renderPass_/renderPassMsaa_, returned as-is so a frame with a single backbuffer cycle creates
    // no variant and records byte-for-byte the pass it always did. Everything else differs from
    // those only in load/store ops and initial layouts -- which render-pass compatibility ignores
    // -- so every existing pipeline and every existing per-image VkFramebuffer stays valid.
    VkRenderPass VulkanRenderer::GetOrCreateSwapchainRenderPass(const SwapchainPassKey& key)
    {
        if (!key.loadColor && !key.loadDepth && !key.loadStencil && !key.storeForNext)
            return key.msaa ? renderPassMsaa_ : renderPass_;

        const uint32_t bits = key.Bits();
        auto it = swapchainPassVariants_.find(bits);
        if (it != swapchainPassVariants_.end()) return it->second;

        const VkAttachmentLoadOp colorLoad =
            key.loadColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        const VkAttachmentLoadOp depthLoad =
            key.loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        const VkAttachmentLoadOp stencilLoad =
            key.loadStencil ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        // A following backbuffer segment loads whatever this one leaves behind; the last segment of
        // the frame keeps the pre-fix DONT_CARE for everything the presentation does not read.
        const VkAttachmentStoreOp handOver =
            key.storeForNext ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFormat_;
        depthAtt.samples        = key.msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp         = depthLoad;
        depthAtt.storeOp        = handOver;
        depthAtt.stencilLoadOp  = stencilLoad;
        depthAtt.stencilStoreOp = handOver;
        // A LOAD of either aspect needs the layout the previous backbuffer segment's finalLayout
        // left the shared depth image in; a full clear can keep UNDEFINED, as the base pass does.
        depthAtt.initialLayout  = (key.loadDepth || key.loadStencil)
                                      ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                      : VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Byte-identical to CreateRenderPass()/CreateRenderPassMsaa() and to every RT pass. Task
        // 905 established -- from live validation errors -- that this renderer's render passes must
        // keep IDENTICAL subpass dependencies for pipelines created against one to be usable in
        // another, so a variant must not narrow or widen them. The extra ordering a second entry
        // into the same image needs (its load reading what the previous entry's colour/depth writes
        // produced) is issued as an explicit vkCmdPipelineBarrier between the two passes in
        // RecordCommandBuffer instead.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        // REMED-GFX-144: see CreateRenderPass(). COLOR_ATTACHMENT_OUTPUT must lead this mask so that
        // vkCmdBeginRenderPass's automatic initialLayout transition is ordered after the acquire
        // semaphore, which SubmitFrame waits at exactly that stage. Identical at all six sites.
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPass rp = VK_NULL_HANDLE;
        if (key.msaa)
        {
            // att0 multisampled colour, att1 the swapchain resolve target, att2 multisampled depth.
            // The resolve is deliberately kept on EVERY backbuffer segment rather than only the
            // last: dropping pResolveAttachments would change the subpass shape and break
            // render-pass compatibility with every pipeline already created against
            // renderPassMsaa_. att0 is LOADed/STOREd across segments instead, so the final resolve
            // carries every segment's work and an intermediate resolve is redundant, not wrong.
            VkAttachmentDescription colorAtt{};
            colorAtt.format         = swapchainFormat_;
            colorAtt.samples        = sampleCount_;
            colorAtt.loadOp         = colorLoad;
            colorAtt.storeOp        = handOver;
            colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAtt.initialLayout  = key.loadColor ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                    : VK_IMAGE_LAYOUT_UNDEFINED;
            colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentDescription resolveAtt{};
            resolveAtt.format         = swapchainFormat_;
            resolveAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
            resolveAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            resolveAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // The resolve rewrites the whole render area, which is always the full extent here, so
            // the previous segment's resolved content never needs loading.
            resolveAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            resolveAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef  { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference resolveRef{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference depthRef  { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription sub{};
            sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount    = 1;
            sub.pColorAttachments       = &colorRef;
            sub.pResolveAttachments     = &resolveRef;
            sub.pDepthStencilAttachment = &depthRef;

            VkAttachmentDescription atts[] = { colorAtt, resolveAtt, depthAtt };
            VkRenderPassCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = 3; ci.pAttachments  = atts;
            ci.subpassCount    = 1; ci.pSubpasses    = &sub;
            ci.dependencyCount = 2; ci.pDependencies = deps;
            if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
                throw std::runtime_error("vkCreateRenderPass (swapchain MSAA variant) failed");
        }
        else
        {
            VkAttachmentDescription colorAtt{};
            colorAtt.format         = swapchainFormat_;
            colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAtt.loadOp         = colorLoad;
            colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // A LOAD reads the image the previous backbuffer segment left in PRESENT_SRC_KHR (that
            // pass's finalLayout); the render pass transitions it to COLOR_ATTACHMENT_OPTIMAL for
            // the subpass and back on exit, so the image is presentable after every segment.
            colorAtt.initialLayout  = key.loadColor ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                    : VK_IMAGE_LAYOUT_UNDEFINED;
            colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription sub{};
            sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount    = 1;
            sub.pColorAttachments       = &colorRef;
            sub.pDepthStencilAttachment = &depthRef;

            VkAttachmentDescription atts[] = { colorAtt, depthAtt };
            VkRenderPassCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = 2; ci.pAttachments  = atts;
            ci.subpassCount    = 1; ci.pSubpasses    = &sub;
            ci.dependencyCount = 2; ci.pDependencies = deps;
            if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
                throw std::runtime_error("vkCreateRenderPass (swapchain variant) failed");
        }
        swapchainPassVariants_[bits] = rp;
        return rp;
    }

    void VulkanRenderer::DestroySwapchainPassVariants()
    {
        for (auto& entry : swapchainPassVariants_)
            if (entry.second != VK_NULL_HANDLE)
                vkDestroyRenderPass(device_, entry.second, nullptr);
        swapchainPassVariants_.clear();
    }

    VkRenderPass VulkanRenderer::GetOrCreateRTRenderPass(VkFormat depthFmt, bool discardContents)
    {
        auto& cache = discardContents ? rtRenderPassByDepthFmt_ : rtRenderPassLoadByDepthFmt_;
        auto it = cache.find(depthFmt);
        if (it != cache.end()) return it->second;

        const bool hasDepth = (depthFmt != VK_FORMAT_UNDEFINED);

        // Same as renderPass_ but color finalLayout = SHADER_READ_ONLY_OPTIMAL.
        // This makes the two passes compatible so pipelines can be reused across them.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // PreserveContents (LOAD_OP_LOAD): initialLayout = SHADER_READ_ONLY_OPTIMAL matches the
        // image state after construction (explicit transition) and after any previous RT render
        // pass (finalLayout = SHADER_READ_ONLY_OPTIMAL).
        colorAtt.initialLayout  = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // REMED-GFX-142: depth and stencil follow the colour attachment's rule instead of being
        // thrown away. This used to be DONT_CARE/DONT_CARE in the PRESERVING variant on the
        // reasoning that "its previous content is never needed across RT passes" -- but FNA3D
        // documents `preserveTargetContents` as storing the "color/depth/stencil" contents, and
        // FNA's own GL and D3D11 drivers preserve all three because an FBO attachment and a DSV
        // simply persist. DONT_CARE makes the contents UNDEFINED, so a depth-tested draw into a
        // rebound PreserveContents target read whatever the driver happened to leave; llvmpipe
        // retains it, which is why this looked correct here and would not on a tiler.
        // STORE is what makes the next cycle's LOAD meaningful. The discarding variant keeps
        // CLEAR/DONT_CARE exactly as before -- it is cleared again on every bind anyway.
        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFmt;
        depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp        = discardContents ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                  : VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp  = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.stencilStoreOp = discardContents ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                  : VK_ATTACHMENT_STORE_OP_STORE;
        // LOAD_OP_LOAD needs a defined layout, which UNDEFINED is not: the owning target
        // transitions its depth image once at construction (see VulkanRenderTargetRenderer's and
        // VulkanRenderTargetCubeRenderer's init barrier) so the FIRST preserving bind is legal, and
        // every later one finds the finalLayout this pass itself leaves behind.
        depthAtt.initialLayout  = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED
                                                  : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // Entry: wait for the previous frame's texture sample AND the previous frame's
        // depth-buffer writes (this RT's own depth image) before this frame clears/tests depth.
        // Task 905: this must match renderPass_'s (CreateRenderPass()) dependencies exactly --
        // not just a "wait for shader reads" subset -- because GetOrCreatePipeline3D() (and
        // every other GetOrCreatePipelineXxx3D) may reuse a pipeline created against renderPass_
        // to draw into this render pass whenever their formats happen to coincide. Render-pass
        // "compatibility" (VUID-vkCmdDraw-renderPass-02684) requires matching subpass dependency
        // stage/access masks too, not just attachment descriptions/subpass shape -- confirmed via
        // live Vulkan validation errors from a depth-tested 3D draw into a non-MSAA RT before the
        // original Task 905 fix (see plans/plan_graphics.md). The depth-related stage/access bits are
        // harmlessly over-broad (safe over-synchronization) for the hasDepth=false variant, kept
        // identical across both for exact byte-for-byte parity with every other render pass in
        // this renderer rather than risk a subtly-incompatible narrower mask.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        // REMED-GFX-144: see CreateRenderPass(). COLOR_ATTACHMENT_OUTPUT must lead this mask so that
        // vkCmdBeginRenderPass's automatic initialLayout transition is ordered after the acquire
        // semaphore, which SubmitFrame waits at exactly that stage. Identical at all six sites.
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: make color writes visible to the fragment shader AND transfer reads (the
        // deferred GetBackBufferData copy path) in the next pass.
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = hasDepth ? 2u : 1u; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (RT) failed");
        cache[depthFmt] = rp;
        return rp;
    }

    VkRenderPass VulkanRenderer::GetOrCreateRTRenderPassMsaa(VkFormat depthFmt,
                                                                    bool discardContents)
    {
        // REMED-GFX-141: two caches, exactly like GetOrCreateRTRenderPass's own clear/load pair.
        auto& cache = discardContents ? rtRenderPassMsaaByDepthFmt_ : rtRenderPassMsaaLoadByDepthFmt_;
        auto it = cache.find(depthFmt);
        if (it != cache.end()) return it->second;

        const bool hasDepth = (depthFmt != VK_FORMAT_UNDEFINED);

        // Task 878/879/911: 3-attachment MSAA RT render pass, shared by every MSAA-enabled
        // RenderTarget2D/RenderTargetCube requesting this same real depth format (or none).
        // Same attachment formats/sample-counts/subpass shape AND (see the deps[] comment below)
        // the same subpass dependency stage/access masks as CreateRenderPassMsaa()'s backbuffer
        // pass, so pipelines already created against renderPassMsaa_ remain render-pass-compatible
        // here whenever depthFmt happens to equal depthFormat_ -- but with the resolve
        // attachment's finalLayout = SHADER_READ_ONLY_OPTIMAL instead of PRESENT_SRC_KHR, since
        // this resolves into an RT's sampleable colorImage_, never presented (attachment
        // initial/final layouts don't affect pipeline compatibility).
        //
        // REMED-GFX-141: this used to be DiscardContents-shaped ONLY (LOAD_OP_CLEAR + STORE_OP_
        // DONT_CARE), so a multisampled PreserveContents target had no way to keep its own samples
        // at all -- every bind cleared the multisample attachment and every pass end threw it away,
        // and the face came back as the frame's clear colour. The load variant loads and stores the
        // multisample colour attachment and leaves it in COLOR_ATTACHMENT_OPTIMAL, which is exactly
        // the layout the previous pass's finalLayout already produced, so back-to-back cycles of one
        // target need no extra barrier. The FIRST bind still finds undefined samples: the owning
        // target transitions the image UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL once at construction so
        // the layout is legal, and the shared layer's discard clear (or the target's own first full
        // draw) is what makes the CONTENT defined -- the same "a brand-new preserving target has no
        // previous content" rule the non-MSAA load variant has always had.
        //
        // REMED-GFX-142 closed that: depth/stencil now follows the same discardContents rule the
        // colour attachment does, on this multisampled leg too. Multisampled depth is never
        // resolved -- FNA allocates one multisampled depth renderbuffer and nothing reads it back
        // -- so preserving it means the MULTISAMPLE depth attachment itself has to survive the
        // bind cycle, which is exactly what LOAD/STORE buys and what CLEAR/DONT_CARE made
        // impossible. No depth resolve was added to fake it.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = sampleCount_;
        colorAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                  : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp        = discardContents ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                  : VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED
                                                  : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription resolveAtt{};
        resolveAtt.format         = swapchainFormat_;
        resolveAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolveAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // REMED-GFX-142: identical rule to the single-sample variant's depth attachment.
        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFmt;
        depthAtt.samples        = sampleCount_;
        depthAtt.loadOp         = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp        = discardContents ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                  : VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp  = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.stencilStoreOp = discardContents ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                  : VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.initialLayout  = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED
                                                  : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef   { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference resolveRef { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef   { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pResolveAttachments     = &resolveRef;
        sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // Task 878/879 empirical finding: Vulkan's validation layer treats a pipeline's
        // originally-bound render pass and the render pass it's actually recorded against as
        // "compatible" only if their subpass dependency stage/access masks match too, not just
        // attachment descriptions/subpass shape as the basic spec text on render-pass
        // compatibility implies (confirmed via VUID-vkCmdDraw-renderPass-02684 validation errors
        // when an earlier, narrower deps[] didn't exactly match renderPassMsaa_'s). Since
        // pipelines with msaa=true may be created against renderPassMsaa_ (see
        // GetOrCreatePipeline3D et al.) and reused here whenever depthFmt coincides, this render
        // pass's dependencies must exactly mirror CreateRenderPassMsaa()'s, including the
        // transfer-read scope that (semantically) only matters for the backbuffer's
        // GetBackBufferData path. Kept identical for hasDepth=false too, for the same
        // safe-over-synchronization reasoning as GetOrCreateRTRenderPass()'s comment.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        // REMED-GFX-144: see CreateRenderPass(). COLOR_ATTACHMENT_OUTPUT must lead this mask so that
        // vkCmdBeginRenderPass's automatic initialLayout transition is ordered after the acquire
        // semaphore, which SubmitFrame waits at exactly that stage. Identical at all six sites.
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, resolveAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = hasDepth ? 3u : 2u; ci.pAttachments = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (RT MSAA) failed");
        cache[depthFmt] = rp;
        return rp;
    }

    VkRenderPass VulkanRenderer::GetOrCreateMRTRenderPass(
        uint32_t colorAttachmentCount, VkSampleCountFlagBits sampleCount,
        VkFormat depthFormat)
    {
        const bool msaa = sampleCount > VK_SAMPLE_COUNT_1_BIT;
        const bool hasDepth = depthFormat != VK_FORMAT_UNDEFINED;
        const uint64_t key = static_cast<uint64_t>(colorAttachmentCount)
            | (static_cast<uint64_t>(sampleCount) << 8)
            | (static_cast<uint64_t>(static_cast<uint32_t>(depthFormat)) << 16);
        auto it = mrtRenderPasses_.find(key);
        if (it != mrtRenderPasses_.end()) return it->second;

        // REMED-GFX-095:
        //   MSAA [source0..sourceN-1, resolve0..resolveN-1, optional depth]
        //   1x   [color0..colorN-1, optional depth]
        // Parallel vectors keep colorRefs[i] and resolveRefs[i] paired explicitly.
        const uint32_t resolveBase = colorAttachmentCount;
        const uint32_t depthIndex = msaa
            ? colorAttachmentCount * 2
            : colorAttachmentCount;
        const uint32_t attachmentCount = depthIndex + (hasDepth ? 1u : 0u);
        std::vector<VkAttachmentDescription> atts(attachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            auto& color = atts[i];
            color.format         = swapchainFormat_;
            color.samples        = sampleCount;
            color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp        = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                        : VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout    = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (msaa) {
                auto& resolve = atts[resolveBase + i];
                resolve.format         = swapchainFormat_;
                resolve.samples        = VK_SAMPLE_COUNT_1_BIT;
                resolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                resolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                resolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                resolve.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        if (hasDepth) {
            auto& depth = atts[depthIndex];
            depth.format         = depthFormat;
            depth.samples        = sampleCount;
            depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        std::vector<VkAttachmentReference> colorRefs(colorAttachmentCount);
        std::vector<VkAttachmentReference> resolveRefs;
        if (msaa) resolveRefs.resize(colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            colorRefs[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            if (msaa)
                resolveRefs[i] = { resolveBase + i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        }
        VkAttachmentReference depthRef{
            depthIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = colorAttachmentCount;
        sub.pColorAttachments       = colorRefs.data();
        sub.pResolveAttachments     = msaa ? resolveRefs.data() : nullptr;
        sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        // REMED-GFX-144: see CreateRenderPass(). COLOR_ATTACHMENT_OUTPUT must lead this mask so that
        // vkCmdBeginRenderPass's automatic initialLayout transition is ordered after the acquire
        // semaphore, which SubmitFrame waits at exactly that stage. Identical at all six sites.
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = attachmentCount;
        ci.pAttachments    = atts.data();
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        ci.dependencyCount = 2;
        ci.pDependencies   = deps;

        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &ci, nullptr, &rp) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (MRT) failed");
        mrtRenderPasses_[key] = rp;
        return rp;
    }

    // Shared render-pass selection for every 2D/custom/3D pipeline. MRT is keyed by color count,
    // sample count, and binding 0's real depth format. Single-target draws reuse the compatible
    // backbuffer pass or fall back to a depth-format-keyed RT pass.
    VkRenderPass VulkanRenderer::PickRTPipelineRenderPass(uint32_t colorAttachmentCount, bool msaa,
                                                                  VkFormat targetDepthFmt)
    {
        if (colorAttachmentCount > 1)
            return GetOrCreateMRTRenderPass(
                colorAttachmentCount,
                msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT,
                targetDepthFmt);
        if (targetDepthFmt == depthFormat_)
            return (msaa && renderPassMsaa_) ? renderPassMsaa_ : renderPass_;
        // Pipelines only ever need a REFERENCE render pass, and REMED-GFX-141's load variant is
        // render-pass-compatible with the clear one (they differ only in loadOp/storeOp/
        // initialLayout, none of which participates in compatibility), so this keeps asking for the
        // clear variant on both legs -- no pipeline cache key changed.
        return msaa ? GetOrCreateRTRenderPassMsaa(targetDepthFmt, true)
                    : GetOrCreateRTRenderPass(targetDepthFmt, true);
    }

    void VulkanRenderer::CreateFramebuffers()
    {
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        const bool msaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT);
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            VkFramebufferCreateInfo ci{};
            // Keep the attachment storage alive through vkCreateFramebuffer().  Declaring a
            // separate `atts` array inside either branch leaves ci.pAttachments dangling as soon
            // as that branch ends; optimized builds happened to retain the stack bytes often
            // enough to hide the UB, while validation/lavapipe observed null and stack-address
            // pseudo-handles here.
            VkImageView atts[3] = {};
            ci.sType  = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.width  = swapchainExtent_.width;
            ci.height = swapchainExtent_.height;
            ci.layers = 1;
            if (msaa) {
                // att 0 = MSAA color, att 1 = resolve (swapchain), att 2 = MSAA depth
                atts[0]            = msaaColorView_;
                atts[1]            = swapchainImageViews_[i];
                atts[2]            = depthImageView_;
                ci.renderPass      = renderPassMsaa_;
                ci.attachmentCount = 3;
                ci.pAttachments    = atts;
            } else {
                atts[0]            = swapchainImageViews_[i];
                atts[1]            = depthImageView_;
                ci.renderPass      = renderPass_;
                ci.attachmentCount = 2;
                ci.pAttachments    = atts;
            }
            if (vkCreateFramebuffer(device_, &ci, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    void VulkanRenderer::CleanupSwapchain()
    {
        if (device_ == VK_NULL_HANDLE) return;
        for (auto fb : swapchainFramebuffers_) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();
        CleanupMsaaColorResources();
        for (auto iv : swapchainImageViews_) if (iv) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
        // NOTE: renderPass_/renderPassMsaa_ are NOT destroyed here; permanent for renderer lifetime

        // Defensive: the swapchain (and therefore its real pixel extent) is about to be torn
        // down and rebuilt at a possibly-different size by RecreateSwapchain() -- a
        // previously game-set custom Viewport should not be trusted to still be meaningful
        // relative to the new extent until GraphicsDevice::UpdateViewportFromWindow() (which
        // runs on every Present()/Reset()/SetVirtualResolution() call, i.e. immediately after
        // every path that reaches this point) re-confirms or replaces it. Currently a no-op in
        // practice, since that re-confirmation already always happens on the very next frame
        // regardless -- kept as a belt-and-braces guard against a future code path that might
        // call CleanupSwapchain()/RecreateSwapchain() without an immediate follow-up refresh.
        viewportSet_ = false;
    }

    void VulkanRenderer::SetSwapInterval(int interval)
    {
        // VULKAN-332. Recorded first, so GetSwapIntervalEXT() answers the request even where the
        // rebuild cannot happen (a zero-sized surface, or no device yet) -- REMED-GFX-243's whole
        // point is separating "CNA never asked" from "the driver declined".
        if (interval == swapInterval_) return;
        swapInterval_ = interval;
        if (device_ == VK_NULL_HANDLE || !initialized_) return;
        // CreateSwapchain reads swapInterval_ for its present mode, and this renderer already
        // rebuilds the whole swapchain on every resize, so applying the change is the existing
        // path rather than a new one.
        RecreateSwapchain();
    }

    void VulkanRenderer::RecreateSwapchain()
    {
        const int w = surfaceInfo_.drawableSize.width;
        const int h = surfaceInfo_.drawableSize.height;
        if (w == 0 || h == 0) return;
        DeviceWaitIdleEXT();
        CleanupSwapchain();
        CleanupDepthResources();
        CreateSwapchain();
        CreateImageViews();
        CreateDepthResources();
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) CreateMsaaColorResources();
        CreateFramebuffers();
        // REMED-GFX-144: the new swapchain may hold a different number of images, and an index that
        // was never reachable before can now be acquired, so the observed-index record starts over
        // with it. The frame-slot sync objects are deliberately NOT recreated: they are indexed by
        // frame slot, never by image, so their count is fixed for the renderer's lifetime.
        ++swapchainRecreateCountEXT_;
        acquiredImageMaskEXT_ = 0;
        // VULKAN-404: the cached backbuffer readback describes the swapchain that has just been
        // destroyed -- its content is the old frame and its staging buffer is sized for the old
        // extent. Leaving the cache marked valid let ReadBackbuffer take the no-submit path and
        // map the NEW extent's byte count out of the OLD extent's allocation.
        readbackStagingValid_ = false;
    }

    // =========================================================================
    // Command pool / buffers / sync
    // =========================================================================

    void VulkanRenderer::CreateCommandPool()
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = graphicsQueueFamily_;
        if (vkCreateCommandPool(device_, &ci, nullptr, &commandPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed");
    }

    void VulkanRenderer::AllocateCommandBuffers()
    {
        commandBuffers_.resize(MaxFramesInFlight);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MaxFramesInFlight;
        if (vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    void VulkanRenderer::CreateSyncObjects()
    {
        imageAvailableSemaphores_.resize(MaxFramesInFlight);
        renderFinishedSemaphores_.resize(MaxFramesInFlight);
        inFlightFences_.resize(MaxFramesInFlight);
        VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MaxFramesInFlight; ++i)
            if (vkCreateSemaphore(device_, &si, nullptr, &imageAvailableSemaphores_[i]) ||
                vkCreateSemaphore(device_, &si, nullptr, &renderFinishedSemaphores_[i]) ||
                vkCreateFence(device_, &fi, nullptr, &inFlightFences_[i]))
                throw std::runtime_error("Vulkan: failed to create sync objects");
    }

    // =========================================================================
    // Sampler / Descriptor layout / Descriptor pool
    // =========================================================================

    void VulkanRenderer::CreateSampler()
    {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = VK_FILTER_LINEAR;
        ci.minFilter    = VK_FILTER_LINEAR;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.borderColor  = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        // Task 878: VkSamplerCreateInfo{} zero-inits maxLod to 0, which clamps every sample to
        // mip level 0 regardless of mipmapMode -- harmless for the single-level images this
        // sampler was exclusively used with until now, but would silently defeat real mip
        // chains (RenderTarget2D mipMap=true) once they exist. The actual visible range is
        // still bounded by each resource's own VkImageView levelCount, so this is a no-op for
        // every pre-existing single-level texture/RT.
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(device_, &ci, nullptr, &defaultSampler_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSampler failed");
        for (auto& s : slotSamplers_) s = defaultSampler_;
    }

    void VulkanRenderer::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &b;
        if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &descriptorSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    void VulkanRenderer::SetDescriptorAllocationFailuresForTestEXT(
        std::uint32_t count, std::uint32_t skipFirst) noexcept
    {
        sDescriptorAllocFailuresToInject = count;
        sDescriptorAllocSkipsBeforeInjection = skipFirst;
    }

    void VulkanRenderer::SetSamplerCreationFailuresForTestEXT(std::uint32_t count) noexcept
    {
        sSamplerCreationFailuresToInject = count;
    }

    void VulkanRenderer::SetSwapchainOutOfDateForTestEXT(std::uint32_t count) noexcept
    {
        sSwapchainOutOfDateToInject = count;
    }

    void VulkanRenderer::SetDepthFormatPreferredUnsupportedForTestEXT(bool unsupported) noexcept
    {
        sDepthFormatPreferredUnsupportedForTest = unsupported;
    }

    void VulkanRenderer::SetSurfaceFormatUnsupportedForTestEXT(int surfaceFormatOrdinal) noexcept
    {
        sSurfaceFormatUnsupportedForTest = surfaceFormatOrdinal;
    }

    void VulkanRenderer::CreateDescriptorPool()
    {
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDescriptorSets };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = MaxDescriptorSets;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    // =========================================================================
    // Per-slot SamplerState (Task 118)
    // =========================================================================

    void VulkanRenderer::ApplySamplerState(int slot, int filter,
                                                   int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (slot < 0 || slot >= 16) return;
        // plans/plan_fx.md FX-092: the slot's state is a function of THIS call's arguments and nothing
        // else. GraphicsDevice::applySamplerStatesToRenderer runs before every draw and follows
        // this call with ApplySamplerMipState and ApplySamplerAddressW, so a device-driven
        // application overwrites the three defaults below with the real SamplerState values
        // immediately. Only a caller that applies filter and addressing alone -- SpriteBatch's own
        // flush -- keeps them, which is exactly the XNA default it means. Without this, a compiled
        // Effect that clamped slot 0 to mip 1 left every later stock sprite sampling mip 1 too.
        //
        // The W axis follows addressU because every XNA 4.0 SamplerState preset (PointClamp,
        // LinearWrap, AnisotropicClamp, ...) sets all three axes to the same mode, and a
        // SpriteBatch's sampler state is always one of those shapes.
        SamplerStateKey& state = samplerSlotState_[slot];
        state.filter = filter;
        state.addressU = addressU;
        state.addressV = addressV;
        state.addressW = addressU;
        state.maxAnisotropy = maxAnisotropy;
        state.maxMipLevel = 0;
        state.lodBias = 0.0f;
        RebuildSlotSamplerEXT(slot);
        VkSamplerTraceEXT("apply.slot       slot=%d filter=%d addrU=%d addrV=%d aniso=%d "
                          "sampler=0x%llx default=0x%llx",
                          slot, filter, addressU, addressV, maxAnisotropy,
                          VkH(slotSamplers_[slot]), VkH(defaultSampler_));
    }

    void VulkanRenderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (slot < 0 || slot >= 16) return;
        samplerSlotState_[slot].maxMipLevel = maxMipLevel;
        samplerSlotState_[slot].lodBias = lodBias;
        RebuildSlotSamplerEXT(slot);
    }

    void VulkanRenderer::ApplySamplerAddressW(int slot, int addressW)
    {
        if (slot < 0 || slot >= 16) return;
        samplerSlotState_[slot].addressW = addressW;
        RebuildSlotSamplerEXT(slot);
    }

    void VulkanRenderer::RebuildSlotSamplerEXT(int slot)
    {
        // VULKAN-161: GetOrCreateSamplerEXT either returns a real sampler or throws, so the old
        // `if (sampler != VK_NULL_HANDLE)` guard is gone. It was not harmless: keeping the previous
        // slot sampler meant the sampler state the game had just assigned silently did not apply.
        slotSamplers_[slot] = GetOrCreateSamplerEXT(samplerSlotState_[slot]);
    }

    VkSampler VulkanRenderer::GetOrCreateSamplerEXT(const SamplerStateKey& key)
    {
        const int filter = key.filter;
        const int addressU = key.addressU;
        const int addressV = key.addressV;
        const int maxAnisotropy = key.maxAnisotropy;
        auto it = samplerCache_.find(key);
        if (it != samplerCache_.end()) {
            it->second.lastUsed = ++samplerUseClock_;   // VULKAN-160: LRU, not arbitrary
            return it->second.sampler;
        }

        // XNA TextureFilter int values:
        //  0=Linear, 1=Point, 2=Anisotropic, 3=LinearMipPoint, 4=PointMipLinear,
        //  5=MinLinearMagPointMipLinear, 6=MinLinearMagPointMipPoint,
        //  7=MinPointMagLinearMipLinear, 8=MinPointMagLinearMipPoint
        VkFilter magF, minF;
        VkSamplerMipmapMode mipMode;
        bool enableAniso = false;
        switch (filter) {
        case 1:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // Point
        case 2:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  enableAniso = true; break; // Anisotropic
        case 3:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // LinearMipPoint
        case 4:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // PointMipLinear
        case 5:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // MinLinearMagPointMipLinear
        case 6:  magF = VK_FILTER_NEAREST; minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // MinLinearMagPointMipPoint
        case 7:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // MinPointMagLinearMipLinear
        case 8:  magF = VK_FILTER_LINEAR;  minF = VK_FILTER_NEAREST; mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // MinPointMagLinearMipPoint
        default: magF = VK_FILTER_LINEAR;  minF = VK_FILTER_LINEAR;  mipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // Linear (0)
        }

        // XNA TextureAddressMode int values: 0=Wrap, 1=Clamp, 2=Mirror
        auto toAddr = [](int a) -> VkSamplerAddressMode {
            switch (a) {
            case 0:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case 2:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; // Clamp (1)
            }
        };

        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = magF;
        ci.minFilter    = minF;
        ci.addressModeU = toAddr(addressU);
        ci.addressModeV = toAddr(addressV);
        ci.addressModeW = toAddr(key.addressW);
        ci.mipmapMode   = mipMode;
        ci.borderColor  = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        // Task 878: see CreateSampler()'s identical comment -- without this, every per-slot
        // sampler variant would silently clamp to mip level 0 too.
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        // plans/plan_fx.md FX-091. XNA's MaxMipLevel names the most DETAILED level the sampler may
        // select, so it is Vulkan's minLod, not its maxLod -- the two names run opposite ways.
        ci.minLod       = static_cast<float>(std::max(0, key.maxMipLevel));
        ci.mipLodBias   = key.lodBias;
        if (enableAniso && anisotropySupported_) {
            ci.anisotropyEnable = VK_TRUE;
            ci.maxAnisotropy    = std::min(static_cast<float>(std::max(1, maxAnisotropy)),
                                           maxSamplerAnisotropy_);
        }

        VkSampler sampler = VK_NULL_HANDLE;
        // VULKAN-161: this used to `return VK_NULL_HANDLE; // caller keeps whatever it already had`,
        // and every one of the three callers turned that null into a resource the game did not ask
        // for -- the descriptor path into `defaultWhiteDescSet_`, the slot setter into the PREVIOUS
        // slot sampler (so the new filtering silently did not take effect), and the compiled-effect
        // path into `defaultSampler_`. That is F-06's shape, one layer below where VULKAN-390 and
        // VULKAN-391 removed it. A device out of samplers now says so.
        const VkResult created = (sSamplerCreationFailuresToInject > 0)
            ? (--sSamplerCreationFailuresToInject, VK_ERROR_TOO_MANY_OBJECTS)
            : vkCreateSampler(device_, &ci, nullptr, &sampler);
        if (created != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: vkCreateSampler failed with " +
                std::to_string(static_cast<int>(created)) + " after " +
                std::to_string(samplerCache_.size()) + " live samplers (this device allows " +
                std::to_string(deviceLimits_.maxSamplerAllocationCount) +
                "). Refused rather than drawing with a substituted sampler.");
        samplerCache_[key] = CachedSamplerEXT{ sampler, ++samplerUseClock_ };
        return sampler;
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateTexSamplerDescSet(VkImageView view,
                                                                         VkSampler sampler)
    {
        // VULKAN-161: these two nulls mean different things, and neither is an allocation failure
        // any more -- `GetOrCreateSamplerEXT` throws now, so a null sampler cannot arrive from a
        // device out of samplers. A null VIEW is an untextured draw, and white is the intended
        // answer. A null SAMPLER means this slot has never been given a sampler state
        // (`slotSamplers_` is zero-initialised and only `ApplySampler*` fills it), which is a
        // renderer-state question rather than a resource one. Kept as one branch because both want
        // the same answer; separated in this comment because a future reader must not read it as
        // "any problem here draws white".
        if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
            return defaultWhiteDescSet_;

        auto key = std::make_pair(view, sampler);
        auto it  = texSamplerDescSets_.find(key);
        if (it != texSamplerDescSets_.end()) {
            VkSamplerTraceEXT("desc.TexSampler  hit=1 key=(0x%llx,0x%llx) set=0x%llx "
                              "binding=0 slot=0 view=0x%llx sampler=0x%llx",
                              VkH(view), VkH(sampler), VkH(it->second.set), VkH(view), VkH(sampler));
            return it->second.set;
        }

        // VULKAN-390: this used to return defaultWhiteDescSet_ when the pool was full, so a game
        // with more than MaxDescriptorSets live (view, sampler) pairs drew WHITE sprites with no
        // exception, no log and no validation message. A wrong picture is the one failure mode a
        // renderer must never choose; the pool is chained instead, and a device that refuses even
        // that says so by name.
        VkDescriptorSet  ds     = VK_NULL_HANDLE;
        VkDescriptorPool dsPool = VK_NULL_HANDLE;
        AllocateTexSamplerDescSetEXT(ds, dsPool);

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = sampler;
        imgInfo.imageView   = view;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = ds;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        texSamplerDescSets_[key] = TexSamplerDescSetEXT{ ds, dsPool };
        return ds;
    }

    void VulkanRenderer::AllocateTexSamplerDescSetEXT(VkDescriptorSet& outSet,
                                                      VkDescriptorPool& outPool)
    {
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorSetCount = 1;
        dsAI.pSetLayouts        = &descriptorSetLayout_;

        // Newest pool first: the older ones are full, which is why the newer ones exist.
        for (auto pool = texSamplerOverflowPools_.rbegin();
             pool != texSamplerOverflowPools_.rend(); ++pool)
        {
            dsAI.descriptorPool = *pool;
            if (AllocateOneDescriptorSet(device_, dsAI, outSet) == VK_SUCCESS) {
                outPool = *pool;
                return;
            }
        }
        dsAI.descriptorPool = descriptorPool_;
        if (AllocateOneDescriptorSet(device_, dsAI, outSet) == VK_SUCCESS) {
            outPool = descriptorPool_;
            return;
        }

        // Every existing pool is full. Chain another of the same size.
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDescriptorSets };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets       = MaxDescriptorSets;
        ci.poolSizeCount = 1;
        ci.pPoolSizes    = &ps;
        VkDescriptorPool grown = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &grown) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: all " + std::to_string(GetTexSamplerDescriptorPoolCountEXT())
                + " combined-image-sampler descriptor pools are full and the device refused "
                  "another. Refused rather than drawing a substituted texture.");

        dsAI.descriptorPool = grown;
        if (AllocateOneDescriptorSet(device_, dsAI, outSet) != VK_SUCCESS) {
            vkDestroyDescriptorPool(device_, grown, nullptr);
            throw std::runtime_error(
                "The Vulkan renderer: a freshly created combined-image-sampler descriptor pool "
                "refused the very first allocation. Refused rather than drawing a substituted "
                "texture.");
        }
        texSamplerOverflowPools_.push_back(grown);
        outPool = grown;
    }

    // =========================================================================
    // VulkanEffectRenderer (Task 119 — SPIR-V custom Effect)
    // =========================================================================

    VulkanEffectRenderer::VulkanEffectRenderer(VulkanRenderer* owner) : owner_(owner) {}

    VulkanEffectRenderer::~VulkanEffectRenderer()
    {
        if (!owner_ || !owner_->device_) return;
        // REMED-GFX-075: a SpriteBatch that used this effect snapshots its VkPipeline/VkPipelineLayout
        // handles at End() (see BatchSnapshot), so a custom Effect disposed before the deferred
        // record must keep those handles valid until the record consumes the batch. Retire them
        // (frame-fence-gated) instead of destroying now; the snapshot never dereferences this wrapper.
        VulkanRenderer::RetiredResources r;
        for (auto& [key, pipeline] : pipelines_)
            if (pipeline != VK_NULL_HANDLE) r.pipelines.push_back(pipeline);
        pipelines_.clear();
        if (pipelineLayout_ != VK_NULL_HANDLE) { r.pipelineLayouts.push_back(pipelineLayout_); pipelineLayout_ = VK_NULL_HANDLE; }
        if (fragModule_     != VK_NULL_HANDLE) { r.shaderModules.push_back(fragModule_);      fragModule_     = VK_NULL_HANDLE; }
        if (vertModule_     != VK_NULL_HANDLE) { r.shaderModules.push_back(vertModule_);      vertModule_     = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
        if (owner_->activeCustomEffect_ == this) owner_->activeCustomEffect_ = nullptr;
    }

    // vertSpv and fragSpv contain raw SPIR-V bytecode (must be 4-byte aligned size).
    // Push-constant contract (128 bytes, vert+frag stages):
    //   [0..7]    = vec2 vpSize  — set automatically by the sprite-batch runtime
    //   [8..15]   = std140 alignment padding
    //   [16..79]  = mat4 uMatrix — SetUniformMat4(any name, ...)
    //   [80..95]  = vec4 uColor  — SetUniformVec4/Vec3/Vec2(any name, ...)
    //   [96..127] = 8 floats     — SetUniformFloat / SetUniformInt (slots 0–7)
    bool VulkanEffectRenderer::CompileProgram(const std::string& vertSpv, const std::string& fragSpv)
    {
        compileError_.clear();
        if (vertSpv.size() % 4 != 0 || fragSpv.size() % 4 != 0) {
            compileError_ = "SPIR-V size must be a multiple of 4 bytes";
            return false;
        }

        VkShaderModuleCreateInfo mci{};
        mci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mci.codeSize = vertSpv.size();
        mci.pCode    = reinterpret_cast<const uint32_t*>(vertSpv.data());
        if (vkCreateShaderModule(owner_->device_, &mci, nullptr, &vertModule_) != VK_SUCCESS) {
            compileError_ = "Failed to create vertex shader module"; return false;
        }
        mci.codeSize = fragSpv.size();
        mci.pCode    = reinterpret_cast<const uint32_t*>(fragSpv.data());
        if (vkCreateShaderModule(owner_->device_, &mci, nullptr, &fragModule_) != VK_SUCCESS) {
            compileError_ = "Failed to create fragment shader module"; return false;
        }

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size   = 128;

        VkPipelineLayoutCreateInfo pli{};
        pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;
        pli.pSetLayouts            = &owner_->descriptorSetLayout_;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges    = &pcRange;
        if (vkCreatePipelineLayout(owner_->device_, &pli, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            compileError_ = "Failed to create pipeline layout"; return false;
        }

        // REMED-GFX-095: a ShaderEffect pipeline is render-pass state, not shader-module
        // state. Defer creation until SpriteBatch.End(), where the actual color count,
        // sample count, depth format, BlendState, write masks, and sample mask are known.
        return true;
    }

    void VulkanEffectRenderer::Bind()
    {
        if (IsValid())
            owner_->activeCustomEffect_ = this;
    }

    void VulkanEffectRenderer::Unbind()
    {
        if (owner_->activeCustomEffect_ == this)
            owner_->activeCustomEffect_ = nullptr;
    }

    bool VulkanEffectRenderer::IsValid() const
    {
        return vertModule_ != VK_NULL_HANDLE
            && fragModule_ != VK_NULL_HANDLE
            && pipelineLayout_ != VK_NULL_HANDLE
            && compileError_.empty();
    }

    std::string VulkanEffectRenderer::GetCompileError() const { return compileError_; }

    void VulkanEffectRenderer::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        // uMatrix at byte offset 16 (GLSL pads vec2 to 16 before mat4): float[4..19]
        std::memcpy(pushConst_ + 4, matrix, 64);
    }

    void VulkanEffectRenderer::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        // uColor at byte offset 80: float[20..23]
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void VulkanEffectRenderer::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void VulkanEffectRenderer::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void VulkanEffectRenderer::SetUniformFloat(const char* /*name*/, float value)
    {
        // uFloat0 at byte offset 96: float[24]
        pushConst_[24] = value;
    }

    // VULKAN-163 (F-31). One refusal for the whole family, because it is one cause: `IEffectRenderer`
    // declares three `Bind*` methods with `{}` bodies, EasyGL overrides all three, and this renderer
    // overrode none -- so every `ShaderEffect::SetTexture` overload was accepted and discarded.
    //
    // The row's own note wondered whether a sibling hole deserved its own row. It does not: the
    // siblings are not separate defects, they are the same missing override, and one fix closes all
    // three. Splitting them would have produced three rows describing one line of code.
    namespace {
        /// plan_vulkan.md VULKAN-265: the array counterpart of RefuseEffectTextureBindEXT.
        /// Named separately because the reason differs -- a texture has no per-unit path here,
        /// an array has no room in a fixed 128-byte push-constant block -- and a caller who
        /// reads only the message should still learn which limit it hit.
        [[noreturn]] void RefuseEffectUniformArrayEXT(const char* setter,
                                                      const char* name,
                                                      int count)
        {
            throw System::NotSupportedException(
                std::string("CNA Vulkan: ShaderEffect::") + setter + "(\"" +
                (name ? name : "<null>") + "\", ..., " + std::to_string(count) +
                ") is not available on this renderer. A ShaderEffect here is driven by a fixed "
                "128-byte push-constant block with fixed slots -- one mat4, one vec4 and eight "
                "floats -- and no shader reflection, so an array of arbitrary length has nowhere "
                "to go. Refused rather than silently ignored.");
        }

        [[noreturn]] void RefuseEffectTextureBindEXT(const char* kind, int unit)
        {
            throw System::NotSupportedException(
                std::string("CNA Vulkan: a ShaderEffect cannot be given a ") + kind +
                " for sampler unit " + std::to_string(unit) + " on this renderer. It supplies a "
                "custom effect's texture from the SpriteBatch draw that uses it; there is no "
                "per-unit binding path. Refused rather than silently ignored.");
        }
    }

    void VulkanEffectRenderer::BindTexture(int unit,
                                           CNA::Internal::Renderers::ITextureRenderer*)
    {
        RefuseEffectTextureBindEXT("Texture2D", unit);
    }

    void VulkanEffectRenderer::BindTextureCube(int unit,
                                               CNA::Internal::Renderers::ITextureCubeRenderer*)
    {
        RefuseEffectTextureBindEXT("TextureCube", unit);
    }

    void VulkanEffectRenderer::BindTexture3D(int unit,
                                             CNA::Internal::Renderers::ITexture3DRenderer*)
    {
        RefuseEffectTextureBindEXT("Texture3D", unit);
    }

    void VulkanEffectRenderer::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    void VulkanEffectRenderer::SetUniformFloatArray(const char* name, const float*, int count)
    {
        RefuseEffectUniformArrayEXT("SetUniformFloatArray", name, count);
    }

    void VulkanEffectRenderer::SetUniformVec2Array(const char* name, const float*, int count)
    {
        RefuseEffectUniformArrayEXT("SetUniformVec2Array", name, count);
    }

    void VulkanEffectRenderer::SetUniformVec3Array(const char* name, const float*, int count)
    {
        RefuseEffectUniformArrayEXT("SetUniformVec3Array", name, count);
    }

    void VulkanEffectRenderer::SetUniformMat4Array(const char* name, const float*, int count)
    {
        RefuseEffectUniformArrayEXT("SetUniformMat4Array", name, count);
    }

    std::unique_ptr<IEffectRenderer> VulkanRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<VulkanEffectRenderer>(this);
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    // =========================================================================
    // Shader module helper
    // =========================================================================

    VkShaderModule VulkanRenderer::CreateShaderModule(const uint32_t* spv, size_t byteSize)
    {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = byteSize;
        ci.pCode    = spv;
        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &ci, nullptr, &mod) != VK_SUCCESS)
            throw std::runtime_error("vkCreateShaderModule failed");
        return mod;
    }

    // =========================================================================
    // 2D Sprite pipeline
    // =========================================================================

    // REMED-GFX-071: forward-declared here because the 2D sprite pipelines (below) reuse the exact
    // same XNA->Vulkan blend translation + cache-key packing the 3D path defines further down.
    static VkBlendFactor ToVkBlendFactor(int xnaBlend);
    static bool UsesBlendConstants(bool blend, const BlendKeyParams& bp)
    {
        if (!blend) return false;
        const auto isConstant = [](VkBlendFactor factor) {
            return factor == VK_BLEND_FACTOR_CONSTANT_COLOR
                || factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR
                || factor == VK_BLEND_FACTOR_CONSTANT_ALPHA
                || factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        };
        // Derive this from the canonical XNA -> Vulkan mapping used by pipeline creation. This
        // single predicate controls both VK_DYNAMIC_STATE_BLEND_CONSTANTS declaration and command
        // replay, so the two sides cannot disagree as new Blend values are mapped.
        return isConstant(ToVkBlendFactor(bp.colorSrc))
            || isConstant(ToVkBlendFactor(bp.colorDst))
            || isConstant(ToVkBlendFactor(bp.alphaSrc))
            || isConstant(ToVkBlendFactor(bp.alphaDst));
    }
    template<std::size_t N>
    static uint32_t AppendBlendConstantsDynamicState(
        VkDynamicState (&states)[N], uint32_t count, bool blend, const BlendKeyParams& bp)
    {
        if (UsesBlendConstants(blend, bp)) {
            assert(count < N);
            states[count++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
        }
        return count;
    }

    static uint32_t PackBlendBits(bool blend, const BlendKeyParams& bp);
    static uint32_t PackColorWriteBits(const BlendKeyParams& bp); // REMED-GFX-077
    static void FillBlendAttachmentState(VkPipelineColorBlendAttachmentState& cba, bool blend,
                                         const BlendKeyParams& bp, int attachmentIndex = 0); // REMED-GFX-077
    static uint64_t FoldDepthFormatIntoKey(uint64_t key, VkFormat depthFmt);

    VkPipeline VulkanRenderer::GetOrCreatePipeline2D(
        VkFormat depthFmt, uint32_t colorAttachmentCount, bool blend,
        const BlendKeyParams& bp)
    {
        // REMED-GFX-071: key by (depth format folded + blend-enable bit, packed blend factor/func
        // enums) -- the same PipelineKey shape the 3D caches use. The BlendFactor *value* is dynamic
        // state (GFX-070) and deliberately absent from the key, so it never fragments the cache.
        const uint64_t countBits =
            (static_cast<uint64_t>(std::max(1u, colorAttachmentCount) - 1u) << 1);
        const PipelineKey key = {
                                  FoldDepthFormatIntoKey((blend ? 1ull : 0ull) | countBits, depthFmt),
                                  PackBlendBits(blend, bp), PackColorWriteBits(bp), NarrowSampleMaskEXT(bp.sampleMask, 1) };
        auto cached = pipelines2DByDepthFmt_.find(key);
        if (cached != pipelines2DByDepthFmt_.end()) return cached->second;

        using namespace Shaders;

        VkShaderModule vert = CreateShaderModule(kSprite2dVertSpv, kSprite2dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kSprite2dFragSpv, kSprite2dFragSpv_size);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        // Sprite2DVertex: x,y | u,v | r,g,b,a (32 bytes)
        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Sprite2DVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r) };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
        VkRect2D sc{ {0,0}, swapchainExtent_ };
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.pViewports = &vp;
        vs.scissorCount  = 1; vs.pScissors  = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (only bit 0 is meaningful at 1 sample). Only set
        // for a non-default mask; pointer valid until vkCreateGraphicsPipelines below.
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(bp.sampleMask, 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        // REMED-GFX-071: colour-attachment blend derived from the batch's BlendState via the same
        // FillBlendAttachmentState the 3D path uses (One canonical XNA->Vulkan mapping for both 2D
        // and 3D), replacing the pre-fix hardcoded SRC_ALPHA/ONE_MINUS_SRC_ALPHA that ignored the
        // BlendState entirely. REMED-GFX-077: the colour write mask (ColorWriteChannels slot 0) is
        // now also derived, inside FillBlendAttachmentState (was hardcoded RGBA).
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
            std::max(1u, colorAttachmentCount));
        for (uint32_t i = 0; i < colorBlendAttachments.size(); ++i)
            FillBlendAttachmentState(colorBlendAttachments[i], blend, bp,
                                     static_cast<int>(i));
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        cbs.pAttachments = colorBlendAttachments.data();

        // Blend factors/functions are static pipeline state. Only the RGBA constant value is
        // dynamic, and only when this pipeline's normalized equation actually consumes it.
        VkDynamicState dynStates[3] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 2, blend, bp);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Push constant: vec2 viewportSize (8 bytes) for NDC conversion
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0; pcRange.size = 8;

        if (pipelineLayout2D_ == VK_NULL_HANDLE) {
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount         = 1;   pli.pSetLayouts         = &descriptorSetLayout_;
            pli.pushConstantRangeCount = 1;   pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout2D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (2D) failed");
        }

        // Depth stencil: disabled for sprites, but required because render pass has depth attachment
        VkPipelineDepthStencilStateCreateInfo ds2d{};
        ds2d.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds2d.depthTestEnable  = VK_FALSE;
        ds2d.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds2d;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout2D_;
        // Task 911: render pass selected per the target's own real depth format.
        pci.renderPass          = PickRTPipelineRenderPass(
            colorAttachmentCount, false, depthFmt);
        pci.subpass             = 0;

        VkPipeline pipe = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (2D) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        pipelines2DByDepthFmt_[key] = pipe;
        return pipe;
    }

    // =========================================================================
    // Depth buffer resources
    // =========================================================================

    // plan_vulkan.md VULKAN-348: the reverse of the two depth pickers, in XNA's vocabulary.
    //
    // XNA has four DepthFormat values and Vulkan offers more shapes than that, so two rows below
    // are lossy on purpose and say which way they round: a 32-bit depth format reports as the
    // nearest XNA name that agrees about the STENCIL plane, because that is the property a caller
    // can act on -- DepthStencilState.StencilEnable either works or it does not. Precision has no
    // XNA enumerator to be precise with.
    static int XnaDepthFormatFromVkFormatEXT(VkFormat fmt) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        switch (fmt) {
            case VK_FORMAT_UNDEFINED:              return static_cast<int>(DepthFormat::None);
            case VK_FORMAT_D16_UNORM:              return static_cast<int>(DepthFormat::Depth16);
            case VK_FORMAT_X8_D24_UNORM_PACK32:    return static_cast<int>(DepthFormat::Depth24);
            case VK_FORMAT_D32_SFLOAT:             return static_cast<int>(DepthFormat::Depth24);
            case VK_FORMAT_D16_UNORM_S8_UINT:      return static_cast<int>(DepthFormat::Depth24Stencil8);
            case VK_FORMAT_D24_UNORM_S8_UINT:      return static_cast<int>(DepthFormat::Depth24Stencil8);
            case VK_FORMAT_D32_SFLOAT_S8_UINT:     return static_cast<int>(DepthFormat::Depth24Stencil8);
            default:                               return static_cast<int>(DepthFormat::None);
        }
    }

    VkFormat VulkanRenderer::FindDepthFormat() const
    {
        // Task 870: stencil-capable formats must be preferred FIRST. Every backbuffer/RT depth
        // image on this renderer shares this one device-wide format (real per-instance
        // DepthStencilFormat fidelity is RenderTarget2D/RenderTargetCube-only, see
        // VulkanRenderTargetRenderer/PickDepthFormat -- Task 911), so if this pick has no stencil
        // aspect, DepthStencilState.StencilEnable can never actually work no matter how correctly
        // ApplyDepthStencilState/the pipeline-creation functions map it. VK_FORMAT_D32_SFLOAT has
        // mandatory support on essentially all real Vulkan hardware and used to be checked first,
        // meaning a stencil-capable format was almost never actually chosen.
        for (VkFormat fmt : { VK_FORMAT_D24_UNORM_S8_UINT,
                              VK_FORMAT_D32_SFLOAT_S8_UINT,
                              VK_FORMAT_D32_SFLOAT }) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return fmt;
        }
        throw std::runtime_error("Vulkan: no suitable depth format");
    }

    void VulkanRenderer::CreateDepthResources()
    {
        depthFormat_ = FindDepthFormat();

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = depthFormat_;
        imgInfo.extent        = { swapchainExtent_.width, swapchainExtent_.height, 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = sampleCount_;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &imgInfo, nullptr, &depthImage_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage (depth) failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, depthImage_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (depth) failed");
        vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = depthFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (depth) failed");
    }

    void VulkanRenderer::CleanupDepthResources()
    {
        if (depthImageView_) { vkDestroyImageView(device_, depthImageView_, nullptr); depthImageView_ = VK_NULL_HANDLE; }
        if (depthImage_)     { vkDestroyImage(device_, depthImage_, nullptr);         depthImage_     = VK_NULL_HANDLE; }
        if (depthMemory_)    { vkFreeMemory(device_, depthMemory_, nullptr);           depthMemory_    = VK_NULL_HANDLE; }
    }

    // =========================================================================
    // MSAA color buffer resources (recreated with swapchain)
    // =========================================================================

    void VulkanRenderer::CreateMsaaColorResources()
    {
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = swapchainFormat_;
        imgInfo.extent        = { swapchainExtent_.width, swapchainExtent_.height, 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = sampleCount_;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &imgInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage (MSAA color) failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, msaaColorImage_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &msaaColorMemory_) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (MSAA color) failed");
        vkBindImageMemory(device_, msaaColorImage_, msaaColorMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = msaaColorImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &msaaColorView_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView (MSAA color) failed");
    }

    void VulkanRenderer::CleanupMsaaColorResources()
    {
        if (msaaColorView_)   { vkDestroyImageView(device_, msaaColorView_, nullptr);   msaaColorView_   = VK_NULL_HANDLE; }
        if (msaaColorImage_)  { vkDestroyImage(device_, msaaColorImage_, nullptr);      msaaColorImage_  = VK_NULL_HANDLE; }
        if (msaaColorMemory_) { vkFreeMemory(device_, msaaColorMemory_, nullptr);       msaaColorMemory_ = VK_NULL_HANDLE; }
    }

    void VulkanRenderer::CreateRenderPassMsaa()
    {
        // att 0: MSAA color (rendered to, not stored — resolved to swapchain)
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFormat_;
        colorAtt.samples        = sampleCount_;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // att 1: resolve target = swapchain image (1 sample, stored, presented)
        VkAttachmentDescription resolveAtt{};
        resolveAtt.format         = swapchainFormat_;
        resolveAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolveAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // att 2: MSAA depth
        VkAttachmentDescription depthAtt{};
        depthAtt.format         = depthFormat_;
        depthAtt.samples        = sampleCount_;
        depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef   { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference resolveRef { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef   { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pResolveAttachments     = &resolveRef;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency deps[2]{};
        // Entry: wait for previous shader reads (RT-as-texture) AND the previous frame's
        // depth-buffer writes (shared depth image) before clearing/testing depth this frame.
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        // REMED-GFX-144: see CreateRenderPass(). COLOR_ATTACHMENT_OUTPUT must lead this mask so that
        // vkCmdBeginRenderPass's automatic initialLayout transition is ordered after the acquire
        // semaphore, which SubmitFrame waits at exactly that stage. Identical at all six sites.
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Exit: synchronize the resolve write + finalLayout=PRESENT_SRC transition against
        // both fragment-shader reads (RT-as-texture) and transfer reads (deferred readback copy).
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkAttachmentDescription atts[] = { colorAtt, resolveAtt, depthAtt };
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 3; ci.pAttachments  = atts;
        ci.subpassCount    = 1; ci.pSubpasses    = &sub;
        ci.dependencyCount = 2; ci.pDependencies = deps;
        if (vkCreateRenderPass(device_, &ci, nullptr, &renderPassMsaa_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass (MSAA) failed");
    }

    VkPipeline VulkanRenderer::GetOrCreatePipeline2DMsaa(
        VkFormat depthFmt, uint32_t colorAttachmentCount, bool blend,
        const BlendKeyParams& bp)
    {
        // REMED-GFX-071: BlendState-keyed, same as the non-MSAA variant (separate map, so no MSAA
        // bit is needed in the key).
        const uint64_t countBits =
            (static_cast<uint64_t>(std::max(1u, colorAttachmentCount) - 1u) << 1);
        const PipelineKey key = {
                                  FoldDepthFormatIntoKey((blend ? 1ull : 0ull) | countBits, depthFmt),
                                  PackBlendBits(blend, bp), PackColorWriteBits(bp), NarrowSampleMaskEXT(bp.sampleMask, SampleCountToInt(sampleCount_)) };
        auto cached = pipelines2DMsaaByDepthFmt_.find(key);
        if (cached != pipelines2DMsaaByDepthFmt_.end()) return cached->second;

        using namespace Shaders;

        VkShaderModule vert = CreateShaderModule(kSprite2dVertSpv, kSprite2dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kSprite2dFragSpv, kSprite2dFragSpv_size);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag; stages[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Sprite2DVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Sprite2DVertex, u) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r) };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
        VkRect2D   sc{ {0,0}, swapchainExtent_ };
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount  = 1; vs.pViewports = &vp;
        vs.scissorCount   = 1; vs.pScissors  = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = sampleCount_;
        // REMED-GFX-077: BlendState.MultiSampleMask on the MSAA sprite pipeline. Only set for a
        // non-default mask; pointer valid until vkCreateGraphicsPipelines below.
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, SampleCountToInt(sampleCount_));
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(bp.sampleMask, SampleCountToInt(sampleCount_));
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        // REMED-GFX-071: BlendState-derived colour-attachment blend (see the non-MSAA variant).
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
            std::max(1u, colorAttachmentCount));
        for (uint32_t i = 0; i < colorBlendAttachments.size(); ++i)
            FillBlendAttachmentState(colorBlendAttachments[i], blend, bp,
                                     static_cast<int>(i));
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        cbs.pAttachments = colorBlendAttachments.data();

        VkDynamicState dynStates[3] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 2, blend, bp);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        if (pipelineLayout2D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{};
            pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pcRange.offset = 0; pcRange.size = 8;
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount         = 1;   pli.pSetLayouts         = &descriptorSetLayout_;
            pli.pushConstantRangeCount = 1;   pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout2D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (2D) failed");
        }

        VkPipelineDepthStencilStateCreateInfo ds2d{};
        ds2d.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds2d.depthTestEnable  = VK_FALSE;
        ds2d.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds2d;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout2D_;
        // Task 911: render pass selected per the target's own real depth format.
        pci.renderPass          = PickRTPipelineRenderPass(
            colorAttachmentCount, true, depthFmt);
        pci.subpass             = 0;

        VkPipeline pipe = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (2D MSAA) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        pipelines2DMsaaByDepthFmt_[key] = pipe;
        return pipe;
    }

    // =========================================================================
    // 3D pipeline layout + per-variant pipeline (lazily created)
    // =========================================================================

    // XNA CompareFunction enum -> VkCompareOp (mirrors EasyGL's ToEasyGLCompareFunc exactly):
    // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7
    static VkCompareOp ToVkCompareOp(int xnaCompare)
    {
        switch (xnaCompare) {
        case 1: return VK_COMPARE_OP_NEVER;
        case 2: return VK_COMPARE_OP_LESS;
        case 3: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 4: return VK_COMPARE_OP_EQUAL;
        case 5: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 6: return VK_COMPARE_OP_GREATER;
        case 7: return VK_COMPARE_OP_NOT_EQUAL;
        default: return VK_COMPARE_OP_ALWAYS; // CompareFunction::Always = 0
        }
    }

    // XNA StencilOperation ordinals -> VkStencilOp (mirrors EasyGL's ToEasyGLStencilOp exactly):
    // Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4, IncrementSaturation=5,
    // DecrementSaturation=6, Invert=7
    static VkStencilOp ToVkStencilOp(int xnaOp)
    {
        switch (xnaOp) {
        case 1: return VK_STENCIL_OP_ZERO;
        case 2: return VK_STENCIL_OP_REPLACE;
        case 3: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 4: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case 5: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 6: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 7: return VK_STENCIL_OP_INVERT;
        default: return VK_STENCIL_OP_KEEP; // StencilOperation::Keep = 0
        }
    }

    // Task 868: XNA Blend enum -> VkBlendFactor (mirrors EasyGL's ToEasyGLBlendFactor exactly):
    // One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5,
    // DestinationColor=6, InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9,
    // BlendFactor=10, InverseBlendFactor=11, SourceAlphaSaturation=12
    static VkBlendFactor ToVkBlendFactor(int xnaBlend)
    {
        switch (xnaBlend) {
        case  1: return VK_BLEND_FACTOR_ZERO;
        case  2: return VK_BLEND_FACTOR_SRC_COLOR;
        case  3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case  4: return VK_BLEND_FACTOR_SRC_ALPHA;
        case  5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case  6: return VK_BLEND_FACTOR_DST_COLOR;
        case  7: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case  8: return VK_BLEND_FACTOR_DST_ALPHA;
        case  9: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 10: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 11: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 12: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE; // Blend::One = 0
        }
    }

    // Task 868: XNA BlendFunction enum -> VkBlendOp (mirrors EasyGL's ToEasyGLBlendEquation
    // exactly): Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4
    static VkBlendOp ToVkBlendOp(int xnaBlendFunc)
    {
        switch (xnaBlendFunc) {
        case 1: return VK_BLEND_OP_SUBTRACT;
        case 2: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 3: return VK_BLEND_OP_MAX;
        case 4: return VK_BLEND_OP_MIN;
        default: return VK_BLEND_OP_ADD; // BlendFunction::Add = 0
        }
    }

    // Task 868: packs BlendKeyParams into a uint32_t (4 bits per Blend value -- 13 real values fit;
    // 3 bits per BlendFunction value -- 5 real values fit), 22 bits total. This is the pipeline
    // cache key's own second half (see PipelineKey/PipelineKeyHash) -- a plain uint64_t ran out of
    // free bit-width for this once a depth VkFormat is already folded in via
    // FoldDepthFormatIntoKey. When blend is disabled, always collapses to 0 regardless of the
    // requested (irrelevant) factors, so different disabled BlendStates don't create duplicate
    // pipelines.
    static uint32_t PackBlendBits(bool blend, const BlendKeyParams& bp)
    {
        if (!blend) return 0;
        return (static_cast<uint32_t>(bp.colorSrc  & 0xF))
             | (static_cast<uint32_t>(bp.colorDst  & 0xF) << 4)
             | (static_cast<uint32_t>(bp.alphaSrc  & 0xF) << 8)
             | (static_cast<uint32_t>(bp.alphaDst  & 0xF) << 12)
             | (static_cast<uint32_t>(bp.colorFunc & 0x7) << 16)
             | (static_cast<uint32_t>(bp.alphaFunc & 0x7) << 19);
    }

    // REMED-GFX-077: packs the four per-MRT-slot BlendState.ColorWriteChannels masks (4 bits each,
    // bit0=R..bit3=A) into the pipeline cache key's `cw` field. Unlike PackBlendBits it is NOT gated
    // on `blend`: a colour write mask applies to opaque (blend-disabled) draws too. The default
    // (All ×4 = 0xFFFF in the low 16 bits) is a fixed contribution, so default draws don't fragment.
    static uint32_t PackColorWriteBits(const BlendKeyParams& bp)
    {
        return (static_cast<uint32_t>(bp.colorWrite[0] & 0xF))
             | (static_cast<uint32_t>(bp.colorWrite[1] & 0xF) << 4)
             | (static_cast<uint32_t>(bp.colorWrite[2] & 0xF) << 8)
             | (static_cast<uint32_t>(bp.colorWrite[3] & 0xF) << 12);
    }

    // Task 868: fills a VkPipelineColorBlendAttachmentState's real blend factors/op from
    // BlendKeyParams -- shared by every 3D pipeline-creation function so the exact same
    // XNA->Vulkan mapping is used everywhere, mirroring FillDepthStencilState's own established
    // pattern. Previously every call site hardcoded BlendState.NonPremultiplied's own equation
    // here whenever blend was true, regardless of what was actually requested.
    static void FillBlendAttachmentState(VkPipelineColorBlendAttachmentState& cba, bool blend,
                                          const BlendKeyParams& bp, int attachmentIndex)
    {
        cba.blendEnable = blend ? VK_TRUE : VK_FALSE;
        if (blend) {
            cba.srcColorBlendFactor = ToVkBlendFactor(bp.colorSrc);
            cba.dstColorBlendFactor = ToVkBlendFactor(bp.colorDst);
            cba.colorBlendOp        = ToVkBlendOp(bp.colorFunc);
            cba.srcAlphaBlendFactor = ToVkBlendFactor(bp.alphaSrc);
            cba.dstAlphaBlendFactor = ToVkBlendFactor(bp.alphaDst);
            cba.alphaBlendOp        = ToVkBlendOp(bp.alphaFunc);
        }
        // REMED-GFX-077: per-attachment colour write mask (BlendState.ColorWriteChannels slot i for
        // MRT attachment i; slots >3 clamp to 3). XNA bits (R=1,G=2,B=4,A=8) are identical to
        // VK_COLOR_COMPONENT_*, so the raw value masked to 0xF is the VkColorComponentFlags directly.
        cba.colorWriteMask = static_cast<VkColorComponentFlags>(
            bp.colorWrite[attachmentIndex < 3 ? attachmentIndex : 3] & 0xF);
    }

    VkPipeline VulkanEffectRenderer::GetOrCreatePipeline(
        uint32_t colorAttachmentCount, VkSampleCountFlagBits sampleCount,
        VkFormat depthFormat, bool blend, const BlendKeyParams& blendParams)
    {
        colorAttachmentCount = std::max(1u, colorAttachmentCount);
        const PipelineVariantKey key{
            colorAttachmentCount,
            static_cast<uint32_t>(sampleCount),
            static_cast<int32_t>(depthFormat),
            blend,
            PackBlendBits(blend, blendParams),
            PackColorWriteBits(blendParams),
            NarrowSampleMaskEXT(blendParams.sampleMask, SampleCountToInt(sampleCount)),
        };
        auto cached = pipelines_.find(key);
        if (cached != pipelines_.end()) return cached->second;
        if (!IsValid()) return VK_NULL_HANDLE;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule_;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule_;
        stages[1].pName  = "main";

        VkVertexInputBindingDescription binding{
            0, sizeof(Sprite2DVertex), VK_VERTEX_INPUT_RATE_VERTEX
        };
        VkVertexInputAttributeDescription attributes[3]{};
        attributes[0] = {
            0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Sprite2DVertex, x)
        };
        attributes[1] = {
            1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Sprite2DVertex, u)
        };
        attributes[2] = {
            2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite2DVertex, r)
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = sampleCount;
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same space,
        // so a fully-set mask still leaves pSampleMask null (Vulkan's own default) and what the KEY
        // says can never drift from what the pipeline does.
        const int samples = SampleCountToInt(sampleCount);
        const VkSampleMask allSamples = NarrowSampleMaskEXT(0xFFFFFFFFu, samples);
        const VkSampleMask sampleMask = NarrowSampleMaskEXT(blendParams.sampleMask, samples);
        if (sampleMask != allSamples)
            multisample.pSampleMask = &sampleMask;

        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
            colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
            FillBlendAttachmentState(
                blendAttachments[i], blend, blendParams, static_cast<int>(i));
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = colorAttachmentCount;
        colorBlend.pAttachments = blendAttachments.data();

        VkDynamicState dynamicStates[3] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        const uint32_t dynamicStateCount = AppendBlendConstantsDynamicState(
            dynamicStates, 2, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = dynamicStateCount;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount = 2;
        createInfo.pStages = stages;
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &assembly;
        createInfo.pViewportState = &viewport;
        createInfo.pRasterizationState = &rasterizer;
        createInfo.pMultisampleState = &multisample;
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamic;
        createInfo.layout = pipelineLayout_;
        createInfo.renderPass = owner_->PickRTPipelineRenderPass(
            colorAttachmentCount, sampleCount > VK_SAMPLE_COUNT_1_BIT, depthFormat);
        createInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(
                owner_->device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline)
            != VK_SUCCESS) {
            throw std::runtime_error(
                "vkCreateGraphicsPipelines (custom Effect MRT variant) failed");
        }
        pipelines_[key] = pipeline;
        return pipeline;
    }

    // Packs every DepthStencilKeyParams field into 29 bits, meant to be OR'd (after shifting past
    // whatever bit range a caller's own existing key dimensions already occupy) into that
    // caller's uint64_t pipeline cache key.
    static uint64_t PackDepthStencilBits(const DepthStencilKeyParams& ds)
    {
        return  (static_cast<uint64_t>(ds.depthFunc & 7))
              | (ds.stencilEnable ? (1ull << 3) : 0ull)
              | (static_cast<uint64_t>(ds.stencilFunc & 7)         << 4)
              | (static_cast<uint64_t>(ds.stencilFail & 7)         << 7)
              | (static_cast<uint64_t>(ds.stencilDepthFail & 7)    << 10)
              | (static_cast<uint64_t>(ds.stencilPass & 7)         << 13)
              | (ds.twoSidedStencilMode ? (1ull << 16) : 0ull)
              | (static_cast<uint64_t>(ds.ccwStencilFunc & 7)      << 17)
              | (static_cast<uint64_t>(ds.ccwStencilFail & 7)      << 20)
              | (static_cast<uint64_t>(ds.ccwStencilDepthFail & 7) << 23)
              | (static_cast<uint64_t>(ds.ccwStencilPass & 7)      << 26);
    }

    // Fills in a VkPipelineDepthStencilStateCreateInfo's depthCompareOp and front/back
    // VkStencilOpState blocks from a DepthStencilKeyParams -- shared by every 3D
    // pipeline-creation function so the exact same XNA->Vulkan mapping is used everywhere.
    // stencilTestEnable's compare/write masks and the reference value are intentionally left
    // untouched here: they are true Vulkan dynamic state (vkCmdSetStencilCompareMask/WriteMask/
    // Reference), applied per-draw, not baked into the pipeline.
    static void FillDepthStencilState(VkPipelineDepthStencilStateCreateInfo& ds,
                                       const DepthStencilKeyParams& p)
    {
        ds.depthCompareOp     = ToVkCompareOp(p.depthFunc);
        ds.stencilTestEnable  = p.stencilEnable ? VK_TRUE : VK_FALSE;
        ds.front.failOp       = ToVkStencilOp(p.stencilFail);
        ds.front.passOp       = ToVkStencilOp(p.stencilPass);
        ds.front.depthFailOp  = ToVkStencilOp(p.stencilDepthFail);
        ds.front.compareOp    = ToVkCompareOp(p.stencilFunc);
        // XNA's TwoSidedStencilMode=false uses the SAME (front, clockwise) ops/func for both
        // faces (FNA's own real behavior: CCW fields are simply ignored when this is false, not
        // reset to any default) -- mirrors EasyGL's identical fallback-to-front pattern exactly.
        //
        // Task 870 empirical finding: with rs.frontFace = VK_FRONT_FACE_CLOCKWISE and this
        // renderer's vertex shaders' Y-flip (pos.y = -pos.y, see colored3d.vert.glsl et al.),
        // VkPipelineDepthStencilStateCreateInfo::front/back end up applied to the OPPOSITE
        // winding from what the *culling* path's frontFace determination would suggest --
        // confirmed via a genuinely differential stencil_twosided test (a back-facing triangle's
        // CounterClockwiseStencilFunction/Fail must apply and did not until front/back were
        // swapped here specifically; culling itself, which uses the exact same frontFace value,
        // is unaffected and already correct across this whole project's existing test suite).
        // Root cause not fully isolated (plausibly an llvmpipe/Mesa software-rasterizer quirk in
        // its own front/back VkStencilOpState assignment specifically, since culling's front/back
        // classification is provably correct on this same driver) -- swapped here pragmatically
        // since XNA's "front"/"CounterClockwise" stencil settings must land on whichever Vulkan
        // slot the hardware/driver actually evaluates for each winding, not on the slot named to
        // match culling's own already-correct convention.
        if (p.twoSidedStencilMode) {
            ds.front.failOp      = ToVkStencilOp(p.ccwStencilFail);
            ds.front.passOp      = ToVkStencilOp(p.ccwStencilPass);
            ds.front.depthFailOp = ToVkStencilOp(p.ccwStencilDepthFail);
            ds.front.compareOp   = ToVkCompareOp(p.ccwStencilFunc);
            ds.back.failOp      = ToVkStencilOp(p.stencilFail);
            ds.back.passOp      = ToVkStencilOp(p.stencilPass);
            ds.back.depthFailOp = ToVkStencilOp(p.stencilDepthFail);
            ds.back.compareOp   = ToVkCompareOp(p.stencilFunc);
        } else {
            ds.back = ds.front;
        }
    }

    // Encode (topology × depthTest × depthWrite × blend × cullMode × msaa × depth/stencil state)
    // into a uint64_t key.
    static uint64_t Make3DKey(VkPrimitiveTopology topo, bool depthTest, bool depthWrite,
                              bool blend, int cullMode, uint32_t colorAttachmentCount,
                              bool wireframe, bool msaa,
                              const DepthStencilKeyParams& ds = {})
    {
        uint64_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        // bits 0-1: topology, 2: depthTest, 3: depthWrite, 4: blend, 5-6: cullMode,
        // 7-9: colorAttachmentCount, 10: wireframe, 11: msaa, 12-40: depth/stencil state
        // (Task 870, see PackDepthStencilBits).
        const uint64_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return t | (depthTest ? 4ull : 0ull) | (depthWrite ? 8ull : 0ull) | (blend ? 16ull : 0ull)
                 | (static_cast<uint64_t>(cullMode & 0x3) << 5)
                 | (nc << 7)
                 | (wireframe ? (1ull << 10) : 0ull)
                 | (msaa     ? (1ull << 11) : 0ull)
                 | (PackDepthStencilBits(ds) << 12);
    }

    // Task 911: folds a target's real depth VkFormat into an already-computed pipeline cache key.
    // Needed because two draws with an otherwise-identical key could still require genuinely
    // different, mutually-incompatible pipelines if they render into targets with different real
    // depth formats -- Vulkan pipeline/render-pass compatibility requires an exact
    // attachment-format match (see PickRTPipelineRenderPass()). Uses bits 45+, well clear of
    // Make3DKey's own bits (top out at 40) and MakeExt3DKey's (top out at 44), and comfortably
    // wide for any real VkFormat ordinal.
    static uint64_t FoldDepthFormatIntoKey(uint64_t key, VkFormat depthFmt)
    {
        return key ^ (static_cast<uint64_t>(depthFmt) << 45);
    }

    // =========================================================================
    // plan_vulkan.md VULKAN-146: the stock programs' vertex inputs, by attribute location
    // =========================================================================
    //
    // One table per shader, in the order that shader declares its `layout(location = N) in`
    // variables -- this project's established "location N == the Nth field of the ported HLSL
    // input struct" convention, which every file under src/shaders/ already follows. These are
    // what BuildVulkanVertexInputLayoutEXT matches a caller's VertexDeclaration against.
    //
    // The tables describe the SHADER. Which shader runs is still chosen by the stride, exactly as
    // before and deliberately: VULKAN-146 makes the OFFSETS come from the declaration, not the
    // program selection, so a 24-byte record still means colored_textured3d. That is the scope
    // this row set, and it is what F-15 actually complained about -- "TextureCoordinate0@12 ... is
    // not bound at all" is an offset the pipeline did not know, not a program it could not pick.
    namespace StockInputs
    {
        using CNA::Internal::Graphics::StockProgramInput;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        constexpr StockProgramInput kPos{
            VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
        constexpr StockProgramInput kColor{
            VertexElementUsage::Color, 0, VertexElementFormat::Color, "aColor"};
        constexpr StockProgramInput kUv{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        constexpr StockProgramInput kNormal{
            VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};

        /// colored3d / colored3d_legacy: float3 position + ubyte4 colour (stride 16).
        constexpr StockProgramInput kColored[]      = { kPos, kColor };
        /// textured3d: float3 position + float2 uv (stride 20).
        constexpr StockProgramInput kTextured[]     = { kPos, kUv };
        /// colored_textured3d: float3 position + ubyte4 colour + float2 uv (stride 24).
        constexpr StockProgramInput kColTextured[]  = { kPos, kColor, kUv };
        /// lit_textured3d and its vertex-lit sibling: float3 position + float3 normal + float2 uv.
        constexpr StockProgramInput kLitTextured[]  = { kPos, kNormal, kUv };

        // VULKAN-147.
        constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        /// VULKAN-151: the shader declares `vec4`, so BOTH spellings XNA allows bind here --
        /// `Vector4` natively, `Byte4` through VK_FORMAT_R8G8B8A8_USCALED. The primary format is
        /// the float one because that is what the shader input actually is; `Byte4` is the
        /// alternate, and BuildVulkanVertexInputLayoutEXT converts it.
        constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Vector4, "aBoneIndices",
            VertexElementFormat::Byte4};

        /// alpha_test3d: one VS for strides 20 and 32, with the UV remapped to location 1 --
        /// which is exactly the offset guess (24 for stride 32, 12 for 20) the declaration removes.
        constexpr StockProgramInput kAlphaTest[]        = { kPos, kUv };
        /// alpha_test_colored3d, stride 24.
        constexpr StockProgramInput kAlphaTestColored[] = { kPos, kColor, kUv };
        /// env_map3d, stride 32.
        constexpr StockProgramInput kEnvMapped[]        = { kPos, kNormal, kUv };
        /// skinned3d and its vertex-lit sibling, stride 52.
        constexpr StockProgramInput kSkinned[]          = { kPos, kNormal, kUv, kWeights, kIndices };
        /// CNB-67's stride-56 sibling: the same with a vertex colour appended.
        constexpr StockProgramInput kSkinnedColored[]   = { kPos, kNormal, kUv, kWeights, kIndices,
                                                            kColor };

        // VULKAN-148.
        constexpr StockProgramInput kTangent{
            VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4, "aTangent"};
        constexpr StockProgramInput kUv1{
            VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};

        /// pbr3d, stride 48.
        constexpr StockProgramInput kPbr[]              = { kPos, kNormal, kTangent, kUv };
        /// pbr3d's dual-UV variant, stride 60 -- which also carries a vertex colour.
        constexpr StockProgramInput kPbrDualUv[]        = { kPos, kNormal, kTangent, kUv, kUv1,
                                                            kColor };
        /// pbr3d_skinned, stride 68.
        constexpr StockProgramInput kPbrSkinned[]       = { kPos, kNormal, kTangent, kUv, kWeights,
                                                            kIndices };
        /// Its dual-UV variant, stride 76.
        constexpr StockProgramInput kPbrSkinnedDualUv[] = { kPos, kNormal, kTangent, kUv, kWeights,
                                                            kIndices, kUv1 };
        /// plans/plan_gltf.md GLTF-463's stride-80 variant: the above plus a vertex colour.
        constexpr StockProgramInput kPbrSkinnedColored[] = { kPos, kNormal, kTangent, kUv, kWeights,
                                                             kIndices, kUv1, kColor };

        // VULKAN-150. DualTextureEffect's two coordinate sets. `kUv1` is a real shader input here,
        // not a spelling of `kUv`: dual_texture3d.frag.glsl samples uTexture2 with it.
        /// dual_texture3d, stride 20 (VertexPositionTexture).
        constexpr StockProgramInput kDualTexture[]        = { kPos, kUv, kUv1 };
        /// dual_texture_colored3d, stride 24 (VertexPositionColorTexture).
        constexpr StockProgramInput kDualTextureColored[] = { kPos, kColor, kUv, kUv1 };

        // VULKAN-149. The instanced route's PER-VERTEX inputs only: locations 0 and 1 of
        // instanced3d.vert.glsl / instanced_colored3d.vert.glsl. Its per-instance matrix columns
        // sit at 4..7 on binding 1, are not declaration-derived and are appended after these.
        /// instanced3d, position-only.
        constexpr StockProgramInput kInstanced[]        = { kPos };
        /// instanced_colored3d, REMED-GFX-212's position+colour sibling.
        constexpr StockProgramInput kInstancedColored[] = { kPos, kColor };
    }

    // VULKAN-146: swap a factory's baked attribute array for the declaration-derived one when the
    // caller supplied a complete layout. Deliberately a mutation of the array the factory already
    // built rather than a replacement of the whole vertex-input state: the BINDING (stride, input
    // rate) is the buffer's and is already correct, and leaving each factory's own array in place
    // for the empty-declaration case keeps the `VertexBuffer(device, count)` path byte-identical.
    static void ApplyDeclaredVertexLayoutEXT(const VulkanVertexInputLayoutEXT& layout,
                                             VkVertexInputAttributeDescription* attrs,
                                             std::size_t attrsCapacity,
                                             uint32_t& attrCount)
    {
        if (!layout.IsComplete()) return;
        if (layout.attributeCount > attrsCapacity) return;   // cannot happen; not worth risking
        for (std::uint32_t i = 0; i < layout.attributeCount; ++i)
            attrs[i] = layout.attributes[i];
        attrCount = layout.attributeCount;
    }

    // VULKAN-150: DualTextureEffect's layout, with the one aliasing rule this family needs.
    //
    // XNA's DualTextureEffect samples `Texture2` with **TEXCOORD1**. This renderer's shader took
    // one UV and used it for both samplers, and the only reason that never showed as a wrong
    // picture is that the stride guard refused every record carrying an independent second set
    // (F-20). With the shader fixed, `inUV1` is a real input that must be bound.
    //
    // **The aliasing rule, and why it is not a fudge.** A record that declares only
    // `TextureCoordinate0` -- which is every stride-20 and stride-24 record this family has ever
    // been given -- gets `inUV1` pointed at `TextureCoordinate0`'s own element. That reproduces the
    // previous behaviour bit for bit rather than leaving a shader input unbound, and it is what
    // XNA itself does in the sense that matters: an effect asked to use a coordinate set the vertex
    // does not have has only one set to use. It is applied ONLY to `TextureCoordinate1`, only when
    // `TextureCoordinate0` is present, and it never invents an offset -- it copies one the
    // declaration supplied.
    static VulkanVertexInputLayoutEXT BuildDualTextureVertexLayoutEXT(
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared, bool colored)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        if (declared.IsEmpty()) return {};
        const auto* inputs = colored ? StockInputs::kDualTextureColored : StockInputs::kDualTexture;
        const std::size_t inputCount = colored ? std::size(StockInputs::kDualTextureColored)
                                               : std::size(StockInputs::kDualTexture);
        VulkanVertexInputLayoutEXT layout =
            BuildVulkanVertexInputLayoutEXT(declared, inputs, inputCount);
        // The UV1 input is the last one in both tables, and UV0 the one before it.
        const std::uint32_t uv1Location = static_cast<std::uint32_t>(inputCount - 1);
        const std::uint32_t uv0Location = static_cast<std::uint32_t>(inputCount - 2);
        const bool uv1Missing = (layout.missingInputMask & (1u << uv1Location)) != 0;
        if (!uv1Missing) return layout;
        // Find UV0's own description; without it there is nothing to alias and the layout stays
        // incomplete, which sends the draw back through the stride path and the fidelity guard.
        for (std::uint32_t i = 0; i < layout.attributeCount; ++i) {
            if (layout.attributes[i].location != uv0Location) continue;
            VkVertexInputAttributeDescription aliased = layout.attributes[i];
            aliased.location = uv1Location;
            layout.attributes[layout.attributeCount++] = aliased;
            layout.missingInputMask &= ~(1u << uv1Location);
            break;
        }
        return layout;
    }

    // VULKAN-146: the input table for whichever of the four BasicEffect-family programs this draw
    // selected. Returns nullptr for a family this row has not converted, so those keep their
    // stride-derived layout untouched.
    static bool DeclarationNamesUsageEXT(
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
    {
        for (const auto& e : declared.GetElements())
            if (e.getVertexElementUsageProperty() == usage && e.getUsageIndexProperty() == 0)
                return true;
        return false;
    }

    // VULKAN-146: which BasicEffect-family program this draw runs, from the stride AND the
    // declaration.
    //
    // One stride is genuinely ambiguous, and it is the one EasyGL's own SelectStockProgramShape
    // calls out under REMED-GFX-234: 32 is VertexPositionNormalTexture's, and a Position+Colour
    // vertex padded to 32 reaches it too. The lit programs take {aPos, aNormal, aUV} and have no
    // colour input, so such a vertex has nothing to bind its colour to and renders correct geometry
    // with its colour silently dropped. A declaration that names no normal cannot be a lit vertex
    // whatever its stride, so ask it. An ABSENT declaration keeps the stride's answer, which is
    // the only thing there is to go on and is what every VertexBuffer(device, count) relies on.
    static BasicProgramShapeEXT SelectBasicProgramShapeEXT(
        bool useFogTex3D, bool useLitTextured, std::size_t stride,
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        if (useLitTextured) return BasicProgramShapeEXT::LitTextured;
        if (!useFogTex3D)   return BasicProgramShapeEXT::None;
        switch (stride) {
            case 16: return BasicProgramShapeEXT::Colored;
            case 20: return BasicProgramShapeEXT::Textured;
            case 24: return BasicProgramShapeEXT::ColoredTextured;
            case 32:
                // Reached only once needsLitTextured has already said no -- see the caller, which
                // asks the declaration for a normal before deciding that.
                if (!declared.IsEmpty() &&
                    !DeclarationNamesUsageEXT(declared, VertexElementUsage::Normal))
                    return BasicProgramShapeEXT::Colored;
                return BasicProgramShapeEXT::None;
            default: return BasicProgramShapeEXT::None;
        }
    }

    // VULKAN-147: the input table for the stock EFFECT program this draw selected. The family is
    // already decided by the caller's own needsX flags -- unlike the BasicEffect bundle, none of
    // these strides is ambiguous -- so only the table is needed here, not a recorded shape.
    static const CNA::Internal::Graphics::StockProgramInput* EffectFamilyStockInputsEXT(
        bool needsAlphaTest, bool needsEnvMap, bool needsSkinned, std::size_t stride,
        std::size_t& countOut)
    {
        countOut = 0;
        if (needsAlphaTest) {
            if (stride == 24) {
                countOut = std::size(StockInputs::kAlphaTestColored);
                return StockInputs::kAlphaTestColored;
            }
            countOut = std::size(StockInputs::kAlphaTest);
            return StockInputs::kAlphaTest;
        }
        if (needsEnvMap) {
            countOut = std::size(StockInputs::kEnvMapped);
            return StockInputs::kEnvMapped;
        }
        if (needsSkinned) {
            if (stride == 56) {
                countOut = std::size(StockInputs::kSkinnedColored);
                return StockInputs::kSkinnedColored;
            }
            countOut = std::size(StockInputs::kSkinned);
            return StockInputs::kSkinned;
        }
        return nullptr;
    }

    // VULKAN-148: the PBR families' input tables. The variant a stride selects is unchanged --
    // which SHADER runs is still the stride's decision here, exactly as in VULKAN-146 -- so a
    // declaration is asked only where each of that shader's inputs lives.
    static const CNA::Internal::Graphics::StockProgramInput* PbrFamilyStockInputsEXT(
        bool skinned, std::size_t stride, std::size_t& countOut)
    {
        countOut = 0;
        if (skinned) {
            if (stride == 80) {
                countOut = std::size(StockInputs::kPbrSkinnedColored);
                return StockInputs::kPbrSkinnedColored;
            }
            if (stride == 76) {
                countOut = std::size(StockInputs::kPbrSkinnedDualUv);
                return StockInputs::kPbrSkinnedDualUv;
            }
            countOut = std::size(StockInputs::kPbrSkinned);
            return StockInputs::kPbrSkinned;
        }
        if (stride == 60) {
            countOut = std::size(StockInputs::kPbrDualUv);
            return StockInputs::kPbrDualUv;
        }
        countOut = std::size(StockInputs::kPbr);
        return StockInputs::kPbr;
    }

    // VULKAN-146: the input table for the program SelectBasicProgramShapeEXT chose. Null for a
    // family this row has not converted, so those keep their stride-derived layout untouched.
    static const CNA::Internal::Graphics::StockProgramInput* BasicShapeStockInputsEXT(
        BasicProgramShapeEXT shape, std::size_t& countOut)
    {
        countOut = 0;
        switch (shape) {
            case BasicProgramShapeEXT::Colored:
                countOut = std::size(StockInputs::kColored);     return StockInputs::kColored;
            case BasicProgramShapeEXT::Textured:
                countOut = std::size(StockInputs::kTextured);    return StockInputs::kTextured;
            case BasicProgramShapeEXT::ColoredTextured:
                countOut = std::size(StockInputs::kColTextured); return StockInputs::kColTextured;
            case BasicProgramShapeEXT::LitTextured:
                countOut = std::size(StockInputs::kLitTextured); return StockInputs::kLitTextured;
            case BasicProgramShapeEXT::None:
                break;
        }
        return nullptr;
    }

    // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary for every route that infers its
    // vertex input from the stride. The ordinary routes fall back to the colored pipeline's
    // Position@0 + Color@12 layout for a stride the canonical table does not list -- which is why
    // a position-only stride-12 buffer renders correctly here today -- while the Instanced3D
    // module is position-only for every stride PackedColorOffsetForStride below does not list.
    // plans/plan_fx.md FX-065: `compiledEffect` exempts a draw from this gate, and only this gate. The
    // gate exists because the stock routes pick their VkVertexInputAttributeDescription set from
    // the buffer's STRIDE, so a declaration that set cannot represent would be rendered from the
    // wrong bytes. A compiled effect derives its attributes from the declaration itself, one per
    // shader input at that element's own format and offset, so any declaration it can satisfy is
    // faithful by construction -- and one it cannot is refused by name in LinkAndGetShadersEXT.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb_in, const char* route,
                                              bool positionOnlyFallback = false,
                                              bool compiledEffect = false)
    {
        if (compiledEffect) return;
        const auto& vb = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        // plan_vulkan.md VULKAN-165, opened by VULKAN-139's audit. The shared guard returns
        // immediately for a buffer that carries NO declaration -- correctly, since there is nothing
        // to be unfaithful to -- and that is the hole: `VertexBuffer(device, count)` +
        // `SetDataRaw(data, count, stride)` produces exactly such a buffer, and an unlisted stride
        // then reached the PositionColor fallback and was drawn as a 16-byte record. Measured on a
        // 28-byte stride: accepted at upload, drawn and presented in silence, with the Khronos
        // layer saying nothing. EasyGL refuses the same call by name (GLTF-157).
        //
        // The rule is narrow on purpose, and the narrowness is the whole design decision:
        //
        //   * only where the fallback INVENTS an attribute. The PositionColor fallback reads a
        //     packed colour at offset 12 out of bytes that mean something else -- and out of bounds
        //     entirely for a record shorter than 16. The PositionOnly fallback reads Position at
        //     offset 0, which every layout in the table also puts there, so it cannot read the
        //     wrong bytes and is left alone. That is why `positionOnlyFallback` short-circuits.
        //   * only where the stride is genuinely unlisted. `InferredLayoutForStride(..,
        //     RendererRefusesIt)` is the canonical "is this stride one we have a layout for"
        //     question, asked of the shared table rather than of a list repeated here.
        //   * only where there is no declaration. A buffer that declares its layout is judged by
        //     the fidelity guard below exactly as before -- including the position-only stride-12
        //     case that guard deliberately admits.
        const int stride = static_cast<int>(vb.GetStride());
        if (!positionOnlyFallback && vb.GetDeclarationEXT().IsEmpty() &&
            !CNA::Internal::Graphics::InferredLayoutForStride(
                 stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt).known)
        {
            throw System::NotSupportedException(
                "Vulkan: a VertexBuffer with no VertexDeclaration and an unsupported vertex stride "
                + std::to_string(stride) + " cannot be drawn on the " + std::string(route) +
                " route. This renderer selects its native vertex layout from the stride, and " +
                std::to_string(stride) + " is not one it has a layout for -- the draw is refused "
                "rather than bound as Position + packed colour and rendered from the wrong bytes. "
                "Give the buffer a VertexDeclaration.");
        }
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            vb.GetDeclarationEXT(), stride,
            positionOnlyFallback
                ? CNA::Internal::Graphics::UnlistedStrideLayout::PositionOnlyFallback
                : CNA::Internal::Graphics::UnlistedStrideLayout::PositionColorFallback,
            "Vulkan", route);
    }

    // REMED-GFX-212: the Instanced3D cache is the one 3D pipeline cache whose per-vertex binding
    // takes the RAW stride rather than a bucketed one -- MakeExt3DKey folds 16 and every stride it
    // does not recognise into the same bucket 0. That was harmless while every stride produced the
    // same position-only vertex input, and is not once the stride decides whether a COLOR0
    // attribute exists at all: a position-only declaration and a position+colour one must never
    // share a pipeline. Bits 53..63 are free -- Make3DKey's own bits top out at 40, MakeExt3DKey's
    // at 44, and FoldDepthFormatIntoKey occupies 45..52 for a core VkFormat ordinal.
    static uint64_t FoldPerVertexStrideIntoKey(uint64_t key, std::size_t pvStride)
    {
        return key ^ ((static_cast<uint64_t>(pvStride) & 0x7FFull) << 53);
    }

    // REMED-GFX-212: whether this per-vertex stride's established packed layout carries a COLOR0
    // element, and at which byte offset. The ordinary route's own pipelines derive their
    // R8G8B8A8_UNORM colour attribute from exactly this table -- GetOrCreatePipelineFogColored3D's
    // stride-16 `attrs[1]` and GetOrCreatePipelineFogTex3D's stride-24 `attrs[1]`, both at offset
    // 12 -- so reading the same one here is what makes the instanced route's VertexColorEnabled
    // mean what its own ordinary route's already means. Every other stride keeps the position-only
    // Instanced3D shader it has always used: 20 (VertexPositionTexture) and 32
    // (VertexPositionNormalTexture) carry no COLOR0 at all, and the skinned/PBR strides carry one
    // the instanced route has no bone palette or tangent basis to render anyway.
    static bool PackedColorOffsetForStride(std::size_t pvStride, uint32_t& colorOffsetOut)
    {
        switch (pvStride) {
        case 16:   // VertexPositionColor
        case 24:   // VertexPositionColorTexture
            colorOffsetOut = 12;
            return true;
        default:
            return false;
        }
    }

    // VULKAN-149: the instanced route's PER-VERTEX layout, and with it the choice between its two
    // vertex shaders.
    //
    // REMED-GFX-212 made that choice from the stride alone, and the table above is the whole of
    // it. That is the same guess REMED-GFX-234 took out of the BasicEffect bundle, in both
    // directions: a position-only vertex padded to 16 bytes gets a colour attribute aimed at four
    // bytes of padding, and a Position+Colour vertex at any stride the table does not list loses
    // its colour with no diagnostic. A declaration answers both questions -- whether there is a
    // colour at all, and where it is.
    //
    // An ABSENT declaration keeps the stride's answer; it is the only thing there is to go on, and
    // it is what every `VertexBuffer(device, count)` relies on. The declaration's answer is taken
    // only when the layout it produces is COMPLETE: a shader picked from the declaration and fed
    // from the stride table would be the worst of both, so an incomplete layout falls back to the
    // stride for the shape and the attributes together, and goes through the fidelity guard.
    //
    // Only the two per-vertex inputs are built here. The per-instance matrix columns are a
    // separate stream at locations 4..7 on binding 1, appended by the factory afterwards, and
    // `MultiStreamVertexInput` stays false.
    static VulkanVertexInputLayoutEXT BuildInstancedVertexLayoutEXT(
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared)
    {
        if (declared.IsEmpty()) return {};
        const bool wantColored = DeclarationNamesUsageEXT(
            declared, Microsoft::Xna::Framework::Graphics::VertexElementUsage::Color);
        std::size_t inputCount = 0;
        const CNA::Internal::Graphics::StockProgramInput* inputs = nullptr;
        if (wantColored) {
            inputs     = StockInputs::kInstancedColored;
            inputCount = std::size(StockInputs::kInstancedColored);
        } else {
            inputs     = StockInputs::kInstanced;
            inputCount = std::size(StockInputs::kInstanced);
        }
        return BuildVulkanVertexInputLayoutEXT(declared, inputs, inputCount);
    }

    VkPipeline VulkanRenderer::GetOrCreatePipeline3D(VkPrimitiveTopology topo,
                                                             bool depthTest, bool depthWrite,
                                                             bool blend, int cullMode,
                                                             uint32_t colorAttachmentCount,
                                                             bool wireframe, bool msaa,
                                                             const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt)
    {
        // Create layout once
        if (pipelineLayout3D_ == VK_NULL_HANDLE) {
            // 128 bytes: colored3d.vert.glsl's PC struct mirrors FillExtPushConst()'s full
            // 32-float layout (Task 364) so diffuseColor/vertexColorEnabled reach the shader.
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (3D) failed");
        }

        PipelineKey key = { FoldDepthFormatIntoKey(Make3DKey(topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1) };
        auto it = pipelines3D_.find(key);
        if (it != pipelines3D_.end()) return it->second;

        using namespace Shaders;
        // Task 899: dedicated shaders (was: reuse kColored3dVertSpv/kColored3dFragSpv) --
        // colored3d.vert.glsl/.frag.glsl now declare a fog UBO binding as part of the shared
        // colored3d/textured3d/colored_textured3d bundle, incompatible with this pipeline's
        // unmodified zero-descriptor-set pipelineLayout3D_ (used only by the legacy,
        // no-GpuDrawParams DrawColoredPrimitives()/DrawIndexedColoredPrimitives() path, which
        // has no fog data to forward anyway).
        VkShaderModule vert = CreateShaderModule(kColored3dLegacyVertSpv, kColored3dLegacyVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kInstanced3dFragSpv, kInstanced3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };
        attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = 2; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // Pipeline uses VK_FRONT_FACE_CLOCKWISE, so CW faces are front faces.
        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;  // cull CW (front) faces
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;   // cull CCW (back) faces

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u));
        for (uint32_t i = 0; i < cbaVec.size(); ++i)
            FillBlendAttachmentState(
                cbaVec[i], blend, blendParams, static_cast<int>(i));

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        // Task 870: stencil reference/compare mask/write mask are true Vulkan dynamic state,
        // set per-draw via vkCmdSetStencilReference/CompareMask/WriteMask (see draw3DFor) --
        // not baked into the pipeline, so DepthStencilState.ReferenceStencil/StencilMask/
        // StencilWriteMask changes never need a new pipeline variant.
        VkDynamicState dynStates[7] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        pci.renderPass          = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass             = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelines3D_[key] = p;
        return p;
    }

    // =========================================================================
    // Sprite GPU buffers
    // =========================================================================

    void VulkanRenderer::CreateSpriteBuffers()
    {
        VkDeviceSize vbSize = MaxSpriteVertices * sizeof(Sprite2DVertex);
        VkDeviceSize ibSize = MaxSpriteIndices  * sizeof(uint16_t);
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                spriteVB_[i], spriteVBMem_[i], &spriteVBPtr_[i]);
            CreateBuffer(ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                spriteIB_[i], spriteIBMem_[i], &spriteIBPtr_[i]);
        }
    }

    void VulkanRenderer::CreateFrame3DBuffers()
    {
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(kFrame3DVBSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DVB_[i], frame3DVBMem_[i], &frame3DVBPtr_[i]);
            CreateBuffer(kFrame3DIBSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DIB_[i], frame3DIBMem_[i], &frame3DIBPtr_[i]);
        }
    }

    void VulkanRenderer::EnsureFrame3DBuffers()
    {
        if (!frame3DBuffersAllocated_) {
            CreateFrame3DBuffers();
            frame3DBuffersAllocated_ = true;
        }
    }

    void VulkanRenderer::CreateFrame3DInstBuffers()
    {
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            CreateBuffer(kFrame3DInstVBSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame3DInstVB_[i], frame3DInstVBMem_[i], &frame3DInstVBPtr_[i]);
        }
    }

    void VulkanRenderer::EnsureFrame3DInstBuffers()
    {
        if (!frame3DInstBuffersAllocated_) {
            CreateFrame3DInstBuffers();
            frame3DInstBuffersAllocated_ = true;
        }
    }

    // =========================================================================
    // Extended 3D pipeline helpers (Tasks 52-54: textured + lit)
    // =========================================================================

    static uint64_t MakeExt3DKey(std::size_t stride, VkPrimitiveTopology topo,
                                  bool depthTest, bool depthWrite, bool blend,
                                  int cullMode, uint32_t colorAttachmentCount,
                                  bool wireframe, bool msaa,
                                  const DepthStencilKeyParams& ds = {})
    {
        uint64_t s = 0;
        switch (stride) { case 20: s = 1; break; case 24: s = 2; break; case 32: s = 3; break;
                          case 52: s = 4; break;
                          // PbrEffect (48, or dual-UV 60) / CNB-67 skinned+color (56) /
                          // SkinnedPbrEffect (68, or dual-UV 76).
                          case 48: s = 5; break; case 56: s = 6; break; case 68: s = 7; break;
                          case 60: s = 8; break; case 76: s = 9; break;
                          // plans/plan_gltf.md GLTF-463: skinned PBR + COLOR_0.
                          case 80: s = 10; break;
                          default: s = 0; }
        uint64_t t = 0;
        switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  t = 0; break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: t = 1; break;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      t = 2; break;
        default:                                   t = 3; break;
        }
        // bits 0-3: stride, 4-5: topology, 6: depthTest, 7: depthWrite, 8: blend,
        // 9-10: cullMode, 11-13: colorAttachmentCount, 14: wireframe, 15: msaa,
        // 16-44: depth/stencil state (Task 870, see PackDepthStencilBits).
        const uint64_t nc = std::min(colorAttachmentCount, 8u) - 1u;
        return s | (t << 4) | (depthTest ? (1ull<<6) : 0) | (depthWrite ? (1ull<<7) : 0)
             | (blend ? (1ull<<8) : 0) | (static_cast<uint64_t>(cullMode & 3) << 9) | (nc << 11)
             | (wireframe ? (1ull<<14) : 0)
             | (msaa      ? (1ull<<15) : 0)
             | (PackDepthStencilBits(ds) << 16);
    }

    void VulkanRenderer::EnsureDefaultWhiteTexture()
    {
        if (defaultWhiteImage_ != VK_NULL_HANDLE) return;
        VkDevice dev = device_;

        // 1×1 RGBA white image.
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent        = {1, 1, 1};
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &info, nullptr, &defaultWhiteImage_) != VK_SUCCESS) return;

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, defaultWhiteImage_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &alloc, nullptr, &defaultWhiteMemory_);
        vkBindImageMemory(dev, defaultWhiteImage_, defaultWhiteMemory_, 0);

        // Upload white pixel.
        VkBuffer       sb = VK_NULL_HANDLE;
        VkDeviceMemory sm = VK_NULL_HANDLE;
        void*          sp = nullptr;
        const uint8_t  whitePixel[4] = {255, 255, 255, 255};
        CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     sb, sm, &sp);
        std::memcpy(sp, whitePixel, 4);

        TransitionImageLayout(defaultWhiteImage_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkCommandBuffer cb = BeginOneTimeCommands();
        VkBufferImageCopy reg{};
        reg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        reg.imageExtent      = {1, 1, 1};
        vkCmdCopyBufferToImage(cb, sb, defaultWhiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
        EndOneTimeCommands(cb);
        TransitionImageLayout(defaultWhiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(dev, sb, nullptr);
        vkFreeMemory(dev, sm, nullptr);

        // Image view.
        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = defaultWhiteImage_;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev, &vci, nullptr, &defaultWhiteView_);

        // Descriptor set.
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorPool     = descriptorPool_;
        dsAI.descriptorSetCount = 1;
        dsAI.pSetLayouts        = &descriptorSetLayout_;
        // VULKAN-391: this site did not even LOOK at the result. VULKAN-390's mutation probe
        // showed what that costs -- `defaultWhiteDescSet_` stays VK_NULL_HANDLE, the three 3D draw
        // routes bind it, and the process segfaults with the layer reporting
        // `pDescriptorSets[0] (VK_NULL_HANDLE)`. Refused by name instead.
        if (AllocateOneDescriptorSet(dev, dsAI, defaultWhiteDescSet_) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: the default white texture's descriptor set could not be "
                "allocated. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo di{};
        di.sampler     = defaultSampler_;
        di.imageView   = defaultWhiteView_;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wr{};
        wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet          = defaultWhiteDescSet_;
        wr.dstBinding      = 0;
        wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount = 1;
        wr.pImageInfo      = &di;
        vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
    }

    // Mirrors EnsureDefaultWhiteTexture() above exactly (1x1 sampled image), except the pixel
    // value and no standalone descriptor set (PbrEffect's normal map is always one of several
    // samplers in a shared descriptor set built by GetOrCreatePbrDescSet/GetOrCreatePbrSkinnedDescSet,
    // unlike the single-sampler descriptorSetLayout_ defaultWhiteDescSet_ belongs to). (128,128,255,255)
    // decodes (rgb*2-1) to tangent-space (0,0,1) -- the geometric normal, unperturbed -- matching
    // EasyGLRenderer::EnsureDefaultFlatNormalTexture()'s own fallback semantics.
    void VulkanRenderer::EnsureDefaultFlatNormalTexture()
    {
        if (defaultFlatNormalImage_ != VK_NULL_HANDLE) return;
        VkDevice dev = device_;

        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent        = {1, 1, 1};
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &info, nullptr, &defaultFlatNormalImage_) != VK_SUCCESS) return;

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, defaultFlatNormalImage_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &alloc, nullptr, &defaultFlatNormalMemory_);
        vkBindImageMemory(dev, defaultFlatNormalImage_, defaultFlatNormalMemory_, 0);

        VkBuffer       sb = VK_NULL_HANDLE;
        VkDeviceMemory sm = VK_NULL_HANDLE;
        void*          sp = nullptr;
        const uint8_t  flatNormalPixel[4] = {128, 128, 255, 255};
        CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     sb, sm, &sp);
        std::memcpy(sp, flatNormalPixel, 4);

        TransitionImageLayout(defaultFlatNormalImage_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkCommandBuffer cb = BeginOneTimeCommands();
        VkBufferImageCopy reg{};
        reg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        reg.imageExtent      = {1, 1, 1};
        vkCmdCopyBufferToImage(cb, sb, defaultFlatNormalImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
        EndOneTimeCommands(cb);
        TransitionImageLayout(defaultFlatNormalImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(dev, sb, nullptr);
        vkFreeMemory(dev, sm, nullptr);

        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = defaultFlatNormalImage_;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev, &vci, nullptr, &defaultFlatNormalView_);
    }

    void VulkanRenderer::FillExtPushConst(float (&pc)[32], const Matrix& wvp,
                                                  const GpuDrawParams& p)
    {
        // [0..15]: MVP matrix (column-major)
        wvp.ToColumnMajor(pc);
        // [16..19]: diffuseColor
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        // [20..22]: ambientColor, [23]: lightingEnabled
        pc[20] = p.ambientColor[0]; pc[21] = p.ambientColor[1]; pc[22] = p.ambientColor[2];
        pc[23] = p.lightingEnabled ? 1.f : 0.f;
        // [24..26]: light0Dir, [27]: textureEnabled
        pc[24] = p.light0Dir[0]; pc[25] = p.light0Dir[1]; pc[26] = p.light0Dir[2];
        pc[27] = p.textureEnabled ? 1.f : 0.f;
        // [28..30]: light0Diffuse, [31]: vertexColorEnabled
        pc[28] = p.light0Diffuse[0]; pc[29] = p.light0Diffuse[1]; pc[30] = p.light0Diffuse[2];
        pc[31] = p.vertexColorEnabled ? 1.f : 0.f;
    }

    // Shared fill for pbr3d.vert/frag.glsl's and pbr3d_skinned.vert/frag.glsl's identical
    // PbrParams UBO layout (124 floats -- see pbr3d.frag.glsl's own struct). DirectionalLight0,
    // DiffuseColor (base color factor), and AmbientColor are NOT here -- they travel through the
    // 128-byte PC via FillExtPushConst instead (reused unchanged for PbrEffect/SkinnedPbrEffect,
    // same field semantics).
    void VulkanRenderer::FillPbrUboData(float (&out)[124], const GpuDrawParams& p,
                                                float weightsPerVertex)
    {
        out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.f;
        out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.f;
        out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.f;
        out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.f;
        for (int wi = 0; wi < 16; ++wi) out[16 + wi] = p.worldColMajor[wi];
        out[32] = p.eyePositionWorld[0]; out[33] = p.eyePositionWorld[1]; out[34] = p.eyePositionWorld[2];
        out[35] = p.pbrMetallicFactor;
        out[36] = p.emissiveColor[0]; out[37] = p.emissiveColor[1]; out[38] = p.emissiveColor[2];
        out[39] = p.pbrRoughnessFactor;
        out[40] = p.fogColor[0]; out[41] = p.fogColor[1]; out[42] = p.fogColor[2];
        // REMED-GFX-010: fogColorEnabled.w (was fogEnabled, now folded into the fog vector) carries
        // WeightsPerVertex instead (pbr3d_skinned reads it; unused/0 for the unskinned pbr3d).
        out[43] = weightsPerVertex;
        // [44..47]: REMED-GFX-010 FNA fog vector, dotted with the object/skinned position -> true
        // view-space fog. Zero when disabled, (0,0,0,1) for the fogStart==fogEnd degenerate case.
        out[44] = p.fogVector[0]; out[45] = p.fogVector[1];
        out[46] = p.fogVector[2]; out[47] = p.fogVector[3];
        out[48] = p.alphaTest[0]; out[49] = p.alphaTest[1];
        out[50] = p.alphaTest[2]; out[51] = p.alphaTest[3];
        out[52] = p.pbrNormalScale; out[53] = p.pbrOcclusionStrength;
        out[54] = 0.f; out[55] = 0.f;
        out[56] = p.pbrBaseColorTextureIsSrgb ? 1.f : 0.f;
        out[57] = p.pbrEmissiveTextureIsSrgb ? 1.f : 0.f;
        out[58] = p.pbrEncodeOutputToSrgb ? 1.f : 0.f;
        out[59] = p.pbrSpecularColorTextureIsSrgb ? 1.f : 0.f;
        out[60] = p.pbrDielectricF0Unclamped[0]; out[61] = p.pbrDielectricF0Unclamped[1];
        out[62] = p.pbrDielectricF0Unclamped[2]; out[63] = p.pbrSpecularFactor;
        for (int row = 0; row < 10; ++row)
            for (int component = 0; component < 4; ++component)
                out[64 + row * 4 + component] = p.pbrTextureTransformRows[row][component];
        for (int row = 0; row < 4; ++row)
            for (int component = 0; component < 4; ++component)
                out[104 + row * 4 + component] =
                    p.pbrSpecularTextureTransformRows[row][component];
        // The dual-UV shaders consume all seven low bits; legacy stride-48/68 shaders keep the
        // same prefix and simply never address this appended selector vec4.
        out[120] = static_cast<float>(p.pbrTextureCoordinateSetMask & 0x7fu);
        out[121] = 0.f; out[122] = 0.f; out[123] = 0.f;
    }

    void VulkanRenderer::FillInstancedPushConst(float (&pc)[32], const Matrix& view,
                                                        const Matrix& proj, const GpuDrawParams& p)
    {
        // [0..15]: VP matrix (view × projection, column-major); world comes from per-instance buffer
        const Matrix vp = view * proj;
        vp.ToColumnMajor(pc);
        // [16..31]: same layout as FillExtPushConst
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        pc[20] = p.ambientColor[0]; pc[21] = p.ambientColor[1]; pc[22] = p.ambientColor[2];
        pc[23] = p.lightingEnabled ? 1.f : 0.f;
        pc[24] = p.light0Dir[0]; pc[25] = p.light0Dir[1]; pc[26] = p.light0Dir[2];
        pc[27] = p.textureEnabled ? 1.f : 0.f;
        pc[28] = p.light0Diffuse[0]; pc[29] = p.light0Diffuse[1]; pc[30] = p.light0Diffuse[2];
        pc[31] = p.vertexColorEnabled ? 1.f : 0.f;
    }

    void VulkanRenderer::FillAlphaTestPushConst(float (&pc)[32], const Matrix& wvp,
                                                        const GpuDrawParams& p)
    {
        // [0..15]: MVP matrix (column-major)
        wvp.ToColumnMajor(pc);
        // [16..19]: diffuseColor
        pc[16] = p.diffuseColor[0]; pc[17] = p.diffuseColor[1];
        pc[18] = p.diffuseColor[2]; pc[19] = p.diffuseColor[3];
        // [20..23]: alpha test params {refVal, tolerance, passWeight, failWeight}
        pc[20] = p.alphaTest[0]; pc[21] = p.alphaTest[1];
        pc[22] = p.alphaTest[2]; pc[23] = p.alphaTest[3];
        // [24]: vertexColorEnabled (Task 887, read only by the stride-24 colored variant's VS)
        pc[24] = p.vertexColorEnabled ? 1.f : 0.f;
        // [25..27]: FogColor.xyz; [28..31]: REMED-GFX-010 FNA fog vector (dotted with object-space
        // pos in the VS -> true view-space fog; zero when disabled, (0,0,0,1) for fogStart==fogEnd).
        pc[25] = p.fogColor[0]; pc[26] = p.fogColor[1]; pc[27] = p.fogColor[2];
        pc[28] = p.fogVector[0]; pc[29] = p.fogVector[1];
        pc[30] = p.fogVector[2]; pc[31] = p.fogVector[3];
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineAlphaTest3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        if (pipelineLayoutAlphaTest3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout_;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutAlphaTest3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (AlphaTest3D) failed");
        }

        // VULKAN-158: this factory already binds the record's own stride; what it did not do is put
        // that stride in the KEY. MakeExt3DKey buckets an unlisted stride and the layout hash covers
        // offsets only, so two records with identical element offsets and different strides -- 64
        // and 68, say -- bucketed and hashed the same and shared one pipeline, whose binding stride
        // belonged to whichever of them drew first.
        const uint32_t recordStride = static_cast<uint32_t>(stride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesAlphaTest3D_.find(key);
        if (it != pipelinesAlphaTest3D_.end()) return it->second;

        using namespace Shaders;
        // Task 887: stride 24 (VertexPositionColorTexture) gets its own vertex shader that reads
        // the color attribute and gates it by VertexColorEnabled; strides 20/32 have no color data
        // and keep the original shared position+UV-only shader (UV offset remapped per stride).
        const bool colored = (stride == 24);
        VkShaderModule vert = colored
            ? CreateShaderModule(kAlphaTestColored3dVertSpv, kAlphaTestColored3dVertSpv_size)
            : CreateShaderModule(kAlphaTest3dVertSpv, kAlphaTest3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kAlphaTest3dFragSpv, kAlphaTest3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        uint32_t attrCount;
        if (colored) {
            // float3 pos + ubyte4 color + float2 uv (mirrors colored_textured3d's layout).
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
            attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    16 };
            attrCount = 3;
        } else {
            // Position always at location=0, UV always remapped to location=1.
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };   // position
            // VULKAN-147: this guess is exactly what a declaration removes -- 24 past a float3
            // normal at stride 32, 12 at stride 20. ApplyDeclaredVertexLayoutEXT below replaces it
            // with the declared offset whenever there is one.
            uint32_t uvOffset = (stride == 32) ? 24 : 12;
            attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, uvOffset }; // UV remapped to location=1
            attrCount = 2;
        }

        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor;
        cbs.pAttachments    = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutAlphaTest3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesAlphaTest3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    void VulkanRenderer::EnsureDualTexResources()
    {
        if (descriptorSetLayout2Tex_ != VK_NULL_HANDLE) return;

        // Two combined image samplers at bindings 0/1, plus (Task 899) a dynamic fog UBO at
        // binding=2 -- DualTextureEffect's 128-byte push constant has no spare bytes for fog.
        VkDescriptorSetLayoutBinding bindings[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding            = i;
            bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount    = 1;
            bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayout2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (2-tex) failed");

        // Pool for up to 512 dual-texture descriptor sets × MaxFramesInFlight (per-frame cache,
        // Task 899 -- the fog UBO buffer differs per frame in flight).
        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 2 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPool2Tex_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (2-tex) failed");

        // Pipeline layout: same 128-byte PC range + 2-sampler+fog descriptor set layout.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout2Tex_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutDualTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (DualTex3D) failed");

        // Per-frame UBO ring buffer for the fog block (Task 899).
        const VkDeviceSize uboSize = kDualTexFogUBOStride * kDualTexFogUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (dualTexFogUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    dualTexFogUBO_[i], dualTexFogUBOMem_[i], &dualTexFogUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateDualTexDescSet(
        uint32_t frameIdx, VkImageView view0, VkImageView view1,
        VkSampler sampler0, VkSampler sampler1)
    {
        EnsureDualTexResources();

        const uint64_t key = reinterpret_cast<uint64_t>(view0)    * 2654435761ULL
                           ^ reinterpret_cast<uint64_t>(view1)    * 40503ULL
                           ^ reinterpret_cast<uint64_t>(sampler0) * 2246822519ULL
                           ^ reinterpret_cast<uint64_t>(sampler1) * 3266489917ULL;
        auto& cache = dualTexDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) {
            VkSamplerTraceEXT("desc.DualTexture hit=1 key=0x%llx set=0x%llx "
                              "binding=0 slot=0 view=0x%llx sampler=0x%llx | "
                              "binding=1 slot=1 view=0x%llx sampler=0x%llx",
                              static_cast<unsigned long long>(key), VkH(it->second.set),
                              VkH(view0), VkH(sampler0), VkH(view1), VkH(sampler1));
            return it->second.set;
        }

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPool2Tex_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayout2Tex_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: DualTextureEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo imgInfo[2]{};
        imgInfo[0].sampler     = sampler0;
        imgInfo[0].imageView   = view0;
        imgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo[1].sampler     = sampler1;
        imgInfo[1].imageView   = view1;
        imgInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = dualTexFogUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 32; // vec4 fogColorEnabled + vec4 fogVector

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
        // REMED-GFX-076: record the sampled views so this entry is evicted+freed when either dies.
        cache[key] = EffectDescSetEntry{ ds, { view0, view1, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineDualTex3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt,
        const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureDualTexResources();

        // DualTexture uses stride=20 (VertexPositionTexture) by default, or stride=24
        // (VertexPositionColorTexture, Task 889) when VertexColorEnabled needs a color attribute.
        const std::size_t dualStride = (stride == 24) ? 24 : 20;
        // VULKAN-158: the same rule as the other converted factories -- the vertex BINDING's stride
        // is the record's own whenever the declaration supplied every input, and the family's
        // canonical 20/24 otherwise. This is the factory whose baked stride made VULKAN-150's own
        // stride-40 fixture render black, before the dual-texture shader was even reached.
        const uint32_t recordStride = vertexLayout.IsComplete()
                                          ? static_cast<uint32_t>(stride)
                                          : static_cast<uint32_t>(dualStride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(dualStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesDualTex3D_.find(key);
        if (it != pipelinesDualTex3D_.end()) return it->second;

        using namespace Shaders;
        // Task 899: dedicated vertex shader (was: reuse kTextured3dVertSpv) -- textured3d.vert.glsl
        // now declares its own fog UBO at binding=1 (Bundle A's shared layout), which conflicts
        // with dual_texture3d's 2-sampler descriptor set layout (fog UBO here is at binding=2).
        // Task 889: stride 24 (VertexPositionColorTexture) gets its own vertex shader that reads
        // the color attribute and gates it by VertexColorEnabled, mirroring Task 887's
        // alpha_test_colored3d.vert.glsl pattern; both variants share the unchanged fragment shader.
        const bool colored = (dualStride == 24);
        VkShaderModule vert = colored
            ? CreateShaderModule(kDualTextureColored3dVertSpv, kDualTextureColored3dVertSpv_size)
            : CreateShaderModule(kDualTexture3dVertSpv,        kDualTexture3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kDualTexture3dFragSpv,  kDualTexture3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, recordStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[4]{};
        uint32_t attrCount;
        if (colored) {
            // float3 pos + ubyte4 color + float2 uv (mirrors colored_textured3d's layout).
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
            attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    16 };
            // VULKAN-150: this stride's record has ONE coordinate set, so the shader's second UV
            // input reads the same element. That is exactly what the shader did before it had a
            // second input, and it keeps every stride-24 draw byte-identical.
            attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,    16 };
            attrCount = 4;
        } else {
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
            attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,    12 };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    12 };   // VULKAN-150, as above
            attrCount = 3;
        }
        // VULKAN-150: and the declaration's own offsets replace all of them when it supplied every
        // input -- including a genuinely independent TextureCoordinate1.
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutDualTex3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesDualTex3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // ---- EnvironmentMapEffect resources (Task 108) ----

    void VulkanRenderer::EnsureEnvMapResources()
    {
        if (descriptorSetLayoutEnvMap_ != VK_NULL_HANDLE) return;

        // Descriptor set layout: binding=0 sampler2D, binding=1 samplerCube, binding=2 UBO dynamic.
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutEnvMap_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (EnvMap) failed");

        // Descriptor pool: max 512 sets × MaxFramesInFlight.
        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 2 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolEnvMap_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (EnvMap) failed");

        // Pipeline layout: 128-byte PC (mvp+world) + env map descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutEnvMap_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutEnvMap3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (EnvMap3D) failed");

        // Per-frame UBO ring buffers for env map FS params.
        const VkDeviceSize uboSize = kEnvMapUBOStride * kEnvMapUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (envMapUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    envMapUBO_[i], envMapUBOMem_[i], &envMapUBOPtr_[i]);
            }
        }

        // Default white cube image for fallback when envMap is null.
        if (defaultWhiteCubeImage_ == VK_NULL_HANDLE) {
            VkImageCreateInfo imgInfo{};
            imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            imgInfo.imageType     = VK_IMAGE_TYPE_2D;
            imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
            imgInfo.extent        = { 1, 1, 1 };
            imgInfo.mipLevels     = 1;
            imgInfo.arrayLayers   = 6;
            imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(device_, &imgInfo, nullptr, &defaultWhiteCubeImage_);

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(device_, defaultWhiteCubeImage_, &memReq);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memReq.size;
            allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device_, &allocInfo, nullptr, &defaultWhiteCubeMem_);
            vkBindImageMemory(device_, defaultWhiteCubeImage_, defaultWhiteCubeMem_, 0);

            // Upload white pixels to all 6 faces via staging buffer.
            const uint32_t white = 0xFFFFFFFF;
            VkBuffer stageBuf = VK_NULL_HANDLE; VkDeviceMemory stageMem = VK_NULL_HANDLE;
            CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stageBuf, stageMem, nullptr);
            void* mapped = nullptr;
            vkMapMemory(device_, stageMem, 0, 4, 0, &mapped);
            std::memcpy(mapped, &white, 4);
            vkUnmapMemory(device_, stageMem);

            VkCommandBuffer cb = BeginOneTimeCommands();
            VkImageMemoryBarrier barr{};
            barr.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barr.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barr.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barr.image               = defaultWhiteCubeImage_;
            barr.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
            barr.srcAccessMask       = 0;
            barr.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barr);

            for (uint32_t face = 0; face < 6; ++face) {
                VkBufferImageCopy region{};
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 };
                region.imageExtent      = { 1, 1, 1 };
                vkCmdCopyBufferToImage(cb, stageBuf, defaultWhiteCubeImage_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
            barr.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barr.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barr.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barr.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barr);
            EndOneTimeCommands(cb);

            vkDestroyBuffer(device_, stageBuf, nullptr);
            vkFreeMemory(device_, stageMem, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image    = defaultWhiteCubeImage_;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
            vkCreateImageView(device_, &viewInfo, nullptr, &defaultWhiteCubeView_);
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateEnvMapDescSet(
        uint32_t frameIdx, VkImageView view2D, VkImageView viewCube,
        VkSampler sampler2D, VkSampler samplerCube)
    {
        EnsureEnvMapResources();
        if (view2D   == VK_NULL_HANDLE) view2D   = defaultWhiteView_;
        if (viewCube == VK_NULL_HANDLE) viewCube = defaultWhiteCubeView_;

        // REMED-GFX-169: the samplers are part of the identity of this descriptor set, not just of
        // its contents -- keying on the views alone handed a second draw the first draw's sampler.
        const uint64_t key = reinterpret_cast<uint64_t>(view2D)      * 2654435761ULL
                           ^ reinterpret_cast<uint64_t>(viewCube)
                           ^ reinterpret_cast<uint64_t>(sampler2D)   * 2246822519ULL
                           ^ reinterpret_cast<uint64_t>(samplerCube) * 3266489917ULL;
        auto& cache = envMapDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolEnvMap_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutEnvMap_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: EnvironmentMapEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo imgInfo[2]{};
        imgInfo[0] = { sampler2D,   view2D,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imgInfo[1] = { samplerCube, viewCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkSamplerTraceEXT("desc.EnvMap      hit=0 key=0x%llx set=0x%llx "
                          "binding=0 slot=0 view=0x%llx sampler=0x%llx | "
                          "binding=1 slot=1 viewCube=0x%llx sampler=0x%llx",
                          static_cast<unsigned long long>(key), VkH(ds),
                          VkH(view2D), VkH(imgInfo[0].sampler),
                          VkH(viewCube), VkH(imgInfo[1].sampler));

        // Binding=2: dynamic UBO pointing to the whole per-frame ring buffer.
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = envMapUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 192; // size of one EnvMapParams block in the shader (96 + fog/899 + light1/2/890)

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled views so this entry is evicted+freed when either dies.
        cache[key] = EffectDescSetEntry{ ds, { view2D, viewCube, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineEnvMap3D(
        std::size_t stride,
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureEnvMapResources();

        constexpr std::size_t kEnvStride = 32;
        // VULKAN-158: the vertex BINDING's stride is the record's own, not this family's canonical
        // one, whenever the declaration supplied every input. Baking the constant made the
        // declaration-driven offsets right and the interval between records wrong -- the attributes
        // point at the correct bytes of a record the fetch never reaches. Without a declaration the
        // canonical constant is all there is, and the key and the binding stay byte-identical.
        const uint32_t recordStride = vertexLayout.IsComplete()
                                          ? static_cast<uint32_t>(stride)
                                          : static_cast<uint32_t>(kEnvStride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kEnvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesEnvMap3D_.find(key);
        if (it != pipelinesEnvMap3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kEnvMap3dVertSpv, kEnvMap3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kEnvMap3dFragSpv, kEnvMap3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, recordStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 };   // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    24 };   // aUV
        uint32_t attrCount = 3;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutEnvMap3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesEnvMap3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    void VulkanRenderer::FillEnvMapPushConst(float (&pc)[32],
                                                     const Matrix& wvp, const Matrix& world)
    {
        wvp.ToColumnMajor(pc);        // [0..15]: MVP
        world.ToColumnMajor(pc + 16); // [16..31]: World
    }

    // ---- BasicEffect lit-textured resources (Task 897) ----
    // DirectionalLight1/DirectionalLight2/EmissiveColor forwarding, added alongside the
    // unchanged 128-byte PC (still filled by FillExtPushConst, same as strides 20/24) since
    // that content is shared/verified — only a new UBO binding is added for the extra data.

    void VulkanRenderer::EnsureLitTexturedResources()
    {
        if (descriptorSetLayoutLitTextured_ != VK_NULL_HANDLE) return;

        // binding=0: sampler2D (fragment), binding=1: light1/2+emissive+world+specular UBO
        // dynamic. Vertex stage needs it too (Task 898: world matrix, for a correct world-space
        // position/normal instead of the wrong MVP-based transform).
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutLitTextured_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (LitTextured) failed");

        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolLitTextured_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (LitTextured) failed");

        // Pipeline layout: same 128-byte PC as pipelineLayoutExt3D_ (unchanged content/fill
        // function) + the new lit-textured descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutLitTextured_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutLitTextured3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (LitTextured3D) failed");

        const VkDeviceSize uboSize = kLitTexturedUBOStride * kLitTexturedUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (litTexturedUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    litTexturedUBO_[i], litTexturedUBOMem_[i], &litTexturedUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateLitTexturedDescSet(
        uint32_t frameIdx, VkImageView view2D, VkSampler sampler)
    {
        EnsureLitTexturedResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        // REMED-GFX-169: sampler identity is part of the key, not only of the contents.
        const uint64_t key = reinterpret_cast<uint64_t>(view2D)
                           ^ reinterpret_cast<uint64_t>(sampler) * 2246822519ULL;
        auto& cache = litTexturedDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolLitTextured_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutLitTextured_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: the lit-textured BasicEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo imgInfo{ sampler, view2D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkSamplerTraceEXT("desc.LitTextured hit=0 key=0x%llx set=0x%llx "
                          "binding=0 slot=0 view=0x%llx sampler=0x%llx",
                          static_cast<unsigned long long>(key), VkH(ds),
                          VkH(view2D), VkH(imgInfo.sampler));

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = litTexturedUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 256;  // size of one LitLightParams block in the shader (Task 886/898/888)

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled view so this entry is evicted+freed when it dies.
        cache[key] = EffectDescSetEntry{ ds, { view2D, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineLitTextured3D(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureLitTexturedResources();

        // VULKAN-158: this factory takes no stride, and that is safe rather than an oversight --
        // the lit-textured route is selected by `stride == 32` and by nothing else (see
        // `needsLitTextured` at the draw), so the record's stride IS 32 whenever this is reached.
        // A declaration only moves the offsets WITHIN that record. If the selection rule ever
        // widens, this constant becomes the same defect the sibling factories had.
        constexpr std::size_t kLitStride = 32;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kLitStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        auto it = pipelinesLitTextured3D_.find(key);
        if (it != pipelinesLitTextured3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kLitTextured3dVertSpv, kLitTextured3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kLitTextured3dFragSpv, kLitTextured3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(kLitStride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 };   // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    24 };   // aUV
        uint32_t attrCount = 3;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutLitTextured3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesLitTextured3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // Task 1103: PreferPerPixelLighting=false (XNA's real default) sibling of
    // GetOrCreatePipelineLitTextured3D above -- identical descriptor set layout/pipeline layout/
    // vertex input state (the shader I/O contract is unchanged, only WHERE lighting is computed
    // moves), so this reuses EnsureLitTexturedResources()/pipelineLayoutLitTextured3D_ unchanged
    // and differs only in which shader modules get compiled into the pipeline and which cache
    // map the result is stored in.
    VkPipeline VulkanRenderer::GetOrCreatePipelineLitTextured3DVertexLit(
        VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureLitTexturedResources();

        // VULKAN-158: this factory takes no stride, and that is safe rather than an oversight --
        // the lit-textured route is selected by `stride == 32` and by nothing else (see
        // `needsLitTextured` at the draw), so the record's stride IS 32 whenever this is reached.
        // A declaration only moves the offsets WITHIN that record. If the selection rule ever
        // widens, this constant becomes the same defect the sibling factories had.
        constexpr std::size_t kLitStride = 32;
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(kLitStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        auto it = pipelinesLitTextured3DVertexLit_.find(key);
        if (it != pipelinesLitTextured3DVertexLit_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kLitTextured3dVertexLitVertSpv, kLitTextured3dVertexLitVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kLitTextured3dVertexLitFragSpv, kLitTextured3dVertexLitFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(kLitStride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 };   // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    24 };   // aUV
        uint32_t attrCount = 3;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutLitTextured3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesLitTextured3DVertexLit_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // ---- BasicEffect fog bundle (Task 899): colored3d / textured3d / colored_textured3d ----
    //
    // These 3 pipelines share the exact same fully-packed 128-byte FillExtPushConst() push
    // constant (zero spare bytes for fog), so fog is forwarded via one small shared dynamic UBO
    // instead -- mirroring descriptorSetLayoutLitTextured_'s exact shape (sampler@0 + dynamic
    // UBO@1). colored3d's own shaders never sample a texture, but the layout still declares
    // binding=0 so all three pipelines can share one descriptor-set-layout/pool/UBO/cache bundle;
    // a fallback white texture is bound there for colored3d draws (same fallback pattern every
    // other pipeline already uses when a slot has no real texture).

    void VulkanRenderer::EnsureFogTex3DResources()
    {
        if (descriptorSetLayoutFogTex3D_ != VK_NULL_HANDLE) return;

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (FogTex3D) failed");

        const uint32_t maxSets = 512u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (FogTex3D) failed");

        // Pipeline layout: same 128-byte PC as FillExtPushConst() (unchanged content/fill
        // function) + the new fog descriptor set. Declared with both stages even though
        // colored3d's own fragment shader never reads it -- Vulkan permits a pipeline layout to
        // declare a stage-accessible range wider than what any specific attached shader reads.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutFogTex3D_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutFogTex3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (FogTex3D) failed");

        const VkDeviceSize uboSize = kFogTex3DUBOStride * kFogTex3DUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (fogTex3DUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    fogTex3DUBO_[i], fogTex3DUBOMem_[i], &fogTex3DUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateFogTex3DDescSet(
        uint32_t frameIdx, VkImageView view2D, VkSampler sampler)
    {
        EnsureFogTex3DResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        // REMED-GFX-169: sampler identity is part of the key, not only of the contents.
        const uint64_t key = reinterpret_cast<uint64_t>(view2D)
                           ^ reinterpret_cast<uint64_t>(sampler) * 2246822519ULL;
        auto& cache = fogTex3DDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolFogTex3D_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutFogTex3D_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: the textured BasicEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo imgInfo{ sampler, view2D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkSamplerTraceEXT("desc.FogTex3D    hit=0 key=0x%llx set=0x%llx "
                          "binding=0 slot=0 view=0x%llx sampler=0x%llx",
                          static_cast<unsigned long long>(key), VkH(ds),
                          VkH(view2D), VkH(imgInfo.sampler));

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = fogTex3DUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = 32; // vec4 fogColorEnabled + vec4 fogVector

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled view so this entry is evicted+freed when it dies.
        cache[key] = EffectDescSetEntry{ ds, { view2D, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE } };
        return ds;
    }

#if defined(CNA_VULKAN_COMPILED_EFFECTS)
    // ---- Compiled XNA effects (plans/plan_fx.md FX-065) -------------------------------------------
    //
    // MojoShader's SPIR-V profile fixes its descriptor sets at 0 = vertex samplers, 1 = vertex
    // uniform block, 2 = pixel samplers, 3 = pixel uniform block. Every pipeline layout declares
    // all four; the ones a shader does not use get an empty layout, which is what
    // vkCreatePipelineLayout needs at that index. Vertex-stage sampling is refused before we get
    // here (plans/plan_fx.md FX-109), so set 0 is always the empty one.

    void VulkanRenderer::PrepareCompiledEffectDrawEXT(
        Pending3DDraw& d, const IVertexBufferRenderer& vb_in, const GpuDrawParams& params)
    {
        // This renderer binds one vertex stream, and reports MultiStreamVertexInput false, so a
        // wider binding set is refused by GraphicsDevice first. Draw*PrimitivesEx is still a public
        // entry point a harness can call directly, and a silently truncated binding list looks
        // exactly like a correct draw of the wrong data.
        RejectUnsupportedStreamCombination(params, "CNA Vulkan compiled-effect drawing");

        const auto& vb = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        const auto& declaration = vb.GetDeclarationEXT();
        if (declaration.IsEmpty())
        {
            throw System::NotSupportedException(
                "CNA Vulkan: a compiled-effect draw needs the vertex buffer's own "
                "VertexDeclaration; this renderer does not infer one from stride for this route.");
        }
        const std::vector<VkFxStreamEXT> streams{
            {&declaration.GetElements(), static_cast<std::uint32_t>(d.stride), false}};
        PrepareCompiledEffectDrawEXT(d, streams, params.compiledEffectRuntime);
    }

    void VulkanRenderer::PrepareCompiledEffectDrawEXT(
        Pending3DDraw& d,
        const std::vector<CNA::Internal::Renderers::Vulkan::VulkanCompiledEffect::
                              CompiledVertexStreamEXT>& streams,
        ICompiledEffectRuntime* runtime)
    {
        using Microsoft::Xna::Framework::Graphics::Texture2D;
        using Microsoft::Xna::Framework::Graphics::Texture3D;
        using Microsoft::Xna::Framework::Graphics::TextureCube;
        namespace VkFx = CNA::Internal::Renderers::Vulkan;

        auto* effect = dynamic_cast<VkFx::VulkanCompiledEffect*>(runtime);
        if (effect == nullptr)
        {
            throw std::runtime_error(
                "CNA Vulkan: the applied compiled effect was not created by this renderer.");
        }

        auto& compiled = d.compiledEffect;
        compiled.pass = effect->LinkAndGetShadersEXT(streams);

        VkFx::VulkanCompiledShaderEXT* vertexShader = nullptr;
        VkFx::VulkanCompiledShaderEXT* pixelShader = nullptr;
        effect->GetBoundShadersEXT(vertexShader, pixelShader);
        if (vertexShader != nullptr && vertexShader->parseData != nullptr &&
            vertexShader->parseData->sampler_count > 0)
        {
            // plans/plan_fx.md FX-109: MojoShader's SPIR-V profile reserves descriptor set 0 for them,
            // but nothing above this renderer routes VertexSamplerStates down here, so a bound
            // vertex sampler would read an undefined image rather than the intended one.
            throw System::NotSupportedException(
                "CNA Vulkan: this compiled effect's vertex shader samples a texture; vertex-stage "
                "sampling is not implemented by this renderer's compiled-effect draw route yet.");
        }

        effect->CaptureUniformSnapshotEXT(compiled.vertexUniforms, compiled.pixelUniforms);
        const auto requireFits = [](const std::vector<std::uint8_t>& bytes, const char* stage) {
            if (bytes.size() > kCompiledEffectUBOStride)
            {
                throw System::NotSupportedException(
                    std::string("CNA Vulkan: this compiled effect's ") + stage +
                    " uniform block is " + std::to_string(bytes.size()) +
                    " bytes, larger than this renderer's " +
                    std::to_string(kCompiledEffectUBOStride) +
                    "-byte compiled-effect uniform slice.");
            }
        };
        requireFits(compiled.vertexUniforms, "vertex");
        requireFits(compiled.pixelUniforms, "pixel");

        const auto kindName = [](MOJOSHADER_samplerType type) {
            switch (type)
            {
                case MOJOSHADER_SAMPLER_CUBE:   return "samplerCUBE (TextureCube)";
                case MOJOSHADER_SAMPLER_VOLUME: return "sampler3D (Texture3D)";
                default:                        return "sampler2D (Texture2D)";
            }
        };

        for (const MOJOSHADER_sampler& reflected : compiled.pass.pixelSamplers)
        {
            const auto slot = static_cast<std::uint32_t>(reflected.index);
            Microsoft::Xna::Framework::Graphics::Texture* boundTexture = nullptr;
            Microsoft::Xna::Framework::Graphics::SamplerState samplerState;
            bool samplerAssigned = false;
            effect->GetBoundSamplerEXT(static_cast<int>(slot), /*vertexStage=*/false, boundTexture,
                                       samplerState, &samplerAssigned);
            if (boundTexture == nullptr)
            {
                const char* name =
                    reflected.name != nullptr ? reflected.name : "<unnamed>";
                throw std::runtime_error(
                    std::string("CNA Vulkan: this compiled effect's pixel shader samples '") +
                    name + "', but no texture is bound to it.");
            }

            auto* texture2D = dynamic_cast<Texture2D*>(boundTexture);
            auto* textureCube = dynamic_cast<TextureCube*>(boundTexture);
            auto* texture3D = dynamic_cast<Texture3D*>(boundTexture);
            const MOJOSHADER_samplerType boundKind =
                textureCube != nullptr ? MOJOSHADER_SAMPLER_CUBE
                : texture3D != nullptr ? MOJOSHADER_SAMPLER_VOLUME
                                       : MOJOSHADER_SAMPLER_2D;
            // plans/plan_fx.md FX-110: a VkImageView carries its own view type, so binding a cube view
            // where the shader declared sampler2D is undefined behaviour that a validation layer
            // catches and a release build renders. Named here instead of either.
            if (reflected.type != boundKind)
            {
                throw System::NotSupportedException(
                    std::string("CNA Vulkan: this compiled effect's pixel shader declares ") +
                    kindName(reflected.type) + " at slot " + std::to_string(slot) +
                    ", but the texture bound there is a " + kindName(boundKind) +
                    ". The dimensions must match.");
            }

            VkImageView view = VK_NULL_HANDLE;
            const ITextureRenderer* sampledSource = nullptr;
            if (textureCube != nullptr)
            {
                const auto* cube =
                    dynamic_cast<const IVulkanCubeSamplable*>(&textureCube->GetRenderer());
                if (cube != nullptr) view = cube->GetVkCubeImageView();
            }
            else if (texture3D != nullptr)
            {
                // plans/plan_fx.md FX-110: VulkanTexture3DRenderer's image already carries
                // VK_IMAGE_USAGE_SAMPLED_BIT and a VK_IMAGE_VIEW_TYPE_3D view, and SetData leaves
                // it in SHADER_READ_ONLY_OPTIMAL, so sampling a volume needed an accessor rather
                // than any new resource handling.
                const auto* volume =
                    dynamic_cast<const IVulkanVolumeSamplable*>(&texture3D->GetRenderer());
                if (volume != nullptr) view = volume->GetVkVolumeImageView();
            }
            else if (texture2D != nullptr)
            {
                sampledSource = &texture2D->GetRenderer();
                const auto* samplable = dynamic_cast<const IVulkanSamplable*>(sampledSource);
                if (samplable != nullptr) view = samplable->GetVkImageView();
            }
            if (view == VK_NULL_HANDLE)
            {
                throw std::runtime_error(
                    "CNA Vulkan: the texture bound to this compiled effect's pixel sampler slot " +
                    std::to_string(slot) + " has no sampleable image view.");
            }
            // REMED-GFX-151: a compiled effect can sample a render target just as a stock draw
            // can, so its sources have to join the same dependency closure -- otherwise the
            // producing pass is still queued when this consumer replays.
            if (sampledSource != nullptr)
            {
                NoteSampledRenderTargetGroupEXT(currentSegment_,
                                                SampledRenderTargetGroupEXT(sampledSource));
            }

            SamplerStateKey samplerKey{};
            if (samplerAssigned)
            {
                // plans/plan_fx.md FX-083: the pass's own sampler_state block, LOD clamp and bias
                // included -- the key expresses all of it, so none of it is lost on the way to a
                // VkSampler.
                samplerKey.filter = static_cast<int>(samplerState.getFilterProperty());
                samplerKey.addressU = static_cast<int>(samplerState.getAddressUProperty());
                samplerKey.addressV = static_cast<int>(samplerState.getAddressVProperty());
                samplerKey.addressW = static_cast<int>(samplerState.getAddressWProperty());
                samplerKey.maxAnisotropy = samplerState.getMaxAnisotropyProperty();
                samplerKey.maxMipLevel = samplerState.getMaxMipLevelProperty();
                samplerKey.lodBias = samplerState.getMipMapLevelOfDetailBiasProperty();
            }
            else if (slot < 16u)
            {
                // No pass assigned this slot, so what the game selected on the device stands --
                // GraphicsDevice.SamplerStates[slot], recorded whole in samplerSlotState_.
                samplerKey = samplerSlotState_[slot];
            }
            // VULKAN-161: no null branch -- a failed creation throws now, and substituting
            // `defaultSampler_` here would have run the pass with filtering it did not ask for.
            const VkSampler sampler = GetOrCreateSamplerEXT(samplerKey);
            compiled.samplerViews.push_back(view);
            compiled.samplerBindings.push_back(slot);
            compiled.samplers.push_back(sampler);
        }

        d.useCompiledEffect = true;
    }

    void VulkanRenderer::QueueCompiledEffectSpriteEXT(const Sprite2DVertex (&quad)[6],
                                                      VkImageView spriteView,
                                                      ICompiledEffectRuntime* runtime)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        // SpriteBatch geometry is this renderer's own, so its declaration is fixed rather than
        // caller-supplied. It has to describe Sprite2DVertex exactly: the compiled vertex shader
        // reads its inputs by semantic, and a mismatched offset here is a wrongly-drawn sprite.
        static const std::vector<VertexElement> kSpriteDeclaration = {
            VertexElement(0,  VertexElementFormat::Vector2, VertexElementUsage::Position, 0),
            VertexElement(8,  VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
        };

        Pending3DDraw d{};
        d.vbData.resize(sizeof(quad));
        std::memcpy(d.vbData.data(), quad, sizeof(quad));
        d.stride = sizeof(Sprite2DVertex);
        d.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        d.drawCount = 6;
        d.indexType = VK_INDEX_TYPE_UINT16;
        // A sprite is 2D geometry the batch orders itself, so it neither tests nor writes depth --
        // the same choice the stock sprite pipeline makes. Everything the batch DOES own (blend,
        // scissor, viewport, blend factor) comes from the current device state, exactly as the
        // stock sprite path snapshots it.
        d.depthTest = false;
        d.depthWrite = false;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode = 0;
        d.wireframe = false;
        d.depthBias = 0.0f;
        d.slopeScaleDepthBias = 0.0f;
        d.rt = currentRT_;
        const std::vector<VkFxStreamEXT> streams{
            {&kSpriteDeclaration, static_cast<std::uint32_t>(sizeof(Sprite2DVertex)), false}};
        PrepareCompiledEffectDrawEXT(d, streams, runtime);

        // plans/plan_fx.md FX-103. FNA's SpriteBatch.DrawPrimitives sets GraphicsDevice.Textures[0] =
        // texture immediately AFTER pass.Apply(), with the comment "Set this _after_ Apply,
        // otherwise EffectParameters override it!". So the sprite being drawn wins slot 0
        // unconditionally, whatever the effect's own texture parameter names -- a backend that
        // binds the effect's texture instead renders a plausible image of the wrong thing. The
        // sampler is NOT overridden: SpriteBatch.Begin's SamplerState is selected before Apply, so
        // a pass that assigns slot 0's sampler_state still wins that half.
        auto& compiled = d.compiledEffect;
        for (std::size_t i = 0; i < compiled.samplerBindings.size(); ++i)
        {
            if (compiled.samplerBindings[i] == 0) compiled.samplerViews[i] = spriteView;
        }
        PushPending3DDraw(std::move(d));
    }

    VkShaderModule VulkanRenderer::GetOrCreateCompiledEffectShaderModuleEXT(
        const void* code, std::size_t codeBytes)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(code);
        std::uint64_t hash = 1469598103934665603ull;  // FNV-1a
        for (std::size_t i = 0; i < codeBytes; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        // Length is folded in as well, so two different-length bodies cannot collide on a prefix.
        hash ^= codeBytes * 0x9E3779B97F4A7C15ull;

        auto it = compiledEffectShaderModules_.find(hash);
        if (it != compiledEffectShaderModules_.end()) return it->second;

        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = codeBytes;
        info.pCode = static_cast<const std::uint32_t*>(code);
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("CNA Vulkan: vkCreateShaderModule failed.");
        compiledEffectShaderModules_.emplace(hash, module);
        return module;
    }

    void VulkanRenderer::EnsureCompiledEffectResourcesEXT()
    {
        if (compiledEffectDescriptorPool_ != VK_NULL_HANDLE) return;

        const auto makeLayout = [&](const VkDescriptorSetLayoutBinding* bindings, uint32_t count) {
            VkDescriptorSetLayoutCreateInfo li{};
            li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            li.bindingCount = count;
            li.pBindings = bindings;
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &layout) != VK_SUCCESS)
                throw std::runtime_error("vkCreateDescriptorSetLayout (compiled effect) failed");
            return layout;
        };

        compiledEffectEmptySetLayout_ = makeLayout(nullptr, 0);

        VkDescriptorSetLayoutBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uniformBinding.descriptorCount = 1;
        uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        compiledEffectUniformSetLayoutVS_ = makeLayout(&uniformBinding, 1);
        uniformBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        compiledEffectUniformSetLayoutPS_ = makeLayout(&uniformBinding, 1);

        const uint32_t maxSets = 256u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 4u };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = maxSets;
        pi.poolSizeCount = 2;
        pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &compiledEffectDescriptorPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (compiled effect) failed");

    }

    VulkanRenderer::CompiledEffectUniformChunkEXT&
    VulkanRenderer::EnsureCompiledEffectUniformChunkEXT(uint32_t frameIdx, std::size_t chunkIndex)
    {
        EnsureCompiledEffectResourcesEXT();
        auto& chunks = compiledEffectUBOChunks_[frameIdx];
        const VkDeviceSize chunkSize = static_cast<VkDeviceSize>(kCompiledEffectUBOStride) *
                                       kCompiledEffectUBODrawsPerChunk;
        // One descriptor set per chunk for each uniform block: the set names only the chunk's
        // buffer, and which slice of it a draw uses travels as a dynamic offset.
        const auto allocateUniformSet = [&](VkDescriptorSetLayout layout, VkBuffer buffer) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = compiledEffectDescriptorPool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            // VULKAN-391: through the shared helper, as every other site now is.
            if (AllocateOneDescriptorSet(device_, ai, set) != VK_SUCCESS)
                throw std::runtime_error("vkAllocateDescriptorSets (compiled effect) failed");
            VkDescriptorBufferInfo bi{};
            bi.buffer = buffer;
            bi.offset = 0;
            bi.range = kCompiledEffectUBOStride;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
            return set;
        };
        while (chunks.size() <= chunkIndex)
        {
            CompiledEffectUniformChunkEXT chunk;
            CreateBuffer(chunkSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         chunk.vsBuffer, chunk.vsMemory, &chunk.vsPtr);
            CreateBuffer(chunkSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         chunk.psBuffer, chunk.psMemory, &chunk.psPtr);
            chunk.vsSet = allocateUniformSet(compiledEffectUniformSetLayoutVS_, chunk.vsBuffer);
            chunk.psSet = allocateUniformSet(compiledEffectUniformSetLayoutPS_, chunk.psBuffer);
            chunks.push_back(chunk);
        }
        return chunks[chunkIndex];
    }

    VkPipelineLayout VulkanRenderer::GetOrCreateCompiledEffectPipelineLayoutEXT(
        const std::vector<std::uint32_t>& pixelSamplerBindings)
    {
        EnsureCompiledEffectResourcesEXT();
        // The signature is the exact set of sampler registers the pixel shader declares. Two
        // effects that sample the same registers share a layout; two that do not cannot be given
        // each other's, which is what stops a binding landing on the wrong slot.
        std::uint64_t signature = 0;
        for (const std::uint32_t binding : pixelSamplerBindings)
        {
            if (binding < 64u) signature |= (1ull << binding);
        }

        auto layoutIt = compiledEffectSamplerSetLayouts_.find(signature);
        if (layoutIt == compiledEffectSamplerSetLayouts_.end())
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bindings.reserve(pixelSamplerBindings.size());
            for (const std::uint32_t binding : pixelSamplerBindings)
            {
                VkDescriptorSetLayoutBinding b{};
                b.binding = binding;
                b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                b.descriptorCount = 1;
                b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                bindings.push_back(b);
            }
            VkDescriptorSetLayoutCreateInfo li{};
            li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            li.bindingCount = static_cast<uint32_t>(bindings.size());
            li.pBindings = bindings.empty() ? nullptr : bindings.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &layout) != VK_SUCCESS)
                throw std::runtime_error("vkCreateDescriptorSetLayout (compiled sampler) failed");
            layoutIt = compiledEffectSamplerSetLayouts_.emplace(signature, layout).first;
        }

        auto pipelineLayoutIt = compiledEffectPipelineLayouts_.find(signature);
        if (pipelineLayoutIt != compiledEffectPipelineLayouts_.end())
            return pipelineLayoutIt->second;

        const VkDescriptorSetLayout setLayouts[4] = {
            compiledEffectEmptySetLayout_,      // MOJOSHADER_SPIRV_VS_SAMPLER_SET
            compiledEffectUniformSetLayoutVS_,  // MOJOSHADER_SPIRV_VS_UNIFORM_SET
            layoutIt->second,                   // MOJOSHADER_SPIRV_PS_SAMPLER_SET
            compiledEffectUniformSetLayoutPS_,  // MOJOSHADER_SPIRV_PS_UNIFORM_SET
        };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 4;
        pli.pSetLayouts = setLayouts;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (compiled effect) failed");
        compiledEffectPipelineLayouts_.emplace(signature, pipelineLayout);
        return pipelineLayout;
    }

    VkPipeline VulkanRenderer::GetOrCreateCompiledEffectPipelineEXT(
        const Pending3DDraw& draw, uint32_t colorAttachmentCount, bool msaa,
        VkFormat targetDepthFmt)
    {
        const auto& compiled = draw.compiledEffect;
        VkPipelineLayout pipelineLayout =
            GetOrCreateCompiledEffectPipelineLayoutEXT(compiled.samplerBindings);

        // The shader pair's identity is part of the key alongside the usual render state: the
        // modules are recreated whenever the pass is re-linked for a different vertex layout, so a
        // new pair genuinely wants a new pipeline.
        const uint64_t stateKey = FoldDepthFormatIntoKey(
            MakeExt3DKey(draw.stride, draw.topology, draw.depthTest, draw.depthWrite, draw.blend,
                         draw.cullMode, colorAttachmentCount, draw.wireframe, msaa, draw.dsParams),
            targetDepthFmt);
        // MakeExt3DKey buckets the stride rather than carrying it (it folds 16 and every stride it
        // does not recognise into bucket 0), which is fine for the stock routes because a bucket
        // decides their whole vertex input. A compiled effect's vertex input comes from its own
        // declaration instead, so the raw stride has to be in the key: without it two draws of
        // different strides in the same bucket share a pipeline whose binding stride fits only the
        // first, and the second reads its vertices from the wrong offsets.
        PipelineKey key = { stateKey ^ (compiled.pass.pipelineKey * 1099511628211ull) ^
                                (static_cast<std::uint64_t>(draw.stride) * 0x9E3779B97F4A7C15ull),
                            PackBlendBits(draw.blend, draw.blendParams),
                            PackColorWriteBits(draw.blendParams), draw.NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1) };
        auto it = compiledEffectPipelines_.find(key);
        if (it != compiledEffectPipelines_.end()) return it->second;

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount =
            static_cast<uint32_t>(compiled.pass.vertexBindings.size());
        vis.pVertexBindingDescriptions = compiled.pass.vertexBindings.empty()
                                             ? nullptr : compiled.pass.vertexBindings.data();
        vis.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(compiled.pass.vertexAttributes.size());
        vis.pVertexAttributeDescriptions = compiled.pass.vertexAttributes.empty()
                                               ? nullptr : compiled.pass.vertexAttributes.data();

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT, compiled.pass.vertexModule,
                      compiled.pass.vertexEntryPoint, nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, compiled.pass.pixelModule,
                      compiled.pass.pixelEntryPoint, nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = draw.topology;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1;
        vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (draw.cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (draw.cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = draw.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode = vkCull;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        const VkSampleMask sampleMask = draw.NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (sampleMask != 0xFFFFFFFFu) ms.pSampleMask = &sampleMask;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = draw.depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = draw.depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, draw.dsParams);

        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u));
        for (uint32_t i = 0; i < cbaVec.size(); ++i)
            FillBlendAttachmentState(cbaVec[i], draw.blend, draw.blendParams, static_cast<int>(i));

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments = cbaVec.data();

        VkDynamicState dynStates[7] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, draw.blend, draw.blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pDepthStencilState = &ds;
        pci.pColorBlendState = &cbs;
        pci.pDynamicState = &dyn;
        pci.layout = pipelineLayout;
        pci.renderPass = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateGraphicsPipelines (compiled effect) failed");
        }
        compiledEffectPipelines_[key] = pipeline;
        return pipeline;
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateCompiledEffectSamplerSetEXT(
        uint32_t frameIdx, VkDescriptorSetLayout layout,
        const std::vector<std::uint32_t>& bindings, const std::vector<VkImageView>& views,
        const std::vector<VkSampler>& samplers)
    {
        std::uint64_t key = 0x9E3779B97F4A7C15ull;
        for (std::size_t i = 0; i < views.size(); ++i)
        {
            key ^= (reinterpret_cast<std::uint64_t>(views[i]) + 0x9E3779B97F4A7C15ull +
                    (key << 6) + (key >> 2)) *
                   (static_cast<std::uint64_t>(bindings[i]) + 1ull);
            key ^= (reinterpret_cast<std::uint64_t>(samplers[i]) * 1099511628211ull) +
                   (key << 5) + (key >> 3);
        }
        auto& cache = compiledEffectSamplerSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = compiledEffectDescriptorPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        // VULKAN-391: through the shared helper, as every other site now is.
        if (AllocateOneDescriptorSet(device_, ai, set) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateDescriptorSets (compiled sampler) failed");

        std::vector<VkDescriptorImageInfo> images(views.size());
        std::vector<VkWriteDescriptorSet> writes(views.size());
        for (std::size_t i = 0; i < views.size(); ++i)
        {
            images[i].sampler = samplers[i];
            images[i].imageView = views[i];
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[i] = {};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = bindings[i];
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        if (!writes.empty())
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(),
                                   0, nullptr);
        cache.emplace(key, set);
        return set;
    }

    void VulkanRenderer::RecordCompiledEffectDrawEXT(
        VkCommandBuffer cb, const Pending3DDraw& draw, uint32_t frameIdx)
    {
        // The pipeline itself, and every dynamic state the pipelines share, are bound by the
        // common replay path -- this records only what is specific to a compiled effect.
        const auto& compiled = draw.compiledEffect;
        VkPipelineLayout pipelineLayout =
            GetOrCreateCompiledEffectPipelineLayoutEXT(compiled.samplerBindings);

        // The uniform bytes were packed at apply time; copy them into this frame's own slice and
        // point the dynamic offsets at it. Each draw gets a slice of its own for the whole
        // recording -- chunks are appended as the cursor runs past them, because two draws sharing
        // a slice means the earlier one silently renders with the later one's constants.
        const uint32_t index = compiledEffectUBOCursor_[frameIdx]++;
        CompiledEffectUniformChunkEXT& chunk = EnsureCompiledEffectUniformChunkEXT(
            frameIdx, index / kCompiledEffectUBODrawsPerChunk);
        const uint32_t offset = (index % kCompiledEffectUBODrawsPerChunk) *
                                kCompiledEffectUBOStride;
        // PrepareCompiledEffectDrawEXT already refused a block that does not fit the stride, so
        // this copy is whole -- truncating one here would be exactly the silent wrong-pixels case
        // the refusal exists to prevent.
        if (!compiled.vertexUniforms.empty())
        {
            std::memcpy(static_cast<uint8_t*>(chunk.vsPtr) + offset,
                        compiled.vertexUniforms.data(), compiled.vertexUniforms.size());
        }
        if (!compiled.pixelUniforms.empty())
        {
            std::memcpy(static_cast<uint8_t*>(chunk.psPtr) + offset,
                        compiled.pixelUniforms.data(), compiled.pixelUniforms.size());
        }

        std::uint64_t samplerSignature = 0;
        for (const std::uint32_t binding : compiled.samplerBindings)
            if (binding < 64u) samplerSignature |= (1ull << binding);
        VkDescriptorSetLayout samplerLayout =
            compiledEffectSamplerSetLayouts_.at(samplerSignature);
        VkDescriptorSet samplerSet = GetOrCreateCompiledEffectSamplerSetEXT(
            frameIdx, samplerLayout, compiled.samplerBindings, compiled.samplerViews,
            compiled.samplers);

        const VkDescriptorSet sets[4] = {
            VK_NULL_HANDLE,       // set 0: vertex samplers, always empty
            chunk.vsSet,
            samplerSet,
            chunk.psSet,
        };
        // Set 0 has no descriptors, so it is never bound; sets 1..3 are bound as one range with
        // both dynamic offsets in the order Vulkan expects (ascending set, then ascending binding).
        const uint32_t dynamicOffsets[2] = { offset, offset };
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 3,
                                &sets[1], 2, dynamicOffsets);
        (void)sets[0];
    }
#endif  // CNA_VULKAN_COMPILED_EFFECTS

    VkPipeline VulkanRenderer::GetOrCreatePipelineFogColored3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureFogTex3DResources();

        // VULKAN-146: the binding stride is the buffer's now, not a constant 16 -- a
        // Position+Colour declaration padded to 32 runs this program too -- so it has to be part
        // of the key. Make3DKey carries no stride term; bits 53..63 are free in this cache.
        PipelineKey key = { FoldPerVertexStrideIntoKey(FoldDepthFormatIntoKey(Make3DKey(topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), stride), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: nothing to add here. This key ALREADY folds the raw stride, unconditionally,
        // and the fold is an XOR -- a second one would cancel the first and collapse two strides
        // onto one pipeline. Measured: adding it here turned four `VertexDeclarationLayoutTest`
        // cases red while `-R '^Vulkan_'` stayed green, which is F-21 all over again.
        auto it = pipelinesFogColored3D_.find(key);
        if (it != pipelinesFogColored3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = CreateShaderModule(kColored3dVertSpv, kColored3dVertSpv_size);
        VkShaderModule frag = CreateShaderModule(kColored3dFragSpv, kColored3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };
        attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };
        uint32_t attrCount = 2;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u));
        for (uint32_t i = 0; i < cbaVec.size(); ++i)
            FillBlendAttachmentState(
                cbaVec[i], blend, blendParams, static_cast<int>(i));

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        VkDynamicState dynStates[7] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutFogTex3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        pci.renderPass          = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass             = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (FogColored3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelinesFogColored3D_[key] = p;
        return p;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineFogTex3D(
        std::size_t stride, bool colored, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureFogTex3DResources();

        // VULKAN-158: this factory already binds the record's own stride; what it did not do is put
        // that stride in the KEY. MakeExt3DKey buckets an unlisted stride and the layout hash covers
        // offsets only, so two records with identical element offsets and different strides -- 64
        // and 68, say -- bucketed and hashed the same and shared one pipeline, whose binding stride
        // belonged to whichever of them drew first.
        const uint32_t recordStride = static_cast<uint32_t>(stride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesFogTex3D_.find(key);
        if (it != pipelinesFogTex3D_.end()) return it->second;

        using namespace Shaders;
        const uint32_t* vertSpv = nullptr; size_t vertSpvSize = 0;
        const uint32_t* fragSpv = nullptr; size_t fragSpvSize = 0;
        // VULKAN-146: `colored` rather than `stride == 24`. The caller decided the program from
        // the declaration; re-deriving it here from the stride would put the two out of step.
        if (colored) {
            vertSpv = kColoredTextured3dVertSpv;  vertSpvSize = kColoredTextured3dVertSpv_size;
            fragSpv = kColoredTextured3dFragSpv;  fragSpvSize = kColoredTextured3dFragSpv_size;
        } else {
            vertSpv = kTextured3dVertSpv;         vertSpvSize = kTextured3dVertSpv_size;
            fragSpv = kTextured3dFragSpv;         fragSpvSize = kTextured3dFragSpv_size;
        }
        VkShaderModule vert = CreateShaderModule(vertSpv, vertSpvSize);
        VkShaderModule frag = CreateShaderModule(fragSpv, fragSpvSize);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[3]{};
        uint32_t attrCount = 0;
        if (colored) {
            // float3 pos + ubyte4 color + float2 uv
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12};
            attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};
            attrCount = 3;
        } else {
            // float3 pos + float2 uv (stride 20)
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
            attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    12};
            attrCount = 2;
        }
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        std::vector<VkPipelineColorBlendAttachmentState> cbaVec(
            std::max(colorAttachmentCount, 1u));
        for (uint32_t i = 0; i < cbaVec.size(); ++i)
            FillBlendAttachmentState(
                cbaVec[i], blend, blendParams, static_cast<int>(i));

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = static_cast<uint32_t>(cbaVec.size());
        cbs.pAttachments    = cbaVec.data();

        VkDynamicState dynStates[7] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutFogTex3D_;
        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass() (which also keeps Task 904's msaa-aware fix above intact).
        pci.renderPass = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);
        pci.subpass = 0;

        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (FogTex3D variant) failed");

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);

        pipelinesFogTex3D_[key] = p;
        return p;
    }

    // ---- SkinnedEffect resources (Task 109) ----

    void VulkanRenderer::EnsureSkinnedResources()
    {
        if (descriptorSetLayoutSkinned_ != VK_NULL_HANDLE) return;

        // binding=0: sampler2D (fragment), binding=1: bone UBO dynamic (vertex),
        // binding=2: fog UBO dynamic (Task 899 -- BoneBlock has zero spare capacity, so fog gets
        // its own small dedicated dynamic UBO instead of being packed alongside the bones).
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (Skinned) failed");

        const uint32_t maxSets = 128u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets * 2 }; // BoneBlock + fog
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (Skinned) failed");

        // 128-byte PC (same Ext3D layout) + skinned descriptor set.
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutSkinned_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutSkinned3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (Skinned3D) failed");

        // Per-frame bone matrix UBO ring buffers.
        const VkDeviceSize uboSize = kSkinnedUBOStride * kSkinnedUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    skinnedUBO_[i], skinnedUBOMem_[i], &skinnedUBOPtr_[i]);
            }
        }
        // Per-frame fog UBO ring buffers (Task 899).
        const VkDeviceSize fogUboSize = kSkinnedFogUBOStride * kSkinnedFogUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (skinnedFogUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(fogUboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    skinnedFogUBO_[i], skinnedFogUBOMem_[i], &skinnedFogUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreateSkinnedDescSet(
        uint32_t frameIdx, VkImageView view2D, VkSampler sampler)
    {
        EnsureSkinnedResources();
        if (view2D == VK_NULL_HANDLE) view2D = defaultWhiteView_;

        // REMED-GFX-169: sampler identity is part of the key, not only of the contents.
        const uint64_t key = reinterpret_cast<uint64_t>(view2D)
                           ^ reinterpret_cast<uint64_t>(sampler) * 2246822519ULL;
        auto& cache = skinnedDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolSkinned_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutSkinned_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: SkinnedEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = sampler;
        VkSamplerTraceEXT("desc.Skinned     hit=0 key=0x%llx set=0x%llx "
                          "binding=0 slot=0 view=0x%llx sampler=0x%llx",
                          static_cast<unsigned long long>(key), VkH(ds),
                          VkH(view2D), VkH(imgInfo.sampler));
        imgInfo.imageView   = view2D;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // binding=1: dynamic UBO pointing to the whole per-frame bone ring buffer.
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = skinnedUBO_[frameIdx];
        bufInfo.offset = 0;
        bufInfo.range  = kSkinnedUBOStride;  // one bone palette block

        // binding=2: dynamic fog UBO (Task 899), extended for DirectionalLight1/2 (Task 893).
        VkDescriptorBufferInfo fogBufInfo{};
        fogBufInfo.buffer = skinnedFogUBO_[frameIdx];
        fogBufInfo.offset = 0;
        // fogColorEnabled+fogVector + light1/2 Dir/Diff (Task 893) + World/eyePos/specular (Task 894)
        // + emissiveColor vec4 (REMED-GFX-008) = 256 bytes; must cover offset 240 or the shader reads 0.
        fogBufInfo.range  = 256;

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfo;
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &fogBufInfo;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled view so this entry is evicted+freed when it dies.
        cache[key] = EffectDescSetEntry{ ds, { view2D, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineSkinned3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureSkinnedResources();

        // Task 11.10: this layout is independently duplicated (magic stride 52) in
        // EasyGLRenderer.cpp's ApplyLayout and BgfxRenderer.cpp's MakeBgfxLayout -
        // see EasyGLRenderer.cpp's own comment at its "case 52" for the full cross-
        // reference to the canonical VertexPositionNormalTextureSkinned::getVertexDeclarationStatic()
        // layout and why a shared-derivation refactor was investigated but deferred.
        // CNB-67: stride 56 is the same layout with a per-vertex Color (normalized ubyte4)
        // appended at offset 52 -- mirrors EasyGLRenderer.cpp's own "case 56" precedent
        // (locations 0-4 identical to stride 52; location 5 = aColor is new).
        // VULKAN-156: the reduction below is what made an unlistable stride silent. Refuse it
        // here too, not only at the draw, so the refusal cannot move to Present() -- VULKAN-148
        // learned that the hard way. A complete declaration says where each input lives and is
        // not held to the list.
        if (!vertexLayout.IsComplete() && stride != 52 && stride != 56)
            throw std::runtime_error(
                "Vulkan SkinnedEffect requires vertex stride 52 or 56, or a VertexDeclaration "
                "supplying every input of its shader");
        const std::size_t skinnedStride = (stride == 56) ? 56 : 52;
        const bool colored = (skinnedStride == 56);
        // VULKAN-158: the vertex BINDING's stride is the record's own, not this family's canonical
        // one, whenever the declaration supplied every input. Baking the constant made the
        // declaration-driven offsets right and the interval between records wrong -- the attributes
        // point at the correct bytes of a record the fetch never reaches. Without a declaration the
        // canonical constant is all there is, and the key and the binding stay byte-identical.
        const uint32_t recordStride = vertexLayout.IsComplete()
                                          ? static_cast<uint32_t>(stride)
                                          : static_cast<uint32_t>(skinnedStride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(skinnedStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesSkinned3D_.find(key);
        if (it != pipelinesSkinned3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = colored
            ? CreateShaderModule(kSkinned3dColorVertSpv, kSkinned3dColorVertSpv_size)
            : CreateShaderModule(kSkinned3dVertSpv,      kSkinned3dVertSpv_size);
        VkShaderModule frag = colored
            ? CreateShaderModule(kSkinned3dColorFragSpv, kSkinned3dColorFragSpv_size)
            : CreateShaderModule(kSkinned3dFragSpv,      kSkinned3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, recordStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[6]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }; // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 }; // aUV
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }; // aBoneWeights
        attrs[4] = { 4, 0, VK_FORMAT_R8G8B8A8_USCALED,    48 }; // aBoneIndices (VULKAN-151)
        uint32_t attrCount = 5;
        if (colored) {
            attrs[5] = { 5, 0, VK_FORMAT_R8G8B8A8_UNORM, 52 }; // aColor
            attrCount = 6;
        }

        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutSkinned3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesSkinned3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // Task 1103: PreferPerPixelLighting=false (XNA's real default) sibling of
    // GetOrCreatePipelineSkinned3D above -- same skinning/descriptor/pipeline layout, different
    // shader modules and pipeline cache only.
    VkPipeline VulkanRenderer::GetOrCreatePipelineSkinned3DVertexLit(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsureSkinnedResources();

        // CNB-67: see GetOrCreatePipelineSkinned3D's identical comment -- stride 56 selects the
        // per-vertex-color shader/attribute-layout variant.
        // VULKAN-156: the reduction below is what made an unlistable stride silent. Refuse it
        // here too, not only at the draw, so the refusal cannot move to Present() -- VULKAN-148
        // learned that the hard way. A complete declaration says where each input lives and is
        // not held to the list.
        if (!vertexLayout.IsComplete() && stride != 52 && stride != 56)
            throw std::runtime_error(
                "Vulkan SkinnedEffect requires vertex stride 52 or 56, or a VertexDeclaration "
                "supplying every input of its shader");
        const std::size_t skinnedStride = (stride == 56) ? 56 : 52;
        const bool colored = (skinnedStride == 56);
        // VULKAN-158: the vertex BINDING's stride is the record's own, not this family's canonical
        // one, whenever the declaration supplied every input. Baking the constant made the
        // declaration-driven offsets right and the interval between records wrong -- the attributes
        // point at the correct bytes of a record the fetch never reaches. Without a declaration the
        // canonical constant is all there is, and the key and the binding stay byte-identical.
        const uint32_t recordStride = vertexLayout.IsComplete()
                                          ? static_cast<uint32_t>(stride)
                                          : static_cast<uint32_t>(skinnedStride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(skinnedStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesSkinned3DVertexLit_.find(key);
        if (it != pipelinesSkinned3DVertexLit_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = colored
            ? CreateShaderModule(kSkinned3dVertexLitColorVertSpv, kSkinned3dVertexLitColorVertSpv_size)
            : CreateShaderModule(kSkinned3dVertexLitVertSpv,      kSkinned3dVertexLitVertSpv_size);
        VkShaderModule frag = colored
            ? CreateShaderModule(kSkinned3dVertexLitColorFragSpv, kSkinned3dVertexLitColorFragSpv_size)
            : CreateShaderModule(kSkinned3dVertexLitFragSpv,      kSkinned3dVertexLitFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, recordStride, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[6]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }; // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 }; // aUV
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }; // aBoneWeights
        attrs[4] = { 4, 0, VK_FORMAT_R8G8B8A8_USCALED,    48 }; // aBoneIndices (VULKAN-151)
        uint32_t attrCount = 5;
        if (colored) {
            attrs[5] = { 5, 0, VK_FORMAT_R8G8B8A8_UNORM, 52 }; // aColor
            attrCount = 6;
        }

        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutSkinned3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesSkinned3DVertexLit_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // ---- PbrEffect (unskinned, stride 48) / SkinnedPbrEffect (PBR + skinning combo, stride 68) ----
    // Metallic-roughness BRDF ported unchanged from EasyGLRenderer::EnsurePbrProgram()/
    // EnsurePbrSkinnedProgram() (pbr3d.frag.glsl/pbr3d_skinned.frag.glsl's own PbrLight()); only
    // the resource-binding plumbing (dynamic UBO instead of individual GL uniform locations)
    // differs, mirroring EnsureSkinnedResources()'s own sampler+dynamic-UBO shape but with 7
    // samplers (the five core maps plus specular strength and colour) instead of 1.

    void VulkanRenderer::EnsurePbrResources()
    {
        if (descriptorSetLayoutPbr_ != VK_NULL_HANDLE) return;

        VkDescriptorSetLayoutBinding bindings[8]{};
        for (uint32_t i = 0; i < 5; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[5].binding         = 5;
        bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        for (uint32_t i = 6; i < 8; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 8; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutPbr_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (Pbr) failed");

        const uint32_t maxSets = 128u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 7 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolPbr_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (Pbr) failed");

        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutPbr_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutPbr3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (Pbr3D) failed");

        const VkDeviceSize uboSize = kPbrUBOStride * kPbrUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (pbrUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    pbrUBO_[i], pbrUBOMem_[i], &pbrUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreatePbrDescSet(
        uint32_t frameIdx, VkImageView baseColor, VkImageView normalMap,
        VkImageView metallicRoughness, VkImageView emissive, VkImageView occlusion,
        VkImageView specular, VkImageView specularColor,
        const VkSampler (&samplers)[7])
    {
        EnsurePbrResources();
        if (baseColor          == VK_NULL_HANDLE) baseColor          = defaultWhiteView_;
        if (normalMap          == VK_NULL_HANDLE) normalMap          = defaultFlatNormalView_;
        if (metallicRoughness  == VK_NULL_HANDLE) metallicRoughness  = defaultWhiteView_;
        if (emissive           == VK_NULL_HANDLE) emissive           = defaultWhiteView_;
        if (occlusion          == VK_NULL_HANDLE) occlusion          = defaultWhiteView_;
        if (specular           == VK_NULL_HANDLE) specular           = defaultWhiteView_;
        if (specularColor      == VK_NULL_HANDLE) specularColor      = defaultWhiteView_;

        // FNV-1a-style combine of all 7 view handles into one cache key.
        uint64_t key = 1469598103934665603ull;
        for (VkImageView v : { baseColor, normalMap, metallicRoughness, emissive, occlusion,
                               specular, specularColor })
            key = (key ^ reinterpret_cast<uint64_t>(v)) * 1099511628211ull;
        // REMED-GFX-169: fold all seven slot samplers into the same FNV-1a chain.
        for (VkSampler sm : samplers)
            key = (key ^ reinterpret_cast<uint64_t>(sm)) * 1099511628211ull;
        auto& cache = pbrDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolPbr_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutPbr_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: PbrEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkImageView views[7] = { baseColor, normalMap, metallicRoughness, emissive, occlusion,
                                 specular, specularColor };
        VkDescriptorImageInfo imgInfo[7]{};
        for (uint32_t i = 0; i < 7; ++i)
            imgInfo[i] = { samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        for (uint32_t i = 0; i < 7; ++i)
            VkSamplerTraceEXT("desc.Pbr        hit=0 key=0x%llx set=0x%llx "
                              "binding=%u slot=%u view=0x%llx sampler=0x%llx",
                              static_cast<unsigned long long>(key), VkH(ds),
                              i < 5 ? i : i + 1, i, VkH(views[i]), VkH(imgInfo[i].sampler));

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = pbrUBO_[frameIdx];
        bufInfo.offset = 0;
        // The shader reads through byte 495 (specular transforms plus texture-coordinate selector).
        bufInfo.range  = sizeof(float) * 124;

        VkWriteDescriptorSet writes[8]{};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = ds;
        writes[5].dstBinding      = 5;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo     = &bufInfo;
        for (uint32_t i = 5; i < 7; ++i) {
            writes[i + 1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i + 1].dstSet          = ds;
            writes[i + 1].dstBinding      = i + 1;
            writes[i + 1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i + 1].descriptorCount = 1;
            writes[i + 1].pImageInfo      = &imgInfo[i];
        }
        vkUpdateDescriptorSets(device_, 8, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled views so this entry is evicted+freed when any dies.
        cache[key] = EffectDescSetEntry{ ds, { baseColor, normalMap, metallicRoughness, emissive,
                                               occlusion, specular, specularColor } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelinePbr3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsurePbrResources();

        // VULKAN-148: the stride list judges a buffer with NO declaration. One that supplies every
        // input of the shader this stride selects says where they are, whatever the stride.
        if (!vertexLayout.IsComplete() && stride != 48 && stride != 60)
            throw std::runtime_error("Vulkan PbrEffect requires vertex stride 48 or 60");
        const bool dualUv = stride == 60;
        // VULKAN-158: this factory already binds the record's own stride; what it did not do is put
        // that stride in the KEY. MakeExt3DKey buckets an unlisted stride and the layout hash covers
        // offsets only, so two records with identical element offsets and different strides -- 64
        // and 68, say -- bucketed and hashed the same and shared one pipeline, whose binding stride
        // belonged to whichever of them drew first.
        const uint32_t recordStride = static_cast<uint32_t>(stride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesPbr3D_.find(key);
        if (it != pipelinesPbr3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = dualUv
            ? CreateShaderModule(kPbr3dDualUvVertSpv, kPbr3dDualUvVertSpv_size)
            : CreateShaderModule(kPbr3dVertSpv, kPbr3dVertSpv_size);
        VkShaderModule frag = dualUv
            ? CreateShaderModule(kPbr3dDualUvFragSpv, kPbr3dDualUvFragSpv_size)
            : CreateShaderModule(kPbr3dFragSpv, kPbr3dFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[6]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }; // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24 }; // aTangent
        attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,       40 }; // aUV
        if (dualUv)
        {
            attrs[4] = { 4, 0, VK_FORMAT_R32G32_SFLOAT,   48 }; // aUV1
            // plans/plan_gltf.md GLTF-462/GLTF-465: stride 60 always carries a packed COLOR_0 here --
            // InferredLayoutForStride(60) has a Color element at 56 whether the primitive authored
            // one or the importer wrote the opaque-white identity, so the attribute is
            // unconditional for this variant and the shader's own uVertexColorEnabled decides
            // whether it multiplies.
            attrs[5] = { 5, 0, VK_FORMAT_R8G8B8A8_UNORM,  56 }; // aColor
        }

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        uint32_t attrCount = dualUv ? 6u : 4u;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount;
        vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutPbr3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesPbr3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    void VulkanRenderer::EnsurePbrSkinnedResources()
    {
        if (descriptorSetLayoutPbrSkinned_ != VK_NULL_HANDLE) return;

        // Bindings 0-4: core samplers; binding 5: bone palette dynamic UBO; binding 6: PbrParams
        // dynamic UBO; bindings 7-8: specular strength and colour samplers.
        VkDescriptorSetLayoutBinding bindings[9]{};
        for (uint32_t i = 0; i < 5; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[5].binding         = 5;
        bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[6].binding         = 6;
        bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        for (uint32_t i = 7; i < 9; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 9; li.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayoutPbrSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout (PbrSkinned) failed");

        const uint32_t maxSets = 128u * MaxFramesInFlight;
        VkDescriptorPoolSize ps[2]{};
        ps[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 7 };
        ps[1] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets * 2 }; // BoneBlock + PbrParams
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // REMED-GFX-076: allow individual vkFreeDescriptorSets so a set evicted when its sampled
        // view dies is returned to the pool (bounded memory), not leaked until teardown.
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = maxSets;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPoolPbrSkinned_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool (PbrSkinned) failed");

        VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayoutPbrSkinned_;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutPbrSkinned3D_) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout (PbrSkinned3D) failed");

        const VkDeviceSize boneUboSize = kPbrSkinnedBoneUBOStride * kPbrSkinnedBoneUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (pbrSkinnedBoneUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(boneUboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    pbrSkinnedBoneUBO_[i], pbrSkinnedBoneUBOMem_[i], &pbrSkinnedBoneUBOPtr_[i]);
            }
        }
        const VkDeviceSize uboSize = kPbrSkinnedUBOStride * kPbrSkinnedUBOMaxDraws;
        for (uint32_t i = 0; i < MaxFramesInFlight; ++i) {
            if (pbrSkinnedUBO_[i] == VK_NULL_HANDLE) {
                CreateBuffer(uboSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    pbrSkinnedUBO_[i], pbrSkinnedUBOMem_[i], &pbrSkinnedUBOPtr_[i]);
            }
        }
    }

    VkDescriptorSet VulkanRenderer::GetOrCreatePbrSkinnedDescSet(
        uint32_t frameIdx, VkImageView baseColor, VkImageView normalMap,
        VkImageView metallicRoughness, VkImageView emissive, VkImageView occlusion,
        VkImageView specular, VkImageView specularColor,
        const VkSampler (&samplers)[7])
    {
        EnsurePbrSkinnedResources();
        if (baseColor          == VK_NULL_HANDLE) baseColor          = defaultWhiteView_;
        if (normalMap          == VK_NULL_HANDLE) normalMap          = defaultFlatNormalView_;
        if (metallicRoughness  == VK_NULL_HANDLE) metallicRoughness  = defaultWhiteView_;
        if (emissive           == VK_NULL_HANDLE) emissive           = defaultWhiteView_;
        if (occlusion          == VK_NULL_HANDLE) occlusion          = defaultWhiteView_;
        if (specular           == VK_NULL_HANDLE) specular           = defaultWhiteView_;
        if (specularColor      == VK_NULL_HANDLE) specularColor      = defaultWhiteView_;

        uint64_t key = 1469598103934665603ull;
        for (VkImageView v : { baseColor, normalMap, metallicRoughness, emissive, occlusion,
                               specular, specularColor })
            key = (key ^ reinterpret_cast<uint64_t>(v)) * 1099511628211ull;
        // REMED-GFX-169: fold all seven slot samplers into the same FNV-1a chain.
        for (VkSampler sm : samplers)
            key = (key ^ reinterpret_cast<uint64_t>(sm)) * 1099511628211ull;
        auto& cache = pbrSkinnedDescSets_[frameIdx];
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.set;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descriptorPoolPbrSkinned_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &descriptorSetLayoutPbrSkinned_;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        // VULKAN-391: one contract for every descriptor allocation in this renderer -- a caller
        // never receives VK_NULL_HANDLE, because the only thing a caller could do with one is bind
        // it, and binding a null descriptor set is a segfault (measured under VULKAN-390's
        // mutation probe). Refused BY NAME at the draw, which is where this runs, rather than at
        // Present. Routed through AllocateOneDescriptorSet so the test-only injection hook can
        // reach this arm at all -- it could not before, which is why this arm had no test.
        if (AllocateOneDescriptorSet(device_, ai, ds) != VK_SUCCESS)
            throw std::runtime_error(
                "The Vulkan renderer: SkinnedPbrEffect\'s descriptor pool is full and the device "
                "refused another set. Refused rather than binding a null descriptor set.");

        VkImageView views[7] = { baseColor, normalMap, metallicRoughness, emissive, occlusion,
                                 specular, specularColor };
        VkDescriptorImageInfo imgInfo[7]{};
        for (uint32_t i = 0; i < 7; ++i)
            imgInfo[i] = { samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        for (uint32_t i = 0; i < 7; ++i)
            VkSamplerTraceEXT("desc.PbrSkinned hit=0 key=0x%llx set=0x%llx "
                              "binding=%u slot=%u view=0x%llx sampler=0x%llx",
                              static_cast<unsigned long long>(key), VkH(ds),
                              i < 5 ? i : i + 2, i, VkH(views[i]), VkH(imgInfo[i].sampler));

        VkDescriptorBufferInfo boneBufInfo{};
        boneBufInfo.buffer = pbrSkinnedBoneUBO_[frameIdx];
        boneBufInfo.offset = 0;
        boneBufInfo.range  = kPbrSkinnedBoneUBOStride;

        VkDescriptorBufferInfo paramsBufInfo{};
        paramsBufInfo.buffer = pbrSkinnedUBO_[frameIdx];
        paramsBufInfo.offset = 0;
        paramsBufInfo.range  = sizeof(float) * 124;

        VkWriteDescriptorSet writes[9]{};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &imgInfo[i];
        }
        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = ds;
        writes[5].dstBinding      = 5;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo     = &boneBufInfo;
        writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = ds;
        writes[6].dstBinding      = 6;
        writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo     = &paramsBufInfo;
        for (uint32_t i = 5; i < 7; ++i) {
            writes[i + 2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i + 2].dstSet          = ds;
            writes[i + 2].dstBinding      = i + 2;
            writes[i + 2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i + 2].descriptorCount = 1;
            writes[i + 2].pImageInfo      = &imgInfo[i];
        }
        vkUpdateDescriptorSets(device_, 9, writes, 0, nullptr);

        // REMED-GFX-076: record the sampled views so this entry is evicted+freed when any dies.
        cache[key] = EffectDescSetEntry{ ds, { baseColor, normalMap, metallicRoughness, emissive,
                                               occlusion, specular, specularColor } };
        return ds;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelinePbrSkinned3D(
        std::size_t stride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt, const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        EnsurePbrSkinnedResources();

        // VULKAN-148: same rule as GetOrCreatePipelinePbr3D's -- the stride list judges a buffer
        // with no declaration.
        if (!vertexLayout.IsComplete() && stride != 68 && stride != 76 && stride != 80)
            throw std::runtime_error("Vulkan SkinnedPbrEffect requires vertex stride 68, 76 or 80");
        // plans/plan_gltf.md GLTF-463: stride 80 is stride 76's record with a packed COLOR_0 appended, so
        // it is a dual-UV layout that additionally binds a colour.
        const bool dualUv  = stride == 76 || stride == 80;
        const bool colored = stride == 80;
        // VULKAN-158: this factory already binds the record's own stride; what it did not do is put
        // that stride in the KEY. MakeExt3DKey buckets an unlisted stride and the layout hash covers
        // offsets only, so two records with identical element offsets and different strides -- 64
        // and 68, say -- bucketed and hashed the same and shared one pipeline, whose binding stride
        // belonged to whichever of them drew first.
        const uint32_t recordStride = static_cast<uint32_t>(stride);
        PipelineKey key = { FoldDepthFormatIntoKey(MakeExt3DKey(stride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        // VULKAN-158: and the record stride reaches the key, so two declarations that differ only in
        // stride cannot share a pipeline. Folded only when the layout is complete, which leaves
        // every stride-derived key exactly as it was.
        if (vertexLayout.IsComplete()) key.a = FoldPerVertexStrideIntoKey(key.a, recordStride);
        auto it = pipelinesPbrSkinned3D_.find(key);
        if (it != pipelinesPbrSkinned3D_.end()) return it->second;

        using namespace Shaders;
        VkShaderModule vert = colored
            ? CreateShaderModule(kPbr3dSkinnedDualUvColorVertSpv, kPbr3dSkinnedDualUvColorVertSpv_size)
            : dualUv
            ? CreateShaderModule(kPbr3dSkinnedDualUvVertSpv, kPbr3dSkinnedDualUvVertSpv_size)
            : CreateShaderModule(kPbr3dSkinnedVertSpv, kPbr3dSkinnedVertSpv_size);
        VkShaderModule frag = colored
            ? CreateShaderModule(kPbr3dSkinnedDualUvColorFragSpv, kPbr3dSkinnedDualUvColorFragSpv_size)
            : dualUv
            ? CreateShaderModule(kPbr3dSkinnedDualUvFragSpv, kPbr3dSkinnedDualUvFragSpv_size)
            : CreateShaderModule(kPbr3dSkinnedFragSpv, kPbr3dSkinnedFragSpv_size);

        VkVertexInputBindingDescription bind{ 0, static_cast<uint32_t>(stride), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[8]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }; // aPos
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }; // aNormal
        attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24 }; // aTangent
        attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,       40 }; // aUV
        attrs[4] = { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48 }; // aBoneWeights
        attrs[5] = { 5, 0, VK_FORMAT_R8G8B8A8_USCALED,    64 }; // aBoneIndices (VULKAN-151)
        if (dualUv)
            attrs[6] = { 6, 0, VK_FORMAT_R32G32_SFLOAT,   68 }; // aUV1
        if (colored)
            attrs[7] = { 7, 0, VK_FORMAT_R8G8B8A8_UNORM,  76 }; // aColor

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        uint32_t attrCount = colored ? 8u : dualUv ? 7u : 6u;
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);
        vis.vertexBindingDescriptionCount   = 1; vis.pVertexBindingDescriptions   = &bind;
        vis.vertexAttributeDescriptionCount = attrCount;
        vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(ds, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutPbrSkinned3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesPbrSkinned3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    VkPipeline VulkanRenderer::GetOrCreatePipelineInstanced3D(
        std::size_t pvStride, VkPrimitiveTopology topo,
        bool depthTest, bool depthWrite, bool blend, int cullMode,
        uint32_t colorAttachmentCount, bool wireframe, bool msaa,
        const DepthStencilKeyParams& dsParams, const BlendKeyParams& blendParams, VkFormat targetDepthFmt,
        const VulkanVertexInputLayoutEXT& vertexLayout)
    {
        // Ensure pipelineLayoutExt3D_ exists (128-byte PC + 1 descriptor set for future texture use).
        if (pipelineLayoutExt3D_ == VK_NULL_HANDLE) {
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcRange;
            pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorSetLayout_;
            if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayoutExt3D_) != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout (Ext3D/Instanced) failed");
        }

        // REMED-GFX-212: the exact per-vertex stride, not MakeExt3DKey's bucket -- see
        // FoldPerVertexStrideIntoKey. VertexColorEnabled itself is deliberately NOT in the key:
        // it travels in the push constant (FillInstancedPushConst's pc[31]), exactly as it does
        // for the ordinary colored3d pipeline, so toggling it never creates a pipeline variant.
        PipelineKey key = { FoldPerVertexStrideIntoKey(FoldDepthFormatIntoKey(MakeExt3DKey(pvStride, topo, depthTest, depthWrite, blend, cullMode, colorAttachmentCount, wireframe, msaa, dsParams), targetDepthFmt), pvStride), PackBlendBits(blend, blendParams), PackColorWriteBits(blendParams), NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1), vertexLayout.Hash() };
        auto it = pipelinesInstanced3D_.find(key);
        if (it != pipelinesInstanced3D_.end()) return it->second;

        using namespace Shaders;
        // REMED-GFX-212: the geometry stride's own packed layout selects the vertex shader, the
        // same way the ordinary route picks colored3d/textured3d/colored_textured3d by stride.
        uint32_t packedColorOffset = 0;
        // VULKAN-149: the declaration decides this when it supplied every input of one of the two
        // programs, and the shape is read back off the layout it produced -- one attribute is
        // instanced3d, two is instanced_colored3d. There is no third shape, so nothing else can
        // be meant. Without a usable declaration the stride table answers, exactly as before.
        const bool hasPackedColor = vertexLayout.IsComplete()
            ? vertexLayout.attributeCount == std::size(StockInputs::kInstancedColored)
            : PackedColorOffsetForStride(pvStride, packedColorOffset);
        VkShaderModule vert = hasPackedColor
            ? CreateShaderModule(kInstancedColored3dVertSpv, kInstancedColored3dVertSpv_size)
            : CreateShaderModule(kInstanced3dVertSpv, kInstanced3dVertSpv_size);
        // Task 899: dedicated FS (was: reuse kColored3dFragSpv) -- colored3d.frag.glsl now
        // declares a 2nd descriptor binding (fog UBO) as part of the shared colored3d/textured3d/
        // colored_textured3d bundle, incompatible with Instanced3D's unmodified 1-binding layout.
        // Both VS variants emit the same single `location = 0` vec4, so they share it unchanged.
        VkShaderModule frag = CreateShaderModule(kInstanced3dFragSpv, kInstanced3dFragSpv_size);

        // Two vertex bindings: binding=0 per-vertex (VERTEX rate), binding=1 per-instance (INSTANCE rate).
        constexpr uint32_t kInstStride = 64; // sizeof(mat4)
        VkVertexInputBindingDescription binds[2]{};
        binds[0] = { 0, static_cast<uint32_t>(pvStride), VK_VERTEX_INPUT_RATE_VERTEX   };
        binds[1] = { 1, kInstStride,                      VK_VERTEX_INPUT_RATE_INSTANCE };

        VkVertexInputAttributeDescription attrs[6]{};
        uint32_t attrCount = 0;
        attrs[attrCount++] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }; // aPos (per-vertex)
        // REMED-GFX-212: the geometry stream's own COLOR0, at its own stride's offset. The
        // per-instance columns keep locations 4..7, so this can never collide with them.
        if (hasPackedColor)
            attrs[attrCount++] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, packedColorOffset }; // aColor
        // VULKAN-149: and the declaration's own offsets and formats replace the two above when it
        // supplied them. Applied here, before the per-instance columns are appended, because the
        // applicator overwrites from index 0 and resets the count -- binding 1's attributes are
        // not declaration-derived and must survive it.
        ApplyDeclaredVertexLayoutEXT(vertexLayout, attrs, std::size(attrs), attrCount);
        attrs[attrCount++] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0  }; // aInstCol0 (per-instance)
        attrs[attrCount++] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 }; // aInstCol1
        attrs[attrCount++] = { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }; // aInstCol2
        attrs[attrCount++] = { 7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 }; // aInstCol3

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 2; vis.pVertexBindingDescriptions   = binds;
        vis.vertexAttributeDescriptionCount = attrCount; vis.pVertexAttributeDescriptions = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT,   vert, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkPipelineViewportStateCreateInfo vpst{};
        vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpst.viewportCount = 1; vpst.scissorCount = 1;

        VkCullModeFlags vkCull = VK_CULL_MODE_NONE;
        if (cullMode == 1) vkCull = VK_CULL_MODE_FRONT_BIT;
        if (cullMode == 2) vkCull = VK_CULL_MODE_BACK_BIT;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode    = vkCull;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth   = 1.f;
        rs.depthBiasEnable = VK_TRUE;  // dynamic; values set via vkCmdSetDepthBias per draw

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = msaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
        // REMED-GFX-077: BlendState.MultiSampleMask (static pipeline state; the pointer is valid
        // until vkCreateGraphicsPipelines below). Only set for a non-default mask, so the common
        // case stays byte-identical (pSampleMask==nullptr == Vulkan's all-ones default).
        // VULKAN-162: narrowed to the samples this pipeline has, and compared in that same
        // space -- so a fully-set mask still leaves pSampleMask null, which is Vulkan's own
        // default, and what the KEY says can never drift from what the pipeline does.
        const VkSampleMask cnaAllSamples_ = NarrowSampleMaskEXT(0xFFFFFFFFu, msaa ? SampleCountToInt(sampleCount_) : 1);
        const VkSampleMask cnaSampleMask_ = NarrowSampleMaskEXT(blendParams.sampleMask, msaa ? SampleCountToInt(sampleCount_) : 1);
        if (cnaSampleMask_ != cnaAllSamples_) ms.pSampleMask = &cnaSampleMask_;

        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.depthTestEnable  = depthTest  ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        FillDepthStencilState(dss, dsParams);

        const uint32_t nColor = std::max(colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(nColor);
        for (size_t bi = 0; bi < blendAttachments.size(); ++bi) { auto& ba = blendAttachments[bi];
            // Task 868: real per-BlendState mapping, replacing the previous hardcoded
            // BlendState.NonPremultiplied-equivalent equation applied whenever blend was true.
            FillBlendAttachmentState(ba, blend, blendParams, static_cast<int>(bi)); // REMED-GFX-077: per-MRT-slot write mask
        }
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = nColor; cbs.pAttachments = blendAttachments.data();

        VkDynamicState dynStates[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        const uint32_t dynStateCount =
            AppendBlendConstantsDynamicState(dynStates, 6, blend, blendParams);
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = dynStateCount; dyn.pDynamicStates = dynStates;

        // Task 911: render pass selected per the target's own real depth format -- see
        // PickRTPipelineRenderPass().
        VkRenderPass rp = PickRTPipelineRenderPass(colorAttachmentCount, msaa, targetDepthFmt);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2; pci.pStages          = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vpst;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &dss;
        pci.pColorBlendState    = &cbs;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayoutExt3D_;
        pci.renderPass          = rp;

        VkPipeline pipe = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
        pipelinesInstanced3D_[key] = pipe;

        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return pipe;
    }

    // Task 899: GetOrCreatePipelineExt3D (textured3d/colored_textured3d via the OLD, plain
    // pipelineLayoutExt3D_/descriptorSetLayout_) was removed here -- BasicEffect draws for
    // stride 20/24 now exclusively use GetOrCreatePipelineFogTex3D (the new fog-capable bundle)
    // instead, so this function had become unreachable dead code, AND would have failed
    // vkCreateGraphicsPipelines validation if ever called (its shaders -- kTextured3dVertSpv/
    // kColoredTextured3dVertSpv -- now declare a 2nd descriptor binding for fog that
    // pipelineLayoutExt3D_'s original 1-binding descriptorSetLayout_ does not provide).
    // pipelineLayoutExt3D_/descriptorSetLayout_ themselves are unchanged and still used by
    // Instanced3D and 2D SpriteBatch, per this task's explicit requirement.

    // =========================================================================
    // Memory / resource helpers
    // =========================================================================

    uint32_t VulkanRenderer::FindMemoryType(uint32_t typeBits,
                                                    VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("Vulkan: FindMemoryType failed");
    }

    void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkMemoryPropertyFlags props,
                                              VkBuffer& buf, VkDeviceMemory& mem, void** mapped)
    {
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size  = size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &ci, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("vkCreateBuffer failed");

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device_, buf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits, props);
        if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory failed");
        vkBindBufferMemory(device_, buf, mem, 0);

        if (mapped)
            vkMapMemory(device_, mem, 0, size, 0, mapped);
    }

    VkCommandBuffer VulkanRenderer::BeginOneTimeCommands()
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &ai, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        return cb;
    }

    void VulkanRenderer::EndOneTimeCommands(VkCommandBuffer cb)
    {
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
        // plan_vulkan.md VULKAN-396: the wait is timed rather than argued about. It is here
        // because the command buffer is freed on the next line and freeing one still executing is
        // illegal -- so the question this instrumentation answers is not "why is it here" but
        // "what does it cost", which no amount of reading can settle.
        const auto waitBegin = std::chrono::steady_clock::now();
        vkQueueWaitIdle(graphicsQueue_);
        oneTimeCommandWaitNanosEXT_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - waitBegin).count());
        ++oneTimeCommandCountEXT_;
        vkFreeCommandBuffers(device_, commandPool_, 1, &cb);
    }

    // plan_vulkan.md VULKAN-401: the RECORDING half, so a caller performing several steps on one
    // image can put them in one command buffer and pay one queue wait instead of one per step.
    void VulkanRenderer::RecordImageLayoutTransition(VkCommandBuffer cb, VkImage img,
                                                     VkImageLayout from, VkImageLayout to,
                                                     uint32_t baseMipLevel)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = img;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, baseMipLevel, 1, 0, 1 };

        VkPipelineStageFlags srcStage, dstStage;
        if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            // Needed by VulkanTextureRenderer::UpdatePixels to re-upload a texture that has
            // already been sampled at least once (i.e. every SetData call after the first).
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            // Task 865: needed by VulkanTexture3DRenderer/VulkanTextureCubeRenderer::GetData to
            // read back a texture that has already been sampled at least once.
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            // Task 865: restores sampling readiness after GetData's readback copy.
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::runtime_error("Vulkan: unsupported image layout transition");
        }
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // The standalone form every other caller still uses: one transition, one submit, one wait.
    void VulkanRenderer::TransitionImageLayout(VkImage img,
                                                       VkImageLayout from, VkImageLayout to,
                                                       uint32_t baseMipLevel)
    {
        VkCommandBuffer cb = BeginOneTimeCommands();
        RecordImageLayoutTransition(cb, img, from, to, baseMipLevel);
        EndOneTimeCommands(cb);
    }

    // VULKAN-401: the recording half of the copy, for the same reason.
    void VulkanRenderer::RecordBufferToImageCopy(VkCommandBuffer cb, VkBuffer buf, VkImage img,
                                                 uint32_t w, uint32_t h)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { w, h, 1 };
        vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    void VulkanRenderer::CopyBufferToImage(VkBuffer buf, VkImage img,
                                                   uint32_t w, uint32_t h)
    {
        VkCommandBuffer cb = BeginOneTimeCommands();
        RecordBufferToImageCopy(cb, buf, img, w, h);
        EndOneTimeCommands(cb);
    }

    // =========================================================================
    // IGraphicsRenderer implementation
    // =========================================================================

    void VulkanRenderer::Clear(float r, float g, float b, float a)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        readbackStagingValid_ = false;  // new frame content invalidates the readback cache
        // Task 875: mark the currently-bound RT as needing its render pass recorded this frame,
        // even if no draw call follows.
        NoteRenderTargetClearEXT(true, false, false);
    }

    void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex,
                                                    RecordMode mode, VulkanRTSource* onlyRT,
                                                    const std::vector<uint64_t>* flushSegments)
    {
        // REMED-GFX-074: RenderTargetsOnly records off-screen passes for a GetData readback flush --
        // Phase 2 (backbuffer) and the backbuffer readback are skipped, and only what this record
        // actually emitted is consumed at the end (see the cleanup below).
        const bool rtOnly = (mode == RecordMode::RenderTargetsOnly);
        (void)onlyRT;
        // REMED-GFX-151: what a RenderTargetsOnly flush records. Every site that decides what to
        // record, what to reset and what to consume must use the SAME predicate, or an entry
        // recorded here would be replayed again at Present (double-render) or dropped without ever
        // being recorded (lost work).
        //
        // This used to be "the depth/stencil group of the target being read" (REMED-GFX-142's
        // refinement of REMED-GFX-074's "this target"), which is a filter on the DESTINATION of a
        // draw. A target's observable content also depends on every render target its draws SAMPLE,
        // and a producer is a different target, so the producer's pass was filtered out and the
        // consumer sampled an image nothing had rendered into -- REMED-GFX-151.
        //
        // FlushDeferredRenderTarget now computes the exact transitive set of bind cycles the
        // readback depends on and hands it over; this is simply membership in that set. Replay
        // order is still ascending segment id, i.e. exactly the public order
        // (BeginRenderPassSegmentEXT advances the counter on every bind), so REMED-GFX-140's
        // one-pass-per-bind-cycle, REMED-GFX-142's whole-cube-group rule and REMED-GFX-143's
        // ascending-id stream are all preserved.
        auto recordedByFlush = [flushSegments](const VulkanRTSource* rt, uint64_t segment) {
            return rt != nullptr && flushSegments != nullptr &&
                   std::find(flushSegments->begin(), flushSegments->end(), segment)
                       != flushSegments->end();
        };
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed");

        // Task 447/854: reset every OcclusionQuery pool actually tagged on a pending draw this
        // frame, before any render pass begins (vkCmdResetQueryPool must not be called inside a
        // render pass instance). Replaces the old "reset once at construction only" bug -- a
        // query reused across frames (the real, idiomatic XNA usage pattern: Begin()/End() called
        // once per Draw()) now gets a fresh reset every time it's actually used, not just the
        // first. recordedThisFrame_ is cleared here too, so draw3DFor's own contiguous-run
        // tracking starts fresh each frame.
        {
            std::vector<VulkanOcclusionQueryRenderer*> queriesThisFrame;
            for (const auto& draw : pending3D_) {
                // REMED-GFX-074: in a RenderTargetsOnly readback flush only the draws this record
                // actually emits are recorded, so only their queries are reset/recorded here; every
                // other query is left untouched for the real Present(). REMED-GFX-151: which draws
                // those are is now the positional predicate, matching the segment filter below.
                if (rtOnly && !recordedByFlush(draw.rt.get(), draw.segment)) continue;
                if (draw.occlusionQuery &&
                    std::find(queriesThisFrame.begin(), queriesThisFrame.end(), draw.occlusionQuery)
                        == queriesThisFrame.end())
                    queriesThisFrame.push_back(draw.occlusionQuery);
            }
            for (auto* q : queriesThisFrame) {
                q->recordedThisFrame_ = false;
                if (q->pool_ != VK_NULL_HANDLE)
                    vkCmdResetQueryPool(cb, q->pool_, 0, 1);
            }
        }

        // REMED-GFX-013: build the dynamic VkRect2D scissor for one draw/batch from its captured
        // XNA scissor state (see Pending3DDraw / BatchSnapshot) and the physical extent of the
        // framebuffer it targets. Disabled or degenerate (zero-sized) → whole framebuffer, matching
        // the backbuffer pass's own long-standing `scissorEnabled_ && scissorW_>0 && scissorH_>0`
        // guard. Otherwise the captured rectangle is clamped to the framebuffer in 64-bit space so
        // the resulting VkRect2D always satisfies Vulkan's requirements (offset ≥ 0, offset+extent
        // within the framebuffer, no signed/unsigned overflow) even for rectangles that begin
        // outside or overhang the target edges.
        auto computeScissor = [](bool enabled, int32_t sx, int32_t sy, uint32_t sw, uint32_t sh,
                                 uint32_t fbW, uint32_t fbH) -> VkRect2D {
            if (!enabled || sw == 0 || sh == 0)
                return VkRect2D{ {0, 0}, { fbW, fbH } };
            const int64_t x0 = std::clamp<int64_t>(sx, 0, static_cast<int64_t>(fbW));
            const int64_t y0 = std::clamp<int64_t>(sy, 0, static_cast<int64_t>(fbH));
            const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(sx) + sw, 0, static_cast<int64_t>(fbW));
            const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(sy) + sh, 0, static_cast<int64_t>(fbH));
            VkRect2D r{};
            r.offset.x      = static_cast<int32_t>(x0);
            r.offset.y      = static_cast<int32_t>(y0);
            r.extent.width  = static_cast<uint32_t>(x1 > x0 ? x1 - x0 : 0);
            r.extent.height = static_cast<uint32_t>(y1 > y0 ? y1 - y0 : 0);
            return r;
        };

        // REMED-GFX-062: convert a captured CNA Viewport into a VkViewport for this pass. Unlike
        // computeScissor, the rectangle is NOT clamped to the framebuffer -- a viewport is the
        // NDC->framebuffer transform, and clamping x/y/w/h would distort where geometry lands
        // (a sub-region viewport within the target is already valid, and D3D/XNA pass an
        // overhanging viewport straight through; Vulkan's viewportBoundsRange is far larger than
        // any framebuffer here). Positive height is deliberate: CNA flips Y in the vertex shader
        // (REMED-GFX-011, `pos.y = -pos.y`), so the viewport must stay top-left/positive-height
        // and XNA Viewport.Y maps directly to VkViewport.y with no additional flip. minDepth/
        // maxDepth are clamped to [0,1] to satisfy VkViewport's VUIDs (XNA's own valid range).
        // set==false or a degenerate (zero-sized) rect falls back to the full physical target,
        // byte-identical to the pre-fix hardcoded full-target viewport.
        auto computeViewport = [](bool set, int32_t vx, int32_t vy, uint32_t vw, uint32_t vh,
                                  float minD, float maxD, uint32_t fbW, uint32_t fbH) -> VkViewport {
            VkViewport v{};
            if (!set || vw == 0 || vh == 0) {
                v.x = 0.0f; v.y = 0.0f;
                v.width  = static_cast<float>(fbW);
                v.height = static_cast<float>(fbH);
                v.minDepth = 0.0f; v.maxDepth = 1.0f;
                return v;
            }
            v.x = static_cast<float>(vx);
            v.y = static_cast<float>(vy);
            v.width  = static_cast<float>(vw);
            v.height = static_cast<float>(vh);
            v.minDepth = std::clamp(minD, 0.0f, 1.0f);
            v.maxDepth = std::clamp(maxD, 0.0f, 1.0f);
            return v;
        };

        // Helper: draw all 2D batches for a specific RT (nullptr = backbuffer) into current render pass.
        // Sprite VB/IB ring buffers are shared across all passes in a frame — each snapshot is
        // memcpy'd/bound at its own running byte offset (vbOff/ibOff below), mirroring draw3DFor's
        // already-correct accumulating-cursor pattern, so multiple Begin()/End() cycles (or
        // multiple SpriteBatch instances) targeting the same RT in one frame compose additively
        // instead of overwriting each other at a hardcoded offset 0 (Task 664 fix).
        // REMED-GFX-140: the sprite and 3D arenas are ONE host-visible allocation per frame in
        // flight, shared by every pass this command buffer records, and every memcpy into them
        // happens while recording -- long before the queue submit that makes the GPU read them. A
        // cursor that restarted at 0 for each pass therefore let a later pass overwrite the exact
        // bytes an earlier pass had already bound, so the earlier pass drew the later pass's
        // geometry (test P6). It was invisible before this task because the only multi-pass
        // fixtures drew identical rectangles and varied the source texture, which lives in the
        // per-draw descriptor set rather than in the arena. Hoisting the cursors to the whole
        // record makes every pass own a disjoint range; segmentation makes that mandatory, since
        // one target's two bind cycles are now two passes over the same arena.
        VkDeviceSize spriteVbCursor = 0;
        VkDeviceSize spriteIbCursor = 0;
        // REMED-GFX-129: `afterOrder`/`beforeOrder` bound this call to the half-open slice of the
        // segment's batches that lies between two ordered Clear() commands. A segment with no clear
        // is replayed in one call with the full (0, UINT64_MAX) range, which selects exactly what
        // the unbounded version selected. The arena cursors are NOT reset per call (see above), and
        // the slices are disjoint and cover the whole segment, so every batch is still copied once,
        // at the same offset, in the same order.
        auto drawSpritesFor = [&](VulkanRTSource* targetRT, uint64_t segment,
                                  float vpW, float vpH,
                                  uint64_t afterOrder, uint64_t beforeOrder)
        {
            static constexpr VkDeviceSize kSpriteVBSize = MaxSpriteVertices * sizeof(Sprite2DVertex);
            static constexpr VkDeviceSize kSpriteIBSize = MaxSpriteIndices  * sizeof(uint16_t);
            VkPipeline   lastBoundPipeline = VK_NULL_HANDLE;
            VkDeviceSize& vbOff = spriteVbCursor;
            VkDeviceSize& ibOff = spriteIbCursor;
            for (auto& entry : activeBatches_) {
                const auto& snapshot = entry.snapshot;
                VulkanRTSource* batchRT = entry.rt.get();
                if (batchRT != targetRT) continue;
                // REMED-GFX-140/143: one pass per bind cycle, backbuffer cycles included, so a
                // batch belongs to exactly the segment it was issued in. There is no longer any
                // "replay everything for this target" mode.
                if (entry.segment != segment) continue;
                if (entry.order <= afterOrder || entry.order >= beforeOrder) continue;
                const auto& verts = snapshot->vertices;
                const auto& inds  = snapshot->indices;
                const auto& draws = snapshot->draws;
                if (verts.empty() || draws.empty()) continue;

                const VkDeviceSize vbBytes = verts.size() * sizeof(Sprite2DVertex);
                const VkDeviceSize ibBytes = inds.size()  * sizeof(uint16_t);
                if (vbOff + vbBytes > kSpriteVBSize || ibOff + ibBytes > kSpriteIBSize) continue;

                std::memcpy(static_cast<uint8_t*>(spriteVBPtr_[currentFrame_]) + vbOff,
                            verts.data(), vbBytes);
                std::memcpy(static_cast<uint8_t*>(spriteIBPtr_[currentFrame_]) + ibOff,
                            inds.data(), ibBytes);

                // Select pipeline: custom SPIR-V effect (Task 119) or built-in 2D.
                // Task 903 finding: this previously only checked the BACKBUFFER's own MSAA state
                // (targetRT == nullptr && sampleCount_ > 1), never an RT's -- meaning a SpriteBatch
                // fill into an MSAA-enabled RenderTarget2D/RenderTargetCube face always bound the
                // non-MSAA pipeline against the RT's real 3-attachment MSAA render pass, a genuine
                // render-pass-compatibility validation error (VUID-vkCmdDraw-multisampledRenderTo
                // SingleSampled-07284/VUID-vkCmdDraw-renderPass-02684), invisible until this task's
                // own RenderTargetCube MSAA test was the first to fill an MSAA target via
                // SpriteBatch (every prior MSAA RT test used a 3D BasicEffect fill instead, whose
                // own pipeline selection already checks rt->WantsMsaa() correctly). Fixed to check
                // the actual bound target's WantsMsaa() when targeting an RT, same as the 3D path.
                const bool targetWantsMsaa = (targetRT == nullptr) ? (sampleCount_ > VK_SAMPLE_COUNT_1_BIT)
                                                                    : targetRT->WantsMsaa();
                const bool useMsaaPipe = targetWantsMsaa && (sampleCount_ > VK_SAMPLE_COUNT_1_BIT);
                if (targetRT && targetRT->GetColorAttachmentCount() > 1) {
                    lastMrtPipelineColorCountEXT_ = targetRT->GetColorAttachmentCount();
                    lastMrtPipelineSampleCountEXT_ =
                        useMsaaPipe ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
                }
                // Task 911: this target's own real depth VkFormat -- see draw3DFor's identical
                // targetDepthFmt computation for the full rationale.
                const VkFormat targetDepthFmt = targetRT ? targetRT->GetDepthFormat() : depthFormat_;
                // REMED-GFX-071: select the 2D pipeline whose colour-attachment blend equation
                // matches this batch's captured BlendState (defaults to Opaque for a batch that
                // predates any BlendState, but SpriteBatch.Begin always applies one).
                const uint32_t colorAttachmentCount =
                    targetRT ? targetRT->GetColorAttachmentCount() : 1u;
                VkPipeline       activePipe   = useMsaaPipe
                    ? GetOrCreatePipeline2DMsaa(
                        targetDepthFmt, colorAttachmentCount,
                        snapshot->blendEnabled, snapshot->blendParams)
                    : GetOrCreatePipeline2D(
                        targetDepthFmt, colorAttachmentCount,
                        snapshot->blendEnabled, snapshot->blendParams);
                VkPipelineLayout activeLayout = pipelineLayout2D_;
                const float*     customPC     = nullptr;
                // REMED-GFX-075: read the effect's pipeline/layout/push-constants from the batch
                // snapshot (captured by value at End()), never from the effect wrapper -- it may have
                // been disposed since. The pipeline handle is kept alive by the effect's retirement.
                if (snapshot->hasCustomEffect && snapshot->customPipeline != VK_NULL_HANDLE) {
                    activePipe   = snapshot->customPipeline;
                    activeLayout = snapshot->customLayout;
                    customPC     = snapshot->customPushConst;
                }

                if (activePipe != lastBoundPipeline) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipe);
                    lastBoundPipeline = activePipe;
                }
                VkDeviceSize vbBindOff = vbOff;
                vkCmdBindVertexBuffers(cb, 0, 1, &spriteVB_[currentFrame_], &vbBindOff);
                vkCmdBindIndexBuffer(cb, spriteIB_[currentFrame_], ibOff, VK_INDEX_TYPE_UINT16);

                // REMED-GFX-072: the sprite2d vertex shader divides pixel-space positions by this
                // vpSize to reach NDC. XNA/FNA build the SpriteBatch ortho from GraphicsDevice.
                // Viewport.Width/Height (CreateOrthographicOffCenter(0, Viewport.Width,
                // Viewport.Height, 0), FNA SpriteBatch.cs PrepRenderState), so a custom sub-Viewport
                // makes sprite coordinates VIEWPORT-LOCAL: the divide must use the active Viewport's
                // W/H, not the full target/virtual size (vpW/vpH). The rasterizer viewport
                // (computeViewport below, GFX-062) already positions the [-1,1] result at Viewport.
                // X/Y, so this is the missing projection half. Only override for a genuine custom
                // sub-region (differs from the physical target extent) -- the default full-target
                // viewport keeps the pre-existing vpW/vpH (byte-identical, and preserves the
                // backbuffer's virtual-resolution divisor). The sprite vertices are already the raw
                // viewport-local pixel coordinates the game passed (Draw() bakes dest.X/Y directly),
                // so only the divisor changes; the transform matrix (GFX-012) is applied CPU-side
                // before this and is unaffected.
                float projW = vpW, projH = vpH;
                {
                    const uint32_t physW = targetRT ? static_cast<uint32_t>(targetRT->GetWidth())
                                                    : swapchainExtent_.width;
                    const uint32_t physH = targetRT ? static_cast<uint32_t>(targetRT->GetHeight())
                                                    : swapchainExtent_.height;
                    if (snapshot->viewportSet && snapshot->viewportW > 0 && snapshot->viewportH > 0 &&
                        (snapshot->viewportX != 0 || snapshot->viewportY != 0 ||
                         snapshot->viewportW != physW || snapshot->viewportH != physH)) {
                        projW = static_cast<float>(snapshot->viewportW);
                        projH = static_cast<float>(snapshot->viewportH);
                    }
                }
                float vpSize[2] = { projW, projH };
                if (customPC) {
                    // Push 128-byte block: vpSize at [0..7], std140 padding at [8..15],
                    // and user uniforms at [16..127].
                    float fullPC[32];
                    std::memcpy(fullPC,     vpSize,      8);
                    std::memcpy(fullPC + 2, customPC + 2, 120);
                    vkCmdPushConstants(cb, activeLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, fullPC);
                } else {
                    vkCmdPushConstants(cb, pipelineLayout2D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, vpSize);
                }

                // REMED-GFX-013: apply this batch's captured scissor, clamped to the target's
                // PHYSICAL extent (not vpW/vpH, which may be the virtual-resolution size).
                {
                    const uint32_t fbW = targetRT ? static_cast<uint32_t>(targetRT->GetWidth())
                                                  : swapchainExtent_.width;
                    const uint32_t fbH = targetRT ? static_cast<uint32_t>(targetRT->GetHeight())
                                                  : swapchainExtent_.height;
                    VkRect2D bsc = computeScissor(snapshot->scissorEnabled,
                                                  snapshot->scissorX, snapshot->scissorY,
                                                  snapshot->scissorW, snapshot->scissorH, fbW, fbH);
                    vkCmdSetScissor(cb, 0, 1, &bsc);
                    // REMED-GFX-062: apply this batch's captured viewport (full target when unset),
                    // so a SpriteBatch fill honors a custom Viewport in RT passes too, not just the
                    // backbuffer. Byte-identical for every existing batch (all run under the full
                    // target viewport SetRenderTarget resets to).
                    VkViewport bvp = computeViewport(snapshot->viewportSet,
                                                     snapshot->viewportX, snapshot->viewportY,
                                                     snapshot->viewportW, snapshot->viewportH,
                                                     snapshot->viewportMinDepth,
                                                     snapshot->viewportMaxDepth, fbW, fbH);
                    // plan_vulkan.md VULKAN-330: the SpriteBatch twin of the 3D replay's own
                    // adjustment -- a backbuffer batch with no viewport of its own presents into
                    // the presented rectangle. Both sites need it; changing only one would
                    // letterbox 3D draws while sprites kept the whole image.
                    if (!targetRT &&
                        !(snapshot->viewportSet && snapshot->viewportW > 0 && snapshot->viewportH > 0))
                    {
                        int px = 0, py = 0, pw = 0, ph = 0;
                        GetPresentedRectEXT(px, py, pw, ph);
                        bvp.x = static_cast<float>(px); bvp.y = static_cast<float>(py);
                        bvp.width  = static_cast<float>(pw);
                        bvp.height = static_cast<float>(ph);
                    }
                    vkCmdSetViewport(cb, 0, 1, &bvp);
                    // REMED-GFX-070/GFX-091: replay the batch's captured RGBA value only when the
                    // selected pipeline's static blend equation consumes it. Custom Effect
                    // variants use the same BlendState translation and declaration predicate.
                    if (UsesBlendConstants(snapshot->blendEnabled, snapshot->blendParams)) {
                        const float bbc[4] = { snapshot->blendFactorR, snapshot->blendFactorG,
                                               snapshot->blendFactorB, snapshot->blendFactorA };
                        vkCmdSetBlendConstants(cb, bbc);
                    }
                }

                for (const auto& d : draws) {
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        activeLayout, 0, 1, &d.descSet, 0, nullptr);
                    VkLifetimeTraceEXT("record.sprite    order=%llu family=SpriteBatch rt=%p "
                                       "seg=%llu boundSet=0x%llx indices=%u",
                                       static_cast<unsigned long long>(entry.order),
                                       static_cast<const void*>(batchRT),
                                       static_cast<unsigned long long>(entry.segment),
                                       VkH(d.descSet), d.indexCount);
                    vkCmdDrawIndexed(cb, d.indexCount, 1, d.firstIndex, 0, 0);
                }

                vbOff += vbBytes;
                ibOff += ibBytes;
            }
        };

        // UBO slot counters (reset once per frame, shared across all RT passes).
        uint32_t envMapUBOSlot  = 0;
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        compiledEffectUBOCursor_[currentFrame_] = 0;
#endif
        uint32_t skinnedUBOSlot = 0;
        uint32_t litTexturedUBOSlot = 0;
        uint32_t fogTex3DUBOSlot    = 0; // Task 899: colored3d/textured3d/colored_textured3d
        uint32_t dualTexFogUBOSlot  = 0; // Task 899: DualTextureEffect
        uint32_t skinnedFogUBOSlot  = 0; // Task 899: SkinnedEffect
        uint32_t pbrUBOSlot         = 0; // PbrEffect (unskinned)
        uint32_t pbrSkinnedBoneUBOSlot = 0; // SkinnedPbrEffect bone palette
        uint32_t pbrSkinnedUBOSlot  = 0; // SkinnedPbrEffect PbrParams

        // Helper: draw all pending 3D draws for a specific RT into the current render pass.
        VkDeviceSize frame3DVbCursor     = 0;
        VkDeviceSize frame3DIbCursor     = 0;
        VkDeviceSize frame3DInstVbCursor = 0;
        // Task 447/854: the OcclusionQuery currently wrapped in a real vkCmdBeginQuery for the
        // render pass being recorded, if any -- tracks contiguous runs of draws sharing the same
        // occlusionQuery tag so they land in one vkCmdBeginQuery/vkCmdEndQuery pair.
        // REMED-GFX-129 hoisted it out of draw3DFor: a segment holding an ordered Clear() replays
        // its draws in several calls, and a query left open by one slice must stay open across the
        // clear rather than being ended and re-begun on the same pool index, which is invalid usage.
        VulkanOcclusionQueryRenderer* openQuery3D = nullptr;
        auto closeOpenQuery3D = [&]()
        {
            if (openQuery3D && openQuery3D->pool_ != VK_NULL_HANDLE) {
                vkCmdEndQuery(cb, openQuery3D->pool_, 0);
                openQuery3D->recordedThisFrame_ = true;
            }
            openQuery3D = nullptr;
        };
        // REMED-GFX-129: see drawSpritesFor for what afterOrder/beforeOrder mean.
        auto draw3DFor = [&](VulkanRTSource* targetRT, uint64_t segment,
                             uint64_t afterOrder, uint64_t beforeOrder)
        {
            if (pending3D_.empty()) return;
            EnsureFrame3DBuffers();
            VkPipeline lastPipe   = VK_NULL_HANDLE;
            // See drawSpritesFor's cursor comment: one arena, many passes, one record.
            VkDeviceSize& vbOff     = frame3DVbCursor;
            VkDeviceSize& ibOff     = frame3DIbCursor;
            VkDeviceSize& instVbOff = frame3DInstVbCursor;
            VulkanOcclusionQueryRenderer*& openQuery = openQuery3D;
            for (const auto& draw : pending3D_) {
                if (draw.rt.get() != targetRT) continue;
                if (draw.segment != segment) continue;
                if (draw.order <= afterOrder || draw.order >= beforeOrder) continue;
                if (draw.isMarker) {
                    if (pfnCmdInsertDebugLabel_) {
                        VkDebugUtilsLabelEXT lbl{};
                        lbl.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                        lbl.pLabelName = draw.markerLabel.c_str();
                        lbl.color[0]   = 1.0f; lbl.color[1] = 1.0f;
                        lbl.color[2]   = 1.0f; lbl.color[3] = 1.0f;
                        pfnCmdInsertDebugLabel_(cb, &lbl);
                    }
                    continue;
                }
                if (draw.vbData.empty()) continue;
                VkDeviceSize nativeIbOff = ibOff;
                if (!draw.ibData.empty()) {
                    const VkDeviceSize indexAlignment =
                        draw.indexType == VK_INDEX_TYPE_UINT32
                            ? sizeof(uint32_t)
                            : sizeof(uint16_t);
                    nativeIbOff =
                        (ibOff + indexAlignment - 1) & ~(indexAlignment - 1);
                }
                if (vbOff + draw.vbData.size() > kFrame3DVBSize) continue;
                if (!draw.ibData.empty()
                    && nativeIbOff + draw.ibData.size() > kFrame3DIBSize) continue;
                if (draw.useInstanced && instVbOff + draw.instVbData.size() > kFrame3DInstVBSize) continue;

                // Task 447/854: this draw is definitely about to be recorded -- open/close real
                // vkCmdBeginQuery/vkCmdEndQuery pairs around contiguous runs of draws sharing the
                // same occlusionQuery tag. A query already recorded once this frame (an earlier
                // contiguous run, in this render pass or an earlier one) is deliberately NOT
                // reopened -- Vulkan requires a reset between two vkCmdBeginQuery calls on the
                // same query, and re-resetting mid-frame would corrupt the first run's own
                // in-flight result; this implements the approved "reject additional spans beyond
                // the first contiguous run" policy.
                if (draw.occlusionQuery != openQuery) {
                    if (openQuery && openQuery->pool_ != VK_NULL_HANDLE) {
                        vkCmdEndQuery(cb, openQuery->pool_, 0);
                        openQuery->recordedThisFrame_ = true;
                    }
                    openQuery = nullptr;
                    if (draw.occlusionQuery && draw.occlusionQuery->pool_ != VK_NULL_HANDLE
                        && !draw.occlusionQuery->recordedThisFrame_) {
                        // VULKAN-370: PRECISE only where the feature was enabled -- passing the
                        // bit without it is a usage error, and the query would answer "any"
                        // rather than a count either way.
                        vkCmdBeginQuery(cb, draw.occlusionQuery->pool_, 0,
                                        occlusionQueryPreciseSupported_
                                            ? VK_QUERY_CONTROL_PRECISE_BIT
                                            : 0);
                        openQuery = draw.occlusionQuery;
                    }
                }

                std::memcpy(static_cast<uint8_t*>(frame3DVBPtr_[currentFrame_]) + vbOff,
                            draw.vbData.data(), draw.vbData.size());
                if (!draw.ibData.empty()) {
                    // REMED-GFX-112: each deferred draw keeps exact logical bytes/counts. Only
                    // its placement in the shared native arena is padded so vkCmdBindIndexBuffer's
                    // offset remains aligned to that draw's VkIndexType.
                    if (nativeIbOff > ibOff)
                        std::memset(
                            static_cast<uint8_t*>(frame3DIBPtr_[currentFrame_]) + ibOff,
                            0,
                            static_cast<std::size_t>(nativeIbOff - ibOff));
                    std::memcpy(static_cast<uint8_t*>(frame3DIBPtr_[currentFrame_]) + nativeIbOff,
                                draw.ibData.data(), draw.ibData.size());
                }
                if (draw.useInstanced && !draw.instVbData.empty())
                    std::memcpy(static_cast<uint8_t*>(frame3DInstVBPtr_[currentFrame_]) + instVbOff,
                                draw.instVbData.data(), draw.instVbData.size());

                // REMED-GFX-013: apply this draw's captured scissor, clamped to the target's
                // physical extent (RT dimensions, or the swapchain for the backbuffer).
                {
                    const uint32_t fbW = targetRT ? static_cast<uint32_t>(targetRT->GetWidth())
                                                  : swapchainExtent_.width;
                    const uint32_t fbH = targetRT ? static_cast<uint32_t>(targetRT->GetHeight())
                                                  : swapchainExtent_.height;
                    VkRect2D dsc = computeScissor(draw.scissorEnabled,
                                                  draw.scissorX, draw.scissorY,
                                                  draw.scissorW, draw.scissorH, fbW, fbH);
                    vkCmdSetScissor(cb, 0, 1, &dsc);
                    // REMED-GFX-062: apply this draw's captured viewport (full target when unset),
                    // so a custom Viewport set while a render target was bound is honored in the RT
                    // pass, not just the backbuffer. Byte-identical for every draw issued under the
                    // full target viewport SetRenderTarget resets to.
                    VkViewport dvp = computeViewport(draw.viewportSet,
                                                     draw.viewportX, draw.viewportY,
                                                     draw.viewportW, draw.viewportH,
                                                     draw.viewportMinDepth, draw.viewportMaxDepth,
                                                     fbW, fbH);
                    // plan_vulkan.md VULKAN-330: for a BACKBUFFER draw that set no viewport of its
                    // own, "full target" is the presented rectangle rather than the whole swapchain
                    // image. Render targets keep their full extent -- a presentation mode describes
                    // how the virtual resolution reaches the WINDOW, not how a game's own targets
                    // are laid out. Under the three non-scaling modes the helper returns the full
                    // extent, so this leaves those byte-identical.
                    if (!targetRT && !(draw.viewportSet && draw.viewportW > 0 && draw.viewportH > 0))
                    {
                        int px = 0, py = 0, pw = 0, ph = 0;
                        GetPresentedRectEXT(px, py, pw, ph);
                        dvp.x = static_cast<float>(px); dvp.y = static_cast<float>(py);
                        dvp.width  = static_cast<float>(pw);
                        dvp.height = static_cast<float>(ph);
                    }
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
                    // plans/plan_fx.md FX-065: Vulkan's clip space has Y pointing down where D3D9's and
                    // OpenGL's point up, and every stock shader here compensates with an explicit
                    // `pos.y = -pos.y` (see colored3d.vert.glsl). A MojoShader-translated D3D9
                    // shader carries no such line and cannot be asked to, so the flip moves to the
                    // viewport for this draw -- a negative height is core Vulkan 1.1, which is the
                    // version this renderer requests. Without it a compiled effect renders
                    // vertically mirrored against every other draw the renderer issues.
                    if (draw.useCompiledEffect) {
                        dvp.y += dvp.height;
                        dvp.height = -dvp.height;
                    }
#endif
                    vkCmdSetViewport(cb, 0, 1, &dvp);
                }

                const uint32_t nColor = targetRT ? targetRT->GetColorAttachmentCount() : 1u;
                // Task 878/879: MSAA-aware for RT passes too, not just the backbuffer -- an RT
                // draw uses the MSAA pipeline variant when this specific RT actually engaged
                // MSAA (VulkanRTSource::WantsMsaa(), true only when the renderer itself has MSAA
                // infrastructure AND the RT requested it; see the "piggyback on sampleCount_"
                // scope decision in plans/plan_graphics.md).
                const bool drawMsaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) &&
                                      (targetRT == nullptr || targetRT->WantsMsaa());
                if (targetRT && targetRT->GetColorAttachmentCount() > 1) {
                    lastMrtPipelineColorCountEXT_ = targetRT->GetColorAttachmentCount();
                    lastMrtPipelineSampleCountEXT_ =
                        drawMsaa ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
                }
                // Task 911: this target's own real depth VkFormat -- the backbuffer's
                // device-wide depthFormat_ when drawing into the swapchain (no VulkanRTSource),
                // else the specific RenderTarget2D/RenderTargetCube instance's own picked format
                // (VK_FORMAT_UNDEFINED for DepthFormat::None). Threaded into every pipeline-cache
                // lookup below so a distinct depth format gets its own pipeline/render pass (see
                // PickRTPipelineRenderPass()).
                const VkFormat targetDepthFmt = targetRT ? targetRT->GetDepthFormat() : depthFormat_;
                VkPipeline pipe;
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
                // plans/plan_fx.md FX-065: a compiled Effect brings its own linked SPIR-V pair, vertex
                // input layout and descriptor layout, so it selects a pipeline of its own instead
                // of any of the stock stride-dispatched ones below.
                if (draw.useCompiledEffect) {
                    pipe = GetOrCreateCompiledEffectPipelineEXT(draw, nColor, drawMsaa,
                                                                targetDepthFmt);
                } else
#endif
                if (draw.useAlphaTest) {
                    pipe = GetOrCreatePipelineAlphaTest3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useDualTexture) {
                    pipe = GetOrCreatePipelineDualTex3D(draw.stride, draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useEnvMap) {
                    pipe = GetOrCreatePipelineEnvMap3D(draw.stride, draw.topology,
                                                       draw.depthTest, draw.depthWrite,
                                                       draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useSkinned) {
                    // Task 1103: real XNA default is PreferPerPixelLighting=false (per-vertex/
                    // Gouraud lighting) -- select that sibling pipeline unless the effect asked
                    // for per-pixel lighting explicitly. CNB-67: draw.stride (52 or 56) also
                    // selects the plain/vertex-color shader+attribute-layout variant.
                    pipe = draw.preferVertexLit
                           ? GetOrCreatePipelineSkinned3DVertexLit(draw.stride, draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout)
                           : GetOrCreatePipelineSkinned3D(draw.stride, draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.usePbrSkinned) {
                    pipe = GetOrCreatePipelinePbrSkinned3D(draw.stride, draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.usePbr) {
                    pipe = GetOrCreatePipelinePbr3D(draw.stride, draw.topology,
                                                        draw.depthTest, draw.depthWrite,
                                                        draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useInstanced) {
                    pipe = GetOrCreatePipelineInstanced3D(draw.stride, draw.topology,
                                                          draw.depthTest, draw.depthWrite,
                                                          draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useLitTextured) {
                    // Task 1103: same rationale as useSkinned above.
                    pipe = draw.preferVertexLit
                           ? GetOrCreatePipelineLitTextured3DVertexLit(draw.topology,
                                                            draw.depthTest, draw.depthWrite,
                                                            draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout)
                           : GetOrCreatePipelineLitTextured3D(draw.topology,
                                                            draw.depthTest, draw.depthWrite,
                                                            draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else if (draw.useFogTex3D) {
                    // Task 899: colored3d / textured3d / colored_textured3d fog-capable bundle.
                    // VULKAN-146: which of the three is `draw.basicShape`, decided at draw time
                    // from the stride AND the declaration -- the stride alone cannot tell a
                    // Position+Colour vertex padded to 32 from a lit one (REMED-GFX-234). A draw
                    // whose shape is None (an unconverted stride, or the legacy no-GpuDrawParams
                    // path, which never sets useFogTex3D) keeps the old stride dispatch.
                    const bool colouredShape =
                        draw.basicShape == BasicProgramShapeEXT::Colored
                        || (draw.basicShape == BasicProgramShapeEXT::None && draw.stride == 16);
                    pipe = colouredShape
                           ? GetOrCreatePipelineFogColored3D(draw.stride, draw.topology,
                                                             draw.depthTest, draw.depthWrite,
                                                             draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout)
                           : GetOrCreatePipelineFogTex3D(draw.stride,
                                                         draw.basicShape == BasicProgramShapeEXT::None
                                                             ? draw.stride == 24
                                                             : draw.basicShape == BasicProgramShapeEXT::ColoredTextured,
                                                         draw.topology,
                                                         draw.depthTest, draw.depthWrite,
                                                         draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt, draw.vertexLayout);
                } else {
                    pipe = GetOrCreatePipeline3D(draw.topology,
                                                 draw.depthTest, draw.depthWrite,
                                                 draw.blend, draw.cullMode, nColor, draw.wireframe, drawMsaa, draw.dsParams, draw.blendParams, targetDepthFmt);
                }
                if (pipe != lastPipe) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    lastPipe = pipe;
                }
                // REMED-GFX-070/GFX-091: blend factors/functions are static and already selected
                // by `pipe`; only a constant-dependent equation declares the RGBA value dynamic.
                // Bind first, then replay this draw's by-value snapshot. Emitting this command for
                // an ordinary static pipeline is invalid (VUID-vkCmdDraw-None-08608).
                if (UsesBlendConstants(draw.blend, draw.blendParams)) {
                    const float dbc[4] = { draw.blendFactorR, draw.blendFactorG,
                                           draw.blendFactorB, draw.blendFactorA };
                    vkCmdSetBlendConstants(cb, dbc);
                }
                // All 3D pipelines declare VK_DYNAMIC_STATE_DEPTH_BIAS, so the dynamic
                // depth bias must be set before each draw. Zero values = no bias.
                vkCmdSetDepthBias(cb, draw.depthBias, 0.0f, draw.slopeScaleDepthBias);
                // Task 870: stencil reference/compare mask/write mask are true Vulkan dynamic
                // state -- every 3D pipeline declares these 3 dynamic states now, so they must be
                // set before each draw regardless of whether StencilEnable is currently true
                // (matches vkCmdSetDepthBias's own always-set convention just above).
                vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                           static_cast<uint32_t>(draw.stencilReadMask));
                vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                         static_cast<uint32_t>(draw.stencilWriteMask));
                vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                                         static_cast<uint32_t>(draw.referenceStencil));
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
                if (draw.useCompiledEffect) {
                    RecordCompiledEffectDrawEXT(cb, draw, currentFrame_);
                } else
#endif
                if (draw.useAlphaTest) {
                    vkCmdPushConstants(cb, pipelineLayoutAlphaTest3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    VkDescriptorSet ds = (draw.descSet != VK_NULL_HANDLE)
                                         ? draw.descSet : defaultWhiteDescSet_;
                    if (ds != VK_NULL_HANDLE)
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutAlphaTest3D_, 0, 1, &ds, 0, nullptr);
                } else if (draw.useDualTexture) {
                    vkCmdPushConstants(cb, pipelineLayoutDualTex3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // Task 899: dualTexDescSet's layout now has a 3rd binding (dynamic fog UBO),
                    // so no defaultWhiteDescSet_ fallback here anymore (that set belongs to the
                    // structurally-incompatible 1-binding descriptorSetLayout_).
                    if (draw.dualTexDescSet != VK_NULL_HANDLE && dualTexFogUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = dualTexFogUBOSlot++;
                        const uint32_t uboOff = slot * kDualTexFogUBOStride;
                        if (uboOff + 32 <= kDualTexFogUBOStride * kDualTexFogUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(dualTexFogUBOPtr_[currentFrame_]) + uboOff,
                                        draw.dualTexFogUboData, 32);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutDualTex3D_, 0, 1,
                                                &draw.dualTexDescSet, 1, &uboOff);
                    }
                } else if (draw.useEnvMap) {
                    vkCmdPushConstants(cb, pipelineLayoutEnvMap3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.envMapPC);
                    if (draw.envMapDescSet != VK_NULL_HANDLE && envMapUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = envMapUBOSlot++;
                        const uint32_t uboOff = slot * kEnvMapUBOStride;
                        if (uboOff + 192 <= kEnvMapUBOStride * kEnvMapUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(envMapUBOPtr_[currentFrame_]) + uboOff,
                                        draw.envMapUboData, 192);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutEnvMap3D_, 0, 1,
                                                &draw.envMapDescSet, 1, &uboOff);
                    }
                } else if (draw.useSkinned) {
                    vkCmdPushConstants(cb, pipelineLayoutSkinned3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    if (draw.skinnedDescSet != VK_NULL_HANDLE && skinnedUBOPtr_[currentFrame_]
                        && !draw.boneMatrices.empty()) {
                        const uint32_t boneSlot = skinnedUBOSlot++;
                        const uint32_t boneOff  = boneSlot * kSkinnedUBOStride;
                        if (boneOff + kSkinnedUBOStride <= kSkinnedUBOStride * kSkinnedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(skinnedUBOPtr_[currentFrame_]) + boneOff,
                                        draw.boneMatrices.data(),
                                        draw.boneMatrices.size() * sizeof(float));
                        }
                        // Task 899: 2nd dynamic UBO (fog, binding=2). vkCmdBindDescriptorSets'
                        // pDynamicOffsets are consumed in ascending-binding-number order of the
                        // set's dynamic bindings, i.e. [BoneBlock@1, FogParams@2].
                        uint32_t fogOff = 0;
                        if (skinnedFogUBOPtr_[currentFrame_]) {
                            const uint32_t fogSlot = skinnedFogUBOSlot++;
                            fogOff = fogSlot * kSkinnedFogUBOStride;
                            // REMED-GFX-008: 256 bytes now (64 floats — added emissiveColor vec4 at
                            // offset 240, filling the 256-byte stride exactly).
                            if (fogOff + 256 <= kSkinnedFogUBOStride * kSkinnedFogUBOMaxDraws) {
                                std::memcpy(static_cast<uint8_t*>(skinnedFogUBOPtr_[currentFrame_]) + fogOff,
                                            draw.skinnedFogUboData, 256);
                            }
                        }
                        const uint32_t dynOffsets[2] = { boneOff, fogOff };
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutSkinned3D_, 0, 1,
                                                &draw.skinnedDescSet, 2, dynOffsets);
                    }
                } else if (draw.usePbrSkinned) {
                    vkCmdPushConstants(cb, pipelineLayoutPbrSkinned3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    if (draw.pbrDescSet != VK_NULL_HANDLE && pbrSkinnedBoneUBOPtr_[currentFrame_]
                        && pbrSkinnedUBOPtr_[currentFrame_] && !draw.boneMatrices.empty()) {
                        const uint32_t boneSlot = pbrSkinnedBoneUBOSlot++;
                        const uint32_t boneOff  = boneSlot * kPbrSkinnedBoneUBOStride;
                        if (boneOff + kPbrSkinnedBoneUBOStride <= kPbrSkinnedBoneUBOStride * kPbrSkinnedBoneUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(pbrSkinnedBoneUBOPtr_[currentFrame_]) + boneOff,
                                        draw.boneMatrices.data(),
                                        draw.boneMatrices.size() * sizeof(float));
                        }
                        // Dynamic offsets consumed in ascending-binding order: [BoneBlock@5, PbrParams@6].
                        uint32_t pbrOff = 0;
                        const uint32_t pbrSlot = pbrSkinnedUBOSlot++;
                        pbrOff = pbrSlot * kPbrSkinnedUBOStride;
                        if (pbrOff + sizeof(draw.pbrUboData) <= kPbrSkinnedUBOStride * kPbrSkinnedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(pbrSkinnedUBOPtr_[currentFrame_]) + pbrOff,
                                        draw.pbrUboData, sizeof(draw.pbrUboData));
                        }
                        const uint32_t dynOffsets[2] = { boneOff, pbrOff };
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutPbrSkinned3D_, 0, 1,
                                                &draw.pbrDescSet, 2, dynOffsets);
                    }
                } else if (draw.usePbr) {
                    vkCmdPushConstants(cb, pipelineLayoutPbr3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    if (draw.pbrDescSet != VK_NULL_HANDLE && pbrUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = pbrUBOSlot++;
                        const uint32_t uboOff = slot * kPbrUBOStride;
                        if (uboOff + sizeof(draw.pbrUboData) <= kPbrUBOStride * kPbrUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(pbrUBOPtr_[currentFrame_]) + uboOff,
                                        draw.pbrUboData, sizeof(draw.pbrUboData));
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutPbr3D_, 0, 1,
                                                &draw.pbrDescSet, 1, &uboOff);
                    }
                } else if (draw.useInstanced) {
                    vkCmdPushConstants(cb, pipelineLayoutExt3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                } else if (draw.useLitTextured) {
                    vkCmdPushConstants(cb, pipelineLayoutLitTextured3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // No defaultWhiteDescSet_ fallback here (unlike useAlphaTest) -- that set
                    // belongs to the simple 1-binding descriptorSetLayout_, structurally
                    // incompatible with this pipeline's 2-binding (sampler+UBO) layout. Mirrors
                    // useEnvMap/useSkinned/useDualTexture/useFogTex3D's own pattern.
                    if (draw.litTexturedDescSet != VK_NULL_HANDLE && litTexturedUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = litTexturedUBOSlot++;
                        const uint32_t uboOff = slot * kLitTexturedUBOStride;
                        if (uboOff + 256 <= kLitTexturedUBOStride * kLitTexturedUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(litTexturedUBOPtr_[currentFrame_]) + uboOff,
                                        draw.litUboData, 256);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutLitTextured3D_, 0, 1,
                                                &draw.litTexturedDescSet, 1, &uboOff);
                    }
                } else if (draw.useFogTex3D) {
                    vkCmdPushConstants(cb, pipelineLayoutFogTex3D_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, 128, draw.pushConst);
                    // No defaultWhiteDescSet_ fallback here -- that set belongs to the
                    // structurally-incompatible 1-binding descriptorSetLayout_ (mirrors
                    // useLitTextured/useSkinned/useDualTexture's own pattern); the fallback
                    // white texture is already substituted inside GetOrCreateFogTex3DDescSet.
                    if (draw.fogTex3DDescSet != VK_NULL_HANDLE && fogTex3DUBOPtr_[currentFrame_]) {
                        const uint32_t slot   = fogTex3DUBOSlot++;
                        const uint32_t uboOff = slot * kFogTex3DUBOStride;
                        if (uboOff + 32 <= kFogTex3DUBOStride * kFogTex3DUBOMaxDraws) {
                            std::memcpy(static_cast<uint8_t*>(fogTex3DUBOPtr_[currentFrame_]) + uboOff,
                                        draw.fogTex3DUboData, 32);
                        }
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayoutFogTex3D_, 0, 1,
                                                &draw.fogTex3DDescSet, 1, &uboOff);
                    }
                } else {
                    // 128 bytes: see pipelineLayout3D_ creation comment (Task 364) — draw.pushConst
                    // already holds diffuseColor/vertexColorEnabled at the same float offsets
                    // FillExtPushConst() uses, filled by DrawPrimitivesEx for stride==16 draws.
                    vkCmdPushConstants(cb, pipelineLayout3D_, VK_SHADER_STAGE_VERTEX_BIT, 0, 128, draw.pushConst);
                }
                VkLifetimeTraceEXT("record.draw3D    order=%llu family=%s rt=%p seg=%llu "
                                   "boundSet=0x%llx count=%u instances=%u",
                                   static_cast<unsigned long long>(draw.order),
                                   Pending3DFamilyEXT(draw),
                                   static_cast<const void*>(draw.rt.get()),
                                   static_cast<unsigned long long>(draw.segment),
                                   VkH(draw.descSet), draw.drawCount, draw.instanceCount);
                vkCmdBindVertexBuffers(cb, 0, 1, &frame3DVB_[currentFrame_], &vbOff);
                if (draw.useInstanced && !draw.instVbData.empty()) {
                    vkCmdBindVertexBuffers(cb, 1, 1, &frame3DInstVB_[currentFrame_], &instVbOff);
                }
                if (!draw.ibData.empty()) {
                    vkCmdBindIndexBuffer(
                        cb, frame3DIB_[currentFrame_], nativeIbOff, draw.indexType);
                    vkCmdDrawIndexed(cb, draw.drawCount, draw.instanceCount, 0, draw.baseVertex, 0);
                    ibOff = nativeIbOff + static_cast<VkDeviceSize>(draw.ibData.size());
                } else {
                    vkCmdDraw(cb, draw.drawCount, draw.instanceCount, 0, 0);
                }
                if (draw.useInstanced)
                    instVbOff += static_cast<VkDeviceSize>(draw.instVbData.size());
                vbOff += static_cast<VkDeviceSize>(draw.vbData.size());
            }
            // Task 447/854's close of a query left open at the end of a render pass's own draw
            // list now happens once per SEGMENT, in closeOpenQuery3D, not once per slice.
        };

        // REMED-GFX-157: replay ONE order slice of a segment with both draw families interleaved in
        // public order, instead of all of its sprites and then all of its 3D draws.
        //
        // The two families live in separate queues (`activeBatches_`, `pending3D_`) and used to be
        // replayed by two back-to-back calls, so within a single bind cycle a 3D draw always landed
        // on top of a sprite issued after it -- the sequence `3D draw; SpriteBatch.Draw` came out
        // inverted. REMED-GFX-143's check O3 declared that as an open defect on this renderer.
        //
        // Nothing about either family's replay changes. Both lambdas already accept the half-open
        // order range REMED-GFX-129 gave them for ordered Clear() slicing, and `commandOrder_` is a
        // single monotonic counter shared by batches, 3D draws and clears alike -- so the whole
        // correction is to split the requested range at every point where the family changes and
        // call the existing lambdas once per same-family RUN, in order. Orders are unique, so a run
        // [first, last] is selected exactly by the range (first - 1, last + 1).
        //
        // This adds no pass, no submit, no barrier and no allocation per draw: one small vector of
        // (order, family) pairs per slice, and one extra pipeline bind per family switch, which is
        // the irreducible cost of actually interleaving. A segment that uses only one family still
        // makes exactly one call, exactly as before.
        auto drawFamiliesInOrder = [&](VulkanRTSource* targetRT, uint64_t segment,
                                       float vpW, float vpH,
                                       uint64_t afterOrder, uint64_t beforeOrder)
        {
            struct Item { uint64_t order; bool sprite; };
            std::vector<Item> items;
            items.reserve(activeBatches_.size() + pending3D_.size());
            for (const auto& e : activeBatches_) {
                if (e.rt.get() != targetRT || e.segment != segment) continue;
                if (e.order <= afterOrder || e.order >= beforeOrder) continue;
                items.push_back({ e.order, true });
            }
            for (const auto& d : pending3D_) {
                if (d.rt.get() != targetRT || d.segment != segment) continue;
                if (d.order <= afterOrder || d.order >= beforeOrder) continue;
                items.push_back({ d.order, false });
            }
            if (items.empty()) return;
            std::sort(items.begin(), items.end(),
                      [](const Item& a, const Item& b) { return a.order < b.order; });

            std::size_t i = 0;
            while (i < items.size()) {
                std::size_t j = i;
                while (j + 1 < items.size() && items[j + 1].sprite == items[i].sprite) ++j;
                const uint64_t lo = items[i].order - 1;
                const uint64_t hi = items[j].order + 1;
                if (items[i].sprite) drawSpritesFor(targetRT, segment, vpW, vpH, lo, hi);
                else                 draw3DFor(targetRT, segment, lo, hi);
                i = j + 1;
            }
        };

        // ---- One ordered stream of passes: every bind cycle, off-screen AND backbuffer ----
        // REMED-GFX-140: this used to collect the UNIQUE render-target sources referenced this
        // frame and give each of them a single pass holding every batch and draw queued against it.
        // That threw away the boundary between two public bind cycles of the same target: they
        // shared one load action, so the second cycle's DiscardContents clear (and any explicit
        // Clear()) simply did not happen. The list is now keyed on the SEGMENT -- the bind cycle --
        // and every entry carries the segment it was issued in, so rebinding the same target
        // creates a genuinely new pass. Ordering is by segment id, i.e. exactly the public order in
        // which the cycles were opened; A -> B -> A therefore records three passes, not two.
        //
        // REMED-GFX-143: the BACKBUFFER joins that stream. It used to be a separate Phase 2 recorded
        // after every off-screen pass and passed `kAllSegments`, so every backbuffer draw of the
        // frame was replayed inside one trailing swapchain pass regardless of when the game issued
        // it. `bind t; draw; unbind; draw t onto the backbuffer; bind t; draw; unbind; draw t onto
        // the backbuffer` therefore ran both of t's passes first and BOTH backbuffer draws sampled
        // the final content. A segment whose rt is nullptr is now a backbuffer cycle recorded in the
        // same ascending-id order as the rest; the swapchain image is acquired once and entered once
        // per cycle (GetOrCreateSwapchainRenderPass supplies the LOAD variant), and presentation
        // stays once per frame.
        //
        // REMED-GFX-129: a segment no longer carries ONE set of clear values. It carries the ordered
        // list of the Clear() calls made inside its bind cycle, plus the position of its earliest
        // draw, which is all the load-op folding decision needs.
        struct PassSegment {
            uint64_t        id        = 0;
            VulkanRTSource* rt        = nullptr;   ///< nullptr = the backbuffer.
            bool            isBackbuffer = false;  ///< Distinguishes "the backbuffer" from "unset".
            /// This bind cycle's Clear() calls, in public call order.
            std::vector<const PendingClear*> clears;
            /// Public-stream position of this segment's earliest draw; none = no draw at all.
            uint64_t        firstDrawOrder = std::numeric_limits<uint64_t>::max();
        };
        std::vector<PassSegment> segments;
        auto segmentFor = [&segments](uint64_t id, VulkanRTSource* rt) -> PassSegment& {
            for (auto& seg : segments)
                if (seg.id == id) return seg;
            PassSegment fresh;
            fresh.id = id;
            fresh.rt = rt;
            fresh.isBackbuffer = (rt == nullptr);
            segments.push_back(fresh);
            return segments.back();
        };
        // Task 875: a segment whose only content is a Clear() still needs its render pass recorded
        // (matches FNA/XNA, where Clear() takes effect regardless of what is drawn afterward) —
        // otherwise the target's colour image never leaves VK_IMAGE_LAYOUT_UNDEFINED. REMED-GFX-143:
        // a backbuffer Clear() opens a segment for the same reason -- a frame whose only backbuffer
        // command in a cycle is a Clear() must still get that cycle's own load action.
        // pendingClears_ is already in public call order, so each segment's list comes out ordered.
        for (const auto& c : pendingClears_)
            segmentFor(c.segment, c.rt.get()).clears.push_back(&c);
        for (const auto& entry : activeBatches_) {
            PassSegment& seg = segmentFor(entry.segment, entry.rt.get());
            seg.firstDrawOrder = std::min(seg.firstDrawOrder, entry.order);
        }
        for (const auto& draw : pending3D_) {
            PassSegment& seg = segmentFor(draw.segment, draw.rt.get());
            seg.firstDrawOrder = std::min(seg.firstDrawOrder, draw.order);
        }
        std::sort(segments.begin(), segments.end(),
                  [](const PassSegment& l, const PassSegment& r) { return l.id < r.id; });

        // REMED-GFX-074/151: a readback flush records the frame's off-screen passes up to the
        // readback point and nothing else. FlushDeferredRenderTarget only invokes this mode when the
        // target being read genuinely has pending work, and passes that target's highest pending
        // segment id, so this keeps every off-screen segment at or before it -- including one whose
        // only entry was a bare Clear(), and including every PRODUCER whose content a segment here
        // samples, which is the defect REMED-GFX-151 fixes. REMED-GFX-140: it is "segments", plural
        // -- a target bound twice before its first readback owes two passes here just as it does at
        // Present. Backbuffer segments are dropped (rt == nullptr fails the predicate), leaving the
        // backbuffer's pending work for the real Present exactly as before.
        if (rtOnly)
            segments.erase(std::remove_if(segments.begin(), segments.end(),
                               [&recordedByFlush](const PassSegment& seg) {
                                   return !recordedByFlush(seg.rt, seg.id);
                               }),
                           segments.end());

        // REMED-GFX-143: the swapchain image must be cleared/stored and left in PRESENT_SRC_KHR
        // every rendered frame even when the game drew nothing to it, which the single trailing pass
        // guaranteed for free. A synthetic final backbuffer segment does it: with no clear of its
        // own it takes exactly the frame-global values the old Phase 2 read, so a frame with no
        // backbuffer work records byte-for-byte the pass it always did.
        const bool hasMsaa = (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) && (renderPassMsaa_ != VK_NULL_HANDLE);
        std::size_t backbufferSegments = 0;
        if (!rtOnly) {
            for (const auto& seg : segments)
                if (seg.isBackbuffer) ++backbufferSegments;
            if (backbufferSegments == 0) {
                PassSegment tail;
                tail.id = ~static_cast<uint64_t>(0);
                tail.isBackbuffer = true;
                segments.push_back(tail);
                backbufferSegments = 1;
            }
        }
        std::size_t backbufferSeen = 0;

        for (const auto& seg : segments) {
            if (seg.isBackbuffer) {
                ++backbufferSeen;
                const bool isFirstBackbuffer = (backbufferSeen == 1);
                const bool isLastBackbuffer  = (backbufferSeen == backbufferSegments);
                // The FIRST backbuffer cycle of the frame always clears, exactly as the single
                // trailing pass did -- the acquired swapchain image's content is undefined and a
                // game that never calls Clear() must still get clearR_. Later cycles LOAD what the
                // earlier one stored unless they issued their own Clear(), per aspect: a colour-only
                // Clear() in a later cycle must not throw away the depth an earlier cycle wrote.
                // REMED-GFX-129: only a clear that PRECEDES every draw of this cycle may become
                // the pass load action -- one issued after a draw has to run after that draw and is
                // recorded as a vkCmdClearAttachments command below. Pre-fix `seg.clearColor` was
                // set by ANY clear in the cycle, which is precisely how "draw, then Clear" ended up
                // running the clear first.
                const PendingClear* folded =
                    (!seg.clears.empty() && seg.clears.front()->order < seg.firstDrawOrder)
                        ? seg.clears.front()
                        : nullptr;

                SwapchainPassKey key;
                key.msaa         = hasMsaa;
                key.loadColor    = !isFirstBackbuffer && !(folded && folded->wantColor);
                key.loadDepth    = !isFirstBackbuffer && !(folded && folded->wantDepth);
                key.loadStencil  = !isFirstBackbuffer && !(folded && folded->wantStencil);
                key.storeForNext = !isLastBackbuffer;

                // A second entry into the same image needs its LOAD (and its initialLayout
                // transition) ordered after the previous entry's colour and depth writes. Neither
                // side's external subpass dependency provides that chain -- the exiting pass's
                // dep[1] makes colour writes available to FRAGMENT_SHADER/TRANSFER reads only, and
                // the entering pass's dep[0] waits on shader reads and depth writes, not on
                // COLOR_ATTACHMENT_OUTPUT -- and those masks must stay byte-identical across every
                // render pass here for pipelines to remain cross-pass compatible (Task 905). So the
                // dependency is issued as an explicit barrier instead, emitted only BETWEEN
                // backbuffer cycles, which means a frame with one cycle pays nothing.
                //
                // Honest note on the evidence: neither Khronos synchronization validation nor
                // llvmpipe exhibits a hazard when this barrier is removed (measured, A/B, with the
                // layer proved loaded). It is kept because the spec-level dependency genuinely is
                // absent, not because a tool reported it -- a discrete GPU that reorders passes
                // would be free to run the load before the write.
                if (!isFirstBackbuffer) {
                    VkMemoryBarrier mb{};
                    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(cb,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        0, 1, &mb, 0, nullptr, 0, nullptr);
                }

                // The FOLDED clear's values; an aspect it did not name falls back to the
                // frame-global value, which is byte-for-byte what the old single pass used.
                const bool foldColor = folded && folded->wantColor;
                const bool foldDS    = folded && (folded->wantDepth || folded->wantStencil);
                const float segR = foldColor ? folded->r : clearR_;
                const float segG = foldColor ? folded->g : clearG_;
                const float segB = foldColor ? folded->b : clearB_;
                const float segA = foldColor ? folded->a : clearA_;
                const float segDepth   = foldDS ? folded->depth   : clearDepth_;
                const int   segStencil = foldDS ? folded->stencil : clearStencil_;
                // MSAA render pass: att0=MSAA color, att1=resolve(swapchain), att2=depth — 3 clear
                // values. Non-MSAA render pass: att0=swapchain color, att1=depth — 2 clear values.
                VkClearValue cv[3]{};
                cv[0].color = { { segR, segG, segB, segA } };
                if (hasMsaa) {
                    cv[1].color        = {};
                    cv[2].depthStencil = { segDepth, static_cast<uint32_t>(segStencil) };
                } else {
                    cv[1].depthStencil = { segDepth, static_cast<uint32_t>(segStencil) };
                }
                VkRenderPassBeginInfo rp{};
                rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp.renderPass      = GetOrCreateSwapchainRenderPass(key);
                rp.framebuffer     = swapchainFramebuffers_[imageIndex];
                rp.renderArea      = { {0, 0}, swapchainExtent_ };
                rp.clearValueCount = hasMsaa ? 3u : 2u;
                rp.pClearValues    = cv;
                vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport vp{};
                if (viewportSet_ && viewportW_ > 0 && viewportH_ > 0) {
                    // Task 880: honor a custom sub-region Viewport for the backbuffer pass. This is
                    // only the pass-level default -- every batch and every 3D draw overrides it from
                    // its own captured state (REMED-GFX-062), so it is already cycle-local.
                    vp.x = static_cast<float>(viewportX_);
                    vp.y = static_cast<float>(viewportY_);
                    vp.width    = static_cast<float>(viewportW_);
                    vp.height   = static_cast<float>(viewportH_);
                    vp.minDepth = viewportMinDepth_;
                    vp.maxDepth = viewportMaxDepth_;
                } else {
                    // plan_vulkan.md VULKAN-330: "full target" for the BACKBUFFER means the
                    // presented rectangle, not the whole swapchain image. Under Letterbox or
                    // Overscan those differ; under the other three modes the helper returns the
                    // full extent, so this is byte-identical to what it replaced for them.
                    int px = 0, py = 0, pw = 0, ph = 0;
                    GetPresentedRectEXT(px, py, pw, ph);
                    vp.x = static_cast<float>(px); vp.y = static_cast<float>(py);
                    vp.width  = static_cast<float>(pw);
                    vp.height = static_cast<float>(ph);
                    vp.minDepth = 0.f; vp.maxDepth = 1.f;
                }
                vkCmdSetViewport(cb, 0, 1, &vp);
                {
                    VkRect2D sc{ {0, 0}, swapchainExtent_ };
                    if (scissorEnabled_ && scissorW_ > 0 && scissorH_ > 0)
                        sc = { {scissorX_, scissorY_}, {scissorW_, scissorH_} };
                    vkCmdSetScissor(cb, 0, 1, &sc);
                }
                const float vpW2D = (virtualWidth_  > 0) ? static_cast<float>(virtualWidth_)
                                                          : static_cast<float>(swapchainExtent_.width);
                const float vpH2D = (virtualHeight_ > 0) ? static_cast<float>(virtualHeight_)
                                                          : static_cast<float>(swapchainExtent_.height);
                // REMED-GFX-129: replay this cycle's batches, 3D draws and Clear() commands in the
                // exact public order they were issued in. The folded clear (if any) has already
                // happened, as this pass's load action.
                {
                    uint64_t cursor = 0;
                    for (const PendingClear* c : seg.clears) {
                        if (c == folded) { cursor = c->order; continue; }
                        drawFamiliesInOrder(nullptr, seg.id, vpW2D, vpH2D, cursor, c->order);
                        RecordOrderedClearEXT(cb, *c, 1u, depthFormat_,
                                              swapchainExtent_.width, swapchainExtent_.height);
                        cursor = c->order;
                    }
                    const uint64_t kEnd = std::numeric_limits<uint64_t>::max();
                    drawFamiliesInOrder(nullptr, seg.id, vpW2D, vpH2D, cursor, kEnd);
                    closeOpenQuery3D();
                }

                vkCmdEndRenderPass(cb);
                continue;
            }

            VulkanRTSource* rt = seg.rt;
            VkLifetimeTraceEXT("record.segment   seg=%llu rt=%p fb=0x%llx pass=0x%llx %dx%d "
                               "clears=%zu firstDraw=%llu",
                               static_cast<unsigned long long>(seg.id),
                               static_cast<const void*>(rt), VkH(rt->GetFramebuffer()),
                               VkH(rt->GetRenderPass()), rt->GetWidth(), rt->GetHeight(),
                               seg.clears.size(),
                               static_cast<unsigned long long>(seg.firstDrawOrder));
            const uint32_t nColor = rt->GetColorAttachmentCount();
            const bool rtMsaa = rt->WantsMsaa();
            const bool hasDepth = rt->GetDepthFormat() != VK_FORMAT_UNDEFINED;
            // REMED-GFX-095: attachment order mirrors the render pass exactly:
            // [N color sources, optional N resolves, optional depth].
            const uint32_t depthIndex = rtMsaa ? nColor * 2 : nColor;
            // REMED-GFX-129: a LEADING Clear() may ride this pass's load action, but only when the
            // pass actually clears its colour attachment -- a PreserveContents target's pass uses
            // VK_ATTACHMENT_LOAD_OP_LOAD, and there is no per-aspect load value to hand a clear to
            // there. Every other Clear() of the cycle, and every Clear() at all on a preserving
            // target, is recorded as an ordered vkCmdClearAttachments command below. Folding is kept
            // for the discarding case because the shared layer issues a Clear() on EVERY
            // DiscardContents bind, so an unfolded version would pay one extra full-target clear per
            // bind for a result the load action already produces.
            const PendingClear* folded =
                (!seg.clears.empty() && seg.clears.front()->order < seg.firstDrawOrder &&
                 rt->ColorLoadOpIsClearEXT())
                    ? seg.clears.front()
                    : nullptr;
            // A segment with no folded clear keeps the pre-fix fallback to the frame-global values.
            const bool foldColor = folded && folded->wantColor;
            const bool foldDS    = folded && (folded->wantDepth || folded->wantStencil);
            const float segR = foldColor ? folded->r : clearR_;
            const float segG = foldColor ? folded->g : clearG_;
            const float segB = foldColor ? folded->b : clearB_;
            const float segA = foldColor ? folded->a : clearA_;
            const float segDepth   = foldDS ? folded->depth   : clearDepth_;
            const int   segStencil = foldDS ? folded->stencil : clearStencil_;
            std::vector<VkClearValue> rtCv(depthIndex + (hasDepth ? 1u : 0u));
            for (uint32_t ci = 0; ci < nColor; ++ci)
                rtCv[ci].color = { { segR, segG, segB, segA } };
            if (rtMsaa)
                for (uint32_t ri = nColor; ri < nColor * 2; ++ri)
                    rtCv[ri].color = {};
            if (hasDepth)
                rtCv[depthIndex].depthStencil = {
                    segDepth, static_cast<uint32_t>(segStencil)
                };
            VkRenderPassBeginInfo rtRp{};
            rtRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rtRp.renderPass      = rt->GetRenderPass();
            rtRp.framebuffer     = rt->GetFramebuffer();
            const uint32_t rtW   = static_cast<uint32_t>(rt->GetWidth());
            const uint32_t rtH   = static_cast<uint32_t>(rt->GetHeight());
            rtRp.renderArea      = { {0, 0}, { rtW, rtH } };
            rtRp.clearValueCount = static_cast<uint32_t>(rtCv.size());
            rtRp.pClearValues    = rtCv.data();
            vkCmdBeginRenderPass(cb, &rtRp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport rtVp{};
            rtVp.x = 0; rtVp.y = 0;
            rtVp.width  = static_cast<float>(rtW);
            rtVp.height = static_cast<float>(rtH);
            rtVp.minDepth = 0.f; rtVp.maxDepth = 1.f;
            vkCmdSetViewport(cb, 0, 1, &rtVp);
            VkRect2D rtSc{ {0, 0}, { rtW, rtH } };
            vkCmdSetScissor(cb, 0, 1, &rtSc);

            // REMED-GFX-129: exact public order, clears included. See the backbuffer branch.
            {
                uint64_t cursor = 0;
                for (const PendingClear* c : seg.clears) {
                    if (c == folded) { cursor = c->order; continue; }
                    drawFamiliesInOrder(rt, seg.id, static_cast<float>(rtW),
                                        static_cast<float>(rtH), cursor, c->order);
                    RecordOrderedClearEXT(cb, *c, nColor, rt->GetDepthFormat(), rtW, rtH);
                    cursor = c->order;
                }
                const uint64_t kEnd = std::numeric_limits<uint64_t>::max();
                drawFamiliesInOrder(rt, seg.id, static_cast<float>(rtW),
                                    static_cast<float>(rtH), cursor, kEnd);
                closeOpenQuery3D();
            }

            vkCmdEndRenderPass(cb);

            // Task 878: regenerate this RT's mip chain (no-op unless it actually owns mips).
            rt->MaybeGenerateMips(cb);
        }

        // REMED-GFX-074: a RenderTargetsOnly readback flush stops here -- no backbuffer pass, no
        // swapchain, no backbuffer readback. Consume the deferred entries this record emitted so
        // Present() does not replay them (no double-render), leaving the backbuffer's and every
        // later bind cycle's pending work untouched, then close the command buffer.
        if (rtOnly) {
            // REMED-GFX-151: consume exactly what was recorded above -- the same positional
            // predicate the segment filter used -- so Present() never replays an entry this flush
            // already emitted, and never drops one it did not.
            activeBatches_.erase(std::remove_if(activeBatches_.begin(), activeBatches_.end(),
                [&recordedByFlush](const PendingBatch& p) { return recordedByFlush(p.rt.get(), p.segment); }),
                activeBatches_.end());
            pending3D_.erase(std::remove_if(pending3D_.begin(), pending3D_.end(),
                [&recordedByFlush](const Pending3DDraw& d) { return recordedByFlush(d.rt.get(), d.segment); }),
                pending3D_.end());
            pendingClears_.erase(std::remove_if(pendingClears_.begin(), pendingClears_.end(),
                [&recordedByFlush](const PendingClear& c) { return recordedByFlush(c.rt.get(), c.segment); }),
                pendingClears_.end());
            // REMED-GFX-151: a consumed cycle's sampling dependencies are spent with it, so the
            // graph stays the size of the still-pending frame rather than growing per readback.
            if (flushSegments != nullptr)
                segmentSampledGroups_.erase(
                    std::remove_if(segmentSampledGroups_.begin(), segmentSampledGroups_.end(),
                        [flushSegments](const std::pair<uint64_t, const void*>& e) {
                            return std::find(flushSegments->begin(), flushSegments->end(), e.first)
                                   != flushSegments->end();
                        }),
                    segmentSampledGroups_.end());
            if (vkEndCommandBuffer(cb) != VK_SUCCESS)
                throw std::runtime_error("vkEndCommandBuffer failed");
            return;
        }

        // REMED-GFX-143: every backbuffer cycle was recorded in the ordered stream above, so what
        // used to be Phase 2 -- one trailing swapchain pass replaying the frame's whole backbuffer
        // queue -- is gone. Only the deferred entries still need consuming here.
        activeBatches_.clear();
        pending3D_.clear();
        pendingClears_.clear();
        // REMED-GFX-151: the whole frame was just recorded, so no sampling dependency survives it.
        segmentSampledGroups_.clear();
        // VULKAN-160: and this is the one instant at which a sampler can safely leave the cache.
        // Both gates are open here: the pending queues are empty, so no CPU record still holds a
        // sampler handle, and the GPU side is handled by retiring rather than destroying.
        TrimSamplerCacheEXT();

        // If ReadBackbuffer queued a deferred readback, copy the swapchain image
        // to the staging buffer NOW — before vkQueuePresentKHR hands the image to
        // the presentation engine. This eliminates the CPU/display-engine race.
        if (readbackPending_)
        {
            const VkDeviceSize needed = static_cast<VkDeviceSize>(readbackW_) * readbackH_ * 4;
            if (readbackStagingBuf_ == VK_NULL_HANDLE
                || readbackAllocW_ < readbackW_ || readbackAllocH_ < readbackH_)
            {
                if (readbackStagingBuf_ != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(device_, readbackStagingBuf_, nullptr);
                    vkFreeMemory(device_, readbackStagingMem_, nullptr);
                }
                CreateBuffer(needed,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             readbackStagingBuf_, readbackStagingMem_, nullptr);
                readbackAllocW_ = readbackW_;
                readbackAllocH_ = readbackH_;
            }

            auto layoutBarrier = [&](VkImage img,
                                     VkImageLayout oldL, VkImageLayout newL,
                                     VkAccessFlags srcA, VkAccessFlags dstA,
                                     VkPipelineStageFlags src, VkPipelineStageFlags dst)
            {
                VkImageMemoryBarrier b{};
                b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout           = oldL;
                b.newLayout           = newL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image               = img;
                b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                b.srcAccessMask       = srcA;
                b.dstAccessMask       = dstA;
                vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
            };

            // The render pass ended with finalLayout = PRESENT_SRC_KHR.
            // Transition to TRANSFER_SRC_OPTIMAL for the copy.
            const VkImage swImg = swapchainImages_[imageIndex];
            layoutBarrier(swImg,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset      = 0;
            region.bufferRowLength   = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageOffset       = { readbackX_, readbackY_, 0 };
            region.imageExtent       = { static_cast<uint32_t>(readbackW_),
                                         static_cast<uint32_t>(readbackH_), 1 };
            vkCmdCopyImageToBuffer(cb, swImg,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readbackStagingBuf_, 1, &region);

            // Transition back to PRESENT_SRC_KHR for vkQueuePresentKHR.
            layoutBarrier(swImg,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_ACCESS_MEMORY_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

            readbackPending_ = false;
        }

        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed");
    }

    void VulkanRenderer::Present()
    {
        if (SubmitFrame(false)) {
            // Non-deferred path already presented inside SubmitFrame.
        }
    }

    bool VulkanRenderer::SubmitFrame(bool deferSwap)
    {
        if (!initialized_) return false;

        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
        ++frameFenceWaitCountEXT_;

        // REMED-GFX-075: the current frame slot's fence just signalled, so free any retired
        // deferred-resource handles whose consuming frame is now provably complete (see
        // ProcessRetiredResources). Ordinary destruction never stalls the device; the cost of the
        // deferred free is paid here, once per frame, on already-idle handles.
        ProcessRetiredResources(false);

        uint32_t imageIndex = 0;
        // VULKAN-026: the injected arm replaces the acquire, it does not follow it -- an image
        // handed over and then declared stale would leave imageAvailableSemaphores_[currentFrame_]
        // signalled with nothing waiting on it, which is a different defect than the one under
        // test and would be reported as a synchronization hazard rather than this branch.
        VkResult result;
        if (sSwapchainOutOfDateToInject > 0) {
            --sSwapchainOutOfDateToInject;
            result = VK_ERROR_OUT_OF_DATE_KHR;
        } else {
            result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return false; }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        // REMED-GFX-144: counted only on an acquire that really handed over an image. An
        // OUT_OF_DATE acquire returns above WITHOUT resetting the fence, which is what keeps a
        // failed acquire from leaving this slot's fence permanently unsignalled.
        ++acquireCountEXT_;
        if (imageIndex < 32) acquiredImageMaskEXT_ |= (1u << imageIndex);

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
        RecordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

        VkSemaphore waitSems[]   = { imageAvailableSemaphores_[currentFrame_] };
        VkSemaphore signalSems[] = { renderFinishedSemaphores_[currentFrame_] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = waitSems;
        si.pWaitDstStageMask    = waitStages;
        si.commandBufferCount   = 1; si.pCommandBuffers   = &commandBuffers_[currentFrame_];
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;
        if (vkQueueSubmit(graphicsQueue_, 1, &si, inFlightFences_[currentFrame_]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed");
        ++frameSubmitCountEXT_;
        if (currentFrame_ < 32) usedFrameSlotMaskEXT_ |= (1u << currentFrame_);

        // REMED-GFX-075: this Full record consumed every deferred entry; advance the retirement
        // generation clock so resources retired during this frame's build are freed only after this
        // submit's fence has completed (generation + MaxFramesInFlight later). See RetireResources.
        ++frameGeneration_;
        VkLifetimeTraceEXT("frame.submitted  gen=%llu retiredBuckets=%zu",
                           static_cast<unsigned long long>(frameGeneration_),
                           retiredResources_.size());

        if (deferSwap) {
            // Wait for render + readback copy to complete, but hold the image. The caller
            // (ReadBackbuffer) reads the staging buffer before the image is presented, so
            // presentation-engine timing can never corrupt the captured pixels.
            vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
            ++frameFenceWaitCountEXT_;
            deferredPresentImageIndex_ = imageIndex;
            hasDeferredPresent_        = true;
            return true;
        }

        VkSwapchainKHR sc[] = { swapchain_ };
        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
        pi.swapchainCount     = 1; pi.pSwapchains     = sc;
        pi.pImageIndices      = &imageIndex;
        result = vkQueuePresentKHR(presentQueue_, &pi);
        ++presentCountEXT_;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            RecreateSwapchain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        lastPresentedImageIndex_ = imageIndex;
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
        return true;
    }

    void VulkanRenderer::FinishDeferredPresent()
    {
        if (!hasDeferredPresent_) return;
        hasDeferredPresent_ = false;

        uint32_t imageIndex = deferredPresentImageIndex_;
        VkSemaphore signalSems[] = { renderFinishedSemaphores_[currentFrame_] };
        VkSwapchainKHR sc[] = { swapchain_ };
        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        // The submit already completed (fence-waited), so the renderFinished semaphore is
        // signalled and the present will not block.
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
        pi.swapchainCount     = 1; pi.pSwapchains     = sc;
        pi.pImageIndices      = &imageIndex;
        VkResult result = vkQueuePresentKHR(presentQueue_, &pi);
        ++presentCountEXT_;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            RecreateSwapchain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        lastPresentedImageIndex_ = imageIndex;
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void VulkanRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!initialized_ || swapchainImages_.empty()) return;

        const int fullW = static_cast<int>(swapchainExtent_.width);
        const int fullH = static_cast<int>(swapchainExtent_.height);
        if (fullW <= 0 || fullH <= 0) return;

        // Capture the WHOLE backbuffer into the staging buffer only when there is new
        // work to render, or when the cache is stale (a new frame was cleared). This lets
        // several GetBackBufferData() reads of the same frame be served from the cache
        // without re-presenting — re-presenting an empty queue would re-render a cleared
        // frame and destroy the content of all but the first read.
        const bool hasNewWork = !pending3D_.empty() || !activeBatches_.empty();
        // VULKAN-404: and the cache is only usable when its allocation actually covers the frame
        // about to be mapped. RecreateSwapchain now invalidates it, but this keeps the map size
        // bounded by the allocation locally, so no future caller can reintroduce the overstep by
        // forgetting to invalidate -- the two dimensions being >= is exactly the test the
        // reallocation below uses.
        const bool stagingCoversFrame = readbackStagingBuf_ != VK_NULL_HANDLE &&
                                        readbackAllocW_ >= fullW && readbackAllocH_ >= fullH;
        if (hasNewWork || !readbackStagingValid_ || !stagingCoversFrame) {
            // Deferred copy: RecordCommandBuffer copies the whole swapchain image into
            // readbackStagingBuf_ before vkQueuePresentKHR. SubmitFrame(true) renders and
            // waits for the GPU but HOLDS the present, so we read the staging buffer below
            // BEFORE the image is handed to the presentation engine — no race possible.
            readbackX_ = 0; readbackY_ = 0; readbackW_ = fullW; readbackH_ = fullH;
            readbackPending_ = true;
            if (!SubmitFrame(true)) {
                // Swapchain out-of-date (common on first frame under Wayland/RADV); the
                // staging buffer was not written. Zero the output so the caller can detect
                // a blank frame and retry, rather than seeing stale data.
                readbackPending_ = false;
                std::memset(pixels, 0, static_cast<std::size_t>(w) * h * 4);
                return;
            }
            readbackStagingValid_ = true;
        }

        // Serve the requested sub-region from the cached full-frame staging buffer,
        // handling the BGRA → RGBA channel swap.
        const VkDeviceSize fullSize = static_cast<VkDeviceSize>(fullW) * fullH * 4;
        void* mapped = nullptr;
        vkMapMemory(device_, readbackStagingMem_, 0, fullSize, 0, &mapped);
        const auto* src = static_cast<const uint8_t*>(mapped);
        const bool isBGRA = (swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB);
        for (int row = 0; row < h; ++row) {
            const int sy = y + row;
            for (int col = 0; col < w; ++col) {
                const int sx = x + col;
                uint8_t* d = pixels + (static_cast<std::size_t>(row) * w + col) * 4;
                if (sx < 0 || sx >= fullW || sy < 0 || sy >= fullH) {
                    d[0] = d[1] = d[2] = d[3] = 0;
                    continue;
                }
                const uint8_t* s = src + (static_cast<std::size_t>(sy) * fullW + sx) * 4;
                if (isBGRA) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            }
        }
        vkUnmapMemory(device_, readbackStagingMem_);

        // Now that the pixels are safely captured, present the held image.
        FinishDeferredPresent();
    }

    void VulkanRenderer::GetViewportSize(int& width, int& height)
    {
        // plan_vulkan.md VULKAN-350. This is the LOGICAL size -- what
        // `GraphicsDevice.Viewport.Width/Height` reports to game code -- and it is a different
        // question from `GetPresentedRectEXT`'s physical rectangle. `VULKAN-330`/`VULKAN-331`
        // implemented the presentation mode in the physical half only, so until this row
        // `FixedHeightDynamicWidth` -- CNA's default -- did not pin the height here and a real
        // window resize moved it. `VULKAN-337` measured that against
        // `easygl_real_window_resize_test.cpp`, which passes its other three checks on this
        // renderer.
        //
        // Deliberately the same algorithm as `EasyGLSurfaceState::GetLogicalSize`, for the reason
        // `GetPresentedRectEXT` gives for mirroring its physical twin: the modes have to mean the
        // same thing on both renderers or the mode is a per-renderer word rather than a contract.
        //
        // The client size, not the drawable size, is what the aspect is taken from -- a HiDPI
        // drawable is the same window -- which is why `displayScale` divides out first.
        const double scale = surfaceInfo_.displayScale > 0.0f
            ? static_cast<double>(surfaceInfo_.displayScale) : 1.0;
        int clientWidth  = static_cast<int>(std::lround(
            static_cast<double>(surfaceInfo_.drawableSize.width) / scale));
        int clientHeight = static_cast<int>(std::lround(
            static_cast<double>(surfaceInfo_.drawableSize.height) / scale));
        if (clientWidth <= 0 || clientHeight <= 0)
        {
            clientWidth  = static_cast<int>(swapchainExtent_.width);
            clientHeight = static_cast<int>(swapchainExtent_.height);
        }

        // No virtual resolution means no mode to apply: the logical size IS the client size, which
        // is exactly what this function returned before this row.
        if (virtualHeight_ <= 0)
        {
            width  = clientWidth;
            height = clientHeight;
            return;
        }

        height = virtualHeight_;
        if (presentationMode_ == CNA::Internal::Renderers::CnaPresentationMode::FixedHeightDynamicWidth
            && clientHeight > 0)
        {
            width = static_cast<int>(
                static_cast<double>(clientWidth) * virtualHeight_ / clientHeight + 0.5);
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : clientWidth;
        }
    }

    void VulkanRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != surfaceInfo_.windowId)
            throw CNA::Platform::PlatformException(
                "VulkanRenderer::OnSurfaceChanged", "stable window id changed");
        const bool sizeChanged = surface.drawableSize.width != surfaceInfo_.drawableSize.width
                              || surface.drawableSize.height != surfaceInfo_.drawableSize.height;
        surfaceInfo_ = surface;
        if (!(surfaceInfo_.displayScale > 0.0f)) surfaceInfo_.displayScale = 1.0f;
        if (initialized_ && sizeChanged && surfaceInfo_.drawableSize.width > 0
            && surfaceInfo_.drawableSize.height > 0)
            RecreateSwapchain();
    }

    // plan_vulkan.md VULKAN-331: both transforms map through the PRESENTED RECTANGLE, mirroring
    // EasyGLSurfaceState::WindowToLogical/LogicalToWindow. Before VULKAN-330 a height-derived
    // scale was equivalent, because no mode produced a rectangle smaller than the drawable; with
    // Letterbox and Overscan real, ignoring the rectangle's origin offsets every mapped coordinate
    // by the bar -- a game would render correctly and pick wrongly.
    bool VulkanRenderer::TransformWindowToLogical(const float windowX, const float windowY,
                                                   float& logX, float& logY) const
    {
        if (surfaceInfo_.windowId == 0 || !(surfaceInfo_.displayScale > 0.0f)) return false;

        const int logicalWidth  = virtualWidth_  > 0 ? virtualWidth_
                                                     : static_cast<int>(swapchainExtent_.width);
        const int logicalHeight = virtualHeight_ > 0 ? virtualHeight_
                                                     : static_cast<int>(swapchainExtent_.height);
        int vx = 0, vy = 0, vw = 0, vh = 0;
        GetPresentedRectEXT(vx, vy, vw, vh);
        if (logicalWidth <= 0 || logicalHeight <= 0 || vw <= 0 || vh <= 0) return false;

        // The rectangle is in drawable pixels; window coordinates are in client units.
        const float inverseDisplayScale = 1.0f / surfaceInfo_.displayScale;
        const float clientX = static_cast<float>(vx) * inverseDisplayScale;
        const float clientY = static_cast<float>(vy) * inverseDisplayScale;
        const float clientW = static_cast<float>(vw) * inverseDisplayScale;
        const float clientH = static_cast<float>(vh) * inverseDisplayScale;
        logX = (windowX - clientX) * static_cast<float>(logicalWidth)  / clientW;
        logY = (windowY - clientY) * static_cast<float>(logicalHeight) / clientH;
        return true;
    }

    bool VulkanRenderer::TransformLogicalToWindow(const float logX, const float logY,
                                                   float& windowX, float& windowY) const
    {
        if (surfaceInfo_.windowId == 0 || !(surfaceInfo_.displayScale > 0.0f)) return false;

        const int logicalWidth  = virtualWidth_  > 0 ? virtualWidth_
                                                     : static_cast<int>(swapchainExtent_.width);
        const int logicalHeight = virtualHeight_ > 0 ? virtualHeight_
                                                     : static_cast<int>(swapchainExtent_.height);
        int vx = 0, vy = 0, vw = 0, vh = 0;
        GetPresentedRectEXT(vx, vy, vw, vh);
        if (logicalWidth <= 0 || logicalHeight <= 0 || vw <= 0 || vh <= 0) return false;

        const float inverseDisplayScale = 1.0f / surfaceInfo_.displayScale;
        const float clientX = static_cast<float>(vx) * inverseDisplayScale;
        const float clientY = static_cast<float>(vy) * inverseDisplayScale;
        const float clientW = static_cast<float>(vw) * inverseDisplayScale;
        const float clientH = static_cast<float>(vh) * inverseDisplayScale;
        windowX = clientX + logX * clientW / static_cast<float>(logicalWidth);
        windowY = clientY + logY * clientH / static_cast<float>(logicalHeight);
        return true;
    }

    void VulkanRenderer::GetPresentedRectEXT(int& x, int& y, int& width, int& height) const
    {
        // plan_vulkan.md VULKAN-330. Deliberately the same algorithm as EasyGLSurfaceState's
        // GetDefaultViewportRect, whose own comment names it the established reference for
        // Letterbox/Overscan/Stretch in this codebase -- reproducing it keeps the modes meaning
        // the same thing on both renderers, which is the entire point of implementing them here.
        const int physWidth  = static_cast<int>(swapchainExtent_.width);
        const int physHeight = static_cast<int>(swapchainExtent_.height);

        x = 0;
        y = 0;
        width  = std::max(0, physWidth);
        height = std::max(0, physHeight);
        if (physWidth <= 0 || physHeight <= 0) return;

        // Full drawable, nothing to centre; a degenerate virtual resolution has no aspect to keep.
        if (presentationMode_ == CNA::Internal::Renderers::CnaPresentationMode::NativeBackBuffer ||
            presentationMode_ == CNA::Internal::Renderers::CnaPresentationMode::FixedHeightDynamicWidth ||
            presentationMode_ == CNA::Internal::Renderers::CnaPresentationMode::Stretch ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return;

        const double logicalWidth  = static_cast<double>(virtualWidth_);
        const double logicalHeight = static_cast<double>(virtualHeight_);
        const double scaleX = static_cast<double>(physWidth)  / logicalWidth;
        const double scaleY = static_cast<double>(physHeight) / logicalHeight;
        const double scale =
            (presentationMode_ == CNA::Internal::Renderers::CnaPresentationMode::Overscan)
                ? std::max(scaleX, scaleY)
                : std::min(scaleX, scaleY);

        width  = static_cast<int>(std::lround(logicalWidth  * scale));
        height = static_cast<int>(std::lround(logicalHeight * scale));
        x = static_cast<int>(std::lround((static_cast<double>(physWidth)  - logicalWidth  * scale) * 0.5));
        y = static_cast<int>(std::lround((static_cast<double>(physHeight) - logicalHeight * scale) * 0.5));
    }

    void VulkanRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_  = width;
        virtualHeight_ = height;
        if (initialized_ && width > 0 && height > 0)
            RecreateSwapchain();
    }

    void VulkanRenderer::DeviceWaitIdleEXT()
    {
        // plan_vulkan.md VULKAN-392: the ONLY place this renderer calls vkDeviceWaitIdle, so
        // GetDeviceWaitIdleCountEXT() cannot under-count and a stall reintroduced anywhere is
        // visible to a test. The four remaining callers are all reconfiguration or teardown --
        // the destructor, RecreateSwapchain, ApplyMultiSampleCount and FlushDeferredRenderTarget
        // -- never a per-draw path.
        ++deviceWaitIdleCountEXT_;
        vkDeviceWaitIdle(device_);
    }

    int VulkanRenderer::GetAppliedBackBufferFormatEXT(int requestedFormat) const
    {
        // plan_vulkan.md VULKAN-348. The argument is deliberately unused, exactly as in
        // GetAppliedMultiSampleCountEXT and for the same reason: both call sites use the answer as
        // a write-back of what is really in effect. The swapchain takes what the surface offers
        // (CreateSwapchain prefers B8G8R8A8_UNORM), never the requested SurfaceFormat, and this
        // renderer accepts SurfaceFormat::Color and nothing else -- so Color is not a fallback
        // here, it is the only answer the swapchain can produce.
        (void)requestedFormat;
        return static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    }

    int VulkanRenderer::GetAppliedDepthStencilFormatEXT(int requestedDepthStencilFormat) const
    {
        // VULKAN-348. Answered from depthFormat_ -- the format CreateDepthResources really chose
        // -- and not from the request, following the two precedents already in this renderer:
        // SupportsCapability(StencilBuffer) reads the same member for the same reason, and
        // VULKAN-347 settled the identical question for the sample count.
        //
        // Consequence worth knowing rather than discovering: CreateDepthResources calls
        // FindDepthFormat() unconditionally, so the backbuffer always HAS a depth buffer. A game
        // that asked for DepthFormat::None is therefore told the format it actually got, because
        // reporting None would describe a buffer that exists as absent -- and the capability
        // query already reports true for it.
        (void)requestedDepthStencilFormat;
        return XnaDepthFormatFromVkFormatEXT(depthFormat_);
    }

    int VulkanRenderer::GetMultiSampleCount() const
    {
        return SampleCountToInt(sampleCount_);
    }

    int VulkanRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const VkSampleCountFlagBits newCount = PickSampleCount(physicalDevice_, requestedMultiSampleCount);
        if (newCount == sampleCount_)
            return SampleCountToInt(sampleCount_);

        DeviceWaitIdleEXT();

        // Tear down every piece of state whose creation baked in the OLD sampleCount_ --
        // the backbuffer's MSAA render pass, every depth-format-keyed render-target/2D-sprite
        // MSAA render pass/pipeline (Task 911 — lazily recreated on next MSAA-enabled use,
        // regardless of depth format), and every lazily-created 3D pipeline (each VkPipeline
        // hardcodes rasterizationSamples at creation time). The sample-count-independent
        // renderPass_/pipelines2DByDepthFmt_/rtRenderPassByDepthFmt_/rtRenderPassLoadByDepthFmt_
        // are left untouched.
        auto clearPipelineCache = [this](auto& cache) {
            for (auto& [key, pipe] : cache)
                if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipe, nullptr);
            cache.clear();
        };
        clearPipelineCache(pipelines2DMsaaByDepthFmt_);
        // REMED-GFX-143: a swapchain load/store variant bakes in swapchainFormat_, sampleCount_ and
        // depthFormat_ exactly as the base passes do, so every one is dropped here and lazily
        // recreated against the new configuration.
        DestroySwapchainPassVariants();
        if (renderPassMsaa_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, renderPassMsaa_, nullptr); renderPassMsaa_ = VK_NULL_HANDLE; }
        for (auto& [fmt, rp] : rtRenderPassMsaaByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaByDepthFmt_.clear();
        // REMED-GFX-141: the load variant bakes in sampleCount_ exactly as the clear variant does,
        // so it is dropped and lazily recreated against the new configuration alongside it.
        for (auto& [fmt, rp] : rtRenderPassMsaaLoadByDepthFmt_)
            if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
        rtRenderPassMsaaLoadByDepthFmt_.clear();
        clearPipelineCache(pipelines3D_);
        clearPipelineCache(pipelinesAlphaTest3D_);
        clearPipelineCache(pipelinesDualTex3D_);
        clearPipelineCache(pipelinesEnvMap3D_);
        clearPipelineCache(pipelinesLitTextured3D_);
        clearPipelineCache(pipelinesLitTextured3DVertexLit_);
        clearPipelineCache(pipelinesFogColored3D_);
        clearPipelineCache(pipelinesFogTex3D_);
        clearPipelineCache(pipelinesSkinned3D_);
        clearPipelineCache(pipelinesSkinned3DVertexLit_);
        clearPipelineCache(pipelinesPbr3D_);
        clearPipelineCache(pipelinesPbrSkinned3D_);
        clearPipelineCache(pipelinesInstanced3D_);

        sampleCount_ = newCount;

        // CreateFramebuffers() (called by RecreateSwapchain() below) reads renderPassMsaa_
        // directly whenever sampleCount_ > 1, so it must already exist before that call. The 2D
        // MSAA sprite pipeline(s) are lazily recreated per depth format on next use instead
        // (Task 911), same as every 3D pipeline.
        if (sampleCount_ > VK_SAMPLE_COUNT_1_BIT) {
            CreateRenderPassMsaa();
        }
        RecreateSwapchain();

        std::clog << "[Vulkan] MultiSampleCount reset to "
                  << SampleCountToInt(sampleCount_) << "x\n";
        return SampleCountToInt(sampleCount_);
    }

    std::unique_ptr<ITextureRenderer> VulkanRenderer::CreateTexture(const ImageData& data)
    {
        auto tex = std::make_unique<VulkanTextureRenderer>(data, this);
        liveTextures_.push_back(tex.get());
        VkLifetimeTraceEXT("tex2d.create     renderer=%p %dx%d gen=%llu",
                           static_cast<const void*>(tex.get()), data.width, data.height,
                           static_cast<unsigned long long>(frameGeneration_));
        return tex;
    }

    std::unique_ptr<ISpriteBatchRenderer> VulkanRenderer::CreateSpriteBatch()
    {
        return std::make_unique<VulkanSpriteBatchRenderer>(this);
    }

    std::unique_ptr<IRenderTargetRenderer> VulkanRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // multiSampleCount is honored on a "piggyback on the renderer's own sampleCount_" basis
        // (Task 878/879) — see VulkanRenderTargetRenderer's constructor comment and
        // plans/plan_graphics.md for the exact scope decision. mipMap (Task 878) is a real
        // vkCmdBlitImage cascade regenerated every frame this RT is rendered into — see
        // VulkanRenderTargetRenderer::MaybeGenerateMips. depthFormat (Task 877) now gets true
        // per-instance fidelity (Task 911) — see VulkanRenderTargetRenderer's constructor comment.
        return std::make_unique<VulkanRenderTargetRenderer>(w, h, depthFormat, preserveContents, this,
                                                            multiSampleCount, mipMap);
    }

    // REMED-GFX-140: open a new logical render pass. The counter advances on EVERY call, even
    // when `rt` equals the currently bound target, because "bind A, draw, unbind, bind A again" is
    // two logical passes with two independent load actions -- merging them is the defect this
    // fixes. An advance that ends up with no queued entry costs nothing: RecordCommandBuffer builds
    // its segment list from the entries themselves, so an empty segment produces no native pass.
    void VulkanRenderer::BeginRenderPassSegmentEXT(std::shared_ptr<VulkanRTSource> rt)
    {
        currentRT_ = std::move(rt);
        ++currentSegment_;
    }

    // REMED-GFX-140 / Task 875: record this bind cycle's clear values, and mark the cycle as
    // needing its render pass recorded even with no draw call (matching FNA/XNA, where Clear()
    // takes effect regardless of what is drawn afterwards).
    //
    // REMED-GFX-143: the BACKBUFFER is recorded too (currentRT_ == nullptr). It used to be skipped
    // because there was exactly one swapchain pass per frame, which simply read the frame-global
    // clearR_/clearDepth_/clearStencil_ as they stood at record time -- so the frame's LAST Clear()
    // supplied the colour for the whole frame's backbuffer work and an earlier cycle's Clear() was
    // both mistimed and lost. A backbuffer cycle now owns its clear exactly as an off-screen cycle
    // does, and `wantColor`/`wantDepth`/`wantStencil` say which aspects it asked for so a later
    // cycle can clear one and LOAD the others.
    void VulkanRenderer::NoteRenderTargetClearEXT(bool color, bool depth, bool stencil)
    {
        // REMED-GFX-129: one record per public Clear(), never merged. The previous code folded a
        // second Clear() of the same bind cycle into the first, because the only delivery mechanism
        // was the pass load op and a pass has exactly one -- which is exactly why "Clear A; draw;
        // Clear B" could not be expressed. RecordCommandBuffer replays these in `order` between the
        // draws they were issued between, and may fold only a LEADING one into the load action.
        pendingClears_.push_back({ currentSegment_, currentRT_,
                                   clearR_, clearG_, clearB_, clearA_,
                                   clearDepth_, clearStencil_,
                                   color, depth, stencil,
                                   NextCommandOrderEXT() });
        VkLifetimeTraceEXT("enqueue.clear    order=%llu rt=%p seg=%llu color=%d depth=%d stencil=%d",
                           static_cast<unsigned long long>(pendingClears_.back().order),
                           static_cast<const void*>(currentRT_.get()),
                           static_cast<unsigned long long>(currentSegment_),
                           color ? 1 : 0, depth ? 1 : 0, stencil ? 1 : 0);
    }

    namespace
    {
        /// REMED-GFX-129: does this depth attachment format own a depth aspect at all?
        bool VkDepthFormatHasDepth(VkFormat f)
        {
            return f != VK_FORMAT_UNDEFINED && f != VK_FORMAT_S8_UINT;
        }
        /// REMED-GFX-129: ... and a stencil aspect? Clearing the stencil aspect of a depth-only
        /// attachment is invalid usage (VUID-vkCmdClearAttachments-aspectMask-02502), so a public
        /// ClearOptions::Stencil on a Depth24/Depth16 target must drop that bit, not pass it on.
        bool VkDepthFormatHasStencil(VkFormat f)
        {
            return f == VK_FORMAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT ||
                   f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
    }

    // REMED-GFX-129: the ordered clear command itself.
    //
    // vkCmdClearAttachments is the only clear Vulkan offers INSIDE a render pass, and it is exactly
    // what XNA's contract needs: it happens where it is recorded, relative to the draws around it.
    // vkCmdClearColorImage is deliberately not used -- it is a transfer-stage command that cannot be
    // recorded inside a render pass, so reaching for it would mean breaking the pass in two and
    // re-transitioning the image for every Clear() the game issues.
    //
    // The clear RECTANGLE is the whole render area, never the Viewport or the ScissorRectangle:
    // REMED-GFX-018 established that contract for this project on Bgfx ("each clear is a full-target
    // ordered operation ... viewport and scissor do not restrict Clear"), it matches what the pass
    // load action this replaces already did, and checks V1/V2 of
    // modules/graphics/examples/graphicsdevice_ordered_clear_test.cpp assert it on every renderer that runs them.
    // layerCount is 1 because every framebuffer this renderer builds is single-layer -- a cube face
    // has its own framebuffer over its own single-layer view (REMED-GFX-134), so the face is already
    // selected by the framebuffer and never by a base array layer here.
    void VulkanRenderer::RecordOrderedClearEXT(VkCommandBuffer cb, const PendingClear& clear,
                                                     uint32_t colorAttachmentCount,
                                                     VkFormat depthFmt,
                                                     uint32_t width, uint32_t height) const
    {
        if (cb == VK_NULL_HANDLE || width == 0 || height == 0) return;

        // MAX_RENDERTARGET_BINDINGS is 4, so 4 colour entries plus one combined depth/stencil is
        // the ceiling; the guard keeps this correct if that limit is ever raised.
        std::array<VkClearAttachment, 5> atts{};
        uint32_t n = 0;
        if (clear.wantColor) {
            // Every bound colour attachment, not just attachment 0: XNA's ClearOptions::Target
            // names the render target, and an MRT set is one render target with several
            // attachments. The 2D and 3D pipelines write attachment 0 only, so this is the single
            // place where the other attachments of an MRT set are ever written.
            for (uint32_t i = 0; i < colorAttachmentCount && n < atts.size(); ++i) {
                atts[n].aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
                atts[n].colorAttachment  = i;
                atts[n].clearValue.color = { { clear.r, clear.g, clear.b, clear.a } };
                ++n;
            }
        }
        VkImageAspectFlags dsAspect = 0;
        if (clear.wantDepth   && VkDepthFormatHasDepth(depthFmt))   dsAspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (clear.wantStencil && VkDepthFormatHasStencil(depthFmt)) dsAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (dsAspect != 0 && n < atts.size()) {
            atts[n].aspectMask = dsAspect;
            atts[n].clearValue.depthStencil = {
                clear.depth, static_cast<uint32_t>(clear.stencil)
            };
            ++n;
        }
        // A request that names nothing this attachment set owns (a stencil clear on a depthless
        // target, say) records no command at all rather than an empty one.
        if (n == 0) return;

        VkClearRect rect{};
        rect.rect           = { {0, 0}, { width, height } };
        rect.baseArrayLayer = 0;
        rect.layerCount     = 1;
        vkCmdClearAttachments(cb, n, atts.data(), 1, &rect);
    }

    void VulkanRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt == nullptr) {
            BeginRenderPassSegmentEXT(nullptr);
        } else {
            auto* vrt = static_cast<VulkanRenderTargetRenderer*>(rt);
            // BindAsRenderTarget() opens the segment; assigning currentRT_ here as well would only
            // burn an id on an empty segment (harmless, but pointlessly noisy in the queues).
            vrt->BindAsRenderTarget();
        }
    }

    std::unique_ptr<IVertexBufferRenderer> VulkanRenderer::CreateVertexBuffer(int cap)
    {
        auto vb = std::make_unique<VulkanVertexBufferRenderer>(cap, this);
        liveVertexBuffers_.push_back(vb.get());
        return vb;
    }

    bool VulkanOcclusionQueryRenderer::PixelCountIsPreciseEXT() const noexcept
    {
        return owner_ != nullptr && owner_->occlusionQueryPreciseSupported_;
    }

    VkDeviceSize VulkanRenderer::GetLiveVertexBufferBytesEXT() const noexcept
    {
        VkDeviceSize total = 0;
        for (const auto* vb : liveVertexBuffers_)
            total += vb->GetAllocatedBytesEXT();
        return total;
    }

    VkDeviceSize VulkanRenderer::GetLiveIndexBufferBytesEXT() const noexcept
    {
        VkDeviceSize total = 0;
        for (const auto* ib : liveIndexBuffers_)
            total += ib->GetAllocatedBytesEXT();
        return total;
    }

    std::unique_ptr<IIndexBufferRenderer> VulkanRenderer::CreateIndexBuffer16(int cap)
    {
        auto ib = std::make_unique<VulkanIndexBufferRenderer>(cap, false, this);
        liveIndexBuffers_.push_back(ib.get());
        return ib;
    }

    std::unique_ptr<IIndexBufferRenderer> VulkanRenderer::CreateIndexBuffer32(int cap)
    {
        auto ib = std::make_unique<VulkanIndexBufferRenderer>(cap, true, this);
        liveIndexBuffers_.push_back(ib.get());
        return ib;
    }

    // ---- 3D pipeline ----

    void VulkanRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearDepth_ = depth;
        readbackStagingValid_ = false;  // new frame content invalidates the readback cache
        // Task 875: see Clear()'s identical fix.
        NoteRenderTargetClearEXT(true, true, false);
    }

    // REMED-GFX-129: this used to update clearDepth_ and stop. With the clear value delivered only
    // through the pass load op there was nothing else it COULD do -- a depth-only clear cannot be
    // expressed as a load action without also taking the colour one the target's usage had chosen --
    // so `Clear(ClearOptions::DepthBuffer, ...)` did nothing at all on Vulkan. It now records an
    // ordered clear request naming the depth aspect only, exactly like every other Clear* entry
    // point here.
    void VulkanRenderer::ClearDepth(float depth)
    {
        clearDepth_ = depth;
        readbackStagingValid_ = false;
        NoteRenderTargetClearEXT(false, true, false);
    }

    // Task 871 introduced these stencil-aware entry points as value-only updates, because every
    // render pass cleared its colour/depth attachments unconditionally and there was no
    // per-request selectivity to hook into. REMED-GFX-129 replaced that with a real ordered clear
    // command, so each one now records exactly the aspects its ClearOptions mask named.
    void VulkanRenderer::ClearStencil(int stencil)
    {
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        NoteRenderTargetClearEXT(false, false, true);
    }

    void VulkanRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        clearDepth_ = depth;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        NoteRenderTargetClearEXT(false, true, true);
    }

    void VulkanRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        NoteRenderTargetClearEXT(true, false, true);
    }

    void VulkanRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
        clearDepth_ = depth;
        clearStencil_ = stencil;
        readbackStagingValid_ = false;
        NoteRenderTargetClearEXT(true, true, true);
    }

    void VulkanRenderer::SetDepthTestEnabled(bool v)  { depthTestEnabled_  = v; }
    void VulkanRenderer::SetBlendEnabled(bool v)      { blendEnabled_      = v; }
    void VulkanRenderer::SetDepthWriteEnabled(bool v) { depthWriteEnabled_ = v; }

    void VulkanRenderer::PushPending3DDraw(Pending3DDraw&& d)
    {
        d.occlusionQuery = activeOcclusionQuery_;
        // REMED-GFX-013: snapshot the scissor active at enqueue time (see Pending3DDraw) so the
        // render-target pass clips this draw with the rect that was set while its RT was bound,
        // not the frame-global rect left over at Present() (which Task 338 resets on RT unbind).
        d.scissorEnabled = scissorEnabled_;
        d.scissorX = scissorX_; d.scissorY = scissorY_;
        d.scissorW = scissorW_; d.scissorH = scissorH_;
        // REMED-GFX-062: snapshot the viewport active at enqueue time (see Pending3DDraw) so the
        // render-target pass draws this call under the Viewport that was set while its RT was bound,
        // not the frame-global viewport left over at Present() (SetRenderTarget resets it to the
        // full target size on RT bind/unbind).
        d.viewportSet = viewportSet_;
        d.viewportX = viewportX_; d.viewportY = viewportY_;
        d.viewportW = viewportW_; d.viewportH = viewportH_;
        d.viewportMinDepth = viewportMinDepth_;
        d.viewportMaxDepth = viewportMaxDepth_;
        // REMED-GFX-070: snapshot the blend constant active at enqueue time (see Pending3DDraw) so
        // the render-target pass replays this draw with the BlendFactor that was set when it was
        // issued, not the frame-global value (RT passes never set it pre-fix; multiple BlendFactor
        // values in one frame otherwise collapse to the single record-time read).
        d.blendFactorR = blendFactorR_; d.blendFactorG = blendFactorG_;
        d.blendFactorB = blendFactorB_; d.blendFactorA = blendFactorA_;
        // REMED-GFX-140: tag the draw with the logical render pass (bind cycle) it was issued in,
        // so RecordCommandBuffer replays it inside that cycle's own pass and no other. Every
        // DrawXPrimitives() call site routes through here, so no site can forget it.
        d.segment = currentSegment_;
        // REMED-GFX-129: position in the frame's public command stream, so an ordered Clear() can
        // be replayed between exactly the draws it was issued between.
        d.order = NextCommandOrderEXT();
        if (VulkanLifetimeTraceOnEXT())
            VkLifetimeTraceEXT("enqueue.3D       order=%llu family=%s rt=%p seg=%llu "
                               "descSet=0x%llx dualTex=0x%llx envMap=0x%llx lit=0x%llx "
                               "skinned=0x%llx pbr=0x%llx fogTex=0x%llx",
                               static_cast<unsigned long long>(d.order), Pending3DFamilyEXT(d),
                               static_cast<const void*>(d.rt.get()),
                               static_cast<unsigned long long>(d.segment),
                               VkH(d.descSet), VkH(d.dualTexDescSet), VkH(d.envMapDescSet),
                               VkH(d.litTexturedDescSet), VkH(d.skinnedDescSet),
                               VkH(d.pbrDescSet), VkH(d.fogTex3DDescSet));
        pending3D_.push_back(std::move(d));
    }

    // REMED-GFX-075: see the header. Tag a bundle of retired Vulkan handles with the current
    // frameGeneration_ and enqueue it; ProcessRetiredResources() frees it once the frame that
    // consumes any deferred entry referencing it has certainly completed on the GPU.
    void VulkanRenderer::RetireResources(RetiredResources&& r)
    {
        VkLifetimeTraceEXT("retire.insert    gen=%llu views=%zu images=%zu mem=%zu fbs=%zu "
                           "descSets=%zu poolSets=%zu",
                           static_cast<unsigned long long>(frameGeneration_),
                           r.imageViews.size(), r.images.size(), r.memories.size(),
                           r.framebuffers.size(), r.descriptorSets.size(),
                           r.poolDescriptorSets.size());
        // plan_vulkan.md VULKAN-392: counted so a test can show the buffer create/destroy cycles
        // it is measuring really happened, rather than inferring it from a pixel.
        retiredBufferCountEXT_ += r.buffers.size();
        r.generation = frameGeneration_;
        retiredResources_.push_back(std::move(r));
    }

    // REMED-GFX-075: evict every persistent per-(view,sampler) descriptor-set cache entry keyed on a
    // dying sampled view, moving the cached VkDescriptorSet into `into` for frame-fence-gated free.
    // Prevents a later resource that reuses the freed VkImageView handle value from being handed a
    // stale descriptor set that still samples the destroyed image.
    void VulkanRenderer::EvictSampledViewFromCaches(VkImageView view, RetiredResources& into)
    {
        if (view == VK_NULL_HANDLE) return;
        const std::size_t before = into.descriptorSets.size() + into.poolDescriptorSets.size();
        // The plain sprite/3D single-sampler cache is (view,sampler)-keyed -> direct reverse lookup.
        for (auto it = texSamplerDescSets_.begin(); it != texSamplerDescSets_.end(); )
        {
            if (it->first.first == view) {
                // VULKAN-390: freed against the pool it was allocated from, which is no longer
                // always descriptorPool_.
                if (it->second.set != VK_NULL_HANDLE)
                    into.poolDescriptorSets.emplace_back(it->second.pool, it->second.set);
                it = texSamplerDescSets_.erase(it);
            } else {
                ++it;
            }
        }
        // REMED-GFX-076: the seven per-frame effect caches are hash-keyed, so they carry each entry's
        // referencing views for reverse lookup here. Evict every entry this dying view participates
        // in; the freed set is fence-retired to its own pool (see ProcessRetiredResources).
        EvictViewFromEffectCache(dualTexDescSets_,     descriptorPool2Tex_,        view, into);
        EvictViewFromEffectCache(envMapDescSets_,      descriptorPoolEnvMap_,      view, into);
        EvictViewFromEffectCache(litTexturedDescSets_, descriptorPoolLitTextured_, view, into);
        EvictViewFromEffectCache(fogTex3DDescSets_,    descriptorPoolFogTex3D_,    view, into);
        EvictViewFromEffectCache(skinnedDescSets_,     descriptorPoolSkinned_,     view, into);
        EvictViewFromEffectCache(pbrDescSets_,         descriptorPoolPbr_,         view, into);
        EvictViewFromEffectCache(pbrSkinnedDescSets_,  descriptorPoolPbrSkinned_,  view, into);
        VkLifetimeTraceEXT("cache.evict      view=0x%llx evictedSets=%zu gen=%llu",
                           VkH(view),
                           into.descriptorSets.size() + into.poolDescriptorSets.size() - before,
                           static_cast<unsigned long long>(frameGeneration_));
    }

    // REMED-GFX-076: drop every entry in one effect descriptor-set cache (all frame slots) that
    // references `view`, moving its set to `into` for frame-fence-gated free from `pool`. Mirrors
    // the texSamplerDescSets_ eviction above, but keyed via each entry's recorded views (the hash
    // key is not reversible). Rare (once per dying sampled resource) and bounded by the small,
    // pool-capped cache size, so the full-cache scan is acceptable.
    void VulkanRenderer::EvictViewFromEffectCache(EffectDescSetCache& caches,
        VkDescriptorPool pool, VkImageView view, RetiredResources& into)
    {
        for (auto& cache : caches)
        {
            for (auto it = cache.begin(); it != cache.end(); )
            {
                bool refs = false;
                for (VkImageView v : it->second.views) if (v == view) { refs = true; break; }
                if (refs) {
                    if (it->second.set != VK_NULL_HANDLE)
                        into.poolDescriptorSets.emplace_back(pool, it->second.set);
                    it = cache.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // REMED-GFX-076: read-only test introspection (see header). Sum of live entries over all seven
    // per-frame effect descriptor-set caches. Not part of the render path.
    std::size_t VulkanRenderer::TotalEffectDescSetEntriesForTests() const
    {
        std::size_t n = 0;
        auto add = [&n](const EffectDescSetCache& c) { for (const auto& m : c) n += m.size(); };
        add(dualTexDescSets_);     add(envMapDescSets_);   add(litTexturedDescSets_);
        add(fogTex3DDescSets_);    add(skinnedDescSets_);  add(pbrDescSets_);
        add(pbrSkinnedDescSets_);
        return n;
    }

    // REMED-GFX-076: read-only test introspection (see header). Count effect-cache entries whose
    // recorded views include `rawImageViewHandle` (a VkImageView cast to uint64_t).
    std::size_t VulkanRenderer::EffectDescSetEntriesForViewInTests(uint64_t rawImageViewHandle) const
    {
        const VkImageView view = reinterpret_cast<VkImageView>(rawImageViewHandle);
        if (view == VK_NULL_HANDLE) return 0; // never match the unused (VK_NULL_HANDLE) padding slots
        std::size_t n = 0;
        auto scan = [&](const EffectDescSetCache& c) {
            for (const auto& m : c)
                for (const auto& kv : m)
                    for (VkImageView v : kv.second.views)
                        if (v == view) { ++n; break; }
        };
        scan(dualTexDescSets_);     scan(envMapDescSets_);   scan(litTexturedDescSets_);
        scan(fogTex3DDescSets_);    scan(skinnedDescSets_);  scan(pbrDescSets_);
        scan(pbrSkinnedDescSets_);
        return n;
    }

    // REMED-GFX-075: free every retirement bucket whose consuming frame's fence has certainly
    // completed. A bucket retired at generation G is (at most) referenced by the Full record at
    // generation G+1; that record's fence is guaranteed signalled before generation G+1+MaxFrames
    // InFlight begins (slot reuse waits it), so `generation + MaxFramesInFlight < frameGeneration_`
    // is a safe, strictly-conservative free condition. `force` frees everything (teardown only,
    // after a full device wait). Runs once per frame at SubmitFrame's fence-wait sync point.
    void VulkanRenderer::TrimSamplerCacheEXT()
    {
        // VULKAN-160. `VULKAN-395` measured why this is needed: two of `SamplerStateKey`'s seven
        // fields are caller-supplied numbers, so the key space is unbounded and a game animating a
        // LOD bias allocates a VkSampler per frame forever, until the device's
        // maxSamplerAllocationCount is gone.
        if (samplerCache_.size() <= kMaxCachedSamplers) return;

        // Pinned: anything a device slot currently points at, and the renderer's own default.
        // These are live handles the next draw will use, whatever their age.
        const auto pinned = [this](VkSampler s) {
            if (s == defaultSampler_) return true;
            for (VkSampler slot : slotSamplers_) if (slot == s) return true;
            return false;
        };

        // Oldest first. A linear sort is fine: this runs only when the bound is exceeded, which
        // real content never does.
        std::vector<std::pair<std::uint64_t, SamplerStateKey>> byAge;
        byAge.reserve(samplerCache_.size());
        for (const auto& [key, entry] : samplerCache_)
            if (!pinned(entry.sampler)) byAge.emplace_back(entry.lastUsed, key);
        std::sort(byAge.begin(), byAge.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        RetiredResources into;
        std::size_t remaining = samplerCache_.size();
        for (const auto& [age, key] : byAge) {
            if (remaining <= kMaxCachedSamplers) break;
            const auto it = samplerCache_.find(key);
            if (it == samplerCache_.end()) continue;
            const VkSampler dying = it->second.sampler;
            samplerCache_.erase(it);
            --remaining;
            if (dying == VK_NULL_HANDLE) continue;
            into.samplers.push_back(dying);
            // Every single-sampler descriptor set keyed on it dies with it -- the same reverse
            // lookup EvictSampledViewFromCaches does for a dying view, with the other half of the
            // key.
            for (auto ds = texSamplerDescSets_.begin(); ds != texSamplerDescSets_.end(); ) {
                if (ds->first.second == dying) {
                    if (ds->second.set != VK_NULL_HANDLE)
                        into.poolDescriptorSets.emplace_back(ds->second.pool, ds->second.set);
                    ds = texSamplerDescSets_.erase(ds);
                } else {
                    ++ds;
                }
            }
        }
        if (into.samplers.empty()) return;

        // The seven per-frame EFFECT caches are the one place a reverse lookup is impossible:
        // `EffectDescSetEntry` records the views an entry references (REMED-GFX-076) but not the
        // samplers, and the samplers are only hashed into the key. Rather than widen seven caches
        // and their insert sites for a path that runs only in the pathological case, every entry is
        // dropped when any sampler is evicted. It costs one frame of re-population, and only for a
        // game that has already exceeded a 256-sampler bound.
        const auto flush = [&into](EffectDescSetCache& cache, VkDescriptorPool pool) {
            for (auto& perFrame : cache) {
                for (auto& [hash, entry] : perFrame)
                    if (entry.set != VK_NULL_HANDLE)
                        into.poolDescriptorSets.emplace_back(pool, entry.set);
                perFrame.clear();
            }
        };
        flush(dualTexDescSets_,     descriptorPool2Tex_);
        flush(envMapDescSets_,      descriptorPoolEnvMap_);
        flush(litTexturedDescSets_, descriptorPoolLitTextured_);
        flush(fogTex3DDescSets_,    descriptorPoolFogTex3D_);
        flush(skinnedDescSets_,     descriptorPoolSkinned_);
        flush(pbrDescSets_,         descriptorPoolPbr_);
        flush(pbrSkinnedDescSets_,  descriptorPoolPbrSkinned_);

        VkLifetimeTraceEXT("cache.trimSampler evicted=%zu remaining=%zu sets=%zu gen=%llu",
                           into.samplers.size(), samplerCache_.size(),
                           into.poolDescriptorSets.size(),
                           static_cast<unsigned long long>(frameGeneration_));
        RetireResources(std::move(into));
    }

    void VulkanRenderer::ProcessRetiredResources(bool force)
    {
        if (device_ == VK_NULL_HANDLE) return;
        auto freeBucket = [this](RetiredResources& r) {
            VkLifetimeTraceEXT("retire.FREE      gen=%llu now=%llu views=%zu images=%zu fbs=%zu "
                               "descSets=%zu",
                               static_cast<unsigned long long>(r.generation),
                               static_cast<unsigned long long>(frameGeneration_),
                               r.imageViews.size(), r.images.size(), r.framebuffers.size(),
                               r.descriptorSets.size());
            for (VkDescriptorSet s : r.descriptorSets)
                if (s != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(device_, descriptorPool_, 1, &s);
            // REMED-GFX-076: effect-cache sets evicted on a sampled view's death, each freed from its
            // own pool (created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT).
            for (auto& ps : r.poolDescriptorSets)
                if (ps.second != VK_NULL_HANDLE && ps.first != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(device_, ps.first, 1, &ps.second);
            // VULKAN-160: samplers evicted from the cache past its bound, destroyed only once the
            // frame that could still reference them has provably completed.
            for (VkSampler sm : r.samplers)          if (sm != VK_NULL_HANDLE) vkDestroySampler(device_, sm, nullptr);
            for (VkImageView v : r.imageViews)       if (v  != VK_NULL_HANDLE) vkDestroyImageView(device_, v, nullptr);
            for (VkImage im : r.images)              if (im != VK_NULL_HANDLE) vkDestroyImage(device_, im, nullptr);
            // VULKAN-392: before the memories below -- a VkBuffer must die before the memory it is bound to.
            for (VkBuffer b : r.buffers)             if (b  != VK_NULL_HANDLE) vkDestroyBuffer(device_, b, nullptr);
            for (VkDeviceMemory m : r.memories)      if (m  != VK_NULL_HANDLE) vkFreeMemory(device_, m, nullptr);
            for (VkFramebuffer fb : r.framebuffers)  if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
            for (VkPipeline p : r.pipelines)         if (p  != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
            for (VkPipelineLayout pl : r.pipelineLayouts) if (pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pl, nullptr);
            for (VkShaderModule sm : r.shaderModules) if (sm != VK_NULL_HANDLE) vkDestroyShaderModule(device_, sm, nullptr);
            for (VkQueryPool qp : r.queryPools)      if (qp != VK_NULL_HANDLE) vkDestroyQueryPool(device_, qp, nullptr);
        };
        // GFX-095 MRT framebuffers borrow their targets' attachment views. Destroy an eligible
        // proxy before the same-generation resource bucket can free those views.
        for (auto it = retiredMrtProxies_.begin(); it != retiredMrtProxies_.end(); )
        {
            if (force || it->first + MaxFramesInFlight < frameGeneration_)
                it = retiredMrtProxies_.erase(it);   // last share frees the proxy (device_ still valid)
            else
                ++it;
        }
        for (auto it = retiredResources_.begin(); it != retiredResources_.end(); )
        {
            if (force || it->generation + MaxFramesInFlight < frameGeneration_) {
                freeBucket(*it);
                it = retiredResources_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // REMED-GFX-075: null a dying OcclusionQuery out of every pending 3D draw (the draw survives;
    // only the query correlation is dropped, whose result is unobservable once the query is disposed)
    // and out of activeOcclusionQuery_, so RecordCommandBuffer never dereferences the freed wrapper.
    void VulkanRenderer::PurgeDeferredQuery(VulkanOcclusionQueryRenderer* q)
    {
        if (!q) return;
        for (auto& d : pending3D_)
            if (d.occlusionQuery == q) d.occlusionQuery = nullptr;
        if (activeOcclusionQuery_ == q) activeOcclusionQuery_ = nullptr;
    }

    // REMED-GFX-166: see the header. The two counts at the end are the whole point -- they say, at
    // the instant of disposal, how much already-issued work would still have to run for this
    // resource's observable effect to survive.
    void VulkanRenderer::TraceTargetDisposalEXT(const char* kind, const void* renderer,
                                                      const void* dest, VkImage image,
                                                      VkImageView view, VkFramebuffer fb) const
    {
        if (!VulkanLifetimeTraceOnEXT()) return;
        std::size_t asDest = 0;
        if (dest) {
            for (const auto& d : pending3D_)     if (static_cast<const void*>(d.rt.get()) == dest) ++asDest;
            for (const auto& b : activeBatches_) if (static_cast<const void*>(b.rt.get()) == dest) ++asDest;
            for (const auto& c : pendingClears_) if (static_cast<const void*>(c.rt.get()) == dest) ++asDest;
        }
        std::size_t asSource = 0;
        for (const auto& e : segmentSampledGroups_)
            if (dest && e.second == dest) ++asSource;
        VkLifetimeTraceEXT("%s.dispose     renderer=%p dest=%p image=0x%llx view=0x%llx fb=0x%llx "
                           "gen=%llu queuedAsDestination=%zu queuedAsSampledSource=%zu",
                           kind, renderer, dest, VkH(image), VkH(view), VkH(fb),
                           static_cast<unsigned long long>(frameGeneration_), asDest, asSource);
    }

    // REMED-GFX-151: the depth/stencil group that identifies a sampled render target, or nullptr
    // when this texture is an ordinary one. `DepthStencilOwnerEXT()` is reused deliberately -- it is
    // already what FlushDeferredRenderTarget and RecordCommandBuffer treat as "one target" (all six
    // faces of a RenderTargetCube answer with the cube's own colour image), so a dependency and the
    // work that satisfies it are keyed identically and cannot drift apart.
    // REMED-GFX-166: a RenderTarget2D's destination is its VulkanTargetPassEXT, not the renderer
    // object itself, so the question "is this sampled texture a render target, and which one" is
    // asked of the concrete type and answered by that pass. Deliberately a dynamic_cast to the
    // concrete class and not a static one between siblings -- REMED-GFX-152's defect exactly.
    const void* VulkanRenderer::SampledRenderTargetGroupEXT(const ITextureRenderer* tex)
    {
        if (const auto* rt = dynamic_cast<const VulkanRenderTargetRenderer*>(tex))
            return rt->PassEXT() ? rt->PassEXT()->DepthStencilOwnerEXT() : nullptr;
        return nullptr;
    }

    const void* VulkanRenderer::SampledRenderTargetGroupEXT(const ITextureCubeRenderer* cube)
    {
        if (const auto* rtc = dynamic_cast<const VulkanRenderTargetCubeRenderer*>(cube))
            return rtc->RenderTargetGroupEXT();
        return nullptr;
    }

    // REMED-GFX-151: remember that bind cycle `segment` reads from render target group `group`.
    // Duplicates are dropped so a batch of a thousand sprites off one target costs one entry.
    void VulkanRenderer::NoteSampledRenderTargetGroupEXT(uint64_t segment, const void* group)
    {
        if (group == nullptr) return;
        for (const auto& e : segmentSampledGroups_)
            if (e.first == segment && e.second == group) return;
        segmentSampledGroups_.emplace_back(segment, group);
    }

    void VulkanRenderer::NoteSampledRenderTargetEXT(uint64_t segment,
                                                           const IVulkanSamplable* tex)
    {
        if (!tex) return;
        // An IVulkanSamplable and an ITextureRenderer are separate bases of the same object, so the
        // cast has to go through the most-derived object rather than between siblings.
        if (const auto* rt = dynamic_cast<const VulkanRenderTargetRenderer*>(tex))
            if (rt->PassEXT())
                NoteSampledRenderTargetGroupEXT(segment, rt->PassEXT()->DepthStencilOwnerEXT());
    }

    // REMED-GFX-151: every render target this draw samples, in one place, so a new stock-effect
    // texture slot cannot silently escape the dependency graph -- GpuDrawParams is the single
    // structure every 3D texture reaches this renderer through.
    void VulkanRenderer::NoteSampledSourcesEXT(const GpuDrawParams& params)
    {
        const ITextureRenderer* const slots[] = {
            params.texture0, params.texture1, params.pbrNormalMap,
            params.pbrMetallicRoughnessMap, params.pbrEmissiveMap, params.pbrOcclusionMap
        };
        for (const ITextureRenderer* t : slots)
            NoteSampledRenderTargetGroupEXT(currentSegment_, SampledRenderTargetGroupEXT(t));
        NoteSampledRenderTargetGroupEXT(currentSegment_, SampledRenderTargetGroupEXT(params.envMap));
    }

    // REMED-GFX-074: record + submit the off-screen passes a GetData readback of `rt` depends on
    // now (no present), so `rt`'s colour image holds the queued sprite/3D result, then drop the
    // consumed entries so the eventual Present() does not replay them. No-op when nothing is queued
    // for `rt` (its colour image already holds the last rendered content). A device wait first
    // ensures no in-flight frame is still using the per-frame ring buffers / UBO pools this record
    // reuses.
    //
    // REMED-GFX-151: "the passes it DEPENDS ON", not "`rt`'s passes". Pre-fix this recorded the
    // segments naming `rt` and nothing else, which silently assumed a target's content depends only
    // on draws issued INTO it. It also depends on every render target those draws SAMPLE, and a
    // producer is a different target -- so the canonical render-to-texture sequence (render into A,
    // unbind, draw with A into B, read B) recorded B's consumer pass while A's producer pass had
    // never been recorded at all. Only an intervening `A.GetData()` repaired it, by running this
    // same flush for A first; GetData was an accidental execution barrier.
    //
    // The set is now a transitive closure: seed with every pending segment of `rt`'s own group, then
    // repeatedly pull in, for each segment already in the set, the pending segments of every group
    // that segment samples which PRECEDE it. It terminates because each pulled-in segment has a
    // strictly smaller id than the one that needed it.
    //
    // Deliberately a closure and not "every off-screen segment up to `rt`'s last one": the wider
    // rule also fixes the finding, but it advances UNRELATED targets early, and a backbuffer draw
    // still deferred to Present would then sample a target whose later bind cycle had already run.
    // Measured, not reasoned: leg I2 of the REMED-GFX-151 fixture (`bind u; draw; unbind; draw u on
    // the backbuffer; bind u; draw; unbind; bind t; draw; unbind; t.GetData()`) reproduced 0/32 with
    // the positional rule and is exact with this one. Pulling in only genuine producers keeps
    // REMED-GFX-143's ascending-id backbuffer contract intact.
    void VulkanRenderer::FlushDeferredRenderTarget(
        VulkanRTSource* rt, const VulkanTargetPassEXT* mrtAttachmentPass,
        int requestedMipLevel)
    {
        lastMrtReadbackMatchesEXT_.clear();
        if (!initialized_ || !rt || device_ == VK_NULL_HANDLE) return;

        // Every pending OFF-SCREEN bind cycle this frame, with the group that owns it. Backbuffer
        // cycles are excluded: they need a swapchain image, REMED-GFX-144's one-acquire-one-submit-
        // one-present-per-frame must not change, and the backbuffer can never be a texture source,
        // so excluding them can never lose a producer.
        struct PendingSegment { uint64_t id; const void* group; };
        std::vector<PendingSegment> pendingSegments;
        auto notePendingSegment = [&pendingSegments](const VulkanRTSource* source, uint64_t id) {
            if (source == nullptr) return;
            for (const auto& s : pendingSegments) if (s.id == id) return;
            pendingSegments.push_back({ id, source->DepthStencilOwnerEXT() });
        };
        for (const auto& p : activeBatches_)  notePendingSegment(p.rt.get(), p.segment);
        for (const auto& d : pending3D_)      notePendingSegment(d.rt.get(), d.segment);
        for (const auto& c : pendingClears_)  notePendingSegment(c.rt.get(), c.segment);

        // Seed: `rt`'s own depth/stencil group. REMED-GFX-142 -- a RenderTargetCube's six faces
        // share one depth image, so reading one face replays the whole group in public order rather
        // than that face alone, and for every other source the group is the target itself.
        std::vector<const void*> groups{ rt->DepthStencilOwnerEXT() };
        // REMED-GFX-194: an MRT draw is queued against one frame-lifetime proxy, not against the
        // constituent target pass. Match the exact immutable pass retained in that proxy's public
        // attachment vector. For a cube this distinguishes all six faces even though they share a
        // parent image/depth group; for every target it also keeps separate proxy instances (and
        // therefore binding cycles) distinct. The requested level is validated against this exact
        // pass' mip chain rather than being erased into parent-resource identity.
        if (mrtAttachmentPass) {
            auto noteProxy = [&](VulkanMRTProxy* proxy) {
                if (!proxy) return;
                const int slot =
                    proxy->FindColorAttachmentSlotEXT(*mrtAttachmentPass, requestedMipLevel);
                if (slot < 0) return;
                const void* g = proxy->DepthStencilOwnerEXT();
                bool hasPendingCycle = false;
                for (const auto& pending : pendingSegments)
                    if (pending.group == g) {
                        hasPendingCycle = true;
                        lastMrtReadbackMatchesEXT_.emplace_back(
                            pending.id, static_cast<uint32_t>(slot));
                    }
                if (!hasPendingCycle) return;
                for (const void* have : groups) if (have == g) return;
                groups.push_back(g);
            };
            for (auto& retired : retiredMrtProxies_) noteProxy(retired.second.get());
            noteProxy(mrtProxy_.get());
            std::sort(lastMrtReadbackMatchesEXT_.begin(), lastMrtReadbackMatchesEXT_.end());
        }

        std::vector<uint64_t> flushSegments;
        auto alreadyNeeded = [&flushSegments](uint64_t id) {
            return std::find(flushSegments.begin(), flushSegments.end(), id) != flushSegments.end();
        };
        for (const auto& s : pendingSegments)
            for (const void* g : groups)
                if (s.group == g && !alreadyNeeded(s.id)) { flushSegments.push_back(s.id); break; }
        if (flushSegments.empty()) return;

        // Transitive closure over the producers each needed cycle samples.
        for (std::size_t i = 0; i < flushSegments.size(); ++i) {
            const uint64_t consumer = flushSegments[i];
            for (const auto& sampled : segmentSampledGroups_) {
                if (sampled.first != consumer) continue;
                for (const auto& s : pendingSegments)
                    if (s.group == sampled.second && s.id < consumer && !alreadyNeeded(s.id))
                        flushSegments.push_back(s.id);
            }
        }
        std::sort(flushSegments.begin(), flushSegments.end());

        DeviceWaitIdleEXT();

        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = commandPool_;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &ai, &cb) != VK_SUCCESS) return;

        // One command buffer, one submit: every pass this flush owes, in ascending segment order.
        // Producer and consumer land in the same submission and are ordered by pass order plus each
        // render pass's own COLOR_ATTACHMENT_WRITE -> SHADER_READ exit dependency, so no extra
        // barrier, fence, queue wait or submit is introduced. This also REMOVES the pre-fix
        // submit-per-MRT-proxy loop, whose passes could only be ordered by the order the proxies
        // happened to be discovered.
        RecordCommandBuffer(cb, 0, RecordMode::RenderTargetsOnly, rt, &flushSegments);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cb);
    }

    void VulkanRenderer::SetStringMarkerEXT(const char* marker)
    {
        if (!marker || !marker[0]) return;
        Pending3DDraw m;
        m.isMarker   = true;
        m.markerLabel = marker;
        m.rt          = currentRT_;
        PushPending3DDraw(std::move(m));
    }

    void VulkanRenderer::DrawColoredPrimitives(
        const IVertexBufferRenderer& vb,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& vulkanVB = static_cast<const VulkanVertexBufferRenderer&>(vb);
        uint32_t drawCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        std::size_t stride = vulkanVB.GetStride() > 0 ? vulkanVB.GetStride() : 16;
        RequireLegacyColoredStrideEXT(stride, "DrawColoredPrimitives");

        Pending3DDraw d{};
        // VULKAN-097: XNA's D3D9 pixel-centre convention, post-multiplied in row-vector order.
        const Matrix wvp = world * view * projection * XnaPixelCenterCorrectionEXT(primitive);
        wvp.ToColumnMajor(d.pushConst);
        // This path carries no BasicEffect diffuse/VertexColorEnabled (no GpuDrawParams at
        // all); preserve the historical behavior of outputting the raw vertex colors
        // unmodified (diffuseColor=white, vertexColorEnabled=true — Task 364).
        d.pushConst[16] = 1.0f; d.pushConst[17] = 1.0f; d.pushConst[18] = 1.0f; d.pushConst[19] = 1.0f;
        d.pushConst[31] = 1.0f;

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(), drawCount * stride);

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = drawCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend      = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType  = VK_INDEX_TYPE_UINT16;  // non-indexed, not used
        d.rt         = currentRT_;
        PushPending3DDraw(std::move(d));
    }

    void VulkanRenderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& vulkanVB = static_cast<const VulkanVertexBufferRenderer&>(vb);
        const auto& vulkanIB = static_cast<const VulkanIndexBufferRenderer&>(ib);
        uint32_t indexCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        std::size_t stride  = vulkanVB.GetStride() > 0 ? vulkanVB.GetStride() : 16;
        RequireLegacyColoredStrideEXT(stride, "DrawIndexedColoredPrimitives");
        int vertexCount     = vulkanVB.GetVertexCount();

        Pending3DDraw d{};
        // VULKAN-097: XNA's D3D9 pixel-centre convention, post-multiplied in row-vector order.
        const Matrix wvp = world * view * projection * XnaPixelCenterCorrectionEXT(primitive);
        wvp.ToColumnMajor(d.pushConst);
        // See DrawColoredPrimitives above: preserve the historical raw-vertex-color output
        // for this no-GpuDrawParams legacy path (Task 364).
        d.pushConst[16] = 1.0f; d.pushConst[17] = 1.0f; d.pushConst[18] = 1.0f; d.pushConst[19] = 1.0f;
        d.pushConst[31] = 1.0f;

        d.vbData.resize(static_cast<std::size_t>(vertexCount) * stride);
        std::memcpy(d.vbData.data(), vulkanVB.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * stride);

        d.ibData.resize(static_cast<std::size_t>(indexCount) * sizeof(uint16_t));
        std::memcpy(d.ibData.data(), vulkanIB.GetMappedPtr(),
                    static_cast<std::size_t>(indexCount) * sizeof(uint16_t));

        d.topology   = ToVkTopology(primitive);
        d.drawCount  = indexCount;
        d.depthTest  = depthTestEnabled_;
        d.depthWrite = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend      = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode   = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType  = vulkanIB.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt         = currentRT_;
        PushPending3DDraw(std::move(d));
    }

    // ---- Extended 3D draws (Tasks 53-55) ----

    void VulkanRenderer::DrawPrimitivesEx(
        const IVertexBufferRenderer& vb_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // REMED-GFX-DECL-GUARD: before anything is recorded, queued or created. This renderer
        // still picks its VkVertexInputAttributeDescription set from the stride, so a declaration
        // that set cannot represent is refused here rather than rendered from the wrong bytes.
        // VULKAN-146: the family the draw selects, and with it whether the declaration can be
        // bound EXACTLY, is decided before the guard runs. These are pure functions of the stride
        // and the draw params -- nothing here has a side effect, which is what makes hoisting them
        // above the guard safe. Everything that does have one still happens after it.
        const auto& vb = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        const std::size_t stride = vb.GetStride() > 0 ? vb.GetStride() : 20;

        const bool needsPbr        = params.pbr;
        const bool needsAlphaTest  = !needsPbr &&
                                     (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex    = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap     = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned    = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // stride==32 always uses the lit-textured shader (BasicEffect's VertexPositionNormalTexture
        // path, lit or not — the shader itself branches on lightingEnabled), unless another
        // effect (alpha test/dual tex/env map/skinned/pbr) takes priority for this stride.
        // VULKAN-146 / REMED-GFX-234: stride 32 alone does not mean a lit vertex. A declaration
        // that names no Normal cannot be one, whatever its stride, and the lit programs have no
        // colour input for it to fall back on. An absent declaration keeps the stride's answer.
        const bool declaresNormal =
            vb.GetDeclarationEXT().IsEmpty() ||
            DeclarationNamesUsageEXT(vb.GetDeclarationEXT(),
                                     Microsoft::Xna::Framework::Graphics::VertexElementUsage::Normal);
        const bool needsLitTextured = (stride == 32) && declaresNormal && !needsAlphaTest
                                     && !needsDualTex && !needsEnvMap && !needsSkinned && !needsPbr;
        const bool usesFogTex3D = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                                && !needsPbr && !needsLitTextured;

        const BasicProgramShapeEXT basicShape = SelectBasicProgramShapeEXT(
            usesFogTex3D, needsLitTextured, stride, vb.GetDeclarationEXT());
        VulkanVertexInputLayoutEXT declaredLayout;
        {
            std::size_t inputCount = 0;
            const auto* inputs = BasicShapeStockInputsEXT(basicShape, inputCount);
            // VULKAN-147: the stock effect families, which the BasicEffect shape does not cover.
            if (inputs == nullptr)
                inputs = EffectFamilyStockInputsEXT(needsAlphaTest, needsEnvMap,
                                                    needsSkinned && !needsPbr, stride, inputCount);
            // VULKAN-148: and the two PBR families.
            if (inputs == nullptr && needsPbr)
                inputs = PbrFamilyStockInputsEXT(needsSkinned, stride, inputCount);
            if (inputs != nullptr)
                declaredLayout = BuildVulkanVertexInputLayoutEXT(
                    vb.GetDeclarationEXT(), inputs, inputCount,
                    // VULKAN-151: lets a `Byte4`-spelled BLENDINDICES bind to the skinned shaders'
                    // `vec4` input, on a device that can carry it.
                    uscaledVertexFormatSupported_);
            // VULKAN-150: DualTextureEffect has its own builder rather than a table, because its
            // second coordinate set is aliased onto the first when the record declares only one.
            if (needsDualTex)
                declaredLayout = BuildDualTextureVertexLayoutEXT(vb.GetDeclarationEXT(),
                                                                stride == 24);
        }
        // VULKAN-146: the guard is for a route that infers its input from the stride. A family
        // this row converted, given a declaration that supplies every one of its inputs, no longer
        // does -- the pipeline is keyed and built from the declaration's own offsets. Anything
        // else still goes through the guard unchanged, including a converted family whose
        // declaration left an input unsupplied.
        if (!declaredLayout.IsComplete())
            RequireFaithfulDeclarationEXT(vb_in, "ordinary-nonindexed", /*positionOnlyFallback=*/false,
                                          params.compiledEffectRuntime != nullptr);
        // REMED-GFX-151: record which render targets this draw SAMPLES, so a mid-frame readback
        // flush replays their producing cycles before this one. See NoteSampledSourcesEXT.
        NoteSampledSourcesEXT(params);
        EnsureDefaultWhiteTexture();
        const uint32_t drawCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));

        Pending3DDraw d{};
        // VULKAN-097: XNA's D3D9 pixel-centre convention, post-multiplied in row-vector order.
        const Matrix wvp = world * view * projection * XnaPixelCenterCorrectionEXT(primitive);
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext, lit-textured, skinned, and pbr (same PC)
        }

        d.vbData.resize(drawCount * stride);
        std::memcpy(d.vbData.data(),
                    static_cast<const uint8_t*>(vb.GetMappedPtr()) + params.vertexStart * stride,
                    drawCount * stride);

        d.topology       = ToVkTopology(primitive);
        d.drawCount      = drawCount;
        d.depthTest      = depthTestEnabled_;
        d.depthWrite     = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend          = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode       = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType      = VK_INDEX_TYPE_UINT16;
        d.rt             = currentRT_;
        d.stride         = stride;
        // Task 899: all BasicEffect draws that reach neither alpha-test/dual-tex/env-map/skinned/
        // pbr nor lit-textured (stride 16/20/24) now route through the fog-capable colored3d/
        // textured3d/colored_textured3d bundle instead of the old zero-fog pipelines.
        d.useFogTex3D    = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                         && !needsPbr && !needsLitTextured;
        d.useDualTexture = needsDualTex;
        // plans/plan_cnj.md CNB-58/CNB-91 Vulkan port: pbr+skinned (SkinnedPbrEffect, stride 68) and
        // pbr-only (PbrEffect, stride 48) are two distinct pipelines/descriptor bundles; plain
        // skinned (stride 52/56) only when pbr is NOT also set (mirrors
        // EasyGLRenderer::SelectProgram()'s own pbr&&skinned / pbr / skinned priority order).
        d.usePbrSkinned  = needsPbr && needsSkinned;
        d.usePbr         = needsPbr && !needsSkinned;
        // VULKAN-346: refuse here, not at Present. Nothing is queued that the replay cannot build.
        // VULKAN-148: the stride list is what a buffer with NO declaration is judged by. A
        // declaration that supplies every one of the selected shader's inputs says where they are,
        // whatever the stride, so it is not held to the list.
        if ((d.usePbr || d.usePbrSkinned) && !declaredLayout.IsComplete())
            RequirePbrStrideEXT(stride, d.usePbrSkinned);
        d.useSkinned     = needsSkinned && !needsPbr;
        // VULKAN-156: same rule as the PBR pair's -- the stride list judges a buffer whose
        // declaration did not supply the shader's inputs. One that did says where they are.
        if (d.useSkinned && !declaredLayout.IsComplete()) RequireSkinnedStrideEXT(stride);
        // VULKAN-151: the stride-derived bone-index attribute is VK_FORMAT_R8G8B8A8_USCALED, which
        // is not a mandatory vertex-buffer format. A device without it cannot take the baked path
        // at all, so say so here rather than let vkCreateGraphicsPipelines fail at Present. A
        // COMPLETE layout needs no check: the builder only emits _USCALED where the device
        // supports it, and a `Vector4`-spelled index never needs it.
        if ((d.useSkinned || d.usePbrSkinned) && !declaredLayout.IsComplete())
            RequireBoneIndexFormatEXT();
        d.useLitTextured = needsLitTextured;
        // VULKAN-146: taken at DRAW time, above, because the record is replayed at Present(), by
        // which point the buffer may carry a different declaration entirely.
        d.vertexLayout = declaredLayout;
        d.basicShape   = basicShape;
        // Task 1103: real XNA default is PreferPerPixelLighting=false (per-vertex/
        // Gouraud lighting) -- only meaningful while lighting is actually enabled.
        d.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        if (needsPbr && needsSkinned) {
            EnsurePbrSkinnedResources();
            const auto* vsBase = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vsNorm = dynamic_cast<const IVulkanSamplable*>(params.pbrNormalMap);
            const auto* vsMR   = dynamic_cast<const IVulkanSamplable*>(params.pbrMetallicRoughnessMap);
            const auto* vsEmis = dynamic_cast<const IVulkanSamplable*>(params.pbrEmissiveMap);
            const auto* vsOcc  = dynamic_cast<const IVulkanSamplable*>(params.pbrOcclusionMap);
            const auto* vsSpec = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularMap);
            const auto* vsSpecColor = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularColorMap);
            EnsureDefaultFlatNormalTexture();
            VkImageView vBase = vsBase ? vsBase->GetVkImageView() : defaultWhiteView_;
            VkImageView vNorm = vsNorm ? vsNorm->GetVkImageView() : defaultFlatNormalView_;
            VkImageView vMR   = vsMR   ? vsMR->GetVkImageView()   : defaultWhiteView_;
            VkImageView vEmis = vsEmis ? vsEmis->GetVkImageView() : defaultWhiteView_;
            VkImageView vOcc  = vsOcc  ? vsOcc->GetVkImageView()  : defaultWhiteView_;
            VkImageView vSpec = vsSpec ? vsSpec->GetVkImageView() : defaultWhiteView_;
            VkImageView vSpecColor = vsSpecColor ? vsSpecColor->GetVkImageView() : defaultWhiteView_;
            d.pbrDescSet = GetOrCreatePbrSkinnedDescSet(
                currentFrame_, vBase, vNorm, vMR, vEmis, vOcc, vSpec, vSpecColor,
                                                        PbrSlotSamplersRawEXT().s);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            FillPbrUboData(d.pbrUboData, params, static_cast<float>(params.weightsPerVertex));
        } else if (needsPbr) {
            EnsurePbrResources();
            const auto* vsBase = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vsNorm = dynamic_cast<const IVulkanSamplable*>(params.pbrNormalMap);
            const auto* vsMR   = dynamic_cast<const IVulkanSamplable*>(params.pbrMetallicRoughnessMap);
            const auto* vsEmis = dynamic_cast<const IVulkanSamplable*>(params.pbrEmissiveMap);
            const auto* vsOcc  = dynamic_cast<const IVulkanSamplable*>(params.pbrOcclusionMap);
            const auto* vsSpec = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularMap);
            const auto* vsSpecColor = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularColorMap);
            EnsureDefaultFlatNormalTexture();
            VkImageView vBase = vsBase ? vsBase->GetVkImageView() : defaultWhiteView_;
            VkImageView vNorm = vsNorm ? vsNorm->GetVkImageView() : defaultFlatNormalView_;
            VkImageView vMR   = vsMR   ? vsMR->GetVkImageView()   : defaultWhiteView_;
            VkImageView vEmis = vsEmis ? vsEmis->GetVkImageView() : defaultWhiteView_;
            VkImageView vOcc  = vsOcc  ? vsOcc->GetVkImageView()  : defaultWhiteView_;
            VkImageView vSpec = vsSpec ? vsSpec->GetVkImageView() : defaultWhiteView_;
            VkImageView vSpecColor = vsSpecColor ? vsSpecColor->GetVkImageView() : defaultWhiteView_;
            d.pbrDescSet = GetOrCreatePbrDescSet(
                currentFrame_, vBase, vNorm, vMR, vEmis, vOcc, vSpec, vSpecColor,
                                                 PbrSlotSamplersRawEXT().s);
            FillPbrUboData(d.pbrUboData, params, 0.0f);
        } else if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d, slotSamplers_[0]);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            d.skinnedFogUboData[0] = params.fogColor[0]; d.skinnedFogUboData[1] = params.fogColor[1];
            d.skinnedFogUboData[2] = params.fogColor[2]; d.skinnedFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.skinnedFogUboData[4] = params.fogVector[0]; d.skinnedFogUboData[5] = params.fogVector[1];
            d.skinnedFogUboData[6] = params.fogVector[2]; d.skinnedFogUboData[7] = params.fogVector[3];
            // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            d.skinnedFogUboData[8]  = params.light1Dir[0]; d.skinnedFogUboData[9]  = params.light1Dir[1];
            d.skinnedFogUboData[10] = params.light1Dir[2]; d.skinnedFogUboData[11] = 0.f;
            d.skinnedFogUboData[12] = params.light1Diffuse[0]; d.skinnedFogUboData[13] = params.light1Diffuse[1];
            d.skinnedFogUboData[14] = params.light1Diffuse[2]; d.skinnedFogUboData[15] = 0.f;
            d.skinnedFogUboData[16] = params.light2Dir[0]; d.skinnedFogUboData[17] = params.light2Dir[1];
            d.skinnedFogUboData[18] = params.light2Dir[2]; d.skinnedFogUboData[19] = 0.f;
            d.skinnedFogUboData[20] = params.light2Diffuse[0]; d.skinnedFogUboData[21] = params.light2Diffuse[1];
            d.skinnedFogUboData[22] = params.light2Diffuse[2]; d.skinnedFogUboData[23] = 0.f;
            // Task 894: World matrix (for world-space position -> eye vector), EyePosition,
            // per-light SpecularColor, and material SpecularColor/SpecularPower.
            for (int wi = 0; wi < 16; ++wi) d.skinnedFogUboData[24 + wi] = params.worldColMajor[wi];
            d.skinnedFogUboData[40] = params.eyePositionWorld[0];
            d.skinnedFogUboData[41] = params.eyePositionWorld[1];
            d.skinnedFogUboData[42] = params.eyePositionWorld[2];
            d.skinnedFogUboData[43] = static_cast<float>(params.weightsPerVertex); // Task 895
            d.skinnedFogUboData[44] = params.specularColor[0]; d.skinnedFogUboData[45] = params.specularColor[1];
            d.skinnedFogUboData[46] = params.specularColor[2]; d.skinnedFogUboData[47] = params.specularPower;
            d.skinnedFogUboData[48] = params.light0Specular[0]; d.skinnedFogUboData[49] = params.light0Specular[1];
            d.skinnedFogUboData[50] = params.light0Specular[2]; d.skinnedFogUboData[51] = 0.f;
            d.skinnedFogUboData[52] = params.light1Specular[0]; d.skinnedFogUboData[53] = params.light1Specular[1];
            d.skinnedFogUboData[54] = params.light1Specular[2]; d.skinnedFogUboData[55] = 0.f;
            d.skinnedFogUboData[56] = params.light2Specular[0]; d.skinnedFogUboData[57] = params.light2Specular[1];
            d.skinnedFogUboData[58] = params.light2Specular[2]; d.skinnedFogUboData[59] = 0.f;

            // REMED-GFX-008: emissiveColor vec4 — the CPU pre-folds (emissive + ambient*diffuse)*alpha
            // into params.emissiveColor. The skinned shaders add it AFTER the lightSum*diffuse multiply
            // (litRGB = lightSum*diffuse + emissiveColor), so both AmbientLightColor and EmissiveColor
            // reach skinned draws (previously the shaders read the always-zero ambientColor and never
            // added emissive, silently dropping both).
            d.skinnedFogUboData[60] = params.emissiveColor[0]; d.skinnedFogUboData[61] = params.emissiveColor[1];
            d.skinnedFogUboData[62] = params.emissiveColor[2]; d.skinnedFogUboData[63] = 0.f;
        } else if (needsEnvMap) {
            EnsureEnvMapResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vtc = dynamic_cast<const IVulkanCubeSamplable*>(params.envMap);
            VkImageView v2d  = vs0 ? vs0->GetVkImageView()       : defaultWhiteView_;
            VkImageView vcub = vtc ? vtc->GetVkCubeImageView()    : defaultWhiteCubeView_;
            d.envMapDescSet  = GetOrCreateEnvMapDescSet(currentFrame_, v2d, vcub,
                                                        slotSamplers_[0], slotSamplers_[1]);
            // Pack UBO data: eyePos, diffuse, emissive+envMapAmount, light0Dir,
            // light0Diff+fresnelEnabled, envMapSpecular+fresnelFactor
            d.envMapUboData[0]  = params.eyePositionWorld[0];
            d.envMapUboData[1]  = params.eyePositionWorld[1];
            d.envMapUboData[2]  = params.eyePositionWorld[2];
            d.envMapUboData[3]  = 0.f;
            d.envMapUboData[4]  = params.diffuseColor[0]; d.envMapUboData[5]  = params.diffuseColor[1];
            d.envMapUboData[6]  = params.diffuseColor[2]; d.envMapUboData[7]  = params.diffuseColor[3];
            d.envMapUboData[8]  = params.emissiveColor[0]; d.envMapUboData[9]  = params.emissiveColor[1];
            d.envMapUboData[10] = params.emissiveColor[2]; d.envMapUboData[11] = params.envMapAmount;
            d.envMapUboData[12] = params.light0Dir[0]; d.envMapUboData[13] = params.light0Dir[1];
            d.envMapUboData[14] = params.light0Dir[2]; d.envMapUboData[15] = 0.f;
            d.envMapUboData[16] = params.light0Diffuse[0]; d.envMapUboData[17] = params.light0Diffuse[1];
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = params.fresnelEnabled ? 1.f : 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = params.fresnelFactor;
            // Task 899's noted cheap leftover: fog packed into EnvMapParams' spare tail bytes.
            d.envMapUboData[24] = params.fogColor[0]; d.envMapUboData[25] = params.fogColor[1];
            d.envMapUboData[26] = params.fogColor[2]; d.envMapUboData[27] = params.fogEnabled ? 1.f : 0.f;
            d.envMapUboData[28] = params.fogVector[0]; d.envMapUboData[29] = params.fogVector[1];
            d.envMapUboData[30] = params.fogVector[2]; d.envMapUboData[31] = params.fogVector[3];
            // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            d.envMapUboData[32] = params.light1Dir[0]; d.envMapUboData[33] = params.light1Dir[1];
            d.envMapUboData[34] = params.light1Dir[2]; d.envMapUboData[35] = 0.f;
            d.envMapUboData[36] = params.light1Diffuse[0]; d.envMapUboData[37] = params.light1Diffuse[1];
            d.envMapUboData[38] = params.light1Diffuse[2]; d.envMapUboData[39] = 0.f;
            d.envMapUboData[40] = params.light2Dir[0]; d.envMapUboData[41] = params.light2Dir[1];
            d.envMapUboData[42] = params.light2Dir[2]; d.envMapUboData[43] = 0.f;
            d.envMapUboData[44] = params.light2Diffuse[0]; d.envMapUboData[45] = params.light2Diffuse[1];
            d.envMapUboData[46] = params.light2Diffuse[2]; d.envMapUboData[47] = 0.f;
        } else if (needsDualTex) {
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(currentFrame_, v0, v1, slotSamplers_[0], slotSamplers_[1]);
            d.dualTexFogUboData[0] = params.fogColor[0]; d.dualTexFogUboData[1] = params.fogColor[1];
            d.dualTexFogUboData[2] = params.fogColor[2]; d.dualTexFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.dualTexFogUboData[4] = params.fogVector[0]; d.dualTexFogUboData[5] = params.fogVector[1];
            d.dualTexFogUboData[6] = params.fogVector[2]; d.dualTexFogUboData[7] = params.fogVector[3];
        } else if (needsLitTextured) {
            EnsureLitTexturedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.litTexturedDescSet = GetOrCreateLitTexturedDescSet(currentFrame_, view, slotSamplers_[0]);
            // Pack UBO data: light1Dir+pad, light1Diffuse+pad, light2Dir+pad, light2Diffuse+pad,
            // emissiveColor+pad.
            d.litUboData[0]  = params.light1Dir[0];     d.litUboData[1]  = params.light1Dir[1];
            d.litUboData[2]  = params.light1Dir[2];     d.litUboData[3]  = 0.f;
            d.litUboData[4]  = params.light1Diffuse[0]; d.litUboData[5]  = params.light1Diffuse[1];
            d.litUboData[6]  = params.light1Diffuse[2]; d.litUboData[7]  = 0.f;
            d.litUboData[8]  = params.light2Dir[0];     d.litUboData[9]  = params.light2Dir[1];
            d.litUboData[10] = params.light2Dir[2];     d.litUboData[11] = 0.f;
            d.litUboData[12] = params.light2Diffuse[0]; d.litUboData[13] = params.light2Diffuse[1];
            d.litUboData[14] = params.light2Diffuse[2]; d.litUboData[15] = 0.f;
            d.litUboData[16] = params.emissiveColor[0]; d.litUboData[17] = params.emissiveColor[1];
            d.litUboData[18] = params.emissiveColor[2]; d.litUboData[19] = 0.f;
            // World matrix (Task 898: needed by the vertex shader for a correct world-space
            // position/normal, since the 128-byte PC has no spare room for it).
            for (int wi = 0; wi < 16; ++wi) d.litUboData[20 + wi] = params.worldColMajor[wi];
            d.litUboData[36] = params.eyePositionWorld[0]; d.litUboData[37] = params.eyePositionWorld[1];
            d.litUboData[38] = params.eyePositionWorld[2]; d.litUboData[39] = 0.f;
            d.litUboData[40] = params.light0Specular[0]; d.litUboData[41] = params.light0Specular[1];
            d.litUboData[42] = params.light0Specular[2]; d.litUboData[43] = 0.f;
            d.litUboData[44] = params.light1Specular[0]; d.litUboData[45] = params.light1Specular[1];
            d.litUboData[46] = params.light1Specular[2]; d.litUboData[47] = 0.f;
            d.litUboData[48] = params.light2Specular[0]; d.litUboData[49] = params.light2Specular[1];
            d.litUboData[50] = params.light2Specular[2]; d.litUboData[51] = 0.f;
            d.litUboData[52] = params.specularColor[0]; d.litUboData[53] = params.specularColor[1];
            d.litUboData[54] = params.specularColor[2]; d.litUboData[55] = params.specularPower;
            d.litUboData[56] = params.fogColor[0]; d.litUboData[57] = params.fogColor[1];
            d.litUboData[58] = params.fogColor[2]; d.litUboData[59] = params.fogEnabled ? 1.f : 0.f;
            d.litUboData[60] = params.fogVector[0]; d.litUboData[61] = params.fogVector[1];
            d.litUboData[62] = params.fogVector[2]; d.litUboData[63] = params.fogVector[3];
        } else {
            // Shared fallback fill: reached both by alpha-test draws (whose pipeline also uses
            // the plain single-sampler descriptorSetLayout_/d.descSet) and, when !needsAlphaTest,
            // by the colored3d/textured3d/colored_textured3d fog-capable bundle (Task 899).
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
            if (d.useFogTex3D) {
                EnsureFogTex3DResources();
                d.fogTex3DDescSet = GetOrCreateFogTex3DDescSet(currentFrame_, view, slotSamplers_[0]);
                d.fogTex3DUboData[0] = params.fogColor[0]; d.fogTex3DUboData[1] = params.fogColor[1];
                d.fogTex3DUboData[2] = params.fogColor[2]; d.fogTex3DUboData[3] = params.fogEnabled ? 1.f : 0.f;
                d.fogTex3DUboData[4] = params.fogVector[0]; d.fogTex3DUboData[5] = params.fogVector[1];
                d.fogTex3DUboData[6] = params.fogVector[2]; d.fogTex3DUboData[7] = params.fogVector[3];
            }
        }
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-065: a compiled Effect owns the whole program, so it replaces the stock
        // stride-dispatched selection above rather than layering on it. Everything the deferred
        // replay needs is captured now, while the pass is still the applied one.
        if (params.compiledEffectRuntime != nullptr)
            PrepareCompiledEffectDrawEXT(d, vb_in, params);
#else
        if (params.compiledEffectRuntime != nullptr)
        {
            throw System::NotSupportedException(
                "CNA Vulkan: this build has no compiled-effect support "
                "(CNA_VULKAN_COMPILED_EFFECTS is off); drawing with a compiled Effect would "
                "silently use a stock shader instead.");
        }
#endif
        PushPending3DDraw(std::move(d));
    }

    void VulkanRenderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // REMED-GFX-DECL-GUARD: see DrawPrimitivesEx above.
        // VULKAN-146: the family the draw selects, and with it whether the declaration can be
        // bound EXACTLY, is decided before the guard runs. These are pure functions of the stride
        // and the draw params -- nothing here has a side effect, which is what makes hoisting them
        // above the guard safe. Everything that does have one still happens after it.
        const auto& vb = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const VulkanIndexBufferRenderer&>(ib_in);
        const std::size_t stride  = vb.GetStride() > 0 ? vb.GetStride() : 20;

        const bool needsPbr       = params.pbr;
        const bool needsAlphaTest = !needsPbr &&
                                    (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex   = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap    = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned   = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // VULKAN-146 / REMED-GFX-234: stride 32 alone does not mean a lit vertex. A declaration
        // that names no Normal cannot be one, whatever its stride, and the lit programs have no
        // colour input for it to fall back on. An absent declaration keeps the stride's answer.
        const bool declaresNormal =
            vb.GetDeclarationEXT().IsEmpty() ||
            DeclarationNamesUsageEXT(vb.GetDeclarationEXT(),
                                     Microsoft::Xna::Framework::Graphics::VertexElementUsage::Normal);
        const bool needsLitTextured = (stride == 32) && declaresNormal && !needsAlphaTest
                                     && !needsDualTex && !needsEnvMap && !needsSkinned && !needsPbr;
        const bool usesFogTex3D = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                                && !needsPbr && !needsLitTextured;

        const BasicProgramShapeEXT basicShape = SelectBasicProgramShapeEXT(
            usesFogTex3D, needsLitTextured, stride, vb.GetDeclarationEXT());
        VulkanVertexInputLayoutEXT declaredLayout;
        {
            std::size_t inputCount = 0;
            const auto* inputs = BasicShapeStockInputsEXT(basicShape, inputCount);
            // VULKAN-147: the stock effect families, which the BasicEffect shape does not cover.
            if (inputs == nullptr)
                inputs = EffectFamilyStockInputsEXT(needsAlphaTest, needsEnvMap,
                                                    needsSkinned && !needsPbr, stride, inputCount);
            // VULKAN-148: and the two PBR families.
            if (inputs == nullptr && needsPbr)
                inputs = PbrFamilyStockInputsEXT(needsSkinned, stride, inputCount);
            if (inputs != nullptr)
                declaredLayout = BuildVulkanVertexInputLayoutEXT(
                    vb.GetDeclarationEXT(), inputs, inputCount,
                    // VULKAN-151: lets a `Byte4`-spelled BLENDINDICES bind to the skinned shaders'
                    // `vec4` input, on a device that can carry it.
                    uscaledVertexFormatSupported_);
            // VULKAN-150: DualTextureEffect has its own builder rather than a table, because its
            // second coordinate set is aliased onto the first when the record declares only one.
            if (needsDualTex)
                declaredLayout = BuildDualTextureVertexLayoutEXT(vb.GetDeclarationEXT(),
                                                                stride == 24);
        }
        // VULKAN-146: the guard is for a route that infers its input from the stride. A family
        // this row converted, given a declaration that supplies every one of its inputs, no longer
        // does -- the pipeline is keyed and built from the declaration's own offsets. Anything
        // else still goes through the guard unchanged, including a converted family whose
        // declaration left an input unsupplied.
        if (!declaredLayout.IsComplete())
            RequireFaithfulDeclarationEXT(vb_in, "ordinary-indexed", /*positionOnlyFallback=*/false,
                                          params.compiledEffectRuntime != nullptr);
        // REMED-GFX-151: record which render targets this draw SAMPLES, so a mid-frame readback
        // flush replays their producing cycles before this one. See NoteSampledSourcesEXT.
        NoteSampledSourcesEXT(params);
        EnsureDefaultWhiteTexture();
        const uint32_t indexCount = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        const int vertexCount     = vb.GetVertexCount();

        Pending3DDraw d{};
        // VULKAN-097: XNA's D3D9 pixel-centre convention, post-multiplied in row-vector order.
        const Matrix wvp = world * view * projection * XnaPixelCenterCorrectionEXT(primitive);
        if (needsAlphaTest) {
            FillAlphaTestPushConst(d.pushConst, wvp, params);
            d.useAlphaTest = true;
        } else if (needsEnvMap) {
            FillEnvMapPushConst(d.envMapPC, wvp, world);
            d.useEnvMap = true;
        } else {
            FillExtPushConst(d.pushConst, wvp, params);  // covers ext, lit-textured, skinned, and pbr (same PC)
        }

        d.vbData.resize(static_cast<std::size_t>(vertexCount) * stride);
        std::memcpy(d.vbData.data(), vb.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * stride);
        const int indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        d.ibData.resize(static_cast<std::size_t>(indexCount) * indexSize);
        std::memcpy(d.ibData.data(),
                    static_cast<const uint8_t*>(ib.GetMappedPtr()) + params.startIndex * indexSize,
                    static_cast<std::size_t>(indexCount) * indexSize);
        d.baseVertex = static_cast<int32_t>(params.baseVertex);

        d.topology      = ToVkTopology(primitive);
        d.drawCount     = indexCount;
        d.depthTest     = depthTestEnabled_;
        d.depthWrite    = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend         = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode      = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType     = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt            = currentRT_;
        d.stride        = stride;
        // Task 899: see DrawPrimitivesEx's identical comment above.
        d.useFogTex3D   = !needsAlphaTest && !needsDualTex && !needsEnvMap && !needsSkinned
                        && !needsPbr && !needsLitTextured;
        d.useDualTexture = needsDualTex;
        // plans/plan_cnj.md CNB-58/CNB-91 Vulkan port: see DrawPrimitivesEx's identical comment above.
        d.usePbrSkinned  = needsPbr && needsSkinned;
        d.usePbr         = needsPbr && !needsSkinned;
        // VULKAN-346: refuse here, not at Present. Nothing is queued that the replay cannot build.
        // VULKAN-148: the stride list is what a buffer with NO declaration is judged by. A
        // declaration that supplies every one of the selected shader's inputs says where they are,
        // whatever the stride, so it is not held to the list.
        if ((d.usePbr || d.usePbrSkinned) && !declaredLayout.IsComplete())
            RequirePbrStrideEXT(stride, d.usePbrSkinned);
        d.useSkinned     = needsSkinned && !needsPbr;
        // VULKAN-156: same rule as the PBR pair's -- the stride list judges a buffer whose
        // declaration did not supply the shader's inputs. One that did says where they are.
        if (d.useSkinned && !declaredLayout.IsComplete()) RequireSkinnedStrideEXT(stride);
        // VULKAN-151: the stride-derived bone-index attribute is VK_FORMAT_R8G8B8A8_USCALED, which
        // is not a mandatory vertex-buffer format. A device without it cannot take the baked path
        // at all, so say so here rather than let vkCreateGraphicsPipelines fail at Present. A
        // COMPLETE layout needs no check: the builder only emits _USCALED where the device
        // supports it, and a `Vector4`-spelled index never needs it.
        if ((d.useSkinned || d.usePbrSkinned) && !declaredLayout.IsComplete())
            RequireBoneIndexFormatEXT();
        d.useLitTextured = needsLitTextured;
        // VULKAN-146: taken at DRAW time, above, because the record is replayed at Present(), by
        // which point the buffer may carry a different declaration entirely.
        d.vertexLayout = declaredLayout;
        d.basicShape   = basicShape;
        // Task 1103: real XNA default is PreferPerPixelLighting=false (per-vertex/
        // Gouraud lighting) -- only meaningful while lighting is actually enabled.
        d.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        if (needsPbr && needsSkinned) {
            EnsurePbrSkinnedResources();
            const auto* vsBase = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vsNorm = dynamic_cast<const IVulkanSamplable*>(params.pbrNormalMap);
            const auto* vsMR   = dynamic_cast<const IVulkanSamplable*>(params.pbrMetallicRoughnessMap);
            const auto* vsEmis = dynamic_cast<const IVulkanSamplable*>(params.pbrEmissiveMap);
            const auto* vsOcc  = dynamic_cast<const IVulkanSamplable*>(params.pbrOcclusionMap);
            const auto* vsSpec = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularMap);
            const auto* vsSpecColor = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularColorMap);
            EnsureDefaultFlatNormalTexture();
            VkImageView vBase = vsBase ? vsBase->GetVkImageView() : defaultWhiteView_;
            VkImageView vNorm = vsNorm ? vsNorm->GetVkImageView() : defaultFlatNormalView_;
            VkImageView vMR   = vsMR   ? vsMR->GetVkImageView()   : defaultWhiteView_;
            VkImageView vEmis = vsEmis ? vsEmis->GetVkImageView() : defaultWhiteView_;
            VkImageView vOcc  = vsOcc  ? vsOcc->GetVkImageView()  : defaultWhiteView_;
            VkImageView vSpec = vsSpec ? vsSpec->GetVkImageView() : defaultWhiteView_;
            VkImageView vSpecColor = vsSpecColor ? vsSpecColor->GetVkImageView() : defaultWhiteView_;
            d.pbrDescSet = GetOrCreatePbrSkinnedDescSet(
                currentFrame_, vBase, vNorm, vMR, vEmis, vOcc, vSpec, vSpecColor,
                                                        PbrSlotSamplersRawEXT().s);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            FillPbrUboData(d.pbrUboData, params, static_cast<float>(params.weightsPerVertex));
        } else if (needsPbr) {
            EnsurePbrResources();
            const auto* vsBase = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vsNorm = dynamic_cast<const IVulkanSamplable*>(params.pbrNormalMap);
            const auto* vsMR   = dynamic_cast<const IVulkanSamplable*>(params.pbrMetallicRoughnessMap);
            const auto* vsEmis = dynamic_cast<const IVulkanSamplable*>(params.pbrEmissiveMap);
            const auto* vsOcc  = dynamic_cast<const IVulkanSamplable*>(params.pbrOcclusionMap);
            const auto* vsSpec = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularMap);
            const auto* vsSpecColor = dynamic_cast<const IVulkanSamplable*>(params.pbrSpecularColorMap);
            EnsureDefaultFlatNormalTexture();
            VkImageView vBase = vsBase ? vsBase->GetVkImageView() : defaultWhiteView_;
            VkImageView vNorm = vsNorm ? vsNorm->GetVkImageView() : defaultFlatNormalView_;
            VkImageView vMR   = vsMR   ? vsMR->GetVkImageView()   : defaultWhiteView_;
            VkImageView vEmis = vsEmis ? vsEmis->GetVkImageView() : defaultWhiteView_;
            VkImageView vOcc  = vsOcc  ? vsOcc->GetVkImageView()  : defaultWhiteView_;
            VkImageView vSpec = vsSpec ? vsSpec->GetVkImageView() : defaultWhiteView_;
            VkImageView vSpecColor = vsSpecColor ? vsSpecColor->GetVkImageView() : defaultWhiteView_;
            d.pbrDescSet = GetOrCreatePbrDescSet(
                currentFrame_, vBase, vNorm, vMR, vEmis, vOcc, vSpec, vSpecColor,
                                                 PbrSlotSamplersRawEXT().s);
            FillPbrUboData(d.pbrUboData, params, 0.0f);
        } else if (needsSkinned) {
            EnsureSkinnedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView v2d = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.skinnedDescSet = GetOrCreateSkinnedDescSet(currentFrame_, v2d, slotSamplers_[0]);
            const int count = std::min(params.boneCount, 72);
            d.boneMatrices.assign(params.boneTransforms, params.boneTransforms + count * 16);
            d.skinnedFogUboData[0] = params.fogColor[0]; d.skinnedFogUboData[1] = params.fogColor[1];
            d.skinnedFogUboData[2] = params.fogColor[2]; d.skinnedFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.skinnedFogUboData[4] = params.fogVector[0]; d.skinnedFogUboData[5] = params.fogVector[1];
            d.skinnedFogUboData[6] = params.fogVector[2]; d.skinnedFogUboData[7] = params.fogVector[3];
            // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            d.skinnedFogUboData[8]  = params.light1Dir[0]; d.skinnedFogUboData[9]  = params.light1Dir[1];
            d.skinnedFogUboData[10] = params.light1Dir[2]; d.skinnedFogUboData[11] = 0.f;
            d.skinnedFogUboData[12] = params.light1Diffuse[0]; d.skinnedFogUboData[13] = params.light1Diffuse[1];
            d.skinnedFogUboData[14] = params.light1Diffuse[2]; d.skinnedFogUboData[15] = 0.f;
            d.skinnedFogUboData[16] = params.light2Dir[0]; d.skinnedFogUboData[17] = params.light2Dir[1];
            d.skinnedFogUboData[18] = params.light2Dir[2]; d.skinnedFogUboData[19] = 0.f;
            d.skinnedFogUboData[20] = params.light2Diffuse[0]; d.skinnedFogUboData[21] = params.light2Diffuse[1];
            d.skinnedFogUboData[22] = params.light2Diffuse[2]; d.skinnedFogUboData[23] = 0.f;
            // Task 894: World matrix (for world-space position -> eye vector), EyePosition,
            // per-light SpecularColor, and material SpecularColor/SpecularPower.
            for (int wi = 0; wi < 16; ++wi) d.skinnedFogUboData[24 + wi] = params.worldColMajor[wi];
            d.skinnedFogUboData[40] = params.eyePositionWorld[0];
            d.skinnedFogUboData[41] = params.eyePositionWorld[1];
            d.skinnedFogUboData[42] = params.eyePositionWorld[2];
            d.skinnedFogUboData[43] = static_cast<float>(params.weightsPerVertex); // Task 895
            d.skinnedFogUboData[44] = params.specularColor[0]; d.skinnedFogUboData[45] = params.specularColor[1];
            d.skinnedFogUboData[46] = params.specularColor[2]; d.skinnedFogUboData[47] = params.specularPower;
            d.skinnedFogUboData[48] = params.light0Specular[0]; d.skinnedFogUboData[49] = params.light0Specular[1];
            d.skinnedFogUboData[50] = params.light0Specular[2]; d.skinnedFogUboData[51] = 0.f;
            d.skinnedFogUboData[52] = params.light1Specular[0]; d.skinnedFogUboData[53] = params.light1Specular[1];
            d.skinnedFogUboData[54] = params.light1Specular[2]; d.skinnedFogUboData[55] = 0.f;
            d.skinnedFogUboData[56] = params.light2Specular[0]; d.skinnedFogUboData[57] = params.light2Specular[1];
            d.skinnedFogUboData[58] = params.light2Specular[2]; d.skinnedFogUboData[59] = 0.f;

            // REMED-GFX-008: emissiveColor vec4 — the CPU pre-folds (emissive + ambient*diffuse)*alpha
            // into params.emissiveColor. The skinned shaders add it AFTER the lightSum*diffuse multiply
            // (litRGB = lightSum*diffuse + emissiveColor), so both AmbientLightColor and EmissiveColor
            // reach skinned draws (previously the shaders read the always-zero ambientColor and never
            // added emissive, silently dropping both).
            d.skinnedFogUboData[60] = params.emissiveColor[0]; d.skinnedFogUboData[61] = params.emissiveColor[1];
            d.skinnedFogUboData[62] = params.emissiveColor[2]; d.skinnedFogUboData[63] = 0.f;
        } else if (needsEnvMap) {
            EnsureEnvMapResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vtc = dynamic_cast<const IVulkanCubeSamplable*>(params.envMap);
            VkImageView v2d  = vs0 ? vs0->GetVkImageView()       : defaultWhiteView_;
            VkImageView vcub = vtc ? vtc->GetVkCubeImageView()    : defaultWhiteCubeView_;
            d.envMapDescSet  = GetOrCreateEnvMapDescSet(currentFrame_, v2d, vcub,
                                                        slotSamplers_[0], slotSamplers_[1]);
            d.envMapUboData[0]  = params.eyePositionWorld[0];
            d.envMapUboData[1]  = params.eyePositionWorld[1];
            d.envMapUboData[2]  = params.eyePositionWorld[2];
            d.envMapUboData[3]  = 0.f;
            d.envMapUboData[4]  = params.diffuseColor[0]; d.envMapUboData[5]  = params.diffuseColor[1];
            d.envMapUboData[6]  = params.diffuseColor[2]; d.envMapUboData[7]  = params.diffuseColor[3];
            d.envMapUboData[8]  = params.emissiveColor[0]; d.envMapUboData[9]  = params.emissiveColor[1];
            d.envMapUboData[10] = params.emissiveColor[2]; d.envMapUboData[11] = params.envMapAmount;
            d.envMapUboData[12] = params.light0Dir[0]; d.envMapUboData[13] = params.light0Dir[1];
            d.envMapUboData[14] = params.light0Dir[2]; d.envMapUboData[15] = 0.f;
            d.envMapUboData[16] = params.light0Diffuse[0]; d.envMapUboData[17] = params.light0Diffuse[1];
            d.envMapUboData[18] = params.light0Diffuse[2]; d.envMapUboData[19] = params.fresnelEnabled ? 1.f : 0.f;
            d.envMapUboData[20] = params.envMapSpecular[0]; d.envMapUboData[21] = params.envMapSpecular[1];
            d.envMapUboData[22] = params.envMapSpecular[2]; d.envMapUboData[23] = params.fresnelFactor;
            // Task 899's noted cheap leftover: fog packed into EnvMapParams' spare tail bytes.
            d.envMapUboData[24] = params.fogColor[0]; d.envMapUboData[25] = params.fogColor[1];
            d.envMapUboData[26] = params.fogColor[2]; d.envMapUboData[27] = params.fogEnabled ? 1.f : 0.f;
            d.envMapUboData[28] = params.fogVector[0]; d.envMapUboData[29] = params.fogVector[1];
            d.envMapUboData[30] = params.fogVector[2]; d.envMapUboData[31] = params.fogVector[3];
            // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            d.envMapUboData[32] = params.light1Dir[0]; d.envMapUboData[33] = params.light1Dir[1];
            d.envMapUboData[34] = params.light1Dir[2]; d.envMapUboData[35] = 0.f;
            d.envMapUboData[36] = params.light1Diffuse[0]; d.envMapUboData[37] = params.light1Diffuse[1];
            d.envMapUboData[38] = params.light1Diffuse[2]; d.envMapUboData[39] = 0.f;
            d.envMapUboData[40] = params.light2Dir[0]; d.envMapUboData[41] = params.light2Dir[1];
            d.envMapUboData[42] = params.light2Dir[2]; d.envMapUboData[43] = 0.f;
            d.envMapUboData[44] = params.light2Diffuse[0]; d.envMapUboData[45] = params.light2Diffuse[1];
            d.envMapUboData[46] = params.light2Diffuse[2]; d.envMapUboData[47] = 0.f;
        } else if (needsDualTex) {
            EnsureDualTexResources();
            const auto* vs0 = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            const auto* vs1 = dynamic_cast<const IVulkanSamplable*>(params.texture1);
            VkImageView v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;
            VkImageView v1 = vs1 ? vs1->GetVkImageView() : defaultWhiteView_;
            d.dualTexDescSet = GetOrCreateDualTexDescSet(currentFrame_, v0, v1, slotSamplers_[0], slotSamplers_[1]);
            d.dualTexFogUboData[0] = params.fogColor[0]; d.dualTexFogUboData[1] = params.fogColor[1];
            d.dualTexFogUboData[2] = params.fogColor[2]; d.dualTexFogUboData[3] = params.fogEnabled ? 1.f : 0.f;
            d.dualTexFogUboData[4] = params.fogVector[0]; d.dualTexFogUboData[5] = params.fogVector[1];
            d.dualTexFogUboData[6] = params.fogVector[2]; d.dualTexFogUboData[7] = params.fogVector[3];
        } else if (needsLitTextured) {
            EnsureLitTexturedResources();
            const auto* vs = dynamic_cast<const IVulkanSamplable*>(params.texture0);
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.litTexturedDescSet = GetOrCreateLitTexturedDescSet(currentFrame_, view, slotSamplers_[0]);
            d.litUboData[0]  = params.light1Dir[0];     d.litUboData[1]  = params.light1Dir[1];
            d.litUboData[2]  = params.light1Dir[2];     d.litUboData[3]  = 0.f;
            d.litUboData[4]  = params.light1Diffuse[0]; d.litUboData[5]  = params.light1Diffuse[1];
            d.litUboData[6]  = params.light1Diffuse[2]; d.litUboData[7]  = 0.f;
            d.litUboData[8]  = params.light2Dir[0];     d.litUboData[9]  = params.light2Dir[1];
            d.litUboData[10] = params.light2Dir[2];     d.litUboData[11] = 0.f;
            d.litUboData[12] = params.light2Diffuse[0]; d.litUboData[13] = params.light2Diffuse[1];
            d.litUboData[14] = params.light2Diffuse[2]; d.litUboData[15] = 0.f;
            d.litUboData[16] = params.emissiveColor[0]; d.litUboData[17] = params.emissiveColor[1];
            d.litUboData[18] = params.emissiveColor[2]; d.litUboData[19] = 0.f;
            // World matrix (Task 898: needed by the vertex shader for a correct world-space
            // position/normal, since the 128-byte PC has no spare room for it).
            for (int wi = 0; wi < 16; ++wi) d.litUboData[20 + wi] = params.worldColMajor[wi];
            d.litUboData[36] = params.eyePositionWorld[0]; d.litUboData[37] = params.eyePositionWorld[1];
            d.litUboData[38] = params.eyePositionWorld[2]; d.litUboData[39] = 0.f;
            d.litUboData[40] = params.light0Specular[0]; d.litUboData[41] = params.light0Specular[1];
            d.litUboData[42] = params.light0Specular[2]; d.litUboData[43] = 0.f;
            d.litUboData[44] = params.light1Specular[0]; d.litUboData[45] = params.light1Specular[1];
            d.litUboData[46] = params.light1Specular[2]; d.litUboData[47] = 0.f;
            d.litUboData[48] = params.light2Specular[0]; d.litUboData[49] = params.light2Specular[1];
            d.litUboData[50] = params.light2Specular[2]; d.litUboData[51] = 0.f;
            d.litUboData[52] = params.specularColor[0]; d.litUboData[53] = params.specularColor[1];
            d.litUboData[54] = params.specularColor[2]; d.litUboData[55] = params.specularPower;
            d.litUboData[56] = params.fogColor[0]; d.litUboData[57] = params.fogColor[1];
            d.litUboData[58] = params.fogColor[2]; d.litUboData[59] = params.fogEnabled ? 1.f : 0.f;
            d.litUboData[60] = params.fogVector[0]; d.litUboData[61] = params.fogVector[1];
            d.litUboData[62] = params.fogVector[2]; d.litUboData[63] = params.fogVector[3];
        } else {
            // Shared fallback fill: reached both by alpha-test draws (whose pipeline also uses
            // the plain single-sampler descriptorSetLayout_/d.descSet) and, when !needsAlphaTest,
            // by the colored3d/textured3d/colored_textured3d fog-capable bundle (Task 899).
            const auto* vs = params.texture0 ? dynamic_cast<const IVulkanSamplable*>(params.texture0) : nullptr;
            VkImageView view = vs ? vs->GetVkImageView() : defaultWhiteView_;
            d.descSet = GetOrCreateTexSamplerDescSet(view, slotSamplers_[0]);
            if (d.useFogTex3D) {
                EnsureFogTex3DResources();
                d.fogTex3DDescSet = GetOrCreateFogTex3DDescSet(currentFrame_, view, slotSamplers_[0]);
                d.fogTex3DUboData[0] = params.fogColor[0]; d.fogTex3DUboData[1] = params.fogColor[1];
                d.fogTex3DUboData[2] = params.fogColor[2]; d.fogTex3DUboData[3] = params.fogEnabled ? 1.f : 0.f;
                d.fogTex3DUboData[4] = params.fogVector[0]; d.fogTex3DUboData[5] = params.fogVector[1];
                d.fogTex3DUboData[6] = params.fogVector[2]; d.fogTex3DUboData[7] = params.fogVector[3];
            }
        }
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-065: a compiled Effect owns the whole program, so it replaces the stock
        // stride-dispatched selection above rather than layering on it. Everything the deferred
        // replay needs is captured now, while the pass is still the applied one.
        if (params.compiledEffectRuntime != nullptr)
            PrepareCompiledEffectDrawEXT(d, vb_in, params);
#else
        if (params.compiledEffectRuntime != nullptr)
        {
            throw System::NotSupportedException(
                "CNA Vulkan: this build has no compiled-effect support "
                "(CNA_VULKAN_COMPILED_EFFECTS is off); drawing with a compiled Effect would "
                "silently use a stock shader instead.");
        }
#endif
        PushPending3DDraw(std::move(d));
    }

    void VulkanRenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
        // REMED-GFX-202: the per-instance stream is the lowest-slot entry of the shared
        // GpuVertexStreamBinding array whose InstanceFrequency is greater than zero.
        const auto* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr) {
            // No per-instance VB — fall back to single-instance indexed draw.
            DrawIndexedPrimitivesEx(vb_in, ib_in, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // REMED-GFX-202: one stream of each rate (REMED-GFX-203 tracks widening it).
        RejectUnsupportedStreamCombination(params, "The Vulkan renderer");
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        const bool compiledEffectDraw = params.compiledEffectRuntime != nullptr;
#else
        constexpr bool compiledEffectDraw = false;
        if (params.compiledEffectRuntime != nullptr)
        {
            throw System::NotSupportedException(
                "CNA Vulkan: this build has no compiled-effect support "
                "(CNA_VULKAN_COMPILED_EFFECTS is off); this instanced draw would silently use a "
                "stock shader instead.");
        }
#endif
        // REMED-GFX-DECL-GUARD: the geometry stream's declaration, against the Instanced3D
        // module's own inferred layout -- which binds a packed colour only at the two strides
        // PackedColorOffsetForStride lists and is position-only everywhere else. plans/plan_fx.md
        // FX-112: not applied to a compiled draw, which builds its vertex input from the
        // declarations rather than from the stride, so any declaration it can satisfy is faithful
        // by construction.
        const auto& vbForLayout = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        const VulkanVertexInputLayoutEXT instancedLayout =
            BuildInstancedVertexLayoutEXT(vbForLayout.GetDeclarationEXT());
        // VULKAN-149: as on the ordinary routes -- the guard is for a route that infers its input
        // from the stride, and a declaration that supplied every per-vertex input of the program
        // it selected means this one no longer does. Everything else still goes through it
        // unchanged, including a declaration that left one of those two inputs unsupplied.
        if (!instancedLayout.IsComplete())
            RequireFaithfulDeclarationEXT(vb_in, "instanced", /*positionOnlyFallback=*/true,
                                          compiledEffectDraw);

        // REMED-GFX-151: as in the two Ex draws above. The `instanceVb == nullptr` branch already
        // returned through DrawIndexedPrimitivesEx, which notes them itself.
        NoteSampledSourcesEXT(params);
        EnsureDefaultWhiteTexture();
        EnsureFrame3DInstBuffers();

        const auto& vb       = static_cast<const VulkanVertexBufferRenderer&>(vb_in);
        const auto& ib       = static_cast<const VulkanIndexBufferRenderer&>(ib_in);
        const auto& instVb   =
            static_cast<const VulkanVertexBufferRenderer&>(*instanceStream->buffer);
        const std::size_t pvStride   = vb.GetStride() > 0 ? vb.GetStride() : 20;
        const std::size_t instStride = instVb.GetStride() > 0 ? instVb.GetStride() : 64;
        const uint32_t indexCount    = static_cast<uint32_t>(VertexCountForPrimitives(primitive, primitiveCount));
        const int vertexCount        = vb.GetVertexCount();
        const int instCountClamped   = std::max(1, instanceCount);

        // REMED-GFX-211: the GEOMETRY binding's own VertexOffset, which this route dropped. The
        // deferred arena copies the whole per-vertex buffer and binds it at the draw's own packed
        // arena offset, so binding 0 has no per-binding native offset channel to carry it -- but
        // vkCmdDrawIndexed's `vertexOffset` is added to every decoded index, which is exactly the
        // term this stream owes, and it is applied to the per-vertex binding only. The route binds
        // exactly one per-vertex stream (RejectUnsupportedStreamCombination above), so folding it
        // into baseVertex advances that stream and nothing else, exactly once: the fetched element
        // becomes `VertexOffset + baseVertex + index`. The index buffer is untouched -- startIndex
        // stays an index-element offset, already applied to the index copy below.
        const GpuVertexStreamBinding* perVertexStream = FirstPerVertexStream(params);
        const int perVertexOffset = perVertexStream != nullptr ? perVertexStream->vertexOffset : 0;

        // REMED-GFX-211/213: the shared layer validates both of these before dispatch
        // (ValidateVertexStreamRanges / ValidateInstanceStreamRanges), so neither can fire for a
        // draw that arrived through GraphicsDevice. They exist because Draw*PrimitivesEx is a
        // public interface method a harness may call with a hand-built GpuDrawParams, and because
        // an offset that was previously ignored now indexes a real source copy -- an out-of-range
        // one must name its slot here rather than over-read the mapped buffer and leave the
        // diagnosis to a native layer that cannot see the public contract.
        const int instanceFrequency = std::max(1, instanceStream->instanceFrequency);
        const int lastInstanceRecord =
            instanceStream->vertexOffset + (instCountClamped - 1) / instanceFrequency;
        if (perVertexOffset < 0 || perVertexOffset > vertexCount ||
            params.baseVertex > vertexCount - perVertexOffset)
        {
            throw std::runtime_error(
                "The Vulkan renderer: the per-vertex VertexBufferBinding.VertexOffset bound to slot " +
                std::to_string(perVertexStream != nullptr ? perVertexStream->slot : 0) +
                " leaves its own vertex buffer.");
        }
        if (instanceStream->vertexOffset < 0 || lastInstanceRecord >= instVb.GetVertexCount())
        {
            throw std::runtime_error(
                "The Vulkan renderer: the per-instance VertexBufferBinding bound to slot " +
                std::to_string(instanceStream->slot) + " does not hold record " +
                std::to_string(lastInstanceRecord) + '.');
        }

        Pending3DDraw d{};
        // VULKAN-097: this route's world comes from the per-instance buffer, so the
        // correction rides on the projection half of the view-projection product.
        FillInstancedPushConst(d.pushConst, view,
                               projection * XnaPixelCenterCorrectionEXT(primitive), params);

        // Copy per-vertex data (all vertices)
        d.vbData.resize(static_cast<std::size_t>(vertexCount) * pvStride);
        std::memcpy(d.vbData.data(), vb.GetMappedPtr(),
                    static_cast<std::size_t>(vertexCount) * pvStride);

        // Copy index data (with startIndex offset)
        const int indexSize = ib.IsThirtyTwoBit() ? 4 : 2;
        d.ibData.resize(static_cast<std::size_t>(indexCount) * indexSize);
        std::memcpy(d.ibData.data(),
                    static_cast<const uint8_t*>(ib.GetMappedPtr()) + params.startIndex * indexSize,
                    static_cast<std::size_t>(indexCount) * indexSize);

        // Copy per-instance data: one destination record per instance, exactly as before -- only
        // WHICH source record each one takes changed.
        //
        // REMED-GFX-211: the first source record is this stream's own VertexOffset, converted with
        // this stream's own stride, never binding 0's.
        //
        // REMED-GFX-213: instance i takes record `VertexOffset + i / InstanceFrequency` -- the same
        // rule glVertexAttribDivisor and D3D11's InstanceDataStepRate define, and the one EasyGL
        // already implements natively. Vulkan 1.1 with VK_KHR_swapchain as its only device
        // extension has no vertex-attribute-divisor feature to enable, so binding 1 keeps the
        // implicit divisor of 1 and the grouping is expanded here, into the staging vector this
        // route already fills. Nothing about the native binding, the pipeline or its cache key
        // changes, and no frequency reaches them -- the divisor is a data-copy concern only.
        // Frequency 1 stays the single bulk copy it has always been.
        d.instVbData.resize(static_cast<std::size_t>(instCountClamped) * instStride);
        const auto* instSrc = static_cast<const uint8_t*>(instVb.GetMappedPtr()) +
                              static_cast<std::size_t>(instanceStream->vertexOffset) * instStride;
        if (instanceFrequency == 1) {
            std::memcpy(d.instVbData.data(), instSrc, d.instVbData.size());
        } else {
            for (int i = 0; i < instCountClamped; ++i)
                std::memcpy(d.instVbData.data() + static_cast<std::size_t>(i) * instStride,
                            instSrc + static_cast<std::size_t>(i / instanceFrequency) * instStride,
                            instStride);
        }

        d.topology     = ToVkTopology(primitive);
        d.drawCount    = indexCount;
        d.depthTest    = depthTestEnabled_;
        d.depthWrite   = depthWriteEnabled_;
        d.dsParams = dsParams_;
        d.stencilReadMask = stencilReadMask_;
        d.stencilWriteMask = stencilWriteMask_;
        d.referenceStencil = referenceStencil_;
        d.blend        = blendEnabled_;
        d.blendParams = blendParams_;
        d.cullMode     = cullMode_;
        d.wireframe  = fillModeWireframe_;
        d.depthBias  = depthBias_;
        d.slopeScaleDepthBias = slopeScaleDepthBias_;
        d.indexType    = ib.IsThirtyTwoBit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        d.rt           = currentRT_;
        d.stride       = pvStride;
        d.instVbStride = instStride;
        d.instanceCount = static_cast<uint32_t>(instCountClamped);
        // REMED-GFX-211: the geometry binding's VertexOffset rides the native draw's own
        // vertexOffset term alongside baseVertex; captured by value here, so a later
        // SetVertexBuffers cannot reach this queued draw.
        d.baseVertex   = static_cast<int32_t>(params.baseVertex + perVertexOffset);
        d.useInstanced = true;
        // VULKAN-149: taken at DRAW time, like every other route's, because the record is replayed
        // at Present() by which time the buffer's declaration may have been replaced.
        d.vertexLayout = instancedLayout;
        d.descSet      = defaultWhiteDescSet_;  // no per-draw texture for now
#if defined(CNA_VULKAN_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-112: two streams, per-vertex first, so binding 0 is the geometry and
        // binding 1 the per-instance records this route has already expanded to divisor 1 (see the
        // REMED-GFX-213 copy above -- the frequency is a data-copy concern here, never a pipeline
        // one). Everything else the compiled draw needs was captured by the block above; the
        // replay path already binds binding 1 and passes instanceCount for `useInstanced` draws,
        // so nothing there changes.
        if (compiledEffectDraw)
        {
            const auto& pvDeclaration = vb.GetDeclarationEXT();
            const auto& instDeclaration = instVb.GetDeclarationEXT();
            if (pvDeclaration.IsEmpty() || instDeclaration.IsEmpty())
            {
                throw System::NotSupportedException(
                    "CNA Vulkan: a compiled-effect instanced draw needs both bound buffers' own "
                    "VertexDeclarations; this renderer does not infer one from stride for this "
                    "route.");
            }
            const std::vector<VkFxStreamEXT> streams{
                {&pvDeclaration.GetElements(), static_cast<std::uint32_t>(pvStride), false},
                {&instDeclaration.GetElements(), static_cast<std::uint32_t>(instStride), true},
            };
            PrepareCompiledEffectDrawEXT(d, streams, params.compiledEffectRuntime);
        }
#endif
        PushPending3DDraw(std::move(d));
    }

    // ---- Graphics state ----

    void VulkanRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 → Opaque preset: src=One, dst=Zero → no blending
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1);
        // Task 868: previously every one of these 6 real values was discarded except for the
        // enabled/disabled boolean above -- every pipeline hardcoded BlendState.NonPremultiplied's
        // own equation whenever blending was on at all. Now stored for real use by
        // FillBlendAttachmentState() at pipeline-creation time.
        blendParams_.colorSrc  = colorSrcBlend;
        blendParams_.colorDst  = colorDstBlend;
        blendParams_.alphaSrc  = alphaSrcBlend;
        blendParams_.alphaDst  = alphaDstBlend;
        blendParams_.colorFunc = colorBlendFunc;
        blendParams_.alphaFunc = alphaBlendFunc;
        // REMED-GFX-077: the four per-MRT-slot colour write masks + the coverage sample mask are
        // static pipeline state — stored into blendParams_ and consumed by FillBlendAttachmentState
        // (per-attachment colorWriteMask) + VkPipelineMultisampleStateCreateInfo::pSampleMask, and
        // folded into the pipeline cache key (PackColorWriteBits + sampleMask). MRT slots 0..3 map
        // to ColorWriteChannels/1/2/3.
        blendParams_.colorWrite[0] = writeState.colorWriteChannels[0];
        blendParams_.colorWrite[1] = writeState.colorWriteChannels[1];
        blendParams_.colorWrite[2] = writeState.colorWriteChannels[2];
        blendParams_.colorWrite[3] = writeState.colorWriteChannels[3];
        blendParams_.sampleMask    = writeState.multiSampleMask;
    }

    void VulkanRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail,
                                                        int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask,
                                                        int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        depthTestEnabled_  = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        dsParams_.depthFunc           = depthFunc;
        dsParams_.stencilEnable       = stencilEnable;
        dsParams_.stencilFunc         = stencilFunc;
        dsParams_.stencilFail         = stencilFail;
        dsParams_.stencilDepthFail    = stencilDepthFail;
        dsParams_.stencilPass         = stencilPass;
        dsParams_.twoSidedStencilMode = twoSidedStencilMode;
        dsParams_.ccwStencilFunc      = ccwStencilFunc;
        dsParams_.ccwStencilFail      = ccwStencilFail;
        dsParams_.ccwStencilDepthFail = ccwStencilDepthFail;
        dsParams_.ccwStencilPass      = ccwStencilPass;
        stencilReadMask_  = stencilMask;
        stencilWriteMask_ = stencilWriteMask;
        referenceStencil_ = referenceStencil;
    }

    void VulkanRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // XNA FillMode: Solid=0, WireFrame=1
        cullMode_            = cullMode;
        fillModeWireframe_   = (fillMode == 1) && fillModeNonSolidSupported_;
        scissorEnabled_      = scissorTestEnable;
        // DepthBias maps to vkCmdSetDepthBias constant factor, SlopeScaleDepthBias to the
        // slope factor — matching FNA's glPolygonOffset(slopeScaleDepthBias, depthBias).
        depthBias_           = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void VulkanRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        // Storage-only: consumed at command-buffer-record time. REMED-GFX-013 snapshots this
        // state per draw/batch at enqueue time (PushPending3DDraw / SpriteBatch End()), so it is
        // applied correctly in BOTH the backbuffer and render-target passes and survives Task
        // 338's ScissorRectangle reset on render-target unbind. Negative width/height clamp to 0
        // (treated as "no clip"); the record-time computeScissor helper clamps offset/extent to
        // the target framebuffer so the resulting VkRect2D is always Vulkan-valid.
        scissorX_ = static_cast<int32_t>(x);
        scissorY_ = static_cast<int32_t>(y);
        scissorW_ = static_cast<uint32_t>(std::max(0, w));
        scissorH_ = static_cast<uint32_t>(std::max(0, h));
    }

    void VulkanRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        // Storage-only (Task 880); consumed at command-buffer-record time via vkCmdSetViewport.
        // REMED-GFX-062 snapshots this state per draw/batch at enqueue time (PushPending3DDraw /
        // SpriteBatch End()) and replays it per draw via computeViewport, so a custom sub-region
        // Viewport is honored in BOTH the backbuffer and render-target passes and survives
        // SetRenderTarget's full-target reset on RT bind/unbind (GraphicsDevice::
        // ResetViewportAndScissorForRenderTarget). This mirrors REMED-GFX-013's identical per-draw
        // scissor capture; a single frame-global value could not survive the deferred,
        // potentially-multi-RT-per-frame recording model. Negative width/height clamp to 0 (treated
        // as "full target"); minDepth/maxDepth are clamped to VkViewport's valid [0,1] at record
        // time.
        viewportX_        = static_cast<int32_t>(x);
        viewportY_        = static_cast<int32_t>(y);
        viewportW_        = static_cast<uint32_t>(std::max(0, w));
        viewportH_        = static_cast<uint32_t>(std::max(0, h));
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
        viewportSet_      = true;
    }

    void VulkanRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        // REMED-GFX-070: store-only. GraphicsDevice pushes GraphicsDevice.BlendFactor here as
        // already-normalized [0,1] floats. Because the renderer is a whole-frame-deferred recorder,
        // this frame-global value is snapshotted per draw/batch at enqueue (PushPending3DDraw /
        // SpriteBatch End()) and replayed per draw via vkCmdSetBlendConstants (see draw3DFor/
        // drawSpritesFor) -- not read once at record time, which would give every queued draw the
        // frame's last BlendFactor and leave render-target passes with no constant set at all.
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void VulkanRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    std::unique_ptr<IOcclusionQueryRenderer> VulkanRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<VulkanOcclusionQueryRenderer>(this);
    }

    std::unique_ptr<ITexture3DRenderer> VulkanRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTexture3DRenderer>(this, w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeRenderer> VulkanRenderer::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<VulkanTextureCubeRenderer>(this, size, mipMap);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> VulkanRenderer::CreateRenderTargetCube(int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136: preserveContents is the public RenderTargetUsage, reaching a cube target
        // for the first time -- see VulkanRenderTargetCubeRenderer's own constructor comment.
        // mipMap (Task 907): real per-face vkCmdBlitImage cascade, mirroring Task 878's
        // RenderTarget2D fix -- see VulkanTargetPassEXT::MaybeGenerateMips.
        // multiSampleCount (Task 903): now wired up -- mirrors VulkanRenderTargetRenderer's
        // Task 878/879 "piggyback on the renderer's own sampleCount_" MSAA treatment, applied per
        // cube face via a shared MSAA color image (see VulkanRenderTargetCubeRenderer's
        // constructor). depthFormat (Task 877) now gets true per-instance fidelity (Task 911),
        // mirroring VulkanRenderTargetRenderer's identical treatment.
        return std::make_unique<VulkanRenderTargetCubeRenderer>(this, size, depthFormat,
                                                               preserveContents, mipMap,
                                                               multiSampleCount);
    }

    void VulkanRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // REMED-GFX-075: a proxy is a VulkanRTSource DESTINATION referenced by the deferred
        // queues. Retiring to backbuffer or to a new binding must not destroy the old proxy while
        // its already-queued render work legitimately awaits Present. It goes to the frame-gated
        // retirement list, which is what keeps its VkFramebuffer alive until the GPU is done with
        // it; REMED-GFX-166's shared destinations independently keep the OBJECT alive until the
        // record that consumes those entries.
        auto releaseMrtProxy = [this]() {
            if (mrtProxy_) retiredMrtProxies_.emplace_back(frameGeneration_, std::move(mrtProxy_));
        };
        if (!renderTargets || count <= 0) {
            BeginRenderPassSegmentEXT(nullptr);
            releaseMrtProxy();
            return;
        }
        if (count == 1) {
            releaseMrtProxy();
            if (renderTargets[0].IsRenderTargetCubeFace()) {
                auto* cube = dynamic_cast<VulkanRenderTargetCubeRenderer*>(
                    renderTargets[0].GetRenderTargetCube());
                if (!cube)
                    throw std::runtime_error(
                        "Vulkan SetRenderTargets: cube target is not a Vulkan render target");
                cube->BindAsRenderTargetFace(renderTargets[0].GetCubeFace());
            } else {
                auto* vrt = dynamic_cast<VulkanRenderTargetRenderer*>(
                    renderTargets[0].GetRenderTarget2D());
                if (!vrt)
                    throw std::runtime_error(
                        "Vulkan SetRenderTargets: 2D target is not a Vulkan render target");
                vrt->BindAsRenderTarget();
            }
            return;
        }
        // Build MRT proxy for N > 1.
        releaseMrtProxy();
        mrtProxy_ = std::make_shared<VulkanMRTProxy>(
            this, renderTargets, static_cast<uint32_t>(count));
        BeginRenderPassSegmentEXT(mrtProxy_);
    }

    // --- VulkanMRTProxy ---

    VulkanMRTProxy::VulkanMRTProxy(VulkanRenderer* owner,
                                    const RenderTargetBindingDescriptor* renderTargets,
                                    uint32_t count)
        : owner_(owner), colorCount_(count)
    {
        if (!owner || !renderTargets || count == 0) return;
        VkDevice dev = owner->device_;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(owner->physicalDevice_, &properties);
        if (count > 4 || count > properties.limits.maxColorAttachments)
            throw std::runtime_error("VulkanMRTProxy: render-target count exceeds supported MRT limit");
        if (!owner->independentBlendSupported_)
            throw std::runtime_error(
                "Vulkan MRT requires independentBlend for CNA per-target render state");

        struct Attachment
        {
            int width = 0;
            int height = 0;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkImageView resolveView = VK_NULL_HANDLE;
            VkImageView msaaView = VK_NULL_HANDLE;
            VkImageView depthView = VK_NULL_HANDLE;
            VkFormat depthFormat = VK_FORMAT_UNDEFINED;
            std::shared_ptr<VulkanTargetPassEXT> targetPass;
        };
        auto normalize = [](const RenderTargetBindingDescriptor& binding) {
            Attachment result;
            result.width = binding.GetWidth();
            result.height = binding.GetHeight();
            if (binding.IsRenderTargetCubeFace()) {
                auto* cube = dynamic_cast<VulkanRenderTargetCubeRenderer*>(
                    binding.GetRenderTargetCube());
                if (!cube)
                    throw std::runtime_error(
                        "VulkanMRTProxy: cube target is not a Vulkan render target");
                result.samples = cube->GetColorSampleCountEXT();
                result.resolveView =
                    cube->GetFaceResolveViewEXT(binding.GetCubeFace());
                // REMED-GFX-141: the bound face's OWN multisample view. This used to be one shared
                // view per cube, so an MRT set naming two faces of the SAME cube wired both colour
                // attachments to identical multisample storage while resolving them to different
                // layers -- the six-face alias reaching the MRT path.
                result.msaaView = cube->GetMsaaColorViewEXT(binding.GetCubeFace());
                result.depthView = cube->GetDepthViewEXT();
                result.depthFormat = cube->GetDepthFormatEXT();
                const int face = binding.GetCubeFace();
                if (face >= 0 && face < 6)
                    result.targetPass = cube->facePasses_[static_cast<std::size_t>(face)];
            } else {
                auto* rt2D = dynamic_cast<VulkanRenderTargetRenderer*>(
                    binding.GetRenderTarget2D());
                if (!rt2D)
                    throw std::runtime_error(
                        "VulkanMRTProxy: 2D target is not a Vulkan render target");
                result.samples = rt2D->GetColorSampleCountEXT();
                result.resolveView = rt2D->GetResolveColorViewEXT();
                result.msaaView = rt2D->GetMsaaColorViewEXT();
                result.depthView = rt2D->GetDepthView();
                result.depthFormat = rt2D->PassEXT() ? rt2D->PassEXT()->GetDepthFormat()
                                                    : VK_FORMAT_UNDEFINED;
                result.targetPass = rt2D->PassEXT();
            }
            return result;
        };

        std::vector<Attachment> attachments;
        attachments.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            attachments.push_back(normalize(renderTargets[i]));

        width_  = attachments[0].width;
        height_ = attachments[0].height;
        colorSampleCount_ = attachments[0].samples;
        depthFormat_ = attachments[0].depthFormat;
        depthView_ = attachments[0].depthView;
        for (uint32_t i = 0; i < count; ++i) {
            if (attachments[i].resolveView == VK_NULL_HANDLE)
                throw std::runtime_error(
                    "VulkanMRTProxy: target resolve attachment view is missing");
            if (attachments[i].samples > VK_SAMPLE_COUNT_1_BIT
                && attachments[i].msaaView == VK_NULL_HANDLE)
                throw std::runtime_error(
                    "VulkanMRTProxy: multisample target source attachment view is missing");
            if (i == 0) continue;
            if (attachments[i].width != width_ || attachments[i].height != height_)
                throw std::runtime_error(
                    "Vulkan MRT targets must have matching dimensions");
            if (attachments[i].samples != colorSampleCount_)
                throw std::runtime_error(
                    "Vulkan MRT targets must have matching applied sample counts");
            for (uint32_t previous = 0; previous < i; ++previous)
                if (attachments[i].resolveView
                    == attachments[previous].resolveView) {
                    throw std::runtime_error(
                        "Vulkan MRT cannot bind the same target subresource more than once");
                }
        }

        renderPass_ = owner->GetOrCreateMRTRenderPass(
            count, colorSampleCount_, depthFormat_);

        // REMED-GFX-095: each target keeps ownership of both resources. MRT only selects
        // the already-existing transient MSAA view as color i and the texture view as
        // resolve i. Non-MSAA continues to bind the texture views directly.
        colorAttachments_.reserve(count);
        colorTargetPasses_.reserve(count);
        if (WantsMsaa()) resolveAttachments_.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const VkImageView resolve = attachments[i].resolveView;
            const VkImageView color = WantsMsaa()
                ? attachments[i].msaaView
                : resolve;
            if (color == VK_NULL_HANDLE || resolve == VK_NULL_HANDLE)
                throw std::runtime_error("VulkanMRTProxy: target attachment view is missing");
            if (!attachments[i].targetPass)
                throw std::runtime_error("VulkanMRTProxy: target pass metadata is missing");
            if (WantsMsaa()
                && std::find(colorAttachments_.begin(), colorAttachments_.end(), color)
                    != colorAttachments_.end())
                throw std::runtime_error(
                    "Vulkan MRT cannot bind one multisample source subresource to more "
                    "than one slot (same-cube multi-face MSAA is unsupported)");
            colorAttachments_.push_back(color);
            colorTargetPasses_.push_back(attachments[i].targetPass);
            if (WantsMsaa()) resolveAttachments_.push_back(resolve);
        }

        framebufferAttachments_ = colorAttachments_;
        framebufferAttachments_.insert(framebufferAttachments_.end(),
                                       resolveAttachments_.begin(),
                                       resolveAttachments_.end());
        if (depthView_ != VK_NULL_HANDLE)
            framebufferAttachments_.push_back(depthView_);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments_.size());
        fbInfo.pAttachments    = framebufferAttachments_.data();
        fbInfo.width           = static_cast<uint32_t>(width_);
        fbInfo.height          = static_cast<uint32_t>(height_);
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
            throw std::runtime_error("VulkanMRTProxy: vkCreateFramebuffer failed");
    }

    // VULKAN-405: the release half, so the renderer's destructor can end this framebuffer's life
    // while the device is still alive rather than hoping the last share is dropped in time.
    void VulkanMRTProxy::ReleaseVulkanResources()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        if (framebuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(owner_->device_, framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
        }
    }

    VulkanMRTProxy::~VulkanMRTProxy()
    {
        // Unchanged for the ordinary case, and a no-op after ReleaseVulkanResources() has run.
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(dev, framebuffer_, nullptr);
    }

    void VulkanMRTProxy::MaybeGenerateMips(VkCommandBuffer cb)
    {
        // REMED-GFX-190: the MRT proxy is the deferred segment's VulkanRTSource, so the generic
        // post-pass hook reaches only this object, not the individual RenderTarget2D/cube-face
        // sources it combines. Preserve the public binding order and delegate to those immutable
        // pass records after vkCmdEndRenderPass: MSAA resolves have therefore completed first,
        // one-level attachments no-op in VulkanTargetPassEXT, and no resource outside this
        // producer segment is visited.
        for (const auto& targetPass : colorTargetPasses_)
            if (targetPass) targetPass->MaybeGenerateMips(cb);
    }

    // --- VulkanTexture3DRenderer ---

    // Task 864: mirrors Texture3D.cpp's CalculateMipLevels(w,h) -- depth does not participate in
    // the level count, matching FNA's Texture3D constructor exactly.
    static int CalculateVulkanTexture3DMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    VulkanTexture3DRenderer::VulkanTexture3DRenderer(VulkanRenderer* owner, int w, int h, int depth, bool mipMap)
        : owner_(owner), width_(w), height_(h), depth_(depth)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        levelCount_ = mipMap ? CalculateVulkanTexture3DMipLevels(w, h) : 1;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_3D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                   static_cast<uint32_t>(depth) };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC (Task 865): needed by GetData's vkCmdCopyImageToBuffer readback.
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS) return;

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) return;
        vkBindImageMemory(dev, image_, memory_, 0);

        // Task 864: covers *all* levelCount_ levels (not just level 0, unlike the shared
        // single-level TransitionImageLayout helper) so SetData/GetData can address any mip
        // level from construction time, mirroring VulkanRenderTargetRenderer's identical fix.
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 1 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       static_cast<uint32_t>(levelCount_), 0, 1 };
        vkCreateImageView(dev, &viewInfo, nullptr, &imageView_);

        owner_->liveTexture3Ds_.push_back(this);   // VULKAN-407
    }

    // VULKAN-407: the release half, so the renderer's destructor can end these resources' lives
    // while the device is still alive instead of hoping this object is destroyed first.
    void VulkanTexture3DRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        if (imageView_ != VK_NULL_HANDLE) { vkDestroyImageView(dev, imageView_, nullptr); imageView_ = VK_NULL_HANDLE; }
        if (image_     != VK_NULL_HANDLE) { vkDestroyImage(dev, image_, nullptr);          image_     = VK_NULL_HANDLE; }
        if (memory_    != VK_NULL_HANDLE) { vkFreeMemory(dev, memory_, nullptr);           memory_    = VK_NULL_HANDLE; }
    }

    VulkanTexture3DRenderer::~VulkanTexture3DRenderer()
    {
        if (owner_) {
            auto& list = owner_->liveTexture3Ds_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    // REMED-GFX-093: Texture3D copies address one mip level of one 3D image array layer.  Depth
    // slices are z coordinates inside that subresource, never VkImage array layers.  Construction
    // puts every mip in SHADER_READ_ONLY_OPTIMAL and each synchronous SetData/GetData operation
    // restores its selected mip to that invariant, so no mutable layout tracker is needed.
    static void CmdTransitionTexture3DLevel(VkCommandBuffer cb, VkImage image, int level,
                                             VkImageLayout from, VkImageLayout to)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1, 0, 1 };

        VkPipelineStageFlags srcStage = 0;
        VkPipelineStageFlags dstStage = 0;
        if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            to   == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                 to   == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
                 to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            throw std::runtime_error("Vulkan Texture3D: unsupported image layout transition");
        }

        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }

    bool VulkanTexture3DRenderer::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int dataLength)
    {
        // REMED-GFX-135: see VulkanTextureCubeRenderer::SetData -- silent returns looked like writes.
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        const int regionBytes = w * h * depth * 4;
        if (dataLength < regionBytes) return false;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(regionBytes),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        if (mapped == nullptr)
        {
            if (stagingBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stagingBuf, nullptr);
            if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, data, static_cast<size_t>(regionBytes));

        // Keep both level-scoped barriers and the copy in one submission.  Besides making the
        // exact transition history explicit, this reduces the old three queue-idle waits to one.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        CmdTransitionTexture3DLevel(cb, image_, level,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { x, y, z };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                     static_cast<uint32_t>(depth) };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        CmdTransitionTexture3DLevel(cb, image_, level,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        owner_->EndOneTimeCommands(cb);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return true;
    }

    // Task 865: real GPU readback via vkCmdCopyImageToBuffer + a host-visible staging buffer,
    // mirroring SetData's upload path in reverse.
    bool VulkanTexture3DRenderer::GetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: this guard used to be a silent `return`, which the shared layer turned
        // into a complete transparent-black volume instead of a refusal.
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return false;
        if (level < 0 || level >= levelCount_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (dataLength < w * h * depth * 4) return false;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        // A failed allocation leaves `mapped` null; copying from it would be undefined behaviour
        // and reporting success would be the same fabrication one level down.
        if (mapped == nullptr)
        {
            if (stagingBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stagingBuf, nullptr);
            if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }

        // As in SetData, transition only the selected mip and restore it in the same submission.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        CmdTransitionTexture3DLevel(cb, image_, level,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, 1 };
        region.imageOffset      = { x, y, z };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                     static_cast<uint32_t>(depth) };
        vkCmdCopyImageToBuffer(cb, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);

        CmdTransitionTexture3DLevel(cb, image_, level,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        owner_->EndOneTimeCommands(cb);

        std::memcpy(data, mapped,
                    static_cast<size_t>(w) * static_cast<size_t>(h) *
                    static_cast<size_t>(depth) * 4u);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return true;
    }

    // --- VulkanTextureCubeRenderer ---

    // Task 864: mirrors TextureCube.cpp's CalculateMipLevels(size,size) -- cube faces are square.
    static int CalculateVulkanTextureCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    VulkanTextureCubeRenderer::VulkanTextureCubeRenderer(VulkanRenderer* owner, int size, bool mipMap)
        : owner_(owner), size_(size)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice dev = owner_->device_;
        levelCount_ = mipMap ? CalculateVulkanTextureCubeMipLevels(size) : 1;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent        = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1 };
        imgInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        imgInfo.arrayLayers   = 6;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC (Task 865): needed by GetData's vkCmdCopyImageToBuffer readback.
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &imgInfo, nullptr, &image_) != VK_SUCCESS) return;

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(dev, image_, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = owner_->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) return;
        vkBindImageMemory(dev, image_, memory_, 0);

        // Transition all 6 faces to shader-read-only (empty initially).
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image_;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                         static_cast<uint32_t>(levelCount_), 0, 6 };
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        owner_->EndOneTimeCommands(cb);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       static_cast<uint32_t>(levelCount_), 0, 6 };
        vkCreateImageView(dev, &viewInfo, nullptr, &imageView_);

        owner_->liveTextureCubes_.push_back(this);   // VULKAN-407
    }

    // VULKAN-407: the release half. Unchanged in mechanism -- the handles still go through the
    // frame-fence-gated retirement queue -- only in who may call it and when.
    void VulkanTextureCubeRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        // REMED-GFX-075: a TextureCube sampled by a deferred draw (e.g. EnvironmentMapEffect) bakes
        // its cube VkImageView into that draw's descriptor set; retire the handles so a cube
        // destroyed before Present keeps the view alive until the record consumes the draw.
        VulkanRenderer::RetiredResources r;
        // REMED-GFX-076: a TextureCube's cube VkImageView is baked into EnvironmentMapEffect's
        // hash-keyed envMapDescSets_ entries; evict them so a later cube reusing the freed handle
        // value cannot collide with this destroyed cube's cached descriptor set.
        owner_->EvictSampledViewFromCaches(imageView_, r);
        if (imageView_ != VK_NULL_HANDLE) { r.imageViews.push_back(imageView_); imageView_ = VK_NULL_HANDLE; }
        if (image_     != VK_NULL_HANDLE) { r.images.push_back(image_);          image_     = VK_NULL_HANDLE; }
        if (memory_    != VK_NULL_HANDLE) { r.memories.push_back(memory_);        memory_    = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
    }

    VulkanTextureCubeRenderer::~VulkanTextureCubeRenderer()
    {
        if (owner_) {
            auto& list = owner_->liveTextureCubes_;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    bool VulkanTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int dataLength)
    {
        // REMED-GFX-135: each of these used to be a silent `return` the shared layer could not tell
        // apart from a completed upload, and the level/rectangle were not range-checked at all.
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || w <= 0 || h <= 0) return false;
        if (face < 0 || face >= 6 || level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const int regionBytes = w * h * 4;
        if (dataLength < regionBytes) return false;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(regionBytes),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        // A failed allocation leaves `mapped` null; copying into it would be undefined behaviour
        // and reporting success would be the same fabrication one level down (REMED-GFX-135,
        // mirroring the identical guard REMED-GFX-130 added to GetData).
        if (mapped == nullptr)
        {
            if (stagingBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stagingBuf, nullptr);
            if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, data, static_cast<size_t>(regionBytes));

        // Transition only the target face layer to TRANSFER_DST_OPTIMAL.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier toXfer{};
        toXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toXfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.image               = image_;
        toXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1,
                                        static_cast<uint32_t>(face), 1 };
        toXfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toXfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toXfer);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT,
                                     static_cast<uint32_t>(level),
                                     static_cast<uint32_t>(face), 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyBufferToImage(cb, stagingBuf, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toXfer;
        std::swap(toRead.oldLayout, toRead.newLayout);
        std::swap(toRead.srcAccessMask, toRead.dstAccessMask);
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);

        owner_->EndOneTimeCommands(cb);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        // EndOneTimeCommands submits and waits, so the copy out of the staging buffer has really
        // landed in the image by the time this returns -- nothing here still depends on `data`.
        return true;
    }

    // Task 865: real GPU readback via vkCmdCopyImageToBuffer + a host-visible staging buffer,
    // mirroring SetData's per-face upload path in reverse (inline barriers scoped to just the
    // target face layer, mirroring SetData's own approach -- the shared TransitionImageLayout
    // helper always transitions layer 0 only, so it can't be reused for an arbitrary cube face).
    bool VulkanTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        // REMED-GFX-130: these guards used to be silent `return`s, which the shared layer turned
        // into a complete transparent-black face instead of a refusal.
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return false;
        if (face < 0 || face >= 6) return false;
        if (level < 0 || level >= levelCount_ || w <= 0 || h <= 0) return false;
        if (dataLength < w * h * 4) return false;
        VkDevice dev = owner_->device_;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(dataLength),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        if (mapped == nullptr)
        {
            if (stagingBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stagingBuf, nullptr);
            if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }

        // Transition only the target face layer to TRANSFER_SRC_OPTIMAL.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier toXfer{};
        toXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toXfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.image               = image_;
        toXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1,
                                        static_cast<uint32_t>(face), 1 };
        toXfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toXfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toXfer);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT,
                                     static_cast<uint32_t>(level),
                                     static_cast<uint32_t>(face), 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyImageToBuffer(cb, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);

        VkImageMemoryBarrier toRead = toXfer;
        std::swap(toRead.oldLayout, toRead.newLayout);
        std::swap(toRead.srcAccessMask, toRead.dstAccessMask);
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);

        owner_->EndOneTimeCommands(cb);

        std::memcpy(data, mapped, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return true;
    }

    // --- VulkanOcclusionQueryRenderer ---

    VulkanOcclusionQueryRenderer::VulkanOcclusionQueryRenderer(VulkanRenderer* owner)
        : owner_(owner)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;

        VkQueryPoolCreateInfo qi{};
        qi.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType  = VK_QUERY_TYPE_OCCLUSION;
        qi.queryCount = 1;
        vkCreateQueryPool(owner_->device_, &qi, nullptr, &pool_);
        // Reset the query slot before first use.
        if (pool_ != VK_NULL_HANDLE)
        {
            VkCommandBuffer cb = owner_->BeginOneTimeCommands();
            vkCmdResetQueryPool(cb, pool_, 0, 1);
            owner_->EndOneTimeCommands(cb);
        }
    }

    VulkanOcclusionQueryRenderer::~VulkanOcclusionQueryRenderer()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        // REMED-GFX-075: pending 3D draws store a raw pointer to this query (read at record time to
        // emit vkCmdBeginQuery/EndQuery). Detach it from every deferred entry first -- the draws are
        // preserved, only the query wrapping is dropped (a disposed query's result is unobservable
        // per XNA, so recording it is pointless) -- so RecordCommandBuffer never dereferences this
        // freed wrapper. The VkQueryPool is retired (frame-fence-gated) in case an already-submitted
        // frame still references it.
        owner_->PurgeDeferredQuery(this);
        if (pool_ != VK_NULL_HANDLE) {
            VulkanRenderer::RetiredResources r;
            r.queryPools.push_back(pool_);
            pool_ = VK_NULL_HANDLE;
            owner_->RetireResources(std::move(r));
        }
    }

    void VulkanOcclusionQueryRenderer::Begin()
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE) return;
        ended_ = false;
        // Task 447/854: occlusion queries in Vulkan must be recorded inside a render pass, and
        // CNA's Vulkan renderer defers all draws to RecordCommandBuffer -- so Begin()/End() don't
        // inject any Vulkan commands directly. Instead, this marks the query "active": every
        // Pending3DDraw pushed via PushPending3DDraw() between now and End() gets tagged with
        // `this`, and RecordCommandBuffer() later wraps whichever contiguous run of tagged draws
        // land in the same render pass in a real vkCmdBeginQuery/vkCmdEndQuery pair.
        owner_->activeOcclusionQuery_ = this;
    }

    void VulkanOcclusionQueryRenderer::End()
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE) return;
        ended_ = true;
        // Guard rather than unconditionally clearing: XNA's own OcclusionQuery API doesn't
        // support nested Begin() calls, so this should always already be `this` in practice.
        if (owner_->activeOcclusionQuery_ == this)
            owner_->activeOcclusionQuery_ = nullptr;
        // pixelCount_ is no longer hardcoded to 0 here -- IsComplete() populates it once the
        // real GPU query result (recorded by RecordCommandBuffer(), see Begin()'s comment) is
        // ready, matching every other renderer's own async completion timing.
    }

    bool VulkanOcclusionQueryRenderer::IsComplete() const
    {
        if (!owner_ || pool_ == VK_NULL_HANDLE || !ended_) return false;
        uint64_t result = 0;
        VkResult r = vkGetQueryPoolResults(owner_->device_, pool_, 0, 1,
                                           sizeof(result), &result,
                                           sizeof(result),
                                           VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS)
        {
            pixelCount_ = static_cast<int>(result);
            return true;
        }
        return false; // VK_NOT_READY
    }

    int VulkanOcclusionQueryRenderer::PixelCount() const { return pixelCount_; }

    // --- VulkanRenderTargetCubeRenderer ---

    VulkanRenderTargetCubeRenderer::VulkanRenderTargetCubeRenderer(VulkanRenderer* owner, int size,
                                                                  int depthFormat,
                                                                  bool preserveContents, bool mipMap,
                                                                  int requestedMultiSampleCount)
        : owner_(owner), size_(size), preserveContents_(preserveContents)
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        VkDevice    dev  = owner_->device_;
        const auto  us   = static_cast<uint32_t>(size);
        levelCount_ = mipMap ? CalculateVulkanRTMipLevels(size, size) : 1;
        // REMED-GFX-166: one independently owned destination per face -- see facePasses_.
        for (auto& fp : facePasses_) fp = std::make_shared<VulkanTargetPassEXT>();

        // Task 911: real per-instance DepthStencilFormat fidelity, mirroring
        // VulkanRenderTargetRenderer's identical constructor fix -- None means no depth
        // attachment at all; otherwise a real, distinct VkFormat picked for THIS instance,
        // independent of the backbuffer's own depthFormat_.
        const bool hasDepth = (static_cast<DepthFormat>(depthFormat) != DepthFormat::None);
        if (hasDepth)
            depthVkFormat_ = PickDepthFormat(owner_->physicalDevice_, static_cast<DepthFormat>(depthFormat));

        // Task 903: mirrors VulkanRenderTargetRenderer's identical "piggyback on the renderer's own
        // sampleCount_" scope decision (Task 878/879) -- see plans/plan_graphics.md.
        const bool wantsMsaa = requestedMultiSampleCount > 0 &&
                               owner_->sampleCount_ > VK_SAMPLE_COUNT_1_BIT;

        // --- Color image: 6-layer cube-compatible, color attachment + sampled ---
        VkImageCreateInfo colorInfo{};
        colorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        colorInfo.imageType     = VK_IMAGE_TYPE_2D;
        colorInfo.format        = owner_->swapchainFormat_;
        colorInfo.extent        = { us, us, 1 };
        colorInfo.mipLevels     = static_cast<uint32_t>(levelCount_);
        colorInfo.arrayLayers   = 6;
        colorInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC/DST (Task 907): needed by VulkanTargetPassEXT::MaybeGenerateMips' vkCmdBlitImage
        // cascade when levelCount_ > 1; harmless when levelCount_==1.
        colorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        colorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &colorInfo, nullptr, &image_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImage failed");

        VkMemoryRequirements colorReq;
        vkGetImageMemoryRequirements(dev, image_, &colorReq);
        VkMemoryAllocateInfo colorAlloc{};
        colorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        colorAlloc.allocationSize  = colorReq.size;
        colorAlloc.memoryTypeIndex = owner_->FindMemoryType(colorReq.memoryTypeBits,
                                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &colorAlloc, nullptr, &memory_) != VK_SUCCESS)
            throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkAllocateMemory failed");
        vkBindImageMemory(dev, image_, memory_, 0);

        // --- Full-cube image view for sampling (VK_IMAGE_VIEW_TYPE_CUBE, all 6 layers, full mip
        // range -- Task 907: levelCount_ levels instead of hardcoded 1, mirroring
        // VulkanRenderTargetRenderer::colorSampleView_'s identical Task 878 fix) ---
        {
            VkImageViewCreateInfo cv{};
            cv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            cv.image    = image_;
            cv.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            cv.format   = owner_->swapchainFormat_;
            cv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(levelCount_), 0, 6 };
            if (vkCreateImageView(dev, &cv, nullptr, &cubeView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImageView (cube) failed");
        }

        // --- 6 per-face color image views (for framebuffer attachments) ---
        for (int face = 0; face < 6; ++face) {
            VkImageViewCreateInfo fv{};
            fv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            fv.image    = image_;
            fv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            fv.format   = owner_->swapchainFormat_;
            fv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                    static_cast<uint32_t>(face), 1 };
            if (vkCreateImageView(dev, &fv, nullptr, &faceViews_[face]) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImageView failed");
        }

        // Task 907: transition every level of every face to SHADER_READ_ONLY_OPTIMAL up front.
        // Level 0 of an actually-rendered face gets this from its own render pass's finalLayout
        // regardless (matching this class's pre-existing, unchanged behavior for the non-mip
        // case), but levels 1..levelCount_-1 are NEVER touched by any render pass -- only by
        // VulkanTargetPassEXT::MaybeGenerateMips' blit cascade, whose own first barrier for each
        // destination level assumes it starts in SHADER_READ_ONLY_OPTIMAL. Without this upfront
        // transition that assumption is false the first time any face is ever rendered,
        // producing live VUID-vkCmdDraw-None-09600 validation errors (mirrors
        // VulkanRenderTargetRenderer's identical Task 878 fix).
        // REMED-GFX-136: `|| preserveContents_` was the second reason this barrier is needed. A
        // PreserveContents face uses the LOAD render-pass variant, whose colour initialLayout is
        // SHADER_READ_ONLY_OPTIMAL, so every layer must already be in that layout the FIRST time
        // any face is bound -- otherwise vkCmdBeginRenderPass sees an image still in UNDEFINED.
        // The discard variant declares initialLayout UNDEFINED and did not need it for BINDING,
        // which is why Task 907's mip-cascade precondition used to be the only other trigger.
        // plan_vulkan.md VULKAN-406: it is now unconditional, because binding is not the only way
        // a layer is reached. A face that is never rendered at all is still readable and still
        // sampleable, and both of those paths declare `oldLayout = SHADER_READ_ONLY_OPTIMAL` for
        // the whole cube -- GetData's own barrier says so in as many words, and cubeView_ exposes
        // all six layers to the descriptor. On a single-level DiscardContents cube neither of the
        // two old conditions held, so layers nobody had rendered stayed in UNDEFINED and
        // `vkQueueSubmit` reported the mismatch (`arrayLayer = 2`) while llvmpipe returned the
        // right pixels anyway. This is what the 2D twin has done unconditionally since Task 878.
        {
            VkCommandBuffer initCb = owner_->BeginOneTimeCommands();
            VkImageMemoryBarrier initBarrier{};
            initBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            initBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initBarrier.image               = image_;
            initBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                                 static_cast<uint32_t>(levelCount_), 0, 6 };
            initBarrier.srcAccessMask       = 0;
            initBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &initBarrier);
            owner_->EndOneTimeCommands(initCb);
        }

        // --- Shared depth image (one 2D image reused across all faces; promoted to MSAA samples
        // when this cube engages MSAA, mirroring VulkanRenderTargetRenderer's depthImage_. Task
        // 911: only created when a real depth format was requested -- DepthFormat::None
        // correctly gets no depth attachment at all now.) ---
        if (hasDepth)
        {
            VkImageCreateInfo depthInfo{};
            depthInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthInfo.imageType     = VK_IMAGE_TYPE_2D;
            depthInfo.format        = depthVkFormat_;
            depthInfo.extent        = { us, us, 1 };
            depthInfo.mipLevels     = 1;
            depthInfo.arrayLayers   = 1;
            depthInfo.samples       = wantsMsaa ? owner_->sampleCount_ : VK_SAMPLE_COUNT_1_BIT;
            depthInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            depthInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &depthInfo, nullptr, &depthImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImage (depth) failed");

            VkMemoryRequirements depthReq;
            vkGetImageMemoryRequirements(dev, depthImage_, &depthReq);
            VkMemoryAllocateInfo depthAlloc{};
            depthAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            depthAlloc.allocationSize  = depthReq.size;
            depthAlloc.memoryTypeIndex = owner_->FindMemoryType(depthReq.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &depthAlloc, nullptr, &depthMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkAllocateMemory (depth) failed");
            vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

            VkImageViewCreateInfo dv{};
            dv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dv.image    = depthImage_;
            dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dv.format   = depthVkFormat_;
            dv.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(dev, &dv, nullptr, &depthView_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImageView (depth) failed");

            // REMED-GFX-142: exact counterpart of VulkanRenderTargetRenderer's own depth barrier.
            // A preserving cube's RT render pass now loads depth/stencil, and LOAD_OP_LOAD
            // declares initialLayout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL, so the shared depth image
            // has to be in that layout before the first face is ever bound. ONE image for all six
            // faces is deliberate and stays: FNA's RenderTargetCube allocates exactly one
            // glDepthStencilBuffer per cube, so depth is a per-TARGET resource that every face
            // shares -- unlike REMED-GFX-141's per-face multisample COLOUR storage.
            if (preserveContents_)
            {
                VkCommandBuffer depthCb = owner_->BeginOneTimeCommands();
                VkImageMemoryBarrier depthBarrier{};
                depthBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                depthBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                depthBarrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.image               = depthImage_;
                depthBarrier.subresourceRange    = {
                    static_cast<VkImageAspectFlags>(
                        VK_IMAGE_ASPECT_DEPTH_BIT |
                        (VkDepthFormatHasStencil(depthVkFormat_) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u)),
                    0, 1, 0, 1 };
                depthBarrier.srcAccessMask       = 0;
                depthBarrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                vkCmdPipelineBarrier(depthCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &depthBarrier);
                owner_->EndOneTimeCommands(depthCb);
            }
        }

        // --- Per-face MSAA color image (Task 903; REMED-GFX-141): ONE multisampled 2D image with
        // SIX array layers, and one per-layer VkImageView per face, so every face owns its own
        // multisample colour state. Task 903 originally allocated a single-layer image shared by
        // all six faces, on depthImage_'s "only one face is ever rendered into at a time"
        // reasoning. That is true while PRODUCING a face and false the moment a face is RELOADED:
        // a PreserveContents face rebound for a partial update loaded whichever face had been
        // rendered last. Six layers is what D3D11 and D3D12 have always allocated (a six-slice
        // multisampled array with one per-slice RTV) and it is what makes all six faces
        // simultaneously live. No cube-compatible flag: this image is never sampled, only
        // rendered into and resolved from, one layer at a time.
        //
        // TRANSIENT_ATTACHMENT is dropped for a preserving target: with REMED-GFX-141's LOAD/STORE
        // render pass the samples must genuinely survive between passes, which is the opposite of
        // what "transient" declares (and of what a LAZILY_ALLOCATED backing would provide on a
        // tiler). A discarding target keeps it, and keeps every byte of Task 903's behaviour. ---
        if (wantsMsaa)
        {
            VkImageCreateInfo msaaColorInfo{};
            msaaColorInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            msaaColorInfo.imageType     = VK_IMAGE_TYPE_2D;
            msaaColorInfo.format        = owner_->swapchainFormat_;
            msaaColorInfo.extent        = { us, us, 1 };
            msaaColorInfo.mipLevels     = 1;
            msaaColorInfo.arrayLayers   = 6;
            msaaColorInfo.samples       = owner_->sampleCount_;
            msaaColorInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            msaaColorInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                          (preserveContents_ ? 0u
                                                             : VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
            msaaColorInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            msaaColorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(dev, &msaaColorInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImage (MSAA color) failed");

            VkMemoryRequirements msaaColorReq;
            vkGetImageMemoryRequirements(dev, msaaColorImage_, &msaaColorReq);
            VkMemoryAllocateInfo msaaColorAlloc{};
            msaaColorAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            msaaColorAlloc.allocationSize  = msaaColorReq.size;
            msaaColorAlloc.memoryTypeIndex = owner_->FindMemoryType(msaaColorReq.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(dev, &msaaColorAlloc, nullptr, &msaaColorMemory_) != VK_SUCCESS)
                throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkAllocateMemory (MSAA color) failed");
            vkBindImageMemory(dev, msaaColorImage_, msaaColorMemory_, 0);

            for (int face = 0; face < 6; ++face)
            {
                VkImageViewCreateInfo msaaColorView{};
                msaaColorView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                msaaColorView.image    = msaaColorImage_;
                msaaColorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                msaaColorView.format   = owner_->swapchainFormat_;
                msaaColorView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                                   static_cast<uint32_t>(face), 1 };
                if (vkCreateImageView(dev, &msaaColorView, nullptr, &msaaColorViews_[face]) != VK_SUCCESS)
                    throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateImageView (MSAA color) failed");
            }

            // REMED-GFX-141: a preserving target's MSAA render pass declares initialLayout
            // COLOR_ATTACHMENT_OPTIMAL, which is what its own finalLayout leaves behind on every
            // subsequent pass -- but not what a freshly created image is in. One up-front
            // transition of all six layers, the exact counterpart of the image_ barrier above.
            if (preserveContents_)
            {
                VkCommandBuffer msaaInitCb = owner_->BeginOneTimeCommands();
                VkImageMemoryBarrier msaaBarrier{};
                msaaBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                msaaBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                msaaBarrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                msaaBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                msaaBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                msaaBarrier.image               = msaaColorImage_;
                msaaBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
                msaaBarrier.srcAccessMask       = 0;
                msaaBarrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
                vkCmdPipelineBarrier(msaaInitCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &msaaBarrier);
                owner_->EndOneTimeCommands(msaaInitCb);
            }

            appliedMultiSampleCount_ = SampleCountToInt(owner_->sampleCount_);
        }

        // --- 6 framebuffers (one per face, sharing the depth view) -- MSAA variant (att0=MSAA
        // color, att1=resolve into this face's own view, att2=MSAA depth if hasDepth) when this
        // cube engages MSAA, else the plain variant, mirroring VulkanRenderTargetRenderer's
        // mutually-exclusive framebuffer_/msaaFramebuffer_ pattern exactly. Task 911: render pass
        // now selected/lazily-created per this instance's own real depthVkFormat_. ---
        for (int face = 0; face < 6; ++face) {
            if (wantsMsaa)
            {
                // REMED-GFX-141: att0 is THIS face's own multisample layer view, not a single
                // shared one, and the pass is picked by usage exactly as the non-MSAA branch below
                // already picks its own -- so a preserving face loads its own samples and resolves
                // them into its own faceViews_[face] layer.
                VkImageView atts[] = { msaaColorViews_[face], faceViews_[face], depthView_ };
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass      = owner_->GetOrCreateRTRenderPassMsaa(depthVkFormat_,
                                                                             !preserveContents_);
                fbInfo.attachmentCount = hasDepth ? 3u : 2u;
                fbInfo.pAttachments    = atts;
                fbInfo.width           = us;
                fbInfo.height          = us;
                fbInfo.layers          = 1;
                if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &msaaFramebuffers_[face]) != VK_SUCCESS)
                    throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateFramebuffer (MSAA) failed");

                facePasses_[face]->framebuffer = msaaFramebuffers_[face];
                facePasses_[face]->renderPass  = owner_->GetOrCreateRTRenderPassMsaa(
                    depthVkFormat_, !preserveContents_);
                facePasses_[face]->msaa        = true;
            }
            else
            {
                VkImageView atts[] = { faceViews_[face], depthView_ };
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                // REMED-GFX-136: this class now HAS a preserveContents concept, because
                // IGraphicsRenderer::CreateRenderTargetCube finally carries one -- so pick the same
                // way VulkanRenderTargetRenderer's own framebuffer does. Pipelines only ever need a
                // reference render pass built against the discard variant (see
                // GetOrCreateRTRenderPass()'s own comment: discard/load differ only in
                // loadOp/initialLayout, neither of which affects render-pass compatibility), so
                // this adds at most one more cached render pass per distinct depth format, never a
                // per-target or per-face one.
                fbInfo.renderPass      = owner_->GetOrCreateRTRenderPass(depthVkFormat_, !preserveContents_);
                fbInfo.attachmentCount = hasDepth ? 2u : 1u;
                fbInfo.pAttachments    = atts;
                fbInfo.width           = us;
                fbInfo.height          = us;
                fbInfo.layers          = 1;
                if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffers_[face]) != VK_SUCCESS)
                    throw std::runtime_error("VulkanRenderTargetCubeRenderer: vkCreateFramebuffer failed");

                facePasses_[face]->framebuffer = framebuffers_[face];
                facePasses_[face]->renderPass  = owner_->GetOrCreateRTRenderPass(depthVkFormat_,
                                                                                 !preserveContents_);
                facePasses_[face]->msaa        = false;
            }

            facePasses_[face]->width       = size;
            facePasses_[face]->height      = size;
            facePasses_[face]->mipImage    = image_;
            facePasses_[face]->mipLevels   = levelCount_;
            facePasses_[face]->mipLayer    = static_cast<uint32_t>(face);
            facePasses_[face]->depthFormat = depthVkFormat_;
            // REMED-GFX-129: the face needs the same usage the render pass above was picked with,
            // so it can report whether its colour attachment is cleared or loaded on entry.
            facePasses_[face]->loadOpIsClear = !preserveContents_;
            // REMED-GFX-142: all six faces of one cube share depthImage_, so they are ONE
            // depth/stencil group, keyed on the cube's own colour image exactly as before.
            facePasses_[face]->depthGroup  = static_cast<const void*>(image_);
        }

        owner_->liveRenderTargetCubes_.push_back(this);   // VULKAN-407
    }

    VulkanRenderTargetCubeRenderer::~VulkanRenderTargetCubeRenderer()
    {
        if (owner_) {
            // REMED-GFX-166: queued work into this cube's faces is NOT dropped -- each face's pass
            // is co-owned by the commands that render into it, so it outlives this wrapper and its
            // producer still runs for a consumer that samples the cube. Same reasoning as the 2D
            // render target destructor; only currentRT_ still has to let go.
            for (auto& fp : facePasses_)
                if (owner_->currentRT_ == fp) {
                    owner_->BeginRenderPassSegmentEXT(nullptr);
                    break;
                }
            owner_->TraceTargetDisposalEXT("rtcube", this,
                                           facePasses_[0] ? facePasses_[0].get() : nullptr,
                                           image_, cubeView_, framebuffers_[0]);
            auto& list = owner_->liveRenderTargetCubes_;   // VULKAN-407
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }
        ReleaseVulkanResources();
    }

    // VULKAN-407: the release half. The mechanism is unchanged -- everything still goes through
    // the frame-fence-gated retirement queue -- only who may call it, and when.
    void VulkanRenderTargetCubeRenderer::ReleaseVulkanResources()
    {
        if (!owner_ || owner_->device_ == VK_NULL_HANDLE) return;
        // REMED-GFX-075: a RenderTargetCube used as a sampled SOURCE (its cubeView_ baked into a
        // deferred draw's descriptor set) destroyed before Present must keep that view alive until
        // the record consumes the draw. Retire all handles (frame-fence-gated free) rather than a
        // device stall + immediate destroy.
        VulkanRenderer::RetiredResources r;
        // REMED-GFX-076: a RenderTargetCube sampled as a SOURCE bakes its cubeView_ into
        // EnvironmentMapEffect's hash-keyed envMapDescSets_ (and the plain texSamplerDescSets_);
        // evict those entries so a later resource reusing the freed handle value cannot alias them.
        owner_->EvictSampledViewFromCaches(cubeView_, r);
        for (int i = 0; i < 6; ++i) {
            if (framebuffers_[i]     != VK_NULL_HANDLE) { r.framebuffers.push_back(framebuffers_[i]);     framebuffers_[i]     = VK_NULL_HANDLE; }
            if (msaaFramebuffers_[i] != VK_NULL_HANDLE) { r.framebuffers.push_back(msaaFramebuffers_[i]); msaaFramebuffers_[i] = VK_NULL_HANDLE; }
            if (faceViews_[i]        != VK_NULL_HANDLE) { r.imageViews.push_back(faceViews_[i]);          faceViews_[i]        = VK_NULL_HANDLE; }
            // REMED-GFX-141: six per-face multisample views now, where there used to be one.
            if (msaaColorViews_[i]   != VK_NULL_HANDLE) { r.imageViews.push_back(msaaColorViews_[i]);     msaaColorViews_[i]   = VK_NULL_HANDLE; }
        }
        if (cubeView_    != VK_NULL_HANDLE) { r.imageViews.push_back(cubeView_);   cubeView_    = VK_NULL_HANDLE; }
        if (depthView_   != VK_NULL_HANDLE) { r.imageViews.push_back(depthView_);  depthView_   = VK_NULL_HANDLE; }
        if (depthImage_  != VK_NULL_HANDLE) { r.images.push_back(depthImage_);     depthImage_  = VK_NULL_HANDLE; }
        if (depthMemory_ != VK_NULL_HANDLE) { r.memories.push_back(depthMemory_);  depthMemory_ = VK_NULL_HANDLE; }
        if (msaaColorImage_  != VK_NULL_HANDLE) { r.images.push_back(msaaColorImage_);     msaaColorImage_  = VK_NULL_HANDLE; }
        if (msaaColorMemory_ != VK_NULL_HANDLE) { r.memories.push_back(msaaColorMemory_);  msaaColorMemory_ = VK_NULL_HANDLE; }
        if (image_       != VK_NULL_HANDLE) { r.images.push_back(image_);          image_       = VK_NULL_HANDLE; }
        if (memory_      != VK_NULL_HANDLE) { r.memories.push_back(memory_);        memory_      = VK_NULL_HANDLE; }
        owner_->RetireResources(std::move(r));
    }

    int VulkanRenderTargetRenderer::GetAppliedDepthStencilFormatEXT(int requested) const
    {
        // plan_vulkan.md VULKAN-348: what this target really has, not what was asked for.
        (void)requested;
        return XnaDepthFormatFromVkFormatEXT(depthVkFormat_);
    }

    int VulkanRenderTargetCubeRenderer::GetAppliedDepthStencilFormatEXT(int requested) const
    {
        // plan_vulkan.md VULKAN-215: what this cube really has, not what was asked for.
        (void)requested;
        return XnaDepthFormatFromVkFormatEXT(depthVkFormat_);
    }

    void VulkanRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        if (owner_ && face >= 0 && face < 6)
            owner_->BeginRenderPassSegmentEXT(facePasses_[static_cast<std::size_t>(face)]);
    }

    void VulkanRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (owner_) {
            for (auto& fp : facePasses_)
                if (owner_->currentRT_ == fp) {
                    owner_->BeginRenderPassSegmentEXT(nullptr);
                    return;
                }
        }
    }

    bool VulkanRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from ITextureCubeRenderer's
        // `return false` default. Same staging-copy mechanism as VulkanTextureCubeRenderer::GetData.
        if (!owner_ || image_ == VK_NULL_HANDLE || !data || dataLength <= 0) return false;
        if (face < 0 || face >= 6) return false;
        if (level < 0 || level >= levelCount_ || w <= 0 || h <= 0) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const std::size_t regionBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (static_cast<std::size_t>(dataLength) < regionBytes) return false;

        // REMED-GFX-074/GFX-194 readback flush, keyed by this face's exact immutable pass and the
        // requested level. A direct or MRT face producer still queued for Present must be recorded
        // BEFORE the copy. A no-op when no matching producer binding cycle remains pending.
        VulkanTargetPassEXT* const facePass =
            facePasses_[static_cast<std::size_t>(face)].get();
        owner_->FlushDeferredRenderTarget(facePass, facePass, level);

        VkDevice dev = owner_->device_;
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void*          mapped     = nullptr;
        owner_->CreateBuffer(static_cast<VkDeviceSize>(regionBytes),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem, &mapped);
        if (mapped == nullptr)
        {
            if (stagingBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stagingBuf, nullptr);
            if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }

        // Every level of every layer sits in SHADER_READ_ONLY_OPTIMAL outside a render pass (the
        // constructor's up-front barrier, the RT render pass's finalLayout, and MaybeGenerateMips'
        // own restore all agree on that), so only this face layer's requested level moves.
        VkCommandBuffer cb = owner_->BeginOneTimeCommands();
        VkImageMemoryBarrier toXfer{};
        toXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toXfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toXfer.image               = image_;
        toXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        static_cast<uint32_t>(level), 1,
                                        static_cast<uint32_t>(face), 1 };
        toXfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toXfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toXfer);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT,
                                     static_cast<uint32_t>(level),
                                     static_cast<uint32_t>(face), 1 };
        region.imageOffset      = { x, y, 0 };
        region.imageExtent      = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyImageToBuffer(cb, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuf, 1, &region);

        VkImageMemoryBarrier toRead = toXfer;
        std::swap(toRead.oldLayout, toRead.newLayout);
        std::swap(toRead.srcAccessMask, toRead.dstAccessMask);
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);

        owner_->EndOneTimeCommands(cb);

        // A cube RENDER TARGET carries the swapchain format (see the constructor), not a plain
        // TextureCube's fixed RGBA8 -- the same correction VulkanRenderTargetRenderer::GetData makes.
        const bool isBGRA = (owner_->swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM ||
                             owner_->swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB);
        auto*       dst = static_cast<uint8_t*>(data);
        const auto* src = static_cast<const uint8_t*>(mapped);
        for (std::size_t i = 0; i < regionBytes / 4u; ++i) {
            const std::size_t o = i * 4u;
            if (isBGRA) { dst[o+0] = src[o+2]; dst[o+1] = src[o+1]; dst[o+2] = src[o+0]; dst[o+3] = src[o+3]; }
            else        { dst[o+0] = src[o+0]; dst[o+1] = src[o+1]; dst[o+2] = src[o+2]; dst[o+3] = src[o+3]; }
        }

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return true;
    }


} // namespace CNA::Internal::Renderers::Vulkan

// =========================================================================
// Factory
// =========================================================================
namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_VULKAN
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Vulkan { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Vulkan::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Vulkan::VulkanRenderer>(args);
    }
#endif
}
