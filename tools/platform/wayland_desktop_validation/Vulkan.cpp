// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114/0122: CNA Wayland window -> the platform's
// VK_KHR_wayland_surface service -> a real VkSurfaceKHR on a real physical device -> swapchain,
// acquire, clear, present, resize, recreate, present again. (The X11 harness's scenario, which is
// window-system-neutral but for the surface the platform hands it.)
//
// The loader is dlopen()ed and every entry point resolved at run time, like the backend itself;
// nothing links libvulkan. `--device NAME` picks a physical device by substring (default: the
// first that is not a CPU implementation), `--validation` enables VK_LAYER_KHRONOS_validation and
// counts its messages.

#include "Harness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#if defined(CNA_WAYLAND_VALIDATION_HAVE_VULKAN)
#  define VK_NO_PROTOTYPES
#  include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <sstream>

namespace CnaWaylandValidation {

#if !defined(CNA_WAYLAND_VALIDATION_HAVE_VULKAN)

    int RunVulkan(const std::vector<std::string>&)
    {
        Skip("vulkan", "built without Vulkan headers");
        return 0;
    }

    namespace VulkanLifetime {
        int Iteration(Session&, bool, std::string& error)
        {
            error = "no Vulkan headers";
            return -1;
        }
    }

#else

    namespace {

        int g_validationErrors = 0;
        int g_validationWarnings = 0;
        std::string g_firstValidationMessage;

        VKAPI_ATTR VkBool32 VKAPI_CALL OnValidationMessage(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                ++g_validationErrors;
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                ++g_validationWarnings;
            }
            if (g_firstValidationMessage.empty() && data != nullptr && data->pMessage != nullptr &&
                (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) != 0)
            {
                g_firstValidationMessage = data->pMessage;
            }
            return VK_FALSE;
        }

        /// Everything one run needs, torn down in reverse order by the destructor.
        struct Context
        {
            PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
            VkInstance instance = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
            VkPhysicalDevice physical = VK_NULL_HANDLE;
            VkDevice device = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
            std::uint32_t family = 0;
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkExtent2D extent{};
            bool canReadBack = false;
            std::vector<VkImage> images;
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer commands = VK_NULL_HANDLE;
            VkSemaphore acquired = VK_NULL_HANDLE;
            VkSemaphore rendered = VK_NULL_HANDLE;
            VkFence done = VK_NULL_HANDLE;
            VkBuffer readback = VK_NULL_HANDLE;
            VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
            IPlatformVulkanSurface* service = nullptr;

#define CNA_VK_FN(name) PFN_##name name = nullptr
            CNA_VK_FN(vkCreateInstance);
            CNA_VK_FN(vkEnumerateInstanceLayerProperties);
            CNA_VK_FN(vkDestroyInstance);
            CNA_VK_FN(vkEnumeratePhysicalDevices);
            CNA_VK_FN(vkGetPhysicalDeviceProperties);
            CNA_VK_FN(vkGetPhysicalDeviceQueueFamilyProperties);
            CNA_VK_FN(vkGetPhysicalDeviceMemoryProperties);
            CNA_VK_FN(vkGetPhysicalDeviceSurfaceSupportKHR);
            CNA_VK_FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
            CNA_VK_FN(vkGetPhysicalDeviceSurfaceFormatsKHR);
            CNA_VK_FN(vkCreateDevice);
            CNA_VK_FN(vkGetDeviceProcAddr);
            CNA_VK_FN(vkCreateDebugUtilsMessengerEXT);
            CNA_VK_FN(vkDestroyDebugUtilsMessengerEXT);
            CNA_VK_FN(vkDestroyDevice);
            CNA_VK_FN(vkGetDeviceQueue);
            CNA_VK_FN(vkDeviceWaitIdle);
            CNA_VK_FN(vkCreateSwapchainKHR);
            CNA_VK_FN(vkDestroySwapchainKHR);
            CNA_VK_FN(vkGetSwapchainImagesKHR);
            CNA_VK_FN(vkAcquireNextImageKHR);
            CNA_VK_FN(vkQueuePresentKHR);
            CNA_VK_FN(vkQueueSubmit);
            CNA_VK_FN(vkCreateCommandPool);
            CNA_VK_FN(vkDestroyCommandPool);
            CNA_VK_FN(vkAllocateCommandBuffers);
            CNA_VK_FN(vkBeginCommandBuffer);
            CNA_VK_FN(vkEndCommandBuffer);
            CNA_VK_FN(vkResetCommandBuffer);
            CNA_VK_FN(vkCmdPipelineBarrier);
            CNA_VK_FN(vkCmdClearColorImage);
            CNA_VK_FN(vkCmdCopyImageToBuffer);
            CNA_VK_FN(vkCreateSemaphore);
            CNA_VK_FN(vkDestroySemaphore);
            CNA_VK_FN(vkCreateFence);
            CNA_VK_FN(vkDestroyFence);
            CNA_VK_FN(vkWaitForFences);
            CNA_VK_FN(vkResetFences);
            CNA_VK_FN(vkCreateBuffer);
            CNA_VK_FN(vkDestroyBuffer);
            CNA_VK_FN(vkGetBufferMemoryRequirements);
            CNA_VK_FN(vkAllocateMemory);
            CNA_VK_FN(vkFreeMemory);
            CNA_VK_FN(vkBindBufferMemory);
            CNA_VK_FN(vkMapMemory);
            CNA_VK_FN(vkUnmapMemory);
#undef CNA_VK_FN

