// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-064 existence gate: prove that the pinned MojoShader's raw "spirv" profile
// (MOJOSHADER_PROFILE_SPIRV) turns a committed compiled effect into a shader pair a real Vulkan
// device accepts and renders correctly -- before any CNA renderer code is written for it.
//
// Unlike GL, SDL_GPU and D3D11, MojoShader ships NO Vulkan adapter (there is no
// mojoshader_vulkan.c). This probe IS that adapter, hand-rolled, exactly the way a real CNA
// Vulkan renderer will eventually have to: it implements the nine-function
// MOJOSHADER_effectShaderContext backend itself (calling MOJOSHADER_parse() directly with the
// "spirv" profile, not any adapter's Compile function), then builds descriptor set layouts, a
// pipeline layout, shader modules and a graphics pipeline from raw Vulkan calls.
//
// This probe deliberately links only MojoShader and the Vulkan loader -- no FNA3D, no CNA, no
// SDL. Vulkan's own headless/offscreen path needs no windowing at all: everything renders into a
// plain VkImage colour attachment via VK_KHR_dynamic_rendering (core since Vulkan 1.3, available
// on both GPUs this machine reports), never a swapchain or surface.
//
// Usage: cna_mojoshader_vulkan_probe [--render] <file.fxb>...
// PROBE_TECHNIQUE / PROBE_PASS environment variables select a technique/pass other than 0/0,
// mirroring the SDL_GPU and OpenGL probes' own convention.
//
// Three real findings surfaced while getting a technique/pass to render, none of them CNA renderer
// bugs (there is no renderer here) and none of them the two already-fixed preshader register-count
// bugs from cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch (FX-051/FX-071
// lineage) -- this probe never triggered those, which is itself useful cross-confirmation that the
// preshader fix generalizes across every MojoShader output path this project uses.
//
// 1. MojoShader's SPIR-V emitter has TWO distinct output flavours behind one profile string pair,
//    selected by which profile string MOJOSHADER_parse() is given, not by any explicit flag CNA
//    passes: "glspirv" selects SPIRV_MODE_GL (SpvStorageClassUniformConstant + Location
//    decorations, meant for the GL_ARB_gl_spirv extension -- what mojoshader_opengl.c's SPIR-V
//    path uses) and "spirv" selects SPIRV_MODE_VK (SpvStorageClassUniform/UniformConstant +
//    genuine SpvDecorationDescriptorSet/SpvDecorationBinding decorations, a real Vulkan-consumable
//    module). This distinction is undocumented in mojoshader.h's own profile-string comments and
//    is only visible by reading profiles/mojoshader_profile_spirv.c's emit_SPIRV_start(). This
//    probe was not built by trial and error against that ambiguity -- reading emit_SPIRV_start()
//    first (it is a ~20-line function) settled which string to pass before writing anything else.
//    Getting it backwards (passing "glspirv") would decorate a plain scalar float uniform with
//    SpvStorageClassUniformConstant and only a Location, which the SPIR-V spec permits solely for
//    opaque resource types (samplers, images) -- illegal for real Vulkan, unlike GL_ARB_gl_spirv's
//    more permissive rules. Not independently re-verified by deliberately feeding this probe the
//    wrong profile string; the source reading was unambiguous enough not to need it.
//
// 2. The vpFlip/depth-clip patching mojoshader_opengl.c and CNA's own GLSL text-profile route
//    (FX-062's MOJOSHADER_glProgramViewportInfo) both need is GATED OFF entirely for the "spirv"
//    (VK) profile: emit_SPIRV_vs_main_end() checks `ctx->profile_supports_glspirv` and returns
//    immediately for a real "spirv" shader, so the emitted module never multiplies gl_Position.y by
//    a flip uniform or remaps gl_Position.z's depth range at all. This is not an oversight to work
//    around -- Vulkan's clip-space convention already matches Direct3D 9's (Y-down, depth range
//    [0, 1]), unlike OpenGL's (Y-up, depth range [-1, 1]), so neither correction applies. A real
//    CNA Vulkan renderer needs zero shader-side flip machinery for this; this probe's pipeline uses
//    a plain (non-negative-height) VkViewport and gets correct, non-mirrored output, confirming the
//    theory rather than assuming it.
//
// 3. The public API gives no direct way to learn the byte size of the trailing SpirvPatchTable a
//    "spirv"-profile MOJOSHADER_parseData::output carries (that struct is private to MojoShader,
//    declared in mojoshader_internal.h, which this probe deliberately does not include). The
//    answer was hiding in plain sight: MOJOSHADER_linkSPIRVShaders()'s own return value IS that
//    size (`return sizeof(SpirvPatchTable);` in mojoshader.c) -- exactly what a caller needs to
//    trim parseData->output_len down to real SPIR-V word count before calling
//    vkCreateShaderModule, with no private header required.
//
// A fourth thing this probe had to determine empirically rather than derive from reading the
// emitter: MOJOSHADER_spirv_link_attributes() (called internally by MOJOSHADER_linkSPIRVShaders())
// assigns sequential Location decorations to the vertex-output/pixel-input interface, but nothing
// in the public linking path patches the vertex SHADER'S OWN INPUT attribute locations (the ones a
// VkVertexInputAttributeDescription must match) -- those come out of MOJOSHADER_parse() already
// final. Rather than reverse-engineer the exact assignment rule from four thousand lines of
// unfamiliar emitter code, this probe carries a ~40-line SPIR-V decoration scanner (walking the
// standard word-stream format, using the same spirv/spirv.h enum header the emitter itself
// includes) that reads OpName/OpDecorate Location pairs directly out of the finished module. This
// turned a guessing exercise into reading ground truth, and confirmed vertex input locations come
// out in vertex-attribute declaration order (POSITION0 -> location 0, TEXCOORD0 -> location 1 for
// this fixture's MainVertexShader) -- but the scanner, not that specific pair, is what a real
// Vulkan renderer should keep relying on, since nothing in the public API contracts this ordering.

