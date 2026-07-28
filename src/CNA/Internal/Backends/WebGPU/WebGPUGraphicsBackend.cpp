#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
#include <SDL3/SDL_metal.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace CNA::Internal::Backends::WebGPU
{
    namespace
    {
        constexpr std::uint64_t kMinimumBufferSize = 4;
        constexpr std::uint64_t kRequestTimeoutNanoseconds = 10'000'000'000ULL;

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
        // live IndexBuffer object or a backend-global "last format".
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
         * attachment this backend renders into must use the linear variant. Channel order is
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

        // See IWebGPUSamplable's own doc comment (WebGPUGraphicsBackend.hpp): resolves any
        // ITextureBackend* to its WGPU-sampleable view, safely degrading to nullptr (treated as
        // "unbound" by every Render*Draws() call site's existing null check) for a null input or
        // a texture from an incompatible concrete type, instead of the unchecked static_cast this
        // replaces everywhere.
        [[nodiscard]] const IWebGPUSamplable* ResolveSamplable(const ITextureBackend* tex)
        {
            return tex != nullptr ? dynamic_cast<const IWebGPUSamplable*>(tex) : nullptr;
        }

        // WEBGPU-114: the IWebGPUCubeSamplable sibling of ResolveSamplable() above -- resolves any
        // ITextureCubeBackend* (WebGPUTextureCubeBackend OR WebGPURenderTargetCubeBackend) to its
        // whole-cube sampling view, safely degrading to nullptr for a null input or an incompatible
        // concrete type.
        [[nodiscard]] const IWebGPUCubeSamplable* ResolveCubeSamplable(const ITextureCubeBackend* tex)
        {
            return tex != nullptr ? dynamic_cast<const IWebGPUCubeSamplable*>(tex) : nullptr;
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

        [[nodiscard]] WGPUAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return WGPUAddressMode_Repeat;
                case 2: return WGPUAddressMode_MirrorRepeat;
                default: return WGPUAddressMode_ClampToEdge;
            }
        }

        [[nodiscard]] int SamplerCacheIndex(int filter, int addressU, int addressV)
        {
            const int filterIndex = filter == 0 ? 0 : 1;
            const int u = std::clamp(addressU, 0, 2);
            const int v = std::clamp(addressV, 0, 2);
            return filterIndex * 9 + u * 3 + v;
        }

        // ---- WEBGPU-41/77/78/79/80/81/82/83: graphics-state -> WGPU translation helpers ----
        // Shared by every 3D GetOrCreatePipeline*() family so the exact same XNA->WGPU mapping is
        // used everywhere (mirrors ToWGPUCompareFunction()'s own established role/pattern, and
        // VulkanGraphicsBackend::ToVkBlendFactor()/ToVkBlendOp()/FillBlendAttachmentState()'s
        // identical purpose on that backend).

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
        void FillWGPUBlendState(WGPUBlendState& blendState, const WebGPUGraphicsBackend::BlendKeyParams& bp)
        {
            blendState.color.srcFactor = ToWGPUBlendFactor(bp.colorSrc);
            blendState.color.dstFactor = ToWGPUBlendFactor(bp.colorDst);
            blendState.color.operation = ToWGPUBlendOperation(bp.colorFunc);
            blendState.alpha.srcFactor = ToWGPUBlendFactor(bp.alphaSrc);
            blendState.alpha.dstFactor = ToWGPUBlendFactor(bp.alphaDst);
            blendState.alpha.operation = ToWGPUBlendOperation(bp.alphaFunc);
        }

        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. Every 3D pipeline
        // sets pipeline.primitive.frontFace = WGPUFrontFace_CCW. Empirically verified (not derived
        // by pure reasoning about NDC/raster-space winding, which turned out backwards on a first
        // pass) via WebGPU_GraphicsState's differential cull-mode checks: a hand-derived-winding
        // quad renders under CullCounterClockwiseFace (XNA's own default -- an ordinary
        // front-facing quad must stay visible) and is culled under CullClockwiseFace. This is the
        // OPPOSITE pairing of what a naive NDC-vs-raster-space mirroring argument predicts,
        // confirming this project's established "empirically verify, don't just derive" rule for
        // this exact class of winding/orientation subtlety (see VulkanGraphicsBackend::
        // FillDepthStencilState()'s own front/back-swap comment for the analogous precedent there).
        [[nodiscard]] WGPUCullMode ToWGPUCullMode(int xnaCullMode)
        {
            switch (xnaCullMode)
            {
                case 1: return WGPUCullMode_Front;  // CullClockwiseFace
                case 2: return WGPUCullMode_Back;   // CullCounterClockwiseFace
                default: return WGPUCullMode_None;
            }
        }

        // Encodes (topology, strip-index compatibility, depth test/write/func, blend enable+params,
        // cull mode, wireframe, depth bias, slope-scale depth bias, and a caller-supplied salt for
        // any extra dimension -- e.g. stride for AlphaTest3D/DualTexture3D) into a single uint64_t
        // pipeline cache key.
        // Mirrors VulkanGraphicsBackend::Make3DKey()/PackBlendBits()'s own role: when blend is
        // disabled, the (irrelevant) blend factors always collapse to the same bits so different
        // disabled BlendStates don't create duplicate pipelines.
        [[nodiscard]] std::uint64_t Make3DPipelineKey(
            WGPUPrimitiveTopology topology,
            WGPUIndexFormat stripIndexFormat,
            bool depthTest,
            bool depthWrite,
            int depthFunc,
            bool blend,
            const WebGPUGraphicsBackend::BlendKeyParams& bp,
            int cullMode,
            bool wireframe,
            float depthBias,
            float slopeScaleDepthBias,
            std::uint64_t salt,
            int colorWriteMask,
            std::uint32_t sampleMask)
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
            return key;
        }

        // WEBGPU-82: fills a full WGPUSamplerDescriptor (address modes, mag/min/mipmap filter,
        // anisotropy) from a real XNA SamplerState -- shared translation used by
        // GetOrCreateSlotSampler(), mirrors VulkanGraphicsBackend::ApplySamplerState()'s own
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
            // filter==2 above) -- matches VulkanGraphicsBackend::ApplySamplerState()'s identical
            // enableAniso gate.
            descriptor.maxAnisotropy = enableAniso
                ? static_cast<std::uint16_t>(std::clamp(maxAnisotropy, 1, 16))
                : 1;
        }

        // Mirrors VulkanGraphicsBackend::DrawColoredPrimitives()'s own use of
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

        // Mirrors VulkanGraphicsBackend::FillExtPushConst() field-for-field (real GpuDrawParams
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
        // BgfxGraphicsBackend::ComputeNormalMatrix3x3() byte-for-byte (verified independently
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
        // VulkanGraphicsBackend's LitLightParams UBO field-for-field (minus fog, deliberately
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
        // push constants -- mirrors VulkanGraphicsBackend::FillEnvMapPushConst() field-for-field.
        void FillEnvMapTransform(std::array<float, 32>& out, const Matrix& mvp, const Matrix& world)
        {
            mvp.ToColumnMajor(out.data());
            world.ToColumnMajor(out.data() + 16);
        }

        // env_map3d.wgsl's EnvMapParams UBO (group 0 binding 1): mirrors
        // VulkanGraphicsBackend::DrawPrimitivesEx()'s own envMapUboData[] packing field-for-field
        // (cross-checked against EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()'s identical
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

        // Mirrors VulkanGraphicsBackend::FillAlphaTestPushConst() field-for-field. AlphaTestEffect
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

        // pbr3d.wgsl's third (small) uniform buffer: PbrEffect's MetallicFactor/RoughnessFactor,
        // the only per-draw PBR-specific scalars not already covered by FillExtUniforms()'s
        // diffuseColor/ambientColor or FillLitLightUniforms()'s emissiveColor/world/eyePos.
        void FillPbrFactors(std::array<float, 4>& out, const GpuDrawParams& p)
        {
            out[0] = p.pbrMetallicFactor;
            out[1] = p.pbrRoughnessFactor;
            out[2] = 0.0f;
            out[3] = 0.0f;
        }

        // New bone-palette uniform buffer for the skinned shaders (skinned3d.wgsl/skinned_pbr3d.wgsl):
        // a small vec4f header (x = WeightsPerVertex, Task 895's 1/2/4 convention -- only the first
        // N weight/index pairs are summed) followed by all 72 column-major bone matrices, matching
        // EasyGLGraphicsBackend's own uBones[72] uniform array shape. Always copies the full fixed
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
                wgpuInstanceProcessEvents(instance);
                if (!completed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!completed)
            {
                throw std::runtime_error(
                    std::string("CNA WebGPU: timed out waiting for ") + operation);
            }
        }
    }

    WebGPUTextureBackend::WebGPUTextureBackend(WebGPUGraphicsBackend& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height), mipLevels_(std::max(1, data.mipLevels))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument("CNA WebGPU: texture dimensions must be positive");

        if (!data.pixels.empty())
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
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                           WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
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

        if (!data.pixels.empty())
            UpdatePixels(data.pixels.data(), width_ * 4);
    }

    WebGPUTextureBackend::~WebGPUTextureBackend()
    {
        if (view_ != nullptr)
            wgpuTextureViewRelease(view_);
        if (texture_ != nullptr)
            wgpuTextureRelease(texture_);
    }

    void WebGPUTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
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
        // WebGPUGraphicsBackend::EnsureMipBlitPipeline()'s own doc comment for the full rationale
        // and the deliberate divergence this introduces from FNA and every sibling CNA backend
        // (which only auto-regenerate mips for a RENDER TARGET being unbound, not a plain
        // Texture2D). Placed here (not just in the constructor) so a LATER Texture2D::SetData()
        // call that replaces level 0 (the only backend entry point XNA-layer SetData(level=0,...)
        // ever calls, even for a partial sub-rectangle -- see Texture2D.cpp's own
        // getMipBuffer()-backed whole-level re-upload) regenerates the mip chain again too,
        // consistent with WebGPUTextureCubeBackend::SetData()'s identical per-level-0-write
        // trigger. A no-op when mipLevels_ == 1.
        if (mipLevels_ > 1)
            owner_->GenerateMips2D(texture_, width_, height_, mipLevels_);
    }

    void WebGPUTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_ || rgba == nullptr || levelW <= 0 || levelH <= 0)
            throw std::invalid_argument("CNA WebGPU: invalid mip upload");

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture_;
        destination.mipLevel = static_cast<std::uint32_t>(level);
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<std::uint32_t>(levelW * 4);
        layout.rowsPerImage = static_cast<std::uint32_t>(levelH);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};
        const std::size_t byteCount = static_cast<std::size_t>(levelW) * static_cast<std::size_t>(levelH) * 4u;
        wgpuQueueWriteTexture(owner_->Queue(), &destination, rgba, byteCount, &layout, &extent);
    }

    // WEBGPU-51: real CPU readback for an arbitrary Texture2D -- reuses the exact staged
    // MAP_READ-buffer/aligned-row/async-map-and-poll technique WEBGPU-91's ReadBackbuffer() and
    // WebGPURenderTargetBackend::GetData() already established (see both for the precedent).
    // Copies the WHOLE requested mip level to a temporary buffer, then extracts the x/y/w/h
    // sub-rectangle from the mapped memory -- this texture is always WGPUTextureFormat_RGBA8Unorm
    // (see the constructor above), so unlike the swapchain/a RenderTarget2D there is never a
    // BGRA byte-swap to apply.
    bool WebGPUTextureBackend::GetData(int level, int x, int y, int w, int h,
                                        void* data, int dataLength) const
    {
        // REMED-GFX-127: nothing is written on these paths, so they must not be reported as a
        // completed readback -- the shared layer would otherwise convert its own zeroed scratch.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (level < 0 || level >= mipLevels_)
            throw std::out_of_range("CNA WebGPU: Texture2D.GetData: mip level out of range");

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
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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

    // WEBGPU-56/74: minimal cube-map texture backend, just enough for
    // EnvironmentMapEffect.EnvironmentMap. Mirrors EasyGLTextureCubeBackend's own
    // CalculateCubeMipLevels(size) mip-count convention. A WebGPU cube map is a plain
    // WGPUTextureDimension_2D texture with 6 array layers; only the *view* (created below with
    // WGPUTextureViewDimension_Cube) makes it sampleable as texture_cube<f32> in WGSL.
    WebGPUTextureCubeBackend::WebGPUTextureCubeBackend(WebGPUGraphicsBackend& owner, int size, bool mipMap)
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
    }

    WebGPUTextureCubeBackend::~WebGPUTextureCubeBackend()
    {
        if (cubeView_ != nullptr) wgpuTextureViewRelease(cubeView_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    // face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z (matches IRenderTargetCubeBackend's own documented
    // convention and TextureCube.cpp's CubeMapFace ordinal values) -- maps directly onto the
    // texture's array layer index, since WebGPU cube faces are just array layers 0..5 in a fixed,
    // implementation-defined order that matches this.
    bool WebGPUTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int dataLength)
    {
        // REMED-GFX-135: these were `throw std::out_of_range`/`std::invalid_argument` from inside a
        // backend, so a request this backend cannot store escaped the public API as a raw std
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
        // level 0 -- see WebGPUGraphicsBackend::EnsureMipBlitPipeline()'s own doc comment for the
        // full rationale and the deliberate divergence this introduces from FNA and every sibling
        // CNA backend. Triggers on every level-0 write (even a partial sub-rectangle, matching
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
    // as WebGPUTextureBackend::GetData() (WEBGPU-51) -- origin.z = face selects the array layer,
    // matching SetData()'s own established convention above. Always RGBA8Unorm, so no BGRA swap.
    bool WebGPUTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
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
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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
    WebGPURenderTargetCubeBackend::WebGPURenderTargetCubeBackend(WebGPUGraphicsBackend& owner, int size,
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
                                     "(mipMap=true) is not implemented on this backend -- see "
                                     "plan_webgpu.md WEBGPU-114");
        // Always allocates a Depth24PlusStencil8 attachment regardless of the requested
        // depthFormat -- identical simplification to WebGPURenderTargetBackend's own constructor
        // (see that class's constructor comment for the full rationale).
        (void) depthFormat;

        // Colour texture: this instance's own surfaceFormat_ snapshot (matching
        // WebGPURenderTargetBackend's own choice), NOT WGPUTextureFormat_RGBA8Unorm
        // (WebGPUTextureCubeBackend's own convention for a plain upload-only TextureCube) --
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
        // face is ever bound/rendered-into at a time (see WebGPUGraphicsBackend::
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
    }

    WebGPURenderTargetCubeBackend::~WebGPURenderTargetCubeBackend()
    {
        // RenderTargetCube::Dispose() (mirroring RenderTarget2D's own Task 717 precedent) already
        // refuses to dispose a render target still bound on its GraphicsDevice -- kept as
        // defense-in-depth, mirroring WebGPURenderTargetBackend's own identical destructor guard.
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

    void WebGPURenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (owner_ == nullptr || face < 0 || face >= 6) return;
        if (owner_->currentRenderTargetCubeFace_ == this && owner_->currentRenderTargetCubeFaceIndex_ == face)
            return; // already the current target/face -- nothing to flush/switch.

        // WEBGPU-114: flush whatever was previously bound (backbuffer / a RenderTarget2D / a
        // DIFFERENT face of this or another RenderTargetCube) into its own render pass before
        // switching to this face -- mirrors WebGPUGraphicsBackend::SetRenderTarget2D()'s own
        // eager-flush-on-switch design, generalised via FlushCurrentRenderTarget() so a cube face
        // is just as much a distinct "target identity" as a RenderTarget2D or the backbuffer.
        owner_->FlushCurrentRenderTarget();
        owner_->currentRenderTarget_ = nullptr;
        owner_->currentRenderTargetCubeFace_ = this;
        owner_->currentRenderTargetCubeFaceIndex_ = face;
    }

    void WebGPURenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr && owner_->currentRenderTargetCubeFace_ == this)
        {
            owner_->currentRenderTargetCubeFace_ = nullptr;
            owner_->currentRenderTargetCubeFaceIndex_ = -1;
        }
    }

    // WEBGPU-114: real per-face CPU readback -- same staged-copy/row-alignment/async-map
    // technique as WebGPUTextureCubeBackend::GetData() (WEBGPU-113), plus the on-demand-flush
    // check WebGPURenderTargetBackend::GetData() (WEBGPU-53/54) established for a still-bound
    // render target.
    bool WebGPURenderTargetCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
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
        // the same defect shape REMED-GFX-135 already removed from this backend's SetData.
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
                const_cast<WebGPURenderTargetCubeBackend*>(this), owner_->currentRenderTargetCubeFaceIndex_);

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
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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
    WebGPUTexture3DBackend::WebGPUTexture3DBackend(WebGPUGraphicsBackend& owner, int width, int height,
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

    WebGPUTexture3DBackend::~WebGPUTexture3DBackend()
    {
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    bool WebGPUTexture3DBackend::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int dataLength)
    {
        // REMED-GFX-135: see WebGPUTextureCubeBackend::SetData for why these are results, not
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

    // Same staged-copy/row-alignment/async-map technique as WebGPUTextureBackend::GetData()
    // (WEBGPU-51), extended to a 3rd (depth) dimension: the WHOLE requested level's volume is
    // copied to a temporary readback buffer (alignedBytesPerRow * levelHeight * levelDepth), then
    // the x/y/z/w/h/depth sub-volume is extracted from the mapped memory on the CPU side.
    bool WebGPUTexture3DBackend::GetData(int level, int x, int y, int z,
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
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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

    WebGPURenderTargetBackend::WebGPURenderTargetBackend(WebGPUGraphicsBackend& owner, int width, int height,
                                                          int depthFormat, bool preserveContents)
        : owner_(&owner), width_(width), height_(height), preserveContents_(preserveContents)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument("CNA WebGPU: RenderTarget2D dimensions must be positive");

        // Always created in the backend's colour format (surfaceFormat_), NOT
        // WGPUTextureFormat_RGBA8Unorm (the format every plain WebGPUTextureBackend always uses)
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
        // Depth24PlusStencil8 depth-stencil state -- this backend has no "pipeline with no depth
        // attachment" variant at all (matching how the swapchain's own depthTexture_ is likewise
        // always allocated unconditionally, see RecreateDepthTexture()) -- so, mirroring
        // VulkanGraphicsBackend's own documented "always allocates a combined depth+stencil
        // buffer using its device-wide format regardless of the exact value requested"
        // simplification (see IGraphicsBackend::CreateRenderTarget2D's own doc comment), this
        // render target ALWAYS allocates a real Depth24PlusStencil8 attachment too, even when
        // depthFormat is DepthFormat::None, purely so a 3D draw into it stays pipeline-compatible.
        // This is invisible to game code: HasRealDepthBuffer()'s inherited IRenderTargetBackend
        // default still reports based on what was actually REQUESTED (the depthFormatWasRequested
        // parameter GraphicsDevice::Clear() computes from RenderTarget2D::
        // getDepthStencilFormatProperty()), not on this internal implementation detail, so
        // GraphicsDevice.Clear(ClearOptions::DepthBuffer) is still correctly masked out for a
        // target that requested DepthFormat::None, exactly as it is on every other backend.
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
        // class's own top-of-class doc comment (WebGPUGraphicsBackend.hpp) for why the per-instance
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
    }

    WebGPURenderTargetBackend::~WebGPURenderTargetBackend()
    {
        // RenderTarget2D::Dispose() (Task 717's own precedent) already refuses to dispose a
        // render target still bound on its GraphicsDevice, so this should never actually trigger
        // in practice -- kept as defense-in-depth, mirroring VulkanRenderTargetBackend's own
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

    void WebGPURenderTargetBackend::BindAsRenderTarget()
    {
        // Pure bookkeeping -- mirrors VulkanRenderTargetBackend::BindAsRenderTarget()'s identical
        // role. The real work (flushing whatever was queued for the PREVIOUS target into its own
        // render pass) already happened in WebGPUGraphicsBackend::SetRenderTarget2D() before this
        // is called; see that function's own comment.
        if (owner_ != nullptr) owner_->currentRenderTarget_ = this;
    }

    void WebGPURenderTargetBackend::UnbindAsRenderTarget()
    {
        if (owner_ != nullptr && owner_->currentRenderTarget_ == this) owner_->currentRenderTarget_ = nullptr;
    }

    bool WebGPURenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        // REMED-GFX-127: see WebGPUTextureBackend::GetData -- nothing written means false, never a
        // silently successful return.
        if (owner_ == nullptr || w <= 0 || h <= 0 || data == nullptr)
            return false;
        if (level != 0)
            throw std::invalid_argument("CNA WebGPU: RenderTarget2D.GetData: mip level > 0 is not "
                                        "supported on this backend (no mip chain, see "
                                        "plan_webgpu.md WEBGPU-53/54)");

        // Mirrors WebGPUGraphicsBackend::ReadBackbuffer()'s own on-demand-flush pattern -- if
        // this render target is STILL the currently bound target, render whatever has been
        // queued into it so far before reading it back, so a SetRenderTarget(rt)+Clear()/draw+
        // GetData() sequence observes its own content without an intervening SetRenderTarget
        // (nullptr) switch first.
        if (owner_->currentRenderTarget_ == this)
            owner_->RenderPendingDrawsToRenderTarget(const_cast<WebGPURenderTargetBackend*>(this));

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
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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

    WebGPUVertexBufferBackend::WebGPUVertexBufferBackend(WebGPUGraphicsBackend& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(std::max(0, vertexCapacity))
    {
    }

    WebGPUVertexBufferBackend::~WebGPUVertexBufferBackend()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions{});
    }

    void WebGPUVertexBufferBackend::SetDataWithOptions(const void* data,
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

    WebGPUIndexBufferBackend::WebGPUIndexBufferBackend(WebGPUGraphicsBackend& owner,
                                                        int indexCapacity,
                                                        bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(std::max(0, indexCapacity)), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    WebGPUIndexBufferBackend::~WebGPUIndexBufferBackend()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUIndexBufferBackend::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferBackend::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true); }
    void WebGPUIndexBufferBackend::SetData16WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferBackend::SetData32WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, true); }

    void WebGPUIndexBufferBackend::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit)
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
        // that this deferred backend already needs, padding only the native write while keeping
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

    WebGPUSpriteBatchBackend::WebGPUSpriteBatchBackend(WebGPUGraphicsBackend& owner) : owner_(&owner) {}

    void WebGPUSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.Begin called twice without End");
        // REMED-GFX-102: SpriteBatch::Begin has already atomically applied its by-value
        // BlendState (including BlendFactor) to GraphicsDevice. Capture that complete normalized
        // state now, before Deferred sorting postpones the backend Draw calls until End().
        blendSnapshot_ = owner_->CaptureSpriteBlendSnapshot();
        begun_ = true;
    }

    void WebGPUSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void WebGPUSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error("CNA WebGPU: custom SpriteBatch effects are not implemented yet");
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
             SpriteEffects::None, 0.0f);
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture,
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
        // WEBGPU-53/54: resolves either a plain WebGPUTextureBackend or a WebGPURenderTargetBackend
        // (a RenderTarget2D sampled back as an ordinary texture) -- see IWebGPUSamplable's own doc
        // comment for why a single dynamic_cast<const WebGPUTextureBackend*> no longer covers every
        // valid case now that this backend has a second ITextureBackend-implementing class.
        const auto* samplable = dynamic_cast<const IWebGPUSamplable*>(&texture);
        if (samplable == nullptr)
            throw std::invalid_argument("CNA WebGPU: SpriteBatch received a texture from another graphics backend");
        owner_->QueueSprite(texture, *samplable, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_,
                            blendSnapshot_);
    }

    WebGPUGraphicsBackend::WebGPUGraphicsBackend(SDL_Window* window,
                                                  int virtualWidth,
                                                  int virtualHeight,
                                                  CnaPresentationMode presentationMode,
                                                  int swapInterval)
        : window_(window),
          virtualWidth_(virtualWidth),
          virtualHeight_(virtualHeight),
          presentationMode_(presentationMode),
          swapInterval_(swapInterval)
    {
        if (window_ == nullptr)
            throw std::invalid_argument("CNA WebGPU: SDL window cannot be null");

        instance_ = wgpuCreateInstance(nullptr);
        if (instance_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuCreateInstance failed");

        try
        {
            CreateSurface();
            RequestAdapterAndDevice();
            ConfigureSurface(true);
            IGraphicsBackend::RegisterForWindow(window_, this);
        }
        catch (...)
        {
            DestroySpriteResources();
            for (WGPUSampler& sampler : samplerCache_)
            {
                if (sampler != nullptr) wgpuSamplerRelease(sampler);
                sampler = nullptr;
            }
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
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
            if (metalView_ != nullptr) SDL_Metal_DestroyView(metalView_);
#endif
            throw;
        }
    }

    WebGPUGraphicsBackend::~WebGPUGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
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
        for (WGPUSampler& sampler : samplerCache_)
        {
            if (sampler != nullptr)
                wgpuSamplerRelease(sampler);
            sampler = nullptr;
        }
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
        if (hasAcquiredTexture_ && acquiredTexture_ != nullptr) wgpuTextureRelease(acquiredTexture_);
        if (surfaceConfigured_ && surface_ != nullptr) wgpuSurfaceUnconfigure(surface_);
        if (queue_ != nullptr) wgpuQueueRelease(queue_);
        if (device_ != nullptr) wgpuDeviceRelease(device_);
        if (adapter_ != nullptr) wgpuAdapterRelease(adapter_);
        if (surface_ != nullptr) wgpuSurfaceRelease(surface_);
        if (instance_ != nullptr) wgpuInstanceRelease(instance_);
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
        if (metalView_ != nullptr) SDL_Metal_DestroyView(metalView_);
#endif
    }

    void WebGPUGraphicsBackend::CreateSurface()
    {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        const char* driver = SDL_GetCurrentVideoDriver();
        WGPUSurfaceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Surface");

#if defined(_WIN32)
        WGPUSurfaceSourceWindowsHWND source{};
        source.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        source.hinstance = GetModuleHandleW(nullptr);
        source.hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (source.hwnd == nullptr)
            throw std::runtime_error("CNA WebGPU: SDL did not expose a Win32 HWND");
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
        metalView_ = SDL_Metal_CreateView(window_);
        if (metalView_ == nullptr)
            throw std::runtime_error(std::string("CNA WebGPU: SDL_Metal_CreateView failed: ") + SDL_GetError());
        WGPUSurfaceSourceMetalLayer source{};
        source.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        source.layer = SDL_Metal_GetLayer(metalView_);
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__ANDROID__) && defined(SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER)
        WGPUSurfaceSourceAndroidNativeWindow source{};
        source.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
        source.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        if (source.window == nullptr)
            throw std::runtime_error("CNA WebGPU: SDL did not expose Android native window");
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__linux__)
        if (driver != nullptr && std::strcmp(driver, "wayland") == 0)
        {
            WGPUSurfaceSourceWaylandSurface source{};
            source.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
            source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            source.surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            if (source.display == nullptr || source.surface == nullptr)
                throw std::runtime_error("CNA WebGPU: SDL did not expose Wayland display/surface properties");
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else if (driver != nullptr && std::strcmp(driver, "x11") == 0)
        {
            WGPUSurfaceSourceXlibWindow source{};
            source.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            source.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            if (source.display == nullptr || source.window == 0)
                throw std::runtime_error("CNA WebGPU: SDL did not expose X11 display/window properties");
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else
        {
            throw std::runtime_error(std::string("CNA WebGPU: unsupported SDL Linux video driver: ") +
                                     (driver != nullptr ? driver : "unknown"));
        }
#else
        (void) properties;
        (void) driver;
        throw std::runtime_error("CNA WebGPU: native surface creation is unsupported on this platform");
#endif

        if (surface_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuInstanceCreateSurface failed");
    }

    void WebGPUGraphicsBackend::RequestAdapterAndDevice()
    {
        AdapterRequestState adapterState;
        WGPURequestAdapterOptions adapterOptions{};
        adapterOptions.compatibleSurface = surface_;
        WGPURequestAdapterCallbackInfo adapterCallback{};
        adapterCallback.mode = WGPUCallbackMode_AllowProcessEvents;
        adapterCallback.callback = OnAdapterRequest;
        adapterCallback.userdata1 = &adapterState;
        wgpuInstanceRequestAdapter(instance_, &adapterOptions, adapterCallback);
        WaitForCompletion(instance_, adapterState.completed, "adapter request");
        if (adapterState.adapter == nullptr)
            throw std::runtime_error("CNA WebGPU: adapter request failed: " + adapterState.error);
        adapter_ = adapterState.adapter;

        DeviceRequestState deviceState;
        WGPUDeviceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Device");
        descriptor.defaultQueue.label = StringView("CNA WebGPU Queue");
        descriptor.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
        descriptor.uncapturedErrorCallbackInfo.userdata1 = &uncapturedErrorCount_;
        descriptor.deviceLostCallbackInfo.callback = OnDeviceLost;
        WGPURequestDeviceCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
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

    void WebGPUGraphicsBackend::ConfigureSurface(bool force)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        const bool minimized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
        if (minimized || width <= 0 || height <= 0)
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

        // REMED-GFX-131: prefer a NON-sRGB surface format, exactly as VulkanGraphicsBackend::
        // CreateSwapchain() already does for the identical decision, and for the identical reason:
        // XNA's SurfaceFormat.Color -- the default backbuffer format, and the only format the
        // IGraphicsBackend render-target entry points can even express -- is a plain 8-bit UNORM
        // byte format, while SurfaceFormat.ColorSrgbEXT is the separate gamma-encoded one. An sRGB
        // surface format makes the hardware encode every value the render pass stores, and this
        // backend copies the surface format into every render target and every pipeline's colour
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
        // below through viewFormats so every view this backend creates over a surface texture
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

    void WebGPUGraphicsBackend::RecreateDepthTexture()
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

    void WebGPUGraphicsBackend::RecreateMsaaColorTexture()
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

    void WebGPUGraphicsBackend::DestroySpriteResources()
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

    void WebGPUGraphicsBackend::CreateSpriteResources()
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

    WebGPUSpriteBlendSnapshot WebGPUGraphicsBackend::CaptureSpriteBlendSnapshot() const
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreateSpritePipeline(
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

        if (const auto found = spritePipelines_.find(key); found != spritePipelines_.end())
            return found->second;
        if (spriteShader_ == nullptr || spritePipelineLayout_ == nullptr ||
            snapshot.targetFormat == WGPUTextureFormat_Undefined || snapshot.sampleCount == 0)
            throw std::runtime_error("CNA WebGPU: invalid SpriteBatch pipeline compatibility state");

        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(SpriteVertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = offsetof(SpriteVertex, uv);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x4;
        attributes[2].offset = offsetof(SpriteVertex, color);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(SpriteVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
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
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        SDL_Log("[WebGPU][GFX-102] Sprite pipeline cache miss -> size=%zu blend=%d "
                "color=(%d,%d,%d) alpha=(%d,%d,%d) writeMask=0x%X sampleMask=0x%08X "
                "format=%d samples=%u",
                spritePipelines_.size(), snapshot.blendEnabled ? 1 : 0,
                snapshot.colorSrc, snapshot.colorDst, snapshot.colorFunc,
                snapshot.alphaSrc, snapshot.alphaDst, snapshot.alphaFunc,
                snapshot.colorWriteMask & 0xF, snapshot.multiSampleMask,
                static_cast<int>(snapshot.targetFormat), snapshot.sampleCount);
        return created;
    }

    void WebGPUGraphicsBackend::DestroyColoredResources()
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

    void WebGPUGraphicsBackend::CreateColoredResources()
    {
        DestroyColoredResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Uniform layout matches VulkanGraphicsBackend::FillExtPushConst()'s 128-byte/32-float
        // push-constant layout byte-for-byte (see plan_webgpu.md's Phase 57 entry-point note):
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineColored3D(WGPUPrimitiveTopology topology,
                                                                            WGPUIndexFormat stripIndexFormat,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = coloredPipelines_.find(key); it != coloredPipelines_.end())
            return it->second;

        struct ColoredVertex { float x, y, z; std::uint8_t r, g, b, a; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(ColoredVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Unorm8x4;
        attributes[1].offset = offsetof(ColoredVertex, r);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = coloredShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Colored3D pipeline");
        coloredPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyTexturedResources()
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

    void WebGPUGraphicsBackend::CreateTexturedResources()
    {
        DestroyTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Uniform layout is the exact same group-0 UBO as colored3d.wgsl (coloredBindGroupLayout_,
        // reused verbatim -- see plan_webgpu.md's WEBGPU-13 note). Group 1 (sampler + texture)
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineTextured3D(WGPUPrimitiveTopology topology,
                                                                            WGPUIndexFormat stripIndexFormat,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = texturedPipelines_.find(key); it != texturedPipelines_.end())
            return it->second;

        struct TexturedVertex { float x, y, z; float u, v; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(TexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = offsetof(TexturedVertex, u);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(TexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = texturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Textured3D pipeline");
        texturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineColoredTextured3D(WGPUPrimitiveTopology topology,
                                                                                    WGPUIndexFormat stripIndexFormat,
                                                                                    bool depthTest, bool depthWrite,
                                                                                    int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = coloredTexturedPipelines_.find(key); it != coloredTexturedPipelines_.end())
            return it->second;

        struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(ColoredTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Unorm8x4;
        attributes[1].offset = offsetof(ColoredTexturedVertex, r);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(ColoredTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = coloredTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create ColoredTextured3D pipeline");
        coloredTexturedPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyLitTexturedResources()
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

    void WebGPUGraphicsBackend::CreateLitTexturedResources()
    {
        DestroyLitTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's lit_textured3d.{vert,frag}.glsl (itself ported from
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

        // Task 1105 (plan_graphics.md Phase 80): real per-vertex-lit sibling -- identical
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineLitTextured3D(WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = litTexturedPipelines_.find(key); it != litTexturedPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = litTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D pipeline");
        litTexturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineLitTextured3DVertexLit(
        WGPUPrimitiveTopology topology, WGPUIndexFormat stripIndexFormat,
        bool depthTest, bool depthWrite, int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = litTexturedVertexLitPipelines_.find(key); it != litTexturedVertexLitPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = litTexturedVertexLitShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D VertexLit pipeline");
        litTexturedVertexLitPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyAlphaTestResources()
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

    void WebGPUGraphicsBackend::CreateAlphaTestResources()
    {
        DestroyAlphaTestResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's alpha_test3d.{vert,frag}.glsl. AlphaTestEffect has
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineAlphaTest3D(std::size_t stride,
                                                                              WGPUPrimitiveTopology topology,
                                                                              WGPUIndexFormat stripIndexFormat,
                                                                              bool depthTest, bool depthWrite,
                                                                              int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias,
                                                     static_cast<std::uint64_t>(stride), colorWriteMask_, sampleMask_);
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
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Unorm8x4;
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
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
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(LitTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(LitTexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(LitTexturedVertex);
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
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

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTest3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyDualTextureResources()
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

    void WebGPUGraphicsBackend::CreateDualTextureResources()
    {
        DestroyDualTextureResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's dual_texture3d.{vert,frag}.glsl. DualTextureEffect
        // has no lighting and no alpha test, so group 0 reuses coloredBindGroupLayout_/the primary
        // Uniforms layout unchanged. Group 1 is a NEW shape (one shared sampler + two textures,
        // since DualTextureEffect samples both layers at the same UV with one shared
        // TextureFilter/AddressMode, matching every other WebGPU 3D shader's own
        // single-sampler-per-draw simplification).
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
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;

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
    var sample0 = textureSample(tex0, texSampler, input.uv);
    let sample1 = textureSample(tex1, texSampler, input.uv);
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
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;

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
    var sample0 = textureSample(tex0, texSampler, input.uv);
    let sample1 = textureSample(tex1, texSampler, input.uv);
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

        std::array<WGPUBindGroupLayoutEntry, 3> layoutEntries{};
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineDualTexture3D(std::size_t stride,
                                                                                WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias,
                                                     static_cast<std::uint64_t>(stride), colorWriteMask_, sampleMask_);
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
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Unorm8x4;
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = offsetof(ColoredTexturedVertex, u);
            attributes[2].shaderLocation = 2;
            attributeCount = 3;
            arrayStride = sizeof(ColoredTexturedVertex);
            shaderModule = dualTextureColoredShader_;
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
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

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyEnvMapResources()
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

    void WebGPUGraphicsBackend::CreateEnvMapResources()
    {
        DestroyEnvMapResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // WEBGPU-25/36/74: ported from VulkanGraphicsBackend's env_map3d.{vert,frag}.glsl, itself
        // cross-checked against EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()'s identical GLSL
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
    let envSample = textureSample(envMap, texSampler, reflDir);
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

        std::array<WGPUBindGroupLayoutEntry, 3> texLayoutEntries{};
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

    void WebGPUGraphicsBackend::EnsureEnvMapDefaultTextures()
    {
        // Mirrors EnsurePbrDefaultTextures()'s own lazy-at-first-draw pattern and
        // VulkanGraphicsBackend's defaultWhiteView_/defaultWhiteCubeView_ fallback role: neither
        // EnvironmentMapEffect::Texture nor ::EnvironmentMap is required to be set.
        if (envMapDefaultWhiteTexture_ == nullptr)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            envMapDefaultWhiteTexture_ = std::make_unique<WebGPUTextureBackend>(*this, white);
        }
        if (envMapDefaultWhiteCube_ == nullptr)
        {
            envMapDefaultWhiteCube_ = std::make_unique<WebGPUTextureCubeBackend>(*this, 1, false);
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineEnvMap3D(WGPUPrimitiveTopology topology,
                                                                          WGPUIndexFormat stripIndexFormat,
                                                                          bool depthTest, bool depthWrite,
                                                                          int depthFunc,
                                               bool blend, const BlendKeyParams& blendParams,
                                               int cullMode, bool wireframe,
                                               float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = envMapPipelines_.find(key); it != envMapPipelines_.end())
            return it->second;

        struct EnvMapVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(EnvMapVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(EnvMapVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(EnvMapVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(EnvMapVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = envMapShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create EnvMap3D pipeline");
        envMapPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::QueueEnvMapDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
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
        // EnvironmentMapEffect::Texture/EnvironmentMap are both genuinely optional (unlike every
        // other stride-32+ effect family's dispatch gate) -- null falls back to the 1x1 white
        // texture/cube at render time, matching VulkanGraphicsBackend's own default-white fallback.
        // ResolveSamplable()/the dynamic_cast below are both already null-safe.
        command.texture = ResolveSamplable(params.texture0);
        command.envMap = ResolveCubeSamplable(params.envMap);
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;

        const Matrix mvp = world * view * projection;
        FillEnvMapTransform(command.transformUniforms, mvp, world);
        FillEnvMapParams(command.envMapUniforms, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
    }

    void WebGPUGraphicsBackend::RenderEnvMapDraws(WGPURenderPassEncoder pass)
    {
        for (const EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;

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
                                                         command.addressV, command.maxAnisotropy);
            WGPUTextureView texView = command.texture != nullptr
                ? command.texture->View()
                : envMapDefaultWhiteTexture_->View();
            WGPUTextureView cubeView = command.envMap != nullptr
                ? command.envMap->CubeView()
                : envMapDefaultWhiteCube_->CubeView();
            std::array<WGPUBindGroupEntry, 3> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = texView;
            texEntries[2].binding = 2;
            texEntries[2].textureView = cubeView;
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU EnvMap3D Texture BindGroup");
            texBindDescriptor.layout = envMapTextureBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineEnvMap3D(
                                                                  command.topology,
                                                                  RequiredStripIndexFormat(command),
                                                                  command.depthTest,
                                                                  command.depthWrite, command.depthFunc,
                                                                  command.blend, command.blendParams,
                                                                  command.cullMode, command.wireframe,
                                                                  command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        envMapDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::DestroyInstancedResources()
    {
        for (auto& [key, pipe] : instancedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        instancedPipelines_.clear();
        if (instancedShader_ != nullptr) wgpuShaderModuleRelease(instancedShader_);
        instancedShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateInstancedResources()
    {
        DestroyInstancedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredPipelineLayout_ == nullptr)
            return;

        // WEBGPU-27: ported from VulkanGraphicsBackend's instanced3d.{vert,frag}.glsl. Only
        // position (binding 0, per-vertex) is consumed -- matches Vulkan's own shader, which reads
        // nothing else even when the per-vertex stride is larger (e.g. a VertexPositionColor
        // buffer's colour is simply unread, exactly like Vulkan's own comment on this). The
        // per-instance mat4 world transform (binding 1, WGPUVertexStepMode_Instance) replaces the
        // caller's own World matrix entirely: [0..15] of the Uniforms block below is View*Projection
        // (not a full MVP), matching FillInstancedPushConst()'s identical choice.
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
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineInstanced3D(
        std::size_t pvStride, std::size_t instVbStride, WGPUPrimitiveTopology topology,
        WGPUIndexFormat stripIndexFormat,
        bool depthTest, bool depthWrite, int depthFunc,
        bool blend, const BlendKeyParams& blendParams,
        int cullMode, bool wireframe, float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t salt = static_cast<std::uint64_t>(pvStride) * 1000003u +
                                    static_cast<std::uint64_t>(instVbStride);
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, salt, colorWriteMask_, sampleMask_);
        if (auto it = instancedPipelines_.find(key); it != instancedPipelines_.end())
            return it->second;

        // Binding 0: per-vertex, only position (location 0) is read regardless of the buffer's
        // real stride. Binding 1: per-instance (WGPUVertexStepMode_Instance), a mat4 world
        // transform as 4 Float32x4 columns at locations 4-7 (matching Vulkan's own location
        // numbering choice for cross-reference clarity, though WGSL/WebGPU impose no such gap
        // requirement).
        std::array<WGPUVertexAttribute, 1> vertexAttrs{};
        vertexAttrs[0].format = WGPUVertexFormat_Float32x3;
        vertexAttrs[0].offset = 0;
        vertexAttrs[0].shaderLocation = 0;

        std::array<WGPUVertexAttribute, 4> instanceAttrs{};
        instanceAttrs[0].format = WGPUVertexFormat_Float32x4;
        instanceAttrs[0].offset = 0;
        instanceAttrs[0].shaderLocation = 4;
        instanceAttrs[1].format = WGPUVertexFormat_Float32x4;
        instanceAttrs[1].offset = 16;
        instanceAttrs[1].shaderLocation = 5;
        instanceAttrs[2].format = WGPUVertexFormat_Float32x4;
        instanceAttrs[2].offset = 32;
        instanceAttrs[2].shaderLocation = 6;
        instanceAttrs[3].format = WGPUVertexFormat_Float32x4;
        instanceAttrs[3].offset = 48;
        instanceAttrs[3].shaderLocation = 7;

        std::array<WGPUVertexBufferLayout, 2> vertexBufferLayouts{};
        vertexBufferLayouts[0].arrayStride = static_cast<std::uint64_t>(pvStride);
        vertexBufferLayouts[0].stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayouts[0].attributeCount = vertexAttrs.size();
        vertexBufferLayouts[0].attributes = vertexAttrs.data();
        vertexBufferLayouts[1].arrayStride = static_cast<std::uint64_t>(instVbStride);
        vertexBufferLayouts[1].stepMode = WGPUVertexStepMode_Instance;
        vertexBufferLayouts[1].attributeCount = instanceAttrs.size();
        vertexBufferLayouts[1].attributes = instanceAttrs.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = instancedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Instanced3D Pipeline");
        pipeline.layout = coloredPipelineLayout_;
        pipeline.vertex.module = instancedShader_;
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
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Instanced3D pipeline");
        instancedPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::RenderInstancedDraws(WGPURenderPassEncoder pass)
    {
        for (const InstancedDrawCommand& command : instancedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() ||
                command.instanceCount == 0 || command.instVbData.empty())
                continue;

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
                                                                     command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        instancedDrawCommands_.clear();
    }

    WGPUSampler WebGPUGraphicsBackend::GetOrCreateSampler(int textureFilter, int addressU, int addressV)
    {
        const int index = SamplerCacheIndex(textureFilter, addressU, addressV);
        if (samplerCache_[index] != nullptr)
            return samplerCache_[index];
        WGPUSamplerDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU SpriteBatch Sampler");
        descriptor.addressModeU = ToAddressMode(addressU);
        descriptor.addressModeV = ToAddressMode(addressV);
        descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
        const WGPUFilterMode filter = textureFilter == 0 ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        descriptor.magFilter = filter;
        descriptor.minFilter = filter;
        descriptor.mipmapFilter = textureFilter == 0 ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        descriptor.lodMaxClamp = 32.0f;
        // A zero-initialized descriptor has maxAnisotropy=0, which wgpu-native rejects.
        // WebGPU's required default is 1 when anisotropic filtering is not requested.
        descriptor.maxAnisotropy = 1;
        samplerCache_[index] = wgpuDeviceCreateSampler(device_, &descriptor);
        if (samplerCache_[index] == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create sampler");
        return samplerCache_[index];
    }

    // WEBGPU-52: lazily creates the shader/bind-group-layout/pipeline-layout/pipeline/sampler
    // used by GenerateMipsForLayer() -- see this method's own header doc comment for the full
    // rationale and the deliberate FNA/cross-backend divergence this introduces. A minimal
    // full-screen-triangle vertex shader (the standard 3-vertex, no-vertex-buffer trick: each
    // vertex's clip position/UV is derived purely from @builtin(vertex_index)) plus a fragment
    // shader that samples the previous mip level through a real linear sampler -- this is what
    // makes the result a genuine filtered downsample, not a nearest-neighbor copy.
    void WebGPUGraphicsBackend::EnsureMipBlitPipeline()
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

        WGPUColorTargetState target{};
        target.format = WGPUTextureFormat_RGBA8Unorm;
        // REMED-GFX-077: internal mipmap-blit utility pipeline — not a game draw, so it is
        // intentionally unaffected by the game's BlendState.ColorWriteChannels/MultiSampleMask.
        target.writeMask = WGPUColorWriteMask_All; // internal mip-blit
        WGPUFragmentState fragment{};
        fragment.module = mipBlitShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // of this backend's own global sampleCount_ (unlike the swapchain/RenderTarget2D
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
    void WebGPUGraphicsBackend::GenerateMipsForLayer(WGPUTexture texture, int layer, int width, int height, int mipLevels)
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
            // every native pass/setViewport this backend issues, not a filtered subset).
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

    void WebGPUGraphicsBackend::GenerateMips2D(WGPUTexture texture, int width, int height, int mipLevels)
    {
        GenerateMipsForLayer(texture, 0, width, height, mipLevels);
    }

    void WebGPUGraphicsBackend::GenerateMipsCubeFace(WGPUTexture texture, int face, int size, int mipLevels)
    {
        GenerateMipsForLayer(texture, face, size, size, mipLevels);
    }

    WebGPUGraphicsBackend::LogicalViewport WebGPUGraphicsBackend::ComputeLogicalViewport() const
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

    void WebGPUGraphicsBackend::QueueSprite(const ITextureBackend& texture,
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
        // SdlGpuGraphicsBackend::QueueSprite()'s own currentRenderTarget_ branch (extended here to
        // the cube face too, since this backend bakes NDC CPU-side at enqueue and each target's
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
        const bool customViewport = viewportSet_ && viewportW_ > 0 && viewportH_ > 0 &&
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
        command.texture = &samplable;
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
        spriteCommands_.push_back(command);
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderSprites(WGPURenderPassEncoder pass,
                                              WGPUTextureFormat targetFormat,
                                              std::uint32_t targetSampleCount)
    {
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

        std::vector<SpriteVertex> vertices;
        vertices.reserve(spriteCommands_.size() * 6u);
        for (const SpriteCommand& command : spriteCommands_)
            vertices.insert(vertices.end(), command.vertices.begin(), command.vertices.end());
        wgpuQueueWriteBuffer(queue_, spriteVertexBuffer_, 0, vertices.data(), vertices.size() * sizeof(SpriteVertex));
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, spriteVertexBuffer_, 0, vertices.size() * sizeof(SpriteVertex));

        WGPURenderPipeline boundPipeline = nullptr;
        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
        {
            const SpriteCommand& command = spriteCommands_[i];
            if (command.blend.targetFormat != targetFormat ||
                command.blend.sampleCount != targetSampleCount)
            {
                throw std::runtime_error(
                    "CNA WebGPU: queued SpriteBatch target compatibility changed before replay");
            }

            // REMED-GFX-102: cache lookup happens per static-state transition; native pipeline
            // creation happens only on a cache miss for a previously unseen complete key. It is
            // never unconditional/per-sprite creation, and the dynamic BlendFactor RGBA value is
            // intentionally absent from GetOrCreateSpritePipeline's key.
            const WGPURenderPipeline pipeline = GetOrCreateSpritePipeline(command.blend);
            if (pipeline != boundPipeline)
            {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                boundPipeline = pipeline;
            }
            const WGPUColor blendConstant{
                command.blend.blendFactorR, command.blend.blendFactorG,
                command.blend.blendFactorB, command.blend.blendFactorA
            };
            wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);

            // REMED-GFX-072/116: this sprite's OWN captured Viewport. A sub-region sprite was
            // baked viewport-local at enqueue and lands at Viewport.X/Y from here; a target-
            // relative sprite's snapshot IS the whole target by construction, so it resets the
            // rasterizer after a preceding sub-region draw and can no longer inherit a viewport
            // that was set after it was queued. RenderSprites runs last in the pass, but the 3D
            // families now drive the same per-draw state, so neither can disturb the other.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
            std::array<WGPUBindGroupEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            entries[1].binding = 1;
            entries[1].textureView = command.texture->View();
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU SpriteBatch BindGroup");
            descriptor.layout = spriteBindGroupLayout_;
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.data();
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &descriptor);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, 1, static_cast<std::uint32_t>(i * 6u), 0);
            wgpuBindGroupRelease(bindGroup);
        }
    }

    void WebGPUGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
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

    void WebGPUGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
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
        // and the keyed SpriteBatch cache include them; only active attachment slot 0 applies
        // because WebGPU MRT remains a separate capability boundary.
        colorWriteMask_ = writeState.colorWriteChannels[0];
        sampleMask_ = writeState.multiSampleMask;
    }

    void WebGPUGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2.
        // XNA FillMode: Solid=0, WireFrame=1.
        cullMode_ = cullMode;
        fillModeWireframe_ = (fillMode == 1);
        scissorEnabled_ = scissorTestEnable;
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void WebGPUGraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                  int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(slotSamplers_.size()))
            return;
        slotSamplers_[static_cast<std::size_t>(slot)] = SlotSamplerState{filter, addressU, addressV, maxAnisotropy};
    }

    void WebGPUGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void WebGPUGraphicsBackend::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void WebGPUGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = x;
        scissorY_ = y;
        scissorW_ = std::max(0, w);
        scissorH_ = std::max(0, h);
    }

    void WebGPUGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
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
    WebGPUViewportSnapshot WebGPUGraphicsBackend::CaptureViewport() const noexcept
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

    void WebGPUGraphicsBackend::BeginPassViewport(WGPURenderPassEncoder pass, int targetWidth, int targetHeight)
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

    void WebGPUGraphicsBackend::ApplyDrawViewport(WGPURenderPassEncoder pass,
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
    WebGPUScissorSnapshot WebGPUGraphicsBackend::CaptureScissor() const noexcept
    {
        WebGPUScissorSnapshot snapshot;
        snapshot.enabled = scissorEnabled_;
        snapshot.x = scissorX_;
        snapshot.y = scissorY_;
        snapshot.width = scissorW_;
        snapshot.height = scissorH_;
        return snapshot;
    }

    void WebGPUGraphicsBackend::BeginPassScissor(WGPURenderPassEncoder pass, int targetWidth,
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

    void WebGPUGraphicsBackend::ApplyDrawScissor(WGPURenderPassEncoder pass,
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
            // active target rather than rejecting it, which is also what this backend has always
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

    WGPUSampler WebGPUGraphicsBackend::GetOrCreateSlotSampler(int filter, int addressU, int addressV,
                                                               int maxAnisotropy)
    {
        const int clampedAniso = std::clamp(maxAnisotropy, 1, 16);
        const std::uint32_t key = (static_cast<std::uint32_t>(filter) & 0xFFu)
                                 | ((static_cast<std::uint32_t>(addressU) & 0xFFu) << 8)
                                 | ((static_cast<std::uint32_t>(addressV) & 0xFFu) << 16)
                                 | ((static_cast<std::uint32_t>(clampedAniso) & 0xFFu) << 24);
        if (auto it = slotSamplerCache_.find(key); it != slotSamplerCache_.end())
            return it->second;

        WGPUSamplerDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU 3D SamplerState Sampler");
        FillWGPUSamplerDescriptor(descriptor, filter, addressU, addressV, clampedAniso);
        WGPUSampler sampler = wgpuDeviceCreateSampler(device_, &descriptor);
        if (sampler == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create per-slot SamplerState sampler");
        slotSamplerCache_[key] = sampler;
        return sampler;
    }

    void WebGPUGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearColor_ = WGPUColor{r, g, b, a};
        clearColorPending_ = true;
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }
    void WebGPUGraphicsBackend::ClearDepth(float depth) { clearDepth_ = depth; clearDepthPending_ = true; framePending_ = true; }
    void WebGPUGraphicsBackend::ClearStencil(int stencil) { clearStencil_ = static_cast<std::uint32_t>(stencil); clearStencilPending_ = true; framePending_ = true; }
    void WebGPUGraphicsBackend::ClearDepthAndStencil(float depth, int stencil) { ClearDepth(depth); ClearStencil(stencil); }
    void WebGPUGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil) { Clear(r, g, b, a); ClearStencil(stencil); }
    void WebGPUGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    { Clear(r, g, b, a); ClearDepth(depth); ClearStencil(stencil); }

    bool WebGPUGraphicsBackend::EnsureFrameRendered()
    {
        if (!hasAcquiredTexture_)
        {
            ConfigureSurface(false);
            if (!surfaceConfigured_)
            {
                spriteCommands_.clear();
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
                spriteCommands_.clear();
                return false;
            }

            acquiredTexture_ = surfaceTexture.texture;
            hasAcquiredTexture_ = true;
            framePending_ = true;
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
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = useMsaa ? msaaColorView_ : backBuffer;
        colorAttachment.resolveTarget = useMsaa ? backBuffer : nullptr;
        colorAttachment.loadOp = clearColorPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor_;

        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = depthView_;
        depthAttachment.depthLoadOp = clearDepthPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store;
        depthAttachment.depthClearValue = clearDepth_;
        depthAttachment.depthReadOnly = false;
        depthAttachment.stencilLoadOp = clearStencilPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
        depthAttachment.stencilClearValue = clearStencil_;
        depthAttachment.stencilReadOnly = false;

        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.label = StringView("CNA WebGPU Main RenderPass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        passDescriptor.depthStencilAttachment = depthView_ != nullptr ? &depthAttachment : nullptr;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        ++renderPassCount_;

        // WEBGPU-29/30: the blend constant and the reference stencil are genuinely dynamic
        // wgpu-native render-pass state (unlike blend/cull/wireframe/depthBias, which are baked
        // per-pipeline above) and are still applied once here from whatever is currently stored.
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
        BeginPassViewport(pass, physicalWidth_, physicalHeight_);
        BeginPassScissor(pass, physicalWidth_, physicalHeight_);
        const WGPUColor blendConstant{blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_};
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
        wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(referenceStencil_));

        // 3D draws first, 2D SpriteBatch/UI on top -- matches typical XNA game draw order
        // (World.Draw() then a HUD SpriteBatch pass), both collapsed into this one deferred pass.
        RenderColoredDraws(pass);
        RenderTexturedDraws(pass);
        RenderLitTexturedDraws(pass);
        RenderAlphaTestDraws(pass);
        RenderDualTextureDraws(pass);
        RenderEnvMapDraws(pass);
        RenderInstancedDraws(pass);
        RenderPbrDraws(pass);
        RenderSkinnedDraws(pass);
        RenderSkinnedPbrDraws(pass);
        RenderSprites(pass, surfaceFormat_,
                      static_cast<std::uint32_t>(std::max(1, sampleCount_)));
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

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

        spriteCommands_.clear();
        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        framePending_ = false;
        return true;
    }

    void WebGPUGraphicsBackend::RenderPendingDrawsToRenderTarget(WebGPURenderTargetBackend* target)
    {
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTarget Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        // WEBGPU-58: ColorAttachmentView() is the multisampled texture's view while this target
        // is multisampled (mirroring the owner's global sampleCount_ -- see
        // WebGPURenderTargetBackend's own class comment), with target->View()'s single-sample
        // colorView_ as the resolveTarget wgpu-native resolves into automatically; both are the
        // same texture/view (no resolve) when this target is not multisampled, identical to
        // before MSAA existed.
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = target->ColorAttachmentView();
        colorAttachment.resolveTarget = target->ResolveTargetView();
        // RenderTargetUsage.DiscardContents (XNA's own default) means content is not guaranteed
        // to survive between separate SetRenderTarget bind cycles -- mirrors
        // VulkanRenderTargetBackend::GetRenderPass()'s own discardContents=!preserveContents_
        // choice, applied on every bind cycle (not just this target's first-ever use). An
        // explicit Clear()/ClearColorAndDepth()/etc. call always wins regardless of
        // preserveContents (matches "Clear() clears whichever target is currently active" XNA
        // semantics). Like Vulkan, the clear VALUE used when no explicit Clear() happened is
        // whatever this backend's global clearColor_/clearDepth_/clearStencil_ currently holds
        // (potentially stale from the last explicit Clear() anywhere, not a fresh per-target
        // default) -- a real, documented imprecision this shares with the Vulkan reference
        // backend rather than a new one introduced here.
        const bool discard = !target->PreserveContents();
        const bool doClearColor = clearColorPending_ || discard;
        colorAttachment.loadOp = doClearColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor_;

        // WEBGPU-53/54/8/9: this target's own real Depth24PlusStencil8 attachment (always
        // present -- see WebGPURenderTargetBackend's constructor comment) -- so, unlike
        // EnsureFrameRendered()'s backbuffer pass (which defensively checks depthView_ != nullptr
        // for the physicalWidth_/physicalHeight_ <= 0 edge case), this is unconditional.
        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = target->DepthView();
        const bool doClearDepth = clearDepthPending_ || discard;
        depthAttachment.depthLoadOp = doClearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store;
        depthAttachment.depthClearValue = clearDepth_;
        depthAttachment.depthReadOnly = false;
        const bool doClearStencil = clearStencilPending_ || discard;
        depthAttachment.stencilLoadOp = doClearStencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
        depthAttachment.stencilClearValue = clearStencil_;
        depthAttachment.stencilReadOnly = false;

        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.label = StringView("CNA WebGPU RenderTarget RenderPass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        passDescriptor.depthStencilAttachment = &depthAttachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        ++renderPassCount_;

        // Blend constant and reference stencil: same "whatever's currently stored" model as
        // EnsureFrameRendered()'s identical block -- see that function's own comment. The viewport
        // (REMED-GFX-116) and the scissor (REMED-GFX-146) are per-draw captured state instead.
        BeginPassViewport(pass, target->GetWidth(), target->GetHeight());
        BeginPassScissor(pass, target->GetWidth(), target->GetHeight());
        const WGPUColor blendConstant{blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_};
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
        wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(referenceStencil_));

        RenderColoredDraws(pass);
        RenderTexturedDraws(pass);
        RenderLitTexturedDraws(pass);
        RenderAlphaTestDraws(pass);
        RenderDualTextureDraws(pass);
        RenderEnvMapDraws(pass);
        RenderInstancedDraws(pass);
        RenderPbrDraws(pass);
        RenderSkinnedDraws(pass);
        RenderSkinnedPbrDraws(pass);
        RenderSprites(pass, target->ColorFormat(),
                      static_cast<std::uint32_t>(std::max(1, target->GetMultiSampleCount())));
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

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

        // RenderSprites() does not clear spriteCommands_ itself (matching EnsureFrameRendered()'s
        // own identical tail); every other Render*Draws() call above already clears its own
        // *DrawCommands_ vector at its own tail.
        spriteCommands_.clear();
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
    // IGraphicsBackend::CreateRenderTargetCube had no preserveContents parameter to thread a
    // RenderTargetCube's real usage through. It has one now, so the choice below is the same
    // `clearPending || !preserveContents` expression the 2D sibling above already used.
    void WebGPUGraphicsBackend::RenderPendingDrawsToRenderTargetCubeFace(WebGPURenderTargetCubeBackend* target, int face)
    {
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU RenderTargetCube Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);

        // REMED-GFX-136: `discard` is read from the target THIS pass is being created for, at the
        // moment the pass descriptor is built, and preserveContents_ is immutable after
        // construction -- so an A -> B -> A sequence can never apply target B's policy to target A,
        // whichever order the flushes happen in.
        const bool discard = !target->PreserveContents();
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = target->ColorAttachmentView(face);
        colorAttachment.resolveTarget = nullptr;
        const bool doClearColor = clearColorPending_ || discard;
        colorAttachment.loadOp = doClearColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor_;

        // REMED-GFX-142: depth/stencil now follows the same rule as the 2D sibling above, which is
        // also FNA3D's own (its SDL_GPU driver loads both aspects unless a clear is pending, and
        // its GL and D3D11 drivers preserve them because an FBO attachment and a DSV simply
        // persist). This used to clear unconditionally on the reasoning that RenderTargetUsage is
        // a colour contract and that one depth texture shared by six faces would "hand face A
        // whatever depth face B last wrote" -- but FNA3D documents `preserveTargetContents` as
        // storing the "color/depth/stencil" contents, and FNA's RenderTargetCube allocates exactly
        // ONE glDepthStencilBuffer for the whole cube, so face A seeing face B's depth IS the
        // reference behaviour rather than a hazard to design around.
        const bool doClearDepthCube = clearDepthPending_ || discard;
        const bool doClearStencilCube = clearStencilPending_ || discard;
        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = target->DepthView();
        depthAttachment.depthLoadOp = doClearDepthCube ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store;
        depthAttachment.depthClearValue = clearDepth_;
        depthAttachment.depthReadOnly = false;
        depthAttachment.stencilLoadOp = doClearStencilCube ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
        depthAttachment.stencilClearValue = clearStencil_;
        depthAttachment.stencilReadOnly = false;

        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.label = StringView("CNA WebGPU RenderTargetCube RenderPass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        passDescriptor.depthStencilAttachment = &depthAttachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        ++renderPassCount_;

        // Blend constant and reference stencil: same "whatever's currently stored" model as
        // RenderPendingDrawsToRenderTarget()'s identical block. The viewport (REMED-GFX-116) and
        // the scissor (REMED-GFX-146) are per-draw captured state instead.
        BeginPassViewport(pass, target->GetSize(), target->GetSize());
        BeginPassScissor(pass, target->GetSize(), target->GetSize());
        const WGPUColor blendConstant{blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_};
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendConstant);
        wgpuRenderPassEncoderSetStencilReference(pass, static_cast<std::uint32_t>(referenceStencil_));

        RenderColoredDraws(pass);
        RenderTexturedDraws(pass);
        RenderLitTexturedDraws(pass);
        RenderAlphaTestDraws(pass);
        RenderDualTextureDraws(pass);
        RenderEnvMapDraws(pass);
        RenderInstancedDraws(pass);
        RenderPbrDraws(pass);
        RenderSkinnedDraws(pass);
        RenderSkinnedPbrDraws(pass);
        RenderSprites(pass, target->ColorFormat(), 1);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

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

        spriteCommands_.clear();
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
    void WebGPUGraphicsBackend::FlushCurrentRenderTarget()
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

    void WebGPUGraphicsBackend::Present()
    {
        if (!EnsureFrameRendered())
            return;
        wgpuSurfacePresent(surface_);
        wgpuTextureRelease(acquiredTexture_);
        acquiredTexture_ = nullptr;
        hasAcquiredTexture_ = false;
    }

    void WebGPUGraphicsBackend::CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture)
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

    void WebGPUGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0)
            return;

        // Render whatever Clear()/sprite work has been queued so far this logical frame before
        // reading it back, so a Clear()+GetBackBufferData() pair observes its own frame's result
        // without needing a real Present() in between (matches Vulkan/Bgfx's on-demand submit).
        EnsureFrameRendered();

        if (!readbackValid_ || readbackBuffer_ == nullptr)
        {
            std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            return;
        }

        const std::uint64_t mapSize = readbackBufferCapacity_;
        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
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
    }

    void WebGPUGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void WebGPUGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void WebGPUGraphicsBackend::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA WebGPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void WebGPUGraphicsBackend::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        if (swapInterval_ != interval)
        {
            swapInterval_ = interval;
            ConfigureSurface(true);
        }
    }

    bool WebGPUGraphicsBackend::Supports4xMsaa()
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
        // this backend verifies it empirically against the concrete adapter/device/surfaceFormat_
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
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnPopErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(device_, callback);
        WaitForCompletion(instance_, state.completed, "MSAA support probe");

        msaa4xSupported_ = (probe != nullptr && state.ok) ? 1 : 0;
        return msaa4xSupported_ != 0;
    }

    int WebGPUGraphicsBackend::PickSampleCount(int requestedMultiSampleCount)
    {
        if (requestedMultiSampleCount < 2)
            return 1;
        return Supports4xMsaa() ? 4 : 1;
    }

    void WebGPUGraphicsBackend::ClearAllPipelineCaches()
    {
        // WEBGPU-58 finding: merely releasing the cached WGPURenderPipeline objects and
        // recreating them from the EXISTING, long-lived coloredShader_/coloredBindGroupLayout_/
        // coloredPipelineLayout_ (and the equivalent members for every other 3D shader family) is
        // NOT enough -- empirically verified (via a standalone probe reusing this backend's own
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

    int WebGPUGraphicsBackend::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const int newSampleCount = PickSampleCount(requestedMultiSampleCount);
        if (newSampleCount == sampleCount_)
            return GetMultiSampleCount();

        sampleCount_ = newSampleCount;

        // Every previously-created 3D/sprite pipeline baked the OLD sampleCount_ into its own
        // WGPUMultisampleState.count -- release them all so the next Queue*Draw()/RenderSprites()
        // lazily rebuilds with the new value (mirrors
        // VulkanGraphicsBackend::ApplyMultiSampleCount()'s identical "invalidate every pipeline
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
            SDL_Log("[WebGPU] MultiSampleCount reset to %dx", sampleCount_);
        else
            SDL_Log("[WebGPU] MultiSampleCount reset to disabled (1x)");

        return GetMultiSampleCount();
    }

    int WebGPUGraphicsBackend::GetMultiSampleCount() const
    {
        return sampleCount_ > 1 ? sampleCount_ : 0;
    }

    bool WebGPUGraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        logicalX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool WebGPUGraphicsBackend::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = viewport.x + logicalX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logicalY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureBackend> WebGPUGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<WebGPUTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> WebGPUGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<WebGPUSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> WebGPUGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // WEBGPU-53/54: mip-chain regeneration (mipMap=true) is not implemented on this backend
        // yet -- throwing here (matching this file's own ThrowUnsupported3DDraw() precedent for
        // "genuinely unsupported, not a silent degrade" cases, and CHECKLIST.md's Draco-import
        // convention of a clear error over quietly under-delivering) is deliberately preferred
        // over silently creating a single-level target while RenderTarget2D::RenderTarget2D()'s
        // own CalculateMipLevels() has already told the XNA layer to expect a full mip chain --
        // that mismatch would let Texture2D::SetData/GetData(level>0, ...) silently write into or
        // read from nothing.
        if (mipMap)
            throw std::runtime_error("CNA WebGPU: RenderTarget2D mip-chain regeneration "
                                     "(mipMap=true) is not implemented on this backend yet -- see "
                                     "plan_webgpu.md WEBGPU-53/54");
        // WEBGPU-58: the per-instance requested multiSampleCount argument is intentionally NOT
        // read here -- WebGPURenderTargetBackend's own constructor unconditionally mirrors this
        // backend's CURRENT global sampleCount_ instead (see that class's own top-of-class doc
        // comment for exactly why a per-instance opt-out is unsafe given this backend's single
        // shared pipeline sample count). RenderTarget2D::RenderTarget2D() reads the real applied
        // value back via GetMultiSampleCount() into its own MultiSampleCount property, matching
        // FNA3D_GetMaxMultiSampleCount's real-clamped-value contract -- 0 whenever the backend has
        // no MSAA active, exactly as before this task.
        (void) multiSampleCount;
        return std::make_unique<WebGPURenderTargetBackend>(*this, w, h, depthFormat, preserveContents);
    }

    void WebGPUGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        auto* newTarget = rt != nullptr ? static_cast<WebGPURenderTargetBackend*>(rt) : nullptr;
        // WEBGPU-114: a currently-bound RenderTargetCube face must NOT be treated as "no change"
        // just because newTarget (a RenderTarget2D pointer, possibly nullptr) happens to equal
        // currentRenderTarget_ (also nullptr while a cube face is bound, since the two are
        // mutually exclusive -- see currentRenderTargetCubeFace_'s own doc comment) -- this is the
        // path SetRenderTargetCubeFace(nullptr, face)'s IGraphicsBackend default takes to restore
        // the backbuffer, and it must genuinely flush the cube face's own pending draws.
        if (newTarget == currentRenderTarget_ && currentRenderTargetCubeFace_ == nullptr)
            return;

        // WEBGPU-53/54: this backend renders every Clear()/3D-draw/SpriteBatch command queued
        // during a logical frame into exactly ONE deferred render pass, lazily, the first time
        // something actually needs the frame to be real (EnsureFrameRendered(), called from
        // Present()/ReadBackbuffer()). Supporting RenderTarget2D means a game can bind an RT,
        // draw, switch back to the backbuffer, draw more -- all inside one logical frame -- and
        // those two sets of draws must NOT collapse into a single pass against whichever target
        // happens to be active at final-flush time. Two designs were considered: (a) eagerly
        // flush/render whatever is currently queued for the CURRENT target the instant
        // SetRenderTarget2D switches to a DIFFERENT target, closing out that render pass
        // immediately and starting fresh accumulation for the new target; or (b) tag every queued
        // draw command (all ~10 of this backend's Queue*Draw families) with which target it
        // belongs to and replay them grouped-by-target at final-flush time, closer to
        // VulkanGraphicsBackend's own deferred/replay model (see its RecordCommandBuffer()).
        // (a) was chosen: it needs one new function (RenderPendingDrawsToRenderTarget) and zero
        // changes to any existing Queue*Draw()/*DrawCommand struct, whereas (b) would add a
        // target-tag field and per-target grouping logic to every one of those ~10 families -- a
        // much larger change for a backend that, unlike Vulkan, has no pre-existing per-draw
        // deferred/replay infrastructure to extend. WEBGPU-114 extends the same choice to a
        // RenderTargetCube face, generalised into FlushCurrentRenderTarget() so it applies
        // regardless of which of the 3 kinds of target ({backbuffer, RenderTarget2D, cube face})
        // is currently bound.
        FlushCurrentRenderTarget();
        currentRenderTargetCubeFace_ = nullptr;
        currentRenderTargetCubeFaceIndex_ = -1;

        if (newTarget != nullptr)
            newTarget->BindAsRenderTarget();
        else
            currentRenderTarget_ = nullptr;
    }

    void WebGPUGraphicsBackend::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count > 1)
            throw std::runtime_error(
                "CNA WebGPU: multiple simultaneous render targets are not implemented "
                "on this backend yet.");
        if (renderTargets[0].IsRenderTargetCubeFace())
            SetRenderTargetCubeFace(
                renderTargets[0].GetRenderTargetCube(),
                renderTargets[0].GetCubeFace());
        else
            SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
    }

    std::unique_ptr<ITextureCubeBackend> WebGPUGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<WebGPUTextureCubeBackend>(*this, size, mipMap);
    }

    // WEBGPU-114: this backend's first real RenderTargetCube support -- see
    // WebGPURenderTargetCubeBackend's own doc comment for exactly what is/isn't implemented
    // (no mip regen, no MSAA -- both deliberate, documented scope cuts).
    std::unique_ptr<IRenderTargetCubeBackend> WebGPUGraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        (void) multiSampleCount; // see WebGPURenderTargetCubeBackend's own doc comment: ignored.
        // REMED-GFX-136: preserveContents is the public RenderTargetUsage, reaching a cube target
        // for the first time -- see RenderPendingDrawsToRenderTargetCubeFace().
        return std::make_unique<WebGPURenderTargetCubeBackend>(*this, size, depthFormat,
                                                              preserveContents, mipMap);
    }

    std::unique_ptr<ITexture3DBackend> WebGPUGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<WebGPUTexture3DBackend>(*this, w, h, depth, mipMap);
    }

    std::unique_ptr<IVertexBufferBackend> WebGPUGraphicsBackend::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<WebGPUVertexBufferBackend>(*this, vertexCapacity);
    }

    std::unique_ptr<IIndexBufferBackend> WebGPUGraphicsBackend::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferBackend>(*this, indexCapacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> WebGPUGraphicsBackend::CreateIndexBuffer32(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferBackend>(*this, indexCapacity, true);
    }

    WGPUPrimitiveTopology WebGPUGraphicsBackend::ToTopology(PrimitiveType primitive) const
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

    int WebGPUGraphicsBackend::PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const
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

    int WebGPUGraphicsBackend::PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const
    {
        return PrimitiveVertexCount(primitive, primitiveCount);
    }

    [[noreturn]] void WebGPUGraphicsBackend::ThrowUnsupported3DDraw(const char* method)
    {
        throw std::runtime_error(std::string("CNA WebGPU: ") + method +
                                 " is not implemented in the initial backend. Clear/present, Texture2D, "
                                 "SpriteBatch and buffer upload are implemented; see plan_webgpu.md Phase 58+ for 3D parity.");
    }

    void WebGPUGraphicsBackend::QueueColoredDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams* params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
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
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                        const Matrix& world, const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                               const IIndexBufferBackend& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        // Matches VulkanGraphicsBackend's own dispatch precedence: alpha test wins over
        // dual-texture/env-map/skinned/lit-textured; dual-texture wins over env-map/skinned/
        // lit-textured (an AlphaTestEffect or DualTextureEffect draw on a
        // VertexPositionNormalTexture buffer never reaches lit_textured3d -- the normal is simply
        // unread in both cases); env-map wins over lit-textured for the same stride-32 buffer
        // shape (EnvironmentMapEffect's own reflection shader takes over the normal/UV attributes
        // that lit_textured3d.wgsl would otherwise consume for Blinn-Phong lighting).
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
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
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32 &&
            params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // SkinnedPbrEffect (PBR + skinning combo, stride 68). Checked BEFORE the unskinned-PBR
        // branch below, matching EasyGLGraphicsBackend::SelectProgram()'s own priority order
        // (pbr&&skinned combo first, then pbr-only, then skinned-only).
        if (!needsAlphaTest && !needsDualTexture && params.pbr && params.skinned && stride == 68 &&
            params.texture0 != nullptr)
        {
            QueueSkinnedPbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // Unskinned PbrEffect (stride 48, VertexPositionNormalTangentTexture). Gated on
        // !params.skinned directly (rather than needsUnsupportedEffect, which already excludes
        // skinned draws via its own OR-condition).
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned && stride == 48 &&
            params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // SkinnedEffect (stride 52, or 56 with VertexColorEnabled). Gated on !params.pbr directly
        // (rather than needsUnsupportedEffect, which already excludes skinned draws via its own
        // OR-condition) so the SkinnedPbrEffect branch above keeps first priority, matching
        // EasyGLGraphicsBackend::SelectProgram()'s own ordering.
        if (!needsAlphaTest && !needsDualTexture && params.skinned && !params.pbr &&
            (stride == 52 || stride == 56) && params.texture0 != nullptr)
        {
            QueueSkinnedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // Remaining unsupported effect (skinned on a non-skinned stride) or an unmatched
        // stride/texture combination -- fall back exactly like IGraphicsBackend's own default
        // implementation did before this override existed. This will itself throw for anything
        // other than a stride-16 buffer (DrawColoredPrimitives' own requirement), matching the
        // pre-existing "unsupported, fail loudly" behaviour.
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
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
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32 &&
            params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // See DrawPrimitivesEx()'s identical branch for the SkinnedPbrEffect/PbrEffect/SkinnedEffect
        // priority ordering rationale.
        if (!needsAlphaTest && !needsDualTexture && params.pbr && params.skinned && stride == 68 &&
            params.texture0 != nullptr)
        {
            QueueSkinnedPbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned && stride == 48 &&
            params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsAlphaTest && !needsDualTexture && params.skinned && !params.pbr &&
            (stride == 52 || stride == 56) && params.texture0 != nullptr)
        {
            QueueSkinnedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawInstancedPrimitivesEx(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
        if (params.instanceVb == nullptr)
        {
            // No per-instance VB -- fall back to a single-instance indexed draw, matching
            // VulkanGraphicsBackend::DrawInstancedPrimitivesEx's own identical fallback.
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(ib);
        const auto& webgpuInstVb = static_cast<const WebGPUVertexBufferBackend&>(*params.instanceVb);

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
        command.instanceCount = static_cast<std::uint32_t>(std::max(1, instanceCount));

        // Copies the FULL vertex/instance buffers (matches VulkanGraphicsBackend::
        // DrawInstancedPrimitivesEx's own identical "no vertexStart applied" simplification --
        // WEBGPU-70's documented, pre-existing baseVertex/vertexStart gap, not something this task
        // introduces).
        const auto& vbShadow = webgpuVb.ShadowData();
        const std::size_t vertexByteCount = static_cast<std::size_t>(webgpuVb.GetVertexCount()) * command.pvStride;
        if (vertexByteCount <= vbShadow.size())
            command.vertexData.assign(vbShadow.begin(), vbShadow.begin() + static_cast<std::ptrdiff_t>(vertexByteCount));
        command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount());

        const auto& instShadow = webgpuInstVb.ShadowData();
        const std::size_t instByteCount = static_cast<std::size_t>(command.instanceCount) * command.instVbStride;
        if (instByteCount <= instShadow.size())
            command.instVbData.assign(instShadow.begin(), instShadow.begin() + static_cast<std::ptrdiff_t>(instByteCount));

        command.indexed = true;
        command.index32 = webgpuIb.IsThirtyTwoBit();
        command.indexData = webgpuIb.ShadowData();
        command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
        command.firstIndex = static_cast<std::uint32_t>(params.startIndex);
        command.baseVertex = params.baseVertex;

        // [0..15]=View*Projection (not a full MVP -- world comes from the per-instance stream),
        // [16..31]=diffuseColor+the same unused-here tail fields as colored3d.wgsl. FillExtUniforms()
        // is reused verbatim: it only cares that its first argument is SOME matrix to dump
        // column-major into [0..15], not specifically a WVP.
        const Matrix vp = view * projection;
        FillExtUniforms(command.uniforms, vp, params);

        instancedDrawCommands_.push_back(std::move(command));
    }

    void WebGPUGraphicsBackend::RenderColoredDraws(WGPURenderPassEncoder pass)
    {
        for (const ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;

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
                                                                   command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        coloredDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::RenderTexturedDraws(WGPURenderPassEncoder pass)
    {
        for (const TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
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
                                                       command.depthBias, command.slopeScaleDepthBias)
                : GetOrCreatePipelineTextured3D(
                                                command.topology,
                                                RequiredStripIndexFormat(command),
                                                command.depthTest,
                                                command.depthWrite, command.depthFunc,
                                                command.blend, command.blendParams,
                                                command.cullMode, command.wireframe,
                                                command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        texturedDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::RenderLitTexturedDraws(WGPURenderPassEncoder pass)
    {
        for (const LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
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
                                                             command.depthBias, command.slopeScaleDepthBias)
                : GetOrCreatePipelineLitTextured3D(
                                                    command.topology,
                                                    RequiredStripIndexFormat(command),
                                                    command.depthTest,
                                                    command.depthWrite, command.depthFunc,
                                                    command.blend, command.blendParams,
                                                    command.cullMode, command.wireframe,
                                                    command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        litTexturedDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueLitTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 32)
            throw std::invalid_argument("CNA WebGPU: QueueLitTexturedDraw requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueLitTexturedDraw requires a bound texture0");

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
        command.texture = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        // Task 1105: XNA's real BasicEffect.PreferPerPixelLighting default is false (per-vertex),
        // matching every other backend's own dispatch condition for this flag.
        command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderAlphaTestDraws(WGPURenderPassEncoder pass)
    {
        for (const AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
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
                                                                     command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        alphaTestDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueAlphaTestDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24 && stride != 32)
            throw std::invalid_argument("CNA WebGPU: QueueAlphaTestDraw requires a stride-20, "
                                        "-24, or -32 vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueAlphaTestDraw requires a bound texture0");

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
        command.texture = ResolveSamplable(params.texture0);
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
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderDualTextureDraws(WGPURenderPassEncoder pass)
    {
        for (const DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() ||
                command.texture0 == nullptr || command.texture1 == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 3> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture0->View();
            texEntries[2].binding = 2;
            texEntries[2].textureView = command.texture1->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU DualTexture3D Texture BindGroup");
            texBindDescriptor.layout = dualTextureBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineDualTexture3D(command.hasVertexColor ? 24 : 20,
                                                                       command.topology,
                                                                       RequiredStripIndexFormat(command),
                                                                       command.depthTest,
                                                                       command.depthWrite, command.depthFunc,
                                                                       command.blend, command.blendParams,
                                                                       command.cullMode, command.wireframe,
                                                                       command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        dualTextureDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueDualTextureDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
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
        command.texture0 = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.texture1 = ResolveSamplable(params.texture1);
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::QueueTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24)
            throw std::invalid_argument("CNA WebGPU: QueueTexturedDraw requires a stride-20 "
                                        "(VertexPositionTexture) or stride-24 "
                                        "(VertexPositionColorTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueTexturedDraw requires a bound texture0");

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
        command.texture = ResolveSamplable(params.texture0);
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
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::DestroyPbrResources()
    {
        for (auto& [key, pipe] : pbrPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        pbrPipelines_.clear();
        if (pbrPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(pbrPipelineLayout_);
        if (pbrBindGroupLayout1_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout1_);
        if (pbrBindGroupLayout0_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout0_);
        if (pbrShader_ != nullptr) wgpuShaderModuleRelease(pbrShader_);
        pbrPipelineLayout_ = nullptr;
        pbrBindGroupLayout1_ = nullptr;
        pbrBindGroupLayout0_ = nullptr;
        pbrShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreatePbrResources()
    {
        DestroyPbrResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Ported from EasyGLGraphicsBackend::EnsurePbrProgram()'s GLSL PbrLight() helper
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
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
};
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
    output.bitangentSign = input.tangent.w;
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    return output;
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, roughness: f32, metallic: f32) -> vec3f {
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
    let f = f0 + (vec3f(1.0) - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, input.uv);
    let albedo = baseColorSample.rgb * u.diffuseColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a;

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    let sampledNormal = textureSample(normalTex, texSampler, input.uv).rgb * 2.0 - 1.0;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, input.uv);
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    let f0 = mix(vec3f(0.04), albedo, metallic);

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
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, roughness, metallic);

    let occlusion = textureSample(occlusionTex, texSampler, input.uv).r;
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissive = lp.emissiveColor.xyz * textureSample(emissiveTex, texSampler, input.uv).rgb;

    return vec4f(ambient + lo + emissive, alpha);
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Pbr3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        pbrShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (pbrShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D shader");

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
        uboEntries[2].buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor uboLayoutDescriptor{};
        uboLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D BindGroupLayout0");
        uboLayoutDescriptor.entryCount = uboEntries.size();
        uboLayoutDescriptor.entries = uboEntries.data();
        pbrBindGroupLayout0_ = wgpuDeviceCreateBindGroupLayout(device_, &uboLayoutDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 6> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].visibility = WGPUShaderStage_Fragment;
        texEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        for (std::uint32_t i = 1; i <= 5; ++i)
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelinePbr3D(WGPUPrimitiveTopology topology,
                                                                         WGPUIndexFormat stripIndexFormat,
                                                                         bool depthTest, bool depthWrite,
                                                                         int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = pbrPipelines_.find(key); it != pbrPipelines_.end())
            return it->second;

        // Matches VertexPositionNormalTangentTexture's 48-byte layout: Position(12) + Normal(12)
        // + Tangent(16, xyz + bitangent-handedness in w) + TextureCoordinate(8).
        struct PbrVertex { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; };
        static_assert(sizeof(PbrVertex) == 48, "PbrVertex must be 48 bytes");
        std::array<WGPUVertexAttribute, 4> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(PbrVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(PbrVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x4;
        attributes[2].offset = offsetof(PbrVertex, tx);
        attributes[2].shaderLocation = 2;
        attributes[3].format = WGPUVertexFormat_Float32x2;
        attributes[3].offset = offsetof(PbrVertex, u);
        attributes[3].shaderLocation = 3;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(PbrVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = pbrShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Pbr3D Pipeline");
        pipeline.layout = pbrPipelineLayout_;
        pipeline.vertex.module = pbrShader_;
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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D pipeline");
        pbrPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::EnsurePbrDefaultTextures()
    {
        // Mirrors EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()/
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
            pbrDefaultWhiteTexture_ = std::make_unique<WebGPUTextureBackend>(*this, white);
        }
        if (pbrDefaultFlatNormalTexture_ == nullptr)
        {
            ImageData flatNormal{};
            flatNormal.width = 1;
            flatNormal.height = 1;
            flatNormal.mipLevels = 1;
            flatNormal.pixels = {128, 128, 255, 255};
            pbrDefaultFlatNormalTexture_ = std::make_unique<WebGPUTextureBackend>(*this, flatNormal);
        }
    }

    void WebGPUGraphicsBackend::QueuePbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount,
                                              const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 48)
            throw std::invalid_argument("CNA WebGPU: QueuePbrDraw requires a stride-48 "
                                        "(VertexPositionNormalTangentTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueuePbrDraw requires a bound texture0");

        EnsurePbrDefaultTextures();

        PbrDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 48u;
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
        command.baseColorTexture = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.normalMap = params.pbrNormalMap != nullptr
            ? ResolveSamplable(params.pbrNormalMap)
            : pbrDefaultFlatNormalTexture_.get();
        command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr
            ? ResolveSamplable(params.pbrMetallicRoughnessMap)
            : pbrDefaultWhiteTexture_.get();
        command.emissiveMap = params.pbrEmissiveMap != nullptr
            ? ResolveSamplable(params.pbrEmissiveMap)
            : pbrDefaultWhiteTexture_.get();
        command.occlusionMap = params.pbrOcclusionMap != nullptr
            ? ResolveSamplable(params.pbrOcclusionMap)
            : pbrDefaultWhiteTexture_.get();

        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillPbrFactors(command.pbrFactors, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderPbrDraws(WGPURenderPassEncoder pass)
    {
        for (const PbrDrawCommand& command : pbrDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.baseColorTexture == nullptr ||
                command.normalMap == nullptr || command.metallicRoughnessMap == nullptr ||
                command.emissiveMap == nullptr || command.occlusionMap == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 6> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.baseColorTexture->View();
            texEntries[2].binding = 2;
            texEntries[2].textureView = command.normalMap->View();
            texEntries[3].binding = 3;
            texEntries[3].textureView = command.metallicRoughnessMap->View();
            texEntries[4].binding = 4;
            texEntries[4].textureView = command.emissiveMap->View();
            texEntries[5].binding = 5;
            texEntries[5].textureView = command.occlusionMap->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU Pbr3D Texture BindGroup");
            texBindDescriptor.layout = pbrBindGroupLayout1_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelinePbr3D(
                                                               command.topology,
                                                               RequiredStripIndexFormat(command),
                                                               command.depthTest,
                                                               command.depthWrite, command.depthFunc,
                                                               command.blend, command.blendParams,
                                                               command.cullMode, command.wireframe,
                                                               command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        pbrDrawCommands_.clear();
    }

    // ------------------------------------------------------------------------------------------
    // plan_cnj.md Phase 14J: SkinnedEffect (skinned3d.wgsl family) -- closes this backend's
    // pre-existing "no skinning shader at all" gap. See CreateSkinnedResources()'s own doc
    // comment for the full formula-fidelity rationale (ported line-for-line from
    // EasyGLGraphicsBackend::EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()).
    // ------------------------------------------------------------------------------------------

    void WebGPUGraphicsBackend::DestroySkinnedResources()
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

    void WebGPUGraphicsBackend::CreateSkinnedResources()
    {
        DestroySkinnedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from EasyGLGraphicsBackend::EnsureSkinnedProgram()'s GLSL shader line-for-line:
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
        // attribute, mirroring this backend's own existing texturedShader_/coloredTexturedShader_
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
        // SkinnedEffect.PreferPerPixelLighting==false default, matching every other backend).
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineSkinned3D(std::size_t stride, bool preferVertexLit,
                                                                             WGPUPrimitiveTopology topology,
                                                                             WGPUIndexFormat stripIndexFormat,
                                                                             bool depthTest, bool depthWrite,
                                                                             int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const bool hasVertexColor = (stride == 56);
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
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
        // normalized -- matches EasyGLGraphicsBackend::ApplyLayout's glVertexAttribIPointer path.
        std::array<WGPUVertexAttribute, 6> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = 12;
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = 24;
        attributes[2].shaderLocation = 2;
        attributes[3].format = WGPUVertexFormat_Float32x4;
        attributes[3].offset = 32;
        attributes[3].shaderLocation = 3;
        attributes[4].format = WGPUVertexFormat_Uint8x4;
        attributes[4].offset = 48;
        attributes[4].shaderLocation = 4;
        std::uint32_t attributeCount = 5;
        std::uint64_t arrayStride = 52;
        if (hasVertexColor)
        {
            attributes[5].format = WGPUVertexFormat_Unorm8x4;
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

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Skinned3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::QueueSkinnedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 52 && stride != 56)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedDraw requires a stride-52 "
                                        "(VertexPositionNormalTextureSkinned) or stride-56 "
                                        "(with vertex colour) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedDraw requires a bound texture0");

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
        command.texture = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        // Real XNA's SkinnedEffect.PreferPerPixelLighting default is false (per-vertex), matching
        // every other backend's own dispatch condition for this flag (Task 1102b).
        command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillSkinningParams(command.skinningParams, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderSkinnedDraws(WGPURenderPassEncoder pass)
    {
        for (const SkinnedDrawCommand& command : skinnedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
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
                                                                    command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        skinnedDrawCommands_.clear();
    }

    // ------------------------------------------------------------------------------------------
    // SkinnedPbrEffect (skinned_pbr3d.wgsl) -- the PBR + skinning combo, stride 68
    // (VertexPositionNormalTangentTextureSkinned). Bone-palette skinning identical to
    // skinned3d.wgsl above (extended to also skin Tangent), feeding pbr3d.wgsl's own fragment BRDF
    // unchanged, matching EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s combination exactly.
    // ------------------------------------------------------------------------------------------

    void WebGPUGraphicsBackend::DestroySkinnedPbrResources()
    {
        for (auto& [key, pipe] : skinnedPbrPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        skinnedPbrPipelines_.clear();
        if (skinnedPbrPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(skinnedPbrPipelineLayout_);
        if (skinnedPbrBindGroupLayout0_ != nullptr) wgpuBindGroupLayoutRelease(skinnedPbrBindGroupLayout0_);
        if (skinnedPbrShader_ != nullptr) wgpuShaderModuleRelease(skinnedPbrShader_);
        skinnedPbrPipelineLayout_ = nullptr;
        skinnedPbrBindGroupLayout0_ = nullptr;
        skinnedPbrShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateSkinnedPbrResources()
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

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) blendWeight: vec4f,
    @location(5) blendIndices: vec4<u32>,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
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
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    // REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix
    // (lp.normalMatrixCol*), not raw worldMat3. Raw World is correct only for rotation and uniform
    // scale and diverges from FNA's mul(normal, WorldInverseTranspose) under non-uniform scale; it
    // also contradicted this backend's own unskinned pbr3d.wgsl, which already uses the inverse
    // transpose. The tangent stays on raw worldMat3: tangents transform as directions, not as
    // normals (glTF convention, unchanged).
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * (skinMat3 * input.normal));
    output.worldTangent = worldMat3 * (skinMat3 * input.tangent.xyz);
    output.bitangentSign = input.tangent.w;
    output.worldPos = (lp.world * skinnedPos).xyz;
    output.uv = input.uv;
    return output;
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, roughness: f32, metallic: f32) -> vec3f {
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
    let f = f0 + (vec3f(1.0) - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, input.uv);
    let albedo = baseColorSample.rgb * u.diffuseColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a;

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    let sampledNormal = textureSample(normalTex, texSampler, input.uv).rgb * 2.0 - 1.0;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, input.uv);
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    let f0 = mix(vec3f(0.04), albedo, metallic);

    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, roughness, metallic);

    let occlusion = textureSample(occlusionTex, texSampler, input.uv).r;
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissive = lp.emissiveColor.xyz * textureSample(emissiveTex, texSampler, input.uv).rgb;

    return vec4f(ambient + lo + emissive, alpha);
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        skinnedPbrShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (skinnedPbrShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D shader");

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
        uboEntries[2].buffer.minBindingSize = 16;
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

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineSkinnedPbr3D(WGPUPrimitiveTopology topology,
                                                                                WGPUIndexFormat stripIndexFormat,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        const std::uint64_t key = Make3DPipelineKey(topology, stripIndexFormat,
                                                     depthTest, depthWrite, depthFunc,
                                                     blend, blendParams, cullMode, wireframe,
                                                     depthBias, slopeScaleDepthBias, 0, colorWriteMask_, sampleMask_);
        if (auto it = skinnedPbrPipelines_.find(key); it != skinnedPbrPipelines_.end())
            return it->second;

        // Matches ApplyLayout's stride==68 case (VertexPositionNormalTangentTextureSkinned):
        // Position(12)+Normal(12)+Tangent(16)+TextureCoordinate(8)+BlendWeight(16)+BlendIndices(4).
        std::array<WGPUVertexAttribute, 6> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = 12;
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x4;
        attributes[2].offset = 24;
        attributes[2].shaderLocation = 2;
        attributes[3].format = WGPUVertexFormat_Float32x2;
        attributes[3].offset = 40;
        attributes[3].shaderLocation = 3;
        attributes[4].format = WGPUVertexFormat_Float32x4;
        attributes[4].offset = 48;
        attributes[4].shaderLocation = 4;
        attributes[5].format = WGPUVertexFormat_Uint8x4;
        attributes[5].offset = 64;
        attributes[5].shaderLocation = 5;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = 68;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = CurrentWriteMask(); // REMED-GFX-077: BlendState.ColorWriteChannels slot 0
        WGPUFragmentState fragment{};
        fragment.module = skinnedPbrShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU SkinnedPbr3D Pipeline");
        pipeline.layout = skinnedPbrPipelineLayout_;
        pipeline.vertex.module = skinnedPbrShader_;
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
        // WEBGPU-58: this backend's single backend-GLOBAL MSAA sample count (see sampleCount_'s
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
        // interpretation (this backend's depth attachment is always Depth24PlusStencil8).
        depthStencil.depthBias = static_cast<std::int32_t>(depthBias * 16777215.0f);
        depthStencil.depthBiasSlopeScale = slopeScaleDepthBias;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SkinnedPbr3D pipeline");
        skinnedPbrPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::QueueSkinnedPbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount,
                                                     const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 68)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedPbrDraw requires a stride-68 "
                                        "(VertexPositionNormalTangentTextureSkinned) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueSkinnedPbrDraw requires a bound texture0");

        EnsurePbrDefaultTextures();

        SkinnedPbrDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 68u;
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
        command.baseColorTexture = ResolveSamplable(params.texture0);
        // WEBGPU-82: real per-slot SamplerState (slot 0) instead of the struct's hardcoded
        // Linear/Clamp/Clamp defaults -- see ApplySamplerState().
        command.textureFilter = slotSamplers_[0].filter;
        command.addressU = slotSamplers_[0].addressU;
        command.addressV = slotSamplers_[0].addressV;
        command.maxAnisotropy = slotSamplers_[0].maxAnisotropy;
        command.normalMap = params.pbrNormalMap != nullptr
            ? ResolveSamplable(params.pbrNormalMap)
            : pbrDefaultFlatNormalTexture_.get();
        command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr
            ? ResolveSamplable(params.pbrMetallicRoughnessMap)
            : pbrDefaultWhiteTexture_.get();
        command.emissiveMap = params.pbrEmissiveMap != nullptr
            ? ResolveSamplable(params.pbrEmissiveMap)
            : pbrDefaultWhiteTexture_.get();
        command.occlusionMap = params.pbrOcclusionMap != nullptr
            ? ResolveSamplable(params.pbrOcclusionMap)
            : pbrDefaultWhiteTexture_.get();

        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillPbrFactors(command.pbrFactors, params);
        FillSkinningParams(command.skinningParams, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
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
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderSkinnedPbrDraws(WGPURenderPassEncoder pass)
    {
        for (const SkinnedPbrDrawCommand& command : skinnedPbrDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.baseColorTexture == nullptr ||
                command.normalMap == nullptr || command.metallicRoughnessMap == nullptr ||
                command.emissiveMap == nullptr || command.occlusionMap == nullptr)
                continue;

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

            WGPUSampler sampler = GetOrCreateSlotSampler(command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy);
            std::array<WGPUBindGroupEntry, 6> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.baseColorTexture->View();
            texEntries[2].binding = 2;
            texEntries[2].textureView = command.normalMap->View();
            texEntries[3].binding = 3;
            texEntries[3].textureView = command.metallicRoughnessMap->View();
            texEntries[4].binding = 4;
            texEntries[4].textureView = command.emissiveMap->View();
            texEntries[5].binding = 5;
            texEntries[5].textureView = command.occlusionMap->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU SkinnedPbr3D Texture BindGroup");
            texBindDescriptor.layout = pbrBindGroupLayout1_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineSkinnedPbr3D(
                                                                      command.topology,
                                                                      RequiredStripIndexFormat(command),
                                                                      command.depthTest,
                                                                      command.depthWrite, command.depthFunc,
                                                                      command.blend, command.blendParams,
                                                                      command.cullMode, command.wireframe,
                                                                      command.depthBias, command.slopeScaleDepthBias);
            // REMED-GFX-116: this draw's OWN captured Viewport, never the live backend value.
            ApplyDrawViewport(pass, command.viewport);
            // REMED-GFX-146: and this draw's OWN captured scissor state, for the same reason.
            ApplyDrawScissor(pass, command.scissor);
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
        skinnedPbrDrawCommands_.clear();
    }
}


namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<WebGPU::WebGPUGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