            ~Context() { Destroy(); }

            void DestroySwapchain()
            {
                if (device == VK_NULL_HANDLE) { return; }
                vkDeviceWaitIdle(device);
                if (swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device, swapchain, nullptr); }
                if (readback != VK_NULL_HANDLE) { vkDestroyBuffer(device, readback, nullptr); }
                if (readbackMemory != VK_NULL_HANDLE) { vkFreeMemory(device, readbackMemory, nullptr); }
                swapchain = VK_NULL_HANDLE;
                readback = VK_NULL_HANDLE;
                readbackMemory = VK_NULL_HANDLE;
                images.clear();
            }

            void Destroy()
            {
                DestroySwapchain();
                if (device != VK_NULL_HANDLE)
                {
                    if (acquired != VK_NULL_HANDLE) { vkDestroySemaphore(device, acquired, nullptr); }
                    if (rendered != VK_NULL_HANDLE) { vkDestroySemaphore(device, rendered, nullptr); }
                    if (done != VK_NULL_HANDLE) { vkDestroyFence(device, done, nullptr); }
                    if (pool != VK_NULL_HANDLE) { vkDestroyCommandPool(device, pool, nullptr); }
                    vkDestroyDevice(device, nullptr);
                    device = VK_NULL_HANDLE;
                }
                if (surface != VK_NULL_HANDLE && service != nullptr)
                {
                    service->DestroySurface(instance, reinterpret_cast<std::uint64_t>(surface));
                    surface = VK_NULL_HANDLE;
                }
                if (messenger != VK_NULL_HANDLE) { vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr); }
                messenger = VK_NULL_HANDLE;
                if (instance != VK_NULL_HANDLE) { vkDestroyInstance(instance, nullptr); }
                instance = VK_NULL_HANDLE;
            }

            std::uint32_t MemoryType(const std::uint32_t bits, const VkMemoryPropertyFlags wanted) const
            {
                VkPhysicalDeviceMemoryProperties properties{};
                vkGetPhysicalDeviceMemoryProperties(physical, &properties);
                for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
                {
                    if ((bits & (1u << index)) != 0 &&
                        (properties.memoryTypes[index].propertyFlags & wanted) == wanted)
                    {
                        return index;
                    }
                }
                return 0;
            }

            bool CreateSwapchain(const std::uint32_t width, const std::uint32_t height,
                                 std::string& error)
            {
                VkSurfaceCapabilitiesKHR capabilities{};
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
                std::uint32_t count = 0;
                vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
                std::vector<VkSurfaceFormatKHR> formats(count);
                vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
                format = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats.front().format;
                VkColorSpaceKHR colorSpace = formats.empty() ? VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                                                             : formats.front().colorSpace;
                for (const VkSurfaceFormatKHR& candidate : formats)
                {
                    if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM)
                    {
                        format = candidate.format;
                        colorSpace = candidate.colorSpace;
                    }
                }
                extent = capabilities.currentExtent.width != 0xFFFFFFFFu
                             ? capabilities.currentExtent
                             : VkExtent2D{width, height};
                canReadBack = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

                VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
                info.surface = surface;
                info.minImageCount = std::max(2u, capabilities.minImageCount);
                if (capabilities.maxImageCount != 0)
                {
                    info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
                }
                info.imageFormat = format;
                info.imageColorSpace = colorSpace;
                info.imageExtent = extent;
                info.imageArrayLayers = 1;
                info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  (canReadBack ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
                info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                info.preTransform = capabilities.currentTransform;
                info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                info.clipped = VK_TRUE;
                info.oldSwapchain = swapchain;
                VkSwapchainKHR created = VK_NULL_HANDLE;
                const VkResult result = vkCreateSwapchainKHR(device, &info, nullptr, &created);
                if (swapchain != VK_NULL_HANDLE)
                {
                    vkDeviceWaitIdle(device);
                    vkDestroySwapchainKHR(device, swapchain, nullptr);
                }
                swapchain = created;
                if (result != VK_SUCCESS)
                {
                    error = "vkCreateSwapchainKHR " + std::to_string(result);
                    return false;
                }
                count = 0;
                vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
                images.resize(count);
                vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());

                if (readback != VK_NULL_HANDLE) { vkDestroyBuffer(device, readback, nullptr); }
                if (readbackMemory != VK_NULL_HANDLE) { vkFreeMemory(device, readbackMemory, nullptr); }
                readback = VK_NULL_HANDLE;
                readbackMemory = VK_NULL_HANDLE;
                if (canReadBack)
                {
                    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bufferInfo.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
                    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    vkCreateBuffer(device, &bufferInfo, nullptr, &readback);
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(device, readback, &requirements);
                    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocate.allocationSize = requirements.size;
                    allocate.memoryTypeIndex =
                        MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                    vkAllocateMemory(device, &allocate, nullptr, &readbackMemory);
                    vkBindBufferMemory(device, readback, readbackMemory, 0);
                }
                return true;
            }

            /// One frame. Returns VK_SUCCESS, or the out-of-date/suboptimal result that asks for a
            /// new swapchain. When @p verify, the cleared image is copied back and its centre read.
            VkResult Frame(const float red, const float green, const float blue, const bool verify,
                           std::string& detail)
            {
                std::uint32_t index = 0;
                VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                                        VK_NULL_HANDLE, &index);
                if (result == VK_ERROR_OUT_OF_DATE_KHR)
                {
                    return result;
                }
                vkResetCommandBuffer(commands, 0);
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(commands, &begin);
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                VkImageMemoryBarrier toClear{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toClear.srcAccessMask = 0;
                toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toClear.image = images[index];
                toClear.subresourceRange = range;
                vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                     &toClear);
                const VkClearColorValue color{{red, green, blue, 1.0f}};
                vkCmdClearColorImage(commands, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     &color, 1, &range);
                VkImageLayout last = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                if (verify && canReadBack)
                {
                    VkImageMemoryBarrier toCopy = toClear;
                    toCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    toCopy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                         1, &toCopy);
                    VkBufferImageCopy region{};
                    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.imageExtent = {extent.width, extent.height, 1};
                    vkCmdCopyImageToBuffer(commands, images[index],
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1,
                                           &region);
                    last = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                }
                VkImageMemoryBarrier toPresent = toClear;
                toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
                toPresent.dstAccessMask = 0;
                toPresent.oldLayout = last;
                toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                                     1, &toPresent);
                vkEndCommandBuffer(commands);

                const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.waitSemaphoreCount = 1;
                submit.pWaitSemaphores = &acquired;
                submit.pWaitDstStageMask = &wait;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &commands;
                submit.signalSemaphoreCount = 1;
                submit.pSignalSemaphores = &rendered;
                vkResetFences(device, 1, &done);
                vkQueueSubmit(queue, 1, &submit, done);

                VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores = &rendered;
                present.swapchainCount = 1;
                present.pSwapchains = &swapchain;
                present.pImageIndices = &index;
                result = vkQueuePresentKHR(queue, &present);
                vkWaitForFences(device, 1, &done, VK_TRUE, UINT64_MAX);

                if (verify && canReadBack)
                {
                    void* mapped = nullptr;
                    vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
                    const auto* pixels = static_cast<const std::uint8_t*>(mapped);
                    const std::size_t at =
                        (static_cast<std::size_t>(extent.height / 2) * extent.width + extent.width / 2) * 4;
                    // B8G8R8A8.
                    const int b = pixels[at + 0];
                    const int g = pixels[at + 1];
                    const int r = pixels[at + 2];
                    vkUnmapMemory(device, readbackMemory);
                    const auto near = [](const int value, const float wanted) {
                        return std::abs(value - static_cast<int>(wanted * 255.0f + 0.5f)) <= 2;
                    };
                    std::ostringstream text;
                    text << "centre " << r << "," << g << "," << b;
                    detail = text.str();
                    if (!(near(r, red) && near(g, green) && near(b, blue)))
                    {
                        return VK_ERROR_UNKNOWN;
                    }
                }
                return result;
            }
        };

        void* LoadLoader()
        {
            static void* handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            return handle;
        }

        /// Builds instance, device and surface for @p window. Returns false with @p error set.
        bool Build(Context& vk, IPlatform& platform, IPlatformWindow& window, const bool validation,
                   const std::string& wantedDevice, std::string& error, std::string& deviceName)
        {
            void* loader = LoadLoader();
            if (loader == nullptr)
            {
                error = "libvulkan.so.1 not found";
                return false;
            }
            vk.getInstanceProcAddr =
                reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
            vk.service = platform.GetVulkanSurface();
            if (vk.getInstanceProcAddr == nullptr || vk.service == nullptr)
            {
                error = "no vkGetInstanceProcAddr or no platform Vulkan surface service";
                return false;
            }
#define CNA_VK_GLOBAL(name) vk.name = reinterpret_cast<PFN_##name>(vk.getInstanceProcAddr(nullptr, #name))
            CNA_VK_GLOBAL(vkCreateInstance);
            CNA_VK_GLOBAL(vkEnumerateInstanceLayerProperties);
#undef CNA_VK_GLOBAL

            std::vector<std::string> extensionNames = vk.service->GetInstanceExtensions();
            std::vector<const char*> layers;
            if (validation)
            {
                std::uint32_t count = 0;
                vk.vkEnumerateInstanceLayerProperties(&count, nullptr);
                std::vector<VkLayerProperties> available(count);
                vk.vkEnumerateInstanceLayerProperties(&count, available.data());
                for (const VkLayerProperties& layer : available)
                {
                    if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    {
                        layers.push_back("VK_LAYER_KHRONOS_validation");
                        extensionNames.push_back("VK_EXT_debug_utils");
                    }
                }
            }
            std::vector<const char*> extensions;
            for (const std::string& name : extensionNames)
            {
                extensions.push_back(name.c_str());
            }
            VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            application.pApplicationName = "cna_wayland_desktop_validation";
            application.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            instanceInfo.pApplicationInfo = &application;
            instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
            instanceInfo.ppEnabledExtensionNames = extensions.data();
            instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
            instanceInfo.ppEnabledLayerNames = layers.data();
            if (vk.vkCreateInstance(&instanceInfo, nullptr, &vk.instance) != VK_SUCCESS)
            {
                error = "vkCreateInstance failed";
                return false;
            }
#define CNA_VK_INSTANCE(name) vk.name = reinterpret_cast<PFN_##name>(vk.getInstanceProcAddr(vk.instance, #name))
            CNA_VK_INSTANCE(vkDestroyInstance);
            CNA_VK_INSTANCE(vkEnumeratePhysicalDevices);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceProperties);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
            CNA_VK_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR);
            CNA_VK_INSTANCE(vkCreateDevice);
            CNA_VK_INSTANCE(vkGetDeviceProcAddr);
            CNA_VK_INSTANCE(vkCreateDebugUtilsMessengerEXT);
            CNA_VK_INSTANCE(vkDestroyDebugUtilsMessengerEXT);
