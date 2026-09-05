// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-151: which vertex-attribute formats can this machine's Vulkan
// drivers actually bind?
//
// The question the row turns on: XNA lets a content processor spell BlendIndices as Byte4 OR as
// Vector4, and CNA's skinned shaders declare `uvec4 aBoneIndices`, so only the integer spelling is
// bindable today. One shader can serve both spellings if the bone-index input becomes `vec4` and
// the Byte4 element is bound as VK_FORMAT_R8G8B8A8_USCALED -- integer values converted to float
// WITHOUT normalisation, which is exactly what glVertexAttribPointer(..., GL_UNSIGNED_BYTE,
// GL_FALSE, ...) does and therefore what EasyGL already relies on.
//
// The catch is that _USCALED vertex formats are NOT in Vulkan's mandatory vertex-buffer list. This
// probe asks each physical device, so the decision is measured rather than assumed. No surface, no
// window, no device creation: instance + vkGetPhysicalDeviceFormatProperties only.
//
//   ccache g++ -std=c++17 -O1 vertex_format_probe.cpp -lvulkan -o vertex_format_probe
//   ./vertex_format_probe

#include <vulkan/vulkan.h>

#include <cstdio>
#include <vector>

namespace
{
    struct Probe { VkFormat format; const char* name; };

    const Probe kProbes[] = {
        { VK_FORMAT_R8G8B8A8_UINT,        "R8G8B8A8_UINT        (Byte4 today, feeds uvec4)" },
        { VK_FORMAT_R8G8B8A8_USCALED,     "R8G8B8A8_USCALED     (Byte4 -> vec4, unnormalised)" },
        { VK_FORMAT_R8G8B8A8_UNORM,       "R8G8B8A8_UNORM       (Color)" },
        { VK_FORMAT_R32G32B32A32_SFLOAT,  "R32G32B32A32_SFLOAT  (Vector4)" },
        { VK_FORMAT_R16G16_USCALED,       "R16G16_USCALED       (Short2 -> vec2)" },
        { VK_FORMAT_R16G16B16A16_USCALED, "R16G16B16A16_USCALED (Short4 -> vec4)" },
    };
}

int main()
{
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
    if (count == 0) { std::printf("no physical devices\n"); return 1; }

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        std::printf("=== %s ===\n", props.deviceName);
        for (const Probe& p : kProbes) {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(dev, p.format, &fp);
            const bool vertex = (fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0;
            std::printf("  %-56s vertexBuffer=%s\n", p.name, vertex ? "YES" : "no");
        }
    }
    vkDestroyInstance(instance, nullptr);
    return 0;
}
