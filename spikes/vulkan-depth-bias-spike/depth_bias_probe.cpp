// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-091: does vkCmdSetDepthBias's CONSTANT factor do anything on this
// machine's Vulkan drivers?
//
// Vulkan_DepthBias fails one leg of four: a flat, coplanar redraw with
// RasterizerState.DepthBias = -1e6 does not move in front of the first draw, while the tilted
// SlopeScaleDepthBias leg of the same test, through the same pipeline and the same
// vkCmdSetDepthBias call, does. The plan forbids re-attributing that to the driver "without
// reproducing the pass on a second driver", and the renderer itself cannot reach the second driver
// under Xvfb -- RADV refuses presentation without DRI3, so PickPhysicalDevice rejects it.
//
// Presentation is the only thing that blocks it. This probe therefore does the same coplanar
// experiment OFF SCREEN, with no surface and no swapchain, on EVERY physical device the loader
// offers, and reads the answer back with vkCmdCopyImageToBuffer. That is what makes RADV
// reachable here at all.
//
// Per device it reports four legs, the same four Vulkan_DepthBias uses:
//   flat   + constant 0      -> the coplanar redraw must FAIL the LESS test (stays red)
//   flat   + constant -1e6   -> the redraw must PASS  (turns green)
//   tilted + slope 0         -> stays red
//   tilted + slope -2000     -> turns green
//
// Build:  g++ -std=c++17 -O1 depth_bias_probe.cpp -lvulkan -o depth_bias_probe
//         (ccache g++ ... works too; see README.md)
// Run:    ./depth_bias_probe

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "probe_spirv.hpp"

namespace
{
    constexpr uint32_t kSize = 32;

    struct PushConst { float color[4]; float z[4]; };

    #define VKCHECK(expr)                                                              \
        do {                                                                           \
            const VkResult vkr_ = (expr);                                              \
            if (vkr_ != VK_SUCCESS) {                                                  \
                std::printf("  [ERR] %s -> VkResult %d\n", #expr, int(vkr_));           \
                return false;                                                          \
            }                                                                          \
        } while (0)

    uint32_t FindMemoryType(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
                return i;
        return UINT32_MAX;
    }

    const char* FormatName(VkFormat f)
    {
        switch (f) {
            case VK_FORMAT_D24_UNORM_S8_UINT:  return "D24_UNORM_S8_UINT (fixed point, r = 2^-24)";
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT (float, r per-primitive)";
            case VK_FORMAT_D32_SFLOAT:         return "D32_SFLOAT (float, r per-primitive)";
            case VK_FORMAT_D16_UNORM:          return "D16_UNORM (fixed point, r = 2^-16)";
            default:                           return "other";
        }
    }

    // One whole offscreen device, built and torn down per physical device.
    struct Probe
    {
        VkPhysicalDevice pd     = VK_NULL_HANDLE;
        VkDevice         dev    = VK_NULL_HANDLE;
        VkQueue          queue  = VK_NULL_HANDLE;
        uint32_t         family = 0;
        VkFormat         depthFormat = VK_FORMAT_UNDEFINED;

        VkImage        colorImage = VK_NULL_HANDLE, depthImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMem   = VK_NULL_HANDLE, depthMem   = VK_NULL_HANDLE;
        VkImageView    colorView  = VK_NULL_HANDLE, depthView  = VK_NULL_HANDLE;
        VkRenderPass   pass       = VK_NULL_HANDLE;
        VkFramebuffer  fb         = VK_NULL_HANDLE;
        VkPipelineLayout layout   = VK_NULL_HANDLE;
        VkPipeline     pipe       = VK_NULL_HANDLE;
        VkCommandPool  pool       = VK_NULL_HANDLE;
        VkBuffer       readback   = VK_NULL_HANDLE;
        VkDeviceMemory readbackMem= VK_NULL_HANDLE;

        bool Create();
        bool RunLeg(float constantFactor, float slopeFactor, bool tilted, uint8_t out[4]);
        void Destroy();
    };

    bool Probe::Create()
    {
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qf.data());
        bool found = false;
        for (uint32_t i = 0; i < qc; ++i)
            if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; found = true; break; }
        if (!found) { std::printf("  [SKIP] no graphics queue family\n"); return false; }

        const float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        VKCHECK(vkCreateDevice(pd, &di, nullptr, &dev));
        vkGetDeviceQueue(dev, family, 0, &queue);

        // The renderer's own FindDepthFormat() order: stencil-capable first.
        for (VkFormat f : { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                            VK_FORMAT_D32_SFLOAT }) {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(pd, f, &fp);
            if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = f; break;
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED) { std::printf("  [SKIP] no depth format\n"); return false; }