#undef CNA_VK_INSTANCE
            if (!layers.empty() && vk.vkCreateDebugUtilsMessengerEXT != nullptr)
            {
                VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                messengerInfo.pfnUserCallback = OnValidationMessage;
                vk.vkCreateDebugUtilsMessengerEXT(vk.instance, &messengerInfo, nullptr, &vk.messenger);
            }

            try
            {
                vk.surface = reinterpret_cast<VkSurfaceKHR>(
                    vk.service->CreateSurface(vk.instance, window.GetId()));
            }
            catch (const std::exception& exception)
            {
                error = std::string("platform CreateSurface: ") + exception.what();
                return false;
            }

            std::uint32_t count = 0;
            vk.vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
            std::vector<VkPhysicalDevice> devices(count);
            vk.vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
            for (VkPhysicalDevice candidate : devices)
            {
                VkPhysicalDeviceProperties properties{};
                vk.vkGetPhysicalDeviceProperties(candidate, &properties);
                const std::string name = properties.deviceName;
                const bool wanted = wantedDevice.empty()
                                        ? properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU
                                        : name.find(wantedDevice) != std::string::npos;
                if (wanted && vk.physical == VK_NULL_HANDLE)
                {
                    vk.physical = candidate;
                    deviceName = name + " (" +
                                 (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU
                                      ? "CPU/software"
                                      : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                                            ? "integrated GPU"
                                            : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                                                  ? "discrete GPU"
                                                  : "other") +
                                 ", driver " + std::to_string(VK_VERSION_MAJOR(properties.driverVersion)) + "." +
                                 std::to_string(VK_VERSION_MINOR(properties.driverVersion)) + "." +
                                 std::to_string(VK_VERSION_PATCH(properties.driverVersion)) + ", API " +
                                 std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) + "." +
                                 std::to_string(VK_VERSION_MINOR(properties.apiVersion)) + ")";
                }
            }
            if (vk.physical == VK_NULL_HANDLE && wantedDevice.empty() && !devices.empty())
            {
                // Only a CPU implementation (lavapipe, as sanitizer runs select): better than none.
                vk.physical = devices.front();
                VkPhysicalDeviceProperties properties{};
                vk.vkGetPhysicalDeviceProperties(vk.physical, &properties);
                deviceName = std::string(properties.deviceName) + " (CPU/software, the only device)";
            }
            if (vk.physical == VK_NULL_HANDLE)
            {
                error = "no matching physical device";
                return false;
            }
            vk.vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vk.vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &count, families.data());
            bool presentable = false;
            for (std::uint32_t index = 0; index < count && !presentable; ++index)
            {
                VkBool32 supported = VK_FALSE;
                vk.vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, index, vk.surface, &supported);
                if (supported == VK_TRUE && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                {
                    vk.family = index;
                    presentable = true;
                }
            }
            if (!presentable)
            {
                error = "no queue family can present to the CNA window's surface";
                return false;
            }

            const float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = vk.family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.enabledExtensionCount = 1;
            deviceInfo.ppEnabledExtensionNames = deviceExtensions;
            if (vk.vkCreateDevice(vk.physical, &deviceInfo, nullptr, &vk.device) != VK_SUCCESS)
            {
                error = "vkCreateDevice failed";
                return false;
            }