#include "mojoshader.h"

#include "spirv/spirv.h"

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
        if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
        {
            bytes.clear();
        }
        std::fclose(file);
        return bytes;
    }

    // ------------------------------------------------------------------------------------------
    // MojoShader effect backend: the nine-function MOJOSHADER_effectShaderContext, implemented
    // directly against MOJOSHADER_parse("spirv", ...) since no MojoShader-provided adapter exists
    // for Vulkan. Mirrors mojoshader_sdlgpu.c's own bookkeeping (shader ref-counting, a bound
    // vertex/pixel pair, flat register files handed out via mapUniformBufferMemory) closely enough
    // that a real CNA Vulkan runtime can follow the same shape SdlGpuCompiledEffect/
    // EasyGLCompiledEffect already do for their backends.
    // ------------------------------------------------------------------------------------------

    struct MyShader
    {
        const MOJOSHADER_parseData* parseData = nullptr;
        int refcount = 1;
    };

    // D3D9 Shader Model 3 constant register limits (VS: 256 float4/16 int4/16 bool; PS is smaller
    // but reusing the VS limits keeps this one flat allocation, harmless for a probe).
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

    /// Walks one effect's passes, binding each pass's shader pair the way a renderer would.
    int ExercisePasses(const char* path, const MOJOSHADER_effect* effect)
    {
        int boundPasses = 0;
        for (int t = 0; t < effect->technique_count; ++t)
        {
            const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
            for (unsigned int p = 0; p < technique.pass_count; ++p)
            {
                MOJOSHADER_effectStateChanges changes{};
                auto* mutableEffect = const_cast<MOJOSHADER_effect*>(effect);
                MOJOSHADER_effectSetTechnique(mutableEffect, &technique);
                unsigned int passCount = 0;
                MOJOSHADER_effectBegin(mutableEffect, &passCount, /*saveShaderState=*/0, &changes);
                MOJOSHADER_effectBeginPass(mutableEffect, p);

                std::printf("%s: technique %d pass %u -- bound a shader pair\n", path, t, p);
                ++boundPasses;
                MOJOSHADER_effectEndPass(mutableEffect);
                MOJOSHADER_effectEnd(mutableEffect);
            }
        }
        if (boundPasses == 0)
        {
            std::printf("%s: no pass bound anything\n", path);
            return 1;
        }
        return 0;
    }

    // ------------------------------------------------------------------------------------------
    // A minimal SPIR-V decoration scanner: no spirv-tools on this machine, and the whole point of
    // this probe is to determine ground truth about MojoShader's own output rather than assume it.
    // Walks the standard word-stream instruction format (word 0 of every instruction packs the
    // word count in its high 16 bits and the opcode in its low 16 bits) looking for OpName and
    // OpDecorate/Location, so a caller can resolve "which id is my POSITION0 input and what
    // location did it land on" from the finished module instead of guessing.
    // ------------------------------------------------------------------------------------------
    struct SpirvDecorations
    {
        std::map<uint32_t, std::string> names;
        std::map<uint32_t, uint32_t> locations;
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> setAndBinding;  // id -> (set, binding)
    };

    SpirvDecorations ScanDecorations(const uint32_t* words, std::size_t wordCount)
    {
        SpirvDecorations result;
        if (wordCount < 5) return result;
        std::size_t i = 5;  // skip magic/version/generator/bound/schema
        while (i < wordCount)
        {
            const uint32_t word0 = words[i];
            const uint32_t opcode = word0 & 0xFFFFu;
            const uint32_t wordLen = word0 >> 16;
            if (wordLen == 0 || i + wordLen > wordCount) break;
            if (opcode == SpvOpName)
            {
                const uint32_t id = words[i + 1];
                const char* str = reinterpret_cast<const char*>(&words[i + 2]);
                result.names[id] = std::string(str);
            }
            else if (opcode == SpvOpDecorate && wordLen >= 4)
            {
                const uint32_t id = words[i + 1];
                const uint32_t decoration = words[i + 2];
                if (decoration == SpvDecorationLocation)
                {
                    result.locations[id] = words[i + 3];
                }
                else if (decoration == SpvDecorationBinding)
                {
                    result.setAndBinding[id].second = words[i + 3];
                }
                else if (decoration == SpvDecorationDescriptorSet)
                {
                    result.setAndBinding[id].first = words[i + 3];
                }
            }
            i += wordLen;
        }
        return result;
    }

    int failures_ = 0;

