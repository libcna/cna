#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Backends::Vulkan
{
    static const char* const kValidationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    static const char* const kDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#ifdef NDEBUG
    static constexpr bool kEnableValidation = false;
#else
    static constexpr bool kEnableValidation = true;
#endif

    // -------------------------------------------------------------------------
    // VulkanTextureBackend
    // -------------------------------------------------------------------------

    VulkanTextureBackend::VulkanTextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height) {}

    VulkanTextureBackend::~VulkanTextureBackend() = default;

    // -------------------------------------------------------------------------
    // VulkanGraphicsBackend — construction / destruction
    // -------------------------------------------------------------------------

    VulkanGraphicsBackend::VulkanGraphicsBackend(SDL_Window* window)
        : window_(window)
    {
        if (!window_)
            throw std::runtime_error("VulkanGraphicsBackend: null window");

        CreateInstance();
        if (kEnableValidation)
            SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateImageViews();
        CreateRenderPass();
        CreateFramebuffers();
        CreateCommandPool();
        AllocateCommandBuffers();
        CreateSyncObjects();
        initialized_ = true;
        SDL_Log("[Vulkan] Backend initialised");
    }

    VulkanGraphicsBackend::~VulkanGraphicsBackend()
    {
        if (device_ != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device_);

        CleanupSwapchain();

        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (i < static_cast<int>(imageAvailableSemaphores_.size()) && imageAvailableSemaphores_[i])
                vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            if (i < static_cast<int>(renderFinishedSemaphores_.size()) && renderFinishedSemaphores_[i])
                vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
            if (i < static_cast<int>(inFlightFences_.size()) && inFlightFences_[i])
                vkDestroyFence(device_, inFlightFences_[i], nullptr);
        }

        if (commandPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPool_, nullptr);

        if (device_ != VK_NULL_HANDLE)
            vkDestroyDevice(device_, nullptr);

        if (kEnableValidation && debugMessenger_ != VK_NULL_HANDLE) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn)
                fn(instance_, debugMessenger_, nullptr);
        }

        if (surface_ != VK_NULL_HANDLE)
            SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);

        if (instance_ != VK_NULL_HANDLE)
            vkDestroyInstance(instance_, nullptr);
    }

    // -------------------------------------------------------------------------
    // CreateInstance
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateInstance()
    {
        if (kEnableValidation) {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> available(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, available.data());
            for (const char* name : kValidationLayers) {
                bool found = false;
                for (const auto& props : available)
                    if (std::strcmp(props.layerName, name) == 0) { found = true; break; }
                if (!found)
                    throw std::runtime_error(std::string("Vulkan validation layer not found: ") + name);
            }
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "CNA Application";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "CNA";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        uint32_t sdlExtCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

        std::vector<const char*> extensions;
        if (sdlExts)
            extensions.assign(sdlExts, sdlExts + sdlExtCount);
        if (kEnableValidation)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        if (kEnableValidation) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            createInfo.ppEnabledLayerNames = kValidationLayers;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
    }

    // -------------------------------------------------------------------------
    // SetupDebugMessenger
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::SetupDebugMessenger()
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

        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (!fn || fn(instance_, &info, nullptr, &debugMessenger_) != VK_SUCCESS)
            SDL_Log("[Vulkan] Warning: could not set up validation debug messenger");
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanGraphicsBackend::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* /*userData*/)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            SDL_Log("[Vulkan Validation] %s", data->pMessage);
        return VK_FALSE;
    }

    // -------------------------------------------------------------------------
    // CreateSurface
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateSurface()
    {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }

    // -------------------------------------------------------------------------
    // PickPhysicalDevice
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::PickPhysicalDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0)
            throw std::runtime_error("Vulkan: no GPU with Vulkan support found");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (auto dev : devices) {
            // Must support VK_KHR_swapchain
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
            bool hasSwapchain = false;
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                { hasSwapchain = true; break; }
            if (!hasSwapchain) continue;

            // Find graphics + present queue families
            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

            std::optional<uint32_t> gfxFamily, presFamily;
            for (uint32_t i = 0; i < qCount; ++i) {
                if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    gfxFamily = i;
                VkBool32 presSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presSupport);
                if (presSupport) presFamily = i;
                if (gfxFamily && presFamily) break;
            }
            if (!gfxFamily || !presFamily) continue;

            // Must have at least one surface format and present mode
            uint32_t fmtCount = 0, modeCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fmtCount, nullptr);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &modeCount, nullptr);
            if (fmtCount == 0 || modeCount == 0) continue;

            physicalDevice_      = dev;
            graphicsQueueFamily_ = *gfxFamily;
            presentQueueFamily_  = *presFamily;
            break;
        }

        if (physicalDevice_ == VK_NULL_HANDLE)
            throw std::runtime_error("Vulkan: no suitable GPU found");

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);
        SDL_Log("[Vulkan] Selected GPU: %s", props.deviceName);
    }

    // -------------------------------------------------------------------------
    // CreateLogicalDevice
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateLogicalDevice()
    {
        std::set<uint32_t> uniqueFamilies = { graphicsQueueFamily_, presentQueueFamily_ };
        float priority = 1.f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (uint32_t fam : uniqueFamilies) {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = fam;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queueInfos.push_back(qi);
        }

        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(kDeviceExtensions));
        createInfo.ppEnabledExtensionNames = kDeviceExtensions;
        if (kEnableValidation) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(std::size(kValidationLayers));
            createInfo.ppEnabledLayerNames = kValidationLayers;
        }

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed");

        vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, presentQueueFamily_,  0, &presentQueue_);
    }

    // -------------------------------------------------------------------------
    // CreateSwapchain
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

        // Surface format: prefer B8G8R8A8_SRGB / SRGB_NONLINEAR
        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());
        VkSurfaceFormatKHR chosenFormat = formats[0];
        for (const auto& f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { chosenFormat = f; break; }

        // Present mode: prefer mailbox (triple-buffer), fall back to FIFO (v-sync)
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, modes.data());
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; }

        // Swapchain extent
        VkExtent2D extent;
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent = caps.currentExtent;
        } else {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
            extent.width  = std::clamp(static_cast<uint32_t>(w),
                                       caps.minImageExtent.width,  caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(h),
                                       caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
            imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR info{};
        info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface          = surface_;
        info.minImageCount    = imageCount;
        info.imageFormat      = chosenFormat.format;
        info.imageColorSpace  = chosenFormat.colorSpace;
        info.imageExtent      = extent;
        info.imageArrayLayers = 1;
        info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { graphicsQueueFamily_, presentQueueFamily_ };
        if (graphicsQueueFamily_ != presentQueueFamily_) {
            info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices   = queueFamilyIndices;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        info.preTransform   = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode    = presentMode;
        info.clipped        = VK_TRUE;

        if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");

        swapchainFormat_ = chosenFormat.format;
        swapchainExtent_ = extent;
    }

    // -------------------------------------------------------------------------
    // CreateImageViews
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateImageViews()
    {
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
        swapchainImages_.resize(count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());

        swapchainImageViews_.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo ivInfo{};
            ivInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivInfo.image    = swapchainImages_[i];
            ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivInfo.format   = swapchainFormat_;
            ivInfo.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            };
            ivInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            ivInfo.subresourceRange.baseMipLevel   = 0;
            ivInfo.subresourceRange.levelCount     = 1;
            ivInfo.subresourceRange.baseArrayLayer = 0;
            ivInfo.subresourceRange.layerCount     = 1;
            if (vkCreateImageView(device_, &ivInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateImageView failed");
        }
    }

    // -------------------------------------------------------------------------
    // CreateRenderPass
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = swapchainFormat_;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        // Ensure the render pass waits for color attachment output stage
        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &colorAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dep;

        if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
    }

    // -------------------------------------------------------------------------
    // CreateFramebuffers
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateFramebuffers()
    {
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            VkImageView attachments[] = { swapchainImageViews_[i] };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = renderPass_;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = attachments;
            fbInfo.width           = swapchainExtent_.width;
            fbInfo.height          = swapchainExtent_.height;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    // -------------------------------------------------------------------------
    // CreateCommandPool / AllocateCommandBuffers
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily_;
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed");
    }

    void VulkanGraphicsBackend::AllocateCommandBuffers()
    {
        commandBuffers_.resize(MaxFramesInFlight);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = commandPool_;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = MaxFramesInFlight;
        if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    // -------------------------------------------------------------------------
    // CreateSyncObjects
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CreateSyncObjects()
    {
        imageAvailableSemaphores_.resize(MaxFramesInFlight);
        renderFinishedSemaphores_.resize(MaxFramesInFlight);
        inFlightFences_.resize(MaxFramesInFlight);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // pre-signaled so first frame doesn't wait

        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
                vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
                throw std::runtime_error("Vulkan: failed to create synchronization objects");
        }
    }

    // -------------------------------------------------------------------------
    // CleanupSwapchain / RecreateSwapchain
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::CleanupSwapchain()
    {
        if (device_ == VK_NULL_HANDLE) return;

        for (auto fb : swapchainFramebuffers_)
            if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchainFramebuffers_.clear();

        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }

        for (auto iv : swapchainImageViews_)
            if (iv) vkDestroyImageView(device_, iv, nullptr);
        swapchainImageViews_.clear();
        swapchainImages_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void VulkanGraphicsBackend::RecreateSwapchain()
    {
        // Skip if window is minimized (extent would be 0×0)
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w == 0 || h == 0) return;

        vkDeviceWaitIdle(device_);
        CleanupSwapchain();
        CreateSwapchain();
        CreateImageViews();
        CreateRenderPass();
        CreateFramebuffers();
    }

    // -------------------------------------------------------------------------
    // RecordCommandBuffer
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::RecordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed");

        VkClearValue clearValue{};
        clearValue.color = { { clearR_, clearG_, clearB_, clearA_ } };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = renderPass_;
        rpBegin.framebuffer       = swapchainFramebuffers_[imageIndex];
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = swapchainExtent_;
        rpBegin.clearValueCount   = 1;
        rpBegin.pClearValues      = &clearValue;

        vkCmdBeginRenderPass(cb, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        // TODO: draw calls go here once SpriteBatch / 3D pipeline is implemented
        vkCmdEndRenderPass(cb);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed");
    }

    // -------------------------------------------------------------------------
    // IGraphicsBackend implementation
    // -------------------------------------------------------------------------

    void VulkanGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearR_ = r; clearG_ = g; clearB_ = b; clearA_ = a;
    }

    void VulkanGraphicsBackend::Present()
    {
        if (!initialized_) return;

        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed");

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
        RecordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

        VkSemaphore waitSems[]   = { imageAvailableSemaphores_[currentFrame_] };
        VkSemaphore signalSems[] = { renderFinishedSemaphores_[currentFrame_] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submitInfo{};
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = waitSems;
        submitInfo.pWaitDstStageMask    = waitStages;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &commandBuffers_[currentFrame_];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = signalSems;

        if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed");

        VkSwapchainKHR swapchains[] = { swapchain_ };
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = signalSems;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = swapchains;
        presentInfo.pImageIndices      = &imageIndex;

        result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            RecreateSwapchain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void VulkanGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    std::unique_ptr<ITextureBackend> VulkanGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<VulkanTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> VulkanGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<VulkanSpriteBatchBackend>();
    }

    // ---- 3D pipeline stubs --------------------------------------------------

    static void ThrowNo3D()
    {
        throw std::runtime_error("Vulkan backend: 3D rendering is not implemented yet.");
    }

    void VulkanGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D(); }
    void VulkanGraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3D(); }
    void VulkanGraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3D(); }
    void VulkanGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D(); }

    std::unique_ptr<IVertexBufferBackend> VulkanGraphicsBackend::CreateVertexBuffer(int)
    { ThrowNo3D(); return nullptr; }

    std::unique_ptr<IIndexBufferBackend> VulkanGraphicsBackend::CreateIndexBuffer16(int)
    { ThrowNo3D(); return nullptr; }

    void VulkanGraphicsBackend::DrawColoredPrimitives(
        const IVertexBufferBackend&, const Matrix&, const Matrix&, const Matrix&,
        PrimitiveType, int) { ThrowNo3D(); }

    void VulkanGraphicsBackend::DrawIndexedColoredPrimitives(
        const IVertexBufferBackend&, const IIndexBufferBackend&,
        const Matrix&, const Matrix&, const Matrix&,
        PrimitiveType, int) { ThrowNo3D(); }

} // namespace CNA::Internal::Backends::Vulkan

// -------------------------------------------------------------------------
// Factory — only compiled when the Vulkan backend is selected
// -------------------------------------------------------------------------
namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_VULKAN
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Vulkan::VulkanGraphicsBackend>(args.window);
    }
#endif
}