#define CNA_VK_DEVICE(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(vk.device, #name))
            CNA_VK_DEVICE(vkDestroyDevice);
            CNA_VK_DEVICE(vkGetDeviceQueue);
            CNA_VK_DEVICE(vkDeviceWaitIdle);
            CNA_VK_DEVICE(vkCreateSwapchainKHR);
            CNA_VK_DEVICE(vkDestroySwapchainKHR);
            CNA_VK_DEVICE(vkGetSwapchainImagesKHR);
            CNA_VK_DEVICE(vkAcquireNextImageKHR);
            CNA_VK_DEVICE(vkQueuePresentKHR);
            CNA_VK_DEVICE(vkQueueSubmit);
            CNA_VK_DEVICE(vkCreateCommandPool);
            CNA_VK_DEVICE(vkDestroyCommandPool);
            CNA_VK_DEVICE(vkAllocateCommandBuffers);
            CNA_VK_DEVICE(vkBeginCommandBuffer);
            CNA_VK_DEVICE(vkEndCommandBuffer);
            CNA_VK_DEVICE(vkResetCommandBuffer);
            CNA_VK_DEVICE(vkCmdPipelineBarrier);
            CNA_VK_DEVICE(vkCmdClearColorImage);
            CNA_VK_DEVICE(vkCmdCopyImageToBuffer);
            CNA_VK_DEVICE(vkCreateSemaphore);
            CNA_VK_DEVICE(vkDestroySemaphore);
            CNA_VK_DEVICE(vkCreateFence);
            CNA_VK_DEVICE(vkDestroyFence);
            CNA_VK_DEVICE(vkWaitForFences);
            CNA_VK_DEVICE(vkResetFences);
            CNA_VK_DEVICE(vkCreateBuffer);
            CNA_VK_DEVICE(vkDestroyBuffer);
            CNA_VK_DEVICE(vkGetBufferMemoryRequirements);
            CNA_VK_DEVICE(vkAllocateMemory);
            CNA_VK_DEVICE(vkFreeMemory);
            CNA_VK_DEVICE(vkBindBufferMemory);
            CNA_VK_DEVICE(vkMapMemory);
            CNA_VK_DEVICE(vkUnmapMemory);
