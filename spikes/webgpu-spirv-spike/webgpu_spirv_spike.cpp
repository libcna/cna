// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-166 existence gate: can the pinned wgpu-native (v29.0.1.1) consume
// the SPIR-V the pinned MojoShader emits for a real compiled XNA Effect, and render its pixels?
//
// The question has three separable halves and this spike answers each one on its own, because a
// negative on any of them means something different for WEBGPU-203/204:
//
//   1. Does the instance advertise WGPUInstanceFeatureName_ShaderSourceSPIRV at all?
//   2. Does a shader module actually build from MojoShader's "spirv" (VK-mode) output --
//      combined image samplers, four fixed descriptor sets and all?
//   3. If it does, does a render pipeline built from that pair draw the expected pixels?
//
// The MojoShader half is a straight lift of tools/graphics/mojoshader_vulkan_probe.cpp's
// nine-function MOJOSHADER_effectShaderContext (FX-064): there is no mojoshader_webgpu.c any more
// than there is a mojoshader_vulkan.c, and the profile string that matters is "spirv"
// (SPIRV_MODE_VK: real DescriptorSet/Binding decorations) rather than "glspirv".
//
// Usage: webgpu_spirv_spike <CnaConformanceEffect.fxb>

#include "mojoshader.h"

#include "spirv_split_combined_samplers.hpp"