        auto makeImage = [&](VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                             VkImage& img, VkDeviceMemory& mem, VkImageView& view) -> bool {
            VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
            ii.extent = {kSize, kSize, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = usage; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VKCHECK(vkCreateImage(dev, &ii, nullptr, &img));
            VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(dev, img, &mr);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = FindMemoryType(pd, mr.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VKCHECK(vkAllocateMemory(dev, &ai, nullptr, &mem));
            VKCHECK(vkBindImageMemory(dev, img, mem, 0));
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
            vi.subresourceRange = {aspect, 0, 1, 0, 1};
            VKCHECK(vkCreateImageView(dev, &vi, nullptr, &view));
            return true;
        };

        if (!makeImage(VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, colorImage, colorMem, colorView)) return false;
        if (!makeImage(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, depthImage, depthMem, depthView)) return false;

        VkAttachmentDescription att[2]{};
        att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        att[1] = att[0];
        att[1].format = depthFormat;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &colorRef;
        sp.pDepthStencilAttachment = &depthRef;
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 2; rpi.pAttachments = att;
        rpi.subpassCount = 1; rpi.pSubpasses = &sp;
        VKCHECK(vkCreateRenderPass(dev, &rpi, nullptr, &pass));

        const VkImageView views[2] = { colorView, depthView };
        VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbi.renderPass = pass; fbi.attachmentCount = 2; fbi.pAttachments = views;
        fbi.width = kSize; fbi.height = kSize; fbi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev, &fbi, nullptr, &fb));

        VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(PushConst)};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        VKCHECK(vkCreatePipelineLayout(dev, &pli, nullptr, &layout));

        auto makeModule = [&](const uint32_t* code, size_t bytes, VkShaderModule& out) -> bool {
            VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            si.codeSize = bytes; si.pCode = code;
            VKCHECK(vkCreateShaderModule(dev, &si, nullptr, &out));
            return true;
        };
        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        if (!makeModule(kProbeVertSpv, kProbeVertSpv_size, vs)) return false;
        if (!makeModule(kProbeFragSpv, kProbeFragSpv_size, fs)) return false;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0, 0, float(kSize), float(kSize), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, {kSize, kSize}};
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        rs.depthBiasEnable = VK_TRUE;   // values supplied dynamically, exactly as the renderer does
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;    // Vulkan_DepthBias's own explicit choice
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        const VkDynamicState dyn[] = { VK_DYNAMIC_STATE_DEPTH_BIAS };
        VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsi.dynamicStateCount = 1; dsi.pDynamicStates = dyn;

        VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gpi.stageCount = 2; gpi.pStages = stages;
        gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
        gpi.pViewportState = &vps; gpi.pRasterizationState = &rs;
        gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds;
        gpi.pColorBlendState = &cb; gpi.pDynamicState = &dsi;
        gpi.layout = layout; gpi.renderPass = pass; gpi.subpass = 0;
        VKCHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipe));
        vkDestroyShaderModule(dev, vs, nullptr);
        vkDestroyShaderModule(dev, fs, nullptr);

        VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpi.queueFamilyIndex = family;
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VKCHECK(vkCreateCommandPool(dev, &cpi, nullptr, &pool));

        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kSize * kSize * 4; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKCHECK(vkCreateBuffer(dev, &bi, nullptr, &readback));
        VkMemoryRequirements br{}; vkGetBufferMemoryRequirements(dev, readback, &br);
        VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        bai.allocationSize = br.size;
        bai.memoryTypeIndex = FindMemoryType(pd, br.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VKCHECK(vkAllocateMemory(dev, &bai, nullptr, &readbackMem));
        VKCHECK(vkBindBufferMemory(dev, readback, readbackMem, 0));
        return true;
    }

    bool Probe::RunLeg(float constantFactor, float slopeFactor, bool tilted, uint8_t out[4])
    {
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VKCHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
        VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKCHECK(vkBeginCommandBuffer(cmd, &cbi));

        VkClearValue clears[2]{};
        clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = pass; rbi.framebuffer = fb;
        rbi.renderArea = {{0, 0}, {kSize, kSize}};
        rbi.clearValueCount = 2; rbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

        // Vulkan_DepthBias's own geometry: flat sits at depth 0.5 everywhere (slope 0), tilted
        // runs 0.2 at the apex to 0.8 at the base so the slope term has something to scale.
        PushConst pc{};
        pc.z[0] = tilted ? 0.8f : 0.5f;
        pc.z[1] = tilted ? 0.8f : 0.5f;
        pc.z[2] = tilted ? 0.2f : 0.5f;
        pc.z[3] = 0.0f;

        // Draw A: red, no bias, writes depth.
        pc.color[0] = 1.0f; pc.color[1] = 0.0f; pc.color[2] = 0.0f; pc.color[3] = 1.0f;
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        // Draw B: green, identical geometry, biased. Under LESS it can only appear if the bias
        // actually moved it toward the camera.
        pc.color[0] = 0.0f; pc.color[1] = 1.0f;
        vkCmdSetDepthBias(cmd, constantFactor, 0.0f, slopeFactor);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kSize, kSize, 1};
        vkCmdCopyImageToBuffer(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback, 1, &region);
        VKCHECK(vkEndCommandBuffer(cmd));

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        VKCHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VKCHECK(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(dev, pool, 1, &cmd);

        void* mapped = nullptr;
        VKCHECK(vkMapMemory(dev, readbackMem, 0, VK_WHOLE_SIZE, 0, &mapped));
        // A point safely inside the triangle for both shapes.
        const size_t idx = (size_t(kSize) * (kSize / 2) + kSize / 2) * 4;
        std::memcpy(out, static_cast<const uint8_t*>(mapped) + idx, 4);
        vkUnmapMemory(dev, readbackMem);
        return true;
    }

    void Probe::Destroy()
    {
        if (dev == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(dev);
        if (readback)    vkDestroyBuffer(dev, readback, nullptr);
        if (readbackMem) vkFreeMemory(dev, readbackMem, nullptr);
        if (pool)        vkDestroyCommandPool(dev, pool, nullptr);
        if (pipe)        vkDestroyPipeline(dev, pipe, nullptr);
        if (layout)      vkDestroyPipelineLayout(dev, layout, nullptr);
        if (fb)          vkDestroyFramebuffer(dev, fb, nullptr);
        if (pass)        vkDestroyRenderPass(dev, pass, nullptr);
        if (colorView)   vkDestroyImageView(dev, colorView, nullptr);
        if (depthView)   vkDestroyImageView(dev, depthView, nullptr);
        if (colorImage)  vkDestroyImage(dev, colorImage, nullptr);
        if (depthImage)  vkDestroyImage(dev, depthImage, nullptr);
        if (colorMem)    vkFreeMemory(dev, colorMem, nullptr);
        if (depthMem)    vkFreeMemory(dev, depthMem, nullptr);
        vkDestroyDevice(dev, nullptr);
        dev = VK_NULL_HANDLE;
    }
}

