#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUMetalSurface.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"  // WEBGPU-142: Effect::GetEffectRendererPtr()
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"  // WEBGPU-144: BC format classify
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

        constexpr std::uint64_t kMinimumBufferSize = 4;
        constexpr std::uint64_t kRequestTimeoutNanoseconds = 10'000'000'000ULL;

        // The callback-completion mode this renderer requests for every async wgpu* operation.
        // Native wgpu-native drives completion by pumping wgpuInstanceProcessEvents() from
        // WaitForCompletion(); a browser has no such pump, so under Emscripten the callbacks fire
        // spontaneously from the JavaScript event loop while WaitForCompletion() yields to it.
#if defined(__EMSCRIPTEN__)
        constexpr WGPUCallbackMode kCnaWebGpuCallbackMode = WGPUCallbackMode_AllowSpontaneous;
#else
        constexpr WGPUCallbackMode kCnaWebGpuCallbackMode = WGPUCallbackMode_AllowProcessEvents;
#endif

        [[nodiscard]] WGPUStringView StringView(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? WGPU_STRLEN : 0};
        }

        [[nodiscard]] std::string ToString(WGPUStringView text)
        {
            if (text.data == nullptr)
                return {};
            if (text.length == WGPU_STRLEN)
                return std::string(text.data);
            return std::string(text.data, text.length);
        }

        [[nodiscard]] std::uint64_t Align4(std::uint64_t value)
        {
            return std::max(kMinimumBufferSize, (value + 3u) & ~std::uint64_t{3u});
        }

        // WEBGPU-144: resolves an XNA SurfaceFormat ordinal to the WGPU texture format used to store
        // it. Block-compressed formats (DXT1/3/5, BC7, and their sRGB variants) map to the matching
        // WGPUTextureFormat_BC* and are uploaded as raw 4x4 blocks (blockBytes bytes each); every
        // other format is treated as the renderer's plain RGBA8Unorm.
        struct WebGPUTexFormatInfo
        {
            WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
            bool compressed = false;
            int blockBytes = 4;  ///< bytes per 4x4 block (compressed) / per texel (RGBA8)
        };

        [[nodiscard]] WebGPUTexFormatInfo ClassifyWebGPUTextureFormat(int surfaceFormat)
        {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Dxt1:        return {WGPUTextureFormat_BC1RGBAUnorm, true, 8};
            case SurfaceFormat::Dxt3:        return {WGPUTextureFormat_BC2RGBAUnorm, true, 16};
            case SurfaceFormat::Dxt5:        return {WGPUTextureFormat_BC3RGBAUnorm, true, 16};
            case SurfaceFormat::Dxt5SrgbEXT: return {WGPUTextureFormat_BC3RGBAUnormSrgb, true, 16};
            case SurfaceFormat::Bc7EXT:      return {WGPUTextureFormat_BC7RGBAUnorm, true, 16};
            case SurfaceFormat::Bc7SrgbEXT:  return {WGPUTextureFormat_BC7RGBAUnormSrgb, true, 16};
            default:                         return {WGPUTextureFormat_RGBA8Unorm, false, 4};
            }
        }

        [[nodiscard]] WGPUBuffer CreateAndBindDeferredIndexBuffer(
            WGPUDevice device,
            WGPUQueue queue,
            WGPURenderPassEncoder pass,
            const char* label,
            const std::vector<std::uint8_t>& logicalData,
            bool index32)
        {
            const std::uint64_t logicalBytes =
                static_cast<std::uint64_t>(logicalData.size());
            if (logicalBytes > std::numeric_limits<std::uint64_t>::max() - 3u)
                throw std::out_of_range(
                    "CNA WebGPU: deferred index-buffer byte count overflow");
            const std::uint64_t nativeBytes = Align4(logicalBytes);

            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView(label);
            descriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            descriptor.size = nativeBytes;
            WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);

            if (nativeBytes == logicalBytes)
            {
                wgpuQueueWriteBuffer(
                    queue, buffer, 0, logicalData.data(), logicalData.size());
            }
            else
            {
                // wgpuQueueWriteBuffer requires a four-byte copy size. Preserve the command's
                // exact logical index snapshot and binding range, padding only this native write.
                // The initialized zero tail is never included in DrawIndexed's logical count.
                std::vector<std::uint8_t> nativeData(logicalData);
                nativeData.resize(static_cast<std::size_t>(nativeBytes), 0);
                wgpuQueueWriteBuffer(
                    queue, buffer, 0, nativeData.data(), nativeData.size());
            }

            wgpuRenderPassEncoderSetIndexBuffer(
                pass,
                buffer,
                index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                0,
                logicalBytes);
            return buffer;
        }

        // REMED-GFX-105: WebGPU enables primitive restart for indexed strip pipelines through
        // WGPUPrimitiveState::stripIndexFormat. It must match the format passed to
        // SetIndexBuffer for indexed line/triangle strips, and must remain Undefined for
        // non-indexed strips and every list/point topology. Both inputs come from the same
        // deferred command snapshot used by CreateAndBindDeferredIndexBuffer(), never from a
        // live IndexBuffer object or a renderer-global "last format".
        [[nodiscard]] WGPUIndexFormat RequiredStripIndexFormat(
            WGPUPrimitiveTopology topology,
            bool indexed,
            bool index32)
        {
            if (!indexed ||
                (topology != WGPUPrimitiveTopology_LineStrip &&
                 topology != WGPUPrimitiveTopology_TriangleStrip))
            {
                return WGPUIndexFormat_Undefined;
            }
            return index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16;
        }

        template<typename TCommand>
        [[nodiscard]] WGPUIndexFormat RequiredStripIndexFormat(const TCommand& command)
        {
            return RequiredStripIndexFormat(
                command.topology,
                command.indexed && !command.indexData.empty(),
                command.index32);
        }

        [[nodiscard]] bool HasPresentMode(const WGPUSurfaceCapabilities& capabilities, WGPUPresentMode mode)
        {
            for (std::size_t i = 0; i < capabilities.presentModeCount; ++i)
            {
                if (capabilities.presentModes[i] == mode)
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasSurfaceFormat(const WGPUSurfaceCapabilities& capabilities, WGPUTextureFormat format)
        {
            for (std::size_t i = 0; i < capabilities.formatCount; ++i)
            {
                if (capabilities.formats[i] == format)
                    return true;
            }
            return false;
        }

        /**
         * @brief The non-sRGB counterpart of an 8-bit colour format, or the format unchanged.
         *
         * REMED-GFX-131: CNA's SurfaceFormat::Color is a plain UNORM byte format, so every colour
         * attachment this renderer renders into must use the linear variant. Channel order is
         * deliberately preserved -- only the transfer function is dropped -- so the readback
         * swizzle that keys on BGRA-ness stays correct either way.
         */
        [[nodiscard]] constexpr WGPUTextureFormat NonSrgbColorFormat(WGPUTextureFormat format)
        {
            switch (format)
            {
                case WGPUTextureFormat_BGRA8UnormSrgb: return WGPUTextureFormat_BGRA8Unorm;
                case WGPUTextureFormat_RGBA8UnormSrgb: return WGPUTextureFormat_RGBA8Unorm;
                default:                               return format;
            }
        }

        struct AdapterRequestState
        {
            WGPUAdapter adapter = nullptr;
            std::string error;
            bool completed = false;
        };

        void OnAdapterRequest(WGPURequestAdapterStatus status,
                              WGPUAdapter adapter,
                              WGPUStringView message,
                              void* userdata1,
                              void*)
        {
            auto& state = *static_cast<AdapterRequestState*>(userdata1);
            if (status == WGPURequestAdapterStatus_Success)
                state.adapter = adapter;
            else
                state.error = ToString(message);
            state.completed = true;
        }

        struct DeviceRequestState
        {
            WGPUDevice device = nullptr;
            std::string error;
            bool completed = false;
        };

        void OnDeviceRequest(WGPURequestDeviceStatus status,
                             WGPUDevice device,
                             WGPUStringView message,
                             void* userdata1,
                             void*)
        {
            auto& state = *static_cast<DeviceRequestState*>(userdata1);
            if (status == WGPURequestDeviceStatus_Success)
                state.device = device;
            else
                state.error = ToString(message);
            state.completed = true;
        }

        void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message,
                               void* userdata1, void*)
        {
            if (userdata1 != nullptr)
                static_cast<std::atomic<std::size_t>*>(userdata1)->fetch_add(1);
            std::cerr << "CNA WebGPU uncaptured error (" << static_cast<int>(type) << "): "
                      << ToString(message) << '\n';
        }

        void OnDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
        {
            std::cerr << "CNA WebGPU device lost (" << static_cast<int>(reason) << "): "
                      << ToString(message) << '\n';
        }

        struct BufferMapState
        {
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            std::string error;
            bool completed = false;
        };

        void OnBufferMap(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*)
        {
            auto& state = *static_cast<BufferMapState*>(userdata1);
            state.status = status;
            if (status != WGPUMapAsyncStatus_Success)
                state.error = ToString(message);
            state.completed = true;
        }

        // WEBGPU-58: Supports4xMsaa()'s own error-scope round trip -- captures whether the scoped
        // wgpuDeviceCreateTexture() call (a scratch sampleCount=4 probe texture) actually succeeded
        // on THIS concrete adapter/device, rather than assuming the WebGPU spec's "count must be 1
        // or 4" text is honored by every real implementation.
        struct ErrorScopeState
        {
            bool completed = false;
            bool ok = false;
        };

        void OnPopErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView,
                             void* userdata1, void*)
        {
            auto& state = *static_cast<ErrorScopeState*>(userdata1);
            state.ok = (status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
            state.completed = true;
        }

        // See IWebGPUSamplable's own doc comment (WebGPURenderer.hpp): resolves any
        // ITextureRenderer* to its WGPU-sampleable view, safely degrading to nullptr (treated as
        // "unbound" by every Render*Draws() call site's existing null check) for a null input or
        // a texture from an incompatible concrete type, instead of the unchecked static_cast this
        // replaces everywhere.
        // REMED-GFX-167: resolves to a VALUE, not a pointer, and does it here at the public draw
        // call while the resource is unambiguously alive. A queued command therefore never
        // dereferences a wrapper at replay time, and carries the native reference that keeps its
        // view valid even when that wrapper is gone by then.
        [[nodiscard]] WebGPUSampledTextureEXT ResolveSamplable(const ITextureRenderer* tex)
        {
            const auto* samplable = tex != nullptr ? dynamic_cast<const IWebGPUSamplable*>(tex) : nullptr;
            return samplable != nullptr ? samplable->Sampled() : WebGPUSampledTextureEXT{};
        }

        // WEBGPU-114: the IWebGPUCubeSamplable sibling of ResolveSamplable() above -- resolves any
        // ITextureCubeRenderer* (WebGPUTextureCubeRenderer OR WebGPURenderTargetCubeRenderer) to its
        // whole-cube sampling view, safely degrading to nullptr for a null input or an incompatible
        // concrete type.
        [[nodiscard]] WebGPUSampledTextureEXT ResolveCubeSamplable(const ITextureCubeRenderer* tex)
        {
            const auto* samplable = tex != nullptr ? dynamic_cast<const IWebGPUCubeSamplable*>(tex) : nullptr;
            return samplable != nullptr ? samplable->SampledCube() : WebGPUSampledTextureEXT{};
        }

        [[nodiscard]] std::uint32_t AlignBytesPerRow(std::uint32_t bytesPerRow)
        {
            constexpr std::uint32_t kAlignment = 256;
            return (bytesPerRow + kAlignment - 1) / kAlignment * kAlignment;
        }

        // WEBGPU-51/57/112/113: mip-level dimension helper shared by every GetData()/texture
        // constructor below that needs a specific level's real width/height/depth -- mirrors
        // Texture2D.cpp's own file-local mipDim(base, level) formula exactly (max(1, base>>level)).
        [[nodiscard]] int MipDim(int base, int level)
        {
            return std::max(1, base >> level);
        }

        // XNA CompareFunction ordinals -> WGPUCompareFunction (mirrors Vulkan's own ToVkCompareOp):
        // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
        [[nodiscard]] WGPUCompareFunction ToWGPUCompareFunction(int xnaCompare)
        {
            switch (xnaCompare)
            {
                case 1: return WGPUCompareFunction_Never;
                case 2: return WGPUCompareFunction_Less;
                case 3: return WGPUCompareFunction_LessEqual;
                case 4: return WGPUCompareFunction_Equal;
                case 5: return WGPUCompareFunction_GreaterEqual;
                case 6: return WGPUCompareFunction_Greater;
                case 7: return WGPUCompareFunction_NotEqual;
                default: return WGPUCompareFunction_Always;
            }
        }

        // WEBGPU-83: XNA StencilOperation ordinal -> WGPU. Increment/Decrement (unsaturated) wrap;
        // the *Saturation variants clamp -- matching the Vulkan renderer's own mapping.
        [[nodiscard]] WGPUStencilOperation ToWGPUStencilOperation(int xnaOp)
        {
            switch (xnaOp)
            {
                case 1: return WGPUStencilOperation_Zero;
                case 2: return WGPUStencilOperation_Replace;
                case 3: return WGPUStencilOperation_IncrementWrap;
                case 4: return WGPUStencilOperation_DecrementWrap;
                case 5: return WGPUStencilOperation_IncrementClamp;
                case 6: return WGPUStencilOperation_DecrementClamp;
                case 7: return WGPUStencilOperation_Invert;
                default: return WGPUStencilOperation_Keep;  // 0 = Keep
            }
        }

        // WEBGPU-83: bakes the XNA stencil state into a WGPUDepthStencilState's stencilFront/back +
        // masks. When disabled, leaves the INIT defaults (a stencil test that always passes and
        // never writes). Two-sided maps XNA's ccw* ops onto the back face (WebGPU's frontFace is CCW,
        // so this follows the spec-literal front=CW/back=CCW split; a two-sided differential test is
        // needed to confirm the winding on real hardware -- documented, and unused by the non-two-
        // sided path the RenderTarget_DepthStencilUsage acceptance test exercises).
        void FillWGPUStencilState(WGPUDepthStencilState& ds, const WebGPURenderer::StencilKeyParams& s)
        {
            if (!s.enable)
                return;
            WGPUStencilFaceState front{};
            front.compare = ToWGPUCompareFunction(s.func);
            front.failOp = ToWGPUStencilOperation(s.stencilFail);
            front.depthFailOp = ToWGPUStencilOperation(s.depthFail);
            front.passOp = ToWGPUStencilOperation(s.stencilPass);
            ds.stencilFront = front;
            if (s.twoSided)
            {
                WGPUStencilFaceState back{};
                back.compare = ToWGPUCompareFunction(s.ccwFunc);
                back.failOp = ToWGPUStencilOperation(s.ccwFail);
                back.depthFailOp = ToWGPUStencilOperation(s.ccwDepthFail);
                back.passOp = ToWGPUStencilOperation(s.ccwPass);
                ds.stencilBack = back;
            }
            else
            {
                ds.stencilBack = front;
            }
            ds.stencilReadMask = static_cast<std::uint32_t>(s.readMask & 0xFF);
            ds.stencilWriteMask = static_cast<std::uint32_t>(s.writeMask & 0xFF);
        }

        // WEBGPU-83: a stencil state's contribution to the pipeline cache key. A disabled stencil
        // folds a single constant (0) regardless of its stored-but-unused func/op fields, so
        // different disabled DepthStencilStates never fragment the cache.
        [[nodiscard]] std::uint64_t HashStencilState(const WebGPURenderer::StencilKeyParams& s)
        {
            if (!s.enable)
                return 0;
            std::uint64_t h = 1;
            auto mix = [&h](std::uint64_t v) { h = h * 31u + v; };
            mix(static_cast<std::uint64_t>(s.func));
            mix(static_cast<std::uint64_t>(s.stencilPass));
            mix(static_cast<std::uint64_t>(s.stencilFail));
            mix(static_cast<std::uint64_t>(s.depthFail));
            mix(static_cast<std::uint64_t>(s.readMask & 0xFF));
            mix(static_cast<std::uint64_t>(s.writeMask & 0xFF));
            mix(s.twoSided ? 1u : 0u);
            if (s.twoSided)
            {
                mix(static_cast<std::uint64_t>(s.ccwFunc));
                mix(static_cast<std::uint64_t>(s.ccwPass));
                mix(static_cast<std::uint64_t>(s.ccwFail));
                mix(static_cast<std::uint64_t>(s.ccwDepthFail));
            }
            return h;
        }

        [[nodiscard]] WGPUAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return WGPUAddressMode_Repeat;
                case 2: return WGPUAddressMode_MirrorRepeat;
                default: return WGPUAddressMode_ClampToEdge;
            }
        }

        // REMED-GFX-170: the public TextureFilter ordinal's own name, for CNA_WEBGPU_SAMPLER_TRACE.
        // Anything outside the enum is reported as such rather than silently printed as a number,
        // because an out-of-range ordinal is exactly the input the pre-fix expression turned into
        // Point without saying so.
        [[nodiscard]] const char* TextureFilterName(int filter)
        {
            switch (filter)
            {
                case 0: return "Linear";
                case 1: return "Point";
                case 2: return "Anisotropic";
                case 3: return "LinearMipPoint";
                case 4: return "PointMipLinear";
                case 5: return "MinLinearMagPointMipLinear";
                case 6: return "MinLinearMagPointMipPoint";
                case 7: return "MinPointMagLinearMipLinear";
                case 8: return "MinPointMagLinearMipPoint";
                default: return "<out-of-range>";
            }
        }

        [[nodiscard]] const char* FilterModeName(WGPUFilterMode mode)
        {
            return mode == WGPUFilterMode_Linear ? "Linear" : "Nearest";
        }

        [[nodiscard]] const char* MipmapFilterModeName(WGPUMipmapFilterMode mode)
        {
            return mode == WGPUMipmapFilterMode_Linear ? "Linear" : "Nearest";
        }

        [[nodiscard]] const char* AddressModeName(WGPUAddressMode mode)
        {
            switch (mode)
            {
                case WGPUAddressMode_Repeat: return "Repeat";
                case WGPUAddressMode_MirrorRepeat: return "MirrorRepeat";
                default: return "ClampToEdge";
            }
        }

        [[nodiscard]] bool SamplerTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_WEBGPU_SAMPLER_TRACE") != nullptr;
            return enabled;
        }

        /// REMED-GFX-172: the multi-texture families' own binding trace. Separate from
        /// SamplerTraceEnabled() above because that one reports the public->native translation of
        /// ONE sampler state (REMED-GFX-170's question) and this one reports which native sampler
        /// each SAMPLED RESOURCE of a two-texture draw was actually bound with -- the question a
        /// shared sampler makes unanswerable from filter ordinals alone.
        [[nodiscard]] bool MultiTextureSamplerTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_WEBGPU_MULTITEX_SAMPLER_TRACE") != nullptr;
            return enabled;
        }

        /**
         * @brief Emits one row of the REMED-GFX-172 multi-texture sampler binding trace.
         *
         * Reports, for a single two-texture draw, both sampled resources' view identities, both
         * CAPTURED public sampler states, both NATIVE sampler handles, the layout/pipeline-layout
         * identities the bind group was built against, and the bind group itself, alongside the
         * draw's public enqueue position and its replay position. A row whose two native sampler
         * handles are equal while its two captured states differ IS the defect, readable directly
         * rather than inferred from pixels.
         *
         * @param family        Draw family name, e.g. "DualTexture3D".
         * @param publicOrder   The draw's position in the public call stream.
         * @param replayPos     The draw's position within the replayed segment.
         * @param view0         Native texture view bound for public sampler slot 0.
         * @param view1         Native texture view bound for public sampler slot 1.
         * @param filter0       Captured slot-0 TextureFilter ordinal.
         * @param addrU0        Captured slot-0 AddressU.
         * @param addrV0        Captured slot-0 AddressV.
         * @param aniso0        Captured slot-0 MaxAnisotropy.
         * @param filter1       Captured slot-1 TextureFilter ordinal.
         * @param addrU1        Captured slot-1 AddressU.
         * @param addrV1        Captured slot-1 AddressV.
         * @param aniso1        Captured slot-1 MaxAnisotropy.
         * @param bound0        Native sampler actually BOUND for slot 0's resource.
         * @param bound1        Native sampler actually BOUND for slot 1's resource.
         * @param slot1Resolved The native sampler slot 1's own captured description resolves to.
         *                      Equal to @p bound1 exactly when slot 1 is honoured; a row where it
         *                      differs is a draw whose second resource is filtered by a sampler its
         *                      public slot never asked for.
         * @param layout        The group-1 bind group layout.
         * @param pipeLayout    The pipeline layout the draw's pipeline was built from.
         * @param bindGroup     The bind group created for this draw.
         * @param entryCount    Number of entries written into that bind group.
         */
        void TraceMultiTextureBinding(const char* family, std::uint32_t publicOrder,
                                      std::size_t replayPos,
                                      WGPUTextureView view0, WGPUTextureView view1,
                                      int filter0, int addrU0, int addrV0, int aniso0,
                                      int filter1, int addrU1, int addrV1, int aniso1,
                                      WGPUSampler bound0, WGPUSampler bound1,
                                      WGPUSampler slot1Resolved,
                                      WGPUBindGroupLayout layout, WGPUPipelineLayout pipeLayout,
                                      WGPUBindGroup bindGroup, std::size_t entryCount)
        {
            // The bind group is built fresh for every draw and released with the frame, so there is
            // no bind-group cache and therefore no cache key: `bgcache=none` records that measured
            // absence explicitly instead of implying one exists.
            const bool statesDiffer = filter0 != filter1 || addrU0 != addrU1 ||
                                      addrV0 != addrV1 || aniso0 != aniso1;
            std::fprintf(stderr,
                         "[cna-wgpu-multitex] family=%s publicOrder=%u replayPos=%zu "
                         "view0=%p view1=%p "
                         "captured0={filter=%d(%s) addrU=%d addrV=%d aniso=%d} "
                         "captured1={filter=%d(%s) addrU=%d addrV=%d aniso=%d} statesDiffer=%s "
                         "bound0=%p bound1=%p slot1Resolved=%p sharedSampler=%s slotMismatch=%s "
                         "layout=%p pipeLayout=%p bgcache=none bindGroup=%p entries=%zu\n",
                         family, publicOrder, replayPos,
                         static_cast<void*>(view0), static_cast<void*>(view1),
                         filter0, TextureFilterName(filter0), addrU0, addrV0, aniso0,
                         filter1, TextureFilterName(filter1), addrU1, addrV1, aniso1,
                         statesDiffer ? "YES" : "no",
                         static_cast<void*>(bound0), static_cast<void*>(bound1),
                         static_cast<void*>(slot1Resolved),
                         (bound0 == bound1) ? "YES" : "no",
                         (bound1 == slot1Resolved) ? "no" : "YES",
                         static_cast<void*>(layout), static_cast<void*>(pipeLayout),
                         static_cast<void*>(bindGroup), entryCount);
        }

        // REMED-GFX-170: XNA's SamplerState.MaxAnisotropy default. ISpriteBatchRenderer carries the
        // filter ordinal and the two address modes and nothing else, so a sprite cannot express a
        // non-default anisotropy on ANY renderer; this is the value SpriteBatch's own
        // SamplerState.LinearClamp/PointClamp/AnisotropicClamp all carry anyway.
        constexpr int kSpriteBatchMaxAnisotropy = 4;

        // ---- WEBGPU-41/77/78/79/80/81/82/83: graphics-state -> WGPU translation helpers ----
        // Shared by every 3D GetOrCreatePipeline*() family so the exact same XNA->WGPU mapping is
        // used everywhere (mirrors ToWGPUCompareFunction()'s own established role/pattern, and
        // VulkanRenderer::ToVkBlendFactor()/ToVkBlendOp()/FillBlendAttachmentState()'s
        // identical purpose on that renderer).

        // XNA Blend enum -> WGPUBlendFactor: One=0, Zero=1, SourceColor=2, InverseSourceColor=3,
        // SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        // DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10, InverseBlendFactor=11,
        // SourceAlphaSaturation=12.
        [[nodiscard]] WGPUBlendFactor ToWGPUBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
                case  1: return WGPUBlendFactor_Zero;
                case  2: return WGPUBlendFactor_Src;
                case  3: return WGPUBlendFactor_OneMinusSrc;
                case  4: return WGPUBlendFactor_SrcAlpha;
                case  5: return WGPUBlendFactor_OneMinusSrcAlpha;
                case  6: return WGPUBlendFactor_Dst;
                case  7: return WGPUBlendFactor_OneMinusDst;
                case  8: return WGPUBlendFactor_DstAlpha;
                case  9: return WGPUBlendFactor_OneMinusDstAlpha;
                case 10: return WGPUBlendFactor_Constant;
                case 11: return WGPUBlendFactor_OneMinusConstant;
                case 12: return WGPUBlendFactor_SrcAlphaSaturated;
                default: return WGPUBlendFactor_One; // Blend::One = 0
            }
        }

        // XNA BlendFunction enum -> WGPUBlendOperation: Add=0, Subtract=1, ReverseSubtract=2,
        // Max=3, Min=4.
        [[nodiscard]] WGPUBlendOperation ToWGPUBlendOperation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
                case 1: return WGPUBlendOperation_Subtract;
                case 2: return WGPUBlendOperation_ReverseSubtract;
                case 3: return WGPUBlendOperation_Max;
                case 4: return WGPUBlendOperation_Min;
                default: return WGPUBlendOperation_Add; // BlendFunction::Add = 0
            }
        }

        // Fills a WGPUBlendState's real blend factors/op from BlendKeyParams. Caller is
        // responsible for only setting target.blend to &result when blending is actually enabled
        // (a disabled WGPUColorTargetState::blend must stay nullptr, not a state with
        // srcFactor=One/dstFactor=Zero -- see every GetOrCreatePipeline*() call site).
        void FillWGPUBlendState(WGPUBlendState& blendState, const WebGPURenderer::BlendKeyParams& bp)
        {
            blendState.color.srcFactor = ToWGPUBlendFactor(bp.colorSrc);
            blendState.color.dstFactor = ToWGPUBlendFactor(bp.colorDst);
            blendState.color.operation = ToWGPUBlendOperation(bp.colorFunc);
            blendState.alpha.srcFactor = ToWGPUBlendFactor(bp.alphaSrc);
            blendState.alpha.dstFactor = ToWGPUBlendFactor(bp.alphaDst);
            blendState.alpha.operation = ToWGPUBlendOperation(bp.alphaFunc);
        }

        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. Every 3D pipeline
        // sets pipeline.primitive.frontFace = WGPUFrontFace_CCW, and WebGPU determines facing from
        // the signed area in FRAMEBUFFER space, whose Y points down -- so this renderer's front face
        // is the counter-clockwise-AS-DISPLAYED one and its back face the clockwise one.
        //
        // REMED-GFX-160: each XNA enum names the face it CULLS, and clockwise-as-displayed is XNA's
        // FRONT face (FNA's SpriteBatch emits TL->TR->BL and BR->BL->TR, both clockwise as
        // displayed, and they must survive the RasterizerState.CullCounterClockwise that
        // SpriteBatch.Begin itself defaults to). So CullClockwiseFace has to remove the clockwise
        // face, which here is WGPUCullMode_Back, and CullCounterClockwiseFace has to remove the
        // counter-clockwise one, which here is WGPUCullMode_Front.
        //
        // This pairing was previously the other way round. The "empirical verification" behind it
        // used a probe quad whose own winding was mis-derived, so it demanded that a BACK face stay
        // visible under XNA's default cull mode and the mapping was inverted to satisfy it -- which
        // is why the defect only ever showed in the stock 3D path (the SpriteBatch pipeline below
        // hardcodes WGPUCullMode_None and so was immune). Now measured against the FNA-derived
        // contract by modules/graphics/examples/frontface_winding_test.cpp on eight renderers.
        [[nodiscard]] WGPUCullMode ToWGPUCullMode(int xnaCullMode)
        {
            switch (xnaCullMode)
            {
                case 1: return WGPUCullMode_Back;   // CullClockwiseFace
                case 2: return WGPUCullMode_Front;  // CullCounterClockwiseFace
                default: return WGPUCullMode_None;
            }
        }

        // WEBGPU-116: the WebGPU spelling of every XNA VertexElementFormat, mirroring
        // VulkanRenderer's own ToVkVertexFormat. Every pipeline builder selects its per-attribute
        // WGPUVertexFormat through this one map, so each attribute names the XNA format it means and
        // the ordinal->format table lives in exactly one place instead of ~55 scattered literals.
        // Color is Unorm8x4, not a BGRA vertex format: WebGPU has no B8G8R8A8 vertex format, and this
        // renderer's packed layouts already store colour as RGBA the shader reads straight -- unlike
        // Vulkan, which picks VK_FORMAT_B8G8R8A8_UNORM to swizzle during the fetch.
        [[nodiscard]] WGPUVertexFormat WebGPUVertexFormatFromVEF(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:  return WGPUVertexFormat_Float32;
                case VertexElementFormat::Vector2: return WGPUVertexFormat_Float32x2;
                case VertexElementFormat::Vector3: return WGPUVertexFormat_Float32x3;
                case VertexElementFormat::Vector4: return WGPUVertexFormat_Float32x4;
                case VertexElementFormat::Color:   return WGPUVertexFormat_Unorm8x4;
                case VertexElementFormat::Byte4:   return WGPUVertexFormat_Uint8x4;
                case VertexElementFormat::Short2:  return WGPUVertexFormat_Sint16x2;
                case VertexElementFormat::Short4:  return WGPUVertexFormat_Sint16x4;
                case VertexElementFormat::NormalizedShort2: return WGPUVertexFormat_Snorm16x2;
                case VertexElementFormat::NormalizedShort4: return WGPUVertexFormat_Snorm16x4;
                case VertexElementFormat::HalfVector2: return WGPUVertexFormat_Float16x2;
                case VertexElementFormat::HalfVector4: return WGPUVertexFormat_Float16x4;
            }
            throw std::invalid_argument(
                "CNA WebGPU: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary. This renderer selects its
        // WGPUVertexBufferLayout from the stride (REMED-GFX-217), so a declaration that layout
        // cannot represent is refused before a pipeline is created or a command queued. The
        // ordinary route leaves an unlisted stride to its own established rejection
        // (DrawColoredPrimitives refuses anything but stride 16); the Instanced3D module is
        // position-only for every stride InstancedPackedColorOffsetForStride below does not list.
        void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route,
                                           bool positionOnlyFallback = false)
        {
            const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
            CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
                webgpuVb.Declaration(), static_cast<int>(webgpuVb.Stride()),
                positionOnlyFallback
                    ? CNA::Internal::Graphics::UnlistedStrideLayout::PositionOnlyFallback
                    : CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
                "WebGPU", route);
        }

        // REMED-GFX-212: whether this per-vertex stride's established packed layout carries a
        // COLOR0 element, and at which byte offset. The ordinary route's own pipelines derive
        // their Unorm8x4 colour attribute from exactly this table -- colored3d.wgsl's stride-16
        // ColoredVertex and the stride-24 colored+textured layout, both at offset 12 -- so reading
        // the same one here is what makes the instanced route's VertexColorEnabled mean what its
        // own ordinary route's already means. Every other stride keeps the position-only
        // Instanced3D module it has always used: 20 (VertexPositionTexture) and 32
        // (VertexPositionNormalTexture) carry no COLOR0 at all, and the skinned/PBR strides carry
        // one the instanced route has no bone palette or tangent basis to render anyway.
        [[nodiscard]] bool InstancedPackedColorOffsetForStride(
            std::size_t pvStride, std::uint64_t& colorOffsetOut)
        {
            switch (pvStride)
            {
                case 16:   // VertexPositionColor
                case 24:   // VertexPositionColorTexture
                    colorOffsetOut = 12;
                    return true;
                default:
                    return false;
            }
        }

        // Encodes (topology, strip-index compatibility, depth test/write/func, blend enable+params,
        // cull mode, wireframe, depth bias, slope-scale depth bias, and a caller-supplied salt for
        // any extra dimension -- e.g. stride for AlphaTest3D/DualTexture3D) into a single uint64_t
        // pipeline cache key.
        // Mirrors VulkanRenderer::Make3DKey()/PackBlendBits()'s own role: when blend is
        // disabled, the (irrelevant) blend factors always collapse to the same bits so different
        // disabled BlendStates don't create duplicate pipelines.
        [[nodiscard]] std::uint64_t Make3DPipelineKey(
            WGPUPrimitiveTopology topology,
            WGPUIndexFormat stripIndexFormat,
            bool depthTest,
            bool depthWrite,
            int depthFunc,
            bool blend,
            const WebGPURenderer::BlendKeyParams& bp,
            int cullMode,
            bool wireframe,
            float depthBias,
            float slopeScaleDepthBias,
            std::uint64_t salt,
            int colorWriteMask,
            std::uint32_t sampleMask,
            int colorAttachmentCount = 1)
        {
            std::uint64_t key = static_cast<std::uint64_t>(topology);
            // REMED-GFX-105: RequiredStripIndexFormat() canonicalizes this to Undefined for
            // list/point topologies and non-indexed strips, so only WebGPU-incompatible strip
            // formats create distinct pipeline variants. Buffer identity never participates.
            key = key * 31u + static_cast<std::uint64_t>(stripIndexFormat);
            key = key * 31u + (depthTest ? 1u : 0u);
            key = key * 31u + (depthWrite ? 1u : 0u);
            key = key * 31u + static_cast<std::uint64_t>(depthFunc);
            key = key * 31u + (blend ? 1u : 0u);
            if (blend)
            {
                key = key * 31u + static_cast<std::uint64_t>(bp.colorSrc);
                key = key * 31u + static_cast<std::uint64_t>(bp.colorDst);
                key = key * 31u + static_cast<std::uint64_t>(bp.alphaSrc);
                key = key * 31u + static_cast<std::uint64_t>(bp.alphaDst);
                key = key * 31u + static_cast<std::uint64_t>(bp.colorFunc);
                key = key * 31u + static_cast<std::uint64_t>(bp.alphaFunc);
            }
            key = key * 31u + static_cast<std::uint64_t>(cullMode);
            key = key * 31u + (wireframe ? 1u : 0u);
            // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias are baked into the pipeline object --
            // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias),
            // so the exact float bits must be part of the cache key, not just approximately
            // bucketed.
            key = key * 1000003u + static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(depthBias));
            key = key * 1000003u + static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(slopeScaleDepthBias));
            key = key * 1000003u + salt;
            // REMED-GFX-077: colour write mask + sample mask are static wgpu-native pipeline state.
            key = key * 31u + static_cast<std::uint64_t>(colorWriteMask & 0xF);
            key = key * 31u + static_cast<std::uint64_t>(sampleMask);
            // WEBGPU-86 MRT: a pipeline built for an N-attachment pass is NOT compatible with a
            // 1-attachment pass (WebGPU rejects the mismatch), so the count must separate them.
            // Folded only when > 1, so every single-target pipeline's key is byte-identical to
            // before -- the instanced/viewport/scissor CARDINALITY tests still see the same variants.
            if (colorAttachmentCount > 1)
                key = key * 31u + static_cast<std::uint64_t>(colorAttachmentCount);
            return key;
        }

        // WEBGPU-82: fills a full WGPUSamplerDescriptor (address modes, mag/min/mipmap filter,
        // anisotropy) from a real XNA SamplerState -- shared translation used by
        // GetOrCreateSlotSampler(), mirrors VulkanRenderer::ApplySamplerState()'s own
        // filter switch verbatim (magFilter/minFilter split per XNA TextureFilter value).
        void FillWGPUSamplerDescriptor(WGPUSamplerDescriptor& descriptor, int filter, int addressU,
                                       int addressV, int maxAnisotropy)
        {
            // XNA TextureFilter: 0=Linear,1=Point,2=Anisotropic,3=LinearMipPoint,4=PointMipLinear,
            // 5=MinLinearMagPointMipLinear,6=MinLinearMagPointMipPoint,
            // 7=MinPointMagLinearMipLinear,8=MinPointMagLinearMipPoint.
            WGPUFilterMode magF = WGPUFilterMode_Linear;
            WGPUFilterMode minF = WGPUFilterMode_Linear;
            WGPUMipmapFilterMode mipMode = WGPUMipmapFilterMode_Linear;
            bool enableAniso = false;
            switch (filter)
            {
                case 1: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Nearest; break;
                case 2: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Linear;  enableAniso = true; break;
                case 3: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Nearest; break;
                case 4: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Linear;  break;
                case 5: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Linear;  break;
                case 6: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Nearest; break;
                case 7: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Linear;  break;
                case 8: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Nearest; break;
                default: break; // Linear (0)
            }
            descriptor.addressModeU = ToAddressMode(addressU);
            descriptor.addressModeV = ToAddressMode(addressV);
            descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
            descriptor.magFilter = magF;
            descriptor.minFilter = minF;
            descriptor.mipmapFilter = mipMode;
            descriptor.lodMaxClamp = 32.0f;
            // WebGPU requires maxAnisotropy==1 unless mag/min/mipmap are all Linear (true only for
            // filter==2 above) -- matches VulkanRenderer::ApplySamplerState()'s identical
            // enableAniso gate.
            descriptor.maxAnisotropy = enableAniso
                ? static_cast<std::uint16_t>(std::clamp(maxAnisotropy, 1, 16))
                : 1;
        }

        // Mirrors VulkanRenderer::DrawColoredPrimitives()'s own use of
        // FillExtPushConst()'s byte layout: this path carries no BasicEffect diffuse/
        // VertexColorEnabled (no GpuDrawParams at all), so it preserves the historical XNA
        // behaviour of outputting the raw vertex colours unmodified (diffuseColor=white,
        // vertexColorEnabled=true), everything else left zeroed.
        void FillColoredUniforms(std::array<float, 32>& out, const Matrix& world, const Matrix& view,
                                 const Matrix& projection)
        {
            const Matrix wvp = world * view * projection;
            wvp.ToColumnMajor(out.data());
            out[16] = 1.0f; out[17] = 1.0f; out[18] = 1.0f; out[19] = 1.0f;
            for (int i = 20; i < 31; ++i) out[i] = 0.0f;
            out[31] = 1.0f;
        }

        // Mirrors VulkanRenderer::FillExtPushConst() field-for-field (real GpuDrawParams
        // this time, not DrawColoredPrimitives()'s hardcoded white/vertex-color-always-true
        // values) -- used by DrawPrimitivesEx()'s stride-16 dispatch so a BasicEffect draw's real
        // DiffuseColor/VertexColorEnabled actually reach the shader.
        void FillExtUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.ambientColor[0]; out[21] = p.ambientColor[1]; out[22] = p.ambientColor[2];
            out[23] = p.lightingEnabled ? 1.0f : 0.0f;
            out[24] = p.light0Dir[0]; out[25] = p.light0Dir[1]; out[26] = p.light0Dir[2];
            out[27] = p.textureEnabled ? 1.0f : 0.0f;
            out[28] = p.light0Diffuse[0]; out[29] = p.light0Diffuse[1]; out[30] = p.light0Diffuse[2];
            out[31] = p.vertexColorEnabled ? 1.0f : 0.0f;
        }

        // Normal matrix = inverse(world3x3), via the cofactor/det shortcut, applied directly to
        // the already-GPU-space (dumped) world matrix -- mirrors
        // BgfxRenderer::ComputeNormalMatrix3x3() byte-for-byte (verified independently
        // against FNA's own Lighting.fxh: HLSL computes WorldInverseTranspose =
        // Transpose(Invert(World)) on the CPU and applies it as mul(normal, WorldInverseTranspose)
        // -- a row-vector multiply. Working through this codebase's established
        // dump(M) = M^T GPU-column-major convention for both World and the result reduces to
        // exactly this cofactor inverse of the dumped array, with no shader-side transpose.
        void ComputeNormalMatrix3x3(const float* w, float out[9])
        {
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            out[0] = (e * i - f * h) * invDet; out[1] = -(b * i - c * h) * invDet; out[2] = (b * f - c * e) * invDet;
            out[3] = -(d * i - f * g) * invDet; out[4] = (a * i - c * g) * invDet; out[5] = -(a * f - c * d) * invDet;
            out[6] = (d * h - e * g) * invDet; out[7] = -(a * h - b * g) * invDet; out[8] = (a * e - b * d) * invDet;
        }

        // Secondary UBO for lit_textured3d.wgsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
        // World (for world-space position), EyePosition, per-light SpecularColor, material
        // SpecularColor/Power, and the 3x3 normal matrix -- forwarded here since the primary
        // 128-byte uniform block (FillExtUniforms) is already fully packed. Mirrors
        // VulkanRenderer's LitLightParams UBO field-for-field (minus fog, deliberately
        // deferred like the other WebGPU 3D shaders).
        void FillLitLightUniforms(std::array<float, 68>& out, const GpuDrawParams& p)
        {
            out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.0f;
            out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.0f;
            out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.0f;
            out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.0f;
            out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2]; out[19] = 0.0f;
            for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi];
            out[36] = p.eyePositionWorld[0]; out[37] = p.eyePositionWorld[1]; out[38] = p.eyePositionWorld[2]; out[39] = 0.0f;
            out[40] = p.light0Specular[0]; out[41] = p.light0Specular[1]; out[42] = p.light0Specular[2]; out[43] = 0.0f;
            out[44] = p.light1Specular[0]; out[45] = p.light1Specular[1]; out[46] = p.light1Specular[2]; out[47] = 0.0f;
            out[48] = p.light2Specular[0]; out[49] = p.light2Specular[1]; out[50] = p.light2Specular[2]; out[51] = 0.0f;
            out[52] = p.specularColor[0]; out[53] = p.specularColor[1]; out[54] = p.specularColor[2]; out[55] = p.specularPower;
            float normalMatrix[9];
            ComputeNormalMatrix3x3(p.worldColMajor, normalMatrix);
            out[56] = normalMatrix[0]; out[57] = normalMatrix[1]; out[58] = normalMatrix[2]; out[59] = 0.0f;
            out[60] = normalMatrix[3]; out[61] = normalMatrix[4]; out[62] = normalMatrix[5]; out[63] = 0.0f;
            out[64] = normalMatrix[6]; out[65] = normalMatrix[7]; out[66] = normalMatrix[8]; out[67] = 0.0f;
        }

        // env_map3d.wgsl's Transform UBO (group 0 binding 0): mvp + world, since WebGPU has no
        // push constants -- mirrors VulkanRenderer::FillEnvMapPushConst() field-for-field.
        void FillEnvMapTransform(std::array<float, 32>& out, const Matrix& mvp, const Matrix& world)
        {
            mvp.ToColumnMajor(out.data());
            world.ToColumnMajor(out.data() + 16);
        }

        // env_map3d.wgsl's EnvMapParams UBO (group 0 binding 1): mirrors
        // VulkanRenderer::DrawPrimitivesEx()'s own envMapUboData[] packing field-for-field
        // (cross-checked against EasyGLRenderer::EnsureEnvMapped3DProgram()'s identical
        // GLSL formula before porting), plus a CPU-precomputed normal matrix at the tail (WGSL has
        // no inverse() -- same reason FillLitLightUniforms() precomputes its own).
        void FillEnvMapParams(std::array<float, 60>& out, const GpuDrawParams& p)
        {
            out[0] = p.eyePositionWorld[0]; out[1] = p.eyePositionWorld[1];
            out[2] = p.eyePositionWorld[2]; out[3] = 0.0f;
            out[4] = p.diffuseColor[0]; out[5] = p.diffuseColor[1];
            out[6] = p.diffuseColor[2]; out[7] = p.diffuseColor[3];
            out[8] = p.emissiveColor[0]; out[9] = p.emissiveColor[1];
            out[10] = p.emissiveColor[2]; out[11] = p.envMapAmount;
            out[12] = p.light0Dir[0]; out[13] = p.light0Dir[1]; out[14] = p.light0Dir[2]; out[15] = 0.0f;
            out[16] = p.light0Diffuse[0]; out[17] = p.light0Diffuse[1]; out[18] = p.light0Diffuse[2];
            out[19] = p.fresnelEnabled ? 1.0f : 0.0f;
            out[20] = p.envMapSpecular[0]; out[21] = p.envMapSpecular[1]; out[22] = p.envMapSpecular[2];
            out[23] = p.fresnelFactor;
            // REMED-GFX-100: the CPU-prepared FNA view-space fog vector is the only fog
            // representation uploaded here. `fogColor.w` remains explicit UBO padding.
            out[24] = p.fogColor[0]; out[25] = p.fogColor[1]; out[26] = p.fogColor[2]; out[27] = 0.0f;
            out[28] = p.fogVector[0]; out[29] = p.fogVector[1];
            out[30] = p.fogVector[2]; out[31] = p.fogVector[3];
            out[32] = p.light1Dir[0]; out[33] = p.light1Dir[1]; out[34] = p.light1Dir[2]; out[35] = 0.0f;
            out[36] = p.light1Diffuse[0]; out[37] = p.light1Diffuse[1]; out[38] = p.light1Diffuse[2]; out[39] = 0.0f;
            out[40] = p.light2Dir[0]; out[41] = p.light2Dir[1]; out[42] = p.light2Dir[2]; out[43] = 0.0f;
            out[44] = p.light2Diffuse[0]; out[45] = p.light2Diffuse[1]; out[46] = p.light2Diffuse[2]; out[47] = 0.0f;
            float normalMatrix[9];
            ComputeNormalMatrix3x3(p.worldColMajor, normalMatrix);
            out[48] = normalMatrix[0]; out[49] = normalMatrix[1]; out[50] = normalMatrix[2]; out[51] = 0.0f;
            out[52] = normalMatrix[3]; out[53] = normalMatrix[4]; out[54] = normalMatrix[5]; out[55] = 0.0f;
            out[56] = normalMatrix[6]; out[57] = normalMatrix[7]; out[58] = normalMatrix[8]; out[59] = 0.0f;
        }

        // Mirrors VulkanRenderer::FillAlphaTestPushConst() field-for-field. AlphaTestEffect
        // has no lighting, so this repurposes the [20..23]/[24] slots (ambient/light0/
        // vertexColorEnabled in FillExtUniforms) for {alphaRef, alphaTolerance, passWeight,
        // failWeight, vertexColorEnabled} instead -- same 128-byte total size, so it still fits
        // the existing coloredBindGroupLayout_ unchanged.
        void FillAlphaTestUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.alphaTest[0]; out[21] = p.alphaTest[1];
            out[22] = p.alphaTest[2]; out[23] = p.alphaTest[3];
            out[24] = p.vertexColorEnabled ? 1.0f : 0.0f;
            for (int i = 25; i < 32; ++i) out[i] = 0.0f;
        }

        // pbr3d.wgsl's third (small) uniform buffer: PBR factors/map scales plus glTF MASK coverage,
        // the only per-draw PBR-specific scalars not already covered by FillExtUniforms()'s
        // diffuseColor/ambientColor or FillLitLightUniforms()'s emissiveColor/world/eyePos.
        // plans/plan_gltf.md GLTF-344: widened from 56 to 76 floats (304 bytes) for KHR_materials_specular's own two
        // inputs -- the UNCLAMPED dielectric F0 plus the specular factor, and two affine transform
        // rows per specular map. The unclamped value is the point: `specularColorTexture` multiplies
        // BEFORE the min(...,1), so a shader handed the pre-clamped F0 cannot reproduce the
        // extension's own equation.
        void FillPbrFactors(std::array<float, 76>& out, const GpuDrawParams& p)
        {
            out[0] = p.pbrMetallicFactor;
            out[1] = p.pbrRoughnessFactor;
            out[2] = p.pbrNormalScale;
            out[3] = p.pbrOcclusionStrength;
            out[4] = p.alphaTest[0];
            out[5] = p.alphaTest[1];
            out[6] = p.alphaTest[2];
            out[7] = p.alphaTest[3];
            out[8] = p.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;
            out[9] = p.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;
            out[10] = p.pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
            // GLTF-344: w decodes the specular COLOUR sample from sRGB, the extension's own rule.
            out[11] = p.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f;
            out[12] = p.pbrDielectricF0[0];
            out[13] = p.pbrDielectricF0[1];
            out[14] = p.pbrDielectricF0[2];
            out[15] = p.pbrDielectricF90;
            for (int row = 0; row < 10; ++row)
                for (int component = 0; component < 4; ++component)
                    out[16 + row * 4 + component] =
                        p.pbrTextureTransformRows[row][component];
            // GLTF-344: xyz = unclamped dielectric F0, w = specularFactor.
            out[56] = p.pbrDielectricF0Unclamped[0];
            out[57] = p.pbrDielectricF0Unclamped[1];
            out[58] = p.pbrDielectricF0Unclamped[2];
            out[59] = p.pbrSpecularFactor;
            for (int row = 0; row < 4; ++row)
                for (int component = 0; component < 4; ++component)
                    out[60 + row * 4 + component] =
                        p.pbrSpecularTextureTransformRows[row][component];
        }

        // New bone-palette uniform buffer for the skinned shaders (skinned3d.wgsl/skinned_pbr3d.wgsl):
        // a small vec4f header (x = WeightsPerVertex, Task 895's 1/2/4 convention -- only the first
        // N weight/index pairs are summed) followed by all 72 column-major bone matrices, matching
        // EasyGLRenderer's own uBones[72] uniform array shape. Always copies the full fixed
        // 72-entry array regardless of GpuDrawParams::boneCount: the tail is guaranteed zero
        // (GpuDrawParams::boneTransforms's own default member initializer, since
        // SkinnedEffect::FillGpuDrawParams only overwrites the first boneCount entries), and
        // well-formed vertex data never indexes past boneCount anyway.
        void FillSkinningParams(std::array<float, 4 + 72 * 16>& out, const GpuDrawParams& p)
        {
            out[0] = static_cast<float>(p.weightsPerVertex);
            out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f;
            for (int i = 0; i < 72 * 16; ++i)
                out[4 + i] = p.boneTransforms[i];
        }

        [[nodiscard]] bool IsSurfaceRecoverable(WGPUSurfaceGetCurrentTextureStatus status)
        {
            return status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
                   status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
                   status == WGPUSurfaceGetCurrentTextureStatus_Lost;
        }

        void WaitForCompletion(WGPUInstance instance, const bool& completed, const char* operation)
        {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::nanoseconds(kRequestTimeoutNanoseconds);
            while (!completed && std::chrono::steady_clock::now() < deadline)
            {
#if defined(__EMSCRIPTEN__)
                // Asyncify hands control back to the browser event loop so the pending WebGPU
                // promise can settle; the following pump then drains any completed future into its
                // callback. A plain sleep would never yield to JavaScript and would only ever time
                // out here.
                emscripten_sleep(1);
                wgpuInstanceProcessEvents(instance);
#else
                wgpuInstanceProcessEvents(instance);
                if (!completed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
            }
            if (!completed)
            {
                throw std::runtime_error(
                    std::string("CNA WebGPU: timed out waiting for ") + operation);
            }
        }
    }

    // REMED-GFX-167: see WebGPUSampledResourceEXT's own doc comment (the header). Both handles are
    // referenced here and released exactly once, when the last queued command carrying this object
    // is gone -- so a resource sampled by a still-queued draw stays valid without the wrapper.
    WebGPUSampledResourceEXT::WebGPUSampledResourceEXT(WGPUTexture texture, WGPUTextureView view)
        : texture_(texture), view_(view)
    {
        if (texture_ != nullptr) wgpuTextureAddRef(texture_);
        if (view_ != nullptr) wgpuTextureViewAddRef(view_);
    }

    WebGPUSampledResourceEXT::~WebGPUSampledResourceEXT()
    {
        if (view_ != nullptr) wgpuTextureViewRelease(view_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    WebGPUTextureRenderer::WebGPUTextureRenderer(WebGPURenderer& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height), mipLevels_(std::max(1, data.mipLevels))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument("CNA WebGPU: texture dimensions must be positive");

        // WEBGPU-144: block-compressed textures (DXT/BC7) store their BC blocks natively.
        const WebGPUTexFormatInfo info = ClassifyWebGPUTextureFormat(data.surfaceFormat);
        surfaceFormat_ = data.surfaceFormat;
        wgpuFormat_ = info.format;
        compressed_ = info.compressed;
        blockBytes_ = info.blockBytes;
        if (compressed_)
            compressedLevels_.resize(static_cast<std::size_t>(mipLevels_));

        if (!compressed_ && !data.pixels.empty())
        {
            const std::size_t required = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
            if (data.pixels.size() < required)
                throw std::invalid_argument("CNA WebGPU: Texture2D RGBA buffer is smaller than width*height*4");
        }

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Texture2D");
        // WEBGPU-51: CopySrc is required by GetData()'s wgpuCommandEncoderCopyTextureToBuffer()
        // readback (added alongside that method -- every texture created before GetData() existed
        // never needed it). WEBGPU-52: RenderAttachment is required by GenerateMips2D()'s
        // render-pass-based mip blit (see that method's own doc comment) -- only actually used
        // when mipLevels_ > 1 and initial pixel data is supplied below, but declared
        // unconditionally since usage flags are fixed at texture-creation time.
        // WEBGPU-144: a block-compressed texture is never a render attachment and never mip-generated
        // (its mips are supplied pre-compressed), so RenderAttachment is dropped for it; CopySrc/Dst
        // stay for block upload/readback.
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                           WGPUTextureUsage_CopySrc |
                           (compressed_ ? WGPUTextureUsage_None : WGPUTextureUsage_RenderAttachment);
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        descriptor.format = wgpuFormat_;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels_);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner.Device(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Texture2D");

        view_ = wgpuTextureCreateView(texture_, nullptr);
        if (view_ == nullptr)
        {
            wgpuTextureRelease(texture_);
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create Texture2D view");
        }
        sampled_ = std::make_shared<const WebGPUSampledResourceEXT>(texture_, view_);

        if (!data.pixels.empty())
        {
            // WEBGPU-144: the compressed ctor buffer is level 0's blocks (zero-filled by the
            // Texture2D ctor, then overwritten by SetData -> UpdatePixelsLevel).
            if (compressed_)
                UpdatePixelsLevel(0, data.pixels.data(), width_, height_);
            else
                UpdatePixels(data.pixels.data(), width_ * 4);
        }
    }

    WebGPUTextureRenderer::~WebGPUTextureRenderer()
    {
        if (view_ != nullptr)
            wgpuTextureViewRelease(view_);
        if (texture_ != nullptr)
            wgpuTextureRelease(texture_);
    }

    void WebGPUTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::invalid_argument("CNA WebGPU: texture update source cannot be null");
        if (stride < width_ * 4)
            throw std::invalid_argument("CNA WebGPU: texture update stride is too small");

        std::vector<std::uint8_t> tightlyPacked;
        const std::uint8_t* upload = rgba;
        if (stride != width_ * 4)
        {
            tightlyPacked.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
            for (int y = 0; y < height_; ++y)
            {
                std::memcpy(tightlyPacked.data() + static_cast<std::size_t>(y) * width_ * 4u,
                            rgba + static_cast<std::size_t>(y) * stride,
                            static_cast<std::size_t>(width_) * 4u);
            }
            upload = tightlyPacked.data();
        }
        UpdatePixelsLevel(0, upload, width_, height_);

        // WEBGPU-52: real, genuinely-linear-filtered mip generation from level 0 -- see
        // WebGPURenderer::EnsureMipBlitPipeline()'s own doc comment for the full rationale
        // and the deliberate divergence this introduces from FNA and every sibling CNA renderer
        // (which only auto-regenerate mips for a RENDER TARGET being unbound, not a plain
        // Texture2D). Placed here (not just in the constructor) so a LATER Texture2D::SetData()
        // call that replaces level 0 (the only renderer entry point XNA-layer SetData(level=0,...)
        // ever calls, even for a partial sub-rectangle -- see Texture2D.cpp's own
        // getMipBuffer()-backed whole-level re-upload) regenerates the mip chain again too,
        // consistent with WebGPUTextureCubeRenderer::SetData()'s identical per-level-0-write
        // trigger. A no-op when mipLevels_ == 1.
        if (mipLevels_ > 1)
            owner_->GenerateMips2D(texture_, width_, height_, mipLevels_);
    }

    void WebGPUTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_ || rgba == nullptr || levelW <= 0 || levelH <= 0)
            throw std::invalid_argument("CNA WebGPU: invalid mip upload");

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture_;
        destination.mipLevel = static_cast<std::uint32_t>(level);
        destination.aspect = WGPUTextureAspect_All;

        // WEBGPU-144: a block-compressed level's data is `rgba` reinterpreted as BC blocks. The copy
        // layout is expressed in blocks (bytesPerRow = block-cols * blockBytes, rowsPerImage =
        // block-rows) while the extent stays the level's texel dimensions; the blocks are also kept
        // in `compressedLevels_` as the authoritative store the framework's GetData reads back.
        if (compressed_)
        {
            const int blockCols = (levelW + 3) / 4;
            const int blockRows = (levelH + 3) / 4;
            const std::size_t byteCount =
                static_cast<std::size_t>(blockCols) * blockRows * static_cast<std::size_t>(blockBytes_);
            compressedLevels_[static_cast<std::size_t>(level)].assign(rgba, rgba + byteCount);

            WGPUTexelCopyBufferLayout layout{};
            layout.bytesPerRow = static_cast<std::uint32_t>(blockCols * blockBytes_);
            layout.rowsPerImage = static_cast<std::uint32_t>(blockRows);
            // WEBGPU-144 Phase 2: the copy extent must be the BLOCK-ALIGNED physical size, not the
            // logical texel size. wgpu-native validates a compressed copy's width/height against the
            // mip's block-aligned physical extent (ceil(dim/4)*4), so a sub-4x4 mip (a 2x2 or 1x1
            // tail level of a mip chain) passed as its logical 2 or 1 is rejected as "Copy width is
            // not a multiple of block width". ceil(levelW/4)*4 equals the logical size for every
            // block-multiple level and rounds a partial tail level up to its single padded block.
            const WGPUExtent3D extent{static_cast<std::uint32_t>(blockCols * 4),
                                      static_cast<std::uint32_t>(blockRows * 4), 1};
            wgpuQueueWriteTexture(owner_->Queue(), &destination, rgba, byteCount, &layout, &extent);
            return;
        }

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<std::uint32_t>(levelW * 4);
        layout.rowsPerImage = static_cast<std::uint32_t>(levelH);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};
        const std::size_t byteCount = static_cast<std::size_t>(levelW) * static_cast<std::size_t>(levelH) * 4u;
        wgpuQueueWriteTexture(owner_->Queue(), &destination, rgba, byteCount, &layout, &extent);
    }

    // WEBGPU-51: real CPU readback for an arbitrary Texture2D -- reuses the exact staged
    // MAP_READ-buffer/aligned-row/async-map-and-poll technique WEBGPU-91's ReadBackbuffer() and
    // WebGPURenderTargetRenderer::GetData() already established (see both for the precedent).
    // Copies the WHOLE requested mip level to a temporary buffer, then extracts the x/y/w/h
    // sub-rectangle from the mapped memory -- this texture is always WGPUTextureFormat_RGBA8Unorm
    // (see the constructor above), so unlike the swapchain/a RenderTarget2D there is never a
    // BGRA byte-swap to apply.
    bool WebGPUTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                        void* data, int dataLength) const
    {
        // REMED-GFX-127: nothing is written on these paths, so they must not be reported as a
        // completed readback -- the shared layer would otherwise convert its own zeroed scratch.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (level < 0 || level >= mipLevels_)
            throw std::out_of_range("CNA WebGPU: Texture2D.GetData: mip level out of range");

        // WEBGPU-144: a compressed texture is its own authoritative block store (Texture2D keeps no
        // compressed CPU shadow). Return the requested block sub-rectangle's raw bytes -- the
        // framework guarantees x/y/w/h are already block-aligned and in bounds.
        if (compressed_)
        {
            const std::size_t lvl = static_cast<std::size_t>(level);
            if (lvl >= compressedLevels_.size() || compressedLevels_[lvl].empty())
                return false;
            const int lW = MipDim(width_, level);
            const int levelBlockCols = (lW + 3) / 4;
            const int rectBlockCols = (w + 3) / 4;
            const int rectBlockRows = (h + 3) / 4;
            const std::size_t needed =
                static_cast<std::size_t>(rectBlockCols) * rectBlockRows * static_cast<std::size_t>(blockBytes_);
            if (dataLength < static_cast<int>(needed)) return false;
            const int blockX = x / 4;
            const int blockY = y / 4;
            const std::size_t levelRowBytes =
                static_cast<std::size_t>(levelBlockCols) * static_cast<std::size_t>(blockBytes_);
            const std::size_t rectRowBytes =
                static_cast<std::size_t>(rectBlockCols) * static_cast<std::size_t>(blockBytes_);
            const std::vector<std::uint8_t>& blocks = compressedLevels_[lvl];
            for (int row = 0; row < rectBlockRows; ++row)
            {
                const std::size_t srcOff =
                    static_cast<std::size_t>(blockY + row) * levelRowBytes +
                    static_cast<std::size_t>(blockX) * static_cast<std::size_t>(blockBytes_);
                if (srcOff + rectRowBytes > blocks.size()) return false;
                std::memcpy(static_cast<std::uint8_t*>(data) + static_cast<std::size_t>(row) * rectRowBytes,
                            blocks.data() + srcOff, rectRowBytes);
            }
            return true;
        }

        const int levelW = MipDim(width_, level);
        const int levelH = MipDim(height_, level);
        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(levelW) * 4u);
        const std::uint64_t bufferSize = static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(levelH);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = StringView("CNA WebGPU Texture2D Readback Buffer");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = bufferSize;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(owner_->Device(), &bufferDescriptor);
        if (readbackBuffer == nullptr)
            throw std::runtime_error("CNA WebGPU: Texture2D.GetData: failed to create readback buffer");

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU Texture2D Readback Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(owner_->Device(), &encoderDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = texture_;
        source.mipLevel = static_cast<std::uint32_t>(level);
        source.origin = WGPUOrigin3D{0, 0, 0};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(levelH);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU Texture2D Readback Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(owner_->Queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);
        WaitForCompletion(owner_->Instance(), mapState.completed, "Texture2D readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readbackBuffer);
            throw std::runtime_error("CNA WebGPU: Texture2D.GetData: readback buffer map failed: " + mapState.error);
        }

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t requiredLength = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        // REMED-GFX-127: an unmappable buffer or an undersized destination used to memset the
        // caller's memory to zero and return as if the read had succeeded -- exactly the fabricated
        // transparent-black frame this finding removes. Report the failure instead, leaving the
        // destination untouched.
        if (mapped == nullptr || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredLength)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        for (int row = 0; row < h; ++row)
        {
            const int sy = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int sx = x + col;
                std::uint8_t* d = out + (static_cast<std::size_t>(row) * w + col) * 4;
                if (sx < 0 || sx >= levelW || sy < 0 || sy >= levelH)
                {
                    d[0] = d[1] = d[2] = d[3] = 0;
                    continue;
                }
                const std::uint8_t* s = mapped + static_cast<std::size_t>(sy) * bytesPerRow +
                                        static_cast<std::size_t>(sx) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return true;
    }

    // WEBGPU-56/74: minimal cube-map texture renderer, just enough for
    // EnvironmentMapEffect.EnvironmentMap. Mirrors EasyGLTextureCubeRenderer's own
    // CalculateCubeMipLevels(size) mip-count convention. A WebGPU cube map is a plain
    // WGPUTextureDimension_2D texture with 6 array layers; only the *view* (created below with
    // WGPUTextureViewDimension_Cube) makes it sampleable as texture_cube<f32> in WGSL.
    WebGPUTextureCubeRenderer::WebGPUTextureCubeRenderer(WebGPURenderer& owner, int size, bool mipMap)
        : owner_(&owner), size_(size)
    {
        if (size_ <= 0)
            throw std::invalid_argument("CNA WebGPU: TextureCube size must be positive");

        mipLevels_ = 1;
        if (mipMap)
        {
            int s = size_;
            while (s > 1) { s = std::max(1, s / 2); ++mipLevels_; }
        }

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU TextureCube");
        // WEBGPU-113: CopySrc is required by GetData()'s wgpuCommandEncoderCopyTextureToBuffer()
        // readback (added alongside that method). WEBGPU-52: RenderAttachment is required by
        // GenerateMipsCubeFace()'s render-pass-based mip blit -- see that method's own doc
        // comment; only actually used when mipLevels_ > 1, declared unconditionally since usage
        // flags are fixed at texture-creation time.
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                           WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(size_), static_cast<std::uint32_t>(size_), 6};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels_);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner.Device(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create TextureCube");

        WGPUTextureViewDescriptor viewDescriptor{};
        viewDescriptor.label = StringView("CNA WebGPU TextureCube View");
        viewDescriptor.format = WGPUTextureFormat_RGBA8Unorm;
        viewDescriptor.dimension = WGPUTextureViewDimension_Cube;
        viewDescriptor.baseMipLevel = 0;
        viewDescriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels_);
        viewDescriptor.baseArrayLayer = 0;
        viewDescriptor.arrayLayerCount = 6;
        viewDescriptor.aspect = WGPUTextureAspect_All;
        cubeView_ = wgpuTextureCreateView(texture_, &viewDescriptor);
        if (cubeView_ == nullptr)
        {
            wgpuTextureRelease(texture_);
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create TextureCube view");
        }
        sampled_ = std::make_shared<const WebGPUSampledResourceEXT>(texture_, cubeView_);
    }

    WebGPUTextureCubeRenderer::~WebGPUTextureCubeRenderer()
    {
        if (cubeView_ != nullptr) wgpuTextureViewRelease(cubeView_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    // face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z (matches IRenderTargetCubeRenderer's own documented
    // convention and TextureCube.cpp's CubeMapFace ordinal values) -- maps directly onto the
    // texture's array layer index, since WebGPU cube faces are just array layers 0..5 in a fixed,
    // implementation-defined order that matches this.
    bool WebGPUTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int dataLength)
    {
        // REMED-GFX-135: these were `throw std::out_of_range`/`std::invalid_argument` from inside a
        // renderer, so a request this renderer cannot store escaped the public API as a raw std
        // exception rather than the shared layer's own deterministic System::NotSupportedException.
        // Returning false routes every such case through the one contract.
        if (face < 0 || face >= 6) return false;
        if (level < 0 || level >= mipLevels_) return false;
        if (data == nullptr || w <= 0 || h <= 0) return false;
        const int levelSize = MipDim(size_, level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture_;
        destination.mipLevel = static_cast<std::uint32_t>(level);
        destination.origin = WGPUOrigin3D{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                          static_cast<std::uint32_t>(face)};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<std::uint32_t>(w * 4);
        layout.rowsPerImage = static_cast<std::uint32_t>(h);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
        wgpuQueueWriteTexture(owner_->Queue(), &destination, data, required, &layout, &extent);

        // WEBGPU-52: real, genuinely-linear-filtered mip generation for THIS face, from its own
        // level 0 -- see WebGPURenderer::EnsureMipBlitPipeline()'s own doc comment for the
        // full rationale and the deliberate divergence this introduces from FNA and every sibling
        // CNA renderer. Triggers on every level-0 write (even a partial sub-rectangle, matching
        // this method's own existing per-call granularity) rather than only once at "the first
        // full-face upload", since TextureCube -- unlike Texture2D -- has no single constructor-
        // time initial-pixel-data moment to hook; a game that calls SetData(face, level=0, ...)
        // more than once simply regenerates that face's mips again each time, which is correct
        // (if not maximally efficient). A no-op when mipLevels_ == 1.
        if (level == 0 && mipLevels_ > 1)
            owner_->GenerateMipsCubeFace(texture_, face, size_, mipLevels_);
        // wgpuQueueWriteTexture copies out of `data` before it returns, so nothing here still
        // depends on caller memory once this call completes (REMED-GFX-135).
        return true;
    }

    // WEBGPU-113: real per-face CPU readback, same staged-copy/row-alignment/async-map technique
    // as WebGPUTextureRenderer::GetData() (WEBGPU-51) -- origin.z = face selects the array layer,
    // matching SetData()'s own established convention above. Always RGBA8Unorm, so no BGRA swap.
    bool WebGPUTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        // REMED-GFX-130: this guard used to be a silent `return`, which the shared layer turned
        // into a complete transparent-black face instead of a refusal.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (face < 0 || face >= 6)
            throw std::out_of_range("CNA WebGPU: TextureCube.GetData: face must be 0..5");
        if (level < 0 || level >= mipLevels_)
            throw std::out_of_range("CNA WebGPU: TextureCube.GetData: mip level out of range");

        const int levelSize = MipDim(size_, level);
        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(levelSize) * 4u);
        const std::uint64_t bufferSize = static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(levelSize);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = StringView("CNA WebGPU TextureCube Readback Buffer");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = bufferSize;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(owner_->Device(), &bufferDescriptor);
        if (readbackBuffer == nullptr)
            throw std::runtime_error("CNA WebGPU: TextureCube.GetData: failed to create readback buffer");

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU TextureCube Readback Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(owner_->Device(), &encoderDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = texture_;
        source.mipLevel = static_cast<std::uint32_t>(level);
        source.origin = WGPUOrigin3D{0, 0, static_cast<std::uint32_t>(face)};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(levelSize);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(levelSize), static_cast<std::uint32_t>(levelSize), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU TextureCube Readback Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(owner_->Queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);
        WaitForCompletion(owner_->Instance(), mapState.completed, "TextureCube readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readbackBuffer);
            throw std::runtime_error("CNA WebGPU: TextureCube.GetData: readback buffer map failed: " + mapState.error);
        }

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t requiredLength = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        // REMED-GFX-130: this branch used to memset the caller's destination to zero and return as
        // if the readback had succeeded -- the same fabrication the shared layer was doing, one
        // level down. Report the failure instead; the caller's buffer is left exactly as it was.
        if (mapped == nullptr || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredLength)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        // Any texel outside the level is not content this texture holds, so the request as a whole
        // cannot be answered -- refuse rather than pad the gap with zeros.
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        for (int row = 0; row < h; ++row)
        {
            const int sy = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int sx = x + col;
                std::uint8_t* d = out + (static_cast<std::size_t>(row) * w + col) * 4;
                const std::uint8_t* s = mapped + static_cast<std::size_t>(sy) * bytesPerRow +
                                        static_cast<std::size_t>(sx) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return true;
    }

    // WEBGPU-114: see this class's own header doc comment for the full architecture summary.
    WebGPURenderTargetCubeRenderer::WebGPURenderTargetCubeRenderer(WebGPURenderer& owner, int size,
                                                                  int depthFormat,
                                                                  bool preserveContents, bool mipMap)
        : owner_(&owner), size_(size), preserveContents_(preserveContents)
    {
        if (size_ <= 0)
            throw std::invalid_argument("CNA WebGPU: RenderTargetCube size must be positive");
        // WEBGPU-114: mip-chain regeneration is not implemented -- same documented scope cut, for
        // the same reason, as CreateRenderTarget2D()'s own mipMap=true throw (see that function's
        // own comment: silently under-delivering a requested mip chain is worse than a clear
        // exception when RenderTargetCube::RenderTargetCube() has already told the XNA layer to
        // expect one).
        if (mipMap)
            throw std::runtime_error("CNA WebGPU: RenderTargetCube mip-chain regeneration "
                                     "(mipMap=true) is not implemented on this renderer -- see "
                                     "plans/plan_webgpu.md WEBGPU-114");
        // Always allocates a Depth24PlusStencil8 attachment regardless of the requested
        // depthFormat -- identical simplification to WebGPURenderTargetRenderer's own constructor
        // (see that class's constructor comment for the full rationale).
        (void) depthFormat;

        // Colour texture: this instance's own surfaceFormat_ snapshot (matching
        // WebGPURenderTargetRenderer's own choice), NOT WGPUTextureFormat_RGBA8Unorm
        // (WebGPUTextureCubeRenderer's own convention for a plain upload-only TextureCube) --
        // every GetOrCreatePipeline*3D() hardcodes `target.format = surfaceFormat_`, so a 3D draw
        // into a cube face must match that format to stay pipeline-compatible.
        //
        // REMED-GFX-131: surfaceFormat_ is guaranteed non-sRGB, so a cube face carries the same
        // byte-exact SurfaceFormat::Color semantics as a RenderTarget2D and a plain TextureCube.
        colorFormat_ = owner_->surfaceFormat_;
        if (colorFormat_ == WGPUTextureFormat_Undefined)
            throw std::runtime_error("CNA WebGPU: cannot create a RenderTargetCube before the "
                                     "swapchain surface format is known");

        WGPUTextureDescriptor colorDescriptor{};
        colorDescriptor.label = StringView("CNA WebGPU RenderTargetCube Color");
        colorDescriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                                WGPUTextureUsage_CopySrc;
        colorDescriptor.dimension = WGPUTextureDimension_2D;
        colorDescriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(size_), static_cast<std::uint32_t>(size_), 6};
        colorDescriptor.format = colorFormat_;
        colorDescriptor.mipLevelCount = 1;
        colorDescriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner_->Device(), &colorDescriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create RenderTargetCube colour texture");

        WGPUTextureViewDescriptor cubeViewDescriptor{};
        cubeViewDescriptor.label = StringView("CNA WebGPU RenderTargetCube Cube View");
        cubeViewDescriptor.format = colorFormat_;
        cubeViewDescriptor.dimension = WGPUTextureViewDimension_Cube;
        cubeViewDescriptor.baseMipLevel = 0;
        cubeViewDescriptor.mipLevelCount = 1;
        cubeViewDescriptor.baseArrayLayer = 0;
        cubeViewDescriptor.arrayLayerCount = 6;
        cubeViewDescriptor.aspect = WGPUTextureAspect_All;
        cubeView_ = wgpuTextureCreateView(texture_, &cubeViewDescriptor);
        if (cubeView_ == nullptr)
        {
            wgpuTextureRelease(texture_);
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTargetCube cube view");
        }

        for (int face = 0; face < 6; ++face)
        {
            WGPUTextureViewDescriptor faceViewDescriptor{};
            faceViewDescriptor.label = StringView("CNA WebGPU RenderTargetCube Face View");
            faceViewDescriptor.format = colorFormat_;
            faceViewDescriptor.dimension = WGPUTextureViewDimension_2D;
            faceViewDescriptor.baseMipLevel = 0;
            faceViewDescriptor.mipLevelCount = 1;
            faceViewDescriptor.baseArrayLayer = static_cast<std::uint32_t>(face);
            faceViewDescriptor.arrayLayerCount = 1;
            faceViewDescriptor.aspect = WGPUTextureAspect_All;
            faceViews_[static_cast<std::size_t>(face)] = wgpuTextureCreateView(texture_, &faceViewDescriptor);
            if (faceViews_[static_cast<std::size_t>(face)] == nullptr)
            {
                for (int cleanupFace = 0; cleanupFace < face; ++cleanupFace)
                    wgpuTextureViewRelease(faceViews_[static_cast<std::size_t>(cleanupFace)]);
                wgpuTextureViewRelease(cubeView_);
                wgpuTextureRelease(texture_);
                cubeView_ = nullptr;
                texture_ = nullptr;
                throw std::runtime_error("CNA WebGPU: failed to create RenderTargetCube face view");
            }
        }

        // Depth+stencil: one shared attachment reused across all 6 faces -- safe because only one
        // face is ever bound/rendered-into at a time (see WebGPURenderer::
        // currentRenderTargetCubeFace_'s own doc comment).
        WGPUTextureDescriptor depthDescriptor{};
        depthDescriptor.label = StringView("CNA WebGPU RenderTargetCube DepthStencil");
        depthDescriptor.usage = WGPUTextureUsage_RenderAttachment;
        depthDescriptor.dimension = WGPUTextureDimension_2D;
        depthDescriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(size_), static_cast<std::uint32_t>(size_), 1};
        depthDescriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthDescriptor.mipLevelCount = 1;
        depthDescriptor.sampleCount = 1;
        depthTexture_ = wgpuDeviceCreateTexture(owner_->Device(), &depthDescriptor);
        if (depthTexture_ == nullptr)
        {
            for (WGPUTextureView view : faceViews_) wgpuTextureViewRelease(view);
            wgpuTextureViewRelease(cubeView_);
            wgpuTextureRelease(texture_);
            cubeView_ = nullptr;
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTargetCube depth-stencil texture");
        }
        depthView_ = wgpuTextureCreateView(depthTexture_, nullptr);
        if (depthView_ == nullptr)
        {
            wgpuTextureRelease(depthTexture_);
            for (WGPUTextureView view : faceViews_) wgpuTextureViewRelease(view);
            wgpuTextureViewRelease(cubeView_);
            wgpuTextureRelease(texture_);
            depthTexture_ = nullptr;
            cubeView_ = nullptr;
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTargetCube depth-stencil view");
        }
        sampled_ = std::make_shared<const WebGPUSampledResourceEXT>(texture_, cubeView_);
    }

    WebGPURenderTargetCubeRenderer::~WebGPURenderTargetCubeRenderer()
    {
        // RenderTargetCube::Dispose() (mirroring RenderTarget2D's own Task 717 precedent) already
        // refuses to dispose a render target still bound on its GraphicsDevice -- kept as
        // defense-in-depth, mirroring WebGPURenderTargetRenderer's own identical destructor guard.
        if (owner_ != nullptr && owner_->currentRenderTargetCubeFace_ == this)
        {
            owner_->currentRenderTargetCubeFace_ = nullptr;
            owner_->currentRenderTargetCubeFaceIndex_ = -1;
        }
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        for (WGPUTextureView view : faceViews_)
            if (view != nullptr) wgpuTextureViewRelease(view);
        if (cubeView_ != nullptr) wgpuTextureViewRelease(cubeView_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    void WebGPURenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        if (owner_ == nullptr || face < 0 || face >= 6) return;
        if (owner_->currentRenderTargetCubeFace_ == this && owner_->currentRenderTargetCubeFaceIndex_ == face)
            return; // already the current target/face -- nothing to flush/switch.

        // WEBGPU-114: flush whatever was previously bound (backbuffer / a RenderTarget2D / a
        // DIFFERENT face of this or another RenderTargetCube) into its own render pass before
        // switching to this face -- mirrors WebGPURenderer::SetRenderTarget2D()'s own
        // eager-flush-on-switch design, generalised via FlushCurrentRenderTarget() so a cube face
        // is just as much a distinct "target identity" as a RenderTarget2D or the backbuffer.
        owner_->FlushCurrentRenderTarget();
        owner_->currentRenderTarget_ = nullptr;
        // WEBGPU-87: binding a cube face drops any prior MRT set (flushed just above).
        owner_->mrtExtraTargets_.clear();
        owner_->currentRenderTargetCubeFace_ = this;
        owner_->currentRenderTargetCubeFaceIndex_ = face;
    }

    void WebGPURenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr && owner_->currentRenderTargetCubeFace_ == this)
        {
            owner_->currentRenderTargetCubeFace_ = nullptr;
            owner_->currentRenderTargetCubeFaceIndex_ = -1;
        }
    }

    // WEBGPU-114: real per-face CPU readback -- same staged-copy/row-alignment/async-map
    // technique as WebGPUTextureCubeRenderer::GetData() (WEBGPU-113), plus the on-demand-flush
    // check WebGPURenderTargetRenderer::GetData() (WEBGPU-53/54) established for a still-bound
    // render target.
    bool WebGPURenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        // REMED-GFX-130: as above -- a silent `return` was converted into a fabricated face.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (face < 0 || face >= 6)
            return false;
        // REMED-GFX-134: this target has exactly one level (WEBGPU-114 refuses a mipMap=true
        // RenderTargetCube at construction), so a higher level is a capability boundary, not an
        // argument error. Reporting it as `false` is what turns it into the shared layer's own
        // deterministic System::NotSupportedException with the caller's destination untouched --
        // the raw std::invalid_argument this replaces escaped the public XNA surface unchanged,
        // the same defect shape REMED-GFX-135 already removed from this renderer's SetData.
        if (level != 0)
            return false;
        if (x < 0 || y < 0 || x + w > size_ || y + h > size_)
            return false;

        // REMED-GFX-134: flush ONLY when there is something queued. This face's render pass always
        // clears (see RenderPendingDrawsToRenderTargetCubeFace's own comment), so flushing
        // unconditionally made a GetData on a still-bound face WIPE the very face it was asked to
        // read and then return the clear colour as content. `framePending_` is set by every Clear()
        // and Queue*Draw() and cleared by each flush, so this keeps the
        // SetRenderTarget+draw+GetData-without-unbinding sequence working while leaving an idle
        // bound target untouched.
        if (owner_->currentRenderTargetCubeFace_ == this && owner_->framePending_)
            owner_->RenderPendingDrawsToRenderTargetCubeFace(
                const_cast<WebGPURenderTargetCubeRenderer*>(this), owner_->currentRenderTargetCubeFaceIndex_);

        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(size_) * 4u);
        const std::uint64_t bufferSize = static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(size_);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = StringView("CNA WebGPU RenderTargetCube Readback Buffer");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = bufferSize;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(owner_->Device(), &bufferDescriptor);
        if (readbackBuffer == nullptr)
            throw std::runtime_error("CNA WebGPU: RenderTargetCube.GetData: failed to create readback buffer");

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTargetCube Readback Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(owner_->Device(), &encoderDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = texture_;
        source.mipLevel = 0;
        source.origin = WGPUOrigin3D{0, 0, static_cast<std::uint32_t>(face)};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(size_);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(size_), static_cast<std::uint32_t>(size_), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU RenderTargetCube Readback Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(owner_->Queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);
        WaitForCompletion(owner_->Instance(), mapState.completed, "RenderTargetCube readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readbackBuffer);
            throw std::runtime_error("CNA WebGPU: RenderTargetCube.GetData: readback buffer map "
                                     "failed: " + mapState.error);
        }

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
        const bool isBgra = (colorFormat_ == WGPUTextureFormat_BGRA8Unorm ||
                             colorFormat_ == WGPUTextureFormat_BGRA8UnormSrgb);
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t requiredLength = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        // REMED-GFX-130: this branch used to memset the caller's destination to zero and return as
        // if the readback had succeeded. Report the failure instead.
        if (mapped == nullptr || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredLength ||
            x < 0 || y < 0 || x + w > size_ || y + h > size_)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        for (int row = 0; row < h; ++row)
        {
            const int sy = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int sx = x + col;
                std::uint8_t* d = out + (static_cast<std::size_t>(row) * w + col) * 4;
                const std::uint8_t* s = mapped + static_cast<std::size_t>(sy) * bytesPerRow +
                                        static_cast<std::size_t>(sx) * 4;
                if (isBgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            }
        }
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return true;
    }

    // WEBGPU-57/112: a plain WGPUTextureDimension_3D volume texture -- upload/readback only (no
    // render-target-ness, matching Texture3D's own XNA semantics: it is never a draw target). Mip
    // level COUNT mirrors Texture3D.cpp's own CalculateMipLevels(width, height) (depth does not
    // participate in the count); wgpu-native still halves the actual per-level depth extent
    // automatically like every other dimension (standard 3D mip rules), independent of this count.
    WebGPUTexture3DRenderer::WebGPUTexture3DRenderer(WebGPURenderer& owner, int width, int height,
                                                    int depth, bool mipMap)
        : owner_(&owner), width_(width), height_(height), depth_(depth)
    {
        if (width_ <= 0 || height_ <= 0 || depth_ <= 0)
            throw std::invalid_argument("CNA WebGPU: Texture3D dimensions must be positive");

        mipLevels_ = 1;
        if (mipMap)
        {
            int w = width_, h = height_;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++mipLevels_; }
        }

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Texture3D");
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        descriptor.dimension = WGPUTextureDimension_3D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_),
                                       static_cast<std::uint32_t>(depth_)};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels_);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner.Device(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Texture3D");
    }

    WebGPUTexture3DRenderer::~WebGPUTexture3DRenderer()
    {
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    bool WebGPUTexture3DRenderer::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int dataLength)
    {
        // REMED-GFX-135: see WebGPUTextureCubeRenderer::SetData for why these are results, not
        // throws.
        if (level < 0 || level >= mipLevels_) return false;
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0) return false;
        const int levelW = MipDim(width_, level);
        const int levelH = MipDim(height_, level);
        const int levelD = MipDim(depth_, level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        const std::size_t required =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(depth) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture_;
        destination.mipLevel = static_cast<std::uint32_t>(level);
        destination.origin = WGPUOrigin3D{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                          static_cast<std::uint32_t>(z)};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<std::uint32_t>(w * 4);
        layout.rowsPerImage = static_cast<std::uint32_t>(h);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                                  static_cast<std::uint32_t>(depth)};
        wgpuQueueWriteTexture(owner_->Queue(), &destination, data, required, &layout, &extent);
        return true;
    }

    // Same staged-copy/row-alignment/async-map technique as WebGPUTextureRenderer::GetData()
    // (WEBGPU-51), extended to a 3rd (depth) dimension: the WHOLE requested level's volume is
    // copied to a temporary readback buffer (alignedBytesPerRow * levelHeight * levelDepth), then
    // the x/y/z/w/h/depth sub-volume is extracted from the mapped memory on the CPU side.
    bool WebGPUTexture3DRenderer::GetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: as above -- a silent `return` was converted into a fabricated volume.
        if (owner_ == nullptr || w <= 0 || h <= 0 || depth <= 0 || data == nullptr)
            return false;
        if (level < 0 || level >= mipLevels_)
            throw std::out_of_range("CNA WebGPU: Texture3D.GetData: level out of range");

        const int levelW = MipDim(width_, level);
        const int levelH = MipDim(height_, level);
        const int levelDepth = MipDim(depth_, level);
        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(levelW) * 4u);
        const std::uint64_t sliceBytes = static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(levelH);
        const std::uint64_t bufferSize = sliceBytes * static_cast<std::uint64_t>(levelDepth);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = StringView("CNA WebGPU Texture3D Readback Buffer");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = bufferSize;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(owner_->Device(), &bufferDescriptor);
        if (readbackBuffer == nullptr)
            throw std::runtime_error("CNA WebGPU: Texture3D.GetData: failed to create readback buffer");

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU Texture3D Readback Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(owner_->Device(), &encoderDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = texture_;
        source.mipLevel = static_cast<std::uint32_t>(level);
        source.origin = WGPUOrigin3D{0, 0, 0};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(levelH);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH),
                                    static_cast<std::uint32_t>(levelDepth)};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU Texture3D Readback Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(owner_->Queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);
        WaitForCompletion(owner_->Instance(), mapState.completed, "Texture3D readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readbackBuffer);
            throw std::runtime_error("CNA WebGPU: Texture3D.GetData: readback buffer map failed: " + mapState.error);
        }

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t requiredLength =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(depth) * 4u;
        // REMED-GFX-130: this branch used to memset the caller's destination to zero and return as
        // if the readback had succeeded. Report the failure instead; likewise a box that reaches
        // outside the level is refused rather than padded with zeros.
        if (mapped == nullptr || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredLength ||
            x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelDepth)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        for (int slice = 0; slice < depth; ++slice)
        {
            const int sz = z + slice;
            for (int row = 0; row < h; ++row)
            {
                const int sy = y + row;
                for (int col = 0; col < w; ++col)
                {
                    const int sx = x + col;
                    std::uint8_t* d = out + (static_cast<std::size_t>(slice) * w * h +
                                             static_cast<std::size_t>(row) * w + col) * 4;
                    const std::uint8_t* s = mapped + static_cast<std::size_t>(sz) * sliceBytes +
                                            static_cast<std::size_t>(sy) * bytesPerRow +
                                            static_cast<std::size_t>(sx) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return true;
    }

    WebGPURenderTargetRenderer::WebGPURenderTargetRenderer(WebGPURenderer& owner, int width, int height,
                                                          int depthFormat, bool preserveContents)
        : owner_(&owner), width_(width), height_(height), preserveContents_(preserveContents)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument("CNA WebGPU: RenderTarget2D dimensions must be positive");

        // Always created in the renderer's colour format (surfaceFormat_), NOT
        // WGPUTextureFormat_RGBA8Unorm (the format every plain WebGPUTextureRenderer always uses)
        // -- this lets every existing GetOrCreatePipeline*3D() (each hardcodes
        // `target.format = surfaceFormat_` at pipeline-creation time) render into this target's
        // colour attachment completely unchanged, with zero new pipeline-cache dimensions.
        // Sampling this render target back as an ordinary texture (SpriteBatch/
        // BasicEffect.Texture) is unaffected by this choice -- WGSL texture_2d<f32> sampling does
        // not care about the exact underlying texture format.
        //
        // REMED-GFX-131: surfaceFormat_ is guaranteed non-sRGB (see its own declaration), so this
        // inheritance no longer imports a hardware gamma encode into a resource whose bytes
        // RenderTarget2D::GetData hands straight to game code. It is the same format the plain
        // Texture2D path uses, which is what makes ordinary textures and render targets share one
        // colour semantics.
        colorFormat_ = owner_->surfaceFormat_;
        if (colorFormat_ == WGPUTextureFormat_Undefined)
            throw std::runtime_error("CNA WebGPU: cannot create a RenderTarget2D before the "
                                     "swapchain surface format is known");

        WGPUTextureDescriptor colorDescriptor{};
        colorDescriptor.label = StringView("CNA WebGPU RenderTarget2D Color");
        colorDescriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                                WGPUTextureUsage_CopySrc;
        colorDescriptor.dimension = WGPUTextureDimension_2D;
        colorDescriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        colorDescriptor.format = colorFormat_;
        colorDescriptor.mipLevelCount = 1;
        colorDescriptor.sampleCount = 1;
        colorTexture_ = wgpuDeviceCreateTexture(owner_->Device(), &colorDescriptor);
        if (colorTexture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D colour texture");
        colorView_ = wgpuTextureCreateView(colorTexture_, nullptr);
        if (colorView_ == nullptr)
        {
            wgpuTextureRelease(colorTexture_);
            colorTexture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D colour view");
        }

        // WEBGPU-53/54/9: every GetOrCreatePipeline*3D() unconditionally declares a
        // Depth24PlusStencil8 depth-stencil state -- this renderer has no "pipeline with no depth
        // attachment" variant at all (matching how the swapchain's own depthTexture_ is likewise
        // always allocated unconditionally, see RecreateDepthTexture()) -- so, mirroring
        // VulkanRenderer's own documented "always allocates a combined depth+stencil
        // buffer using its device-wide format regardless of the exact value requested"
        // simplification (see IGraphicsRenderer::CreateRenderTarget2D's own doc comment), this
        // render target ALWAYS allocates a real Depth24PlusStencil8 attachment too, even when
        // depthFormat is DepthFormat::None, purely so a 3D draw into it stays pipeline-compatible.
        // This is invisible to game code: HasRealDepthBuffer()'s inherited IRenderTargetRenderer
        // default still reports based on what was actually REQUESTED (the depthFormatWasRequested
        // parameter GraphicsDevice::Clear() computes from RenderTarget2D::
        // getDepthStencilFormatProperty()), not on this internal implementation detail, so
        // GraphicsDevice.Clear(ClearOptions::DepthBuffer) is still correctly masked out for a
        // target that requested DepthFormat::None, exactly as it is on every other renderer.
        (void) depthFormat;
        WGPUTextureDescriptor depthDescriptor{};
        depthDescriptor.label = StringView("CNA WebGPU RenderTarget2D DepthStencil");
        depthDescriptor.usage = WGPUTextureUsage_RenderAttachment;
        depthDescriptor.dimension = WGPUTextureDimension_2D;
        depthDescriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        depthDescriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthDescriptor.mipLevelCount = 1;
        // WEBGPU-58: matches the colour attachment's own sample count exactly, whatever that ends
        // up being below (wgpu-native validation requires every attachment in a render pass to
        // agree) -- 1 outside MSAA, identical to this texture's behaviour before MSAA existed.
        depthDescriptor.sampleCount = static_cast<std::uint32_t>(owner_->sampleCount_);
        depthTexture_ = wgpuDeviceCreateTexture(owner_->Device(), &depthDescriptor);
        if (depthTexture_ == nullptr)
        {
            wgpuTextureViewRelease(colorView_);
            wgpuTextureRelease(colorTexture_);
            colorView_ = nullptr;
            colorTexture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D depth-stencil texture");
        }
        depthView_ = wgpuTextureCreateView(depthTexture_, nullptr);
        if (depthView_ == nullptr)
        {
            wgpuTextureRelease(depthTexture_);
            wgpuTextureViewRelease(colorView_);
            wgpuTextureRelease(colorTexture_);
            depthTexture_ = nullptr;
            colorView_ = nullptr;
            colorTexture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D depth-stencil view");
        }

        // WEBGPU-58: mirror the owner's CURRENT global sampleCount_ unconditionally -- see this
        // class's own top-of-class doc comment (WebGPURenderer.hpp) for why the per-instance
        // requested multiSampleCount argument is intentionally not read here at all. colorTexture_/
        // colorView_ above stay single-sample regardless -- they become the resolve
        // destination (still what View()/GetData() read from) once this multisampled texture
        // exists alongside them.
        if (owner_->sampleCount_ > 1)
        {
            WGPUTextureDescriptor msaaDescriptor{};
            msaaDescriptor.label = StringView("CNA WebGPU RenderTarget2D MSAA Colour");
            msaaDescriptor.usage = WGPUTextureUsage_RenderAttachment;
            msaaDescriptor.dimension = WGPUTextureDimension_2D;
            msaaDescriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
            msaaDescriptor.format = colorFormat_;
            msaaDescriptor.mipLevelCount = 1;
            msaaDescriptor.sampleCount = static_cast<std::uint32_t>(owner_->sampleCount_);
            msaaColorTexture_ = wgpuDeviceCreateTexture(owner_->Device(), &msaaDescriptor);
            if (msaaColorTexture_ == nullptr)
            {
                wgpuTextureViewRelease(depthView_);
                wgpuTextureRelease(depthTexture_);
                wgpuTextureViewRelease(colorView_);
                wgpuTextureRelease(colorTexture_);
                depthView_ = nullptr;
                depthTexture_ = nullptr;
                colorView_ = nullptr;
                colorTexture_ = nullptr;
                throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D MSAA colour texture");
            }
            msaaColorView_ = wgpuTextureCreateView(msaaColorTexture_, nullptr);
            if (msaaColorView_ == nullptr)
            {
                wgpuTextureRelease(msaaColorTexture_);
                wgpuTextureViewRelease(depthView_);
                wgpuTextureRelease(depthTexture_);
                wgpuTextureViewRelease(colorView_);
                wgpuTextureRelease(colorTexture_);
                msaaColorTexture_ = nullptr;
                depthView_ = nullptr;
                depthTexture_ = nullptr;
                colorView_ = nullptr;
                colorTexture_ = nullptr;
                throw std::runtime_error("CNA WebGPU: failed to create RenderTarget2D MSAA colour view");
            }
            appliedMultiSampleCount_ = owner_->sampleCount_;
        }
        sampled_ = std::make_shared<const WebGPUSampledResourceEXT>(colorTexture_, colorView_);
    }

    WebGPURenderTargetRenderer::~WebGPURenderTargetRenderer()
    {
        // RenderTarget2D::Dispose() (Task 717's own precedent) already refuses to dispose a
        // render target still bound on its GraphicsDevice, so this should never actually trigger
        // in practice -- kept as defense-in-depth, mirroring VulkanRenderTargetRenderer's own
        // identical destructor guard.
        if (owner_ != nullptr && owner_->currentRenderTarget_ == this)
            owner_->currentRenderTarget_ = nullptr;
        if (msaaColorView_ != nullptr) wgpuTextureViewRelease(msaaColorView_);
        if (msaaColorTexture_ != nullptr) wgpuTextureRelease(msaaColorTexture_);
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        if (colorView_ != nullptr) wgpuTextureViewRelease(colorView_);
        if (colorTexture_ != nullptr) wgpuTextureRelease(colorTexture_);
    }

    void WebGPURenderTargetRenderer::BindAsRenderTarget()
    {
        // Pure bookkeeping -- mirrors VulkanRenderTargetRenderer::BindAsRenderTarget()'s identical
        // role. The real work (flushing whatever was queued for the PREVIOUS target into its own
        // render pass) already happened in WebGPURenderer::SetRenderTarget2D() before this
        // is called; see that function's own comment.
        if (owner_ != nullptr) owner_->currentRenderTarget_ = this;
    }

    void WebGPURenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr && owner_->currentRenderTarget_ == this) owner_->currentRenderTarget_ = nullptr;
    }

    bool WebGPURenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        // REMED-GFX-127: see WebGPUTextureRenderer::GetData -- nothing written means false, never a
        // silently successful return.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (level != 0)
            throw std::invalid_argument("CNA WebGPU: RenderTarget2D.GetData: mip level > 0 is not "
                                        "supported on this renderer (no mip chain, see "
                                        "plans/plan_webgpu.md WEBGPU-53/54)");

        // Mirrors WebGPURenderer::ReadBackbuffer()'s own on-demand-flush pattern -- if
        // this render target is STILL the currently bound target, render whatever has been
        // queued into it so far before reading it back, so a SetRenderTarget(rt)+Clear()/draw+
        // GetData() sequence observes its own content without an intervening SetRenderTarget
        // (nullptr) switch first.
        if (owner_->currentRenderTarget_ == this)
            owner_->RenderPendingDrawsToRenderTarget(const_cast<WebGPURenderTargetRenderer*>(this));

        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(width_) * 4u);
        const std::uint64_t bufferSize = static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(height_);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = StringView("CNA WebGPU RenderTarget2D Readback Buffer");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = bufferSize;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(owner_->Device(), &bufferDescriptor);
        if (readbackBuffer == nullptr)
            throw std::runtime_error("CNA WebGPU: RenderTarget2D.GetData: failed to create readback buffer");

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTarget2D Readback Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(owner_->Device(), &encoderDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = colorTexture_;
        source.mipLevel = 0;
        source.origin = WGPUOrigin3D{0, 0, 0};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(height_);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU RenderTarget2D Readback Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(owner_->Queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);
        WaitForCompletion(owner_->Instance(), mapState.completed, "RenderTarget2D readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readbackBuffer);
            throw std::runtime_error("CNA WebGPU: RenderTarget2D.GetData: readback buffer map "
                                     "failed: " + mapState.error);
        }

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
        const bool isBgra = (colorFormat_ == WGPUTextureFormat_BGRA8Unorm ||
                             colorFormat_ == WGPUTextureFormat_BGRA8UnormSrgb);
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t requiredLength = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        // REMED-GFX-127: was a memset-to-zero-and-return-as-success path; report the failure and
        // leave the destination alone instead.
        if (mapped == nullptr || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredLength)
        {
            wgpuBufferUnmap(readbackBuffer);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }
        for (int row = 0; row < h; ++row)
        {
            const int sy = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int sx = x + col;
                std::uint8_t* d = out + (static_cast<std::size_t>(row) * w + col) * 4;
                if (sx < 0 || sx >= width_ || sy < 0 || sy >= height_)
                {
                    d[0] = d[1] = d[2] = d[3] = 0;
                    continue;
                }
                const std::uint8_t* s = mapped + static_cast<std::size_t>(sy) * bytesPerRow +
                                        static_cast<std::size_t>(sx) * 4;
                if (isBgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            }
        }
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return true;
    }

    WebGPUVertexBufferRenderer::WebGPUVertexBufferRenderer(WebGPURenderer& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(std::max(0, vertexCapacity))
    {
    }

    WebGPUVertexBufferRenderer::~WebGPUVertexBufferRenderer()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUVertexBufferRenderer::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions{});
    }

    // WEBGPU-44: the SetDataOptions argument (None/Discard/NoOverwrite) is intentionally not
    // branched on. Discard exists so an immediate-mode renderer can orphan a buffer the GPU is
    // still reading, and NoOverwrite so it can append without a stall. Neither hazard reaches this
    // renderer: every draw SNAPSHOTS its vertex bytes into its own ColoredDrawCommand/
    // InstancedDrawCommand at queue time (QueueColoredDraw/DrawInstancedPrimitivesEx copy
    // ShadowData()) and uploads that snapshot into a fresh per-draw WGPUBuffer at replay, so two
    // draws issued between two SetData calls already hold independent copies -- a stronger
    // guarantee than orphaning. And wgpuQueueWriteBuffer is a queue-ordered, non-stalling copy, so
    // there is no CPU stall for Discard to avoid. All three options therefore produce identical,
    // correct results (verified by WebGPU_SetDataOptions); mirrors VulkanRenderer, which likewise
    // does not branch buffer uploads on the option.
    void WebGPUVertexBufferRenderer::SetDataWithOptions(const void* data,
                                                        int vertexCount,
                                                        std::size_t strideInBytes,
                                                        SetDataOptions)
    {
        if (vertexCount < 0 || strideInBytes == 0)
            throw std::invalid_argument("CNA WebGPU: invalid vertex buffer upload");
        if (vertexCount == 0)
            return;
        if (data == nullptr)
            throw std::invalid_argument("CNA WebGPU: invalid vertex buffer upload");
        if (vertexCount > vertexCapacity_)
            throw std::out_of_range("CNA WebGPU: vertex buffer upload exceeds declared capacity");

        const auto unsignedCount = static_cast<std::uint64_t>(vertexCount);
        if (strideInBytes > std::numeric_limits<std::uint64_t>::max() / unsignedCount)
            throw std::out_of_range("CNA WebGPU: vertex buffer upload byte count overflow");
        const std::uint64_t logicalBytes = unsignedCount * strideInBytes;
        if (logicalBytes > std::numeric_limits<std::uint64_t>::max() - 3u ||
            logicalBytes > std::numeric_limits<std::size_t>::max())
        {
            throw std::out_of_range("CNA WebGPU: vertex buffer upload byte count overflow");
        }
        const std::uint64_t required = Align4(logicalBytes);
        if (buffer_ == nullptr || required > capacityBytes_)
        {
            if (buffer_ != nullptr)
                wgpuBufferRelease(buffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU VertexBuffer");
            descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            descriptor.size = required;
            buffer_ = wgpuDeviceCreateBuffer(owner_->Device(), &descriptor);
            capacityBytes_ = required;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        // wgpuQueueWriteBuffer requires a four-byte-aligned copy size. Keep native padding
        // internal: the public count, stride, and deferred-draw shadow retain exactly the logical
        // bytes supplied by the caller.
        shadowData_.clear();
        shadowData_.reserve(static_cast<std::size_t>(required));
        shadowData_.insert(
            shadowData_.end(), bytes, bytes + static_cast<std::size_t>(logicalBytes));
        shadowData_.resize(static_cast<std::size_t>(required), 0);
        wgpuQueueWriteBuffer(
            owner_->Queue(), buffer_, 0, shadowData_.data(), static_cast<std::size_t>(required));
        shadowData_.resize(static_cast<std::size_t>(logicalBytes));
        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    WebGPUIndexBufferRenderer::WebGPUIndexBufferRenderer(WebGPURenderer& owner,
                                                        int indexCapacity,
                                                        bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(std::max(0, indexCapacity)), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    WebGPUIndexBufferRenderer::~WebGPUIndexBufferRenderer()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUIndexBufferRenderer::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferRenderer::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true); }
    // WEBGPU-44: index uploads ignore SetDataOptions for the same reason vertex uploads do -- each
    // draw snapshots its index bytes (command.indexData = ShadowData()) at queue time, and the
    // queue write is non-stalling; see WebGPUVertexBufferRenderer::SetDataWithOptions above.
    void WebGPUIndexBufferRenderer::SetData16WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferRenderer::SetData32WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, true); }

    void WebGPUIndexBufferRenderer::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit)
    {
        if (indexCount < 0 || dataIsThirtyTwoBit != thirtyTwoBit_)
            throw std::invalid_argument("CNA WebGPU: invalid index buffer upload");
        if (indexCount == 0)
            return;
        if (data == nullptr)
            throw std::invalid_argument("CNA WebGPU: invalid index buffer upload");
        if (indexCount > indexCapacity_)
            throw std::out_of_range("CNA WebGPU: index buffer upload exceeds declared capacity");

        const std::size_t stride = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t logicalBytes = static_cast<std::size_t>(indexCount) * stride;
        const std::uint64_t required = Align4(logicalBytes);
        if (buffer_ == nullptr || required > capacityBytes_)
        {
            if (buffer_ != nullptr)
                wgpuBufferRelease(buffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU IndexBuffer");
            descriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            descriptor.size = required;
            buffer_ = wgpuDeviceCreateBuffer(owner_->Device(), &descriptor);
            capacityBytes_ = required;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        // queue.WriteBuffer requires a four-byte-aligned copy size. Reuse the shadow allocation
        // that this deferred renderer already needs, padding only the native write while keeping
        // the public/logical byte count exact.
        shadowData_.clear();
        shadowData_.reserve(static_cast<std::size_t>(required));
        shadowData_.insert(shadowData_.end(), bytes, bytes + logicalBytes);
        shadowData_.resize(static_cast<std::size_t>(required), 0);
        wgpuQueueWriteBuffer(owner_->Queue(), buffer_, 0, shadowData_.data(),
                             static_cast<std::size_t>(required));
        shadowData_.resize(logicalBytes);
        indexCount_ = indexCount;
    }

    WebGPUSpriteBatchRenderer::WebGPUSpriteBatchRenderer(WebGPURenderer& owner) : owner_(&owner) {}

    void WebGPUSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.Begin called twice without End");
        // REMED-GFX-102: SpriteBatch::Begin has already atomically applied its by-value
        // BlendState (including BlendFactor) to GraphicsDevice. Capture that complete normalized
        // state now, before Deferred sorting postpones the renderer Draw calls until End().
        blendSnapshot_ = owner_->CaptureSpriteBlendSnapshot();
        begun_ = true;
    }

    void WebGPUSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void WebGPUSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // WEBGPU-142: SpriteBatch.Begin(..., effect) routes every sprite of this batch through a
        // custom WGSL ShaderEffect (the same WebGPUEffectRenderer the 3D route uses). Only a
        // ShaderEffect is supported here -- a compiled Effect-Framework effect has no WGSL to run,
        // so its GetEffectRendererPtr() is not a WebGPUEffectRenderer and the draw is refused.
        if (effect == nullptr)
        {
            owner_->activeSpriteCustomEffect_ = nullptr;
            return;
        }
        auto* effectRenderer = dynamic_cast<WebGPUEffectRenderer*>(effect->GetEffectRendererPtr());
        if (effectRenderer == nullptr)
            throw System::NotSupportedException(
                "CNA WebGPU: SpriteBatch.Begin was given an effect this renderer cannot run. Only a "
                "custom-WGSL ShaderEffect is supported for SpriteBatch; compiled Effect-Framework "
                "effects are not.");
        owner_->activeSpriteCustomEffect_ = effectRenderer;
    }

    void WebGPUSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void WebGPUSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
             SpriteEffects::None, 0.0f);
    }

    void WebGPUSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        if (!begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.Draw called outside Begin/End");
        // WEBGPU-53/54: resolves either a plain WebGPUTextureRenderer or a WebGPURenderTargetRenderer
        // (a RenderTarget2D sampled back as an ordinary texture) -- see IWebGPUSamplable's own doc
        // comment for why a single dynamic_cast<const WebGPUTextureRenderer*> no longer covers every
        // valid case now that this renderer has a second ITextureRenderer-implementing class.
        const auto* samplable = dynamic_cast<const IWebGPUSamplable*>(&texture);
        if (samplable == nullptr)
            throw std::invalid_argument("CNA WebGPU: SpriteBatch received a texture from another graphics renderer");
        owner_->QueueSprite(texture, *samplable, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_,
                            blendSnapshot_);
    }

    WebGPURenderer::WebGPURenderer(const GraphicsRendererCreateArgs& args)
        : surfaceState_(args.surface, "WebGPURenderer"),
          virtualWidth_(args.virtualWidth),
          virtualHeight_(args.virtualHeight),
          presentationMode_(args.presentationMode),
          swapInterval_(args.swapInterval)
    {
        instance_ = wgpuCreateInstance(nullptr);
        if (instance_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuCreateInstance failed");

        try
        {
            CreateSurface();
            RequestAdapterAndDevice();
            ConfigureSurface(true);
            IGraphicsRenderer::RegisterForWindow(surfaceState_.GetWindowId(), this);
        }
        catch (...)
        {
            DestroySpriteResources();
            ReleaseSamplerCache();
            if (msaaColorView_ != nullptr) wgpuTextureViewRelease(msaaColorView_);
            if (msaaColorTexture_ != nullptr) wgpuTextureRelease(msaaColorTexture_);
            if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
            if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
            if (surfaceConfigured_ && surface_ != nullptr) wgpuSurfaceUnconfigure(surface_);
            if (queue_ != nullptr) wgpuQueueRelease(queue_);
            if (device_ != nullptr) wgpuDeviceRelease(device_);
            if (adapter_ != nullptr) wgpuAdapterRelease(adapter_);
            if (surface_ != nullptr) wgpuSurfaceRelease(surface_);
            if (instance_ != nullptr) wgpuInstanceRelease(instance_);
#if defined(__APPLE__)
            DestroyWebGPUMetalLayer(metalSurfaceOwner_);
#endif
            throw;
        }
    }

    WebGPURenderer::~WebGPURenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surfaceState_.GetWindowId());
        // REMED-GFX-167: FIRST, before any native handle below is released. A queued command holds
        // a reference to the texture it samples, and these vectors are members -- destroyed after
        // this body, i.e. after device_/adapter_/instance_ are gone. A frame abandoned rather than
        // presented (surface acquisition failure) is the path that can still hold commands here.
        DiscardQueuedCommands();
        DestroySpriteResources();
        DestroyColoredResources();
        DestroyTexturedResources();
        DestroyLitTexturedResources();
        DestroyAlphaTestResources();
        DestroyDualTextureResources();
        DestroyEnvMapResources();
        DestroyInstancedResources();
        DestroyPbrResources();
        DestroySkinnedResources();
        DestroySkinnedPbrResources();
        pbrDefaultWhiteTexture_.reset();
        pbrDefaultFlatNormalTexture_.reset();
        envMapDefaultWhiteTexture_.reset();
        envMapDefaultWhiteCube_.reset();
        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        ReleaseSamplerCache();
        // WEBGPU-52: mip-blit resources -- see EnsureMipBlitPipeline()'s own doc comment.
        if (mipBlitPipeline_ != nullptr) wgpuRenderPipelineRelease(mipBlitPipeline_);
        if (mipBlitPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(mipBlitPipelineLayout_);
        if (mipBlitBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(mipBlitBindGroupLayout_);
        if (mipBlitShader_ != nullptr) wgpuShaderModuleRelease(mipBlitShader_);
        if (mipBlitSampler_ != nullptr) wgpuSamplerRelease(mipBlitSampler_);
        if (msaaColorView_ != nullptr) wgpuTextureViewRelease(msaaColorView_);
        if (msaaColorTexture_ != nullptr) wgpuTextureRelease(msaaColorTexture_);
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        if (readbackBuffer_ != nullptr) wgpuBufferRelease(readbackBuffer_);
        // WEBGPU-84: the shared occlusion query set and its resolve/readback buffers.
        if (occlusionReadbackBuffer_ != nullptr) wgpuBufferRelease(occlusionReadbackBuffer_);
        if (occlusionResolveBuffer_ != nullptr) wgpuBufferRelease(occlusionResolveBuffer_);
        if (occlusionQuerySet_ != nullptr) wgpuQuerySetRelease(occlusionQuerySet_);
        if (hasAcquiredTexture_ && acquiredTexture_ != nullptr) wgpuTextureRelease(acquiredTexture_);
        if (surfaceConfigured_ && surface_ != nullptr) wgpuSurfaceUnconfigure(surface_);
        if (queue_ != nullptr) wgpuQueueRelease(queue_);
        if (device_ != nullptr) wgpuDeviceRelease(device_);
        if (adapter_ != nullptr) wgpuAdapterRelease(adapter_);
        if (surface_ != nullptr) wgpuSurfaceRelease(surface_);
        if (instance_ != nullptr) wgpuInstanceRelease(instance_);
#if defined(__APPLE__)
        DestroyWebGPUMetalLayer(metalSurfaceOwner_);
#endif
    }

    void WebGPURenderer::CreateSurface()
    {
        WGPUSurfaceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Surface");
        const auto& nativeHandle = surfaceState_.GetNativeHandle();

#if defined(_WIN32)
        CNA::Platform::Win32NativeWindow native;
        if (!CNA::Platform::TryGetWin32(nativeHandle, native))
            throw std::runtime_error("CNA WebGPU: a Win32 native window is required");
        WGPUSurfaceSourceWindowsHWND source{};
        source.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        source.hinstance = GetModuleHandleW(nullptr);
        source.hwnd = native.hwnd;
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__APPLE__)
        const auto drawableSize = surfaceState_.GetDrawableSize();
        WGPUSurfaceSourceMetalLayer source{};
        source.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        source.layer = CreateWebGPUMetalLayer(
            nativeHandle, drawableSize.width, drawableSize.height,
            surfaceState_.GetDisplayScale(), metalSurfaceOwner_);
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__ANDROID__)
        CNA::Platform::AndroidNativeWindow native;
        if (!CNA::Platform::TryGetAndroid(nativeHandle, native))
            throw std::runtime_error("CNA WebGPU: an Android native window is required");
        WGPUSurfaceSourceAndroidNativeWindow source{};
        source.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
        source.window = native.window;
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__linux__)
        CNA::Platform::WaylandNativeWindow wayland;
        CNA::Platform::X11NativeWindow x11;
        if (CNA::Platform::TryGetWayland(nativeHandle, wayland))
        {
            WGPUSurfaceSourceWaylandSurface source{};
            source.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
            source.display = wayland.display;
            source.surface = wayland.surface;
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else if (CNA::Platform::TryGetX11(nativeHandle, x11))
        {
            WGPUSurfaceSourceXlibWindow source{};
            source.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            source.display = x11.display;
            source.window = x11.window;
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else
        {
            throw std::runtime_error("CNA WebGPU: unsupported Linux native window: " +
                                     CNA::Platform::Describe(nativeHandle));
        }
#elif defined(__EMSCRIPTEN__)
        // The browser owns the drawing surface: a <canvas> element chosen by a CSS selector, not a
        // native window handle. SDL3's Emscripten port renders into "#canvas" by default, so the
        // WebGPU surface must target that same element for the two to agree on one canvas.
        (void) nativeHandle;
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
        source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        source.selector = StringView("#canvas");
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#else
        (void) nativeHandle;
        throw std::runtime_error("CNA WebGPU: native surface creation is unsupported on this platform");
#endif

        if (surface_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuInstanceCreateSurface failed");
    }

    void WebGPURenderer::RequestAdapterAndDevice()
    {
        AdapterRequestState adapterState;
        WGPURequestAdapterOptions adapterOptions{};
        adapterOptions.compatibleSurface = surface_;
        WGPURequestAdapterCallbackInfo adapterCallback{};
        adapterCallback.mode = kCnaWebGpuCallbackMode;
        adapterCallback.callback = OnAdapterRequest;
        adapterCallback.userdata1 = &adapterState;
        wgpuInstanceRequestAdapter(instance_, &adapterOptions, adapterCallback);
        WaitForCompletion(instance_, adapterState.completed, "adapter request");
        if (adapterState.adapter == nullptr)
            throw std::runtime_error("CNA WebGPU: adapter request failed: " + adapterState.error);
        adapter_ = adapterState.adapter;

        // WEBGPU-144: request native block-compressed texture support when the adapter has it, so
        // BC1/2/3/7 (DXT/BC7) textures upload as blocks instead of being CPU-decompressed to RGBA8.
        bcSupported_ = wgpuAdapterHasFeature(adapter_, WGPUFeatureName_TextureCompressionBC) != 0;
        std::array<WGPUFeatureName, 1> requiredFeatures{WGPUFeatureName_TextureCompressionBC};

        DeviceRequestState deviceState;
        WGPUDeviceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Device");
        if (bcSupported_)
        {
            descriptor.requiredFeatureCount = requiredFeatures.size();
            descriptor.requiredFeatures = requiredFeatures.data();
        }
        descriptor.defaultQueue.label = StringView("CNA WebGPU Queue");
        descriptor.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
        descriptor.uncapturedErrorCallbackInfo.userdata1 = &uncapturedErrorCount_;
        // WGPUDeviceLostCallbackInfo carries a completion mode that must be a valid enumerant: the
        // browser's emdawnwebgpu rejects a zero-initialised mode ("Invalid WGPUCallbackMode 0"),
        // where wgpu-native tolerates it. Set it to the same mode used for the other async callbacks.
        descriptor.deviceLostCallbackInfo.mode = kCnaWebGpuCallbackMode;
        descriptor.deviceLostCallbackInfo.callback = OnDeviceLost;
        WGPURequestDeviceCallbackInfo callback{};
        callback.mode = kCnaWebGpuCallbackMode;
        callback.callback = OnDeviceRequest;
        callback.userdata1 = &deviceState;
        wgpuAdapterRequestDevice(adapter_, &descriptor, callback);
        WaitForCompletion(instance_, deviceState.completed, "device request");
        if (deviceState.device == nullptr)
            throw std::runtime_error("CNA WebGPU: device request failed: " + deviceState.error);
        device_ = deviceState.device;
        queue_ = wgpuDeviceGetQueue(device_);
        if (queue_ == nullptr)
            throw std::runtime_error("CNA WebGPU: device returned no queue");
    }

    void WebGPURenderer::ConfigureSurface(bool force)
    {
        const auto drawableSize = surfaceState_.GetDrawableSize();
        const int width = drawableSize.width;
        const int height = drawableSize.height;
        if (width <= 0 || height <= 0)
        {
            if (surfaceConfigured_ && surface_ != nullptr)
                wgpuSurfaceUnconfigure(surface_);
            surfaceConfigured_ = false;
            physicalWidth_ = width;
            physicalHeight_ = height;
            RecreateDepthTexture();
            RecreateMsaaColorTexture();
            return;
        }
        if (!force && surfaceConfigured_ && width == physicalWidth_ && height == physicalHeight_)
            return;

        WGPUSurfaceCapabilities capabilities{};
        const WGPUStatus capabilitiesStatus = wgpuSurfaceGetCapabilities(surface_, adapter_, &capabilities);
        if (capabilitiesStatus != WGPUStatus_Success || capabilities.formatCount == 0)
        {
            wgpuSurfaceCapabilitiesFreeMembers(capabilities);
            throw std::runtime_error("CNA WebGPU: failed to query surface capabilities");
        }

        // REMED-GFX-131: prefer a NON-sRGB surface format, exactly as VulkanRenderer::
        // CreateSwapchain() already does for the identical decision, and for the identical reason:
        // XNA's SurfaceFormat.Color -- the default backbuffer format, and the only format the
        // IGraphicsRenderer render-target entry points can even express -- is a plain 8-bit UNORM
        // byte format, while SurfaceFormat.ColorSrgbEXT is the separate gamma-encoded one. An sRGB
        // surface format makes the hardware encode every value the render pass stores, and this
        // renderer copies the surface format into every render target and every pipeline's colour
        // target, so that encode reached offscreen resources whose bytes a game reads back.
        constexpr WGPUTextureFormat preferredFormats[] = {
            WGPUTextureFormat_BGRA8Unorm,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureFormat_BGRA8UnormSrgb,
            WGPUTextureFormat_RGBA8UnormSrgb
        };
        WGPUTextureFormat chosenFormat = capabilities.formats[0];
        for (const auto format : preferredFormats)
        {
            if (HasSurfaceFormat(capabilities, format))
            {
                chosenFormat = format;
                break;
            }
        }

        // The colour format CNA renders in. Equal to chosenFormat whenever the surface offers a
        // non-sRGB format at all; otherwise the sRGB format's own non-sRGB counterpart, requested
        // below through viewFormats so every view this renderer creates over a surface texture
        // reinterprets those same bytes without a transfer function. This keeps SurfaceFormat::
        // Color byte-exact even on a surface that can only be CONFIGURED as sRGB, and costs
        // nothing: no extra pipeline, pass, texture, submit or per-pixel conversion.
        const WGPUTextureFormat renderFormat = NonSrgbColorFormat(chosenFormat);
        const bool needsViewReinterpretation = renderFormat != chosenFormat;

        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        if (swapInterval_ == 0)
        {
            if (HasPresentMode(capabilities, WGPUPresentMode_Immediate))
                presentMode = WGPUPresentMode_Immediate;
            else if (HasPresentMode(capabilities, WGPUPresentMode_Mailbox))
                presentMode = WGPUPresentMode_Mailbox;
        }
        else if (!HasPresentMode(capabilities, presentMode) && capabilities.presentModeCount > 0)
        {
            presentMode = capabilities.presentModes[0];
        }

        const WGPUCompositeAlphaMode alphaMode = capabilities.alphaModeCount > 0
            ? capabilities.alphaModes[0]
            : WGPUCompositeAlphaMode_Auto;

        const bool formatChanged = surfaceFormat_ != renderFormat;
        surfaceFormat_ = renderFormat;
        surfaceConfiguredFormat_ = chosenFormat;
        surfaceConfig_ = {};
        surfaceConfig_.device = device_;
        surfaceConfig_.format = surfaceConfiguredFormat_;
        surfaceConfig_.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        surfaceConfig_.width = static_cast<std::uint32_t>(width);
        surfaceConfig_.height = static_cast<std::uint32_t>(height);
        // Must outlive wgpuSurfaceConfigure(): WGPUSurfaceConfiguration only borrows the array.
        // surfaceConfig_ is a member, so this pointer is deliberately kept valid for as long as
        // the configuration it belongs to is.
        surfaceViewFormats_[0] = surfaceFormat_;
        surfaceConfig_.viewFormatCount = needsViewReinterpretation ? 1u : 0u;
        surfaceConfig_.viewFormats = needsViewReinterpretation ? surfaceViewFormats_.data() : nullptr;
        surfaceConfig_.presentMode = presentMode;
        surfaceConfig_.alphaMode = alphaMode;
        wgpuSurfaceConfigure(surface_, &surfaceConfig_);
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);

        physicalWidth_ = width;
        physicalHeight_ = height;
        surfaceConfigured_ = true;
        RecreateDepthTexture();
        RecreateMsaaColorTexture();
        if (formatChanged || spriteShader_ == nullptr)
            CreateSpriteResources();
        if (formatChanged || coloredShader_ == nullptr)
            CreateColoredResources();
        if (formatChanged || texturedShader_ == nullptr)
            CreateTexturedResources();
        if (formatChanged || litTexturedShader_ == nullptr)
            CreateLitTexturedResources();
        if (formatChanged || alphaTestShader_ == nullptr)
            CreateAlphaTestResources();
        if (formatChanged || dualTextureShader_ == nullptr)
            CreateDualTextureResources();
        if (formatChanged || envMapShader_ == nullptr)
            CreateEnvMapResources();
        // WEBGPU-27/38/68: reuses coloredPipelineLayout_/coloredBindGroupLayout_, which
        // CreateColoredResources() above already guarantees exist by this point.
        if (formatChanged || instancedShader_ == nullptr)
            CreateInstancedResources();
        if (formatChanged || pbrShader_ == nullptr)
            CreatePbrResources();
        if (formatChanged || skinnedShader_ == nullptr)
            CreateSkinnedResources();
        // Must run after CreatePbrResources(): reuses pbrBindGroupLayout1_ (group 1: sampler + 5
        // textures) unchanged for its own texture bind group.
        if (formatChanged || skinnedPbrShader_ == nullptr)
            CreateSkinnedPbrResources();
    }

    void WebGPURenderer::RecreateDepthTexture()
    {
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        depthView_ = nullptr;
        depthTexture_ = nullptr;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return;

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU DepthStencil");
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(physicalWidth_),
                                       static_cast<std::uint32_t>(physicalHeight_), 1};
        descriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        descriptor.mipLevelCount = 1;
        // WEBGPU-58: must match the colour attachment's own sample count exactly (wgpu-native
        // validation requires every attachment in a render pass to agree) -- 1 outside MSAA,
        // identical to this texture's behaviour before MSAA existed.
        descriptor.sampleCount = static_cast<std::uint32_t>(sampleCount_);
        depthTexture_ = wgpuDeviceCreateTexture(device_, &descriptor);
        if (depthTexture_ != nullptr)
            depthView_ = wgpuTextureCreateView(depthTexture_, nullptr);
    }

    void WebGPURenderer::RecreateMsaaColorTexture()
    {
        if (msaaColorView_ != nullptr) wgpuTextureViewRelease(msaaColorView_);
        if (msaaColorTexture_ != nullptr) wgpuTextureRelease(msaaColorTexture_);
        msaaColorView_ = nullptr;
        msaaColorTexture_ = nullptr;
        if (sampleCount_ <= 1 || physicalWidth_ <= 0 || physicalHeight_ <= 0 ||
            surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Backbuffer MSAA Colour");
        // RENDER_ATTACHMENT only: this texture is never sampled directly (the swapchain's own
        // single-sample texture is the only thing ever read back/presented -- see
        // EnsureFrameRendered()'s resolveTarget usage), so no TextureBinding usage is needed.
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(physicalWidth_),
                                       static_cast<std::uint32_t>(physicalHeight_), 1};
        descriptor.format = surfaceFormat_;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = static_cast<std::uint32_t>(sampleCount_);
        msaaColorTexture_ = wgpuDeviceCreateTexture(device_, &descriptor);
        if (msaaColorTexture_ != nullptr)
            msaaColorView_ = wgpuTextureCreateView(msaaColorTexture_, nullptr);
    }

    void WebGPURenderer::DestroySpriteResources()
    {
        if (spriteVertexBuffer_ != nullptr) wgpuBufferRelease(spriteVertexBuffer_);
        for (auto& [key, pipeline] : spritePipelines_)
        {
            (void) key;
            if (pipeline != nullptr) wgpuRenderPipelineRelease(pipeline);
        }
        spritePipelines_.clear();
        if (spritePipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(spritePipelineLayout_);
        if (spriteBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(spriteBindGroupLayout_);
        if (spriteShader_ != nullptr) wgpuShaderModuleRelease(spriteShader_);
        spriteVertexBuffer_ = nullptr;
        spritePipelineLayout_ = nullptr;
        spriteBindGroupLayout_ = nullptr;
        spriteShader_ = nullptr;
        spriteVertexCapacityBytes_ = 0;
    }

    void WebGPURenderer::CreateSpriteResources()
    {
        DestroySpriteResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        static constexpr char shaderSource[] = R"WGSL(
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
@group(0) @binding(0) var spriteSampler: sampler;
@group(0) @binding(1) var spriteTexture: texture_2d<f32>;
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(spriteTexture, spriteSampler, input.uv) * input.color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU SpriteBatch WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        spriteShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU SpriteBatch BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        spriteBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU SpriteBatch PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &spriteBindGroupLayout_;
        spritePipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        // REMED-GFX-102: pipelines are now created lazily once per complete STATIC compatibility
        // key by GetOrCreateSpritePipeline(). This function creates only the shared shader/layout
        // resources; there are no longer two fixed opaque/straight-alpha pipelines.
        if (spriteShader_ == nullptr || spriteBindGroupLayout_ == nullptr ||
            spritePipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SpriteBatch shared GPU resources");
    }

    WebGPUSpriteBlendSnapshot WebGPURenderer::CaptureSpriteBlendSnapshot() const
    {
        WebGPUSpriteBlendSnapshot snapshot;
        snapshot.blendEnabled = blendEnabled_;
        snapshot.colorSrc = blendParams_.colorSrc;
        snapshot.colorDst = blendParams_.colorDst;
        snapshot.alphaSrc = blendParams_.alphaSrc;
        snapshot.alphaDst = blendParams_.alphaDst;
        snapshot.colorFunc = blendParams_.colorFunc;
        snapshot.alphaFunc = blendParams_.alphaFunc;
        snapshot.colorWriteMask = colorWriteMask_ & 0xF;
        snapshot.multiSampleMask = sampleMask_;
        snapshot.blendFactorR = blendFactorR_;
        snapshot.blendFactorG = blendFactorG_;
        snapshot.blendFactorB = blendFactorB_;
        snapshot.blendFactorA = blendFactorA_;

        // Target compatibility is captured by VALUE too. Object/face identity is deliberately
        // absent: compatible backbuffers, RenderTarget2D instances, and cube faces reuse a cache
        // entry. Render-target switches flush pending commands before changing these pointers.
        if (currentRenderTarget_ != nullptr)
        {
            snapshot.targetFormat = currentRenderTarget_->ColorFormat();
            snapshot.sampleCount = static_cast<std::uint32_t>(
                std::max(1, currentRenderTarget_->GetMultiSampleCount()));
        }
        else if (currentRenderTargetCubeFace_ != nullptr)
        {
            snapshot.targetFormat = currentRenderTargetCubeFace_->ColorFormat();
            snapshot.sampleCount = 1;
        }
        else
        {
            snapshot.targetFormat = surfaceFormat_;
            snapshot.sampleCount = static_cast<std::uint32_t>(std::max(1, sampleCount_));
        }
        return snapshot;
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreateSpritePipeline(
        const WebGPUSpriteBlendSnapshot& snapshot)
    {
        SpritePipelineKey key;
        key.blendEnabled = snapshot.blendEnabled;
        key.colorSrc = snapshot.colorSrc;
        key.colorDst = snapshot.colorDst;
        key.alphaSrc = snapshot.alphaSrc;
        key.alphaDst = snapshot.alphaDst;
        key.colorFunc = snapshot.colorFunc;
        key.alphaFunc = snapshot.alphaFunc;
        key.colorWriteMask = snapshot.colorWriteMask & 0xF;
        key.multiSampleMask = snapshot.multiSampleMask;
        key.targetFormat = snapshot.targetFormat;
        key.sampleCount = snapshot.sampleCount;
        key.colorAttachmentCount = replayColorAttachmentCount_;  // WEBGPU-86 MRT (see ExpandStockColorTargetsEXT)

        if (const auto found = spritePipelines_.find(key); found != spritePipelines_.end())
            return found->second;
        if (spriteShader_ == nullptr || spritePipelineLayout_ == nullptr ||
            snapshot.targetFormat == WGPUTextureFormat_Undefined || snapshot.sampleCount == 0)
            throw std::runtime_error("CNA WebGPU: invalid SpriteBatch pipeline compatibility state");

        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(SpriteVertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[1].offset = offsetof(SpriteVertex, uv);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        attributes[2].offset = offsetof(SpriteVertex, color);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(SpriteVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = snapshot.targetFormat;
        target.writeMask = static_cast<WGPUColorWriteMask>(snapshot.colorWriteMask & 0xF);
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        const BlendKeyParams blendParams{
            snapshot.colorSrc, snapshot.colorDst, snapshot.alphaSrc, snapshot.alphaDst,
            snapshot.colorFunc, snapshot.alphaFunc
        };
        FillWGPUBlendState(blendState, blendParams);
        target.blend = snapshot.blendEnabled ? &blendState : nullptr;
        WGPUFragmentState fragment{};
        fragment.module = spriteShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU SpriteBatch Pipeline");
        pipeline.layout = spritePipelineLayout_;
        pipeline.vertex.module = spriteShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = snapshot.sampleCount;
        pipeline.multisample.mask = snapshot.multiSampleMask;
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        // Present() always uses the shared Depth24PlusStencil8 attachment. The SpriteBatch
        // pipeline must declare the same attachment format even though 2D sprites do not write
        // depth, otherwise wgpu-native rejects the render pass as incompatible.
        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        depthStencil.depthCompare = WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        // BlendState::AlphaBlend reaches FillWGPUBlendState as One/InverseSourceAlpha for both
        // colour/alpha, preserving CNA/XNA's premultiplied-alpha convention. NonPremultiplied is
        // the explicit SourceAlpha variant; no shader mutation is needed or made.
        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create keyed SpriteBatch pipeline");
        spritePipelines_.emplace(key, created);
        std::fprintf(stderr, "[WebGPU][GFX-102] Sprite pipeline cache miss -> size=%zu blend=%d "
                "color=(%d,%d,%d) alpha=(%d,%d,%d) writeMask=0x%X sampleMask=0x%08X "
                "format=%d samples=%u\n",
                spritePipelines_.size(), snapshot.blendEnabled ? 1 : 0,
                snapshot.colorSrc, snapshot.colorDst, snapshot.colorFunc,
                snapshot.alphaSrc, snapshot.alphaDst, snapshot.alphaFunc,
                snapshot.colorWriteMask & 0xF, snapshot.multiSampleMask,
                static_cast<int>(snapshot.targetFormat), snapshot.sampleCount);
        return created;
    }

    void WebGPURenderer::DestroyColoredResources()
    {
        for (auto& [key, pipe] : coloredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        coloredPipelines_.clear();
        if (coloredPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(coloredPipelineLayout_);
        if (coloredBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(coloredBindGroupLayout_);
        if (coloredShader_ != nullptr) wgpuShaderModuleRelease(coloredShader_);
        coloredPipelineLayout_ = nullptr;
        coloredBindGroupLayout_ = nullptr;
        coloredShader_ = nullptr;
    }

    void WebGPURenderer::CreateColoredResources()
    {
        DestroyColoredResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Uniform layout matches VulkanRenderer::FillExtPushConst()'s 128-byte/32-float
        // push-constant layout byte-for-byte (see plans/plan_webgpu.md's Phase 57 entry-point note):
        // [0..15] MVP, [16..19] diffuseColor, [20..23] ambient+lightingEnabled,
        // [24..27] light0Dir+textureEnabled, [28..31] light0Diffuse+vertexColorEnabled. Only the
        // fields this minimal DrawColoredPrimitives slice actually reads are named; the rest keep
        // the same byte offsets so a future DrawPrimitivesEx (BasicEffect) shader can reuse this
        // exact uniform block unchanged.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.color = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Colored3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        coloredShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        WGPUBindGroupLayoutEntry uniformEntry{};
        uniformEntry.binding = 0;
        uniformEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
        uniformEntry.buffer.minBindingSize = 128;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU Colored3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = 1;
        bindLayoutDescriptor.entries = &uniformEntry;
        coloredBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Colored3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &coloredBindGroupLayout_;
        coloredPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (coloredShader_ == nullptr || coloredBindGroupLayout_ == nullptr || coloredPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Colored3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineColored3D(WGPUPrimitiveTopology topology,
                                                                            WGPUIndexFormat stripIndexFormat,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        // WEBGPU-83: the stencil state is folded into the colored3d cache key locally (Make3DPipelineKey
        // stays shared/unchanged), so a stencil-enabled draw is a distinct pipeline variant.
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = coloredPipelines_.find(key); it != coloredPipelines_.end())
            return it->second;

        struct ColoredVertex { float x, y, z; std::uint8_t r, g, b, a; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(ColoredVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
        attributes[1].offset = offsetof(ColoredVertex, r);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = coloredShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Colored3D Pipeline");
        pipeline.layout = coloredPipelineLayout_;
        pipeline.vertex.module = coloredShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Colored3D pipeline");
        coloredPipelines_[key] = created;
        return created;
    }

    void WebGPURenderer::DestroyTexturedResources()
    {
        for (auto& [key, pipe] : texturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        texturedPipelines_.clear();
        for (auto& [key, pipe] : coloredTexturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        coloredTexturedPipelines_.clear();
        if (texturedPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(texturedPipelineLayout_);
        if (texturedBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(texturedBindGroupLayout_);
        if (texturedShader_ != nullptr) wgpuShaderModuleRelease(texturedShader_);
        if (coloredTexturedShader_ != nullptr) wgpuShaderModuleRelease(coloredTexturedShader_);
        texturedPipelineLayout_ = nullptr;
        texturedBindGroupLayout_ = nullptr;
        texturedShader_ = nullptr;
        coloredTexturedShader_ = nullptr;
    }

    void WebGPURenderer::CreateTexturedResources()
    {
        DestroyTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Uniform layout is the exact same group-0 UBO as colored3d.wgsl (coloredBindGroupLayout_,
        // reused verbatim -- see plans/plan_webgpu.md's WEBGPU-13 note). Group 1 (sampler + texture)
        // mirrors the SpriteBatch bind group layout shape exactly.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    return sampled * u.diffuseColor;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Textured3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        texturedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU Textured3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        texturedBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{coloredBindGroupLayout_, texturedBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Textured3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        texturedPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (texturedShader_ == nullptr || texturedBindGroupLayout_ == nullptr || texturedPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Textured3D GPU resources");

        // WEBGPU-21: colored_textured3d (stride 24, VertexPositionColorTexture) -- same UBO
        // (group 0) and texture (group 1) bind groups as textured3d.wgsl above, just a different
        // vertex layout (adds a per-vertex colour) and shader that mixes it with DiffuseColor
        // before sampling, matching colored3d.wgsl's own vertexColorEnabled mixing formula.
        static constexpr char coloredTexturedShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    return sampled * input.tint;
}
)WGSL";

        WGPUShaderSourceWGSL coloredTexturedWgsl{};
        coloredTexturedWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredTexturedWgsl.code = StringView(coloredTexturedShaderSource);
        WGPUShaderModuleDescriptor coloredTexturedShaderDescriptor{};
        coloredTexturedShaderDescriptor.label = StringView("CNA WebGPU ColoredTextured3D WGSL");
        coloredTexturedShaderDescriptor.nextInChain = &coloredTexturedWgsl.chain;
        coloredTexturedShader_ = wgpuDeviceCreateShaderModule(device_, &coloredTexturedShaderDescriptor);
        if (coloredTexturedShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create ColoredTextured3D shader");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineTextured3D(WGPUPrimitiveTopology topology,
                                                                            WGPUIndexFormat stripIndexFormat,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = texturedPipelines_.find(key); it != texturedPipelines_.end())
            return it->second;

        struct TexturedVertex { float x, y, z; float u, v; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(TexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[1].offset = offsetof(TexturedVertex, u);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(TexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = texturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Textured3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = texturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Textured3D pipeline");
        texturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineColoredTextured3D(WGPUPrimitiveTopology topology,
                                                                                    WGPUIndexFormat stripIndexFormat,
                                                                                    bool depthTest, bool depthWrite,
                                                                                    int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = coloredTexturedPipelines_.find(key); it != coloredTexturedPipelines_.end())
            return it->second;

        struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(ColoredTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
        attributes[1].offset = offsetof(ColoredTexturedVertex, r);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[2].offset = offsetof(ColoredTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = coloredTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU ColoredTextured3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = coloredTexturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create ColoredTextured3D pipeline");
        coloredTexturedPipelines_[key] = created;
        return created;
    }

    void WebGPURenderer::DestroyLitTexturedResources()
    {
        for (auto& [key, pipe] : litTexturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        litTexturedPipelines_.clear();
        // Task 1105: the vertex-lit sibling's own pipeline cache + shader module, torn down
        // alongside the per-pixel-lit one -- litBindGroupLayout_/litPipelineLayout_ are shared by
        // both and only released once, below.
        for (auto& [key, pipe] : litTexturedVertexLitPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        litTexturedVertexLitPipelines_.clear();
        if (litTexturedVertexLitShader_ != nullptr) wgpuShaderModuleRelease(litTexturedVertexLitShader_);
        litTexturedVertexLitShader_ = nullptr;
        if (litPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(litPipelineLayout_);
        if (litBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(litBindGroupLayout_);
        if (litTexturedShader_ != nullptr) wgpuShaderModuleRelease(litTexturedShader_);
        litPipelineLayout_ = nullptr;
        litBindGroupLayout_ = nullptr;
        litTexturedShader_ = nullptr;
    }

    void WebGPURenderer::CreateLitTexturedResources()
    {
        DestroyLitTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanRenderer's lit_textured3d.{vert,frag}.glsl (itself ported from
        // FNA's Lighting.fxh ComputeLights()). Group 0 binding 0 is the same primary Uniforms
        // block as colored3d/textured3d (MVP, diffuseColor, ambientColor+lightingEnabled,
        // light0Dir+textureEnabled, light0Diffuse); binding 1 is the secondary LitLightParams
        // block (light1/light2, emissive, world, eye position, per-light specular, material
        // specular, normal matrix) filled by FillLitLightUniforms(). Group 1 (sampler + texture)
        // is texturedBindGroupLayout_, reused unchanged.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    if (lightingEnabled <= 0.5) {
        return u.diffuseColor * sampled;
    }
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    // A disabled/unconfigured DirectionalLight forwards Direction=(0,0,0) (its DiffuseColor/
    // SpecularColor are what get zeroed, matching FNA's own DirectionalLight.cs -- Direction
    // itself is untouched by Enabled=false). normalize() on a true zero vector is undefined and
    // can poison the whole lightSum/specular computation with NaN on real GPU hardware, even
    // though that light's own diffuse/specular contribution is already zero -- guard it here.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRgb = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let lit = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    var color = vec4f(lit, u.diffuseColor.a) * sampled;
    color = vec4f(color.rgb + specularRgb * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU LitTextured3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        litTexturedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        // Task 1105 (plans/plan_graphics.md Phase 80): real per-vertex-lit sibling -- identical
        // Blinn-Phong math to shaderSource above (FNA's Lighting.fxh ComputeLights()), moved from
        // fs_main into vs_main and passed onward as litRGB/specularRGB varyings (WGSL naturally
        // interpolates any @location(n) VertexOutput field across the triangle -- this alone is
        // what gives Gouraud shading, no separate interpolation logic needed). fs_main keeps the
        // exact same lightingEnabled<=0.5 unlit branch and non-lighting math (texture sample)
        // unchanged, just consuming the interpolated value instead of recomputing it per fragment.
        // Same UBO/binding layout as the per-pixel-lit shader (reuses litBindGroupLayout_/
        // litPipelineLayout_ unchanged below), so only a new shader module is needed here.
        static constexpr char vertexLitShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let worldNormal = normalMatrix * input.normal;
    let worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    let n = normalize(worldNormal);
    let e = normalize(lp.eyePos.xyz - worldPos);
    // Same disabled-light NaN guard as the per-pixel-lit shader: a disabled DirectionalLight
    // forwards Direction=(0,0,0) (only DiffuseColor/SpecularColor are zeroed), and normalize() on
    // a true zero vector is undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    if (lightingEnabled <= 0.5) {
        return u.diffuseColor * sampled;
    }
    var color = vec4f(input.litRGB, u.diffuseColor.a) * sampled;
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL vertexLitWgsl{};
        vertexLitWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        vertexLitWgsl.code = StringView(vertexLitShaderSource);
        WGPUShaderModuleDescriptor vertexLitShaderDescriptor{};
        vertexLitShaderDescriptor.label = StringView("CNA WebGPU LitTextured3D VertexLit WGSL");
        vertexLitShaderDescriptor.nextInChain = &vertexLitWgsl.chain;
        litTexturedVertexLitShader_ = wgpuDeviceCreateShaderModule(device_, &vertexLitShaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[0].buffer.minBindingSize = 128;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[1].buffer.minBindingSize = 272;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU LitTextured3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        litBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{litBindGroupLayout_, texturedBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU LitTextured3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        litPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (litTexturedShader_ == nullptr || litBindGroupLayout_ == nullptr || litPipelineLayout_ == nullptr ||
            litTexturedVertexLitShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineLitTextured3D(WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = litTexturedPipelines_.find(key); it != litTexturedPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = litTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU LitTextured3D Pipeline");
        pipeline.layout = litPipelineLayout_;
        pipeline.vertex.module = litTexturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D pipeline");
        litTexturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineLitTextured3DVertexLit(
        WGPUPrimitiveTopology topology, WGPUIndexFormat stripIndexFormat,
        bool depthTest, bool depthWrite, int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = litTexturedVertexLitPipelines_.find(key); it != litTexturedVertexLitPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = litTexturedVertexLitShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU LitTextured3D VertexLit Pipeline");
        pipeline.layout = litPipelineLayout_;
        pipeline.vertex.module = litTexturedVertexLitShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D VertexLit pipeline");
        litTexturedVertexLitPipelines_[key] = created;
        return created;
    }

    void WebGPURenderer::DestroyAlphaTestResources()
    {
        for (auto& [key, pipe] : alphaTestPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        alphaTestPipelines_.clear();
        for (auto& [key, pipe] : alphaTestColoredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        alphaTestColoredPipelines_.clear();
        if (alphaTestShader_ != nullptr) wgpuShaderModuleRelease(alphaTestShader_);
        if (alphaTestColoredShader_ != nullptr) wgpuShaderModuleRelease(alphaTestColoredShader_);
        alphaTestShader_ = nullptr;
        alphaTestColoredShader_ = nullptr;
    }

    void WebGPURenderer::CreateAlphaTestResources()
    {
        DestroyAlphaTestResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanRenderer's alpha_test3d.{vert,frag}.glsl. AlphaTestEffect has
        // no lighting, so the primary Uniforms block repurposes [20..23]/[24] for
        // {alphaRef, alphaTolerance, passWeight, failWeight, vertexColorEnabled} instead (see
        // FillAlphaTestUniforms()) -- same 128-byte shape, so this reuses coloredBindGroupLayout_
        // (group 0) and texturedBindGroupLayout_ (group 1) unchanged; no new bind group layout or
        // pipeline layout needed.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(tex, texSampler, input.uv) * u.diffuseColor;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU AlphaTest3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        alphaTestShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (alphaTestShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTest3D shader");

        // WEBGPU-23: stride 24 (VertexPositionColorTexture) variant -- same shape as
        // colored_textured3d.wgsl's own vertex-colour mixing, combined with the alpha-test
        // discard above.
        static constexpr char coloredShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.extra.x;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(tex, texSampler, input.uv) * input.tint;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL coloredWgsl{};
        coloredWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredWgsl.code = StringView(coloredShaderSource);
        WGPUShaderModuleDescriptor coloredShaderDescriptor{};
        coloredShaderDescriptor.label = StringView("CNA WebGPU AlphaTestColored3D WGSL");
        coloredShaderDescriptor.nextInChain = &coloredWgsl.chain;
        alphaTestColoredShader_ = wgpuDeviceCreateShaderModule(device_, &coloredShaderDescriptor);
        if (alphaTestColoredShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTestColored3D shader");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineAlphaTest3D(std::size_t stride,
                                                                              WGPUPrimitiveTopology topology,
                                                                              WGPUIndexFormat stripIndexFormat,
                                                                              bool depthTest, bool depthWrite,
                                                                              int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias,
                                                     static_cast<std::uint64_t>(stride), colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        auto& cache = (stride == 24) ? alphaTestColoredPipelines_ : alphaTestPipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        WGPUVertexAttribute attributes[3]{};
        std::uint32_t attributeCount = 0;
        std::uint64_t arrayStride = stride;
        WGPUShaderModule shaderModule = alphaTestShader_;

        if (stride == 24)
        {
            struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[2].offset = offsetof(ColoredTexturedVertex, u);
            attributes[2].shaderLocation = 2;
            attributeCount = 3;
            arrayStride = sizeof(ColoredTexturedVertex);
            shaderModule = alphaTestColoredShader_;
        }
        else if (stride == 32)
        {
            // VertexPositionNormalTexture: position (offset 0) + UV (offset 24, past the unread
            // 12-byte normal) -- one shared shader for strides 20 and 32, only the vertex buffer
            // layout differs.
            struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(LitTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[1].offset = offsetof(LitTexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(LitTexturedVertex);
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[1].offset = offsetof(TexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(TexturedVertex);
        }

        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = arrayStride;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributeCount;
        vertexBufferLayout.attributes = attributes;

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU AlphaTest3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = shaderModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTest3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPURenderer::DestroyDualTextureResources()
    {
        for (auto& [key, pipe] : dualTexturePipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        dualTexturePipelines_.clear();
        for (auto& [key, pipe] : dualTextureColoredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        dualTextureColoredPipelines_.clear();
        if (dualTexturePipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(dualTexturePipelineLayout_);
        if (dualTextureBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(dualTextureBindGroupLayout_);
        if (dualTextureShader_ != nullptr) wgpuShaderModuleRelease(dualTextureShader_);
        if (dualTextureColoredShader_ != nullptr) wgpuShaderModuleRelease(dualTextureColoredShader_);
        dualTexturePipelineLayout_ = nullptr;
        dualTextureBindGroupLayout_ = nullptr;
        dualTextureShader_ = nullptr;
        dualTextureColoredShader_ = nullptr;
    }

    void WebGPURenderer::CreateDualTextureResources()
    {
        DestroyDualTextureResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanRenderer's dual_texture3d.{vert,frag}.glsl. DualTextureEffect
        // has no lighting and no alpha test, so group 0 reuses coloredBindGroupLayout_/the primary
        // Uniforms layout unchanged. Group 1 carries FOUR bindings: one sampler and one texture per
        // public sampler slot. REMED-GFX-172: the two layers do NOT share a TextureFilter/
        // AddressMode -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
        // DECLARE_TEXTURE(Texture2, 1), so Texture reads GraphicsDevice.SamplerStates[0] and
        // Texture2 reads SamplerStates[1]. One WGSL sampler for both textures made slot 1
        // inexpressible, and slot 1 silently inherited slot 0's.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var tex0Sampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;
@group(1) @binding(3) var tex1Sampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSample(tex0, tex0Sampler, input.uv);
    let sample1 = textureSample(tex1, tex1Sampler, input.uv);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    return sample0 * sample1 * u.diffuseColor;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU DualTexture3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        dualTextureShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (dualTextureShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D shader");

        // WEBGPU-24: stride 24 (VertexPositionColorTexture) variant -- adds vertex-colour tint,
        // mirroring colored_textured3d.wgsl's own mixing formula.
        static constexpr char coloredShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var tex0Sampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;
@group(1) @binding(3) var tex1Sampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSample(tex0, tex0Sampler, input.uv);
    let sample1 = textureSample(tex1, tex1Sampler, input.uv);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    return sample0 * sample1 * input.tint;
}
)WGSL";

        WGPUShaderSourceWGSL coloredWgsl{};
        coloredWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredWgsl.code = StringView(coloredShaderSource);
        WGPUShaderModuleDescriptor coloredShaderDescriptor{};
        coloredShaderDescriptor.label = StringView("CNA WebGPU DualTextureColored3D WGSL");
        coloredShaderDescriptor.nextInChain = &coloredWgsl.chain;
        dualTextureColoredShader_ = wgpuDeviceCreateShaderModule(device_, &coloredShaderDescriptor);
        if (dualTextureColoredShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTextureColored3D shader");

        // REMED-GFX-172: one sampler binding per public sampler slot. Binding 3 is the second
        // sampler rather than a renumbering of 1/2, so the two texture views keep the binding
        // numbers every other WebGPU 3D family uses.
        std::array<WGPUBindGroupLayoutEntry, 4> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        layoutEntries[2].binding = 2;
        layoutEntries[2].visibility = WGPUShaderStage_Fragment;
        layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[2].texture.multisampled = false;
        layoutEntries[3].binding = 3;
        layoutEntries[3].visibility = WGPUShaderStage_Fragment;
        layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU DualTexture3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        dualTextureBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{coloredBindGroupLayout_, dualTextureBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU DualTexture3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        dualTexturePipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (dualTextureBindGroupLayout_ == nullptr || dualTexturePipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineDualTexture3D(std::size_t stride,
                                                                                WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias,
                                                     static_cast<std::uint64_t>(stride), colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        auto& cache = (stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        WGPUVertexAttribute attributes[3]{};
        std::uint32_t attributeCount = 0;
        std::uint64_t arrayStride = stride;
        WGPUShaderModule shaderModule = dualTextureShader_;

        if (stride == 24)
        {
            struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[2].offset = offsetof(ColoredTexturedVertex, u);
            attributes[2].shaderLocation = 2;
            attributeCount = 3;
            arrayStride = sizeof(ColoredTexturedVertex);
            shaderModule = dualTextureColoredShader_;
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[1].offset = offsetof(TexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(TexturedVertex);
        }

        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = arrayStride;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributeCount;
        vertexBufferLayout.attributes = attributes;

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU DualTexture3D Pipeline");
        pipeline.layout = dualTexturePipelineLayout_;
        pipeline.vertex.module = shaderModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPURenderer::DestroyEnvMapResources()
    {
        for (auto& [key, pipe] : envMapPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        envMapPipelines_.clear();
        if (envMapPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(envMapPipelineLayout_);
        if (envMapBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(envMapBindGroupLayout_);
        if (envMapTextureBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(envMapTextureBindGroupLayout_);
        if (envMapShader_ != nullptr) wgpuShaderModuleRelease(envMapShader_);
        envMapPipelineLayout_ = nullptr;
        envMapBindGroupLayout_ = nullptr;
        envMapTextureBindGroupLayout_ = nullptr;
        envMapShader_ = nullptr;
    }

    void WebGPURenderer::CreateEnvMapResources()
    {
        DestroyEnvMapResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // WEBGPU-25/36/74: ported from VulkanRenderer's env_map3d.{vert,frag}.glsl, itself
        // cross-checked against EasyGLRenderer::EnsureEnvMapped3DProgram()'s identical GLSL
        // formula before porting -- both references already agree field-for-field, so this is a
        // direct WGSL translation, not a re-derivation. Group 0 binding 0 (Transform: mvp+world)
        // stands in for Vulkan's 128-byte push-constant range (WebGPU has none); binding 1
        // (EnvMapParams) carries everything else, including a CPU-precomputed normal matrix (WGSL
        // has no inverse()). Group 1 is a new 3-binding shape: sampler + texture_2d + texture_cube.
        static constexpr char shaderSource[] = R"WGSL(
struct Transform {
    mvp: mat4x4f,
    world: mat4x4f,
};
@group(0) @binding(0) var<uniform> t: Transform;

struct EnvMapParams {
    eyePos: vec4f,
    diffuseColor: vec4f,
    emissiveAmount: vec4f,
    light0Dir: vec4f,
    light0DiffuseFresnelEn: vec4f,
    envMapSpecFresnelF: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> ep: EnvMapParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;
@group(1) @binding(2) var envMap: texture_cube<f32>;
@group(1) @binding(3) var envMapSampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) eyeDir: vec3f,
    @location(2) uv: vec2f,
    @location(3) fogFactor: f32,
};

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = t.mvp * vec4f(input.position, 1.0);
    let worldPos = (t.world * vec4f(input.position, 1.0)).xyz;
    let normalMatrix = mat3x3f(ep.normalMatrixCol0.xyz, ep.normalMatrixCol1.xyz, ep.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    output.eyeDir = ep.eyePos.xyz - worldPos;
    output.uv = input.uv;
    // REMED-GFX-100: FNA view-space fog. fogVector is prepared once by the public effect from
    // World*View; all-zero disables fog and {0,0,0,1} gives the defined full-fog zero range.
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), ep.fogVector), 0.0, 1.0);
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(input.eyeDir);
    let texColor = textureSample(tex, texSampler, input.uv);
    let ndotl0 = max(dot(n, -ep.light0Dir.xyz), 0.0);
    let ndotl1 = max(dot(n, -ep.light1Dir.xyz), 0.0);
    let ndotl2 = max(dot(n, -ep.light2Dir.xyz), 0.0);
    let lightSum = ep.light0DiffuseFresnelEn.xyz * ndotl0
                 + ep.light1Diffuse.xyz * ndotl1
                 + ep.light2Diffuse.xyz * ndotl2;
    // REMED-GFX-007: FNA Lighting.fxh adds emissive UNSCALED (litRGB = lightSum*Diffuse +
    // Emissive), not (Emissive + lightSum)*Diffuse -- the latter re-scales the already
    // ambient-folded emissive by DiffuseColor a second time (and, since the CPU layer pre-folds
    // Alpha into both operands, squares Alpha too). emissiveAmount.xyz is the CPU-side pre-folded
    // (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha (EnvironmentMapEffect.cpp).
    let litRGB = lightSum * ep.diffuseColor.rgb + ep.emissiveAmount.xyz;
    let baseColor = litRGB * texColor.rgb;
    let combinedAlpha = ep.diffuseColor.a * texColor.a;
    let reflDir = reflect(-e, n);
    let envSample = textureSample(envMap, envMapSampler, reflDir);
    let viewAngle = dot(e, n);
    let fresnelEnabled = ep.light0DiffuseFresnelEn.w;
    let blendFactor = select(ep.emissiveAmount.w,
                             pow(max(1.0 - abs(viewAngle), 0.0), ep.envMapSpecFresnelF.w) * ep.emissiveAmount.w,
                             fresnelEnabled > 0.5);
    var rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor)
            + ep.envMapSpecFresnelF.xyz * envSample.a * combinedAlpha;
    rgb = mix(ep.fogColor.xyz, rgb, input.fogFactor);
    return vec4f(rgb, combinedAlpha);
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU EnvMap3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        envMapShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (envMapShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create EnvMap3D shader");

        std::array<WGPUBindGroupLayoutEntry, 2> uboLayoutEntries{};
        uboLayoutEntries[0].binding = 0;
        uboLayoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        uboLayoutEntries[0].buffer.minBindingSize = 128;
        uboLayoutEntries[1].binding = 1;
        uboLayoutEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboLayoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        uboLayoutEntries[1].buffer.minBindingSize = 240;
        WGPUBindGroupLayoutDescriptor uboLayoutDescriptor{};
        uboLayoutDescriptor.label = StringView("CNA WebGPU EnvMap3D UBO BindGroupLayout");
        uboLayoutDescriptor.entryCount = uboLayoutEntries.size();
        uboLayoutDescriptor.entries = uboLayoutEntries.data();
        envMapBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &uboLayoutDescriptor);

        // REMED-GFX-172: one sampler binding per public sampler slot -- binding 0 for the base 2D
        // texture (SamplerStates[0]) and binding 3 for the reflection cube (SamplerStates[1]).
        std::array<WGPUBindGroupLayoutEntry, 4> texLayoutEntries{};
        texLayoutEntries[0].binding = 0;
        texLayoutEntries[0].visibility = WGPUShaderStage_Fragment;
        texLayoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        texLayoutEntries[1].binding = 1;
        texLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
        texLayoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        texLayoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        texLayoutEntries[1].texture.multisampled = false;
        texLayoutEntries[2].binding = 2;
        texLayoutEntries[2].visibility = WGPUShaderStage_Fragment;
        texLayoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
        texLayoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;
        texLayoutEntries[2].texture.multisampled = false;
        texLayoutEntries[3].binding = 3;
        texLayoutEntries[3].visibility = WGPUShaderStage_Fragment;
        texLayoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor texLayoutDescriptor{};
        texLayoutDescriptor.label = StringView("CNA WebGPU EnvMap3D Texture BindGroupLayout");
        texLayoutDescriptor.entryCount = texLayoutEntries.size();
        texLayoutDescriptor.entries = texLayoutEntries.data();
        envMapTextureBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &texLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{envMapBindGroupLayout_, envMapTextureBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU EnvMap3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        envMapPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (envMapBindGroupLayout_ == nullptr || envMapTextureBindGroupLayout_ == nullptr ||
            envMapPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create EnvMap3D GPU resources");
    }

    void WebGPURenderer::EnsureEnvMapDefaultTextures()
    {
        // Mirrors EnsurePbrDefaultTextures()'s own lazy-at-first-draw pattern and
        // VulkanRenderer's defaultWhiteView_/defaultWhiteCubeView_ fallback role: neither
        // EnvironmentMapEffect::Texture nor ::EnvironmentMap is required to be set.
        if (envMapDefaultWhiteTexture_ == nullptr)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            envMapDefaultWhiteTexture_ = std::make_unique<WebGPUTextureRenderer>(*this, white);
        }
        if (envMapDefaultWhiteCube_ == nullptr)
        {
            envMapDefaultWhiteCube_ = std::make_unique<WebGPUTextureCubeRenderer>(*this, 1, false);
            const std::array<std::uint8_t, 4> whitePixel{255, 255, 255, 255};
            for (int face = 0; face < 6; ++face)
            {
                // REMED-GFX-135: SetData now reports completion. This 1x1 white fallback is what
                // an EnvironmentMapEffect without a cube map samples, so a face that failed to
                // upload would silently darken every such draw -- say so instead.
                if (!envMapDefaultWhiteCube_->SetData(face, 0, 0, 0, 1, 1, whitePixel.data(),
                                                      static_cast<int>(whitePixel.size())))
                {
                    throw std::runtime_error(
                        "CNA WebGPU: failed to upload the default white env-map cube face " +
                        std::to_string(face));
                }
            }
        }
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineEnvMap3D(WGPUPrimitiveTopology topology,
                                                                          WGPUIndexFormat stripIndexFormat,
                                                                          bool depthTest, bool depthWrite,
                                                                          int depthFunc,
                                               bool blend, const BlendKeyParams& blendParams,
                                               int cullMode, bool wireframe,
                                               float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = envMapPipelines_.find(key); it != envMapPipelines_.end())
            return it->second;

        struct EnvMapVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(EnvMapVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = offsetof(EnvMapVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[2].offset = offsetof(EnvMapVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(EnvMapVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = envMapShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU EnvMap3D Pipeline");
        pipeline.layout = envMapPipelineLayout_;
        pipeline.vertex.module = envMapShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create EnvMap3D pipeline");
        envMapPipelines_[key] = created;
        return created;
    }

    void WebGPURenderer::QueueEnvMapDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        if (webgpuVb.Stride() != 32)
            throw std::invalid_argument("CNA WebGPU: QueueEnvMapDraw requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");
        EnsureEnvMapDefaultTextures();

        EnvMapDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 32u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // EnvironmentMapEffect::Texture/EnvironmentMap are both genuinely optional (unlike every
        // other stride-32+ effect family's dispatch gate) -- null falls back to the 1x1 white
        // texture/cube at render time, matching VulkanRenderer's own default-white fallback.
        // ResolveSamplable()/the dynamic_cast below are both already null-safe.
        command.texture = ResolveSamplable(params.texture0);
        command.envMap = ResolveCubeSamplable(params.envMap);
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        // REMED-GFX-172: and the reflection cube's own slot, captured here for the same reason.
        command.envMapFilter = slotSamplers_[1].filter;
        command.envMapAddressU = slotSamplers_[1].addressU;
        command.envMapAddressV = slotSamplers_[1].addressV;
        command.envMapMaxAnisotropy = slotSamplers_[1].maxAnisotropy;

        const Matrix mvp = world * view * projection;
        FillEnvMapTransform(command.transformUniforms, mvp, world);
        FillEnvMapParams(command.envMapUniforms, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        envMapDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::EnvMap, envMapDrawCommands_.size() - 1);
    }

    void WebGPURenderer::IssueEnvMapDraw(WGPURenderPassEncoder pass,
                                                const EnvMapDrawCommand& command,
                                                ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty())
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU EnvMap3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor transformDescriptor{};
        transformDescriptor.label = StringView("CNA WebGPU EnvMap3D Transform UBO");
        transformDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        transformDescriptor.size = sizeof(command.transformUniforms);
        WGPUBuffer transformBuffer = wgpuDeviceCreateBuffer(device_, &transformDescriptor);
        wgpuQueueWriteBuffer(queue_, transformBuffer, 0, command.transformUniforms.data(),
                            sizeof(command.transformUniforms));

        WGPUBufferDescriptor paramsDescriptor{};
        paramsDescriptor.label = StringView("CNA WebGPU EnvMap3D Params UBO");
        paramsDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        paramsDescriptor.size = sizeof(command.envMapUniforms);
        WGPUBuffer paramsBuffer = wgpuDeviceCreateBuffer(device_, &paramsDescriptor);
        wgpuQueueWriteBuffer(queue_, paramsBuffer, 0, command.envMapUniforms.data(),
                            sizeof(command.envMapUniforms));

        std::array<WGPUBindGroupEntry, 2> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].buffer = transformBuffer;
        uboEntries[0].size = sizeof(command.transformUniforms);
        uboEntries[1].binding = 1;
        uboEntries[1].buffer = paramsBuffer;
        uboEntries[1].size = sizeof(command.envMapUniforms);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU EnvMap3D UBO BindGroup");
        uboBindDescriptor.layout = envMapBindGroupLayout_;
        uboBindDescriptor.entryCount = uboEntries.size();
        uboBindDescriptor.entries = uboEntries.data();
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "EnvironmentMap3D");
        WGPUTextureView texView = command.texture
            ? command.texture.View()
            : envMapDefaultWhiteTexture_->View();
        WGPUTextureView cubeView = command.envMap
            ? command.envMap.View()
            : envMapDefaultWhiteCube_->CubeView();
        // REMED-GFX-172: the reflection cube's own SamplerStates[1], from the description captured
        // at this draw's public call. The fallback 1x1 white cube is filtered by the same slot --
        // a missing resource does not change WHICH sampler slot owns that binding.
        WGPUSampler cubeSampler = GetOrCreateSlotSampler(command.envMapFilter, command.envMapAddressU,
                                                         command.envMapAddressV,
                                                         command.envMapMaxAnisotropy,
                                                         "EnvironmentMap3D/slot1");
        std::array<WGPUBindGroupEntry, 4> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = texView;
        texEntries[2].binding = 2;
        texEntries[2].textureView = cubeView;
        texEntries[3].binding = 3;
        texEntries[3].sampler = cubeSampler;
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU EnvMap3D Texture BindGroup");
        texBindDescriptor.layout = envMapTextureBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        if (MultiTextureSamplerTraceEnabled())
        {
            TraceMultiTextureBinding("EnvironmentMap3D", state.publicOrder, state.replayPosition,
                                     texView, cubeView,
                                     command.textureFilter, command.addressU, command.addressV,
                                     command.maxAnisotropy,
                                     command.envMapFilter, command.envMapAddressU,
                                     command.envMapAddressV, command.envMapMaxAnisotropy,
                                     sampler, cubeSampler, cubeSampler,
                                     envMapTextureBindGroupLayout_,
                                     envMapPipelineLayout_, texBindGroup, texEntries.size());
        }

        WGPURenderPipeline pipe = GetOrCreatePipelineEnvMap3D(
                                                              command.topology,
                                                              RequiredStripIndexFormat(command),
                                                              command.depthTest,
                                                              command.depthWrite, command.depthFunc,
                                                              command.blend, command.blendParams,
                                                              command.cullMode, command.wireframe,
                                                              command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU EnvMap3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(transformBuffer);
        pendingBufferReleases_.push_back(paramsBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::DestroyInstancedResources()
    {
        for (auto& [key, pipe] : instancedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        instancedPipelines_.clear();
        if (instancedShader_ != nullptr) wgpuShaderModuleRelease(instancedShader_);
        instancedShader_ = nullptr;
        // REMED-GFX-212: the colour-capable sibling shares this cache and this lifetime exactly.
        if (instancedColoredShader_ != nullptr) wgpuShaderModuleRelease(instancedColoredShader_);
        instancedColoredShader_ = nullptr;
    }

    void WebGPURenderer::CreateInstancedResources()
    {
        DestroyInstancedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredPipelineLayout_ == nullptr)
            return;

        // WEBGPU-27: ported from VulkanRenderer's instanced3d.{vert,frag}.glsl. Only
        // position (binding 0, per-vertex) is consumed by THIS module -- it is the variant selected
        // for a geometry stride whose packed layout carries no COLOR0 element at all (20
        // VertexPositionTexture, 32 VertexPositionNormalTexture, and any stride the table does not
        // recognise). REMED-GFX-212 added the position+colour sibling below for the strides that do
        // carry one. The per-instance mat4 world transform (binding 1, WGPUVertexStepMode_Instance)
        // replaces the caller's own World matrix entirely: [0..15] of the Uniforms block below is
        // View*Projection (not a full MVP), matching FillInstancedPushConst()'s identical choice.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    vp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
};
struct InstanceInput {
    @location(4) instCol0: vec4f,
    @location(5) instCol1: vec4f,
    @location(6) instCol2: vec4f,
    @location(7) instCol3: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput, instance: InstanceInput) -> VertexOutput {
    var output: VertexOutput;
    let world = mat4x4f(instance.instCol0, instance.instCol1, instance.instCol2, instance.instCol3);
    output.position = u.vp * world * vec4f(input.position, 1.0);
    output.color = u.diffuseColor;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Instanced3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        instancedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (instancedShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Instanced3D shader");

        // REMED-GFX-212: the position+colour sibling. BasicEffect's shader index has no instancing
        // term (FNA BasicEffect.cs:490-511) and every vertex-colour permutation of BasicEffect.fx
        // multiplies `vout.Diffuse *= vin.Color`, so an instanced draw owes exactly the mixing
        // colored3d.wgsl above already performs -- the expression below is character-for-character
        // its own, and `vertexColorEnabled` reaches it through the same uniform field
        // (light0DiffuseVertexColor.w, FillExtUniforms' out[31]) the instanced route already fills.
        static constexpr char coloredShaderSource[] = R"WGSL(
struct Uniforms {
    vp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
};
struct InstanceInput {
    @location(4) instCol0: vec4f,
    @location(5) instCol1: vec4f,
    @location(6) instCol2: vec4f,
    @location(7) instCol3: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput, instance: InstanceInput) -> VertexOutput {
    var output: VertexOutput;
    let world = mat4x4f(instance.instCol0, instance.instCol1, instance.instCol2, instance.instCol3);
    output.position = u.vp * world * vec4f(input.position, 1.0);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.color = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

        WGPUShaderSourceWGSL coloredWgsl{};
        coloredWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredWgsl.code = StringView(coloredShaderSource);
        WGPUShaderModuleDescriptor coloredShaderDescriptor{};
        coloredShaderDescriptor.label = StringView("CNA WebGPU Instanced3D Colored WGSL");
        coloredShaderDescriptor.nextInChain = &coloredWgsl.chain;
        instancedColoredShader_ = wgpuDeviceCreateShaderModule(device_, &coloredShaderDescriptor);
        if (instancedColoredShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Instanced3D Colored shader");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineInstanced3D(
        std::size_t pvStride, std::size_t instVbStride, WGPUPrimitiveTopology topology,
        WGPUIndexFormat stripIndexFormat,
        bool depthTest, bool depthWrite, int depthFunc,
        bool blend, const BlendKeyParams& blendParams,
        int cullMode, bool wireframe, float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t salt = static_cast<std::uint64_t>(pvStride) * 1000003u +
                                    static_cast<std::uint64_t>(instVbStride);
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, salt, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        if (auto it = instancedPipelines_.find(key); it != instancedPipelines_.end())
            return it->second;

        // Binding 0: per-vertex -- position at location 0, plus REMED-GFX-212's COLOR0 at location
        // 1 when this stride's packed layout carries one. Binding 1: per-instance
        // (WGPUVertexStepMode_Instance), a mat4 world transform as 4 Float32x4 columns at
        // locations 4-7 (matching Vulkan's own location numbering choice for cross-reference
        // clarity, though WGSL/WebGPU impose no such gap requirement), which is why the geometry
        // colour at location 1 can never collide with the instance data.
        std::uint64_t packedColorOffset = 0;
        const bool hasPackedColor = InstancedPackedColorOffsetForStride(pvStride, packedColorOffset);
        std::array<WGPUVertexAttribute, 2> vertexAttrs{};
        std::size_t vertexAttrCount = 0;
        vertexAttrs[vertexAttrCount].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        vertexAttrs[vertexAttrCount].offset = 0;
        vertexAttrs[vertexAttrCount].shaderLocation = 0;
        ++vertexAttrCount;
        if (hasPackedColor)
        {
            // The same normalized R8G8B8A8 element colored3d.wgsl's own pipeline binds for this
            // stride -- WGPUVertexFormat_Unorm8x4 is WebGPU's spelling of VertexElementFormat::Color.
            vertexAttrs[vertexAttrCount].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            vertexAttrs[vertexAttrCount].offset = packedColorOffset;
            vertexAttrs[vertexAttrCount].shaderLocation = 1;
            ++vertexAttrCount;
        }

        std::array<WGPUVertexAttribute, 4> instanceAttrs{};
        instanceAttrs[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        instanceAttrs[0].offset = 0;
        instanceAttrs[0].shaderLocation = 4;
        instanceAttrs[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        instanceAttrs[1].offset = 16;
        instanceAttrs[1].shaderLocation = 5;
        instanceAttrs[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        instanceAttrs[2].offset = 32;
        instanceAttrs[2].shaderLocation = 6;
        instanceAttrs[3].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        instanceAttrs[3].offset = 48;
        instanceAttrs[3].shaderLocation = 7;

        std::array<WGPUVertexBufferLayout, 2> vertexBufferLayouts{};
        vertexBufferLayouts[0].arrayStride = static_cast<std::uint64_t>(pvStride);
        vertexBufferLayouts[0].stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayouts[0].attributeCount = vertexAttrCount;
        vertexBufferLayouts[0].attributes = vertexAttrs.data();
        vertexBufferLayouts[1].arrayStride = static_cast<std::uint64_t>(instVbStride);
        vertexBufferLayouts[1].stepMode = WGPUVertexStepMode_Instance;
        vertexBufferLayouts[1].attributeCount = instanceAttrs.size();
        vertexBufferLayouts[1].attributes = instanceAttrs.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        // REMED-GFX-212: the geometry stride's own packed layout selects the module, the same way
        // the ordinary route picks colored3d/textured3d/colored_textured3d by stride. Both modules
        // declare the same single `@location(0) color: vec4f` fragment input, so the fragment
        // entry point is unchanged either way.
        WGPUShaderModule instancedModule = hasPackedColor ? instancedColoredShader_ : instancedShader_;
        fragment.module = instancedModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Instanced3D Pipeline");
        pipeline.layout = coloredPipelineLayout_;
        pipeline.vertex.module = instancedModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = vertexBufferLayouts.size();
        pipeline.vertex.buffers = vertexBufferLayouts.data();
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Instanced3D pipeline");
        instancedPipelines_[key] = created;
        return created;
    }

    void WebGPURenderer::IssueInstancedDraw(WGPURenderPassEncoder pass,
                                                   const InstancedDrawCommand& command,
                                                   ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() ||
            command.instanceCount == 0 || command.instVbData.empty())
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU Instanced3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor instVbDescriptor{};
        instVbDescriptor.label = StringView("CNA WebGPU Instanced3D InstanceBuffer");
        instVbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        instVbDescriptor.size = Align4(command.instVbData.size());
        WGPUBuffer instVertexBuffer = wgpuDeviceCreateBuffer(device_, &instVbDescriptor);
        wgpuQueueWriteBuffer(queue_, instVertexBuffer, 0, command.instVbData.data(), command.instVbData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU Instanced3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBindGroupEntry bindEntry{};
        bindEntry.binding = 0;
        bindEntry.buffer = uniformBuffer;
        bindEntry.size = sizeof(command.uniforms);
        WGPUBindGroupDescriptor bindDescriptor{};
        bindDescriptor.label = StringView("CNA WebGPU Instanced3D BindGroup");
        bindDescriptor.layout = coloredBindGroupLayout_;
        bindDescriptor.entryCount = 1;
        bindDescriptor.entries = &bindEntry;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelineInstanced3D(command.pvStride, command.instVbStride,
                                                                 command.topology,
                                                                 RequiredStripIndexFormat(command),
                                                                 command.depthTest,
                                                                 command.depthWrite, command.depthFunc,
                                                                 command.blend, command.blendParams,
                                                                 command.cullMode, command.wireframe,
                                                                 command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, instVertexBuffer, 0, command.instVbData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU Instanced3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, command.instanceCount,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, command.instanceCount, 0, 0);
        }

        pendingBindGroupReleases_.push_back(bindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(instVertexBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    // WEBGPU-52: lazily creates the shader/bind-group-layout/pipeline-layout/pipeline/sampler
    // used by GenerateMipsForLayer() -- see this method's own header doc comment for the full
    // rationale and the deliberate FNA/cross-renderer divergence this introduces. A minimal
    // full-screen-triangle vertex shader (the standard 3-vertex, no-vertex-buffer trick: each
    // vertex's clip position/UV is derived purely from @builtin(vertex_index)) plus a fragment
    // shader that samples the previous mip level through a real linear sampler -- this is what
    // makes the result a genuine filtered downsample, not a nearest-neighbor copy.
    void WebGPURenderer::EnsureMipBlitPipeline()
    {
        if (mipBlitPipeline_ != nullptr)
            return;

        static constexpr char shaderSource[] = R"WGSL(
struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VSOut {
    var output: VSOut;
    let x = f32((vertexIndex << 1u) & 2u);
    let y = f32(vertexIndex & 2u);
    output.position = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    output.uv = vec2f(x, y);
    return output;
}
@group(0) @binding(0) var mipSampler: sampler;
@group(0) @binding(1) var mipSource: texture_2d<f32>;
@fragment fn fs_main(input: VSOut) -> @location(0) vec4f {
    return textureSample(mipSource, mipSampler, input.uv);
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU MipBlit WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        mipBlitShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (mipBlitShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create MipBlit shader");

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU MipBlit BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        mipBlitBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU MipBlit PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &mipBlitBindGroupLayout_;
        mipBlitPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = WGPUTextureFormat_RGBA8Unorm;
        // REMED-GFX-077: internal mipmap-blit utility pipeline — not a game draw, so it is
        // intentionally unaffected by the game's BlendState.ColorWriteChannels/MultiSampleMask.
        target.writeMask = WGPUColorWriteMask_All; // internal mip-blit
        WGPUFragmentState fragment{};
        fragment.module = mipBlitShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU MipBlit Pipeline");
        pipeline.layout = mipBlitPipelineLayout_;
        pipeline.vertex.module = mipBlitShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 0;
        pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        // Always single-sample -- a plain Texture2D/TextureCube is never multisampled, regardless
        // of this renderer's own global sampleCount_ (unlike the swapchain/RenderTarget2D
        // pipelines, which must match sampleCount_ to stay render-pass compatible).
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max(); // REMED-GFX-077: internal mip-blit (see above)
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;
        // No depthStencil: this is its own dedicated colour-only render pass, not sharing the
        // swapchain/RenderTarget2D's depth attachment the way SpriteBatch's pipeline must.
        mipBlitPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (mipBlitPipeline_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create MipBlit pipeline");

        WGPUSamplerDescriptor samplerDescriptor{};
        samplerDescriptor.label = StringView("CNA WebGPU MipBlit Sampler");
        samplerDescriptor.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDescriptor.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDescriptor.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDescriptor.magFilter = WGPUFilterMode_Linear;
        samplerDescriptor.minFilter = WGPUFilterMode_Linear;
        samplerDescriptor.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDescriptor.lodMaxClamp = 32.0f;
        samplerDescriptor.maxAnisotropy = 1;
        mipBlitSampler_ = wgpuDeviceCreateSampler(device_, &samplerDescriptor);
        if (mipBlitSampler_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create MipBlit sampler");
    }

    // WEBGPU-52: one render pass per mip level, each sampling the previous level (via the real
    // linear mipBlitSampler_) into the next level's own single-mip render-attachment view -- see
    // EnsureMipBlitPipeline()'s own doc comment for why this, not a hypothetical filtered "blit"
    // call (wgpu-native has none). All levels for this ONE layer are recorded into a single
    // command encoder and submitted together; per-level transient views/bind-groups are released
    // only AFTER submission (matching EnsureFrameRendered()'s own pendingBindGroupReleases_/
    // pendingBufferReleases_ precedent for resources a not-yet-submitted encoder still
    // references), not eagerly like RenderSprites()'s per-draw bind groups (whose referenced
    // views are long-lived texture members, unlike these transient single-mip-level views).
    void WebGPURenderer::GenerateMipsForLayer(WGPUTexture texture, int layer, int width, int height, int mipLevels)
    {
        if (texture == nullptr || mipLevels <= 1 || width <= 0 || height <= 0)
            return;
        EnsureMipBlitPipeline();

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU MipBlit Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        std::vector<WGPUTextureView> pendingViews;
        std::vector<WGPUBindGroup> pendingGroups;
        pendingViews.reserve(static_cast<std::size_t>(mipLevels - 1) * 2u);
        pendingGroups.reserve(static_cast<std::size_t>(mipLevels - 1));

        int srcW = width;
        int srcH = height;
        for (int level = 1; level < mipLevels; ++level)
        {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            WGPUTextureViewDescriptor srcViewDescriptor{};
            srcViewDescriptor.label = StringView("CNA WebGPU MipBlit Source View");
            srcViewDescriptor.format = WGPUTextureFormat_RGBA8Unorm;
            srcViewDescriptor.dimension = WGPUTextureViewDimension_2D;
            srcViewDescriptor.baseMipLevel = static_cast<std::uint32_t>(level - 1);
            srcViewDescriptor.mipLevelCount = 1;
            srcViewDescriptor.baseArrayLayer = static_cast<std::uint32_t>(layer);
            srcViewDescriptor.arrayLayerCount = 1;
            srcViewDescriptor.aspect = WGPUTextureAspect_All;
            WGPUTextureView srcView = wgpuTextureCreateView(texture, &srcViewDescriptor);
            if (srcView == nullptr)
                throw std::runtime_error("CNA WebGPU: MipBlit: failed to create source view");

            WGPUTextureViewDescriptor dstViewDescriptor = srcViewDescriptor;
            dstViewDescriptor.label = StringView("CNA WebGPU MipBlit Destination View");
            dstViewDescriptor.baseMipLevel = static_cast<std::uint32_t>(level);
            WGPUTextureView dstView = wgpuTextureCreateView(texture, &dstViewDescriptor);
            if (dstView == nullptr)
            {
                wgpuTextureViewRelease(srcView);
                throw std::runtime_error("CNA WebGPU: MipBlit: failed to create destination view");
            }
            pendingViews.push_back(srcView);
            pendingViews.push_back(dstView);

            std::array<WGPUBindGroupEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].sampler = mipBlitSampler_;
            entries[1].binding = 1;
            entries[1].textureView = srcView;
            WGPUBindGroupDescriptor bindGroupDescriptor{};
            bindGroupDescriptor.label = StringView("CNA WebGPU MipBlit BindGroup");
            bindGroupDescriptor.layout = mipBlitBindGroupLayout_;
            bindGroupDescriptor.entryCount = entries.size();
            bindGroupDescriptor.entries = entries.data();
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindGroupDescriptor);
            if (bindGroup == nullptr)
                throw std::runtime_error("CNA WebGPU: MipBlit: failed to create bind group");
            pendingGroups.push_back(bindGroup);

            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = dstView;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

            WGPURenderPassDescriptor passDescriptor{};
            passDescriptor.label = StringView("CNA WebGPU MipBlit RenderPass");
            passDescriptor.colorAttachmentCount = 1;
            passDescriptor.colorAttachments = &colorAttachment;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
            ++renderPassCount_;
            // Internal mip-blit pass: nothing public is queued into it, so it neither reads nor
            // needs a captured Viewport. It is still counted (REMED-GFX-116's diagnostics measure
            // every native pass/setViewport this renderer issues, not a filtered subset).
            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, static_cast<float>(dstW), static_cast<float>(dstH), 0.0f, 1.0f);
            ++setViewportCallCount_;
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, static_cast<std::uint32_t>(dstW), static_cast<std::uint32_t>(dstH));
            wgpuRenderPassEncoderSetPipeline(pass, mipBlitPipeline_);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            srcW = dstW;
            srcH = dstH;
        }

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU MipBlit Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commandBuffer);
        ++queueSubmitCount_;
        wgpuCommandBufferRelease(commandBuffer);

        for (WGPUBindGroup bg : pendingGroups) wgpuBindGroupRelease(bg);
        for (WGPUTextureView view : pendingViews) wgpuTextureViewRelease(view);
    }

    void WebGPURenderer::GenerateMips2D(WGPUTexture texture, int width, int height, int mipLevels)
    {
        GenerateMipsForLayer(texture, 0, width, height, mipLevels);
    }

    void WebGPURenderer::GenerateMipsCubeFace(WGPUTexture texture, int face, int size, int mipLevels)
    {
        GenerateMipsForLayer(texture, face, size, size, mipLevels);
    }

    WebGPURenderer::LogicalViewport WebGPURenderer::ComputeLogicalViewport() const
    {
        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physicalWidth_));
        viewport.height = static_cast<float>(std::max(0, physicalHeight_));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer || virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth_) / static_cast<float>(physicalHeight_);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;
        const float sx = static_cast<float>(physicalWidth_) / logicalWidth;
        const float sy = static_cast<float>(physicalHeight_) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physicalWidth_) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physicalHeight_) - viewport.height) * 0.5f;
        return viewport;
    }

    void WebGPURenderer::QueueSprite(const ITextureRenderer& texture,
                                             const IWebGPUSamplable& samplable,
                                             const Rectangle& destination,
                                             const Rectangle& source,
                                             const Color& color,
                                             float rotation,
                                             const Vector2& origin,
                                             SpriteEffects effects,
                                             float layerDepth,
                                             const Matrix& transform,
                                             int textureFilter,
                                             int addressU,
                                             int addressV,
                                             const WebGPUSpriteBlendSnapshot& blendSnapshot)
    {
        if (destination.Width == 0 || destination.Height == 0 || source.Width == 0 || source.Height == 0)
            return;
        // REMED-GFX-019: a SpriteBatch destination rectangle is expressed in the CURRENTLY-BOUND
        // render target's own pixel coordinate system (XNA/FNA semantics), never the backbuffer's.
        // The backbuffer's virtual-resolution letterbox/presentation-mode scaling
        // (ComputeLogicalViewport()) is a backbuffer-only concept, so while a RenderTarget2D or a
        // RenderTargetCube face is bound the sprite maps 1:1 into that target's own pixels (an
        // identity viewport, x=y=0, width=height=logical=target size) and the clip-space divide
        // below uses the target's dimensions, not physicalWidth_/physicalHeight_. Mirrors
        // the native GPU renderer's own current-target branch (extended here to
        // the cube face too, since this renderer bakes NDC CPU-side at enqueue and each target's
        // pending sprites are flushed into that target's own render pass on the next target switch).
        LogicalViewport viewport;
        int targetWidth;
        int targetHeight;
        if (currentRenderTarget_ != nullptr)
        {
            targetWidth = currentRenderTarget_->GetWidth();
            targetHeight = currentRenderTarget_->GetHeight();
            viewport.width = viewport.logicalWidth = static_cast<float>(std::max(0, targetWidth));
            viewport.height = viewport.logicalHeight = static_cast<float>(std::max(0, targetHeight));
        }
        else if (currentRenderTargetCubeFace_ != nullptr)
        {
            targetWidth = targetHeight = currentRenderTargetCubeFace_->GetSize();
            viewport.width = viewport.logicalWidth = static_cast<float>(std::max(0, targetWidth));
            viewport.height = viewport.logicalHeight = static_cast<float>(std::max(0, targetHeight));
        }
        else
        {
            viewport = ComputeLogicalViewport();
            targetWidth = physicalWidth_;
            targetHeight = physicalHeight_;
        }
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f || targetWidth <= 0 || targetHeight <= 0)
            return;

        // REMED-GFX-072: when a custom sub-Viewport is active, XNA/FNA make SpriteBatch coordinates
        // VIEWPORT-LOCAL (CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0)): sprite
        // (0,0) is the viewport's top-left and the projection extent is Viewport.Width/Height. Bake
        // the NDC relative to the Viewport (divide by viewportW_/viewportH_, no letterbox), and
        // capture the Viewport into the SpriteCommand so RenderSprites can set the rasterizer viewport
        // to it per draw. Only a genuine sub-region (differs from the target extent) overrides -- the
        // default full-target viewport keeps the existing letterbox/1:1 NDC path byte-identical.
        // WEBGPU-141 (A): a genuine custom Viewport (REMED-GFX-072's feature) is one the caller set to
        // a proper sub-region of the render target, in the target's own pixel space -- so it must be
        // CONTAINED within [0,targetWidth] x [0,targetHeight]. Comparing only "differs from the target
        // extent", as this once did (REMED-GFX-072), mis-fired for a letterboxed backbuffer: the
        // default GraphicsDevice.Viewport there is a LOGICAL rectangle that can exceed the physical
        // surface (e.g. 107x64 over a 64x64 backbuffer), so every ordinary sprite was squished through
        // the viewport-local path and its sampled UV shifted -- the WebGPU_Clear_Readback Wrap/Mirror
        // regression. Requiring containment keeps the genuine sub-Viewport case (which fits inside the
        // target) while excluding that spurious oversized default, restoring the pre-GFX-072 NDC path
        // for the full-target backbuffer.
        const bool viewportIsContainedSubRegion =
            viewportX_ >= 0 && viewportY_ >= 0 &&
            viewportX_ + viewportW_ <= targetWidth && viewportY_ + viewportH_ <= targetHeight;
        const bool customViewport = viewportSet_ && viewportW_ > 0 && viewportH_ > 0 &&
            viewportIsContainedSubRegion &&
            (viewportX_ != 0 || viewportY_ != 0 || viewportW_ != targetWidth || viewportH_ != targetHeight);

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);
        std::array<Vector2, 4> points{Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};
        const float s = std::sin(rotation);
        const float c = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * c - point.Y * s + static_cast<float>(destination.X);
            const float rotatedY = point.X * s + point.Y * c + static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / texture.GetWidth();
        float v0 = static_cast<float>(source.Y) / texture.GetHeight();
        float u1 = static_cast<float>(source.X + source.Width) / texture.GetWidth();
        float v1 = static_cast<float>(source.Y + source.Height) / texture.GetHeight();
        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);
        const std::array<Vector2, 4> uv{Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int indices[6] = {0, 1, 2, 2, 1, 3};

        SpriteCommand command{};
        // REMED-GFX-167: the resolved view plus its keep-alive, never `&samplable` -- a SpriteBatch
        // draw is replayed at Present too, by which time a short-lived Texture2D may be gone.
        command.texture = samplable.Sampled();
        command.textureFilter = textureFilter;
        command.addressU = addressU;
        command.addressV = addressV;
        command.blend = blendSnapshot;
        command.viewportCustom = customViewport;
        // REMED-GFX-116: capture the Viewport for EVERY sprite, not only a sub-region one.
        // GFX-072 captured only the custom case, so a target-relative sprite still inherited
        // whatever viewport the pass resolved live at flush time -- setting a sub-Viewport after
        // the batch but before the flush squeezed it into that sub-region. The two fields are
        // independent: viewportCustom decides how the NDC above was baked, the snapshot decides
        // where the rasterizer puts it.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };
        for (int i = 0; i < 6; ++i)
        {
            const int corner = indices[i];
            float ndcX, ndcY;
            if (customViewport)
            {
                // Viewport-local: divide by Viewport.W/H; RenderSprites places the [-1,1] result at
                // Viewport.X/Y via the per-draw rasterizer viewport.
                ndcX = 2.0f * points[corner].X / static_cast<float>(viewportW_) - 1.0f;
                ndcY = 1.0f - 2.0f * points[corner].Y / static_cast<float>(viewportH_);
            }
            else
            {
                const float px = viewport.x + points[corner].X * viewport.width / viewport.logicalWidth;
                const float py = viewport.y + points[corner].Y * viewport.height / viewport.logicalHeight;
                ndcX = 2.0f * px / static_cast<float>(targetWidth) - 1.0f;
                ndcY = 1.0f - 2.0f * py / static_cast<float>(targetHeight);
            }
            auto& vertex = command.vertices[static_cast<std::size_t>(i)];
            vertex.position[0] = ndcX;
            vertex.position[1] = ndcY;
            vertex.position[2] = std::clamp(layerDepth, 0.0f, 1.0f);
            vertex.uv[0] = uv[corner].X;
            vertex.uv[1] = uv[corner].Y;
            std::copy(std::begin(rgba), std::end(rgba), vertex.color);
        }
        // WEBGPU-142: capture the active SpriteBatch custom effect (if any) and its uniform block by
        // value, so a later Begin with different uniforms does not change this sprite at replay.
        command.customEffect = activeSpriteCustomEffect_;
        if (activeSpriteCustomEffect_ != nullptr)
            command.customUniforms = activeSpriteCustomEffect_->uniformStaging_;
        spriteCommands_.push_back(command);
        // REMED-GFX-159: the public position of this sprite, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Sprite, spriteCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::UploadSpriteVertices()
    {
        spriteVertexBytes_ = 0;
        if (spriteCommands_.empty())
            return;
        const std::uint64_t required = Align4(spriteCommands_.size() * 6u * sizeof(SpriteVertex));
        if (spriteVertexBuffer_ == nullptr || required > spriteVertexCapacityBytes_)
        {
            if (spriteVertexBuffer_ != nullptr)
                wgpuBufferRelease(spriteVertexBuffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU SpriteBatch Vertex Buffer");
            descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            descriptor.size = std::max<std::uint64_t>(required, 64u * 1024u);
            spriteVertexBuffer_ = wgpuDeviceCreateBuffer(device_, &descriptor);
            spriteVertexCapacityBytes_ = descriptor.size;
        }

        // REMED-GFX-159: every sprite of the cycle still goes into ONE buffer in ONE write, exactly
        // as before -- interleaving changes when each sprite is DRAWN, not how its vertices are
        // staged, so a sprite's vertices stay at offset i*6 and there is still no per-sprite
        // allocation. This is a queue-timeline write, so it is ordered against the submit below
        // rather than against the draws being recorded, and may still happen up front.
        std::vector<SpriteVertex> vertices;
        vertices.reserve(spriteCommands_.size() * 6u);
        for (const SpriteCommand& command : spriteCommands_)
            vertices.insert(vertices.end(), command.vertices.begin(), command.vertices.end());
        spriteVertexBytes_ = vertices.size() * sizeof(SpriteVertex);
        wgpuQueueWriteBuffer(queue_, spriteVertexBuffer_, 0, vertices.data(), spriteVertexBytes_);
    }

    void WebGPURenderer::IssueSpriteWithCustomEffect(WGPURenderPassEncoder pass,
                                                     const SpriteCommand& command,
                                                     std::uint32_t spriteIndex,
                                                     WGPUTextureFormat targetFormat,
                                                     std::uint32_t targetSampleCount,
                                                     ReplayState& state)
    {
        WebGPUEffectRenderer* effect = command.customEffect;
        if (effect == nullptr || !effect->valid_)
            return;

        const int colorCount = std::max(1, replayColorAttachmentCount_);

        // Uniform buffer: the effect's block captured at queue time; min 16 bytes so an effect with
        // no declared block still binds.
        const std::uint64_t uboSize =
            std::max<std::uint64_t>(16u, (command.customUniforms.size() + 15u) & ~std::uint64_t{15u});
        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU SpriteBatch ShaderEffect UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = uboSize;
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        if (!command.customUniforms.empty())
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0,
                                 command.customUniforms.data(), command.customUniforms.size());

        // Pipeline: cached on the effect, keyed by the sprite pass state (distinct from the effect's
        // 3D-route keys by the topmost salt bit, so a 3D and a sprite pipeline never collide).
        auto mix = [](std::uint64_t h, std::uint64_t v) {
            return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)));
        };
        std::uint64_t key = mix(0x5350524954453031ull /*"SPRITE01"*/, static_cast<std::uint64_t>(targetFormat));
        key = mix(key, targetSampleCount);
        key = mix(key, static_cast<std::uint64_t>(colorCount));
        key = mix(key, command.blend.blendEnabled ? 1u : 0u);
        key = mix(key, static_cast<std::uint64_t>(command.blend.colorSrc) |
                       (static_cast<std::uint64_t>(command.blend.colorDst) << 8) |
                       (static_cast<std::uint64_t>(command.blend.alphaSrc) << 16) |
                       (static_cast<std::uint64_t>(command.blend.alphaDst) << 24) |
                       (static_cast<std::uint64_t>(command.blend.colorFunc) << 32) |
                       (static_cast<std::uint64_t>(command.blend.alphaFunc) << 40));
        key = mix(key, static_cast<std::uint64_t>(command.blend.colorWriteMask & 0xF));

        WGPURenderPipeline pipe = nullptr;
        if (auto it = effect->pipelineCache_.find(key); it != effect->pipelineCache_.end())
        {
            pipe = it->second;
        }
        else
        {
            std::array<WGPUVertexAttribute, 3> attributes{};
            attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
            attributes[0].offset = offsetof(SpriteVertex, position);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
            attributes[1].offset = offsetof(SpriteVertex, uv);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
            attributes[2].offset = offsetof(SpriteVertex, color);
            attributes[2].shaderLocation = 2;
            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = sizeof(SpriteVertex);
            vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.attributeCount = attributes.size();
            vertexBufferLayout.attributes = attributes.data();

            // Slot 0 carries the sprite's blend/write mask; extra MRT slots write nothing (a sprite
            // effect writes @location(0)).
            std::array<WGPUColorTargetState, 4> targets{};
            const int builtCount = InitStockColorTargetsEXT(targets);
            targets[0].format = targetFormat;
            targets[0].writeMask = static_cast<WGPUColorWriteMask>(command.blend.colorWriteMask & 0xF);
            WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
            const BlendKeyParams blendParams{
                command.blend.colorSrc, command.blend.colorDst, command.blend.alphaSrc,
                command.blend.alphaDst, command.blend.colorFunc, command.blend.alphaFunc};
            FillWGPUBlendState(blendState, blendParams);
            targets[0].blend = command.blend.blendEnabled ? &blendState : nullptr;

            WGPUFragmentState fragment{};
            fragment.module = effect->fragmentModule_;
            fragment.entryPoint = StringView("fs_main");
            fragment.targetCount = static_cast<std::size_t>(builtCount);
            fragment.targets = targets.data();

            WGPURenderPipelineDescriptor pipeline{};
            pipeline.label = StringView("CNA WebGPU SpriteBatch ShaderEffect Pipeline");
            pipeline.layout = effect->pipelineLayout_;
            pipeline.vertex.module = effect->vertexModule_;
            pipeline.vertex.entryPoint = StringView("vs_main");
            pipeline.vertex.bufferCount = 1;
            pipeline.vertex.buffers = &vertexBufferLayout;
            pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipeline.primitive.frontFace = WGPUFrontFace_CCW;
            pipeline.primitive.cullMode = WGPUCullMode_None;
            pipeline.multisample.count = targetSampleCount;
            pipeline.multisample.mask = command.blend.multiSampleMask;
            pipeline.multisample.alphaToCoverageEnabled = false;
            pipeline.fragment = &fragment;

            // Sprites do not test/write depth, but the pass owns a Depth24PlusStencil8 attachment,
            // so the pipeline must declare a matching (disabled) depth-stencil state -- exactly the
            // stock sprite pipeline's own choice.
            WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
            depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
            depthStencil.depthCompare = WGPUCompareFunction_Always;
            pipeline.depthStencil = &depthStencil;

            pipe = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
            if (pipe == nullptr)
                throw std::runtime_error("CNA WebGPU: failed to create a SpriteBatch ShaderEffect pipeline");
            effect->pipelineCache_[key] = pipe;
        }

        if (pipe != state.boundPipeline)
        {
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            state.boundPipeline = pipe;
        }
        const WGPUColor blendConstant{
            command.blend.blendFactorR, command.blend.blendFactorG,
            command.blend.blendFactorB, command.blend.blendFactorA};
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
        state.blendConstantIsPassDefault = false;
        ApplyDrawViewport(pass, command.viewport);
        ApplyDrawScissor(pass, command.scissor);

        // Bind group: UBO @0, and -- when the effect samples -- the sprite's own sampler @1 and the
        // sprite's texture @2 (the effect's own reserved @binding(1)/@binding(2) convention).
        std::array<WGPUBindGroupEntry, 3> bindEntries{};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = uniformBuffer;
        bindEntries[0].size = uboSize;
        std::size_t bindEntryCount = 1;
        if (effect->samplesTexture_)
        {
            bindEntries[1].binding = 1;
            bindEntries[1].sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                            command.addressV, kSpriteBatchMaxAnisotropy,
                                                            "SpriteBatch ShaderEffect");
            bindEntries[2].binding = 2;
            bindEntries[2].textureView = command.texture.View();
            bindEntryCount = 3;
        }
        WGPUBindGroupDescriptor bindDescriptor{};
        bindDescriptor.label = StringView("CNA WebGPU SpriteBatch ShaderEffect BindGroup");
        bindDescriptor.layout = effect->bindGroupLayout_;
        bindDescriptor.entryCount = bindEntryCount;
        bindDescriptor.entries = bindEntries.data();
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindDescriptor);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, spriteIndex * 6u, 0);

        pendingBindGroupReleases_.push_back(bindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
    }

    void WebGPURenderer::IssueSprite(WGPURenderPassEncoder pass,
                                            const SpriteCommand& command,
                                            std::uint32_t spriteIndex,
                                            WGPUTextureFormat targetFormat,
                                            std::uint32_t targetSampleCount,
                                            ReplayState& state)
    {
        if (command.blend.targetFormat != targetFormat ||
            command.blend.sampleCount != targetSampleCount)
        {
            throw std::runtime_error(
                "CNA WebGPU: queued SpriteBatch target compatibility changed before replay");
        }

        // REMED-GFX-159: vertex slot 0 is shared with every 3D family, each of which binds its own
        // buffer there, so the sprite buffer is rebound whenever a 3D draw has intervened -- once
        // per sprite RUN, not once per sprite.
        if (!state.spriteVerticesBound)
        {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, spriteVertexBuffer_, 0, spriteVertexBytes_);
            state.spriteVerticesBound = true;
        }

        // WEBGPU-142: a sprite whose batch bound a custom WGSL ShaderEffect runs the effect's shader
        // instead of the stock sprite pipeline (its own modules, layout and uniform block).
        if (command.customEffect != nullptr)
        {
            IssueSpriteWithCustomEffect(pass, command, spriteIndex, targetFormat, targetSampleCount, state);
            return;
        }

        // REMED-GFX-102: cache lookup happens per static-state transition; native pipeline
        // creation happens only on a cache miss for a previously unseen complete key. It is
        // never unconditional/per-sprite creation, and the dynamic BlendFactor RGBA value is
        // intentionally absent from GetOrCreateSpritePipeline's key.
        // REMED-GFX-159: the redundant-bind skip is tracked across ALL families now, so it cannot
        // skip a rebind after a 3D draw bound a pipeline of its own.
        const WGPURenderPipeline pipeline = GetOrCreateSpritePipeline(command.blend);
        if (pipeline != state.boundPipeline)
        {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            state.boundPipeline = pipeline;
        }
        const WGPUColor blendConstant{
            command.blend.blendFactorR, command.blend.blendFactorG,
            command.blend.blendFactorB, command.blend.blendFactorA
        };
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
        state.blendConstantIsPassDefault = false;

        // REMED-GFX-072/116: this sprite's OWN captured Viewport. A sub-region sprite was
        // baked viewport-local at enqueue and lands at Viewport.X/Y from here; a target-
        // relative sprite's snapshot IS the whole target by construction, so it resets the
        // rasterizer after a preceding sub-region draw and can no longer inherit a viewport
        // that was set after it was queued. Sprites no longer run last in the pass, but every
        // family drives the same per-draw state, so neither can disturb the other.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        std::array<WGPUBindGroupEntry, 2> entries{};
        entries[0].binding = 0;
        // REMED-GFX-170: the SAME authoritative translation the 3D families use. This path used to
        // resolve its own sampler with `textureFilter == 0 ? Linear : Nearest`, which is correct
        // only for Linear(0) and Point(1) and turned Anisotropic(2), LinearMipPoint(3),
        // MinPointMagLinearMipLinear(7) and MinPointMagLinearMipPoint(8) into a POINT
        // magnification they do not name. MaxAnisotropy is the XNA SamplerState default (4)
        // because ISpriteBatchRenderer::SetSamplerFilter carries the filter ordinal alone -- see
        // that declaration's own note; it is a constant here, so it is trivially immutable per
        // queued sprite.
        entries[0].sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                    command.addressV, kSpriteBatchMaxAnisotropy,
                                                    "SpriteBatch");
        entries[1].binding = 1;
        entries[1].textureView = command.texture.View();
        WGPUBindGroupDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU SpriteBatch BindGroup");
        descriptor.layout = spriteBindGroupLayout_;
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &descriptor);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, spriteIndex * 6u, 0);
        wgpuBindGroupRelease(bindGroup);
    }

    void WebGPURenderer::Begin3DDrawState(WGPURenderPassEncoder pass, ReplayState& state)
    {
        // REMED-GFX-159: a sprite sets the blend constant from its own captured BlendState
        // (REMED-GFX-102). Before interleaving, every 3D draw was guaranteed the pass-level value
        // because sprites could only ever run after them; now one can precede a 3D draw, so the
        // pass-level value is restored rather than inherited. What a 3D draw sees is therefore
        // byte-identical to what it saw before this change.
        if (!state.blendConstantIsPassDefault)
        {
            const WGPUColor blendConstant{blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_};
            wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
            state.blendConstantIsPassDefault = true;
        }
        // Every 3D issue function binds its own pipeline and its own vertex buffer into slot 0
        // unconditionally, so a following sprite must assume neither is still its own.
        state.boundPipeline = nullptr;
        state.spriteVerticesBound = false;
    }

    /**
     * @brief REMED-GFX-159: name of a deferred draw family, for the order trace below.
     *
     * A family is exactly one shader/pipeline family here, so this doubles as the program identity
     * the trace reports, and (family, slot) names one queued command -- and therefore its geometry
     * -- uniquely for as long as the bind cycle it belongs to is being recorded.
     */
    const char* WebGPURenderer::DrawFamilyName(DrawFamily family) noexcept
    {
        switch (family)
        {
        case DrawFamily::Sprite:      return "sprite";
        case DrawFamily::Colored:     return "colored3d";
        case DrawFamily::Textured:    return "textured3d";
        case DrawFamily::LitTextured: return "litTextured3d";
        case DrawFamily::AlphaTest:   return "alphaTest3d";
        case DrawFamily::DualTexture: return "dualTexture3d";
        case DrawFamily::EnvMap:      return "envMap3d";
        case DrawFamily::Instanced:   return "instanced3d";
        case DrawFamily::Pbr:         return "pbr3d";
        case DrawFamily::Skinned:     return "skinned3d";
        case DrawFamily::SkinnedPbr:  return "skinnedPbr3d";
        case DrawFamily::CustomEffect: return "customEffect";
        }
        return "unknown";
    }

    /**
     * @brief REMED-GFX-159: whether the draw-order trace is switched on for this process.
     *
     * Off unless `CNA_WEBGPU_TRACE_DRAW_ORDER` is set in the environment, read exactly once. This
     * exists so an ordering claim can be checked against what the renderer actually recorded rather
     * than inferred from pixels alone -- a family-grouped replay and a correct one can produce the
     * same image for one of the two orderings, which is how the defect it measures survived.
     */
    static bool TraceDrawOrder() noexcept
    {
        static const bool enabled = std::getenv("CNA_WEBGPU_TRACE_DRAW_ORDER") != nullptr;
        return enabled;
    }

    void WebGPURenderer::RecordDrawOrder(DrawFamily family, std::size_t index)
    {
        if (TraceDrawOrder())
        {
            std::fprintf(stderr, "[wgpu-order] enqueue #%zu family=%s slot=%zu\n",
                         drawOrder_.size(), DrawFamilyName(family), index);
        }
        DrawOrderEntry entry{family, static_cast<std::uint32_t>(index),
                             static_cast<std::uint32_t>(drawOrder_.size())};
        // WEBGPU-84: tag the draw with the occlusion query open at queue time (nullptr if none), so
        // replay can wrap exactly this query's draws in BeginOcclusionQuery/EndOcclusionQuery.
        entry.occlusionQuery = activeOcclusionQuery_;
        drawOrder_.push_back(entry);
        // WEBGPU-115: the cumulative counterpart of drawOrder_.size(), which a flush drains.
        ++queuedDrawCommandCount_;
    }

    void WebGPURenderer::RecordOrderedClear(bool color, bool depth, bool stencil)
    {
        OrderedClearCommand command;
        command.color = color;
        command.depth = depth;
        command.stencil = stencil;
        command.colorValue = clearColor_;
        command.depthValue = clearDepth_;
        command.stencilValue = clearStencil_;
        if (TraceDrawOrder())
        {
            std::fprintf(stderr, "[wgpu-order] enqueue #%zu clear aspects=%s%s%s\n",
                         drawOrder_.size(), color ? "target" : "", depth ? "|depth" : "",
                         stencil ? "|stencil" : "");
        }
        clearCommands_.push_back(command);
        drawOrder_.push_back(DrawOrderEntry{DrawFamily::Sprite,
                                            static_cast<std::uint32_t>(clearCommands_.size() - 1),
                                            static_cast<std::uint32_t>(drawOrder_.size()),
                                            OrderedKind::Clear});
    }

    void WebGPURenderer::DiscardQueuedSprites()
    {
        spriteCommands_.clear();
        // The 3D families are deliberately NOT cleared here (they never were), so their entries
        // keep addressing live commands and only the sprite references are dropped.
        // REMED-GFX-156: `kind` first -- a Clear entry stores its clearCommands_ slot in `index`
        // and leaves `family` at its default, which happens to be Sprite, so testing the family
        // alone would drop every ordered clear of the cycle along with the sprites.
        drawOrder_.erase(std::remove_if(drawOrder_.begin(), drawOrder_.end(),
                                        [](const DrawOrderEntry& e)
                                        {
                                            return e.kind == OrderedKind::Draw &&
                                                   e.family == DrawFamily::Sprite;
                                        }),
                         drawOrder_.end());
    }

    std::vector<WebGPURenderer::PassSegmentPlan>
    WebGPURenderer::BuildPassSegments() const
    {
        std::vector<PassSegmentPlan> segments;
        segments.push_back(PassSegmentPlan{});
        for (std::size_t i = 0; i < drawOrder_.size(); ++i)
        {
            const DrawOrderEntry& entry = drawOrder_[i];
            if (entry.kind == OrderedKind::Clear)
            {
                // Nothing observable separates this clear from the segment's own load action, so it
                // folds in: consecutive Clear()s are one pass, and a later request overrides an
                // earlier one only for the aspects it actually names. A draw in between makes the
                // clear observable, which is a boundary and therefore a second pass.
                if (segments.back().entryCount != 0)
                {
                    PassSegmentPlan next;
                    next.firstEntry = i + 1;
                    segments.push_back(next);
                }
                const OrderedClearCommand& request = clearCommands_[entry.index];
                OrderedClearCommand& accumulated = segments.back().clear;
                if (request.color)
                {
                    accumulated.color = true;
                    accumulated.colorValue = request.colorValue;
                }
                if (request.depth)
                {
                    accumulated.depth = true;
                    accumulated.depthValue = request.depthValue;
                }
                if (request.stencil)
                {
                    accumulated.stencil = true;
                    accumulated.stencilValue = request.stencilValue;
                }
                continue;
            }
            if (segments.back().entryCount == 0)
                segments.back().firstEntry = i;
            segments.back().entryCount = i + 1 - segments.back().firstEntry;
        }
        return segments;
    }

    void WebGPURenderer::ReplayOrderedSegments(WGPUCommandEncoder encoder,
                                                       const PassDestination& destination)
    {
        // WEBGPU-84: consume any occlusion results a prior flush resolved but nothing has read yet,
        // before this flush's own resolve overwrites the shared readback buffer. A no-op unless a
        // previous flush left a pending result (and the prior encoder is already submitted, so the
        // map completes at once). Polling IsComplete()/PixelCount() drains it the same way.
        ReadbackOcclusionResults();

        // One upload for the whole bind cycle, before any pass opens: wgpuQueueWriteBuffer is not
        // legal inside a render pass, and every segment reads the same shared sprite buffer.
        UploadSpriteVertices();

        const std::vector<PassSegmentPlan> segments = BuildPassSegments();
        const bool trace = TraceDrawOrder();

        for (std::size_t s = 0; s < segments.size(); ++s)
        {
            const PassSegmentPlan& segment = segments[s];
            const bool isFirst = (s == 0);
            const bool isLast = (s + 1 == segments.size());
            // The bind cycle's usage policy and the "the attachment was just recreated and holds
            // undefined bytes" forcing flags belong to the cycle, so they apply to its FIRST
            // segment only; every later segment loads whatever the previous one stored unless its
            // own Clear() named that aspect.
            const bool clearColor = segment.clear.color ||
                                    (isFirst && (clearColorPending_ || destination.discardFirstSegment));
            const bool clearDepth = segment.clear.depth ||
                                    (isFirst && (clearDepthPending_ || destination.discardFirstSegment));
            const bool clearStencil = segment.clear.stencil ||
                                      (isFirst && (clearStencilPending_ || destination.discardFirstSegment));

            // WEBGPU-85 MRT: one attachment per bound render target (1 for the backbuffer, a single
            // RenderTarget2D or a cube face; up to 4 for an MRT set). Slot 0 is always the single
            // destination fields; slots 1..N-1 come from the destination's mrt* arrays. Every slot
            // shares this segment's clear/load policy and clear value, matching XNA's device-wide
            // Clear() semantics (there is no per-attachment clear granularity in XNA).
            const int colorCount = std::max(1, destination.colorAttachmentCount);
            std::array<WGPURenderPassColorAttachment, 4> colorAttachments{};
            for (int c = 0; c < colorCount; ++c)
            {
                WGPURenderPassColorAttachment& ca = colorAttachments[static_cast<std::size_t>(c)];
                ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                ca.view = destination.ColorViewAt(c);
                // Resolving once, at the end of the cycle, is enough: the multisampled attachment is
                // stored and reloaded across the segment boundary, so the final resolve sees every
                // segment's samples. An intermediate resolve would be pure extra bandwidth.
                ca.resolveTarget = isLast ? destination.ResolveViewAt(c) : nullptr;
                ca.loadOp = clearColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = segment.clear.color ? segment.clear.colorValue : clearColor_;
            }
            WGPURenderPassColorAttachment& colorAttachment = colorAttachments[0];

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = destination.depthView;
            depthAttachment.depthLoadOp = clearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthClearValue = segment.clear.depth ? segment.clear.depthValue : clearDepth_;
            depthAttachment.depthReadOnly = false;
            depthAttachment.stencilLoadOp = clearStencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
            depthAttachment.stencilClearValue =
                segment.clear.stencil ? segment.clear.stencilValue : clearStencil_;
            depthAttachment.stencilReadOnly = false;

            WGPURenderPassDescriptor passDescriptor{};
            passDescriptor.label = StringView(destination.passLabel);
            passDescriptor.colorAttachmentCount = static_cast<std::size_t>(colorCount);
            passDescriptor.colorAttachments = colorAttachments.data();
            passDescriptor.depthStencilAttachment =
                destination.depthView != nullptr ? &depthAttachment : nullptr;
            // WEBGPU-84: BeginOcclusionQuery is only valid on a pass whose descriptor names the
            // query set. occlusionQuerySet_ is null until the first OcclusionQuery is created, so a
            // frame with none leaves this at the default null -- the pass is created exactly as before.
            passDescriptor.occlusionQuerySet = occlusionQuerySet_;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);

            if (trace)
            {
                std::fprintf(stderr,
                             "[wgpu-order] pass #%zu destination=%s segment=%zu/%zu opened-by=%s "
                             "clear=%s%s%s colorLoad=%s colorStore=store resolve=%s depthLoad=%s "
                             "stencilLoad=%s draws=%zu\n",
                             renderPassCount_, destination.traceName, s, segments.size(),
                             isFirst ? "bind" : "clear",
                             segment.clear.color ? "target" : "",
                             segment.clear.depth ? "|depth" : "",
                             segment.clear.stencil ? "|stencil" : "",
                             clearColor ? "clear" : "load",
                             colorAttachment.resolveTarget != nullptr ? "yes" : "no",
                             destination.depthView == nullptr ? "none" : (clearDepth ? "clear" : "load"),
                             destination.depthView == nullptr ? "none"
                                                              : (clearStencil ? "clear" : "load"),
                             segment.entryCount);
            }
            ++renderPassCount_;

            // Nothing survives the end of a render pass, so every segment reopens the whole pass
            // state: the viewport and scissor bookkeeping REMED-GFX-116/146 install at the target
            // extents, plus the two genuinely dynamic pass values. Each draw then reapplies its own
            // captured viewport/scissor/blend constant on top, exactly as within one pass.
            BeginPassViewport(pass, destination.width, destination.height);
            BeginPassScissor(pass, destination.width, destination.height);
            const WGPUColor blendConstant{blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_};
            wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
            wgpuRenderPassEncoderSetStencilReference(pass,
                                                     static_cast<std::uint32_t>(referenceStencil_));

            ReplayDrawsInOrder(pass, destination, segment.firstEntry, segment.entryCount);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        // WEBGPU-84: every query recorded this flush had its samples counted into occlusionQuerySet_
        // by the passes above; resolve the whole set into a GPU buffer and copy that into a
        // mappable one, both on this same (not-yet-submitted) encoder. The lazy readback in
        // ReadbackOcclusionResults() maps it once the caller has submitted. No queries -> no work.
        if (!occlusionResolvedThisFlush_.empty() && occlusionQuerySet_ != nullptr)
        {
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(kMaxOcclusionSlots) * sizeof(std::uint64_t);
            wgpuCommandEncoderResolveQuerySet(encoder, occlusionQuerySet_, 0,
                                              static_cast<std::uint32_t>(kMaxOcclusionSlots),
                                              occlusionResolveBuffer_, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, occlusionResolveBuffer_, 0,
                                                 occlusionReadbackBuffer_, 0, bytes);
            occlusionReadbackPending_ = true;
        }

        DiscardQueuedCommands();
    }

    // REMED-GFX-159 keeps the ordered stream and all eleven family vectors alive for the whole
    // walk, so they are dropped together, here, and never part-way through it.
    // REMED-GFX-167: this is also what the destructor calls before releasing the device. A command
    // now carries a native reference to the texture it samples, so a vector left populated would
    // release those handles when it is destroyed as a MEMBER -- which happens after the destructor
    // BODY has already released the queue, device, adapter and instance. Draining the queue first
    // keeps native release strictly inside the device's lifetime.
    void WebGPURenderer::DiscardQueuedCommands()
    {
        drawOrder_.clear();
        clearCommands_.clear();
        spriteCommands_.clear();
        coloredDrawCommands_.clear();
        texturedDrawCommands_.clear();
        litTexturedDrawCommands_.clear();
        alphaTestDrawCommands_.clear();
        dualTextureDrawCommands_.clear();
        envMapDrawCommands_.clear();
        instancedDrawCommands_.clear();
        pbrDrawCommands_.clear();
        skinnedDrawCommands_.clear();
        skinnedPbrDrawCommands_.clear();
        customEffectDrawCommands_.clear();
    }

    // ---- WEBGPU-84: occlusion queries -----------------------------------------------------------

    std::unique_ptr<IOcclusionQueryRenderer> WebGPURenderer::CreateOcclusionQuery()
    {
        return std::make_unique<WebGPUOcclusionQueryRenderer>(this);
    }

    void WebGPURenderer::EnsureOcclusionResources()
    {
        if (occlusionQuerySet_ != nullptr) return;

        WGPUQuerySetDescriptor querySetDescriptor{};
        querySetDescriptor.label = StringView("CNA WebGPU OcclusionQuerySet");
        querySetDescriptor.type = WGPUQueryType_Occlusion;
        querySetDescriptor.count = static_cast<std::uint32_t>(kMaxOcclusionSlots);
        occlusionQuerySet_ = wgpuDeviceCreateQuerySet(Device(), &querySetDescriptor);

        const std::uint64_t bytes =
            static_cast<std::uint64_t>(kMaxOcclusionSlots) * sizeof(std::uint64_t);

        WGPUBufferDescriptor resolveDescriptor{};
        resolveDescriptor.label = StringView("CNA WebGPU OcclusionResolve");
        resolveDescriptor.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        resolveDescriptor.size = bytes;
        occlusionResolveBuffer_ = wgpuDeviceCreateBuffer(Device(), &resolveDescriptor);

        WGPUBufferDescriptor readbackDescriptor{};
        readbackDescriptor.label = StringView("CNA WebGPU OcclusionReadback");
        readbackDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readbackDescriptor.size = bytes;
        occlusionReadbackBuffer_ = wgpuDeviceCreateBuffer(Device(), &readbackDescriptor);
    }

    int WebGPURenderer::AllocateOcclusionSlot(WebGPUOcclusionQueryRenderer* query)
    {
        for (int i = 0; i < kMaxOcclusionSlots; ++i)
        {
            if (occlusionSlots_[static_cast<std::size_t>(i)] == nullptr)
            {
                occlusionSlots_[static_cast<std::size_t>(i)] = query;
                return i;
            }
        }
        return -1;  // Every slot in use: this query never records, so its PixelCount stays 0.
    }

    void WebGPURenderer::FreeOcclusionSlot(int slot)
    {
        if (slot >= 0 && slot < kMaxOcclusionSlots)
            occlusionSlots_[static_cast<std::size_t>(slot)] = nullptr;
    }

    void WebGPURenderer::PurgeOcclusionQuery(WebGPUOcclusionQueryRenderer* query)
    {
        if (activeOcclusionQuery_ == query) activeOcclusionQuery_ = nullptr;
        occlusionResolvedThisFlush_.erase(
            std::remove(occlusionResolvedThisFlush_.begin(), occlusionResolvedThisFlush_.end(), query),
            occlusionResolvedThisFlush_.end());
        FreeOcclusionSlot(query->slot_);
    }

    void WebGPURenderer::ReadbackOcclusionResults()
    {
        if (!occlusionReadbackPending_) return;
        occlusionReadbackPending_ = false;

        const std::size_t bytes =
            static_cast<std::size_t>(kMaxOcclusionSlots) * sizeof(std::uint64_t);
        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(occlusionReadbackBuffer_, WGPUMapMode_Read, 0, bytes, callbackInfo);
        WaitForCompletion(Instance(), mapState.completed, "OcclusionQuery readback buffer map");
        if (mapState.status == WGPUMapAsyncStatus_Success)
        {
            const auto* counts = static_cast<const std::uint64_t*>(
                wgpuBufferGetConstMappedRange(occlusionReadbackBuffer_, 0, bytes));
            if (counts != nullptr)
            {
                for (WebGPUOcclusionQueryRenderer* q : occlusionResolvedThisFlush_)
                {
                    if (q == nullptr || q->slot_ < 0 || q->slot_ >= kMaxOcclusionSlots) continue;
                    const std::uint64_t c = counts[static_cast<std::size_t>(q->slot_)];
                    q->pixelCount_ = static_cast<int>(std::min<std::uint64_t>(
                        c, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
                    q->complete_ = true;
                }
            }
            wgpuBufferUnmap(occlusionReadbackBuffer_);
        }
        // The slots are free to be re-recorded next flush whether or not the map succeeded.
        for (WebGPUOcclusionQueryRenderer* q : occlusionResolvedThisFlush_)
            if (q != nullptr) q->recordedThisFlush_ = false;
        occlusionResolvedThisFlush_.clear();
    }

    WebGPUOcclusionQueryRenderer::WebGPUOcclusionQueryRenderer(WebGPURenderer* owner)
        : owner_(owner)
    {
        owner_->EnsureOcclusionResources();
        slot_ = owner_->AllocateOcclusionSlot(this);
    }

    WebGPUOcclusionQueryRenderer::~WebGPUOcclusionQueryRenderer()
    {
        if (owner_ != nullptr) owner_->PurgeOcclusionQuery(this);
    }

    void WebGPUOcclusionQueryRenderer::Begin()
    {
        // A fresh measurement: drop any previous result and become the active query.
        complete_ = false;
        ended_ = false;
        recordedThisFlush_ = false;
        pixelCount_ = 0;
        owner_->activeOcclusionQuery_ = this;
    }

    void WebGPUOcclusionQueryRenderer::End()
    {
        ended_ = true;
        if (owner_->activeOcclusionQuery_ == this)
            owner_->activeOcclusionQuery_ = nullptr;
    }

    bool WebGPUOcclusionQueryRenderer::IsComplete() const
    {
        const_cast<WebGPURenderer*>(owner_)->ReadbackOcclusionResults();
        return complete_;
    }

    int WebGPUOcclusionQueryRenderer::PixelCount() const
    {
        const_cast<WebGPURenderer*>(owner_)->ReadbackOcclusionResults();
        return pixelCount_;
    }

    // ===================================================================================
    // WEBGPU-76: custom-WGSL ShaderEffect (IEffectRenderer). See WebGPUEffectRenderer's class
    // doc comment for the binding convention and the design; the draw-path integration is
    // QueueCustomEffectDraw()/IssueCustomEffectDraw() below plus the branch at the top of
    // DrawPrimitivesEx()/DrawIndexedPrimitivesEx().
    // ===================================================================================

    WebGPUEffectRenderer::WebGPUEffectRenderer(WebGPURenderer* owner) : owner_(owner) {}

    WebGPUEffectRenderer::~WebGPUEffectRenderer()
    {
        for (auto& [key, pipe] : pipelineCache_)
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        pipelineCache_.clear();
        if (pipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(pipelineLayout_);
        if (bindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(bindGroupLayout_);
        if (sampler_ != nullptr) wgpuSamplerRelease(sampler_);
        if (fragmentModule_ != nullptr) wgpuShaderModuleRelease(fragmentModule_);
        if (vertexModule_ != nullptr) wgpuShaderModuleRelease(vertexModule_);
    }

    WGPUShaderModule WebGPUEffectRenderer::CompileModule(const std::string& wgsl, const char* label)
    {
        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code = StringView(wgsl.c_str());
        WGPUShaderModuleDescriptor descriptor{};
        descriptor.label = StringView(label);
        descriptor.nextInChain = &source.chain;

        // A validation error scope around creation turns an invalid WGSL source into a catchable
        // failure with a message, rather than an uncaught device error -- the same scoped pattern
        // Supports4xMsaa() uses. Never throws, so the tolerant neutral ShaderEffect clone test
        // (which hands a placeholder source) survives.
        struct CompileScopeState { bool completed = false; bool ok = false; std::string message; };
        CompileScopeState scope;
        wgpuDevicePushErrorScope(owner_->device_, WGPUErrorFilter_Validation);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(owner_->device_, &descriptor);
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = kCnaWebGpuCallbackMode;
        callback.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type,
                               WGPUStringView message, void* userdata1, void*)
        {
            auto& s = *static_cast<CompileScopeState*>(userdata1);
            s.ok = (status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
            if (!s.ok) s.message = ToString(message);
            s.completed = true;
        };
        callback.userdata1 = &scope;
        wgpuDevicePopErrorScope(owner_->device_, callback);
        WaitForCompletion(owner_->instance_, scope.completed, "ShaderEffect WGSL compile");

        if (!scope.ok || module == nullptr)
        {
            if (module != nullptr) { wgpuShaderModuleRelease(module); module = nullptr; }
            compileError_ = std::string(label) + ": " +
                            (scope.message.empty() ? "WGSL validation failed" : scope.message);
            return nullptr;
        }
        return module;
    }

    bool WebGPUEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        valid_ = false;
        compileError_.clear();
        if (fragmentModule_ != nullptr) { wgpuShaderModuleRelease(fragmentModule_); fragmentModule_ = nullptr; }
        if (vertexModule_ != nullptr) { wgpuShaderModuleRelease(vertexModule_); vertexModule_ = nullptr; }

        if (owner_ == nullptr || owner_->device_ == nullptr)
        {
            compileError_ = "WebGPU: no device is available to compile a ShaderEffect";
            return false;
        }
        if (vertSrc.empty() || fragSrc.empty())
        {
            compileError_ = "WebGPU: a ShaderEffect requires non-empty vertex and fragment WGSL source";
            return false;
        }

        vertexModule_ = CompileModule(vertSrc, "CNA WebGPU ShaderEffect VS");
        if (vertexModule_ == nullptr) return false;
        fragmentModule_ = CompileModule(fragSrc, "CNA WebGPU ShaderEffect FS");
        if (fragmentModule_ == nullptr) return false;

        // Source scan: only a fragment that samples a texture needs the sampler/texture bindings.
        // A shader that does not sample (e.g. the MRT G-buffer writer) gets a uniform-only layout,
        // so no unused bind-group entry has to be satisfied with a dummy resource.
        samplesTexture_ = fragSrc.find("texture_2d") != std::string::npos;

        std::array<WGPUBindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = 0;  // the concrete block size is validated per draw
        std::size_t entryCount = 1;
        if (samplesTexture_)
        {
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled = false;
            entryCount = 3;

            WGPUSamplerDescriptor samplerDescriptor{};
            samplerDescriptor.label = StringView("CNA WebGPU ShaderEffect Sampler");
            samplerDescriptor.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDescriptor.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDescriptor.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDescriptor.magFilter = WGPUFilterMode_Linear;
            samplerDescriptor.minFilter = WGPUFilterMode_Linear;
            samplerDescriptor.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDescriptor.maxAnisotropy = 1;
            samplerDescriptor.lodMaxClamp = 32.0f;
            sampler_ = wgpuDeviceCreateSampler(owner_->device_, &samplerDescriptor);
        }

        WGPUBindGroupLayoutDescriptor layoutDescriptor{};
        layoutDescriptor.label = StringView("CNA WebGPU ShaderEffect BindGroupLayout");
        layoutDescriptor.entryCount = entryCount;
        layoutDescriptor.entries = entries.data();
        bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(owner_->device_, &layoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU ShaderEffect PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &bindGroupLayout_;
        pipelineLayout_ = wgpuDeviceCreatePipelineLayout(owner_->device_, &pipelineLayoutDescriptor);

        if (bindGroupLayout_ == nullptr || pipelineLayout_ == nullptr ||
            (samplesTexture_ && sampler_ == nullptr))
        {
            compileError_ = "WebGPU: failed to create ShaderEffect bind-group/pipeline layout";
            return false;
        }
        valid_ = true;
        return true;
    }

    void WebGPUEffectRenderer::Bind()
    {
        // The 3D route carries this effect to the renderer through GpuDrawParams::customEffectRenderer
        // (set by ShaderEffect::FillGpuDrawParams), not through renderer-global "active effect" state,
        // and every SetUniform* writes into this object's own staging block regardless of bind state.
        // There is therefore nothing to bind eagerly here, unlike EasyGL's immediate glUseProgram.
    }

    void WebGPUEffectRenderer::Unbind() {}

    bool WebGPUEffectRenderer::IsValid() const { return valid_; }

    std::string WebGPUEffectRenderer::GetCompileError() const { return compileError_; }

    void WebGPUEffectRenderer::DeclareUniformBlockEXT(int blockSizeBytes, const char* const* names,
                                                      const int* offsets, int count)
    {
        if (blockSizeBytes < 0) blockSizeBytes = 0;
        uniformStaging_.assign(static_cast<std::size_t>(blockSizeBytes), std::uint8_t{0});
        uniformOffsets_.clear();
        for (int i = 0; i < count; ++i)
            if (names != nullptr && names[i] != nullptr && offsets != nullptr)
                uniformOffsets_[names[i]] = offsets[i];
    }

    void WebGPUEffectRenderer::WriteUniformBytes(const char* name, const void* data, int byteCount)
    {
        if (name == nullptr) return;
        auto it = uniformOffsets_.find(name);
        if (it == uniformOffsets_.end()) return;  // undeclared name: accepted-and-ignored, like EasyGL
        const std::size_t offset = static_cast<std::size_t>(it->second);
        if (offset + static_cast<std::size_t>(byteCount) > uniformStaging_.size()) return;
        std::memcpy(uniformStaging_.data() + offset, data, static_cast<std::size_t>(byteCount));
    }

    void WebGPUEffectRenderer::SetUniformFloat(const char* name, float value)
    {
        WriteUniformBytes(name, &value, 4);
    }

    void WebGPUEffectRenderer::SetUniformInt(const char* name, int value)
    {
        const std::int32_t v = value;
        WriteUniformBytes(name, &v, 4);
    }

    void WebGPUEffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        const float v[2] = {x, y};
        WriteUniformBytes(name, v, 8);
    }

    void WebGPUEffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const float v[3] = {x, y, z};
        WriteUniformBytes(name, v, 12);
    }

    void WebGPUEffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const float v[4] = {x, y, z, w};
        WriteUniformBytes(name, v, 16);
    }

    void WebGPUEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        WriteUniformBytes(name, matrix, 64);
    }

    void WebGPUEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        if (values != nullptr && count > 0) WriteUniformBytes(name, values, count * 4);
    }

    void WebGPUEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        if (values != nullptr && count > 0) WriteUniformBytes(name, values, count * 8);
    }

    void WebGPUEffectRenderer::SetUniformVec3Array(const char* name, const float* values, int count)
    {
        if (values != nullptr && count > 0) WriteUniformBytes(name, values, count * 12);
    }

    void WebGPUEffectRenderer::SetUniformMat4Array(const char* name, const float* matrices, int count)
    {
        if (matrices != nullptr && count > 0) WriteUniformBytes(name, matrices, count * 64);
    }

    void WebGPUEffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        // Only unit 0 (@binding(2)) is consumed by this renderer's custom-effect pipeline; the raw
        // pointer is resolved to a keep-alive sampleable view at queue time (QueueCustomEffectDraw),
        // never dereferenced at replay.
        if (unit == 0) boundTexture0_ = texture;
    }

    std::unique_ptr<IEffectRenderer> WebGPURenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<WebGPUEffectRenderer>(this);
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    void WebGPURenderer::QueueCustomEffectDraw(const IVertexBufferRenderer& vb,
                                               const IIndexBufferRenderer* ib,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               const GpuDrawParams& params)
    {
        auto* effect = static_cast<WebGPUEffectRenderer*>(params.customEffectRenderer);
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);

        // The renderer supplies World/View/Projection to the custom shader by exactly those names,
        // column-major and untransposed (WGSL mat4x4 storage), the same way EasyGL's
        // BindCustomEffectMatrices does -- a 3D custom shader reads them from its uniform block.
        float worldCM[16], viewCM[16], projCM[16];
        world.ToColumnMajor(worldCM);
        view.ToColumnMajor(viewCM);
        projection.ToColumnMajor(projCM);
        effect->SetUniformMat4("World", worldCM);
        effect->SetUniformMat4("View", viewCM);
        effect->SetUniformMat4("Projection", projCM);

        CustomEffectDrawCommand command;
        command.effect = effect;
        // Capture the effect's uniform block BY VALUE now: a second draw of the same effect with
        // different SetUniform* values must not see this draw's block at replay (the stock families
        // capture their fixed `uniforms` array for the identical reason).
        command.uniforms = effect->uniformStaging_;
        command.texture = ResolveSamplable(effect->boundTexture0_);

        const int vertexStart = params.vertexStart;
        const std::size_t stride = webgpuVb.Stride();
        command.arrayStride = static_cast<std::uint64_t>(stride);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset),
                                      shadow.end());

        // Attribute location = the element's index in the declaration (the cross-renderer
        // "location = declaration index" convention -- see CustomEffectVertexAttr's doc comment).
        const auto& elements = webgpuVb.Declaration().GetElements();
        command.attributes.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            CustomEffectVertexAttr attr;
            attr.format = WebGPUVertexFormatFromVEF(elements[i].getVertexElementFormatProperty());
            attr.offset = static_cast<std::uint64_t>(elements[i].getOffsetProperty());
            attr.shaderLocation = static_cast<std::uint32_t>(i);
            command.attributes.push_back(attr);
        }

        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthWrite = depthWriteEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.colorWriteChannels = colorWriteChannels_;  // WEBGPU-143: per-slot MRT write masks
        command.cullMode = cullMode_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        command.viewport = CaptureViewport();
        command.scissor = CaptureScissor();

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        customEffectDrawCommands_.push_back(std::move(command));
        RecordDrawOrder(DrawFamily::CustomEffect, customEffectDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssueCustomEffectDraw(WGPURenderPassEncoder pass,
                                               const CustomEffectDrawCommand& command,
                                               const PassDestination& destination,
                                               ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        WebGPUEffectRenderer* effect = command.effect;
        if (effect == nullptr || !effect->valid_ ||
            command.vertexCount == 0 || command.vertexData.empty())
            return;

        // Vertex buffer.
        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU ShaderEffect VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        // Uniform buffer (the block captured at queue time); min 16 bytes so an effect with no
        // declared block still has a bindable buffer.
        const std::uint64_t uboSize =
            std::max<std::uint64_t>(16u, (command.uniforms.size() + 15u) & ~std::uint64_t{15u});
        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU ShaderEffect UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = uboSize;
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        if (!command.uniforms.empty())
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), command.uniforms.size());

        // Pipeline: cached on the effect, keyed by the concrete pass state. WEBGPU-86: the fragment
        // target count and each slot's format come from the bound pass -- 1 for a single target,
        // 2..4 for an MRT set -- so a pipeline built for a 1-target pass is a distinct cache entry
        // from the same effect's 2-target pipeline (WebGPU validation rejects a mismatch).
        const int colorAttachmentCount = std::max(1, destination.colorAttachmentCount);
        auto mix = [](std::uint64_t h, std::uint64_t v) {
            return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)));
        };
        std::uint64_t key = 0;
        key = mix(key, destination.sampleCount);
        key = mix(key, static_cast<std::uint64_t>(colorAttachmentCount));
        for (int c = 0; c < colorAttachmentCount; ++c)
            key = mix(key, static_cast<std::uint64_t>(destination.ColorFormatAt(c)));
        key = mix(key, static_cast<std::uint64_t>(command.topology));
        key = mix(key, command.arrayStride);
        key = mix(key, (command.depthTest ? 1u : 0u) | (command.depthWrite ? 2u : 0u));
        key = mix(key, static_cast<std::uint64_t>(command.depthFunc));
        key = mix(key, command.blend ? 1u : 0u);
        key = mix(key, static_cast<std::uint64_t>(command.blendParams.colorSrc) |
                       (static_cast<std::uint64_t>(command.blendParams.colorDst) << 8) |
                       (static_cast<std::uint64_t>(command.blendParams.alphaSrc) << 16) |
                       (static_cast<std::uint64_t>(command.blendParams.alphaDst) << 24) |
                       (static_cast<std::uint64_t>(command.blendParams.colorFunc) << 32) |
                       (static_cast<std::uint64_t>(command.blendParams.alphaFunc) << 40));
        key = mix(key, static_cast<std::uint64_t>(command.cullMode));
        for (int c = 0; c < colorAttachmentCount; ++c)  // WEBGPU-143: per-slot write masks
            key = mix(key, static_cast<std::uint64_t>(command.colorWriteChannels[static_cast<std::size_t>(c)] & 0xF));
        for (const auto& a : command.attributes)
            key = mix(key, (static_cast<std::uint64_t>(a.format) << 40) ^
                           (a.offset << 8) ^ a.shaderLocation);

        WGPURenderPipeline pipe = nullptr;
        if (auto it = effect->pipelineCache_.find(key); it != effect->pipelineCache_.end())
        {
            pipe = it->second;
        }
        else
        {
            std::vector<WGPUVertexAttribute> attributes;
            attributes.reserve(command.attributes.size());
            for (const auto& a : command.attributes)
            {
                WGPUVertexAttribute wa{};
                wa.format = a.format;
                wa.offset = a.offset;
                wa.shaderLocation = a.shaderLocation;
                attributes.push_back(wa);
            }
            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = command.arrayStride;
            vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.attributeCount = attributes.size();
            vertexBufferLayout.attributes = attributes.data();

            // WEBGPU-86 MRT: one WGPUColorTargetState per bound attachment (1 or 2..4), each with
            // this slot's format and the same blend/write state. The custom WGSL fragment must write
            // `@location(0..N-1)`; a mismatch is a pipeline-creation validation error caught below.
            std::array<WGPUColorTargetState, 4> targets{};
            std::array<WGPUBlendState, 4> blendStates{};
            for (int c = 0; c < colorAttachmentCount; ++c)
            {
                targets[static_cast<std::size_t>(c)].format = destination.ColorFormatAt(c);
                targets[static_cast<std::size_t>(c)].writeMask =  // WEBGPU-143: per-slot ColorWriteChannels
                    static_cast<WGPUColorWriteMask>(command.colorWriteChannels[static_cast<std::size_t>(c)] & 0xF);
                blendStates[static_cast<std::size_t>(c)] = WGPU_BLEND_STATE_INIT;
                FillWGPUBlendState(blendStates[static_cast<std::size_t>(c)], command.blendParams);
                targets[static_cast<std::size_t>(c)].blend =
                    command.blend ? &blendStates[static_cast<std::size_t>(c)] : nullptr;
            }

            WGPUFragmentState fragment{};
            fragment.module = effect->fragmentModule_;
            fragment.entryPoint = StringView("fs_main");
            fragment.targetCount = static_cast<std::size_t>(colorAttachmentCount);
            fragment.targets = targets.data();

            WGPURenderPipelineDescriptor pipeline{};
            pipeline.label = StringView("CNA WebGPU ShaderEffect Pipeline");
            pipeline.layout = effect->pipelineLayout_;
            pipeline.vertex.module = effect->vertexModule_;
            pipeline.vertex.entryPoint = StringView("vs_main");
            pipeline.vertex.bufferCount = 1;
            pipeline.vertex.buffers = &vertexBufferLayout;
            pipeline.primitive.topology = command.topology;
            pipeline.primitive.stripIndexFormat = RequiredStripIndexFormat(command);
            pipeline.primitive.frontFace = WGPUFrontFace_CCW;
            pipeline.primitive.cullMode = ToWGPUCullMode(command.cullMode);
            pipeline.multisample.count = destination.sampleCount;
            pipeline.multisample.mask = 0xFFFFFFFFu;
            pipeline.multisample.alphaToCoverageEnabled = false;
            pipeline.fragment = &fragment;

            WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
            depthStencil.depthWriteEnabled =
                command.depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depthStencil.depthCompare = command.depthTest
                ? ToWGPUCompareFunction(command.depthFunc) : WGPUCompareFunction_Always;
            depthStencil.depthBias = static_cast<std::int32_t>(command.depthBias * 16777215.0f);
            depthStencil.depthBiasSlopeScale = command.slopeScaleDepthBias;
            pipeline.depthStencil = &depthStencil;

            pipe = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
            if (pipe == nullptr)
                throw std::runtime_error("CNA WebGPU: failed to create a ShaderEffect pipeline");
            effect->pipelineCache_[key] = pipe;
        }

        // Bind group: UBO at binding 0, and -- when the fragment samples -- the sampler and a
        // texture view at bindings 1/2 (the app-bound texture, or the shared default white).
        std::array<WGPUBindGroupEntry, 3> bindEntries{};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = uniformBuffer;
        bindEntries[0].size = uboSize;
        std::size_t bindEntryCount = 1;
        WebGPUSampledTextureEXT resolvedTexture;
        if (effect->samplesTexture_)
        {
            resolvedTexture = command.texture
                ? command.texture : ResolveSamplable(pbrDefaultWhiteTexture_.get());
            bindEntries[1].binding = 1;
            bindEntries[1].sampler = effect->sampler_;
            bindEntries[2].binding = 2;
            bindEntries[2].textureView = resolvedTexture.View();
            bindEntryCount = 3;
        }
        WGPUBindGroupDescriptor bindDescriptor{};
        bindDescriptor.label = StringView("CNA WebGPU ShaderEffect BindGroup");
        bindDescriptor.layout = effect->bindGroupLayout_;
        bindDescriptor.entryCount = bindEntryCount;
        bindDescriptor.entries = bindEntries.data();
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindDescriptor);

        ApplyDrawViewport(pass, command.viewport);
        ApplyDrawScissor(pass, command.scissor);
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU ShaderEffect IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1, command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(bindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    int WebGPURenderer::InitStockColorTargetsEXT(std::array<WGPUColorTargetState, 4>& out) const
    {
        const int count = std::max(1, replayColorAttachmentCount_);
        // Slot 0 is left default here: the builder holds a reference to out[0] and fills its real
        // format/write-mask/blend, so a `blend` set AFTER this call still lands in the array.
        for (int c = 1; c < count; ++c)
        {
            out[static_cast<std::size_t>(c)] = WGPUColorTargetState{};
            out[static_cast<std::size_t>(c)].format = surfaceFormat_;  // every RT here shares surfaceFormat_
            out[static_cast<std::size_t>(c)].writeMask = WGPUColorWriteMask_None;  // stock draw writes slot 0 only
            out[static_cast<std::size_t>(c)].blend = nullptr;
        }
        return count;
    }

    void WebGPURenderer::ReplayDrawsInOrder(WGPURenderPassEncoder pass,
                                                    const PassDestination& destination,
                                                    std::size_t firstEntry, std::size_t entryCount)
    {
        // WEBGPU-86 MRT: every pipeline built during this replay must match this pass's attachment
        // count. Stock builders read it via ExpandStockColorTargetsEXT and fold it into their key.
        replayColorAttachmentCount_ = std::max(1, destination.colorAttachmentCount);
        const bool trace = TraceDrawOrder();

        ReplayState state{};
        std::size_t issued = 0;
        // WEBGPU-84: the occlusion query whose BeginOcclusionQuery is currently open on this pass.
        WebGPUOcclusionQueryRenderer* openQuery = nullptr;
        for (std::size_t e = firstEntry; e < firstEntry + entryCount; ++e)
        {
            const DrawOrderEntry& entry = drawOrder_[e];
            const std::size_t i = entry.index;

            // WEBGPU-84: wrap this query's contiguous run of draws in one BeginOcclusionQuery/
            // EndOcclusionQuery pair. A run ends when the tagged query changes (including to none);
            // only a query's FIRST run this flush is recorded, since a query slot may be written just
            // once per resolve -- the same policy VulkanRenderer applies.
            WebGPUOcclusionQueryRenderer* drawQuery =
                (entry.kind == OrderedKind::Draw) ? entry.occlusionQuery : nullptr;
            if (drawQuery != openQuery)
            {
                if (openQuery != nullptr)
                {
                    wgpuRenderPassEncoderEndOcclusionQuery(pass);
                    openQuery = nullptr;
                }
                if (drawQuery != nullptr && drawQuery->slot_ >= 0 && !drawQuery->recordedThisFlush_)
                {
                    wgpuRenderPassEncoderBeginOcclusionQuery(
                        pass, static_cast<std::uint32_t>(drawQuery->slot_));
                    drawQuery->recordedThisFlush_ = true;
                    occlusionResolvedThisFlush_.push_back(drawQuery);
                    openQuery = drawQuery;
                }
            }
            if (trace)
            {
                std::fprintf(stderr,
                             "[wgpu-order]   %s issue #%zu <- enqueue #%u family=%s slot=%u\n",
                             destination.traceName, issued, entry.order, DrawFamilyName(entry.family),
                             entry.index);
            }
            // REMED-GFX-172, trace only: both positions the multi-texture sampler trace reports.
            state.publicOrder = entry.order;
            state.replayPosition = issued;
            ++issued;
            // WEBGPU-115: one increment per command actually handed to the pass encoder. This is
            // the single point every family's native draw passes through, so a refused draw's
            // "nothing reached the GPU" is a measured zero here, not an inference from pixels.
            ++nativeDrawIssueCount_;
            switch (entry.family)
            {
            case DrawFamily::Sprite:
                IssueSprite(pass, spriteCommands_[i], entry.index, destination.colorFormat,
                            destination.sampleCount, state);
                break;
            case DrawFamily::Colored:     IssueColoredDraw(pass, coloredDrawCommands_[i], state); break;
            case DrawFamily::Textured:    IssueTexturedDraw(pass, texturedDrawCommands_[i], state); break;
            case DrawFamily::LitTextured: IssueLitTexturedDraw(pass, litTexturedDrawCommands_[i], state); break;
            case DrawFamily::AlphaTest:   IssueAlphaTestDraw(pass, alphaTestDrawCommands_[i], state); break;
            case DrawFamily::DualTexture: IssueDualTextureDraw(pass, dualTextureDrawCommands_[i], state); break;
            case DrawFamily::EnvMap:      IssueEnvMapDraw(pass, envMapDrawCommands_[i], state); break;
            case DrawFamily::Instanced:   IssueInstancedDraw(pass, instancedDrawCommands_[i], state); break;
            case DrawFamily::Pbr:         IssuePbrDraw(pass, pbrDrawCommands_[i], state); break;
            case DrawFamily::Skinned:     IssueSkinnedDraw(pass, skinnedDrawCommands_[i], state); break;
            case DrawFamily::SkinnedPbr:  IssueSkinnedPbrDraw(pass, skinnedPbrDrawCommands_[i], state); break;
            case DrawFamily::CustomEffect:
                IssueCustomEffectDraw(pass, customEffectDrawCommands_[i], destination, state);
                break;
            }
        }
        // WEBGPU-84: a query whose run reached the end of this pass is closed here.
        if (openQuery != nullptr)
            wgpuRenderPassEncoderEndOcclusionQuery(pass);
    }

    void WebGPURenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail, int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask, int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
        // WEBGPU-83: stored for a future task -- see stencilEnable_'s own declaration comment for
        // why baking these into every pipeline's WGPUStencilFaceState is deliberately deferred.
        stencilEnable_ = stencilEnable;
        stencilFunc_ = stencilFunc;
        stencilPass_ = stencilPass;
        stencilFail_ = stencilFail;
        stencilDepthFail_ = stencilDepthFail;
        stencilReadMask_ = stencilMask;
        stencilWriteMask_ = stencilWriteMask;
        referenceStencil_ = referenceStencil;
        twoSidedStencilMode_ = twoSidedStencilMode;
        ccwStencilFunc_ = ccwStencilFunc;
        ccwStencilPass_ = ccwStencilPass;
        ccwStencilFail_ = ccwStencilFail;
        ccwStencilDepthFail_ = ccwStencilDepthFail;
    }

    void WebGPURenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        // REMED-GFX-102 normalization: blending is disabled only for the complete Opaque identity
        // (One/Zero/Add independently for colour and alpha). Looking only at the factors would
        // incorrectly erase a custom ReverseSubtract/Min/Max equation that happens to retain
        // Opaque's factors.
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1 &&
                          colorBlendFunc == 0 && alphaBlendFunc == 0);
        blendParams_.colorSrc  = colorSrcBlend;
        blendParams_.colorDst  = colorDstBlend;
        blendParams_.alphaSrc  = alphaSrcBlend;
        blendParams_.alphaDst  = alphaDstBlend;
        blendParams_.colorFunc = colorBlendFunc;
        blendParams_.alphaFunc = alphaBlendFunc;
        // REMED-GFX-077/GFX-102: both are STATIC wgpu-native pipeline state. The generic 3D caches
        // and the keyed SpriteBatch cache include the slot-0 mask.
        colorWriteMask_ = writeState.colorWriteChannels[0];
        sampleMask_ = writeState.multiSampleMask;
        // WEBGPU-143 MRT: keep the full per-slot ColorWriteChannels too, for a custom-effect MRT draw.
        for (int i = 0; i < 4; ++i)
            colorWriteChannels_[static_cast<std::size_t>(i)] = writeState.colorWriteChannels[i];
    }

    bool WebGPURenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // WEBGPU-115: the single entry this renderer genuinely lacks. wgpu-native has no polygon
        // mode -- WGPUPrimitiveState carries topology, strip index format, front face and cull
        // mode, and nothing that selects a fill. Reporting the shared permissive default here
        // promised a mode the renderer then silently replaced with a solid fill, which is the one
        // shape a capability query exists to prevent.
        if (capability == CNA::GraphicsCapability::WireFrame)
            return false;
        // WEBGPU-85/86/87: multiple simultaneous render targets are now real. SetRenderTargets()
        // accepts 2..4 RenderTarget2D targets, ReplayOrderedSegments builds an N-attachment pass,
        // and a custom ShaderEffect that writes @location(0..N-1) fans out to every slot
        // (WebGPU_MRT proves slot N holds slot N's content). A built-in (stock) draw into an MRT set
        // writes attachment 0 only (its single @location(0) output; slots 1..N-1 have writeMask 0 --
        // see ExpandStockColorTargetsEXT), the same "the 2D/stock pipeline writes attachment 0"
        // behaviour every other renderer has, so the shared cross-renderer MRT tests pass unchanged.
        // The WEBGPU-134 false arm that stood here while MRT was refused is therefore gone; the
        // capability falls through to the shared permissive default, which reports true.
        // WEBGPU-84: occlusion queries are now real -- CreateOcclusionQuery() returns a
        // WebGPUOcclusionQueryRenderer that records a genuine BeginOcclusionQuery/EndOcclusionQuery
        // pair around its draws and resolves an exact sample count (WebGPU_OcclusionQuery proves a
        // fully occluded quad reads 0 and a visible one a full target of samples). The WEBGPU-135
        // false arm that stood here while the feature was a no-op is therefore gone, and the
        // capability falls through to the shared permissive default, which reports true.
        return IGraphicsRenderer::SupportsCapability(capability);
    }

    bool WebGPURenderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        // WEBGPU-144: only when the device actually enabled the BC feature. Then the DXT/BC7 formats
        // transfer as blocks, and WebGPUTextureRenderer uploads them to a WGPUTextureFormat_BC*.
        return bcSupported_ && ClassifyWebGPUTextureFormat(surfaceFormat).compressed;
    }

    bool WebGPURenderer::LoadsCompressedContentNativelyEXT() const
    {
        // WEBGPU-144 Phase 2: the content loaders keep DXT/BC content compressed. The specific
        // format (and the bcSupported_ device gate) is enforced separately by
        // IsCompressedTransferFormatEXT, which the loaders AND with this policy flag.
        return true;
    }

    RendererFormatVerdict WebGPURenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        // WEBGPU-144: a BC format is genuinely Supported when the device can store it natively;
        // otherwise defer to the framework's own rule (which CPU-decompresses to Color elsewhere).
        if (bcSupported_ && ClassifyWebGPUTextureFormat(surfaceFormat).compressed)
            return RendererFormatVerdict::Supported;
        return RendererFormatVerdict::Defer;
    }

    void WebGPURenderer::RequireSupportedFillModeEXT(PrimitiveType primitive,
                                                             const char* route) const
    {
        if (!fillModeWireframe_)
            return;
        // A fill mode describes how a POLYGON's interior is rasterized. A line or point list has no
        // interior, so Solid and WireFrame are the same request for it -- this renderer substitutes
        // nothing there and the output already matches every other renderer's. Refusing it would
        // delete a correct draw rather than prevent a wrong one.
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            return;
        throw System::NotSupportedException(
            std::string("WebGPU: FillMode::WireFrame is not supported on this renderer, so the ") +
            route +
            " draw is refused rather than rendered as a solid fill. wgpu-native exposes no polygon "
            "mode, so a wireframe request cannot reach any native pipeline state. Query "
            "GraphicsDevice::SupportsCapability(GraphicsCapability::WireFrame) -- it reports false "
            "here -- and select FillMode::Solid instead.");
    }

    void WebGPURenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2.
        // XNA FillMode: Solid=0, WireFrame=1.
        cullMode_ = cullMode;
        // WEBGPU-115: stored, not judged. Selecting a RasterizerState is a state operation in
        // XNA's lifecycle and this setter cannot know whether a draw will follow, or which route
        // it would take -- the same reasoning REMED-GFX-DECL-GUARD applied to SetVertexDeclaration.
        // RequireSupportedFillModeEXT() refuses at the first draw that would consume this.
        fillModeWireframe_ = (fillMode == 1);
        scissorEnabled_ = scissorTestEnable;
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void WebGPURenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                  int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(slotSamplers_.size()))
            return;
        slotSamplers_[static_cast<std::size_t>(slot)] = SlotSamplerState{filter, addressU, addressV, maxAnisotropy};
    }

    void WebGPURenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void WebGPURenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void WebGPURenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = x;
        scissorY_ = y;
        scissorW_ = std::max(0, w);
        scissorH_ = std::max(0, h);
    }

    void WebGPURenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportX_ = x;
        viewportY_ = y;
        viewportW_ = std::max(0, w);
        viewportH_ = std::max(0, h);
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
        viewportSet_ = true;
    }

    // REMED-GFX-116: these three functions are the whole viewport-capture mechanism. Everything
    // above stores the LIVE GraphicsDevice.Viewport; CaptureViewport() copies it into a queued
    // command at the public draw call, and ApplyDrawViewport() is the only thing that turns a
    // captured value back into native pass state. No Render*Draws() may read viewportX_ and
    // friends -- that is precisely the "resolve at flush time" defect this replaces.
    WebGPUViewportSnapshot WebGPURenderer::CaptureViewport() const noexcept
    {
        WebGPUViewportSnapshot snapshot;
        snapshot.set = viewportSet_;
        snapshot.x = viewportX_;
        snapshot.y = viewportY_;
        snapshot.width = viewportW_;
        snapshot.height = viewportH_;
        snapshot.minDepth = viewportMinDepth_;
        snapshot.maxDepth = viewportMaxDepth_;
        return snapshot;
    }

    void WebGPURenderer::BeginPassViewport(WGPURenderPassEncoder pass, int targetWidth, int targetHeight)
    {
        passViewport_ = WebGPUPassViewportState{};
        passViewport_.targetWidth = std::max(0, targetWidth);
        passViewport_.targetHeight = std::max(0, targetHeight);
        passViewport_.width = std::max(static_cast<float>(passViewport_.targetWidth), 1.0f);
        passViewport_.height = std::max(static_cast<float>(passViewport_.targetHeight), 1.0f);
        passViewport_.maxDepth = 1.0f;
        passViewport_.applied = true;
        wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, passViewport_.width, passViewport_.height,
                                         0.0f, 1.0f);
        ++setViewportCallCount_;
    }

    void WebGPURenderer::ApplyDrawViewport(WGPURenderPassEncoder pass,
                                                  const WebGPUViewportSnapshot& snapshot)
    {
        const std::uint32_t fullW = static_cast<std::uint32_t>(passViewport_.targetWidth);
        const std::uint32_t fullH = static_cast<std::uint32_t>(passViewport_.targetHeight);
        float vx = 0.0f;
        float vy = 0.0f;
        float vw = static_cast<float>(fullW);
        float vh = static_cast<float>(fullH);
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
        if (snapshot.set)
        {
            // wgpu-native requires x+width <= target width and y+height <= target height (a hard
            // validation rule -- unlike scissor, which wgpu-native happens to accept out of bounds
            // without complaint, an oversized viewport silently distorts geometry instead of being
            // rejected or clipped). GraphicsDevice.Viewport can legitimately be stale relative to
            // the CURRENT physical surface across a live window resize (its default value is only
            // refreshed by GraphicsDevice.UpdateViewportFromWindow(), not on every frame) -- clamp
            // defensively so a stale/oversized Viewport degrades to "as much of it as actually
            // fits" instead of stretching the draw. Same arithmetic the pass-level block used
            // before this became per-draw state.
            vx = static_cast<float>(std::clamp(snapshot.x, 0, passViewport_.targetWidth));
            vy = static_cast<float>(std::clamp(snapshot.y, 0, passViewport_.targetHeight));
            vw = static_cast<float>(std::min(static_cast<std::uint32_t>(std::max(0, snapshot.width)),
                                             fullW - std::min(static_cast<std::uint32_t>(vx), fullW)));
            vh = static_cast<float>(std::min(static_cast<std::uint32_t>(std::max(0, snapshot.height)),
                                             fullH - std::min(static_cast<std::uint32_t>(vy), fullH)));
            minDepth = snapshot.minDepth;
            maxDepth = snapshot.maxDepth;
        }
        vw = std::max(vw, 1.0f);
        vh = std::max(vh, 1.0f);

        if (passViewport_.applied && passViewport_.x == vx && passViewport_.y == vy &&
            passViewport_.width == vw && passViewport_.height == vh &&
            passViewport_.minDepth == minDepth && passViewport_.maxDepth == maxDepth)
        {
            return;  // Consecutive draws sharing a viewport need one native call, not one each.
        }
        wgpuRenderPassEncoderSetViewport(pass, vx, vy, vw, vh, minDepth, maxDepth);
        ++setViewportCallCount_;
        passViewport_.applied = true;
        passViewport_.x = vx;
        passViewport_.y = vy;
        passViewport_.width = vw;
        passViewport_.height = vh;
        passViewport_.minDepth = minDepth;
        passViewport_.maxDepth = maxDepth;
    }

    // REMED-GFX-146: these three functions are the whole scissor-capture mechanism, and they are
    // deliberately the exact shape of REMED-GFX-116's viewport trio. SetScissorRect() and
    // ApplyRasterizerState() store the LIVE state; CaptureScissor() copies both halves of it into a
    // queued command at the public draw call, and ApplyDrawScissor() is the only thing that turns a
    // captured value back into native pass state. No Render*Draws() may read scissorEnabled_ or
    // scissorX_ and friends -- that is precisely the "resolve at flush time" defect this replaces.
    WebGPUScissorSnapshot WebGPURenderer::CaptureScissor() const noexcept
    {
        WebGPUScissorSnapshot snapshot;
        snapshot.enabled = scissorEnabled_;
        snapshot.x = scissorX_;
        snapshot.y = scissorY_;
        snapshot.width = scissorW_;
        snapshot.height = scissorH_;
        return snapshot;
    }

    void WebGPURenderer::BeginPassScissor(WGPURenderPassEncoder pass, int targetWidth,
                                                 int targetHeight)
    {
        passScissor_ = WebGPUPassScissorState{};
        passScissor_.targetWidth = std::max(0, targetWidth);
        passScissor_.targetHeight = std::max(0, targetHeight);
        passScissor_.width = static_cast<std::uint32_t>(passScissor_.targetWidth);
        passScissor_.height = static_cast<std::uint32_t>(passScissor_.targetHeight);
        passScissor_.applied = true;
        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, passScissor_.width, passScissor_.height);
        ++setScissorCallCount_;
    }

    void WebGPURenderer::ApplyDrawScissor(WGPURenderPassEncoder pass,
                                                 const WebGPUScissorSnapshot& snapshot)
    {
        const int fullW = passScissor_.targetWidth;
        const int fullH = passScissor_.targetHeight;
        // "Disabled" is the whole attachment: WebGPU has no separate scissor-test toggle the way
        // GL and Vulkan do -- the rectangle IS the only clip -- so the full extent is the identity.
        int sx = 0;
        int sy = 0;
        int sw = fullW;
        int sh = fullH;
        if (snapshot.enabled)
        {
            // Clip in SIGNED arithmetic, before anything reaches the unsigned native call: a
            // negative public X/Y or a rectangle hanging off the target would otherwise wrap
            // through uint32_t into an enormous rectangle. XNA/FNA clip a ScissorRectangle to the
            // active target rather than rejecting it, which is also what this renderer has always
            // done and what Vulkan's computeScissor does; ranges are clipped, never rejected.
            // The edge sums are formed in 64 bits: X and Width both come straight from the public
            // Rectangle, so `x + width` can overflow a 32-bit int on absurd input, and signed
            // overflow is undefined behaviour rather than a large number.
            using I64 = std::int64_t;
            const I64 x0 = std::clamp<I64>(snapshot.x, 0, fullW);
            const I64 y0 = std::clamp<I64>(snapshot.y, 0, fullH);
            // Width/Height are measured from the UNCLAMPED origin, so a rectangle whose left edge
            // is off-target keeps the part of it that is on-target instead of being shifted right.
            const I64 x1 = std::clamp<I64>(static_cast<I64>(snapshot.x) +
                                           std::max(0, snapshot.width), 0, fullW);
            const I64 y1 = std::clamp<I64>(static_cast<I64>(snapshot.y) +
                                           std::max(0, snapshot.height), 0, fullH);
            sx = static_cast<int>(x0);
            sy = static_cast<int>(y0);
            sw = static_cast<int>(std::max<I64>(0, x1 - x0));
            sh = static_cast<int>(std::max<I64>(0, y1 - y0));
        }
        const std::uint32_t nx = static_cast<std::uint32_t>(sx);
        const std::uint32_t ny = static_cast<std::uint32_t>(sy);
        const std::uint32_t nw = static_cast<std::uint32_t>(sw);
        const std::uint32_t nh = static_cast<std::uint32_t>(sh);

        if (passScissor_.applied && passScissor_.x == nx && passScissor_.y == ny &&
            passScissor_.width == nw && passScissor_.height == nh)
        {
            return;  // Consecutive draws sharing a rectangle need one native call, not one each.
        }
        wgpuRenderPassEncoderSetScissorRect(pass, nx, ny, nw, nh);
        ++setScissorCallCount_;
        passScissor_.applied = true;
        passScissor_.x = nx;
        passScissor_.y = ny;
        passScissor_.width = nw;
        passScissor_.height = nh;
    }

    // REMED-GFX-170: releases every native sampler this renderer owns. The 18-entry SpriteBatch-only
    // array this replaces WAS released here; slotSamplerCache_ never was, so consolidating the two
    // caches without this would have turned a pre-existing leak of the 3D samplers into the only
    // leak. Both are one cache now, and it is released exactly once.
    void WebGPURenderer::ReleaseSamplerCache()
    {
        for (auto& [key, sampler] : slotSamplerCache_)
        {
            if (sampler != nullptr)
                wgpuSamplerRelease(sampler);
        }
        slotSamplerCache_.clear();
    }

    WGPUSampler WebGPURenderer::GetOrCreateSlotSampler(int filter, int addressU, int addressV,
                                                               int maxAnisotropy, const char* family)
    {
        const int clampedAniso = std::clamp(maxAnisotropy, 1, 16);
        const std::uint32_t key = (static_cast<std::uint32_t>(filter) & 0xFFu)
                                 | ((static_cast<std::uint32_t>(addressU) & 0xFFu) << 8)
                                 | ((static_cast<std::uint32_t>(addressV) & 0xFFu) << 16)
                                 | ((static_cast<std::uint32_t>(clampedAniso) & 0xFFu) << 24);
        const auto it = slotSamplerCache_.find(key);
        const bool hit = it != slotSamplerCache_.end();

        WGPUSamplerDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU SamplerState Sampler");
        FillWGPUSamplerDescriptor(descriptor, filter, addressU, addressV, clampedAniso);
        WGPUSampler sampler = nullptr;
        if (hit)
        {
            sampler = it->second;
        }
        else
        {
            sampler = wgpuDeviceCreateSampler(device_, &descriptor);
            if (sampler == nullptr)
                throw std::runtime_error("CNA WebGPU: failed to create per-slot SamplerState sampler");
            slotSamplerCache_[key] = sampler;
        }
        // REMED-GFX-170: the whole public->native translation on one line, so a wrong ordinal
        // mapping is readable directly instead of being inferred from pixels.
        if (SamplerTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-wgpu-sampler] family=%s filter=%d(%s) mag=%s min=%s mip=%s "
                         "aniso=%u addrU=%s addrV=%s key=0x%08x sampler=%p %s\n",
                         family, filter, TextureFilterName(filter),
                         FilterModeName(descriptor.magFilter),
                         FilterModeName(descriptor.minFilter),
                         MipmapFilterModeName(descriptor.mipmapFilter),
                         static_cast<unsigned>(descriptor.maxAnisotropy),
                         AddressModeName(descriptor.addressModeU),
                         AddressModeName(descriptor.addressModeV),
                         static_cast<unsigned>(key), static_cast<void*>(sampler),
                         hit ? "HIT" : "CREATE");
        }
        return sampler;
    }

    // REMED-GFX-156: every entry point below records ONE ordered clear command carrying exactly the
    // aspects its ClearOptions mask named, at its public call position in the same stream the eleven
    // draw families use (REMED-GFX-159). The clearXPending_ flags are kept and still decide the
    // FIRST segment's load actions, because they carry a second, unrelated meaning -- "this
    // attachment was just recreated and holds undefined bytes, force a real clear" (see
    // ApplyMultiSampleCount) -- which is a property of the bind cycle, not of a public command.
    // A multi-aspect public Clear() reaches several of these in one call; each records its own
    // entry, and BuildPassSegments folds them back together because no draw separates them, so one
    // public Clear() is still one pass boundary.
    void WebGPURenderer::Clear(float r, float g, float b, float a)
    {
        clearColor_ = WGPUColor{r, g, b, a};
        clearColorPending_ = true;
        framePending_ = true;
        RecordOrderedClear(true, false, false);
    }

    void WebGPURenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }
    void WebGPURenderer::ClearDepth(float depth)
    { clearDepth_ = depth; clearDepthPending_ = true; framePending_ = true; RecordOrderedClear(false, true, false); }
    void WebGPURenderer::ClearStencil(int stencil)
    { clearStencil_ = static_cast<std::uint32_t>(stencil); clearStencilPending_ = true; framePending_ = true; RecordOrderedClear(false, false, true); }
    void WebGPURenderer::ClearDepthAndStencil(float depth, int stencil) { ClearDepth(depth); ClearStencil(stencil); }
    void WebGPURenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil) { Clear(r, g, b, a); ClearStencil(stencil); }
    void WebGPURenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    { Clear(r, g, b, a); ClearDepth(depth); ClearStencil(stencil); }

    bool WebGPURenderer::EnsureFrameRendered()
    {
#if defined(__EMSCRIPTEN__)
        // WEBGPU-133: a prior readback marked the acquired surface texture stale (the browser
        // invalidated it during the readback's event-loop yield). Reusing it for another read-only
        // re-read of the same frame is fine -- readbackBuffer_ already holds the captured pixels --
        // but an actual RENDER must not resubmit the destroyed texture. framePending_ is what
        // distinguishes "about to render" from a read-only flush; discard here so the block below
        // re-acquires a fresh current texture only when we are really going to draw.
        if (hasAcquiredTexture_ && acquiredBackbufferStale_ && framePending_)
            DiscardAcquiredBackbuffer();
#endif
        if (!hasAcquiredTexture_)
        {
            ConfigureSurface(false);
            if (!surfaceConfigured_)
            {
                // REMED-GFX-159: and their ordered-stream entries with them, so the surviving
                // 3D entries cannot address a sprite slot that no longer exists.
                DiscardQueuedSprites();
                return false;
            }

            WGPUSurfaceTexture surfaceTexture{};
            wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
            if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
            {
                if (surfaceTexture.texture != nullptr)
                    wgpuTextureRelease(surfaceTexture.texture);
                if (IsSurfaceRecoverable(surfaceTexture.status))
                    ConfigureSurface(true);
                else
                    throw std::runtime_error(
                        "CNA WebGPU: unrecoverable surface acquisition failure (status " +
                        std::to_string(static_cast<int>(surfaceTexture.status)) + ")");
                // REMED-GFX-159: and their ordered-stream entries with them, so the surviving
                // 3D entries cannot address a sprite slot that no longer exists.
                DiscardQueuedSprites();
                return false;
            }

            acquiredTexture_ = surfaceTexture.texture;
            hasAcquiredTexture_ = true;
            framePending_ = true;

#if defined(__EMSCRIPTEN__)
            acquiredBackbufferStale_ = false;   // WEBGPU-133: freshly acquired, valid for a render.
            // In the browser the surface's backing store follows the <canvas>'s own width/height,
            // which SDL updates on a resize independently of the size ConfigureSurface() requested.
            // wgpuSurfaceGetCurrentTexture() therefore returns the canvas size, not surfaceConfig_'s,
            // so a resize leaves the depth/MSAA attachments (sized from physicalWidth_/physicalHeight_
            // via ConfigureSurface) mismatched against the colour attachment -- a render-pass
            // attachment-size validation error. Sync them, and the viewport, to the texture we
            // actually got. Native surfaces are app-driven and always already agree, so this is web
            // only.
            const int acquiredWidth = static_cast<int>(wgpuTextureGetWidth(acquiredTexture_));
            const int acquiredHeight = static_cast<int>(wgpuTextureGetHeight(acquiredTexture_));
            if (acquiredWidth > 0 && acquiredHeight > 0 &&
                (acquiredWidth != physicalWidth_ || acquiredHeight != physicalHeight_))
            {
                physicalWidth_ = acquiredWidth;
                physicalHeight_ = acquiredHeight;
                RecreateDepthTexture();
                RecreateMsaaColorTexture();
            }
#endif
        }

        if (!framePending_)
            return true;

        // WEBGPU-58: backBuffer (the swapchain's own single-sample texture) is always the RESOLVE
        // destination while MSAA is active -- the actual render-pass colour attachment is the
        // persistent msaaColorView_ instead. wgpu-native performs the multisample resolve
        // automatically as the render pass ends (no separate explicit resolve command); storeOp
        // stays Store (not Discard) on the multisampled attachment so a future frame's
        // WGPULoadOp_Load (when no Clear() was queued that frame) still observes real content,
        // exactly like the pre-MSAA behaviour of Load-ing straight from the swapchain texture.
        // REMED-GFX-131: created explicitly in surfaceFormat_ (CNA's non-sRGB SurfaceFormat::Color
        // mapping) rather than with a null descriptor, which would inherit the surface texture's
        // own configured format. The two differ only on a surface that offers no non-sRGB format at
        // all, where ConfigureSurface() listed this format in viewFormats precisely so this view is
        // legal; everywhere else this is the texture's own format and the call is unchanged.
        WGPUTextureViewDescriptor backBufferViewDescriptor{};
        backBufferViewDescriptor.label = StringView("CNA WebGPU Backbuffer View");
        backBufferViewDescriptor.format = surfaceFormat_;
        backBufferViewDescriptor.dimension = WGPUTextureViewDimension_2D;
        backBufferViewDescriptor.baseMipLevel = 0;
        backBufferViewDescriptor.mipLevelCount = 1;
        backBufferViewDescriptor.baseArrayLayer = 0;
        backBufferViewDescriptor.arrayLayerCount = 1;
        backBufferViewDescriptor.aspect = WGPUTextureAspect_All;
        WGPUTextureView backBuffer = wgpuTextureCreateView(acquiredTexture_, &backBufferViewDescriptor);
        const bool useMsaa = sampleCount_ > 1 && msaaColorView_ != nullptr;
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU Frame Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        // WEBGPU-29/30: the blend constant and the reference stencil are genuinely dynamic
        // wgpu-native render-pass state (unlike blend/cull/wireframe/depthBias, which are baked
        // per-pipeline above) and are applied once per PASS from whatever is currently stored.
        // REMED-GFX-116: the VIEWPORT is no longer in that list. It used to be applied here from
        // the live viewport*_ members, which handed every already-queued draw whatever viewport
        // happened to be current at flush time; BeginPassViewport() now installs the whole target
        // and each draw applies the Viewport captured at its own public call.
        // REMED-GFX-146: neither is the SCISSOR, for exactly the same reason. It used to be
        // computed here from the live scissorEnabled_/scissorX_/scissorY_/scissorW_/scissorH_
        // members, so several rectangles in one bind cycle collapsed last-wins and the full-target
        // rectangle SetRenderTarget installs on every bind unclipped every already-queued draw.
        // BeginPassScissor() opens the pass at the whole target and ApplyDrawScissor() replays each
        // command's own captured state -- including its ScissorTestEnable, since WebGPU has no
        // separate scissor-test toggle and "disabled" therefore means "rect == the whole target".
        // REMED-GFX-159: ONE ordered replay across all eleven deferred families. This used
        // to be a fixed list of per-family loops ending in the sprites, justified as "3D first,
        // 2D SpriteBatch/UI on top -- matches typical XNA game draw order". A typical order is
        // not a contract: it made the source order of those calls, rather than the game's, decide
        // what executed first, so a game drawing its HUD before its world got the two swapped.
        // REMED-GFX-156: and Clear() is in that same stream now, so the cycle may become several
        // native passes -- ReplayOrderedSegments owns the whole loop, including the per-pass state
        // above, which no longer survives from one segment to the next.
        PassDestination destination;
        destination.colorView = useMsaa ? msaaColorView_ : backBuffer;
        destination.resolveView = useMsaa ? backBuffer : nullptr;
        destination.depthView = depthView_;
        destination.colorFormat = surfaceFormat_;
        destination.sampleCount = static_cast<std::uint32_t>(std::max(1, sampleCount_));
        destination.width = physicalWidth_;
        destination.height = physicalHeight_;
        destination.discardFirstSegment = false;   // the backbuffer has no RenderTargetUsage.
        destination.passLabel = "CNA WebGPU Main RenderPass";
        destination.traceName = "backbuffer";
        ReplayOrderedSegments(encoder, destination);

        CaptureReadback(encoder, acquiredTexture_);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU Frame Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commandBuffer);
        ++queueSubmitCount_;
        wgpuCommandBufferRelease(commandBuffer);
        wgpuTextureViewRelease(backBuffer);

        // Transient per-draw colored3D resources are only safe to release once the command
        // buffer referencing them has actually been submitted -- see pendingBufferReleases_'s
        // own doc comment in the header.
        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        pendingBindGroupReleases_.clear();
        pendingBufferReleases_.clear();

        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        framePending_ = false;
        return true;
    }

    void WebGPURenderer::RenderPendingDrawsToRenderTarget(WebGPURenderTargetRenderer* target)
    {
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTarget Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        // WEBGPU-58: ColorAttachmentView() is the multisampled texture's view while this target
        // is multisampled (mirroring the owner's global sampleCount_ -- see
        // WebGPURenderTargetRenderer's own class comment), with target->View()'s single-sample
        // colorView_ as the resolveTarget wgpu-native resolves into automatically; both are the
        // same texture/view (no resolve) when this target is not multisampled, identical to
        // before MSAA existed.
        // RenderTargetUsage.DiscardContents (XNA's own default) means content is not guaranteed
        // to survive between separate SetRenderTarget bind cycles -- mirrors
        // VulkanRenderTargetRenderer::GetRenderPass()'s own discardContents=!preserveContents_
        // choice, applied on every bind cycle (not just this target's first-ever use). An
        // explicit Clear()/ClearColorAndDepth()/etc. call always wins regardless of
        // preserveContents (matches "Clear() clears whichever target is currently active" XNA
        // semantics). Like Vulkan, the clear VALUE used when no explicit Clear() happened is
        // whatever this renderer's global clearColor_/clearDepth_/clearStencil_ currently holds
        // (potentially stale from the last explicit Clear() anywhere, not a fresh per-target
        // default) -- a real, documented imprecision this shares with the Vulkan reference
        // renderer rather than a new one introduced here.
        // WEBGPU-53/54/8/9: this target's own real Depth24PlusStencil8 attachment is always
        // present -- see WebGPURenderTargetRenderer's constructor comment.
        // REMED-GFX-159/156: ONE ordered replay across all eleven deferred families AND the
        // ordered Clear()s between them, which may cut this bind cycle into several native passes.
        // The per-family loops this replaced meant the source order of those calls -- not the
        // game's -- decided what executed first.
        PassDestination destination;
        destination.colorView = target->ColorAttachmentView();
        destination.resolveView = target->ResolveTargetView();
        destination.depthView = target->DepthView();
        destination.colorFormat = target->ColorFormat();
        destination.sampleCount = static_cast<std::uint32_t>(std::max(1, target->GetMultiSampleCount()));
        destination.width = target->GetWidth();
        destination.height = target->GetHeight();
        destination.discardFirstSegment = !target->PreserveContents();
        destination.passLabel = "CNA WebGPU RenderTarget RenderPass";
        destination.traceName = "rendertarget2d";
        // WEBGPU-85/87 MRT: when extra targets are bound alongside this slot-0 target, add one
        // attachment per extra target (slots 1..N-1). The depth attachment stays shared (slot 0's).
        // SetRenderTargets validated that every slot shares width/height/sample count.
        if (!mrtExtraTargets_.empty())
        {
            destination.colorAttachmentCount = 1 + static_cast<int>(mrtExtraTargets_.size());
            destination.passLabel = "CNA WebGPU MRT RenderPass";
            destination.traceName = "mrt";
            for (std::size_t i = 0; i < mrtExtraTargets_.size(); ++i)
            {
                WebGPURenderTargetRenderer* extra = mrtExtraTargets_[i];
                const std::size_t slot = i + 1;
                destination.mrtColorViews[slot] = extra->ColorAttachmentView();
                destination.mrtResolveViews[slot] = extra->ResolveTargetView();
                destination.mrtColorFormats[slot] = extra->ColorFormat();
            }
        }
        ReplayOrderedSegments(encoder, destination);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU RenderTarget Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commandBuffer);
        ++queueSubmitCount_;
        wgpuCommandBufferRelease(commandBuffer);

        // Same "only release after the referencing command buffer is submitted" rule as
        // EnsureFrameRendered() -- see pendingBufferReleases_'s own doc comment in the header.
        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        pendingBindGroupReleases_.clear();
        pendingBufferReleases_.clear();

        // REMED-GFX-159: ReplayDrawsInOrder() clears the ordered stream and all eleven family
        // vectors together at its own tail -- the vectors have to outlive the whole walk now, so
        // no family may drop its own commands part-way through it.
        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        // framePending_ is only ever set true by a Clear()/Queue*Draw() call while THIS target
        // was the current one (backbuffer-only queuing is impossible during the window between
        // switching to this target and switching away from it again, the only window in which
        // this function runs) -- so, having just flushed everything responsible for that, it is
        // safe to reset here too, exactly like the 3 clear-pending flags above. Without this, a
        // later SetRenderTarget2D(nullptr)/Present()/ReadBackbuffer() call would see a stale
        // framePending_==true left over from this target's own draws and render one harmless but
        // wasted extra empty backbuffer pass.
        framePending_ = false;
    }

    // WEBGPU-114: the cube-face counterpart of RenderPendingDrawsToRenderTarget() immediately
    // above -- near-identical structure, with one difference: the colour attachment is one face's
    // own single-layer 2D view (no MSAA/resolveTarget -- this class does not implement MSAA at
    // all, see its own doc comment).
    // REMED-GFX-136: the second difference is gone. This function used to set
    // `loadOp = WGPULoadOp_Clear` unconditionally -- RenderTargetUsage::DiscardContents behaviour
    // on every bind cycle whatever the game asked for -- because
    // IGraphicsRenderer::CreateRenderTargetCube had no preserveContents parameter to thread a
    // RenderTargetCube's real usage through. It has one now, so the choice below is the same
    // `clearPending || !preserveContents` expression the 2D sibling above already used.
    void WebGPURenderer::RenderPendingDrawsToRenderTargetCubeFace(WebGPURenderTargetCubeRenderer* target, int face)
    {
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTargetCube Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        // REMED-GFX-136: `discard` is read from the target THIS pass is being created for, at the
        // moment the pass descriptor is built, and preserveContents_ is immutable after
        // construction -- so an A -> B -> A sequence can never apply target B's policy to target A,
        // whichever order the flushes happen in.
        // REMED-GFX-142: depth/stencil now follows the same rule as the 2D sibling above, which is
        // also FNA3D's own (its native GPU driver loads both aspects unless a clear is pending, and
        // its GL and D3D11 drivers preserve them because an FBO attachment and a DSV simply
        // persist). This used to clear unconditionally on the reasoning that RenderTargetUsage is
        // a colour contract and that one depth texture shared by six faces would "hand face A
        // whatever depth face B last wrote" -- but FNA3D documents `preserveTargetContents` as
        // storing the "color/depth/stencil" contents, and FNA's RenderTargetCube allocates exactly
        // ONE glDepthStencilBuffer for the whole cube, so face A seeing face B's depth IS the
        // reference behaviour rather than a hazard to design around.
        // REMED-GFX-159/156: ONE ordered replay across all eleven deferred families and the
        // ordered Clear()s between them. `discard` is read from the target THIS cycle is being
        // recorded for, so an A -> B -> A sequence can never apply target B's policy to target A.
        // This face's own single-layer 2D view is the colour attachment -- this class implements no
        // MSAA at all, so there is never a resolve target.
        PassDestination destination;
        destination.colorView = target->ColorAttachmentView(face);
        destination.resolveView = nullptr;
        destination.depthView = target->DepthView();
        destination.colorFormat = target->ColorFormat();
        destination.sampleCount = 1;
        destination.width = target->GetSize();
        destination.height = target->GetSize();
        destination.discardFirstSegment = !target->PreserveContents();
        destination.passLabel = "CNA WebGPU RenderTargetCube RenderPass";
        destination.traceName = "rendertargetcube-face";
        ReplayOrderedSegments(encoder, destination);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU RenderTargetCube Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commandBuffer);
        ++queueSubmitCount_;
        wgpuCommandBufferRelease(commandBuffer);

        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        pendingBindGroupReleases_.clear();
        pendingBufferReleases_.clear();

        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        framePending_ = false;
    }

    // WEBGPU-114: single shared "flush whatever is currently bound into its own render pass"
    // entry point -- extends the pre-existing SetRenderTarget2D()-only switch logic (previously
    // an inline if/else choosing between RenderPendingDrawsToRenderTarget() and
    // EnsureFrameRendered()) to a 3rd case, a currently-bound RenderTargetCube face, so switching
    // between ANY pair of {backbuffer, RenderTarget2D, RenderTargetCube face} always flushes the
    // OUTGOING one first. Deliberately does not touch currentRenderTarget_/
    // currentRenderTargetCubeFace_ itself -- every call site immediately reassigns them right
    // after calling this, exactly like the pre-existing SetRenderTarget2D() body did.
    void WebGPURenderer::FlushCurrentRenderTarget()
    {
        if (currentRenderTarget_ != nullptr)
        {
            RenderPendingDrawsToRenderTarget(currentRenderTarget_);
        }
        else if (currentRenderTargetCubeFace_ != nullptr)
        {
            RenderPendingDrawsToRenderTargetCubeFace(currentRenderTargetCubeFace_, currentRenderTargetCubeFaceIndex_);
        }
        else
        {
            // Flushes whatever Clear()/draws were queued against the backbuffer so far this
            // frame, exactly like a mid-frame ReadBackbuffer() call already does -- see
            // EnsureFrameRendered()'s own comment. A no-op (beyond a possible early swapchain
            // acquisition) if nothing was actually queued for the backbuffer yet this frame.
            EnsureFrameRendered();
        }
    }

    void WebGPURenderer::Present()
    {
        if (!EnsureFrameRendered())
            return;
#if !defined(__EMSCRIPTEN__)
        // In the browser there is no explicit present: emdawnwebgpu aborts on wgpuSurfacePresent
        // ("use requestAnimationFrame instead"). The canvas's current texture is shown automatically
        // once control returns to the event loop, which Game::RunLoop() does every frame by awaiting
        // requestAnimationFrame. The queue submit in EnsureFrameRendered() above is the whole frame.
        wgpuSurfacePresent(surface_);
#endif
        wgpuTextureRelease(acquiredTexture_);
        acquiredTexture_ = nullptr;
        hasAcquiredTexture_ = false;
    }

    void WebGPURenderer::CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture)
    {
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
        {
            readbackValid_ = false;
            return;
        }

        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(physicalWidth_) * 4);
        const std::uint64_t requiredCapacity =
            static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(physicalHeight_);
        if (readbackBuffer_ == nullptr || readbackBufferCapacity_ < requiredCapacity)
        {
            if (readbackBuffer_ != nullptr)
                wgpuBufferRelease(readbackBuffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU Readback Buffer");
            descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            descriptor.size = requiredCapacity;
            readbackBuffer_ = wgpuDeviceCreateBuffer(device_, &descriptor);
            readbackBufferCapacity_ = requiredCapacity;
        }
        if (readbackBuffer_ == nullptr)
        {
            readbackValid_ = false;
            return;
        }

        WGPUTexelCopyTextureInfo source{};
        source.texture = surfaceTexture;
        source.mipLevel = 0;
        source.origin = WGPUOrigin3D{0, 0, 0};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer_;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(physicalHeight_);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(physicalWidth_),
                                     static_cast<std::uint32_t>(physicalHeight_), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        readbackBytesPerRow_ = bytesPerRow;
        readbackWidth_ = physicalWidth_;
        readbackHeight_ = physicalHeight_;
        readbackValid_ = true;
    }

    void WebGPURenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0)
            return;

        // Render whatever Clear()/sprite work has been queued so far this logical frame before
        // reading it back, so a Clear()+GetBackBufferData() pair observes its own frame's result
        // without needing a real Present() in between (matches Vulkan/Bgfx's on-demand submit).
        EnsureFrameRendered();

        if (const char* trace = std::getenv("CNA_BACKBUFFER_READ_TRACE"); trace != nullptr && *trace != '\0')
        {
            std::fprintf(stderr,
                         "[GFX-165][WEBGPU] ReadBackbuffer req=(%d,%d,%dx%d) physical=%dx%d "
                         "readback=%dx%d bytesPerRow=%u virtual=%dx%d mode=%d\n",
                         x, y, w, h, physicalWidth_, physicalHeight_, readbackWidth_, readbackHeight_,
                         readbackBytesPerRow_, virtualWidth_, virtualHeight_,
                         static_cast<int>(presentationMode_));
            std::fflush(stderr);
        }

        if (!readbackValid_ || readbackBuffer_ == nullptr)
        {
            std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            return;
        }

        const std::uint64_t mapSize = readbackBufferCapacity_;
        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = kCnaWebGpuCallbackMode;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer_, WGPUMapMode_Read, 0, mapSize, callbackInfo);
        WaitForCompletion(instance_, mapState.completed, "readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
            throw std::runtime_error("CNA WebGPU: readback buffer map failed: " + mapState.error);

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer_, 0, mapSize));
        const bool isBgra = (surfaceFormat_ == WGPUTextureFormat_BGRA8Unorm ||
                             surfaceFormat_ == WGPUTextureFormat_BGRA8UnormSrgb);
        if (mapped == nullptr)
        {
            std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        }
        else
        {
            for (int row = 0; row < h; ++row)
            {
                const int sy = y + row;
                for (int col = 0; col < w; ++col)
                {
                    const int sx = x + col;
                    std::uint8_t* d = pixels + (static_cast<std::size_t>(row) * w + col) * 4;
                    if (sx < 0 || sx >= readbackWidth_ || sy < 0 || sy >= readbackHeight_)
                    {
                        d[0] = d[1] = d[2] = d[3] = 0;
                        continue;
                    }
                    const std::uint8_t* s =
                        mapped + static_cast<std::size_t>(sy) * readbackBytesPerRow_ + static_cast<std::size_t>(sx) * 4;
                    if (isBgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                    else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
                }
            }
        }
        wgpuBufferUnmap(readbackBuffer_);

#if defined(__EMSCRIPTEN__)
        // WEBGPU-133: the buffer map above yielded to the browser event loop (emscripten_sleep,
        // Asyncify), which lets the browser present and invalidate the canvas's current surface
        // texture. Mark it stale rather than release it now: a subsequent same-frame read (the
        // skinned suite reads two points per check) must still reuse the already-captured
        // readbackBuffer_ without forcing a re-acquire, while the next actual RENDER must re-acquire a
        // fresh texture instead of resubmitting this destroyed one (Dawn rejects that as "Destroyed
        // texture ... used in a submit"; wgpu-native tolerates it, so this is web only).
        // EnsureFrameRendered() consumes the flag: it discards the stale texture only when it is
        // about to render. The frame content was already copied into readbackBuffer_ before the
        // yield, so nothing is lost.
        acquiredBackbufferStale_ = true;
#endif
    }

    void WebGPURenderer::DiscardAcquiredBackbuffer()
    {
        if (hasAcquiredTexture_ && acquiredTexture_ != nullptr)
            wgpuTextureRelease(acquiredTexture_);
        acquiredTexture_ = nullptr;
        hasAcquiredTexture_ = false;
        acquiredBackbufferStale_ = false;
    }

    void WebGPURenderer::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void WebGPURenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void WebGPURenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA WebGPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void WebGPURenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surfaceState_.Update(surface);
#if defined(__APPLE__)
        const auto drawableSize = surfaceState_.GetDrawableSize();
        ResizeWebGPUMetalLayer(metalSurfaceOwner_, drawableSize.width, drawableSize.height,
                               surfaceState_.GetDisplayScale());
#endif
    }

    void WebGPURenderer::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        if (swapInterval_ != interval)
        {
            swapInterval_ = interval;
            ConfigureSurface(true);
        }
    }

    bool WebGPURenderer::Supports4xMsaa()
    {
        if (msaa4xSupported_ >= 0)
            return msaa4xSupported_ != 0;

        // Pessimistic default if the probe itself cannot even run (e.g. called before the device/
        // surface format are known -- should not happen in practice, ApplyMultiSampleCount() is
        // only ever reachable after construction, but defend anyway).
        msaa4xSupported_ = 0;
        if (device_ == nullptr || instance_ == nullptr || surfaceFormat_ == WGPUTextureFormat_Undefined)
            return false;

        // WEBGPU-58: the WebGPU spec text says GPUMultisampleState.count "must be 1 or 4", but
        // this renderer verifies it empirically against the concrete adapter/device/surfaceFormat_
        // combination actually in use here (e.g. a software Vulkan/llvmpipe fallback under a
        // headless Xvfb CI run) rather than trusting spec-compliance blindly -- a scoped
        // WGPUErrorFilter_Validation error scope around a scratch sampleCount=4 RENDER_ATTACHMENT
        // texture creation, mirroring this file's own WaitForCompletion()-based async-callback
        // pattern used for adapter/device requests and buffer maps.
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU MSAA Support Probe");
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{4, 4, 1};
        descriptor.format = surfaceFormat_;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 4;
        WGPUTexture probe = wgpuDeviceCreateTexture(device_, &descriptor);
        if (probe != nullptr)
            wgpuTextureRelease(probe);

        ErrorScopeState state;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = kCnaWebGpuCallbackMode;
        callback.callback = OnPopErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(device_, callback);
        WaitForCompletion(instance_, state.completed, "MSAA support probe");

        msaa4xSupported_ = (probe != nullptr && state.ok) ? 1 : 0;
        return msaa4xSupported_ != 0;
    }

    int WebGPURenderer::PickSampleCount(int requestedMultiSampleCount)
    {
        if (requestedMultiSampleCount < 2)
            return 1;
        return Supports4xMsaa() ? 4 : 1;
    }

    void WebGPURenderer::ClearAllPipelineCaches()
    {
        // WEBGPU-58 finding: merely releasing the cached WGPURenderPipeline objects and
        // recreating them from the EXISTING, long-lived coloredShader_/coloredBindGroupLayout_/
        // coloredPipelineLayout_ (and the equivalent members for every other 3D shader family) is
        // NOT enough -- empirically verified (via a standalone probe reusing this renderer's own
        // real device_/queue_) that a WGPUShaderModule/WGPUBindGroupLayout/WGPUPipelineLayout
        // that has ALREADY been used to create at least one WGPURenderPipeline, when reused
        // UNCHANGED to create a NEW pipeline with a DIFFERENT WGPUMultisampleState.count, silently
        // produces a pipeline that does not actually render/resolve correctly on this wgpu-native
        // version/driver -- with no validation error at all (pipeline creation succeeds, the
        // draw call succeeds, only the final pixels are wrong). A completely fresh shader module
        // + bind group layout + pipeline layout combination (identical WGSL source, identical
        // layout shape) at the new sample count renders correctly. The safe, general fix is to
        // fully tear down and recreate every 3D/sprite shader module, bind group layout, pipeline
        // layout AND pipeline cache -- not merely the pipeline objects -- exactly once whenever
        // sampleCount_ actually changes (this is why every one of the CreateXResources() functions
        // below starts with its own DestroyXResources() call). Order matches ConfigureSurface()'s
        // own dependency chain (CreateTexturedResources()/CreateLitTexturedResources()/
        // CreateAlphaTestResources()/CreateSkinnedResources() need coloredBindGroupLayout_/
        // texturedBindGroupLayout_ to already exist; CreateSkinnedPbrResources() needs
        // pbrBindGroupLayout1_ from CreatePbrResources()).
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;
        CreateSpriteResources();
        CreateColoredResources();
        CreateTexturedResources();
        CreateLitTexturedResources();
        CreateAlphaTestResources();
        CreateDualTextureResources();
        CreateEnvMapResources();
        CreateInstancedResources();
        CreatePbrResources();
        CreateSkinnedResources();
        CreateSkinnedPbrResources();
    }

    int WebGPURenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const int newSampleCount = PickSampleCount(requestedMultiSampleCount);
        if (newSampleCount == sampleCount_)
            return GetMultiSampleCount();

        sampleCount_ = newSampleCount;

        // Every previously-created 3D/sprite pipeline baked the OLD sampleCount_ into its own
        // WGPUMultisampleState.count -- release them all so the next Queue*Draw()/RenderSprites()
        // lazily rebuilds with the new value (mirrors
        // VulkanRenderer::ApplyMultiSampleCount()'s identical "invalidate every pipeline
        // cache" approach; a rare, not-a-hot-path event).
        ClearAllPipelineCaches();

        // The backbuffer's own depth buffer and (while MSAA is now active) multisampled colour
        // buffer must be rebuilt at the new sample count/current physical size.
        RecreateDepthTexture();
        RecreateMsaaColorTexture();

        // Both of the textures just (re)created above have entirely UNDEFINED initial GPU
        // content -- unlike a normal resize (where RecreateDepthTexture() alone runs but a
        // Clear() almost always follows soon after anyway), nothing else guarantees
        // clearDepthPending_/clearColorPending_ are still true at this exact moment: if the very
        // last Clear() before this call already consumed them (clearDepthPending_ reset to false
        // at EnsureFrameRendered()'s own tail), the NEXT render pass would use
        // WGPULoadOp_Load on a depth attachment that was never actually cleared, reading
        // whatever undefined bytes the driver happened to leave there -- which, for a
        // LessEqual-vs-a-cleared-1.0 depth test, is observably non-deterministic (some drivers
        // zero-initialize fresh allocations, some reuse recently-freed memory verbatim) rather
        // than a hard failure, making this exact bug easy to miss in ad-hoc testing. Force a real
        // clear on the next render pass for all three attachments to guarantee well-defined
        // content regardless of what any earlier Clear() call already consumed.
        clearColorPending_ = true;
        clearDepthPending_ = true;
        clearStencilPending_ = true;

        if (sampleCount_ > 1)
            std::fprintf(stderr, "[WebGPU] MultiSampleCount reset to %dx\n", sampleCount_);
        else
            std::fprintf(stderr, "[WebGPU] MultiSampleCount reset to disabled (1x)\n");

        return GetMultiSampleCount();
    }

    int WebGPURenderer::GetMultiSampleCount() const
    {
        return sampleCount_ > 1 ? sampleCount_ : 0;
    }

    bool WebGPURenderer::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        const float drawableX = surfaceState_.WindowToDrawable(windowX);
        const float drawableY = surfaceState_.WindowToDrawable(windowY);
        logicalX = (drawableX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (drawableY - viewport.y) * viewport.logicalHeight / viewport.height;
        return drawableX >= viewport.x && drawableX < viewport.x + viewport.width &&
               drawableY >= viewport.y && drawableY < viewport.y + viewport.height;
    }

    bool WebGPURenderer::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = surfaceState_.DrawableToWindow(
            viewport.x + logicalX * viewport.width / viewport.logicalWidth);
        windowY = surfaceState_.DrawableToWindow(
            viewport.y + logicalY * viewport.height / viewport.logicalHeight);
        return true;
    }

    std::unique_ptr<ITextureRenderer> WebGPURenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<WebGPUTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> WebGPURenderer::CreateSpriteBatch()
    {
        return std::make_unique<WebGPUSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> WebGPURenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // WEBGPU-53/54: mip-chain regeneration (mipMap=true) is not implemented on this renderer
        // yet -- throwing here (matching this file's own ThrowUnsupported3DDraw() precedent for
        // "genuinely unsupported, not a silent degrade" cases, and CHECKLIST.md's Draco-import
        // convention of a clear error over quietly under-delivering) is deliberately preferred
        // over silently creating a single-level target while RenderTarget2D::RenderTarget2D()'s
        // own CalculateMipLevels() has already told the XNA layer to expect a full mip chain --
        // that mismatch would let Texture2D::SetData/GetData(level>0, ...) silently write into or
        // read from nothing.
        if (mipMap)
            throw std::runtime_error("CNA WebGPU: RenderTarget2D mip-chain regeneration "
                                     "(mipMap=true) is not implemented on this renderer yet -- see "
                                     "plans/plan_webgpu.md WEBGPU-53/54");
        // WEBGPU-58: the per-instance requested multiSampleCount argument is intentionally NOT
        // read here -- WebGPURenderTargetRenderer's own constructor unconditionally mirrors this
        // renderer's CURRENT global sampleCount_ instead (see that class's own top-of-class doc
        // comment for exactly why a per-instance opt-out is unsafe given this renderer's single
        // shared pipeline sample count). RenderTarget2D::RenderTarget2D() reads the real applied
        // value back via GetMultiSampleCount() into its own MultiSampleCount property, matching
        // FNA3D_GetMaxMultiSampleCount's real-clamped-value contract -- 0 whenever the renderer has
        // no MSAA active, exactly as before this task.
        (void) multiSampleCount;
        return std::make_unique<WebGPURenderTargetRenderer>(*this, w, h, depthFormat, preserveContents);
    }

    void WebGPURenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        auto* newTarget = rt != nullptr ? static_cast<WebGPURenderTargetRenderer*>(rt) : nullptr;
        // WEBGPU-114: a currently-bound RenderTargetCube face must NOT be treated as "no change"
        // just because newTarget (a RenderTarget2D pointer, possibly nullptr) happens to equal
        // currentRenderTarget_ (also nullptr while a cube face is bound, since the two are
        // mutually exclusive -- see currentRenderTargetCubeFace_'s own doc comment) -- this is the
        // path SetRenderTargetCubeFace(nullptr, face)'s IGraphicsRenderer default takes to restore
        // the backbuffer, and it must genuinely flush the cube face's own pending draws.
        // WEBGPU-87: while an MRT set is bound (mrtExtraTargets_ non-empty) a re-bind of the same
        // slot-0 target as a SINGLE target is a real change (drop from N to 1) and must not early-out.
        if (newTarget == currentRenderTarget_ && currentRenderTargetCubeFace_ == nullptr &&
            mrtExtraTargets_.empty())
            return;

        // WEBGPU-53/54: this renderer renders every Clear()/3D-draw/SpriteBatch command queued
        // during a logical frame into exactly ONE deferred render pass, lazily, the first time
        // something actually needs the frame to be real (EnsureFrameRendered(), called from
        // Present()/ReadBackbuffer()). Supporting RenderTarget2D means a game can bind an RT,
        // draw, switch back to the backbuffer, draw more -- all inside one logical frame -- and
        // those two sets of draws must NOT collapse into a single pass against whichever target
        // happens to be active at final-flush time. Two designs were considered: (a) eagerly
        // flush/render whatever is currently queued for the CURRENT target the instant
        // SetRenderTarget2D switches to a DIFFERENT target, closing out that render pass
        // immediately and starting fresh accumulation for the new target; or (b) tag every queued
        // draw command (all ~10 of this renderer's Queue*Draw families) with which target it
        // belongs to and replay them grouped-by-target at final-flush time, closer to
        // VulkanRenderer's own deferred/replay model (see its RecordCommandBuffer()).
        // (a) was chosen: it needs one new function (RenderPendingDrawsToRenderTarget) and zero
        // changes to any existing Queue*Draw()/*DrawCommand struct, whereas (b) would add a
        // target-tag field and per-target grouping logic to every one of those ~10 families -- a
        // much larger change for a renderer that, unlike Vulkan, has no pre-existing per-draw
        // deferred/replay infrastructure to extend. WEBGPU-114 extends the same choice to a
        // RenderTargetCube face, generalised into FlushCurrentRenderTarget() so it applies
        // regardless of which of the 3 kinds of target ({backbuffer, RenderTarget2D, cube face})
        // is currently bound.
        FlushCurrentRenderTarget();
        currentRenderTargetCubeFace_ = nullptr;
        currentRenderTargetCubeFaceIndex_ = -1;
        // WEBGPU-87: any single-target / backbuffer bind drops a prior MRT set (already flushed
        // just above by FlushCurrentRenderTarget()).
        mrtExtraTargets_.clear();

        if (newTarget != nullptr)
            newTarget->BindAsRenderTarget();
        else
            currentRenderTarget_ = nullptr;
    }

    void WebGPURenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            // Unbind every slot back to the backbuffer (SetRenderTarget2D clears mrtExtraTargets_
            // after flushing the outgoing target/set).
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1)
        {
            // Single-target and cube-face paths are byte-identical to before; SetRenderTarget2D /
            // SetRenderTargetCubeFace clear any prior MRT set after flushing it.
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(
                    renderTargets[0].GetRenderTargetCube(),
                    renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }

        // WEBGPU-87 MRT: 2..4 simultaneous RenderTarget2D targets. XNA's own ceiling is 4
        // (GraphicsDevice enforces MAX_RENDERTARGET_BINDINGS separately).
        if (count > 4)
            throw System::NotSupportedException(
                "WebGPU: at most 4 simultaneous render targets are supported (XNA's own ceiling), so "
                "binding " + std::to_string(count) + " is refused.");

        std::vector<WebGPURenderTargetRenderer*> targets;
        targets.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw System::NotSupportedException(
                    "WebGPU: a RenderTargetCube face cannot be bound in a multiple-render-target set "
                    "(target " + std::to_string(i) + " is a cube face). Bind up to 4 RenderTarget2D "
                    "targets instead.");
            auto* target = static_cast<WebGPURenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D());
            if (target == nullptr)
                throw System::NotSupportedException(
                    "WebGPU: render target " + std::to_string(i) + " in the MRT set is null.");
            targets.push_back(target);
        }

        // Every attachment in a WebGPU render pass must share width, height and sample count; XNA
        // requires the same. Refuse a mismatch explicitly, naming the target that disagrees, rather
        // than letting wgpu-native reject the pass with an opaque message.
        const int width = targets[0]->GetWidth();
        const int height = targets[0]->GetHeight();
        const int samples = std::max(1, targets[0]->GetMultiSampleCount());
        for (int i = 1; i < count; ++i)
        {
            if (targets[i]->GetWidth() != width || targets[i]->GetHeight() != height)
                throw System::NotSupportedException(
                    "WebGPU: render target " + std::to_string(i) + " is " +
                    std::to_string(targets[i]->GetWidth()) + "x" + std::to_string(targets[i]->GetHeight()) +
                    " but target 0 is " + std::to_string(width) + "x" + std::to_string(height) +
                    "; every target in an MRT set must share dimensions.");
            if (std::max(1, targets[i]->GetMultiSampleCount()) != samples)
                throw System::NotSupportedException(
                    "WebGPU: render target " + std::to_string(i) + " has sample count " +
                    std::to_string(std::max(1, targets[i]->GetMultiSampleCount())) +
                    " but target 0 has " + std::to_string(samples) +
                    "; every target in an MRT set must share a sample count.");
        }

        // Flush the outgoing target/set, then bind slot 0 (which sets currentRenderTarget_) and
        // record slots 1..N-1. BindAsRenderTarget() clears mrtExtraTargets_ via SetRenderTarget2D's
        // path only when called through it -- here it is called directly, so set the extras after.
        FlushCurrentRenderTarget();
        currentRenderTargetCubeFace_ = nullptr;
        currentRenderTargetCubeFaceIndex_ = -1;
        targets[0]->BindAsRenderTarget();
        mrtExtraTargets_.assign(targets.begin() + 1, targets.end());
    }

    std::unique_ptr<ITextureCubeRenderer> WebGPURenderer::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<WebGPUTextureCubeRenderer>(*this, size, mipMap);
    }

    // WEBGPU-114: this renderer's first real RenderTargetCube support -- see
    // WebGPURenderTargetCubeRenderer's own doc comment for exactly what is/isn't implemented
    // (no mip regen, no MSAA -- both deliberate, documented scope cuts).
    std::unique_ptr<IRenderTargetCubeRenderer> WebGPURenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        (void) multiSampleCount; // see WebGPURenderTargetCubeRenderer's own doc comment: ignored.
        // REMED-GFX-136: preserveContents is the public RenderTargetUsage, reaching a cube target
        // for the first time -- see RenderPendingDrawsToRenderTargetCubeFace().
        return std::make_unique<WebGPURenderTargetCubeRenderer>(*this, size, depthFormat,
                                                              preserveContents, mipMap);
    }

    std::unique_ptr<ITexture3DRenderer> WebGPURenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<WebGPUTexture3DRenderer>(*this, w, h, depth, mipMap);
    }

    std::unique_ptr<IVertexBufferRenderer> WebGPURenderer::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<WebGPUVertexBufferRenderer>(*this, vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> WebGPURenderer::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferRenderer>(*this, indexCapacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> WebGPURenderer::CreateIndexBuffer32(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferRenderer>(*this, indexCapacity, true);
    }

    WGPUPrimitiveTopology WebGPURenderer::ToTopology(PrimitiveType primitive) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return WGPUPrimitiveTopology_TriangleList;
            case PrimitiveType::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
            case PrimitiveType::LineList: return WGPUPrimitiveTopology_LineList;
            case PrimitiveType::LineStrip: return WGPUPrimitiveTopology_LineStrip;
            case PrimitiveType::PointListEXT: return WGPUPrimitiveTopology_PointList;
        }
        throw std::invalid_argument("CNA WebGPU: unsupported primitive topology");
    }

    int WebGPURenderer::PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList: return primitiveCount * 2;
            case PrimitiveType::LineStrip: return primitiveCount + 1;
            case PrimitiveType::PointListEXT: return primitiveCount;
        }
        return 0;
    }

    int WebGPURenderer::PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const
    {
        return PrimitiveVertexCount(primitive, primitiveCount);
    }

    [[noreturn]] void WebGPURenderer::ThrowUnsupported3DDraw(const char* method)
    {
        throw std::runtime_error(std::string("CNA WebGPU: ") + method +
                                 " is not implemented in the initial renderer. Clear/present, Texture2D, "
                                 "SpriteBatch and buffer upload are implemented; see plans/plan_webgpu.md Phase 58+ for 3D parity.");
    }

    void WebGPURenderer::QueueColoredDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams* params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        if (webgpuVb.Stride() != 16)
            throw std::invalid_argument("CNA WebGPU: DrawColoredPrimitives requires a stride-16 "
                                        "(VertexPositionColor) vertex buffer");

        ColoredDrawCommand command;
        const int vertexStart = params != nullptr ? params->vertexStart : 0;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * 16u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate in one
        // frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        if (params != nullptr)
        {
            const Matrix wvp = world * view * projection;
            FillExtUniforms(command.uniforms, wvp, *params);
        }
        else
        {
            FillColoredUniforms(command.uniforms, world, view, projection);
        }

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex =
                params != nullptr ? static_cast<std::uint32_t>(params->startIndex) : 0;
            command.baseVertex = params != nullptr ? params->baseVertex : 0;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        coloredDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Colored, coloredDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                        const Matrix& world, const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        RequireSupportedFillModeEXT(primitive, "user-nonindexed");
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount);
    }

    void WebGPURenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                               const IIndexBufferRenderer& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        RequireSupportedFillModeEXT(primitive, "user-indexed");
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount);
    }

    // REMED-GFX-DECL-GUARD lives at the top of each of the three routes below; see the anonymous
    // namespace helper of the same name. WEBGPU-115's fill-mode guard sits beside it, on these
    // three plus the two DrawColoredPrimitives routes above -- the five public 3D draw entry
    // points, which together are the narrowest boundary every Queue*Draw() family passes through.
    void WebGPURenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        RequireSupportedFillModeEXT(primitive, "ordinary-nonindexed");
        RequireFaithfulDeclarationEXT(vb, "ordinary-nonindexed");
        // WEBGPU-76: a bound custom WGSL ShaderEffect owns the whole draw -- its own shaders,
        // vertex layout and uniforms -- so it is routed before any stock stride dispatch below.
        if (params.customEffectRenderer != nullptr)
        {
            QueueCustomEffectDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        // PBR owns its glTF MASK coverage; standalone AlphaTestEffect wins only for non-PBR
        // draws. Dual-texture then wins over env-map/skinned/
        // lit-textured (an AlphaTestEffect or DualTextureEffect draw on a
        // VertexPositionNormalTexture buffer never reaches lit_textured3d -- the normal is simply
        // unread in both cases); env-map wins over lit-textured for the same stride-32 buffer
        // shape (EnvironmentMapEffect's own reflection shader takes over the normal/UV attributes
        // that lit_textured3d.wgsl would otherwise consume for Blinn-Phong lighting).
        const bool needsAlphaTest = !params.pbr &&
                                    (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
        const bool needsUnsupportedEffect = !needsAlphaTest && !needsDualTexture && !needsEnvMap &&
                                            params.skinned;
        const std::size_t stride = webgpuVb.Stride();
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // WEBGPU-25/36/74: EnvironmentMapEffect. Unlike every other stride-32+ effect branch
        // below, this is NOT gated on params.texture0 != nullptr -- both Texture and
        // EnvironmentMap are genuinely optional on real XNA EnvironmentMapEffect instances (see
        // QueueEnvMapDraw()'s own fallback-to-white comment).
        if (needsEnvMap && stride == 32)
        {
            QueueEnvMapDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 16)
        {
            QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        // plans/plan_gltf.md GLTF-474: no `params.texture0 != nullptr` clause. A stock effect's
        // base-colour map is optional in XNA and absent in glTF's own default material, and the
        // replay binds neutral white for it -- so requiring one here did not make the draw safe,
        // it made an untextured primitive fall past every branch into the stride-16 colour path
        // and be refused there. Same defect, and same fix, as SDL_GPU's own.
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture &&
            (stride == 20 || stride == 24))
        {
            QueueTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32)
        {
            QueueLitTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // SkinnedPbrEffect (PBR + skinning combo, stride 68). Checked BEFORE the unskinned-PBR
        // branch below, matching EasyGLRenderer::SelectProgram()'s own priority order
        // (pbr&&skinned combo first, then pbr-only, then skinned-only).
        // plans/plan_gltf.md GLTF-465: strides 80 and 60 join their bare twins, and the `texture0` clause
        // is gone -- a base-colour map is optional in glTF (3.9.2's default material has none) and
        // the PBR replay already binds a neutral-white default for it, so requiring one here only
        // made such a draw fall through to a route that refuses it.
        if (!needsAlphaTest && !needsDualTexture && params.pbr && params.skinned &&
            (stride == 68 || stride == 80))
        {
            QueueSkinnedPbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // Unskinned PbrEffect (stride 48, VertexPositionNormalTangentTexture). Gated on
        // !params.skinned directly (rather than needsUnsupportedEffect, which already excludes
        // skinned draws via its own OR-condition).
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned &&
            (stride == 48 || stride == 60))
        {
            QueuePbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // SkinnedEffect (stride 52, or 56 with VertexColorEnabled). Gated on !params.pbr directly
        // (rather than needsUnsupportedEffect, which already excludes skinned draws via its own
        // OR-condition) so the SkinnedPbrEffect branch above keeps first priority, matching
        // EasyGLRenderer::SelectProgram()'s own ordering.
        if (!needsAlphaTest && !needsDualTexture && params.skinned && !params.pbr &&
            (stride == 52 || stride == 56))
        {
            QueueSkinnedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // Remaining unsupported effect (skinned on a non-skinned stride) or an unmatched
        // stride/texture combination -- fall back exactly like IGraphicsRenderer's own default
        // implementation did before this override existed. This will itself throw for anything
        // other than a stride-16 buffer (DrawColoredPrimitives' own requirement), matching the
        // pre-existing "unsupported, fail loudly" behaviour.
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void WebGPURenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        RequireSupportedFillModeEXT(primitive, "ordinary-indexed");
        RequireFaithfulDeclarationEXT(vb, "ordinary-indexed");
        // WEBGPU-76: a bound custom WGSL ShaderEffect owns the whole draw (see the non-indexed twin).
        if (params.customEffectRenderer != nullptr)
        {
            QueueCustomEffectDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const bool needsAlphaTest = !params.pbr &&
                                    (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
        const bool needsUnsupportedEffect = !needsAlphaTest && !needsDualTexture && !needsEnvMap &&
                                            params.skinned;
        const std::size_t stride = webgpuVb.Stride();
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsEnvMap && stride == 32)
        {
            QueueEnvMapDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 16)
        {
            QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        // plans/plan_gltf.md GLTF-474: no `params.texture0 != nullptr` clause. A stock effect's
        // base-colour map is optional in XNA and absent in glTF's own default material, and the
        // replay binds neutral white for it -- so requiring one here did not make the draw safe,
        // it made an untextured primitive fall past every branch into the stride-16 colour path
        // and be refused there. Same defect, and same fix, as SDL_GPU's own.
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture &&
            (stride == 20 || stride == 24))
        {
            QueueTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32)
        {
            QueueLitTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // See DrawPrimitivesEx()'s identical branch for the SkinnedPbrEffect/PbrEffect/SkinnedEffect
        // priority ordering rationale.
        // plans/plan_gltf.md GLTF-465: strides 80 and 60 join their bare twins, and the `texture0` clause
        // is gone -- a base-colour map is optional in glTF (3.9.2's default material has none) and
        // the PBR replay already binds a neutral-white default for it, so requiring one here only
        // made such a draw fall through to a route that refuses it.
        if (!needsAlphaTest && !needsDualTexture && params.pbr && params.skinned &&
            (stride == 68 || stride == 80))
        {
            QueueSkinnedPbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned &&
            (stride == 48 || stride == 60))
        {
            QueuePbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsAlphaTest && !needsDualTexture && params.skinned && !params.pbr &&
            (stride == 52 || stride == 56))
        {
            QueueSkinnedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    void WebGPURenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
        // WEBGPU-115: before the no-instance-stream fallback below, so a refused instanced draw
        // names its own route rather than re-entering DrawIndexedPrimitivesEx and reporting the
        // ordinary one.
        RequireSupportedFillModeEXT(primitive, "instanced");
        // REMED-GFX-202: the per-instance stream is the lowest-slot entry of the shared
        // GpuVertexStreamBinding array whose InstanceFrequency is greater than zero.
        const auto* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr)
        {
            // No per-instance VB -- fall back to a single-instance indexed draw, matching
            // VulkanRenderer::DrawInstancedPrimitivesEx's own identical fallback.
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(ib);
        // REMED-GFX-202: one stream of each rate (REMED-GFX-205 tracks widening it).
        RejectUnsupportedStreamCombination(params, "The WebGPU renderer");
        // REMED-GFX-DECL-GUARD: the geometry stream's declaration, against the Instanced3D
        // module's own inferred layout.
        RequireFaithfulDeclarationEXT(vb, "instanced", /*positionOnlyFallback=*/true);
        const auto& webgpuInstVb =
            static_cast<const WebGPUVertexBufferRenderer&>(*instanceStream->buffer);

        // REMED-GFX-211: the GEOMETRY binding's own VertexOffset, which this route dropped. The
        // command copies the whole per-vertex buffer and the replay binds that copy at byte offset
        // zero, so binding 0 has no per-binding native offset channel to carry it -- but
        // wgpuRenderPassEncoderDrawIndexed's own `baseVertex` is added to every decoded index,
        // which is exactly the term this stream owes, and it reaches the per-vertex binding only.
        // The route binds exactly one per-vertex stream (RejectUnsupportedStreamCombination above),
        // so folding it there applies the offset once, to its own stream only: the fetched element
        // becomes `VertexOffset + baseVertex + index`. The index buffer is untouched -- startIndex
        // stays an index-element offset, carried by firstIndex below.
        const GpuVertexStreamBinding* perVertexStream = FirstPerVertexStream(params);
        const int perVertexOffset = perVertexStream != nullptr ? perVertexStream->vertexOffset : 0;
        const int instCountClamped = std::max(1, instanceCount);
        const int instanceFrequency = std::max(1, instanceStream->instanceFrequency);
        const int lastInstanceRecord =
            instanceStream->vertexOffset + (instCountClamped - 1) / instanceFrequency;

        // REMED-GFX-211/213: the shared layer validates both of these before dispatch
        // (ValidateVertexStreamRanges / ValidateInstanceStreamRanges), so neither can fire for a
        // draw that arrived through GraphicsDevice. They exist because Draw*PrimitivesEx is a
        // public interface method a harness may call with a hand-built GpuDrawParams, and because
        // an offset that was previously ignored now selects a real source range -- an out-of-range
        // one must name its slot here rather than silently select the wrong records or drop the
        // draw at replay.
        const int perVertexCount = webgpuVb.GetVertexCount();
        if (perVertexOffset < 0 || perVertexOffset > perVertexCount ||
            params.baseVertex > perVertexCount - perVertexOffset)
        {
            throw std::runtime_error(
                "The WebGPU renderer: the per-vertex VertexBufferBinding.VertexOffset bound to slot " +
                std::to_string(perVertexStream != nullptr ? perVertexStream->slot : 0) +
                " leaves its own vertex buffer.");
        }
        if (instanceStream->vertexOffset < 0 || lastInstanceRecord >= webgpuInstVb.GetVertexCount())
        {
            throw std::runtime_error(
                "The WebGPU renderer: the per-instance VertexBufferBinding bound to slot " +
                std::to_string(instanceStream->slot) + " does not hold record " +
                std::to_string(lastInstanceRecord) + '.');
        }

        InstancedDrawCommand command;
        command.pvStride = webgpuVb.Stride() > 0 ? webgpuVb.Stride() : 20;
        command.instVbStride = webgpuInstVb.Stride() > 0 ? webgpuInstVb.Stride() : 64;
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        command.instanceCount = static_cast<std::uint32_t>(instCountClamped);

        // Copies the FULL per-vertex buffer (matches VulkanRenderer::
        // DrawInstancedPrimitivesEx's own identical "no vertexStart applied" simplification --
        // WEBGPU-70's documented, pre-existing baseVertex/vertexStart gap, not something this task
        // introduces). The replay binds this copy at byte offset zero, which is why REMED-GFX-211's
        // per-vertex term rides the native draw's baseVertex rather than this copy's source base.
        const auto& vbShadow = webgpuVb.ShadowData();
        const std::size_t vertexByteCount = static_cast<std::size_t>(webgpuVb.GetVertexCount()) * command.pvStride;
        if (vertexByteCount <= vbShadow.size())
            command.vertexData.assign(vbShadow.begin(), vbShadow.begin() + static_cast<std::ptrdiff_t>(vertexByteCount));
        command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount());

        // Per-instance data: one destination record per instance, exactly as before -- only WHICH
        // source record each one takes changed.
        //
        // REMED-GFX-211: the first source record is this stream's own VertexOffset, converted with
        // this stream's own stride, never binding 0's, and never advanced by baseVertex, which
        // addresses a per-vertex stream only.
        //
        // REMED-GFX-213: instance i takes record `VertexOffset + i / InstanceFrequency` -- the same
        // rule glVertexAttribDivisor and D3D11's InstanceDataStepRate define, and the one EasyGL
        // already implements natively. The grouping is expanded here rather than bound natively
        // because the native capability is absent, not merely unused: in wgpu-native v29.0.1.1
        // WGPUVertexBufferLayout carries only nextInChain/stepMode/arrayStride/attributes, no
        // WGPUNativeSType extends it, WGPUVertexStepMode offers only Vertex and Instance, and no
        // WGPUNativeFeature adds a step rate -- so binding 1 keeps WGPUVertexStepMode_Instance's
        // implicit divisor of one. The destination is still one record per instance, so nothing
        // about the native binding, the pipeline or its cache key moves and no frequency reaches
        // them: the divisor is a data-preparation concern only. Frequency 1 stays the single bulk
        // copy it has always been.
        const auto& instShadow = webgpuInstVb.ShadowData();
        const std::size_t instSrcBase =
            static_cast<std::size_t>(instanceStream->vertexOffset) * command.instVbStride;
        const std::size_t instSrcByteCount =
            static_cast<std::size_t>(lastInstanceRecord + 1) * command.instVbStride;
        if (instSrcByteCount <= instShadow.size())
        {
            command.instVbData.resize(
                static_cast<std::size_t>(instCountClamped) * command.instVbStride);
            if (instanceFrequency == 1)
            {
                std::memcpy(command.instVbData.data(), instShadow.data() + instSrcBase,
                            command.instVbData.size());
            }
            else
            {
                for (int i = 0; i < instCountClamped; ++i)
                    std::memcpy(
                        command.instVbData.data() +
                            static_cast<std::size_t>(i) * command.instVbStride,
                        instShadow.data() + instSrcBase +
                            static_cast<std::size_t>(i / instanceFrequency) * command.instVbStride,
                        command.instVbStride);
            }
        }

        command.indexed = true;
        command.index32 = webgpuIb.IsThirtyTwoBit();
        command.indexData = webgpuIb.ShadowData();
        command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
        command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
        // REMED-GFX-211: the geometry binding's VertexOffset rides the native draw's own baseVertex
        // term alongside the caller's baseVertex -- captured by value here, so a later
        // SetVertexBuffers cannot reach this already-queued draw. The term stays signed, so a
        // negative baseVertex keeps behaving exactly as it did.
        command.baseVertex = params.baseVertex + perVertexOffset;

        // [0..15]=View*Projection (not a full MVP -- world comes from the per-instance stream),
        // [16..31]=diffuseColor+the same unused-here tail fields as colored3d.wgsl. FillExtUniforms()
        // is reused verbatim: it only cares that its first argument is SOME matrix to dump
        // column-major into [0..15], not specifically a WVP.
        const Matrix vp = view * projection;
        FillExtUniforms(command.uniforms, vp, params);

        instancedDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Instanced, instancedDrawCommands_.size() - 1);
    }

    void WebGPURenderer::IssueColoredDraw(WGPURenderPassEncoder pass,
                                                 const ColoredDrawCommand& command,
                                                 ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty())
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU Colored3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU Colored3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBindGroupEntry bindEntry{};
        bindEntry.binding = 0;
        bindEntry.buffer = uniformBuffer;
        bindEntry.size = sizeof(command.uniforms);
        WGPUBindGroupDescriptor bindDescriptor{};
        bindDescriptor.label = StringView("CNA WebGPU Colored3D BindGroup");
        bindDescriptor.layout = coloredBindGroupLayout_;
        bindDescriptor.entryCount = 1;
        bindDescriptor.entries = &bindEntry;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelineColored3D(
                                                               command.topology,
                                                               RequiredStripIndexFormat(command),
                                                               command.depthTest,
                                                               command.depthWrite, command.depthFunc,
                                                               command.blend, command.blendParams,
                                                               command.cullMode, command.wireframe,
                                                               command.depthBias, command.slopeScaleDepthBias,
                                                               command.stencil);  // WEBGPU-83
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic state; the ops/masks are in the
        // pipeline above). A gate draw and a stamp draw can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU Colored3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(bindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::IssueTexturedDraw(WGPURenderPassEncoder pass,
                                                  const TexturedDrawCommand& command,
                                                  ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.texture)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU Textured3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU Textured3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBindGroupEntry uboEntry{};
        uboEntry.binding = 0;
        uboEntry.buffer = uniformBuffer;
        uboEntry.size = sizeof(command.uniforms);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU Textured3D UBO BindGroup");
        uboBindDescriptor.layout = coloredBindGroupLayout_;
        uboBindDescriptor.entryCount = 1;
        uboBindDescriptor.entries = &uboEntry;
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "Textured3D");
        std::array<WGPUBindGroupEntry, 2> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.texture.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU Textured3D Texture BindGroup");
        texBindDescriptor.layout = texturedBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = command.hasVertexColor
            ? GetOrCreatePipelineColoredTextured3D(
                                                   command.topology,
                                                   RequiredStripIndexFormat(command),
                                                   command.depthTest,
                                                   command.depthWrite, command.depthFunc,
                                                   command.blend, command.blendParams,
                                                   command.cullMode, command.wireframe,
                                                   command.depthBias, command.slopeScaleDepthBias, command.stencil)
            : GetOrCreatePipelineTextured3D(
                                            command.topology,
                                            RequiredStripIndexFormat(command),
                                            command.depthTest,
                                            command.depthWrite, command.depthFunc,
                                            command.blend, command.blendParams,
                                            command.cullMode, command.wireframe,
                                            command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU Textured3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::IssueLitTexturedDraw(WGPURenderPassEncoder pass,
                                                     const LitTexturedDrawCommand& command,
                                                     ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.texture)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU LitTextured3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU LitTextured3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBufferDescriptor lightUboDescriptor{};
        lightUboDescriptor.label = StringView("CNA WebGPU LitTextured3D LightUBO");
        lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        lightUboDescriptor.size = sizeof(command.lightUniforms);
        WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
        wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

        std::array<WGPUBindGroupEntry, 2> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].buffer = uniformBuffer;
        uboEntries[0].size = sizeof(command.uniforms);
        uboEntries[1].binding = 1;
        uboEntries[1].buffer = lightUniformBuffer;
        uboEntries[1].size = sizeof(command.lightUniforms);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU LitTextured3D UBO BindGroup");
        uboBindDescriptor.layout = litBindGroupLayout_;
        uboBindDescriptor.entryCount = uboEntries.size();
        uboBindDescriptor.entries = uboEntries.data();
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "LitTextured3D");
        std::array<WGPUBindGroupEntry, 2> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.texture.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU LitTextured3D Texture BindGroup");
        texBindDescriptor.layout = texturedBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = command.preferVertexLit
            ? GetOrCreatePipelineLitTextured3DVertexLit(
                                                         command.topology,
                                                         RequiredStripIndexFormat(command),
                                                         command.depthTest,
                                                         command.depthWrite, command.depthFunc,
                                                         command.blend, command.blendParams,
                                                         command.cullMode, command.wireframe,
                                                         command.depthBias, command.slopeScaleDepthBias, command.stencil)
            : GetOrCreatePipelineLitTextured3D(
                                                command.topology,
                                                RequiredStripIndexFormat(command),
                                                command.depthTest,
                                                command.depthWrite, command.depthFunc,
                                                command.blend, command.blendParams,
                                                command.cullMode, command.wireframe,
                                                command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU LitTextured3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(lightUniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::QueueLitTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        if (webgpuVb.Stride() != 32)
            throw std::invalid_argument("CNA WebGPU: QueueLitTexturedDraw requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");
        // plans/plan_gltf.md GLTF-474: an absent base-colour map is no longer refused -- the command
        // takes the neutral-white default below, which is the identity for `tex * colour`.

        LitTexturedDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 32u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474: neutral white when the effect binds no texture -- `tex * colour`
        // then collapses to the colour, which is what an untextured stock-effect draw should be.
        EnsurePbrDefaultTextures();
        command.texture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        // Task 1105: XNA's real BasicEffect.PreferPerPixelLighting default is false (per-vertex),
        // matching every other renderer's own dispatch condition for this flag.
        command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        litTexturedDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::LitTextured, litTexturedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssueAlphaTestDraw(WGPURenderPassEncoder pass,
                                                   const AlphaTestDrawCommand& command,
                                                   ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.texture)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU AlphaTest3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU AlphaTest3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBindGroupEntry uboEntry{};
        uboEntry.binding = 0;
        uboEntry.buffer = uniformBuffer;
        uboEntry.size = sizeof(command.uniforms);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU AlphaTest3D UBO BindGroup");
        uboBindDescriptor.layout = coloredBindGroupLayout_;
        uboBindDescriptor.entryCount = 1;
        uboBindDescriptor.entries = &uboEntry;
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "AlphaTest3D");
        std::array<WGPUBindGroupEntry, 2> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.texture.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU AlphaTest3D Texture BindGroup");
        texBindDescriptor.layout = texturedBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelineAlphaTest3D(
                                                                 command.stride,
                                                                 command.topology,
                                                                 RequiredStripIndexFormat(command),
                                                                 command.depthTest, command.depthWrite,
                                                                 command.depthFunc,
                                                                 command.blend, command.blendParams,
                                                                 command.cullMode, command.wireframe,
                                                                 command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU AlphaTest3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::QueueAlphaTestDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24 && stride != 32)
            throw std::invalid_argument("CNA WebGPU: QueueAlphaTestDraw requires a stride-20, "
                                        "-24, or -32 vertex buffer");
        // plans/plan_gltf.md GLTF-474: an absent base-colour map is no longer refused -- the command
        // takes the neutral-white default below, which is the identity for `tex * colour`.

        AlphaTestDrawCommand command;
        command.stride = stride;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474: neutral white when the effect binds no texture -- `tex * colour`
        // then collapses to the colour, which is what an untextured stock-effect draw should be.
        EnsurePbrDefaultTextures();
        command.texture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        const Matrix wvp = world * view * projection;
        FillAlphaTestUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        alphaTestDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::AlphaTest, alphaTestDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssueDualTextureDraw(WGPURenderPassEncoder pass,
                                                     const DualTextureDrawCommand& command,
                                                     ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() ||
            !command.texture0 || !command.texture1)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU DualTexture3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU DualTexture3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBindGroupEntry uboEntry{};
        uboEntry.binding = 0;
        uboEntry.buffer = uniformBuffer;
        uboEntry.size = sizeof(command.uniforms);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU DualTexture3D UBO BindGroup");
        uboBindDescriptor.layout = coloredBindGroupLayout_;
        uboBindDescriptor.entryCount = 1;
        uboBindDescriptor.entries = &uboEntry;
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "DualTexture3D");
        // REMED-GFX-172: Texture2's own SamplerStates[1], from the description captured at this
        // draw's public call. Resolved through the same REMED-GFX-170 sampler cache -- an identical
        // pair of slots is one cache hit, not a second native sampler.
        WGPUSampler sampler1 = GetOrCreateSlotSampler(command.texture1Filter, command.texture1AddressU,
                                                      command.texture1AddressV,
                                                      command.texture1MaxAnisotropy,
                                                      "DualTexture3D/slot1");
        std::array<WGPUBindGroupEntry, 4> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.texture0.View();
        texEntries[2].binding = 2;
        texEntries[2].textureView = command.texture1.View();
        texEntries[3].binding = 3;
        texEntries[3].sampler = sampler1;
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU DualTexture3D Texture BindGroup");
        texBindDescriptor.layout = dualTextureBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        if (MultiTextureSamplerTraceEnabled())
        {
            TraceMultiTextureBinding("DualTexture3D", state.publicOrder, state.replayPosition,
                                     command.texture0.View(), command.texture1.View(),
                                     command.textureFilter, command.addressU, command.addressV,
                                     command.maxAnisotropy,
                                     command.texture1Filter, command.texture1AddressU,
                                     command.texture1AddressV, command.texture1MaxAnisotropy,
                                     sampler, sampler1, sampler1, dualTextureBindGroupLayout_,
                                     dualTexturePipelineLayout_, texBindGroup, texEntries.size());
        }

        WGPURenderPipeline pipe = GetOrCreatePipelineDualTexture3D(command.hasVertexColor ? 24 : 20,
                                                                   command.topology,
                                                                   RequiredStripIndexFormat(command),
                                                                   command.depthTest,
                                                                   command.depthWrite, command.depthFunc,
                                                                   command.blend, command.blendParams,
                                                                   command.cullMode, command.wireframe,
                                                                   command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU DualTexture3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    void WebGPURenderer::QueueDualTextureDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24)
            throw std::invalid_argument("CNA WebGPU: QueueDualTextureDraw requires a stride-20 "
                                        "or stride-24 vertex buffer");
        if (params.texture0 == nullptr || params.texture1 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueDualTextureDraw requires both texture0 "
                                        "and texture1 to be bound");

        DualTextureDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        command.texture0 = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.texture1 = ResolveSamplable(params.texture1);
        // REMED-GFX-172: and the SECOND layer's own slot, captured here for the same reason. Both
        // descriptions travel with the command, so replay never reads live sampler state.
        command.texture1Filter = slotSamplers_[1].filter;
        command.texture1AddressU = slotSamplers_[1].addressU;
        command.texture1AddressV = slotSamplers_[1].addressV;
        command.texture1MaxAnisotropy = slotSamplers_[1].maxAnisotropy;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        dualTextureDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::DualTexture, dualTextureDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::QueueTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24)
            throw std::invalid_argument("CNA WebGPU: QueueTexturedDraw requires a stride-20 "
                                        "(VertexPositionTexture) or stride-24 "
                                        "(VertexPositionColorTexture) vertex buffer");
        // plans/plan_gltf.md GLTF-474: an absent base-colour map is no longer refused -- the command
        // takes the neutral-white default below, which is the identity for `tex * colour`.

        TexturedDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474: neutral white when the effect binds no texture -- `tex * colour`
        // then collapses to the colour, which is what an untextured stock-effect draw should be.
        EnsurePbrDefaultTextures();
        command.texture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        texturedDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Textured, texturedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::DestroyPbrResources()
    {
        for (auto& [key, pipe] : pbrPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        pbrPipelines_.clear();
        // plans/plan_gltf.md GLTF-465: the stride-60 cache and module are released the same way -- a
        // pipeline cache nobody frees is exactly the leak this function exists to prevent.
        for (auto& [key, pipe] : pbrColorPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        pbrColorPipelines_.clear();
        if (pbrColorShader_ != nullptr) wgpuShaderModuleRelease(pbrColorShader_);
        pbrColorShader_ = nullptr;
        if (pbrPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(pbrPipelineLayout_);
        if (pbrBindGroupLayout1_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout1_);
        if (pbrBindGroupLayout0_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout0_);
        if (pbrShader_ != nullptr) wgpuShaderModuleRelease(pbrShader_);
        pbrPipelineLayout_ = nullptr;
        pbrBindGroupLayout1_ = nullptr;
        pbrBindGroupLayout0_ = nullptr;
        pbrShader_ = nullptr;
    }

namespace
{
    /// plans/plan_gltf.md GLTF-465: expands the PBR WGSL's colour markers for one pipeline variant.
    ///
    /// WGSL rejects a shader input with no matching vertex attribute, so the stride-48/68 records
    /// and their stride-60/80 twins need different modules -- and a second copy of a 600-line
    /// shader is a second thing to keep in step. Expanding one source is the same technique the
    /// Diligent renderer uses for the same reason.
    ///
    /// @param source The marked WGSL.
    /// @param colored Whether this variant's vertex layout supplies COLOR_0.
    /// @param attributeLocation Free vertex-input location for the colour (4 rigid, 6 skinned).
    /// @return The expanded WGSL, with every marker consumed.
    std::string ExpandPbrVertexColourWgslEXT(std::string source, bool colored,
                                             int attributeLocation)
    {
        const auto Replace = [&source](const std::string& marker, const std::string& text) {
            for (std::size_t at = source.find(marker); at != std::string::npos;
                 at = source.find(marker, at + text.size()))
                source.replace(at, marker.size(), text);
        };
        Replace("/*CNA_PBR_COLOR_ATTRIBUTE*/",
                colored ? "@location(" + std::to_string(attributeLocation) + ") color: vec4f," : "");
        Replace("/*CNA_PBR_COLOR_VARYING*/", colored ? "@location(5) color: vec4f," : "");
        Replace("/*CNA_PBR_COLOR_ASSIGN*/", colored ? "    output.color = input.color;" : "");
        // u.light0DiffuseVertexColor.w is the effect's own VertexColorEnabledEXT, already uploaded
        // by FillExtUniforms -- so a caller can opt back into the opaque-white identity deliberately.
        Replace("/*CNA_PBR_COLOR_VALUE*/vec4f(1.0)/**/",
                colored ? "select(vec4f(1.0), input.color, u.light0DiffuseVertexColor.w > 0.5)"
                        : "vec4f(1.0)");
        if (source.find("/*CNA_PBR_COLOR") != std::string::npos)
            throw std::runtime_error("CNA WebGPU: unexpanded PBR colour shader marker");
        return source;
    }
}

    void WebGPURenderer::CreatePbrResources()
    {
        DestroyPbrResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Ported from EasyGLRenderer::EnsurePbrProgram()'s GLSL PbrLight() helper
        // unchanged: GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting
        // k=(roughness+1)^2/8), and Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF
        // (Appendix B.3.3/B.3.4/B.3.2). The TBN basis is built per-pixel from the vertex tangent
        // (Gram-Schmidt re-orthogonalized against the interpolated world normal), with the
        // bitangent sign from tangent.w (glTF convention) -- identical to the EasyGL fragment
        // shader's own construction. Group 0's Uniforms/LitLightParams struct shapes match
        // lit_textured3d.wgsl's own field-for-field (populated by the same
        // FillExtUniforms()/FillLitLightUniforms() helpers); PbrFactors is the one genuinely new
        // (small) buffer this shader needs.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct PbrFactors {
    metallicRoughness: vec4f,
    alphaTest: vec4f,
    // plans/plan_gltf.md GLTF-344: w decodes the specular COLOUR sample from sRGB.
    srgbFlags: vec4f,
    dielectricFresnel: vec4f,
    textureTransformRows: array<vec4f, 10>,
    // KHR_materials_specular: xyz = UNCLAMPED dielectric F0, w = specularFactor. Unclamped because
    // specularColorTexture multiplies before the min(...,1) below.
    specularFresnelInputs: vec4f,
    specularTextureTransformRows: array<vec4f, 4>,
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;
// plans/plan_gltf.md GLTF-344: KHR_materials_specular's own two inputs, at the same slots every other
// sampling renderer uses -- strength in the scalar map's ALPHA, colour in the colour map's RGB.
@group(1) @binding(6) var specularTex: texture_2d<f32>;
@group(1) @binding(7) var specularColorTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    /*CNA_PBR_COLOR_ATTRIBUTE*/
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
    /*CNA_PBR_COLOR_VARYING*/
};
fn directionHandedness(m: mat3x3f) -> f32 {
    return select(1.0, -1.0, dot(m[0], cross(m[1], m[2])) < 0.0);
}
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    // Tangent transforms as a plain direction under mat3(world) (uniform-scale assumption),
    // matching EnsurePbrProgram()'s own documented simplification.
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    output.worldTangent = worldMat3 * input.tangent.xyz;
    output.bitangentSign = input.tangent.w * directionHandedness(worldMat3);
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    /*CNA_PBR_COLOR_ASSIGN*/
    return output;
}

fn srgbToLinear(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + vec3f(0.055)) / 1.055, vec3f(2.4));
    return select(lo, hi, c >= vec3f(0.04045));
}

fn linearToSrgb(c: vec3f) -> vec3f {
    let lo = c * 12.92;
    let hi = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - vec3f(0.055);
    return select(lo, hi, c >= vec3f(0.0031308));
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, f90: vec3f, roughness: f32, metallic: f32) -> vec3f {
    let h = normalize(v + l);
    let ndotl = max(dot(n, l), 0.0);
    let ndotv = max(dot(n, v), 1e-4);
    let ndoth = max(dot(n, h), 0.0);
    let vdoth = max(dot(v, h), 0.0);
    let a2 = pow(roughness, 4.0);
    let dTerm = ndoth * ndoth * (a2 - 1.0) + 1.0;
    let d = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    var k = roughness + 1.0;
    k = k * k / 8.0;
    let g = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));
    let f = f0 + (f90 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

fn pbrSpecularTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.specularTextureTransformRows[slot * 2u].xyz),
                 dot(value, pf.specularTextureTransformRows[slot * 2u + 1u].xyz));
}

fn pbrTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.textureTransformRows[slot * 2u].xyz),
                 dot(value, pf.textureTransformRows[slot * 2u + 1u].xyz));
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u));
    let baseColor = select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5);
    // plans/plan_gltf.md GLTF-465: glTF 3.9.2 makes COLOR_0 an additional LINEAR multiplier on the whole
    // base-colour product, alpha included -- the alpha half is where a BLEND-mode vertex-coloured
    // primitive's transparency comes from. Expanded to the opaque-white identity in the variants
    // whose vertex layout supplies no colour, so one fragment body serves both.
    let cnaVertexColor = /*CNA_PBR_COLOR_VALUE*/vec4f(1.0)/**/;
    let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;
    let useTolerance = pf.alphaTest.y > 0.0;
    let lessTest = alpha < pf.alphaTest.x;
    let toleranceTest = abs(alpha - pf.alphaTest.x) < pf.alphaTest.y;
    let passesAlphaTest = select(lessTest, toleranceTest, useTolerance);
    let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest);
    if (alphaWeight < 0.0) {
        discard;
    }

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    var sampledNormal = textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0;
    sampledNormal.x *= pf.metallicRoughness.z;
    sampledNormal.y *= pf.metallicRoughness.z;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u));
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    // plans/plan_gltf.md GLTF-344: KHR_materials_specular. strength comes from the scalar map's ALPHA and
    // colour from the colour map's sRGB-decoded RGB, each through its own affine transform; the
    // dielectric F0 is min(unclampedF0 * colourSample, 1) * strength, which is the extension's own
    // ordering and the reason the unclamped value is uploaded. A material without either map samples
    // the white identity, so the product collapses to the factor alone.
    let specularStrength = pf.specularFresnelInputs.w
        * textureSample(specularTex, texSampler, pbrSpecularTransformUv(input.uv, 0u)).a;
    let specularColorSample = textureSample(specularColorTex, texSampler,
                                            pbrSpecularTransformUv(input.uv, 1u)).rgb;
    let specularColorLinear = select(specularColorSample, srgbToLinear(specularColorSample),
                                     pf.srgbFlags.w > 0.5);
    let dielectricF0 = min(pf.specularFresnelInputs.xyz * specularColorLinear, vec3f(1.0))
        * specularStrength;
    let f0 = mix(dielectricF0, albedo, metallic);
    let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic);

    // Same disabled-light NaN guard as lit_textured3d.wgsl: a disabled DirectionalLight forwards
    // Direction=(0,0,0) (only DiffuseColor is zeroed), and normalize() on a true zero vector is
    // undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, f90, roughness, metallic);

    let occlusionSample = textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r;
    let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissiveSample = textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u)).rgb;
    let emissiveLinear = select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5);
    let emissive = lp.emissiveColor.xyz * emissiveLinear;

    let linearRgb = ambient + lo + emissive;
    let outputRgb = select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5);
    return vec4f(outputRgb, alpha);
}
)WGSL";

        // plans/plan_gltf.md GLTF-465: two modules from one marked source -- the stride-48 record has no
        // colour element, and WGSL rejects a vertex input with no matching attribute.
        const std::string bareWgsl = ExpandPbrVertexColourWgslEXT(shaderSource, false, 4);
        const std::string colorWgsl = ExpandPbrVertexColourWgslEXT(shaderSource, true, 4);

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(bareWgsl.c_str());
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Pbr3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        pbrShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (pbrShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D shader");

        WGPUShaderSourceWGSL colorWgslChain{};
        colorWgslChain.chain.sType = WGPUSType_ShaderSourceWGSL;
        colorWgslChain.code = StringView(colorWgsl.c_str());
        WGPUShaderModuleDescriptor colorShaderDescriptor{};
        colorShaderDescriptor.label = StringView("CNA WebGPU Pbr3D VertexColor WGSL");
        colorShaderDescriptor.nextInChain = &colorWgslChain.chain;
        pbrColorShader_ = wgpuDeviceCreateShaderModule(device_, &colorShaderDescriptor);
        if (pbrColorShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D vertex-colour shader");

        std::array<WGPUBindGroupLayoutEntry, 3> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[0].buffer.minBindingSize = 128;
        uboEntries[1].binding = 1;
        uboEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[1].buffer.minBindingSize = 272;
        uboEntries[2].binding = 2;
        uboEntries[2].visibility = WGPUShaderStage_Fragment;
        uboEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
        // Four material vec4s followed by ten affine texture-transform rows.
        uboEntries[2].buffer.minBindingSize = 76 * sizeof(float);
        WGPUBindGroupLayoutDescriptor uboLayoutDescriptor{};
        uboLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D BindGroupLayout0");
        uboLayoutDescriptor.entryCount = uboEntries.size();
        uboLayoutDescriptor.entries = uboEntries.data();
        pbrBindGroupLayout0_ = wgpuDeviceCreateBindGroupLayout(device_, &uboLayoutDescriptor);

        // plans/plan_gltf.md GLTF-344: eight entries -- the sampler, the five core PBR maps, and
        // KHR_materials_specular's strength and colour maps at 6 and 7.
        std::array<WGPUBindGroupLayoutEntry, 8> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].visibility = WGPUShaderStage_Fragment;
        texEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        for (std::uint32_t i = 1; i <= 7; ++i)
        {
            texEntries[i].binding = i;
            texEntries[i].visibility = WGPUShaderStage_Fragment;
            texEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
            texEntries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            texEntries[i].texture.multisampled = false;
        }
        WGPUBindGroupLayoutDescriptor texLayoutDescriptor{};
        texLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D BindGroupLayout1");
        texLayoutDescriptor.entryCount = texEntries.size();
        texLayoutDescriptor.entries = texEntries.data();
        pbrBindGroupLayout1_ = wgpuDeviceCreateBindGroupLayout(device_, &texLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{pbrBindGroupLayout0_, pbrBindGroupLayout1_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        pbrPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (pbrBindGroupLayout0_ == nullptr || pbrBindGroupLayout1_ == nullptr || pbrPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelinePbr3D(bool colored,
                                                                WGPUPrimitiveTopology topology,
                                                                         WGPUIndexFormat stripIndexFormat,
                                                                         bool depthTest, bool depthWrite,
                                                                         int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        auto& cache = colored ? pbrColorPipelines_ : pbrPipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        // Matches VertexPositionNormalTangentTexture's 48-byte layout: Position(12) + Normal(12)
        // + Tangent(16, xyz + bitangent-handedness in w) + TextureCoordinate(8).
        // plans/plan_gltf.md GLTF-465: stride 60 is that record with TEXCOORD_1 at 48 and a packed,
        // normalized COLOR_0 at 56. The second UV set stays unbound -- this renderer's PBR shader
        // samples one set, which is a separate capability gap and unchanged here.
        struct PbrVertex { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; };
        static_assert(sizeof(PbrVertex) == 48, "PbrVertex must be 48 bytes");
        std::array<WGPUVertexAttribute, 5> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = offsetof(PbrVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = offsetof(PbrVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        attributes[2].offset = offsetof(PbrVertex, tx);
        attributes[2].shaderLocation = 2;
        attributes[3].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[3].offset = offsetof(PbrVertex, u);
        attributes[3].shaderLocation = 3;
        if (colored)
        {
            attributes[4].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            attributes[4].offset = 56;
            attributes[4].shaderLocation = 4;
        }
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = colored ? 60u : sizeof(PbrVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = colored ? 5u : 4u;
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = colored ? pbrColorShader_ : pbrShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Pbr3D Pipeline");
        pipeline.layout = pbrPipelineLayout_;
        pipeline.vertex.module = colored ? pbrColorShader_ : pbrShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPURenderer::EnsurePbrDefaultTextures()
    {
        // Mirrors EasyGLRenderer::EnsureDefaultWhiteTexture()/
        // EnsureDefaultFlatNormalTexture(): a 1x1 flat tangent-space normal (0,0,1), encoded as
        // RGB (128,128,255), so the sampled/decoded (rgb*2-1) normal is exactly the geometric
        // normal (no perturbation) when PbrEffect::NormalMap is unbound. The other 3 PBR map
        // fallbacks (metallic-roughness, emissive, occlusion) all reuse a shared 1x1 white texture
        // -- their respective factor/no-op semantics already make (1,1,1,1) the correct "map
        // absent" value (factor*1.0=factor; emissive tint*1.0=tint; occlusion 1.0=unoccluded).
        if (pbrDefaultWhiteTexture_ == nullptr)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            pbrDefaultWhiteTexture_ = std::make_unique<WebGPUTextureRenderer>(*this, white);
        }
        if (pbrDefaultFlatNormalTexture_ == nullptr)
        {
            ImageData flatNormal{};
            flatNormal.width = 1;
            flatNormal.height = 1;
            flatNormal.mipLevels = 1;
            flatNormal.pixels = {128, 128, 255, 255};
            pbrDefaultFlatNormalTexture_ = std::make_unique<WebGPUTextureRenderer>(*this, flatNormal);
        }
    }

    void WebGPURenderer::QueuePbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount,
                                              const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        // plans/plan_gltf.md GLTF-465: stride 60 is the same record with TEXCOORD_1 and a packed COLOR_0
        // appended, and it selects the colour-carrying pipeline and shader module.
        const std::size_t pbrStride = webgpuVb.Stride();
        if (pbrStride != 48 && pbrStride != 60)
            throw std::invalid_argument("CNA WebGPU: QueuePbrDraw requires a stride-48 or stride-60 "
                                        "(VertexPositionNormalTangentTexture, optionally with "
                                        "COLOR_0) vertex buffer");

        EnsurePbrDefaultTextures();

        PbrDrawCommand command;
        command.colored = (pbrStride == 60);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * pbrStride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474/GLTF-465: neutral white when the material carries no base-colour
        // map -- glTF's own default material (3.9.2) has none, and `tex * colour` then collapses
        // to the colour, which is what every other renderer's PBR base-colour bind already does.
        command.baseColorTexture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.normalMap = params.pbrNormalMap != nullptr
            ? ResolveSamplable(params.pbrNormalMap)
            : pbrDefaultFlatNormalTexture_->Sampled();
        command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr
            ? ResolveSamplable(params.pbrMetallicRoughnessMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.emissiveMap = params.pbrEmissiveMap != nullptr
            ? ResolveSamplable(params.pbrEmissiveMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.occlusionMap = params.pbrOcclusionMap != nullptr
            ? ResolveSamplable(params.pbrOcclusionMap)
            : pbrDefaultWhiteTexture_->Sampled();
        // plans/plan_gltf.md GLTF-344: white is the identity for both -- alpha 1 leaves the specular
        // factor alone, RGB 1 leaves the unclamped F0 alone -- so a material with neither map
        // renders exactly as the factor-only path already did.
        command.specularMap = params.pbrSpecularMap != nullptr
            ? ResolveSamplable(params.pbrSpecularMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.specularColorMap = params.pbrSpecularColorMap != nullptr
            ? ResolveSamplable(params.pbrSpecularColorMap)
            : pbrDefaultWhiteTexture_->Sampled();

        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillPbrFactors(command.pbrFactors, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        pbrDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Pbr, pbrDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssuePbrDraw(WGPURenderPassEncoder pass,
                                             const PbrDrawCommand& command,
                                             ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.baseColorTexture ||
            !command.normalMap || !command.metallicRoughnessMap ||
            !command.emissiveMap || !command.occlusionMap)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU Pbr3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU Pbr3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBufferDescriptor lightUboDescriptor{};
        lightUboDescriptor.label = StringView("CNA WebGPU Pbr3D LightUBO");
        lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        lightUboDescriptor.size = sizeof(command.lightUniforms);
        WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
        wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

        WGPUBufferDescriptor factorsUboDescriptor{};
        factorsUboDescriptor.label = StringView("CNA WebGPU Pbr3D FactorsUBO");
        factorsUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        factorsUboDescriptor.size = sizeof(command.pbrFactors);
        WGPUBuffer factorsUniformBuffer = wgpuDeviceCreateBuffer(device_, &factorsUboDescriptor);
        wgpuQueueWriteBuffer(queue_, factorsUniformBuffer, 0, command.pbrFactors.data(), sizeof(command.pbrFactors));

        std::array<WGPUBindGroupEntry, 3> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].buffer = uniformBuffer;
        uboEntries[0].size = sizeof(command.uniforms);
        uboEntries[1].binding = 1;
        uboEntries[1].buffer = lightUniformBuffer;
        uboEntries[1].size = sizeof(command.lightUniforms);
        uboEntries[2].binding = 2;
        uboEntries[2].buffer = factorsUniformBuffer;
        uboEntries[2].size = sizeof(command.pbrFactors);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU Pbr3D UBO BindGroup");
        uboBindDescriptor.layout = pbrBindGroupLayout0_;
        uboBindDescriptor.entryCount = uboEntries.size();
        uboBindDescriptor.entries = uboEntries.data();
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "Pbr3D");
        // plans/plan_gltf.md GLTF-344: entries 6 and 7 are KHR_materials_specular's own maps, which
        // resolve to the white identity when the material declares neither.
        std::array<WGPUBindGroupEntry, 8> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.baseColorTexture.View();
        texEntries[2].binding = 2;
        texEntries[2].textureView = command.normalMap.View();
        texEntries[3].binding = 3;
        texEntries[3].textureView = command.metallicRoughnessMap.View();
        texEntries[4].binding = 4;
        texEntries[4].textureView = command.emissiveMap.View();
        texEntries[5].binding = 5;
        texEntries[5].textureView = command.occlusionMap.View();
        texEntries[6].binding = 6;
        texEntries[6].textureView = command.specularMap.View();
        texEntries[7].binding = 7;
        texEntries[7].textureView = command.specularColorMap.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU Pbr3D Texture BindGroup");
        texBindDescriptor.layout = pbrBindGroupLayout1_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelinePbr3D(command.colored,
                                                           command.topology,
                                                           RequiredStripIndexFormat(command),
                                                           command.depthTest,
                                                           command.depthWrite, command.depthFunc,
                                                           command.blend, command.blendParams,
                                                           command.cullMode, command.wireframe,
                                                           command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU Pbr3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(lightUniformBuffer);
        pendingBufferReleases_.push_back(factorsUniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    // ------------------------------------------------------------------------------------------
    // plans/plan_cnj.md Phase 14J: SkinnedEffect (skinned3d.wgsl family) -- closes this renderer's
    // pre-existing "no skinning shader at all" gap. See CreateSkinnedResources()'s own doc
    // comment for the full formula-fidelity rationale (ported line-for-line from
    // EasyGLRenderer::EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()).
    // ------------------------------------------------------------------------------------------

    void WebGPURenderer::DestroySkinnedResources()
    {
        for (auto* cache : { &skinnedPipelines_, &skinnedColorPipelines_,
                             &skinnedVertexLitPipelines_, &skinnedVertexLitColorPipelines_ })
        {
            for (auto& [key, pipe] : *cache)
            {
                if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
            }
            cache->clear();
        }
        if (skinnedPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(skinnedPipelineLayout_);
        if (skinnedBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(skinnedBindGroupLayout_);
        if (skinnedShader_ != nullptr) wgpuShaderModuleRelease(skinnedShader_);
        if (skinnedColorShader_ != nullptr) wgpuShaderModuleRelease(skinnedColorShader_);
        if (skinnedVertexLitShader_ != nullptr) wgpuShaderModuleRelease(skinnedVertexLitShader_);
        if (skinnedVertexLitColorShader_ != nullptr) wgpuShaderModuleRelease(skinnedVertexLitColorShader_);
        skinnedPipelineLayout_ = nullptr;
        skinnedBindGroupLayout_ = nullptr;
        skinnedShader_ = nullptr;
        skinnedColorShader_ = nullptr;
        skinnedVertexLitShader_ = nullptr;
        skinnedVertexLitColorShader_ = nullptr;
    }

    void WebGPURenderer::CreateSkinnedResources()
    {
        DestroySkinnedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from EasyGLRenderer::EnsureSkinnedProgram()'s GLSL shader line-for-line:
        // bone-palette skinning (Task 895's weightsPerVertex 1/2/4 convention -- only the first N
        // weight/index pairs are summed), then Blinn-Phong lighting identical in shape to
        // lit_textured3d.wgsl's own per-pixel-lit shader. SkinnedEffect has no separate
        // AmbientLightColor uniform because SkinnedEffect::FillGpuDrawParams() already pre-folds
        // (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha into emissiveColor. FNA's shader
        // therefore adds that prepared term after `lightSum*diffuseColor`; it must not multiply
        // emissiveColor by DiffuseColor or Alpha again. No fog/alpha-test
        // (SkinnedEffect never sets GpuDrawParams::alphaTest away from
        // its always-pass default; fog is deferred uniformly across every WebGPU 3D shader so far).
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

fn skinNormal(m: mat3x3f, n: vec3f) -> vec3f {
    let c0 = m[0];
    let c1 = m[1];
    let c2 = m[2];
    let co0 = cross(c1, c2);
    let co1 = cross(c2, c0);
    let co2 = cross(c0, c1);
    let det = dot(c0, co0);
    let transformed = mat3x3f(co0, co1, co2) * n;
    if (abs(det) > 1e-6) {
        return transformed * select(-1.0, 1.0, det >= 0.0);
    }
    return m * n;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). The world factor was missing entirely (audit
    // Variant A), so any rotated or non-uniformly-scaled skinned model was lit as if World were
    // identity. The fragment stage re-normalizes worldNormal.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * skinNormal(skinMat3, input.normal));
    output.worldPos = (lp.world * skinnedPos).xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    // Same disabled-light NaN guard as lit_textured3d.wgsl/pbr3d.wgsl: a disabled DirectionalLight
    // forwards Direction=(0,0,0) (only DiffuseColor/SpecularColor are zeroed), and normalize() on a
    // true zero vector is undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    let litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let texColor = textureSample(tex, texSampler, input.uv);
    var color = vec4f(litRGB * texColor.rgb, u.diffuseColor.a * texColor.a);
    color = vec4f(color.rgb + specularRGB * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Skinned3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        skinnedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        // Stride-56 sibling: adds the CNB-67 trailing per-vertex Color (normalized ubyte4, offset
        // 52), gated by uVertexColorEnabled (u.light0DiffuseVertexColor.w). A WebGPU pipeline must
        // supply every vertex-shader-referenced attribute location from its own vertex buffer
        // layout (unlike EasyGL's own "simply leave attribute 5 unbound" precedent for stride 52),
        // so this is a genuinely separate shader module rather than a conditionally-bound
        // attribute, mirroring this renderer's own existing texturedShader_/coloredTexturedShader_
        // precedent for the analogous stride-20/24 split. The vertex-colour gate multiplies the
        // FINAL combined diffuse+specular output, applied AFTER the specular add -- matches the
        // EasyGL reference exactly (a real ordering bug was once found and fixed there: applying
        // the gate to the diffuse term alone lets an unmodulated specular highlight leak through).
        static constexpr char colorShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
    @location(5) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
    @location(3) color: vec4f,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). The world factor was missing entirely (audit
    // Variant A), so any rotated or non-uniformly-scaled skinned model was lit as if World were
    // identity. The fragment stage re-normalizes worldNormal.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * (skinMat3 * input.normal));
    output.worldPos = (lp.world * skinnedPos).xyz;
    output.color = input.color;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    let litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let texColor = textureSample(tex, texSampler, input.uv);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    let vc = select(vec4f(1.0, 1.0, 1.0, 1.0), input.color, vertexColorEnabled > 0.5);
    var color = vec4f(litRGB * texColor.rgb, u.diffuseColor.a * texColor.a * vc.a);
    color = vec4f(color.rgb + specularRGB * color.a, color.a);
    color = vec4f(color.rgb * vc.rgb, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL colorWgsl{};
        colorWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        colorWgsl.code = StringView(colorShaderSource);
        WGPUShaderModuleDescriptor colorShaderDescriptor{};
        colorShaderDescriptor.label = StringView("CNA WebGPU Skinned3D VertexColor WGSL");
        colorShaderDescriptor.nextInChain = &colorWgsl.chain;
        skinnedColorShader_ = wgpuDeviceCreateShaderModule(device_, &colorShaderDescriptor);

        // Real per-vertex-lit sibling of both shaders above -- Task 1102b's identical technique
        // (move the Blinn-Phong math from fs_main into vs_main, Gouraud-interpolate via varyings),
        // applied to EnsureSkinnedVertexLitProgram()'s GLSL shader, selected when
        // params.skinned && params.lightingEnabled && !params.preferPerPixelLighting (XNA's own
        // SkinnedEffect.PreferPerPixelLighting==false default, matching every other renderer).
        static constexpr char vertexLitShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). This per-vertex-lit sibling had the identical
    // missing-world-factor defect (audit Variant A); lighting is evaluated in this stage, so the
    // world-transformed normal is re-normalized here.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let n = normalize(normalMatrix * (skinMat3 * input.normal));
    let worldPos = (lp.world * skinnedPos).xyz;
    let e = normalize(lp.eyePos.xyz - worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(tex, texSampler, input.uv);
    var color = vec4f(input.litRGB * texColor.rgb, u.diffuseColor.a * texColor.a);
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL vertexLitWgsl{};
        vertexLitWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        vertexLitWgsl.code = StringView(vertexLitShaderSource);
        WGPUShaderModuleDescriptor vertexLitShaderDescriptor{};
        vertexLitShaderDescriptor.label = StringView("CNA WebGPU Skinned3D VertexLit WGSL");
        vertexLitShaderDescriptor.nextInChain = &vertexLitWgsl.chain;
        skinnedVertexLitShader_ = wgpuDeviceCreateShaderModule(device_, &vertexLitShaderDescriptor);

        // Vertex-lit + vertex-colour combo (stride 56).
        static constexpr char vertexLitColorShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
    @location(5) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
    @location(3) color: vec4f,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    output.color = input.color;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). This per-vertex-lit sibling had the identical
    // missing-world-factor defect (audit Variant A); lighting is evaluated in this stage, so the
    // world-transformed normal is re-normalized here.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let n = normalize(normalMatrix * (skinMat3 * input.normal));
    let worldPos = (lp.world * skinnedPos).xyz;
    let e = normalize(lp.eyePos.xyz - worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(tex, texSampler, input.uv);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    let vc = select(vec4f(1.0, 1.0, 1.0, 1.0), input.color, vertexColorEnabled > 0.5);
    var color = vec4f(input.litRGB * texColor.rgb, u.diffuseColor.a * texColor.a * vc.a);
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    color = vec4f(color.rgb * vc.rgb, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL vertexLitColorWgsl{};
        vertexLitColorWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        vertexLitColorWgsl.code = StringView(vertexLitColorShaderSource);
        WGPUShaderModuleDescriptor vertexLitColorShaderDescriptor{};
        vertexLitColorShaderDescriptor.label = StringView("CNA WebGPU Skinned3D VertexLit VertexColor WGSL");
        vertexLitColorShaderDescriptor.nextInChain = &vertexLitColorWgsl.chain;
        skinnedVertexLitColorShader_ = wgpuDeviceCreateShaderModule(device_, &vertexLitColorShaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 3> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[0].buffer.minBindingSize = 128;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[1].buffer.minBindingSize = 272;
        layoutEntries[2].binding = 2;
        layoutEntries[2].visibility = WGPUShaderStage_Vertex;
        layoutEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[2].buffer.minBindingSize = (4 + 72 * 16) * sizeof(float);
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU Skinned3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        skinnedBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{skinnedBindGroupLayout_, texturedBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Skinned3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        skinnedPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (skinnedShader_ == nullptr || skinnedColorShader_ == nullptr ||
            skinnedVertexLitShader_ == nullptr || skinnedVertexLitColorShader_ == nullptr ||
            skinnedBindGroupLayout_ == nullptr || skinnedPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Skinned3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinned3D(std::size_t stride, bool preferVertexLit,
                                                                             WGPUPrimitiveTopology topology,
                                                                             WGPUIndexFormat stripIndexFormat,
                                                                             bool depthTest, bool depthWrite,
                                                                             int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const bool hasVertexColor = (stride == 56);
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        auto& cache = preferVertexLit
            ? (hasVertexColor ? skinnedVertexLitColorPipelines_ : skinnedVertexLitPipelines_)
            : (hasVertexColor ? skinnedColorPipelines_ : skinnedPipelines_);
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        WGPUShaderModule shaderModule = preferVertexLit
            ? (hasVertexColor ? skinnedVertexLitColorShader_ : skinnedVertexLitShader_)
            : (hasVertexColor ? skinnedColorShader_ : skinnedShader_);

        // Matches ApplyLayout's stride==52/56 cases (VertexPositionNormalTextureSkinned, with an
        // optional trailing Color appended at offset 52 for stride 56 -- CNB-67's own "append
        // rather than insert" convention keeps locations 0-4 byte-identical between the two
        // strides). BlendIndices (Byte4/Uint8x4) is read as a true unsigned integer, not
        // normalized -- matches EasyGLRenderer::ApplyLayout's glVertexAttribIPointer path.
        std::array<WGPUVertexAttribute, 6> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = 12;
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[2].offset = 24;
        attributes[2].shaderLocation = 2;
        attributes[3].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        attributes[3].offset = 32;
        attributes[3].shaderLocation = 3;
        attributes[4].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Byte4);
        attributes[4].offset = 48;
        attributes[4].shaderLocation = 4;
        std::uint32_t attributeCount = 5;
        std::uint64_t arrayStride = 52;
        if (hasVertexColor)
        {
            attributes[5].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            attributes[5].offset = 52;
            attributes[5].shaderLocation = 5;
            attributeCount = 6;
            arrayStride = 56;
        }

        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = arrayStride;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributeCount;
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Skinned3D Pipeline");
        pipeline.layout = skinnedPipelineLayout_;
        pipeline.vertex.module = shaderModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Skinned3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPURenderer::QueueSkinnedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 52 && stride != 56)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedDraw requires a stride-52 "
                                        "(VertexPositionNormalTextureSkinned) or stride-56 "
                                        "(with vertex colour) vertex buffer");
        // plans/plan_gltf.md GLTF-474: an absent base-colour map is no longer refused -- the command
        // takes the neutral-white default below, which is the identity for `tex * colour`.

        SkinnedDrawCommand command;
        command.stride = stride;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474: neutral white when the effect binds no texture -- `tex * colour`
        // then collapses to the colour, which is what an untextured stock-effect draw should be.
        EnsurePbrDefaultTextures();
        command.texture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        // Real XNA's SkinnedEffect.PreferPerPixelLighting default is false (per-vertex), matching
        // every other renderer's own dispatch condition for this flag (Task 1102b).
        command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillSkinningParams(command.skinningParams, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        skinnedDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::Skinned, skinnedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssueSkinnedDraw(WGPURenderPassEncoder pass,
                                                 const SkinnedDrawCommand& command,
                                                 ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.texture)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU Skinned3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU Skinned3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBufferDescriptor lightUboDescriptor{};
        lightUboDescriptor.label = StringView("CNA WebGPU Skinned3D LightUBO");
        lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        lightUboDescriptor.size = sizeof(command.lightUniforms);
        WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
        wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

        WGPUBufferDescriptor skinningUboDescriptor{};
        skinningUboDescriptor.label = StringView("CNA WebGPU Skinned3D SkinningUBO");
        skinningUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        skinningUboDescriptor.size = sizeof(command.skinningParams);
        WGPUBuffer skinningUniformBuffer = wgpuDeviceCreateBuffer(device_, &skinningUboDescriptor);
        wgpuQueueWriteBuffer(queue_, skinningUniformBuffer, 0, command.skinningParams.data(), sizeof(command.skinningParams));

        std::array<WGPUBindGroupEntry, 3> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].buffer = uniformBuffer;
        uboEntries[0].size = sizeof(command.uniforms);
        uboEntries[1].binding = 1;
        uboEntries[1].buffer = lightUniformBuffer;
        uboEntries[1].size = sizeof(command.lightUniforms);
        uboEntries[2].binding = 2;
        uboEntries[2].buffer = skinningUniformBuffer;
        uboEntries[2].size = sizeof(command.skinningParams);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU Skinned3D UBO BindGroup");
        uboBindDescriptor.layout = skinnedBindGroupLayout_;
        uboBindDescriptor.entryCount = uboEntries.size();
        uboBindDescriptor.entries = uboEntries.data();
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "Skinned3D");
        std::array<WGPUBindGroupEntry, 2> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.texture.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU Skinned3D Texture BindGroup");
        texBindDescriptor.layout = texturedBindGroupLayout_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelineSkinned3D(command.stride, command.preferVertexLit,
                                                                command.topology,
                                                                RequiredStripIndexFormat(command),
                                                                command.depthTest,
                                                                command.depthWrite, command.depthFunc,
                                                                command.blend, command.blendParams,
                                                                command.cullMode, command.wireframe,
                                                                command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU Skinned3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(lightUniformBuffer);
        pendingBufferReleases_.push_back(skinningUniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }

    // ------------------------------------------------------------------------------------------
    // SkinnedPbrEffect (skinned_pbr3d.wgsl) -- the PBR + skinning combo, stride 68
    // (VertexPositionNormalTangentTextureSkinned). Bone-palette skinning identical to
    // skinned3d.wgsl above (extended to also skin Tangent), feeding pbr3d.wgsl's own fragment BRDF
    // unchanged, matching EasyGLRenderer::EnsurePbrSkinnedProgram()'s combination exactly.
    // ------------------------------------------------------------------------------------------

    void WebGPURenderer::DestroySkinnedPbrResources()
    {
        for (auto& [key, pipe] : skinnedPbrPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        skinnedPbrPipelines_.clear();
        // plans/plan_gltf.md GLTF-463/GLTF-465: the stride-80 cache and module.
        for (auto& [key, pipe] : skinnedPbrColorPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        skinnedPbrColorPipelines_.clear();
        if (skinnedPbrColorShader_ != nullptr) wgpuShaderModuleRelease(skinnedPbrColorShader_);
        skinnedPbrColorShader_ = nullptr;
        if (skinnedPbrPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(skinnedPbrPipelineLayout_);
        if (skinnedPbrBindGroupLayout0_ != nullptr) wgpuBindGroupLayoutRelease(skinnedPbrBindGroupLayout0_);
        if (skinnedPbrShader_ != nullptr) wgpuShaderModuleRelease(skinnedPbrShader_);
        skinnedPbrPipelineLayout_ = nullptr;
        skinnedPbrBindGroupLayout0_ = nullptr;
        skinnedPbrShader_ = nullptr;
    }

    void WebGPURenderer::CreateSkinnedPbrResources()
    {
        DestroySkinnedPbrResources();
        // Reuses pbrBindGroupLayout1_ (group 1: sampler + 5 textures) unchanged, so this must run
        // after CreatePbrResources() -- enforced by ConfigureSurface()'s own call ordering.
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || pbrBindGroupLayout1_ == nullptr)
            return;

        // Vertex stage: skinned3d.wgsl's bone-palette transform (skinMatrix()), extended to also
        // skin Tangent, feeding pbr3d.wgsl's own pbrLight()/fs_main BRDF unchanged. REMED-GFX-006:
        // the normal now uses the inverse-transpose world normal matrix (lp.normalMatrixCol*), the
        // same one pbr3d.wgsl's own unskinned vertex shader uses -- the previous raw-World
        // (mat3(uWorld)*(skinMat3*normal)) path was audit Variant B, correct only for rotation and
        // uniform scale. The tangent stays on raw World (direction transform, glTF convention). No
        // vertex colour (SkinnedPbrEffect has none).
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct PbrFactors {
    metallicRoughness: vec4f,
    alphaTest: vec4f,
    // plans/plan_gltf.md GLTF-344: w decodes the specular COLOUR sample from sRGB.
    srgbFlags: vec4f,
    dielectricFresnel: vec4f,
    textureTransformRows: array<vec4f, 10>,
    // KHR_materials_specular: xyz = UNCLAMPED dielectric F0, w = specularFactor. Unclamped because
    // specularColorTexture multiplies before the min(...,1) below.
    specularFresnelInputs: vec4f,
    specularTextureTransformRows: array<vec4f, 4>,
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(3) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;
// plans/plan_gltf.md GLTF-344: KHR_materials_specular's own two inputs, at the same slots every other
// sampling renderer uses -- strength in the scalar map's ALPHA, colour in the colour map's RGB.
@group(1) @binding(6) var specularTex: texture_2d<f32>;
@group(1) @binding(7) var specularColorTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) blendWeight: vec4f,
    @location(5) blendIndices: vec4<u32>,
    /*CNA_PBR_COLOR_ATTRIBUTE*/
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
    /*CNA_PBR_COLOR_VARYING*/
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

fn pbrSkinNormal(m: mat3x3f, n: vec3f) -> vec3f {
    let c0 = m[0];
    let c1 = m[1];
    let c2 = m[2];
    let co0 = cross(c1, c2);
    let co1 = cross(c2, c0);
    let co2 = cross(c0, c1);
    let det = dot(c0, co0);
    let transformed = mat3x3f(co0, co1, co2) * n;
    if (abs(det) > 1e-6) {
        return transformed * select(-1.0, 1.0, det >= 0.0);
    }
    return m * n;
}

fn pbrDirectionHandedness(m: mat3x3f) -> f32 {
    return select(1.0, -1.0, dot(m[0], cross(m[1], m[2])) < 0.0);
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    // REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix
    // (lp.normalMatrixCol*), not raw worldMat3. Raw World is correct only for rotation and uniform
    // scale and diverges from FNA's mul(normal, WorldInverseTranspose) under non-uniform scale; it
    // also contradicted this renderer's own unskinned pbr3d.wgsl, which already uses the inverse
    // transpose. The tangent stays on raw worldMat3: tangents transform as directions, not as
    // normals (glTF convention, unchanged).
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * pbrSkinNormal(skinMat3, input.normal));
    output.worldTangent = worldMat3 * (skinMat3 * input.tangent.xyz);
    output.bitangentSign = input.tangent.w * pbrDirectionHandedness(worldMat3)
                                           * pbrDirectionHandedness(skinMat3);
    output.worldPos = (lp.world * skinnedPos).xyz;
    /*CNA_PBR_COLOR_ASSIGN*/
    output.uv = input.uv;
    return output;
}

fn srgbToLinear(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + vec3f(0.055)) / 1.055, vec3f(2.4));
    return select(lo, hi, c >= vec3f(0.04045));
}

fn linearToSrgb(c: vec3f) -> vec3f {
    let lo = c * 12.92;
    let hi = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - vec3f(0.055);
    return select(lo, hi, c >= vec3f(0.0031308));
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, f90: vec3f, roughness: f32, metallic: f32) -> vec3f {
    let h = normalize(v + l);
    let ndotl = max(dot(n, l), 0.0);
    let ndotv = max(dot(n, v), 1e-4);
    let ndoth = max(dot(n, h), 0.0);
    let vdoth = max(dot(v, h), 0.0);
    let a2 = pow(roughness, 4.0);
    let dTerm = ndoth * ndoth * (a2 - 1.0) + 1.0;
    let d = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    var k = roughness + 1.0;
    k = k * k / 8.0;
    let g = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));
    let f = f0 + (f90 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

fn pbrSpecularTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.specularTextureTransformRows[slot * 2u].xyz),
                 dot(value, pf.specularTextureTransformRows[slot * 2u + 1u].xyz));
}

fn pbrTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.textureTransformRows[slot * 2u].xyz),
                 dot(value, pf.textureTransformRows[slot * 2u + 1u].xyz));
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u));
    let baseColor = select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5);
    // plans/plan_gltf.md GLTF-465: glTF 3.9.2 makes COLOR_0 an additional LINEAR multiplier on the whole
    // base-colour product, alpha included -- the alpha half is where a BLEND-mode vertex-coloured
    // primitive's transparency comes from. Expanded to the opaque-white identity in the variants
    // whose vertex layout supplies no colour, so one fragment body serves both.
    let cnaVertexColor = /*CNA_PBR_COLOR_VALUE*/vec4f(1.0)/**/;
    let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;
    let useTolerance = pf.alphaTest.y > 0.0;
    let lessTest = alpha < pf.alphaTest.x;
    let toleranceTest = abs(alpha - pf.alphaTest.x) < pf.alphaTest.y;
    let passesAlphaTest = select(lessTest, toleranceTest, useTolerance);
    let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest);
    if (alphaWeight < 0.0) {
        discard;
    }

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    var sampledNormal = textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0;
    sampledNormal.x *= pf.metallicRoughness.z;
    sampledNormal.y *= pf.metallicRoughness.z;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u));
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    // plans/plan_gltf.md GLTF-344: KHR_materials_specular. strength comes from the scalar map's ALPHA and
    // colour from the colour map's sRGB-decoded RGB, each through its own affine transform; the
    // dielectric F0 is min(unclampedF0 * colourSample, 1) * strength, which is the extension's own
    // ordering and the reason the unclamped value is uploaded. A material without either map samples
    // the white identity, so the product collapses to the factor alone.
    let specularStrength = pf.specularFresnelInputs.w
        * textureSample(specularTex, texSampler, pbrSpecularTransformUv(input.uv, 0u)).a;
    let specularColorSample = textureSample(specularColorTex, texSampler,
                                            pbrSpecularTransformUv(input.uv, 1u)).rgb;
    let specularColorLinear = select(specularColorSample, srgbToLinear(specularColorSample),
                                     pf.srgbFlags.w > 0.5);
    let dielectricF0 = min(pf.specularFresnelInputs.xyz * specularColorLinear, vec3f(1.0))
        * specularStrength;
    let f0 = mix(dielectricF0, albedo, metallic);
    let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic);

    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, f90, roughness, metallic);

    let occlusionSample = textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r;
    let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissiveSample = textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u)).rgb;
    let emissiveLinear = select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5);
    let emissive = lp.emissiveColor.xyz * emissiveLinear;

    let linearRgb = ambient + lo + emissive;
    let outputRgb = select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5);
    return vec4f(outputRgb, alpha);
}
)WGSL";

        // plans/plan_gltf.md GLTF-463/GLTF-465: the stride-80 twin. Location 6 is the first free vertex
        // input here, because the skinned record already uses 0..5.
        const std::string bareWgsl = ExpandPbrVertexColourWgslEXT(shaderSource, false, 6);
        const std::string colorWgsl = ExpandPbrVertexColourWgslEXT(shaderSource, true, 6);

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(bareWgsl.c_str());
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        skinnedPbrShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (skinnedPbrShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D shader");

        WGPUShaderSourceWGSL colorWgslChain{};
        colorWgslChain.chain.sType = WGPUSType_ShaderSourceWGSL;
        colorWgslChain.code = StringView(colorWgsl.c_str());
        WGPUShaderModuleDescriptor colorShaderDescriptor{};
        colorShaderDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D VertexColor WGSL");
        colorShaderDescriptor.nextInChain = &colorWgslChain.chain;
        skinnedPbrColorShader_ = wgpuDeviceCreateShaderModule(device_, &colorShaderDescriptor);
        if (skinnedPbrColorShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D vertex-colour shader");

        std::array<WGPUBindGroupLayoutEntry, 4> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[0].buffer.minBindingSize = 128;
        uboEntries[1].binding = 1;
        uboEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[1].buffer.minBindingSize = 272;
        uboEntries[2].binding = 2;
        uboEntries[2].visibility = WGPUShaderStage_Fragment;
        uboEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
        // Four material vec4s followed by ten affine texture-transform rows.
        uboEntries[2].buffer.minBindingSize = 76 * sizeof(float);
        uboEntries[3].binding = 3;
        uboEntries[3].visibility = WGPUShaderStage_Vertex;
        uboEntries[3].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[3].buffer.minBindingSize = (4 + 72 * 16) * sizeof(float);
        WGPUBindGroupLayoutDescriptor uboLayoutDescriptor{};
        uboLayoutDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D BindGroupLayout0");
        uboLayoutDescriptor.entryCount = uboEntries.size();
        uboLayoutDescriptor.entries = uboEntries.data();
        skinnedPbrBindGroupLayout0_ = wgpuDeviceCreateBindGroupLayout(device_, &uboLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{skinnedPbrBindGroupLayout0_, pbrBindGroupLayout1_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        skinnedPbrPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (skinnedPbrBindGroupLayout0_ == nullptr || skinnedPbrPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D GPU resources");
    }

    WGPURenderPipeline WebGPURenderer::GetOrCreatePipelineSkinnedPbr3D(bool colored,
                                                                       WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                    const StencilKeyParams& stencil)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_, replayColorAttachmentCount_)
                                  ^ (HashStencilState(stencil) * 0x9e3779b97f4a7c15ull);
        auto& cache = colored ? skinnedPbrColorPipelines_ : skinnedPbrPipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        // Matches ApplyLayout's stride==68 case (VertexPositionNormalTangentTextureSkinned):
        // Position(12)+Normal(12)+Tangent(16)+TextureCoordinate(8)+BlendWeight(16)+BlendIndices(4).
        // plans/plan_gltf.md GLTF-463/GLTF-465: stride 80 is this record with TEXCOORD_1 at 68 and a
        // packed, normalized COLOR_0 at 76. The second UV set stays unbound, as on the rigid twin.
        std::array<WGPUVertexAttribute, 7> attributes{};
        attributes[0].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector3);
        attributes[1].offset = 12;
        attributes[1].shaderLocation = 1;
        attributes[2].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        attributes[2].offset = 24;
        attributes[2].shaderLocation = 2;
        attributes[3].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector2);
        attributes[3].offset = 40;
        attributes[3].shaderLocation = 3;
        attributes[4].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Vector4);
        attributes[4].offset = 48;
        attributes[4].shaderLocation = 4;
        attributes[5].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Byte4);
        attributes[5].offset = 64;
        attributes[5].shaderLocation = 5;
        if (colored)
        {
            attributes[6].format = WebGPUVertexFormatFromVEF(VertexElementFormat::Color);
            attributes[6].offset = 76;
            attributes[6].shaderLocation = 6;
        }
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = colored ? 80u : 68u;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = colored ? 7u : 6u;
        vertexBufferLayout.attributes = attributes.data();

        std::array<WGPUColorTargetState, 4> mrtColorTargets{};
        const int mrtColorCount = InitStockColorTargetsEXT(mrtColorTargets);
        WGPUColorTargetState& target = mrtColorTargets[0];
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = colored ? skinnedPbrColorShader_ : skinnedPbrShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = static_cast<std::size_t>(mrtColorCount);
        fragment.targets = mrtColorTargets.data();

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU SkinnedPbr3D Pipeline");
        pipeline.layout = skinnedPbrPipelineLayout_;
        pipeline.vertex.module = colored ? skinnedPbrColorShader_ : skinnedPbrShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.stripIndexFormat = stripIndexFormat;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = ToWGPUCullMode(cullMode);
        // WEBGPU-78: baked into the pipeline object (no dynamic WGPU blend override) --
        // target.blend stays null (opaque overwrite) when blend is disabled, matching
        // WGPUColorTargetState::blend's own "absent = no blending" semantics.
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(blendState, blendParams);
        target.blend = blend ? &blendState : nullptr;
        // WEBGPU-58: this renderer's single renderer-GLOBAL MSAA sample count (see sampleCount_'s
        // own comment) -- 1 outside MSAA, identical to every one of these pipelines' behaviour
        // before MSAA existed.
        pipeline.multisample.count = static_cast<std::uint32_t>(sampleCount_);
        pipeline.multisample.mask = CurrentSampleMask(); // REMED-GFX-077: BlendState.MultiSampleMask
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias).
        // Scale matches FNA's own FNA3D_Driver_OpenGL.c XNAToGL_DepthBiasScale for a 24-bit
        // depth format ((1<<24)-1): XNA's DepthBias is a fraction of the depth range,
        // WGPUDepthStencilState::depthBias is an integer count of smallest-representable-
        // depth-buffer units, exactly like D3D/GL's own "scaled by format precision"
        // interpretation (this renderer's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        FillWGPUStencilState(depthStencil, stencil);  // WEBGPU-83
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPURenderer::QueueSkinnedPbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount,
                                                     const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferRenderer&>(vb);
        // plans/plan_gltf.md GLTF-463/GLTF-465: stride 80 is the same record with TEXCOORD_1 and a packed
        // COLOR_0 appended.
        const std::size_t skinnedPbrStride = webgpuVb.Stride();
        if (skinnedPbrStride != 68 && skinnedPbrStride != 80)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedPbrDraw requires a stride-68 or "
                                        "stride-80 (VertexPositionNormalTangentTextureSkinned, "
                                        "optionally with COLOR_0) vertex buffer");

        EnsurePbrDefaultTextures();

        SkinnedPbrDrawCommand command;
        command.colored = (skinnedPbrStride == 80);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * skinnedPbrStride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        // WEBGPU-41/77/78/79: captured at queue time exactly like depthTest/depthWrite/
        // depthFunc above -- each is baked into the pipeline object, so a later ApplyBlendState/
        // ApplyRasterizerState() call must not retroactively change an already-queued draw.
        command.blend = blendEnabled_;
        command.blendParams = blendParams_;
        command.cullMode = cullMode_;
        command.wireframe = fillModeWireframe_;
        command.depthBias = depthBias_;
        command.slopeScaleDepthBias = slopeScaleDepthBias_;
        // REMED-GFX-116: captured here, at the public draw call, for the same reason the
        // pipeline state above is -- a later SetViewport() must not move an already-queued draw.
        command.viewport = CaptureViewport();
        // REMED-GFX-146: and the scissor state with it, for exactly the same reason -- a
        // later ScissorRectangle or RasterizerState change must not reclip an already-
        // queued draw, and SetRenderTarget resets the rectangle to the target's full size
        // on every bind, so the live value at flush time is never this draw's.
        command.scissor = CaptureScissor();
        // WEBGPU-83: the stencil state + reference, captured per draw (a stamp and a gate
        // in one frame differ, so it cannot be read as frame-global at replay).
        command.stencil = CaptureStencilStateEXT();
        command.stencilRef = referenceStencil_;
        // plans/plan_gltf.md GLTF-474/GLTF-465: neutral white when the material carries no base-colour
        // map -- glTF's own default material (3.9.2) has none, and `tex * colour` then collapses
        // to the colour, which is what every other renderer's PBR base-colour bind already does.
        command.baseColorTexture = params.texture0 != nullptr
            ? ResolveSamplable(params.texture0)
            : ResolveSamplable(pbrDefaultWhiteTexture_.get());
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.normalMap = params.pbrNormalMap != nullptr
            ? ResolveSamplable(params.pbrNormalMap)
            : pbrDefaultFlatNormalTexture_->Sampled();
        command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr
            ? ResolveSamplable(params.pbrMetallicRoughnessMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.emissiveMap = params.pbrEmissiveMap != nullptr
            ? ResolveSamplable(params.pbrEmissiveMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.occlusionMap = params.pbrOcclusionMap != nullptr
            ? ResolveSamplable(params.pbrOcclusionMap)
            : pbrDefaultWhiteTexture_->Sampled();
        // plans/plan_gltf.md GLTF-344: white is the identity for both -- alpha 1 leaves the specular
        // factor alone, RGB 1 leaves the unclamped F0 alone -- so a material with neither map
        // renders exactly as the factor-only path already did.
        command.specularMap = params.pbrSpecularMap != nullptr
            ? ResolveSamplable(params.pbrSpecularMap)
            : pbrDefaultWhiteTexture_->Sampled();
        command.specularColorMap = params.pbrSpecularColorMap != nullptr
            ? ResolveSamplable(params.pbrSpecularColorMap)
            : pbrDefaultWhiteTexture_->Sampled();

        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillPbrFactors(command.pbrFactors, params);
        FillSkinningParams(command.skinningParams, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
            command.baseVertex = params.baseVertex;
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        skinnedPbrDrawCommands_.push_back(std::move(command));
        // REMED-GFX-159: the public position of this draw, the only thing replay orders by.
        RecordDrawOrder(DrawFamily::SkinnedPbr, skinnedPbrDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void WebGPURenderer::IssueSkinnedPbrDraw(WGPURenderPassEncoder pass,
                                                    const SkinnedPbrDrawCommand& command,
                                                    ReplayState& state)
    {
        Begin3DDrawState(pass, state);
        if (command.vertexCount == 0 || command.vertexData.empty() || !command.baseColorTexture ||
            !command.normalMap || !command.metallicRoughnessMap ||
            !command.emissiveMap || !command.occlusionMap)
            return;

        WGPUBufferDescriptor vbDescriptor{};
        vbDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D VertexBuffer");
        vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDescriptor.size = Align4(command.vertexData.size());
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
        wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

        WGPUBufferDescriptor uboDescriptor{};
        uboDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D UBO");
        uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uboDescriptor.size = sizeof(command.uniforms);
        WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
        wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

        WGPUBufferDescriptor lightUboDescriptor{};
        lightUboDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D LightUBO");
        lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        lightUboDescriptor.size = sizeof(command.lightUniforms);
        WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
        wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

        WGPUBufferDescriptor factorsUboDescriptor{};
        factorsUboDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D FactorsUBO");
        factorsUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        factorsUboDescriptor.size = sizeof(command.pbrFactors);
        WGPUBuffer factorsUniformBuffer = wgpuDeviceCreateBuffer(device_, &factorsUboDescriptor);
        wgpuQueueWriteBuffer(queue_, factorsUniformBuffer, 0, command.pbrFactors.data(), sizeof(command.pbrFactors));

        WGPUBufferDescriptor skinningUboDescriptor{};
        skinningUboDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D SkinningUBO");
        skinningUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        skinningUboDescriptor.size = sizeof(command.skinningParams);
        WGPUBuffer skinningUniformBuffer = wgpuDeviceCreateBuffer(device_, &skinningUboDescriptor);
        wgpuQueueWriteBuffer(queue_, skinningUniformBuffer, 0, command.skinningParams.data(), sizeof(command.skinningParams));

        std::array<WGPUBindGroupEntry, 4> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].buffer = uniformBuffer;
        uboEntries[0].size = sizeof(command.uniforms);
        uboEntries[1].binding = 1;
        uboEntries[1].buffer = lightUniformBuffer;
        uboEntries[1].size = sizeof(command.lightUniforms);
        uboEntries[2].binding = 2;
        uboEntries[2].buffer = factorsUniformBuffer;
        uboEntries[2].size = sizeof(command.pbrFactors);
        uboEntries[3].binding = 3;
        uboEntries[3].buffer = skinningUniformBuffer;
        uboEntries[3].size = sizeof(command.skinningParams);
        WGPUBindGroupDescriptor uboBindDescriptor{};
        uboBindDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D UBO BindGroup");
        uboBindDescriptor.layout = skinnedPbrBindGroupLayout0_;
        uboBindDescriptor.entryCount = uboEntries.size();
        uboBindDescriptor.entries = uboEntries.data();
        WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

        WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU,
                                                     command.addressV, command.maxAnisotropy,
                                                     "SkinnedPbr3D");
        // plans/plan_gltf.md GLTF-344: entries 6 and 7 are KHR_materials_specular's own maps, which
        // resolve to the white identity when the material declares neither.
        std::array<WGPUBindGroupEntry, 8> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].sampler = sampler;
        texEntries[1].binding = 1;
        texEntries[1].textureView = command.baseColorTexture.View();
        texEntries[2].binding = 2;
        texEntries[2].textureView = command.normalMap.View();
        texEntries[3].binding = 3;
        texEntries[3].textureView = command.metallicRoughnessMap.View();
        texEntries[4].binding = 4;
        texEntries[4].textureView = command.emissiveMap.View();
        texEntries[5].binding = 5;
        texEntries[5].textureView = command.occlusionMap.View();
        texEntries[6].binding = 6;
        texEntries[6].textureView = command.specularMap.View();
        texEntries[7].binding = 7;
        texEntries[7].textureView = command.specularColorMap.View();
        WGPUBindGroupDescriptor texBindDescriptor{};
        texBindDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D Texture BindGroup");
        texBindDescriptor.layout = pbrBindGroupLayout1_;
        texBindDescriptor.entryCount = texEntries.size();
        texBindDescriptor.entries = texEntries.data();
        WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

        WGPURenderPipeline pipe = GetOrCreatePipelineSkinnedPbr3D(command.colored,
                                                                  command.topology,
                                                                  RequiredStripIndexFormat(command),
                                                                  command.depthTest,
                                                                  command.depthWrite, command.depthFunc,
                                                                  command.blend, command.blendParams,
                                                                  command.cullMode, command.wireframe,
                                                                  command.depthBias, command.slopeScaleDepthBias, command.stencil);
        // REMED-GFX-116: this draw's OWN captured Viewport, never the live renderer value.
        ApplyDrawViewport(pass, command.viewport);
        // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
        ApplyDrawScissor(pass, command.scissor);
        // WEBGPU-83: this draw's OWN stencil reference (dynamic; ops/masks are baked into
        // the pipeline above). A gate and a stamp can carry different references in one pass.
        if (command.stencil.enable)
            wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(command.stencilRef));
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

        if (command.indexed && !command.indexData.empty())
        {
            WGPUBuffer indexBuffer = CreateAndBindDeferredIndexBuffer(
                device_, queue_, pass, "CNA WebGPU SkinnedPbr3D IndexBuffer",
                command.indexData, command.index32);
            wgpuRenderPassEncoderDrawIndexed(
                pass, command.indexCount, 1,
                command.firstIndex, command.baseVertex, 0);
            pendingBufferReleases_.push_back(indexBuffer);
        }
        else
        {
            wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
        }

        pendingBindGroupReleases_.push_back(uboBindGroup);
        pendingBindGroupReleases_.push_back(texBindGroup);
        pendingBufferReleases_.push_back(uniformBuffer);
        pendingBufferReleases_.push_back(lightUniformBuffer);
        pendingBufferReleases_.push_back(factorsUniformBuffer);
        pendingBufferReleases_.push_back(skinningUniformBuffer);
        pendingBufferReleases_.push_back(vertexBuffer);
    }
}


namespace CNA::Internal::Renderers
{
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace WebGPU { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> WebGPU::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<WebGPU::WebGPURenderer>(args);
    }
}