// WEBGPU-203: the real CNA translator, compiled straight into this spike so the loop that
// iterates on it is the same code the renderer ships.
#include "CNA/Internal/Renderers/MojoShader/SpirvToWgsl.hpp"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // ------------------------------------------------------------------------------------------
    // MojoShader effect backend (lifted from the FX-064 Vulkan probe; see that file's header for
    // why each piece exists).
    // ------------------------------------------------------------------------------------------
    struct MyShader
    {
        const MOJOSHADER_parseData* parseData = nullptr;
        int refcount = 1;
    };

    constexpr int kMaxFloat4Registers = 256;
    constexpr int kMaxInt4Registers = 16;
    constexpr int kMaxBoolRegisters = 16;

    struct MyEffectContext
    {
        const char* profile = MOJOSHADER_PROFILE_SPIRV;
        MyShader* boundVertex = nullptr;
        MyShader* boundPixel = nullptr;
        float vsRegF[kMaxFloat4Registers * 4]{};
        int vsRegI[kMaxInt4Registers * 4]{};
        unsigned char vsRegB[kMaxBoolRegisters]{};
        float psRegF[kMaxFloat4Registers * 4]{};
        int psRegI[kMaxInt4Registers * 4]{};
        unsigned char psRegB[kMaxBoolRegisters]{};
        std::string lastError;
    };

    void* MOJOSHADERCALL BackendCompileShader(
        const void* ctxVoid, const char* mainfn, const unsigned char* tokenbuf,
        const unsigned int bufsize, const MOJOSHADER_swizzle* swiz, const unsigned int swizcount,
        const MOJOSHADER_samplerMap* smap, const unsigned int smapcount)
    {
        auto* ctx = static_cast<MyEffectContext*>(const_cast<void*>(ctxVoid));
        const MOJOSHADER_parseData* pd = MOJOSHADER_parse(
            ctx->profile, mainfn, tokenbuf, bufsize, swiz, swizcount, smap, smapcount, nullptr,
            nullptr, nullptr);
        if (pd->error_count > 0)
        {
            ctx->lastError = pd->errors[0].error != nullptr ? pd->errors[0].error : "<null>";
            MOJOSHADER_freeParseData(pd);
            return nullptr;
        }
        auto* shader = new MyShader{};
        shader->parseData = pd;
        return shader;
    }

    void MOJOSHADERCALL BackendShaderAddRef(void* shaderVoid)
    {
        if (shaderVoid != nullptr) static_cast<MyShader*>(shaderVoid)->refcount++;
    }

    void MOJOSHADERCALL BackendDeleteShader(const void* ctxVoid, void* shaderVoid)
    {
        if (shaderVoid == nullptr) return;
        auto* shader = static_cast<MyShader*>(shaderVoid);
        if (--shader->refcount > 0) return;
        auto* ctx = static_cast<MyEffectContext*>(const_cast<void*>(ctxVoid));
        if (ctx->boundVertex == shader) ctx->boundVertex = nullptr;
        if (ctx->boundPixel == shader) ctx->boundPixel = nullptr;
        MOJOSHADER_freeParseData(shader->parseData);
        delete shader;
    }

    MOJOSHADER_parseData* MOJOSHADERCALL BackendGetParseData(void* shaderVoid)
    {
        return shaderVoid != nullptr
                   ? const_cast<MOJOSHADER_parseData*>(static_cast<MyShader*>(shaderVoid)->parseData)
                   : nullptr;
    }

    void MOJOSHADERCALL BackendBindShaders(const void* ctxVoid, void* vshader, void* pshader)
    {
        auto* ctx = static_cast<MyEffectContext*>(const_cast<void*>(ctxVoid));
        ctx->boundVertex = static_cast<MyShader*>(vshader);
        ctx->boundPixel = static_cast<MyShader*>(pshader);
    }

    void MOJOSHADERCALL BackendGetBoundShaders(const void* ctxVoid, void** vshader, void** pshader)
    {
        const auto* ctx = static_cast<const MyEffectContext*>(ctxVoid);
        if (vshader != nullptr) *vshader = ctx->boundVertex;
        if (pshader != nullptr) *pshader = ctx->boundPixel;
    }

    void MOJOSHADERCALL BackendMapUniformBufferMemory(
        const void* ctxVoid, float** vsf, int** vsi, unsigned char** vsb, float** psf, int** psi,
        unsigned char** psb)
    {
        auto* ctx = static_cast<MyEffectContext*>(const_cast<void*>(ctxVoid));
        *vsf = ctx->vsRegF;
        *vsi = ctx->vsRegI;
        *vsb = ctx->vsRegB;
        *psf = ctx->psRegF;
        *psi = ctx->psRegI;
        *psb = ctx->psRegB;
    }

    void MOJOSHADERCALL BackendUnmapUniformBufferMemory(const void*) {}

    const char* MOJOSHADERCALL BackendGetError(const void* ctxVoid)
    {
        return static_cast<const MyEffectContext*>(ctxVoid)->lastError.c_str();
    }

    MOJOSHADER_effectShaderContext MakeBackend(MyEffectContext* ctx)
    {
        MOJOSHADER_effectShaderContext backend{};
        backend.shaderContext = ctx;
        backend.compileShader = BackendCompileShader;
        backend.shaderAddRef = BackendShaderAddRef;
        backend.deleteShader = BackendDeleteShader;
        backend.getParseData = BackendGetParseData;
        backend.bindShaders = BackendBindShaders;
        backend.getBoundShaders = BackendGetBoundShaders;
        backend.mapUniformBufferMemory = BackendMapUniformBufferMemory;
        backend.unmapUniformBufferMemory = BackendUnmapUniformBufferMemory;
        backend.getError = BackendGetError;
        return backend;
    }

    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
        if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
            bytes.clear();
        std::fclose(file);
        return bytes;
    }

    // ------------------------------------------------------------------------------------------
    // WebGPU plumbing
    // ------------------------------------------------------------------------------------------
    WGPUStringView SV(const char* s)
    {
        WGPUStringView v{};
        v.data = s;
        v.length = s != nullptr ? std::strlen(s) : 0;
        return v;
    }

    std::string ToString(WGPUStringView v)
    {
        if (v.data == nullptr) return {};
        if (v.length == WGPU_STRLEN) return std::string(v.data);
        return std::string(v.data, v.length);
    }

    struct AdapterState
    {
        WGPUAdapter adapter = nullptr;
        std::string error;
        bool completed = false;
    };

    void OnAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                   void* u1, void*)
    {
        auto& s = *static_cast<AdapterState*>(u1);
        if (status == WGPURequestAdapterStatus_Success) s.adapter = adapter;
        else s.error = ToString(message);
        s.completed = true;
    }

    struct DeviceState
    {
        WGPUDevice device = nullptr;
        std::string error;
        bool completed = false;
    };

    void OnDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                  void* u1, void*)
    {
        auto& s = *static_cast<DeviceState*>(u1);
        if (status == WGPURequestDeviceStatus_Success) s.device = device;
        else s.error = ToString(message);
        s.completed = true;
    }

    void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*,
                           void*)
    {
        std::printf("    [uncaptured error type=%d] %s\n", (int) type, ToString(message).c_str());
    }

    void Pump(WGPUInstance instance, const bool& done, int millis = 5000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
        while (!done && std::chrono::steady_clock::now() < deadline)
        {
            wgpuInstanceProcessEvents(instance);
            if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Minimal SPIR-V decoration scanner: reads OpName / OpDecorate(Location, Binding,
    // DescriptorSet) straight out of the finished module. Same reasoning as the Vulkan probe --
    // ground truth beats guessing at MojoShader's assignment rules.
    constexpr uint32_t kOpName = 5;
    constexpr uint32_t kOpDecorate = 71;
    constexpr uint32_t kDecorationLocation = 30;
    constexpr uint32_t kDecorationBinding = 33;
    constexpr uint32_t kDecorationDescriptorSet = 34;

    constexpr uint32_t kOpEntryPoint = 15;

    struct Decorations
    {
        std::map<uint32_t, std::string> names;
        std::map<uint32_t, uint32_t> locations;
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> setAndBinding;
        /// The OpEntryPoint interface list, in module order. naga reports its findings by index
        /// into this list ("Argument 5 varying error"), so a diagnosis needs the list itself --
        /// the Location decorations alone cannot show an id that appears in it TWICE.
        std::vector<uint32_t> entryInterface;
        uint32_t version = 0;
    };

    Decorations Scan(const uint32_t* words, std::size_t wordCount)
    {
        Decorations d;
        if (wordCount < 5) return d;
        d.version = words[1];
        std::size_t i = 5;
        while (i < wordCount)
        {
            const uint32_t word0 = words[i];
            const uint32_t opcode = word0 & 0xFFFFu;
            const uint32_t len = word0 >> 16;
            if (len == 0 || i + len > wordCount) break;
            if (opcode == kOpName && len >= 3)
            {
                d.names[words[i + 1]] =
                    std::string(reinterpret_cast<const char*>(&words[i + 2]));
            }
            else if (opcode == kOpEntryPoint && len >= 4)
            {
                // words: opcode | execution model | entry id | name... | interface ids
                std::size_t j = i + 3;
                while (j < i + len && words[j] != 0)
                {
                    const char* text = reinterpret_cast<const char*>(&words[j]);
                    const std::size_t bytes = std::strlen(text) + 1;
                    j += (bytes + 3) / 4;
                    break;
                }
                for (; j < i + len; ++j) d.entryInterface.push_back(words[j]);
            }
            else if (opcode == kOpDecorate && len >= 4)
            {
                const uint32_t id = words[i + 1];
                switch (words[i + 2])
                {
                    case kDecorationLocation: d.locations[id] = words[i + 3]; break;
                    case kDecorationBinding: d.setAndBinding[id].second = words[i + 3]; break;
                    case kDecorationDescriptorSet: d.setAndBinding[id].first = words[i + 3]; break;
                    default: break;
                }
            }
            i += len;
        }
        return d;
    }

    void WriteSpv(const std::string& path, const char* words, std::size_t wordCount)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) return;
        std::fwrite(words, sizeof(uint32_t), wordCount, f);
        std::fclose(f);
    }

    /// One pushed validation error scope, popped and printed with the label it belongs to.
    struct ScopeGuard
    {
        explicit ScopeGuard(WGPUDevice d) : device(d)
        {
            wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        }

        void Report(WGPUInstance instance, const char* label, void* result)
        {
            struct State { bool done = false; std::string message; WGPUErrorType type{}; } state;
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowProcessEvents;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView msg,
                             void* u1, void*) {
                auto& s = *static_cast<State*>(u1);
                s.type = type;
                s.message = ToString(msg);
                s.done = true;
            };
            cb.userdata1 = &state;
            wgpuDevicePopErrorScope(device, cb);
            Pump(instance, state.done);
            std::printf("  %s -> %p (error type %d)\n", label, result, (int) state.type);
            if (!state.message.empty()) std::printf("  %s says:\n%s\n", label, state.message.c_str());
            reported = true;
        }

        ~ScopeGuard() = default;

        WGPUDevice device;
        bool reported = false;
    };

    WGPUShaderModule CreateSpirvModuleA(WGPUInstance instance, WGPUDevice device, const char* label,
                                        const char* words, std::size_t wordCount)
    {
        ScopeGuard scope(device);
        WGPUShaderSourceSPIRV src{};
        src.chain.sType = WGPUSType_ShaderSourceSPIRV;
        src.codeSize = static_cast<uint32_t>(wordCount);
        src.code = reinterpret_cast<const uint32_t*>(words);
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &src.chain;
        desc.label = SV(label);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &desc);
        scope.Report(instance, label, module);
        return module;
    }

    /// MojoShader's SPIR-V uniform block: one 16-byte slot per declared register, in declaration
    /// order (the same rule VulkanCompiledEffect::CaptureUniformSnapshotEXT packs to).
    void PackUniforms(const MOJOSHADER_parseData* pd, const float* regF, const int* regI,
                      const unsigned char* regB, std::vector<uint8_t>& out)
    {
        out.clear();
        if (pd == nullptr || pd->uniform_count <= 0) return;
        std::size_t total = 0;
        for (int i = 0; i < pd->uniform_count; ++i)
            total += static_cast<std::size_t>(pd->uniforms[i].array_count
                                                  ? pd->uniforms[i].array_count : 1) * 16u;
        out.assign(total, 0u);
        std::size_t offset = 0;
        for (int i = 0; i < pd->uniform_count; ++i)
        {
            const MOJOSHADER_uniform& u = pd->uniforms[i];
            const int span = u.array_count ? u.array_count : 1;
            const std::size_t bytes = static_cast<std::size_t>(span) * 16u;
            uint8_t* dst = out.data() + offset;
            if (u.type == MOJOSHADER_UNIFORM_FLOAT && u.index >= 0)
                std::memcpy(dst, &regF[4 * u.index], bytes);
            else if (u.type == MOJOSHADER_UNIFORM_INT && u.index >= 0)
                std::memcpy(dst, &regI[4 * u.index], bytes);
            else if (u.type == MOJOSHADER_UNIFORM_BOOL && u.index >= 0)
                for (int j = 0; j < span; ++j)
                    std::memcpy(dst + j * 16, &regB[u.index + j], 1);
            offset += bytes;
        }
    }

    struct Rendered
    {
        bool ok = false;
        std::string error;
        uint8_t centre[4]{};
    };

    Rendered RenderOnce(WGPUInstance instance, WGPUDevice device, WGPUQueue queue,
                        WGPUShaderModule vsModule, WGPUShaderModule psModule,
                        const std::vector<uint8_t>& vsUniforms,
                        const std::vector<uint8_t>& psUniforms,
                        const CnaSpirv::SplitResult& psSplit, int size,
                        const char* vsEntry, const char* psEntry)
    {
        Rendered out;

        const auto MakeBuffer = [&](const std::vector<uint8_t>& bytes, WGPUBufferUsage usage) {
            WGPUBufferDescriptor d{};
            d.size = bytes.empty() ? 16u : ((bytes.size() + 15u) & ~std::size_t(15));
            d.usage = usage | WGPUBufferUsage_CopyDst;
            WGPUBuffer b = wgpuDeviceCreateBuffer(device, &d);
            if (!bytes.empty()) wgpuQueueWriteBuffer(queue, b, 0, bytes.data(), bytes.size());
            return b;
        };

        WGPUBuffer vsUbo = MakeBuffer(vsUniforms, WGPUBufferUsage_Uniform);
        WGPUBuffer psUbo = MakeBuffer(psUniforms, WGPUBufferUsage_Uniform);

        // A white 1x1 texture, so the pixel shader's output is purely its constant arithmetic.
        WGPUTextureDescriptor texDesc{};
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.size = {1, 1, 1};
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        WGPUTexture white = wgpuDeviceCreateTexture(device, &texDesc);
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        WGPUTexelCopyTextureInfo dstInfo{};
        dstInfo.texture = white;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D writeSize{1, 1, 1};
        wgpuQueueWriteTexture(queue, &dstInfo, whitePixel, 4, &layout, &writeSize);
        WGPUTextureView whiteView = wgpuTextureCreateView(white, nullptr);

        WGPUSamplerDescriptor samplerDesc{};
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        samplerDesc.lodMaxClamp = 32.0f;
        samplerDesc.maxAnisotropy = 1;
        WGPUSampler sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

        // Group layouts for MojoShader's four fixed sets.
        const auto UniformLayout = [&](WGPUShaderStage stage) {
            WGPUBindGroupLayoutEntry e{};
            e.binding = 0;
            e.visibility = stage;
            e.buffer.type = WGPUBufferBindingType_Uniform;
            WGPUBindGroupLayoutDescriptor d{};
            d.entryCount = 1;
            d.entries = &e;
            return wgpuDeviceCreateBindGroupLayout(device, &d);
        };
        WGPUBindGroupLayoutDescriptor emptyDesc{};
        WGPUBindGroupLayout group0 = wgpuDeviceCreateBindGroupLayout(device, &emptyDesc);
        WGPUBindGroupLayout group1 = UniformLayout(WGPUShaderStage_Vertex);
        WGPUBindGroupLayout group3 = UniformLayout(WGPUShaderStage_Fragment);

        std::vector<WGPUBindGroupLayoutEntry> psSamplerEntries;
        for (const auto& b : psSplit.samplers)
        {
            if (b.set != 2) continue;
            WGPUBindGroupLayoutEntry t{};
            t.binding = b.textureBinding;
            t.visibility = WGPUShaderStage_Fragment;
            t.texture.sampleType = WGPUTextureSampleType_Float;
            t.texture.viewDimension = WGPUTextureViewDimension_2D;
            psSamplerEntries.push_back(t);
            WGPUBindGroupLayoutEntry s{};
            s.binding = b.samplerBinding;
            s.visibility = WGPUShaderStage_Fragment;
            s.sampler.type = WGPUSamplerBindingType_Filtering;
            psSamplerEntries.push_back(s);
        }
        WGPUBindGroupLayoutDescriptor psSamplerLayoutDesc{};
        psSamplerLayoutDesc.entryCount = psSamplerEntries.size();
        psSamplerLayoutDesc.entries = psSamplerEntries.data();
        WGPUBindGroupLayout group2 = wgpuDeviceCreateBindGroupLayout(device, &psSamplerLayoutDesc);

        WGPUBindGroupLayout groups[4] = {group0, group1, group2, group3};
        WGPUPipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 4;
        plDesc.bindGroupLayouts = groups;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

        // Vertex layout: POSITION0 at location 0, TEXCOORD0 at location 1 -- the locations the
        // decoration scan read out of the module rather than any assumed convention.
        WGPUVertexAttribute attributes[2]{};
        attributes[0].format = WGPUVertexFormat_Float32x4;
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = 16;
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vbLayout{};
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.arrayStride = 24;
        vbLayout.attributeCount = 2;
        vbLayout.attributes = attributes;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = psModule;
        fragment.entryPoint = SV(psEntry);
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipeDesc{};
        pipeDesc.label = SV("spike-pipeline");
        pipeDesc.layout = pipelineLayout;
        pipeDesc.vertex.module = vsModule;
        pipeDesc.vertex.entryPoint = SV(vsEntry);
        pipeDesc.vertex.bufferCount = 1;
        pipeDesc.vertex.buffers = &vbLayout;
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.primitive.cullMode = WGPUCullMode_None;
        pipeDesc.multisample.count = 1;
        pipeDesc.multisample.mask = 0xFFFFFFFFu;
        pipeDesc.fragment = &fragment;

        ScopeGuard pipeScope(device);
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeDesc);
        pipeScope.Report(instance, "spike-pipeline", pipeline);
        if (pipeline == nullptr)
        {
            out.error = "pipeline creation returned null";
            return out;
        }

        // Two triangles covering clip space, with the identity Transform the effect defaults to.
        const float quad[6][6] = {
            {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f}, {1.f, -1.f, 0.f, 1.f, 1.f, 1.f},
            {1.f, 1.f, 0.f, 1.f, 1.f, 0.f},   {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f},
            {1.f, 1.f, 0.f, 1.f, 1.f, 0.f},   {-1.f, 1.f, 0.f, 1.f, 0.f, 0.f}};
        WGPUBufferDescriptor vbDesc{};
        vbDesc.size = sizeof(quad);
        vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
        wgpuQueueWriteBuffer(queue, vertexBuffer, 0, quad, sizeof(quad));

        const auto MakeUniformGroup = [&](WGPUBindGroupLayout gl, WGPUBuffer buffer,
                                          std::size_t bytes) {
            WGPUBindGroupEntry e{};
            e.binding = 0;
            e.buffer = buffer;
            e.size = bytes == 0 ? 16u : ((bytes + 15u) & ~std::size_t(15));
            WGPUBindGroupDescriptor d{};
            d.layout = gl;
            d.entryCount = 1;
            d.entries = &e;
            return wgpuDeviceCreateBindGroup(device, &d);
        };
        WGPUBindGroupDescriptor emptyGroupDesc{};
        emptyGroupDesc.layout = group0;
        WGPUBindGroup bind0 = wgpuDeviceCreateBindGroup(device, &emptyGroupDesc);
        WGPUBindGroup bind1 = MakeUniformGroup(group1, vsUbo, vsUniforms.size());
        WGPUBindGroup bind3 = MakeUniformGroup(group3, psUbo, psUniforms.size());

        std::vector<WGPUBindGroupEntry> psEntries;
        for (const auto& b : psSplit.samplers)
        {
            if (b.set != 2) continue;
            WGPUBindGroupEntry t{};
            t.binding = b.textureBinding;
            t.textureView = whiteView;
            psEntries.push_back(t);
            WGPUBindGroupEntry s{};
            s.binding = b.samplerBinding;
            s.sampler = sampler;
            psEntries.push_back(s);
        }
        WGPUBindGroupDescriptor psGroupDesc{};
        psGroupDesc.layout = group2;
        psGroupDesc.entryCount = psEntries.size();
        psGroupDesc.entries = psEntries.data();
        WGPUBindGroup bind2 = wgpuDeviceCreateBindGroup(device, &psGroupDesc);

        WGPUTextureDescriptor targetDesc{};
        targetDesc.dimension = WGPUTextureDimension_2D;
        targetDesc.size = {static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1};
        targetDesc.format = WGPUTextureFormat_RGBA8Unorm;
        targetDesc.mipLevelCount = 1;
        targetDesc.sampleCount = 1;
        targetDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture target = wgpuDeviceCreateTexture(device, &targetDesc);
        WGPUTextureView targetView = wgpuTextureCreateView(target, nullptr);

        const std::size_t readbackBytes = static_cast<std::size_t>(size) * size * 4u;
        WGPUBufferDescriptor rbDesc{};
        rbDesc.size = readbackBytes;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &rbDesc);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind0, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bind1, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 2, bind2, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, bind3, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, sizeof(quad));
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUTexelCopyTextureInfo src{};
        src.texture = target;
        WGPUTexelCopyBufferInfo dst{};
        dst.buffer = readback;
        dst.layout.bytesPerRow = static_cast<uint32_t>(size * 4);
        dst.layout.rowsPerImage = static_cast<uint32_t>(size);
        WGPUExtent3D copySize{static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);

        struct MapState { bool done = false; WGPUMapAsyncStatus status{}; } mapState;
        WGPUBufferMapCallbackInfo mapCb{};
        mapCb.mode = WGPUCallbackMode_AllowProcessEvents;
        mapCb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
            auto& s = *static_cast<MapState*>(u1);
            s.status = status;
            s.done = true;
        };
        mapCb.userdata1 = &mapState;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, readbackBytes, mapCb);
        Pump(instance, mapState.done);
        if (mapState.status != WGPUMapAsyncStatus_Success)
        {
            out.error = "buffer map failed";
            return out;
        }
        const auto* pixels =
            static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, readbackBytes));
        const std::size_t centre = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
        std::memcpy(out.centre, pixels + centre, 4);
        wgpuBufferUnmap(readback);
        out.ok = true;
        return out;
    }


    /// Q6 -- the opcode repertoire MojoShader's SPIR-V profile actually emits, accumulated over
    /// every swept module. This is the measurement that sizes a SPIR-V -> WGSL translator for
    /// the browser (plans/plan_webgpu.md WEBGPU-203): the question is not "can arbitrary SPIR-V
    /// be translated" but "how large is the subset this pipeline can produce".
    std::map<uint32_t, int> g_opcodeHistogram;
    std::map<uint32_t, int> g_glslExtHistogram;

    std::map<uint32_t, int> g_decorationHistogram;
    std::map<uint32_t, int> g_builtinHistogram;
    std::map<uint32_t, int> g_storageClassHistogram;

    void CountOpcodes(const uint32_t* words, std::size_t wordCount)
    {
        if (wordCount < 5) return;
        std::size_t i = 5;
        while (i < wordCount)
        {
            const uint32_t opcode = words[i] & 0xFFFFu;
            const uint32_t len = words[i] >> 16;
            if (len == 0 || i + len > wordCount) break;
            g_opcodeHistogram[opcode] += 1;
            // OpExtInst: result type, result id, set id, instruction literal
            if (opcode == 12u && len >= 5) g_glslExtHistogram[words[i + 4]] += 1;
            // OpDecorate: target id, decoration [, literals]
            if (opcode == 71u && len >= 3)
            {
                g_decorationHistogram[words[i + 2]] += 1;
                if (words[i + 2] == 11u && len >= 4) g_builtinHistogram[words[i + 3]] += 1;
            }
            // OpMemberDecorate: struct id, member, decoration
            if (opcode == 72u && len >= 4) g_decorationHistogram[words[i + 3]] += 1;
            // OpVariable: result type, result id, storage class
            if (opcode == 59u && len >= 4) g_storageClassHistogram[words[i + 3]] += 1;
            // OpTypePointer: result id, storage class, type
            if (opcode == 32u && len >= 4) g_storageClassHistogram[words[i + 2]] += 1;
            i += len;
        }
    }

    /// WEBGPU-203: translate the same split SPIR-V to WGSL and create a module from THAT, which
    /// is the route the browser must take. wgpu-native accepts WGSL, so the native build is a
    /// usable oracle for the browser route.
    bool QuietWgslModule(WGPUInstance instance, WGPUDevice device, const char* label,
                         const uint32_t* words, std::size_t wordCount)
    {
        const auto translated =
            CNA::Internal::Renderers::MojoShaderEffect::TranslateSpirvToWgsl(words, wordCount);
        if (!translated.error.empty())
        {
            std::printf("  WGSL-FAIL %s: %s\n", label, translated.error.c_str());
            return false;
        }
        if (std::getenv("SPIKE_WGSL_DUMP") != nullptr)
            std::printf("----- %s -----\n%s\n", label, translated.wgsl.c_str());

        struct State { bool done = false; std::string message; WGPUErrorType type{}; } state;
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = SV(translated.wgsl.c_str());
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &src.chain;
        desc.label = SV(label);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &desc);
        WGPUPopErrorScopeCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView msg, void* u1,
                         void*) {
            auto& s = *static_cast<State*>(u1);
            s.type = type;
            s.message = ToString(msg);
            s.done = true;
        };
        cb.userdata1 = &state;
        wgpuDevicePopErrorScope(device, cb);
        Pump(instance, state.done);
        if (module != nullptr) wgpuShaderModuleRelease(module);
        if (state.type != WGPUErrorType_NoError)
        {
            std::printf("  WGSL-FAIL %s: %s\n", label, state.message.c_str());
            std::printf("----- generated WGSL -----\n%s\n", translated.wgsl.c_str());
            return false;
        }
        return true;
    }

    /// Creates a module and reports only failures, for the sweep.
    bool QuietModule(WGPUInstance instance, WGPUDevice device, const char* label,
                     const uint32_t* words, std::size_t wordCount)
    {
        CountOpcodes(words, wordCount);
        {
            // Two module-level invariants that a naga "accepted" verdict does NOT check, and that
            // the ps_1_x path was violating silently: a well-formed header, and an OpEntryPoint
            // interface that lists each variable once.
            const Decorations pre = Scan(words, wordCount);
            if (pre.version != 0x00010000u)
                std::printf("  WARN %s: header version 0x%08x (expected 0x00010000)\n", label,
                            pre.version);
            std::map<uint32_t, int> count;
            for (const uint32_t id : pre.entryInterface)
            {
                if (++count[id] == 2)
                    std::printf("  WARN %s: OpEntryPoint lists %%%u twice\n", label, id);
            }
        }
        struct State { bool done = false; std::string message; WGPUErrorType type{}; } state;
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        WGPUShaderSourceSPIRV src{};
        src.chain.sType = WGPUSType_ShaderSourceSPIRV;
        src.codeSize = static_cast<uint32_t>(wordCount);
        src.code = words;
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &src.chain;
        desc.label = SV(label);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &desc);
        WGPUPopErrorScopeCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView msg, void* u1,
                         void*) {
            auto& s = *static_cast<State*>(u1);
            s.type = type;
            s.message = ToString(msg);
            s.done = true;
        };
        cb.userdata1 = &state;
        wgpuDevicePopErrorScope(device, cb);
        Pump(instance, state.done);
        if (module != nullptr) wgpuShaderModuleRelease(module);
        if (state.type == WGPUErrorType_NoError && std::getenv("SPIKE_WGSL") != nullptr)
        {
            if (!QuietWgslModule(instance, device, label, words, wordCount)) return false;
        }
        if (state.type != WGPUErrorType_NoError)
        {
            std::printf("  FAIL %s: %s\n", label, state.message.c_str());
            const Decorations d = Scan(words, wordCount);
            for (const auto& [id, loc] : d.locations)
            {
                const auto it = d.names.find(id);
                std::printf("    Location %-12u on %%%-4u (%s)\n", loc, id,
                            it != d.names.end() ? it->second.c_str() : "?");
            }
            std::printf("    header version 0x%08x, OpEntryPoint interface (%zu ids):\n",
                        d.version, d.entryInterface.size());
            std::map<uint32_t, int> seen;
            for (std::size_t k = 0; k < d.entryInterface.size(); ++k)
            {
                const uint32_t id = d.entryInterface[k];
                const auto nit = d.names.find(id);
                const auto lit = d.locations.find(id);
                char loctext[32] = "-";
                if (lit != d.locations.end())
                    std::snprintf(loctext, sizeof(loctext), "%u", lit->second);
                std::printf("      arg %-2zu %%%-4u %-24s location %-6s%s\n", k, id,
                            nit != d.names.end() ? nit->second.c_str() : "?", loctext,
                            ++seen[id] > 1 ? "  <-- DUPLICATE ENTRY" : "");
            }
            return false;
        }
        return true;
    }


    /// Q5 -- every technique/pass of every fixture, so the answer is not one lucky shader.
    int SweepFile(WGPUInstance instance, WGPUDevice device, const char* path)
    {
        const std::vector<unsigned char> bytes = ReadFile(path);
        if (bytes.empty())
        {
            std::printf("  %s: UNREADABLE\n", path);
            return 1;
        }
        MyEffectContext ctx;
        MOJOSHADER_effectShaderContext backend = MakeBackend(&ctx);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0, &backend);
        if (effect == nullptr || effect->error_count > 0)
        {
            std::printf("  %s: PARSE FAILED (%s)\n", path,
                        effect != nullptr && effect->error_count > 0 ? effect->errors[0].error
                                                                    : "<null>");
            return 1;
        }
        int failures = 0;
        int passes = 0;
        for (int t = 0; t < effect->technique_count; ++t)
        {
            const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
            for (unsigned int p = 0; p < technique.pass_count; ++p)
            {
                MOJOSHADER_effectStateChanges changes{};
                MOJOSHADER_effectSetTechnique(effect, &technique);
                unsigned int count = 0;
                MOJOSHADER_effectBegin(effect, &count, 0, &changes);
                MOJOSHADER_effectBeginPass(effect, p);
                MyShader* v = ctx.boundVertex;
                MyShader* f = ctx.boundPixel;
                ++passes;
                if (v == nullptr || f == nullptr)
                {
                    std::printf("  %s t%d p%u: NO SHADER PAIR\n", path, t, p);
                    ++failures;
                }
                else
                {
                    // The link step patches vertex INPUT types too, so it needs one
                    // MOJOSHADER_vertexAttribute per declared shader input. Passing none leaves
                    // MojoShader's 0xDEADBEEF location placeholders in the module, which naga
                    // then reports as "Multiple bindings at location 3735928559".
                    std::vector<MOJOSHADER_vertexAttribute> attributes;
                    for (int ai = 0; ai < v->parseData->attribute_count; ++ai)
                    {
                        MOJOSHADER_vertexAttribute attribute{};
                        attribute.usage = v->parseData->attributes[ai].usage;
                        attribute.usageIndex = v->parseData->attributes[ai].index;
                        attribute.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
                        attributes.push_back(attribute);
                    }
                    const int table = MOJOSHADER_linkSPIRVShaders(
                        v->parseData, f->parseData, attributes.data(),
                        static_cast<int>(attributes.size()));
                    if (table <= 0)
                    {
                        std::printf("  %s t%d p%u: LINK FAILED\n", path, t, p);
                        ++failures;
                    }
                    else
                    {
                        const std::size_t vw =
                            static_cast<std::size_t>(v->parseData->output_len - table) / 4;
                        const std::size_t fw =
                            static_cast<std::size_t>(f->parseData->output_len - table) / 4;
                        const std::vector<uint32_t> vin(
                            reinterpret_cast<const uint32_t*>(v->parseData->output),
                            reinterpret_cast<const uint32_t*>(v->parseData->output) + vw);
                        const std::vector<uint32_t> fin(
                            reinterpret_cast<const uint32_t*>(f->parseData->output),
                            reinterpret_cast<const uint32_t*>(f->parseData->output) + fw);
                        const CnaSpirv::SplitResult vsp = CnaSpirv::SplitCombinedImageSamplers(vin);
                        const CnaSpirv::SplitResult fsp = CnaSpirv::SplitCombinedImageSamplers(fin);
                        char label[256];
                        std::snprintf(label, sizeof(label), "%s t%d p%u vs", path, t, p);
                        const bool vok = QuietModule(instance, device, label,
                                                     vsp.words.data(), vsp.words.size());
                        std::snprintf(label, sizeof(label), "%s t%d p%u ps", path, t, p);
                        const bool fok = QuietModule(instance, device, label,
                                                     fsp.words.data(), fsp.words.size());
                        if (!vok || !fok) ++failures;
                    }
                }
                MOJOSHADER_effectEndPass(effect);
                MOJOSHADER_effectEnd(effect);
            }
        }
        std::printf("  %s: %d passes, %d failures\n", path, passes, failures);
        MOJOSHADER_deleteEffect(effect);
        return failures;
    }

    void DumpModule(const char* label, const MOJOSHADER_parseData* pd, std::size_t words)
    {
        std::printf("  %s: %d words of SPIR-V, %d uniforms, %d samplers, %d attributes\n", label,
                    (int) words, pd->uniform_count, pd->sampler_count, pd->attribute_count);
        for (int i = 0; i < pd->uniform_count; ++i)
        {
            std::printf("      uniform[%d] type=%d index=%d array=%d name=%s\n", i,
                        (int) pd->uniforms[i].type, pd->uniforms[i].index,
                        pd->uniforms[i].array_count,
                        pd->uniforms[i].name != nullptr ? pd->uniforms[i].name : "");
        }
        for (int i = 0; i < pd->sampler_count; ++i)
        {
            std::printf("      sampler[%d] type=%d index=%d name=%s\n", i,
                        (int) pd->samplers[i].type, pd->samplers[i].index,
                        pd->samplers[i].name != nullptr ? pd->samplers[i].name : "");
        }
        for (int i = 0; i < pd->attribute_count; ++i)
        {
            std::printf("      attribute[%d] usage=%d index=%d name=%s\n", i,
                        (int) pd->attributes[i].usage, pd->attributes[i].index,
                        pd->attributes[i].name != nullptr ? pd->attributes[i].name : "");
        }
        const Decorations d = Scan(reinterpret_cast<const uint32_t*>(pd->output), words);
        for (const auto& [id, loc] : d.locations)
        {
            const auto it = d.names.find(id);
            std::printf("      OpDecorate %%%u Location %u  (%s)\n", id, loc,
                        it != d.names.end() ? it->second.c_str() : "?");
        }
        for (const auto& [id, sb] : d.setAndBinding)
        {
            const auto it = d.names.find(id);
            std::printf("      OpDecorate %%%u DescriptorSet %u Binding %u  (%s)\n", id, sb.first,
                        sb.second, it != d.names.end() ? it->second.c_str() : "?");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <effect.fxb>\n", argv[0]);
        return 2;
    }

    // ---------------------------------------------------------------------------------------
    // Q1: does this build of wgpu-native advertise the SPIR-V instance feature?
    // ---------------------------------------------------------------------------------------
    // BOTH instance-feature query entry points -- wgpuGetInstanceFeatures and
    // wgpuHasInstanceFeature -- are exported symbols that PANIC ("not implemented") in
    // wgpu-native v29.0.1.1 and abort the process: `panic_cannot_unwind`, not a catchable
    // failure and not a false return. So the runtime question WEBGPU-166 was written to ask
    // ("does the implementation advertise the feature?") cannot be asked of this pin at all.
    // What is left is to REQUEST it in the instance descriptor and see whether the instance
    // comes back, then whether a module actually builds.
    WGPUInstanceFeatureName requested[] = {WGPUInstanceFeatureName_ShaderSourceSPIRV};
    WGPUInstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = requested;
    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    std::printf("Q1: wgpuCreateInstance(requiredFeatures={ShaderSourceSPIRV}) -> %p\n",
                (void*) instance);
    if (instance == nullptr)
    {
        instance = wgpuCreateInstance(nullptr);
        std::printf("Q1: wgpuCreateInstance(nullptr) -> %p\n", (void*) instance);
    }
    if (instance == nullptr)
    {
        std::printf("FAIL: wgpuCreateInstance returned null\n");
        return 1;
    }

    AdapterState adapterState;
    WGPURequestAdapterOptions adapterOptions{};
    WGPURequestAdapterCallbackInfo adapterCb{};
    adapterCb.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCb.callback = OnAdapter;
    adapterCb.userdata1 = &adapterState;
    wgpuInstanceRequestAdapter(instance, &adapterOptions, adapterCb);
    Pump(instance, adapterState.completed);
    if (adapterState.adapter == nullptr)
    {
        std::printf("FAIL: no adapter: %s\n", adapterState.error.c_str());
        return 1;
    }

    WGPUAdapterInfo info{};
    wgpuAdapterGetInfo(adapterState.adapter, &info);
    std::printf("adapter: %s / %s (backend %d)\n", ToString(info.device).c_str(),
                ToString(info.description).c_str(), (int) info.backendType);
    wgpuAdapterInfoFreeMembers(info);

    DeviceState deviceState;
    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
    WGPURequestDeviceCallbackInfo deviceCb{};
    deviceCb.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCb.callback = OnDevice;
    deviceCb.userdata1 = &deviceState;
    wgpuAdapterRequestDevice(adapterState.adapter, &deviceDesc, deviceCb);
    Pump(instance, deviceState.completed);
    if (deviceState.device == nullptr)
    {
        std::printf("FAIL: no device: %s\n", deviceState.error.c_str());
        return 1;
    }
    WGPUDevice device = deviceState.device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // ---------------------------------------------------------------------------------------
    // Q2: translate a real compiled XNA effect and try to build shader modules from it.
    // ---------------------------------------------------------------------------------------
    const std::vector<unsigned char> bytes = ReadFile(argv[1]);
    if (bytes.empty())
    {
        std::printf("FAIL: could not read %s\n", argv[1]);
        return 1;
    }

    MyEffectContext ctx;
    MOJOSHADER_effectShaderContext backend = MakeBackend(&ctx);
    MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
        bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0, &backend);
    if (effect == nullptr || effect->error_count > 0)
    {
        std::printf("FAIL: MojoShader parse: %s\n",
                    effect != nullptr && effect->error_count > 0 ? effect->errors[0].error
                                                                 : "<null effect>");
        return 1;
    }
    std::printf("Q2: parsed %s -- %d techniques, %d params, %d objects\n", argv[1],
                effect->technique_count, effect->param_count, effect->object_count);

    // Bind technique 0 pass 0 the way a renderer would, then link the SPIR-V pair.
    MOJOSHADER_effectStateChanges changes{};
    MOJOSHADER_effectSetTechnique(effect, &effect->techniques[0]);
    unsigned int passCount = 0;
    MOJOSHADER_effectBegin(effect, &passCount, 0, &changes);
    MOJOSHADER_effectBeginPass(effect, 0);

    MyShader* vs = ctx.boundVertex;
    MyShader* ps = ctx.boundPixel;
    if (vs == nullptr || ps == nullptr)
    {
        std::printf("FAIL: pass 0 bound no shader pair\n");
        return 1;
    }

    const int patchTableBytes = MOJOSHADER_linkSPIRVShaders(vs->parseData, ps->parseData, nullptr, 0);
    if (patchTableBytes <= 0)
    {
        std::printf("FAIL: MOJOSHADER_linkSPIRVShaders returned %d\n", patchTableBytes);
        return 1;
    }
    const std::size_t vsWords =
        static_cast<std::size_t>(vs->parseData->output_len - patchTableBytes) / sizeof(uint32_t);
    const std::size_t psWords =
        static_cast<std::size_t>(ps->parseData->output_len - patchTableBytes) / sizeof(uint32_t);
    std::printf("Q2: linked; patch table = %d bytes\n", patchTableBytes);
    DumpModule("vertex", vs->parseData, vsWords);
    DumpModule("pixel ", ps->parseData, psWords);

    if (const char* dumpDir = std::getenv("SPIKE_DUMP_DIR"))
    {
        WriteSpv(std::string(dumpDir) + "/spike_vs.spv", vs->parseData->output, vsWords);
        WriteSpv(std::string(dumpDir) + "/spike_ps.spv", ps->parseData->output, psWords);
        std::printf("Q2: wrote spike_vs.spv / spike_ps.spv to %s\n", dumpDir);
    }

    // Route A -- the standard chained WGPUShaderSourceSPIRV, one error scope per module so a
    // failure is attributed to the stage that produced it.
    std::printf("Q2 route A: wgpuDeviceCreateShaderModule + WGPUShaderSourceSPIRV\n");
    const WGPUShaderModule vsModuleA = CreateSpirvModuleA(
        instance, device, "spike-vs-A", vs->parseData->output, vsWords);
    const WGPUShaderModule psModuleA = CreateSpirvModuleA(
        instance, device, "spike-ps-A", ps->parseData->output, psWords);

    // Route B -- wgpu-native's own wgpuDeviceCreateShaderModuleSpirV passthrough entry point.
    std::printf("Q2 route B: wgpuDeviceCreateShaderModuleSpirV (wgpu-native extension)\n");
    {
        ScopeGuard scope(device);
        WGPUShaderModuleDescriptorSpirV descB{};
        descB.label = SV("spike-vs-B");
        descB.sourceSize = static_cast<uint32_t>(vsWords);
        descB.source = reinterpret_cast<const uint32_t*>(vs->parseData->output);
        const WGPUShaderModule vsModuleB = wgpuDeviceCreateShaderModuleSpirV(device, &descB);
        scope.Report(instance, "spike-vs-B", vsModuleB);
    }

    (void) vsModuleA;
    (void) psModuleA;

    // ---------------------------------------------------------------------------------------
    // Q3: the same two modules after the combined image samplers are split into an image and a
    // sampler, which is the one construct WGSL's model cannot express.
    // ---------------------------------------------------------------------------------------
    std::printf("Q3: route A again, after SplitCombinedImageSamplers()\n");
    const std::vector<uint32_t> vsIn(
        reinterpret_cast<const uint32_t*>(vs->parseData->output),
        reinterpret_cast<const uint32_t*>(vs->parseData->output) + vsWords);
    const std::vector<uint32_t> psIn(
        reinterpret_cast<const uint32_t*>(ps->parseData->output),
        reinterpret_cast<const uint32_t*>(ps->parseData->output) + psWords);
    const CnaSpirv::SplitResult vsSplit = CnaSpirv::SplitCombinedImageSamplers(vsIn);
    const CnaSpirv::SplitResult psSplit = CnaSpirv::SplitCombinedImageSamplers(psIn);
    for (const auto* s : {&vsSplit, &psSplit})
    {
        if (!s->error.empty()) std::printf("  split error: %s\n", s->error.c_str());
        for (const auto& b : s->samplers)
        {
            std::printf("  split %s: set %u binding %u -> texture %u, sampler %u (dim %u)\n",
                        b.name.c_str(), b.set, b.originalBinding, b.textureBinding,
                        b.samplerBinding, b.dim);
        }
    }
    if (const char* dumpDir = std::getenv("SPIKE_DUMP_DIR"))
    {
        WriteSpv(std::string(dumpDir) + "/spike_ps_split.spv",
                 reinterpret_cast<const char*>(psSplit.words.data()), psSplit.words.size());
    }

    const WGPUShaderModule vsModule = CreateSpirvModuleA(
        instance, device, "spike-vs-split", reinterpret_cast<const char*>(vsSplit.words.data()),
        vsSplit.words.size());
    const WGPUShaderModule psModule = CreateSpirvModuleA(
        instance, device, "spike-ps-split", reinterpret_cast<const char*>(psSplit.words.data()),
        psSplit.words.size());

    // ---------------------------------------------------------------------------------------
    // Q4: does a pipeline built from that pair actually draw the effect's pixels?
    //
    // The bind-group layout comes straight from MojoShader's four fixed descriptor sets
    // (mojoshader_profile_spirv.h): 0 = VS samplers, 1 = VS uniforms, 2 = PS samplers,
    // 3 = PS uniforms -- which is exactly WebGPU's default maxBindGroups of 4, with the sampler
    // sets now holding two entries per register because of the split above.
    // ---------------------------------------------------------------------------------------
    std::printf("Q4: build a pipeline and render (entry points '%s' / '%s')\n",
                vs->parseData->mainfn, ps->parseData->mainfn);
    std::vector<uint8_t> vsUniformBytes;
    std::vector<uint8_t> psUniformBytes;
    PackUniforms(vs->parseData, ctx.vsRegF, ctx.vsRegI, ctx.vsRegB, vsUniformBytes);
    PackUniforms(ps->parseData, ctx.psRegF, ctx.psRegI, ctx.psRegB, psUniformBytes);
    std::printf("  packed uniforms: vs %d bytes, ps %d bytes\n", (int) vsUniformBytes.size(),
                (int) psUniformBytes.size());
    for (std::size_t u = 0; u + 3 < psUniformBytes.size() / 4; u += 4)
    {
        const float* f = reinterpret_cast<const float*>(psUniformBytes.data()) + u;
        std::printf("  ps c%d = (%g, %g, %g, %g)\n", (int) (u / 4), f[0], f[1], f[2], f[3]);
    }

    const int kSize = 64;
    const Rendered rendered = RenderOnce(instance, device, queue, vsModule, psModule,
                                         vsUniformBytes, psUniformBytes, psSplit, kSize,
                                         vs->parseData->mainfn, ps->parseData->mainfn);
    if (!rendered.ok)
    {
        std::printf("FAIL: render: %s\n", rendered.error.c_str());
        return 1;
    }
    std::printf("Q4: centre pixel = (%u, %u, %u, %u)\n", rendered.centre[0], rendered.centre[1],
                rendered.centre[2], rendered.centre[3]);

    MOJOSHADER_effectEndPass(effect);
    MOJOSHADER_effectEnd(effect);
    MOJOSHADER_deleteEffect(effect);

    // ---------------------------------------------------------------------------------------
    // Q5: sweep every fixture named on the command line -- every technique, every pass.
    // ---------------------------------------------------------------------------------------
    std::printf("Q5: sweep\n");
    int sweepFailures = 0;
    for (int a = 1; a < argc; ++a) sweepFailures += SweepFile(instance, device, argv[a]);
    std::printf("Q5: total failures = %d\n", sweepFailures);

    std::printf("Q6: distinct SPIR-V opcodes emitted across the swept corpus = %zu\n",
                g_opcodeHistogram.size());
    for (const auto& [opcode, count] : g_opcodeHistogram)
        std::printf("  op %-5u x%d\n", opcode, count);
    std::printf("Q6: distinct GLSL.std.450 instructions = %zu\n", g_glslExtHistogram.size());
    for (const auto& [instruction, count] : g_glslExtHistogram)
        std::printf("  glsl450 %-4u x%d\n", instruction, count);
    std::printf("Q6: decorations used\n");
    for (const auto& [decoration, count] : g_decorationHistogram)
        std::printf("  decoration %-4u x%d\n", decoration, count);
    std::printf("Q6: builtins used\n");
    for (const auto& [builtin, count] : g_builtinHistogram)
        std::printf("  builtin %-4u x%d\n", builtin, count);
    std::printf("Q6: storage classes used\n");
    for (const auto& [storage, count] : g_storageClassHistogram)
        std::printf("  storage %-4u x%d\n", storage, count);

    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapterState.adapter);
    wgpuInstanceRelease(instance);
    std::printf("spike finished\n");
    return 0;
}
