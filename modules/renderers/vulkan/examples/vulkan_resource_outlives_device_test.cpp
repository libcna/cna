// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-407 -- a Texture3D, TextureCube or RenderTargetCube renderer that outlives
// its GraphicsDevice must not leak the Vulkan objects it owns, and must not read the destroyed
// VulkanRenderer on its way out.
//
// This is not a hypothetical shape. MetalResourceHealth's own
// RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove exists precisely to document that a
// base-class move can publish a renderer that outlives its device -- and in the Vulkan
// configuration that test emitted ten `vkDestroyDevice(): Object Tracking ... has not been
// destroyed` messages while passing, because liveRenderTargets_ tracks VulkanRenderTargetRenderer*
// and these three classes were in no list at all.
//
// THE DISCRIMINATOR IS NOT IN THIS FILE, and that is deliberate rather than an omission. The layer
// reports the leak DURING vkDestroyDevice -- after the last statement that could read the
// renderer's message list, out of the very object being destroyed. VULKAN-393's output gate is what
// sees it: this test is registered with FAIL_REGULAR_EXPRESSION "\[Vulkan Validation\]" like every
// other Vulkan CTest, so a leak here fails the test even though nothing below asserts it.
//
// What this file DOES assert is that the escape genuinely happened -- three renderers still alive
// after their device is gone. Without those legs a build in which nothing was constructed, or in
// which the escapes silently released early, would sail through the output gate having proved
// nothing at all.

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::ITexture3DRenderer;
using CNA::Internal::Renderers::ITextureCubeRenderer;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount;
        if (ok) ++passCount;
    }
}

int main()
{
    // Held deliberately across the device's destruction. Their use counts are what proves the
    // escape; their destructors run at the end of main, long after vkDestroyDevice.
    std::shared_ptr<ITexture3DRenderer>   escapedVolume;
    std::shared_ptr<ITextureCubeRenderer> escapedCube;
    std::shared_ptr<ITextureCubeRenderer> escapedCubeTarget;
    bool volumeConstructed = false;
    bool cubeConstructed = false;
    bool cubeTargetConstructed = false;

    {
        GraphicsDevice device;

        try
        {
            Texture3D volume(device, 4, 4, 4, /*mipMap=*/false, SurfaceFormat::Color);
            escapedVolume = volume.GetRenderer().shared_from_this();
            volumeConstructed = true;
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] Texture3D construction refused: %s\n", e.what());
        }

        try
        {
            TextureCube cube(device, 4, /*mipMap=*/false, SurfaceFormat::Color);
            escapedCube = cube.GetRenderer().shared_from_this();
            cubeConstructed = true;
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] TextureCube construction refused: %s\n", e.what());
        }

        try
        {
            // The exact shape MetalResourceHealth documents: a RenderTargetCube moved into its
            // TextureCube base, so the renderer is published through the base object and can then
            // outlive everything the device owns.
            RenderTargetCube target(device, 4, /*mipMap=*/false, SurfaceFormat::Color,
                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            if (target.GetRenderTargetCubeRenderer() != nullptr)
            {
                TextureCube escaped(std::move(target));
                escapedCubeTarget = escaped.GetRenderer().shared_from_this();
                cubeTargetConstructed = true;
            }
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] RenderTargetCube construction refused: %s\n", e.what());
        }

        // The device -- and with it the VulkanRenderer and its VkDevice -- dies here, while all
        // three shared_ptrs above are still holding their renderers.
    }

    check(volumeConstructed && escapedVolume != nullptr,
          "A a Texture3D renderer outlived its GraphicsDevice");
    check(cubeConstructed && escapedCube != nullptr,
          "B a TextureCube renderer outlived its GraphicsDevice");
    check(cubeTargetConstructed && escapedCubeTarget != nullptr,
          "C a RenderTargetCube renderer outlived its GraphicsDevice, through its TextureCube base");

    std::printf("%d/%d checks passed\n", passCount, totalCount);
    std::fflush(stdout);

    // The three destructors run below, on objects whose owner is gone. Before VULKAN-407 each read
    // owner_->device_ out of a destroyed VulkanRenderer; they are disconnected now, so this is a
    // no-op rather than a use-after-free -- and an ASan build of this test is the way to prove it.
    return (passCount == totalCount && totalCount == 3) ? 0 : 1;
}