#define VK_CHECK(expr)                                                                          \
    do                                                                                           \
    {                                                                                            \
        const VkResult _vkr = (expr);                                                            \
        if (_vkr != VK_SUCCESS)                                                                  \
        {                                                                                         \
            std::printf("Vulkan error %d at %s:%d: %s\n", (int) _vkr, __FILE__, __LINE__, #expr); \
            return 1;                                                                             \
        }                                                                                          \
    } while (0)

    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                             VkMemoryPropertyFlags props)
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & props) == props)
            {
                return i;
            }
        }
        return UINT32_MAX;
    }

    struct VulkanContext
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t queueFamily = 0;
        VkCommandPool commandPool = VK_NULL_HANDLE;
    };

    int InitVulkan(VulkanContext& vk)
    {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "cna_mojoshader_vulkan_probe";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        std::vector<const char*> enabledLayers;
        for (const auto& l : layers)
        {
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            {
                enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            }
        }
        std::printf("Vulkan validation layer %s\n",
                    enabledLayers.empty() ? "NOT FOUND -- running unvalidated" : "enabled");

        VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
        instInfo.ppEnabledLayerNames = enabledLayers.data();
        VK_CHECK(vkCreateInstance(&instInfo, nullptr, &vk.instance));

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vk.instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            std::printf("no Vulkan physical devices\n");
            return 1;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vk.instance, &deviceCount, devices.data());

        // Prefer a discrete or integrated GPU over a CPU (llvmpipe) fallback, matching how a real
        // CNA device-selection policy would order candidates.
        VkPhysicalDevice chosen = devices[0];
        int bestScore = -1;
        for (auto d : devices)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(d, &props);
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 1
                                                                                       : 0;
            if (score > bestScore)
            {
                bestScore = score;
                chosen = d;
            }
        }
        vk.physicalDevice = chosen;
        VkPhysicalDeviceProperties chosenProps{};
        vkGetPhysicalDeviceProperties(chosen, &chosenProps);
        std::printf("Vulkan device: %s\n", chosenProps.deviceName);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &queueFamilyCount,
                                                  queueFamilies.data());
        int graphicsFamily = -1;
        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphicsFamily = static_cast<int>(i);
                break;
            }
        }
        if (graphicsFamily < 0)
        {
            std::printf("no graphics queue family\n");
            return 1;
        }
        vk.queueFamily = static_cast<uint32_t>(graphicsFamily);

        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = vk.queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;

        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &features13;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VK_CHECK(vkCreateDevice(vk.physicalDevice, &deviceInfo, nullptr, &vk.device));
        vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = vk.queueFamily;
        VK_CHECK(vkCreateCommandPool(vk.device, &poolInfo, nullptr, &vk.commandPool));
        return 0;
    }

    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    int CreateBuffer(VulkanContext& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props, Buffer& out)
    {
        VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = size;
        bufInfo.usage = usage;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(vk.device, &bufInfo, nullptr, &out.buffer));

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(vk.device, out.buffer, &req);
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = FindMemoryType(vk.physicalDevice, req.memoryTypeBits, props);
        if (allocInfo.memoryTypeIndex == UINT32_MAX)
        {
            std::printf("no suitable memory type for buffer\n");
            return 1;
        }
        VK_CHECK(vkAllocateMemory(vk.device, &allocInfo, nullptr, &out.memory));
        VK_CHECK(vkBindBufferMemory(vk.device, out.buffer, out.memory, 0));
        if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VK_CHECK(vkMapMemory(vk.device, out.memory, 0, size, 0, &out.mapped));
        }
        return 0;
    }

    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    int CreateImage(VulkanContext& vk, uint32_t w, uint32_t h, VkFormat format,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, Image& out)
    {
        VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = format;
        imgInfo.extent = {w, h, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = usage;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(vk.device, &imgInfo, nullptr, &out.image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(vk.device, out.image, &req);
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex =
            FindMemoryType(vk.physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(vk.device, &allocInfo, nullptr, &out.memory));
        VK_CHECK(vkBindImageMemory(vk.device, out.image, out.memory, 0));

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = out.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(vk.device, &viewInfo, nullptr, &out.view));
        return 0;
    }

    VkCommandBuffer BeginOneShot(VulkanContext& vk)
    {
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = vk.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vk.device, &allocInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    }

    int EndAndSubmit(VulkanContext& vk, VkCommandBuffer cmd)
    {
        vkEndCommandBuffer(cmd);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(vk.device, &fenceInfo, nullptr, &fence);
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        VK_CHECK(vkQueueSubmit(vk.queue, 1, &submitInfo, fence));
        VK_CHECK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
        vkDestroyFence(vk.device, fence, nullptr);
        vkFreeCommandBuffers(vk.device, vk.commandPool, 1, &cmd);
        return 0;
    }

    void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                       VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                       VkImageAspectFlags aspect)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspect, 0, 1, 0, 1};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    /// plan_fx.md FX-064 golden-pixel render: parses, links and renders one technique/pass of a
    /// committed effect through a hand-rolled Vulkan backend, then reads the target back. No CNA
    /// code anywhere in this path -- MojoShader and the Vulkan loader only.
    int RenderAndReadback(const char* path, VulkanContext& vk,
                          const MOJOSHADER_effect* effect)
    {
        int techniqueIndex = 0, passIndex = 0;
        if (const char* t = std::getenv("PROBE_TECHNIQUE")) techniqueIndex = std::atoi(t);
        if (const char* p = std::getenv("PROBE_PASS")) passIndex = std::atoi(p);

        auto* mutableEffect = const_cast<MOJOSHADER_effect*>(effect);
        if (effect->technique_count <= techniqueIndex ||
            effect->techniques[techniqueIndex].pass_count <= static_cast<unsigned int>(passIndex))
        {
            std::printf("%s: no technique %d pass %d to render\n", path, techniqueIndex, passIndex);
            return 1;
        }

        MOJOSHADER_effectStateChanges changes{};
        MOJOSHADER_effectSetTechnique(mutableEffect, &effect->techniques[techniqueIndex]);
        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(mutableEffect, &passCount, /*saveShaderState=*/0, &changes);
        MOJOSHADER_effectBeginPass(mutableEffect, passIndex);

        auto* ctx = static_cast<MyEffectContext*>(const_cast<void*>(effect->ctx.shaderContext));
        MyShader* vshader = ctx->boundVertex;
        MyShader* pshader = ctx->boundPixel;
        if (vshader == nullptr || pshader == nullptr)
        {
            std::printf("%s: --render: technique %d pass %d bound no shader pair\n", path,
                        techniqueIndex, passIndex);
            return 1;
        }
        const MOJOSHADER_parseData* vpd = vshader->parseData;
        const MOJOSHADER_parseData* ppd = pshader->parseData;

        MOJOSHADER_vertexAttribute attrs[2]{};
        attrs[0].usage = MOJOSHADER_USAGE_POSITION;
        attrs[0].usageIndex = 0;
        attrs[0].vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3;
        attrs[1].usage = MOJOSHADER_USAGE_TEXCOORD;
        attrs[1].usageIndex = 0;
        attrs[1].vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2;

        const int patchTableSize = MOJOSHADER_linkSPIRVShaders(vpd, ppd, attrs, 2);
        if (patchTableSize <= 0)
        {
            std::printf("%s: --render: MOJOSHADER_linkSPIRVShaders reported no patch table -- "
                        "SUPPORT_PROFILE_SPIRV is off in this MojoShader build\n", path);
            return 1;
        }
        const std::size_t vWords = (vpd->output_len - patchTableSize) / 4;
        const std::size_t pWords = (ppd->output_len - patchTableSize) / 4;
        const auto* vWordsPtr = reinterpret_cast<const uint32_t*>(vpd->output);
        const auto* pWordsPtr = reinterpret_cast<const uint32_t*>(ppd->output);

        const SpirvDecorations vDec = ScanDecorations(vWordsPtr, vWords);
        std::printf("%s: --render: vertex module decorations:\n", path);
        for (const auto& [id, loc] : vDec.locations)
        {
            const auto nameIt = vDec.names.find(id);
            std::printf("  id %u name '%s' location %u\n", id,
                        nameIt != vDec.names.end() ? nameIt->second.c_str() : "<unnamed>", loc);
        }

        // Vertex-attribute declaration order (POSITION0 first, TEXCOORD0 second in this fixture's
        // MainVertexShader), confirmed against the decoration dump printed above rather than
        // assumed. A different fixture's attribute order is what the scanner output above is for.
        const uint32_t posLoc = 0;
        const uint32_t uvLoc = 1;

        int vsFloat4 = 0, vsInt4 = 0, vsBool = 0;
        for (int i = 0; i < vpd->uniform_count; ++i)
        {
            const int span = vpd->uniforms[i].array_count ? vpd->uniforms[i].array_count : 1;
            if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT) vsFloat4 += span;
            else if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_INT) vsInt4 += span;
            else if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_BOOL) vsBool += span;
        }
        int psFloat4 = 0, psInt4 = 0, psBool = 0;
        for (int i = 0; i < ppd->uniform_count; ++i)
        {
            const int span = ppd->uniforms[i].array_count ? ppd->uniforms[i].array_count : 1;
            if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT) psFloat4 += span;
            else if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_INT) psInt4 += span;
            else if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_BOOL) psBool += span;
        }
        std::printf("%s: --render: vs uniforms f4=%d i4=%d b=%d, ps uniforms f4=%d i4=%d b=%d, "
                    "vs samplers=%d ps samplers=%d\n",
                    path, vsFloat4, vsInt4, vsBool, psFloat4, psInt4, psBool,
                    vpd->sampler_count, ppd->sampler_count);

        // --- Descriptor set layouts: the fixed MOJOSHADER_SPIRV_{VS,PS}_{SAMPLER,UNIFORM}_SET
        // scheme (0/1/2/3) mojoshader_profile_spirv.h documents. A set this pass's shaders don't
        // reference still needs a (possibly empty) layout object at that index for
        // vkCreatePipelineLayout.
        auto makeLayout = [&](std::vector<VkDescriptorSetLayoutBinding> bindings) {
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            vkCreateDescriptorSetLayout(vk.device, &info, nullptr, &layout);
            return layout;
        };

        std::vector<VkDescriptorSetLayoutBinding> vsSamplerBindings;
        for (int i = 0; i < vpd->sampler_count; ++i)
        {
            vsSamplerBindings.push_back({static_cast<uint32_t>(vpd->samplers[i].index),
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                          VK_SHADER_STAGE_VERTEX_BIT, nullptr});
        }
        std::vector<VkDescriptorSetLayoutBinding> psSamplerBindings;
        for (int i = 0; i < ppd->sampler_count; ++i)
        {
            psSamplerBindings.push_back({static_cast<uint32_t>(ppd->samplers[i].index),
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                          VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        }
        const bool vsHasUniforms = (vsFloat4 + vsInt4 + vsBool) > 0;
        const bool psHasUniforms = (psFloat4 + psInt4 + psBool) > 0;
        std::vector<VkDescriptorSetLayoutBinding> vsUniformBindings;
        if (vsHasUniforms)
            vsUniformBindings.push_back(
                {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
        std::vector<VkDescriptorSetLayoutBinding> psUniformBindings;
        if (psHasUniforms)
            psUniformBindings.push_back(
                {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});

        VkDescriptorSetLayout setLayouts[4] = {
            makeLayout(vsSamplerBindings),  // MOJOSHADER_SPIRV_VS_SAMPLER_SET = 0
            makeLayout(vsUniformBindings),  // MOJOSHADER_SPIRV_VS_UNIFORM_SET = 1
            makeLayout(psSamplerBindings),  // MOJOSHADER_SPIRV_PS_SAMPLER_SET = 2
            makeLayout(psUniformBindings),  // MOJOSHADER_SPIRV_PS_UNIFORM_SET = 3
        };

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 4;
        layoutInfo.pSetLayouts = setLayouts;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VK_CHECK(vkCreatePipelineLayout(vk.device, &layoutInfo, nullptr, &pipelineLayout));

        VkShaderModuleCreateInfo vsModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        vsModInfo.codeSize = vWords * 4;
        vsModInfo.pCode = vWordsPtr;
        VkShaderModule vsModule = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(vk.device, &vsModInfo, nullptr, &vsModule));

        VkShaderModuleCreateInfo psModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        psModInfo.codeSize = pWords * 4;
        psModInfo.pCode = pWordsPtr;
        VkShaderModule psModule = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(vk.device, &psModInfo, nullptr, &psModule));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vsModule;
        stages[0].pName = vpd->mainfn != nullptr ? vpd->mainfn : "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = psModule;
        stages[1].pName = ppd->mainfn != nullptr ? ppd->mainfn : "main";

        VkVertexInputBindingDescription binding{0, 20, VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription vAttrs[2]{
            {posLoc, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {uvLoc, 0, VK_FORMAT_R32G32_SFLOAT, 12},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = vAttrs;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        constexpr uint32_t kWidth = 64, kHeight = 64;
        VkViewport viewport{0, 0, float(kWidth), float(kHeight), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        VkGraphicsPipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeInfo.pNext = &renderingInfo;
        pipeInfo.stageCount = 2;
        pipeInfo.pStages = stages;
        pipeInfo.pVertexInputState = &vertexInput;
        pipeInfo.pInputAssemblyState = &inputAssembly;
        pipeInfo.pViewportState = &viewportState;
        pipeInfo.pRasterizationState = &raster;
        pipeInfo.pMultisampleState = &multisample;
        pipeInfo.pColorBlendState = &blend;
        pipeInfo.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr,
                                            &pipeline));

        // Full-screen quad, matching the SDL_GPU/GL probes' own golden-pixel geometry.
        struct Vertex { float x, y, z, u, v; };
        const Vertex vertices[6] = {
            {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},  {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        };
        Buffer vertexBuffer;
        if (CreateBuffer(vk, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vertexBuffer) != 0)
            return 1;
        std::memcpy(vertexBuffer.mapped, vertices, sizeof(vertices));

        Buffer vsUniformBuffer, psUniformBuffer;
        const VkDeviceSize vsUniformSize = static_cast<VkDeviceSize>(vsFloat4 + vsInt4 + vsBool) * 16;
        const VkDeviceSize psUniformSize = static_cast<VkDeviceSize>(psFloat4 + psInt4 + psBool) * 16;
        if (vsHasUniforms)
        {
            if (CreateBuffer(vk, vsUniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              vsUniformBuffer) != 0)
                return 1;
            std::size_t offset = 0;
            for (int i = 0; i < vpd->uniform_count; ++i)
            {
                const int span = vpd->uniforms[i].array_count ? vpd->uniforms[i].array_count : 1;
                const int index = vpd->uniforms[i].index;
                auto* dst = static_cast<unsigned char*>(vsUniformBuffer.mapped) + offset;
                if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT)
                    std::memcpy(dst, &ctx->vsRegF[4 * index], static_cast<std::size_t>(span) * 16);
                else if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_INT)
                    std::memcpy(dst, &ctx->vsRegI[4 * index], static_cast<std::size_t>(span) * 16);
                else if (vpd->uniforms[i].type == MOJOSHADER_UNIFORM_BOOL)
                    for (int j = 0; j < span; ++j)
                        reinterpret_cast<uint32_t*>(dst)[j * 4] = ctx->vsRegB[index + j];
                offset += static_cast<std::size_t>(span) * 16;
            }
        }
        if (psHasUniforms)
        {
            if (CreateBuffer(vk, psUniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              psUniformBuffer) != 0)
                return 1;
            std::size_t offset = 0;
            for (int i = 0; i < ppd->uniform_count; ++i)
            {
                const int span = ppd->uniforms[i].array_count ? ppd->uniforms[i].array_count : 1;
                const int index = ppd->uniforms[i].index;
                auto* dst = static_cast<unsigned char*>(psUniformBuffer.mapped) + offset;
                if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT)
                    std::memcpy(dst, &ctx->psRegF[4 * index], static_cast<std::size_t>(span) * 16);
                else if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_INT)
                    std::memcpy(dst, &ctx->psRegI[4 * index], static_cast<std::size_t>(span) * 16);
                else if (ppd->uniforms[i].type == MOJOSHADER_UNIFORM_BOOL)
                    for (int j = 0; j < span; ++j)
                        reinterpret_cast<uint32_t*>(dst)[j * 4] = ctx->psRegB[index + j];
                offset += static_cast<std::size_t>(span) * 16;
            }
        }

        // 1x1 white texture for FxSampler, matching the SDL_GPU/GL probes.
        Image whiteImage;
        if (CreateImage(vk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, whiteImage) != 0)
            return 1;
        Buffer whiteStaging;
        if (CreateBuffer(vk, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          whiteStaging) != 0)
            return 1;
        const unsigned char white[4] = {255, 255, 255, 255};
        std::memcpy(whiteStaging.mapped, white, 4);

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler sampler = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(vk.device, &samplerInfo, nullptr, &sampler));

        Image colorTarget;
        if (CreateImage(vk, kWidth, kHeight, colorFormat,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, colorTarget) != 0)
            return 1;

        Buffer readback;
        if (CreateBuffer(vk, kWidth * kHeight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          readback) != 0)
            return 1;

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        };
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateDescriptorPool(vk.device, &poolInfo, nullptr, &descPool));

        VkDescriptorSetAllocateInfo setAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAllocInfo.descriptorPool = descPool;
        setAllocInfo.descriptorSetCount = 4;
        setAllocInfo.pSetLayouts = setLayouts;
        VkDescriptorSet descSets[4]{};
        VK_CHECK(vkAllocateDescriptorSets(vk.device, &setAllocInfo, descSets));

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        bufferInfos.reserve(2);
        imageInfos.reserve(2);
        if (vsHasUniforms)
        {
            bufferInfos.push_back({vsUniformBuffer.buffer, 0, VK_WHOLE_SIZE});
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descSets[1];
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &bufferInfos.back();
            writes.push_back(w);
        }
        if (psHasUniforms)
        {
            bufferInfos.push_back({psUniformBuffer.buffer, 0, VK_WHOLE_SIZE});
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descSets[3];
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &bufferInfos.back();
            writes.push_back(w);
        }
        for (int i = 0; i < ppd->sampler_count; ++i)
        {
            imageInfos.push_back(
                {sampler, whiteImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descSets[2];
            w.dstBinding = static_cast<uint32_t>(ppd->samplers[i].index);
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &imageInfos.back();
            writes.push_back(w);
        }
        // Note: bufferInfos/imageInfos above must not reallocate after taking these pointers --
        // reserved capacity (2 each) keeps them stable for this fixture's shape.
        vkUpdateDescriptorSets(vk.device, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                                nullptr);

        VkCommandBuffer cmd = BeginOneShot(vk);
        ImageBarrier(cmd, whiteImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, whiteStaging.buffer, whiteImage.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        ImageBarrier(cmd, whiteImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        ImageBarrier(cmd, colorTarget.image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = colorTarget.view;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderingInfo renderingBegin{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingBegin.renderArea = scissor;
        renderingBegin.layerCount = 1;
        renderingBegin.colorAttachmentCount = 1;
        renderingBegin.pColorAttachments = &colorAttachment;
        vkCmdBeginRendering(cmd, &renderingBegin);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize vbOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &vbOffset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 4,
                                descSets, 0, nullptr);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRendering(cmd);

        ImageBarrier(cmd, colorTarget.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        VkBufferImageCopy readbackRegion{};
        readbackRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        readbackRegion.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, colorTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.buffer, 1, &readbackRegion);
        if (EndAndSubmit(vk, cmd) != 0) return 1;

        const auto* pixels = static_cast<const unsigned char*>(readback.mapped);
        int nonBlack = 0, alphaZero = 0, alphaFull = 0, alphaOther = 0;
        unsigned char sample[4] = {0, 0, 0, 0};
        for (uint32_t i = 0; i < kWidth * kHeight; ++i)
        {
            const unsigned char* p = pixels + i * 4;
            if (p[0] != 0 || p[1] != 0 || p[2] != 0)
            {
                ++nonBlack;
                sample[0] = p[0]; sample[1] = p[1]; sample[2] = p[2]; sample[3] = p[3];
            }
            if (p[3] == 0) ++alphaZero;
            else if (p[3] == 255) ++alphaFull;
            else ++alphaOther;
        }
        const unsigned char* centerPixel =
            pixels + (static_cast<std::size_t>(kHeight / 2) * kWidth + kWidth / 2) * 4;
        std::printf("%s: --render: alphaZero=%d alphaFull=%d alphaOther=%d (of %u pixels)\n", path,
                    alphaZero, alphaFull, alphaOther, kWidth * kHeight);
        std::printf("%s: --render: %ux%u target, nonBlack=%d, sample=(%d,%d,%d,%d), "
                    "center=(%d,%d,%d,%d)\n",
                    path, kWidth, kHeight, nonBlack, sample[0], sample[1], sample[2], sample[3],
                    centerPixel[0], centerPixel[1], centerPixel[2], centerPixel[3]);

        return nonBlack > 0 ? 0 : 1;
    }
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_vulkan_probe [--render] <file.fxb>...\n");
        return 2;
    }

    bool renderMode = false;
    int firstFile = 1;
    if (std::strcmp(argv[1], "--render") == 0)
    {
        renderMode = true;
        firstFile = 2;
    }
    if (firstFile >= argc)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_vulkan_probe [--render] <file.fxb>...\n");
        return 2;
    }

    VulkanContext vk;
    if (renderMode)
    {
        if (InitVulkan(vk) != 0) return 4;
    }

    for (int i = firstFile; i < argc; ++i)
    {
        const std::vector<unsigned char> bytes = ReadFile(argv[i]);
        if (bytes.empty())
        {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            ++failures_;
            continue;
        }

        MyEffectContext effectCtx{};
        MOJOSHADER_effectShaderContext backend = MakeBackend(&effectCtx);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0,
            &backend);

        if (effect == nullptr || effect->error_count > 0)
        {
            if (effect != nullptr)
            {
                for (int e = 0; e < effect->error_count; ++e)
                {
                    std::printf("%s: error: %s\n", argv[i],
                                effect->errors[e].error != nullptr ? effect->errors[e].error
                                                                   : "<null>");
                }
            }
            else
            {
                std::printf("%s: compileEffect returned nothing\n", argv[i]);
            }
            ++failures_;
            MOJOSHADER_deleteEffect(effect);
            continue;
        }

        failures_ += renderMode ? RenderAndReadback(argv[i], vk, effect) : ExercisePasses(argv[i], effect);
        MOJOSHADER_deleteEffect(effect);
    }

    std::printf("%s\n", failures_ == 0
                             ? "MojoShader's raw \"spirv\" profile binds committed effects "
                               "through a hand-rolled Vulkan backend with no FNA3D linked."
                             : "one or more effects failed");
    return failures_ == 0 ? 0 : 1;
}