#undef CNA_VK_DEVICE
            vk.vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);

            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = vk.family;
            vk.vkCreateCommandPool(vk.device, &poolInfo, nullptr, &vk.pool);
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = vk.pool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            vk.vkAllocateCommandBuffers(vk.device, &allocate, &vk.commands);
            VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk.vkCreateSemaphore(vk.device, &semaphoreInfo, nullptr, &vk.acquired);
            vk.vkCreateSemaphore(vk.device, &semaphoreInfo, nullptr, &vk.rendered);
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            vk.vkCreateFence(vk.device, &fenceInfo, nullptr, &vk.done);

            const WindowSize size = window.GetPixelSize();
            return vk.CreateSwapchain(static_cast<std::uint32_t>(size.width),
                                      static_cast<std::uint32_t>(size.height), error);
        }

    } // namespace

    // Used by the lifetime scenario: one full build/present/teardown cycle.
    namespace VulkanLifetime {
        int Iteration(Session& session, const bool validation, std::string& error)
        {
            auto window = session.Make("CNA Vulkan lifetime", 320, 240, true, WindowRenderIntent::Vulkan);
            session.WaitFor(window->GetId(), WindowEventKind::Exposed);
            Context vk;
            std::string device;
            if (!Build(vk, session.Platform(), *window, validation, std::string(), error, device))
            {
                return -1;
            }
            for (int frame = 0; frame < 3; ++frame)
            {
                std::string unused;
                vk.Frame(0.2f, 0.4f, 0.6f, frame == 2, unused);
                session.Poll();
            }
            vk.Destroy();
            return g_validationErrors;
        }
    }

    int RunVulkan(const std::vector<std::string>& arguments)
    {
        const int frames = static_cast<int>(OptionInt(arguments, "frames", 2000));
        const bool validation = OptionFlag(arguments, "validation");
        std::string wantedDevice;
        for (std::size_t index = 0; index + 1 < arguments.size(); ++index)
        {
            if (arguments[index] == "--device") { wantedDevice = arguments[index + 1]; }
        }
        Session session;
        if (!session.Ok())
        {
            Fail("vulkan.session", session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        if (!platform.GetCapabilities().vulkanSurface)
        {
            Skip("vulkan", "the backend reports no vulkanSurface capability");
            return 0;
        }
        auto window = session.Make("CNA Wayland Vulkan", 800, 600, true, WindowRenderIntent::Vulkan);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);

        Context vk;
        std::string error;
        std::string deviceName;
        if (!Check(Build(vk, platform, *window, validation, wantedDevice, error, deviceName),
                   "vulkan.instance-surface-device-swapchain", error))
        {
            return 1;
        }
        Info("physical device: " + deviceName);
        Pass("vulkan.surface-supports-present", "vkGetPhysicalDeviceSurfaceSupportKHR = VK_TRUE");
        Info("swapchain " + std::to_string(vk.extent.width) + "x" + std::to_string(vk.extent.height) +
             ", " + std::to_string(vk.images.size()) + " images, format " + std::to_string(vk.format) +
             (vk.canReadBack ? ", readback" : ", no readback"));
        Check(static_cast<int>(vk.extent.width) == window->GetPixelSize().width &&
                  static_cast<int>(vk.extent.height) == window->GetPixelSize().height,
              "vulkan.swapchain-extent-matches-window");

        int bad = 0;
        int recreated = 0;
        std::string lastDetail;
        const double started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            std::string detail;
            const float phase = static_cast<float>(frame % 256) / 255.0f;
            const VkResult result = vk.Frame(phase, 0.3f, 1.0f - phase, frame % 200 == 0, detail);
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                ++recreated;
                vk.CreateSwapchain(static_cast<std::uint32_t>(window->GetPixelSize().width),
                                   static_cast<std::uint32_t>(window->GetPixelSize().height), error);
            }
            else if (result != VK_SUCCESS)
            {
                ++bad;
                lastDetail = detail;
            }
            session.Poll();
            session.Clear();
        }
        const double seconds = (NowMs() - started) / 1000.0;
        Check(bad == 0, "vulkan.frames-present-correctly",
              std::to_string(frames) + " frames in " + std::to_string(seconds) + " s (" +
                  std::to_string(static_cast<int>(frames / seconds)) + " fps, FIFO), " +
                  std::to_string(bad) + " bad, " + std::to_string(recreated) + " recreations" +
                  (lastDetail.empty() ? std::string() : ": " + lastDetail));

        // --- resize: the swapchain goes out of date and is rebuilt at the new size -------------------
        {
            session.Clear();
            window->SetSize(1100, 700);
            window->Sync();
            // On Wayland the client decides its surface's size: the platform reports the new
            // pixel size at once, and the swapchain is rebuilt to it (currentExtent is 0xFFFFFFFF).
            session.WaitFor(id, WindowEventKind::PixelSizeChanged, std::chrono::milliseconds(500));
            std::string detail;
            VkResult result = VK_SUCCESS;
            int attempts = 0;
            do
            {
                result = vk.Frame(0.9f, 0.1f, 0.1f, false, detail);
                if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
                    static_cast<int>(vk.extent.width) != window->GetPixelSize().width)
                {
                    vk.CreateSwapchain(static_cast<std::uint32_t>(window->GetPixelSize().width),
                                       static_cast<std::uint32_t>(window->GetPixelSize().height),
                                       error);
                }
                session.Poll();
            } while (static_cast<int>(vk.extent.width) != window->GetPixelSize().width && ++attempts < 20);
            const VkResult verified = vk.Frame(0.9f, 0.1f, 0.1f, true, detail);
            Check(verified == VK_SUCCESS && static_cast<int>(vk.extent.width) == window->GetPixelSize().width &&
                      static_cast<int>(vk.extent.height) == window->GetPixelSize().height &&
                      window->GetClientBounds().width == 1100,
                  "vulkan.resize-recreates-and-presents",
                  std::to_string(vk.extent.width) + "x" + std::to_string(vk.extent.height) + ", " + detail);
            for (int frame = 0; frame < 120; ++frame)
            {
                vk.Frame(0.1f, 0.6f, 0.2f, false, detail);
                session.Poll();
            }
        }
        vk.Destroy();
        window.reset();
        if (validation)
        {
            Check(g_validationErrors == 0, "vulkan.validation-layer-clean",
                  std::to_string(g_validationErrors) + " errors, " +
                      std::to_string(g_validationWarnings) + " warnings" +
                      (g_firstValidationMessage.empty() ? std::string() : ": " + g_firstValidationMessage));
        }
        return Results().failed == 0 ? 0 : 1;
    }

#endif

} // namespace CnaWaylandValidation