int main()
{
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "cna-depth-bias-probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        std::printf("vkCreateInstance failed\n");
        return 1;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    std::printf("%u physical device(s)\n\n", count);

    int failures = 0;
    for (VkPhysicalDevice pd : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        std::printf("== %s (driver %u, subPixelPrecisionBits=%u)\n",
                    props.deviceName, props.driverVersion, props.limits.subPixelPrecisionBits);

        Probe probe; probe.pd = pd;
        if (!probe.Create()) { probe.Destroy(); std::printf("\n"); continue; }
        std::printf("  depth format: %s\n", FormatName(probe.depthFormat));

        struct Leg { const char* name; float constantF; float slopeF; bool tilted; bool wantGreen; };
        const Leg legs[] = {
            { "flat,   constant 0    ", 0.0f,     0.0f,     false, false },
            { "flat,   constant -1e6 ", -1.0e6f,  0.0f,     false, true  },
            { "tilted, slope 0       ", 0.0f,     0.0f,     true,  false },
            { "tilted, slope -2000   ", 0.0f,     -2000.0f, true,  true  },
        };
        for (const Leg& leg : legs)
        {
            uint8_t px[4] = {0, 0, 0, 0};
            if (!probe.RunLeg(leg.constantF, leg.slopeF, leg.tilted, px)) { ++failures; continue; }
            const bool green = px[1] > 200 && px[0] < 60;
            const bool ok = (green == leg.wantGreen);
            if (!ok) ++failures;
            std::printf("  [%s] %s -> (%3u,%3u,%3u) %s (expected %s)\n",
                        ok ? "PASS" : "FAIL", leg.name, px[0], px[1], px[2],
                        green ? "GREEN" : "RED", leg.wantGreen ? "GREEN" : "RED");
        }
        probe.Destroy();
        std::printf("\n");
    }

    vkDestroyInstance(instance, nullptr);
    std::printf("%s\n", failures == 0 ? "all legs behaved as Vulkan specifies"
                                      : "at least one leg diverged -- see above");
    return failures == 0 ? 0 : 1;
}
