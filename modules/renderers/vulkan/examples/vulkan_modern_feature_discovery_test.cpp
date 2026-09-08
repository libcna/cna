// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2240: the selected device is discovered through Vulkan's extensible
// feature/property queries, and native support is kept distinct from what CNA enables and claims.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <string>

using CNA::GraphicsCapability;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

namespace
{
    struct FeatureCase
    {
        const char* name;
        VkBool32 VkPhysicalDeviceFeatures::* field;
        bool consumedByCna;
    };

#define CNA_VK_FEATURE(name, consumed) FeatureCase{#name, &VkPhysicalDeviceFeatures::name, consumed}
    constexpr FeatureCase kCoreFeatures[] = {
        CNA_VK_FEATURE(robustBufferAccess, false),
        CNA_VK_FEATURE(fullDrawIndexUint32, false),
        CNA_VK_FEATURE(imageCubeArray, false),
        CNA_VK_FEATURE(independentBlend, true),
        CNA_VK_FEATURE(geometryShader, false),
        CNA_VK_FEATURE(tessellationShader, false),
        CNA_VK_FEATURE(sampleRateShading, false),
        CNA_VK_FEATURE(dualSrcBlend, false),
        CNA_VK_FEATURE(logicOp, false),
        CNA_VK_FEATURE(multiDrawIndirect, false),
        CNA_VK_FEATURE(drawIndirectFirstInstance, false),
        CNA_VK_FEATURE(depthClamp, false),
        CNA_VK_FEATURE(depthBiasClamp, false),
        CNA_VK_FEATURE(fillModeNonSolid, true),
        CNA_VK_FEATURE(depthBounds, false),
        CNA_VK_FEATURE(wideLines, false),
        CNA_VK_FEATURE(largePoints, false),
        CNA_VK_FEATURE(alphaToOne, false),
        CNA_VK_FEATURE(multiViewport, false),
        CNA_VK_FEATURE(samplerAnisotropy, true),
        CNA_VK_FEATURE(textureCompressionETC2, false),
        CNA_VK_FEATURE(textureCompressionASTC_LDR, false),
        CNA_VK_FEATURE(textureCompressionBC, true),
        CNA_VK_FEATURE(occlusionQueryPrecise, true),
        CNA_VK_FEATURE(pipelineStatisticsQuery, false),
        CNA_VK_FEATURE(vertexPipelineStoresAndAtomics, false),
        CNA_VK_FEATURE(fragmentStoresAndAtomics, false),
        CNA_VK_FEATURE(shaderTessellationAndGeometryPointSize, false),
        CNA_VK_FEATURE(shaderImageGatherExtended, false),
        CNA_VK_FEATURE(shaderStorageImageExtendedFormats, false),
        CNA_VK_FEATURE(shaderStorageImageMultisample, false),
        CNA_VK_FEATURE(shaderStorageImageReadWithoutFormat, false),
        CNA_VK_FEATURE(shaderStorageImageWriteWithoutFormat, false),
        CNA_VK_FEATURE(shaderUniformBufferArrayDynamicIndexing, false),
        CNA_VK_FEATURE(shaderSampledImageArrayDynamicIndexing, false),
        CNA_VK_FEATURE(shaderStorageBufferArrayDynamicIndexing, false),
        CNA_VK_FEATURE(shaderStorageImageArrayDynamicIndexing, false),
        CNA_VK_FEATURE(shaderClipDistance, false),
        CNA_VK_FEATURE(shaderCullDistance, false),
        CNA_VK_FEATURE(shaderFloat64, false),
        CNA_VK_FEATURE(shaderInt64, false),
        CNA_VK_FEATURE(shaderInt16, false),
        CNA_VK_FEATURE(shaderResourceResidency, false),
        CNA_VK_FEATURE(shaderResourceMinLod, false),
        CNA_VK_FEATURE(sparseBinding, false),
        CNA_VK_FEATURE(sparseResidencyBuffer, false),
        CNA_VK_FEATURE(sparseResidencyImage2D, false),
        CNA_VK_FEATURE(sparseResidencyImage3D, false),
        CNA_VK_FEATURE(sparseResidency2Samples, false),
        CNA_VK_FEATURE(sparseResidency4Samples, false),
        CNA_VK_FEATURE(sparseResidency8Samples, false),
        CNA_VK_FEATURE(sparseResidency16Samples, false),
        CNA_VK_FEATURE(sparseResidencyAliased, false),
        CNA_VK_FEATURE(variableMultisampleRate, false),
        CNA_VK_FEATURE(inheritedQueries, false),
    };
#undef CNA_VK_FEATURE
}

class VulkanModernFeatureDiscoveryTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool condition, const char* label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", condition ? "PASS" : "FAIL", label, detail.c_str());
        if (condition) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        check(renderer != nullptr, "A native Vulkan renderer is active",
              renderer != nullptr ? "dynamic_cast succeeded" : "wrong renderer identity");
        if (renderer == nullptr) {
            Exit();
            return;
        }

        const auto& properties = renderer->GetPhysicalDevicePropertiesEXT();
        check(properties.apiVersion >= VK_API_VERSION_1_0 && properties.deviceName[0] != '\0',
              "B properties2 discovery produced a usable selected-device snapshot",
              std::string("api=") + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion))
                  + "." + std::to_string(VK_API_VERSION_MINOR(properties.apiVersion))
                  + " device=" + properties.deviceName);
        check(&renderer->GetDeviceLimitsEXT() == &properties.limits,
              "C every capability query reads the same property snapshot",
              "GetDeviceLimitsEXT aliases VkPhysicalDeviceProperties::limits");

        const auto& supported = renderer->GetSupportedDeviceFeaturesEXT();
        const auto& enabled = renderer->GetEnabledDeviceFeaturesEXT();
        int supportedCount = 0;
        int enabledCount = 0;
        std::string firstMismatch;
        for (const FeatureCase& feature : kCoreFeatures) {
            const bool has = supported.*(feature.field) == VK_TRUE;
            const bool uses = enabled.*(feature.field) == VK_TRUE;
            if (has) ++supportedCount;
            if (uses) ++enabledCount;
            const bool expected = has && feature.consumedByCna;
            if (uses != expected && firstMismatch.empty()) {
                firstMismatch = std::string(feature.name) + " supported=" + (has ? "true" : "false")
                    + " enabled=" + (uses ? "true" : "false")
                    + " consumed=" + (feature.consumedByCna ? "true" : "false");
            }
        }
        check(firstMismatch.empty(),
              "D enabled core features are exactly the implemented CNA feature set",
              firstMismatch.empty()
                  ? std::to_string(enabledCount) + " enabled of "
                        + std::to_string(supportedCount) + " supported"
                  : firstMismatch);

        const VkQueueFlags queueFlags = renderer->GetGraphicsQueueFlagsEXT();
        check((queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0,
              "E the ordered public-command queue is graphics-capable",
              "flags=" + std::to_string(queueFlags));

        const bool nativeCompute = (queueFlags & VK_QUEUE_COMPUTE_BIT) != 0
            && properties.limits.maxComputeWorkGroupInvocations > 0;
        check(!device.SupportsCapability(GraphicsCapability::ComputeShaders)
                  && !renderer->SupportsComputeShadersEXT()
                  && renderer->GetMaxComputeWorkGroupInvocationsEXT() == 0,
              "F native compute availability is not mistaken for an implemented CNA path",
              std::string("nativeOrderedQueue=") + (nativeCompute ? "true" : "false")
                  + " publicCapability=false publicLimit=0");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanModernFeatureDiscoveryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanModernFeatureDiscoveryTest game;
    game.Run();
    return game.getResult();
}
