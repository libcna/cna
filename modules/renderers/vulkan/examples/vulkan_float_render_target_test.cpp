// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2223/MOD-2234: real Vulkan float/HDR RenderTarget2D and
// RenderTargetCube storage must preserve exact native formats and values above 1.0.

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderTargetCubeRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderTargetRenderer;

namespace
{
    constexpr int kSize = 4;
    constexpr int kOddWidth = 7;
    constexpr int kOddHeight = 5;
    constexpr float kRed = 4.0f;
    constexpr float kGreen = 2.0f;
    constexpr float kBlue = 1.0f;

    bool Near(const float left, const float right)
    {
        return std::fabs(left - right) <= 0.001f;
    }

    bool Matches(const Vector4& value)
    {
        return Near(value.X, kRed) && Near(value.Y, kGreen) &&
               Near(value.Z, kBlue) && Near(value.W, 1.0f);
    }
}

class VulkanFloatRenderTargetTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void Check(const bool ok, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "A live renderer is Vulkan");
        if (renderer == nullptr)
        {
            Exit();
            return;
        }

        struct ExpectedFormat
        {
            SurfaceFormat surface;
            VkFormat native;
            int bytes;
        };
        constexpr std::array<ExpectedFormat, 8> expected{{
            {SurfaceFormat::Rgba64, VK_FORMAT_R16G16B16A16_UNORM, 8},
            {SurfaceFormat::Single, VK_FORMAT_R32_SFLOAT, 4},
            {SurfaceFormat::Vector2, VK_FORMAT_R32G32_SFLOAT, 8},
            {SurfaceFormat::Vector4, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
            {SurfaceFormat::HalfSingle, VK_FORMAT_R16_SFLOAT, 2},
            {SurfaceFormat::HalfVector2, VK_FORMAT_R16G16_SFLOAT, 4},
            {SurfaceFormat::HalfVector4, VK_FORMAT_R16G16B16A16_SFLOAT, 8},
            {SurfaceFormat::HdrBlendable, VK_FORMAT_R16G16B16A16_SFLOAT, 8},
        }};
        bool mappingsMatch = true;
        bool allocationsMatch = true;
        int supportedCount = 0;
        for (const auto& item : expected)
        {
            VulkanRenderer::VulkanSurfaceFormatStorageEXT storage{};
            mappingsMatch = mappingsMatch &&
                renderer->MapRenderTargetFormatToStorageEXT(
                    static_cast<int>(item.surface), storage) &&
                storage.format == item.native && storage.bytesPerTexel == item.bytes;
            if (!device.SupportsSurfaceFormatAsRenderTargetEXT(item.surface)) continue;
            ++supportedCount;
            RenderTarget2D target(
                device, kSize, kSize, false, item.surface, DepthFormat::None);
            auto* native = dynamic_cast<VulkanRenderTargetRenderer*>(
                target.GetRenderTargetRenderer());
            allocationsMatch = allocationsMatch && native != nullptr &&
                native->GetVkFormatEXT() == item.native &&
                native->GetSurfaceFormatEXT() == static_cast<int>(item.surface);
        }
        Check(mappingsMatch, "B all implemented non-Color target mappings are exact");
        Check(allocationsMatch && supportedCount > 0,
              "C every advertised float target allocates its requested VkFormat",
              std::to_string(supportedCount) + " formats supported by this device");

        const bool hasHalf =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4);
        bool oneMeansSingleSample = true;
        if (hasHalf)
        {
            RenderTarget2D oneSample(
                device, kSize, kSize, false, SurfaceFormat::HalfVector4,
                DepthFormat::None, 1);
            oneMeansSingleSample = oneSample.getMultiSampleCountProperty() == 0;
        }
        Check(!hasHalf || oneMeansSingleSample,
              "D a preferred sample count of one remains a valid single-sample target");

        bool vectorClear = true;
        bool halfClear = true;
        bool sampledDraw = true;
        bool mixedMrt = true;
        const bool hasVector = device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4);
        if (hasVector)
        {
            RenderTarget2D target(
                device, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Vector4> pixels(static_cast<std::size_t>(kSize) * kSize);
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            for (const Vector4& pixel : pixels) vectorClear = vectorClear && Matches(pixel);
        }
        Check(!hasVector || vectorClear,
              "E Vector4 clear/readback preserves values above one",
              hasVector ? "advertised and exercised" : "not advertised on this device");

        if (hasHalf)
        {
            RenderTarget2D target(
                device, kSize, kSize, false, SurfaceFormat::HalfVector4, DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            for (const HalfVector4& pixel : pixels)
                halfClear = halfClear && Matches(pixel.ToVector4());
        }
        Check(!hasHalf || halfClear,
              "F HalfVector4 clear/readback preserves values above one",
              hasHalf ? "advertised and exercised" : "not advertised on this device");

        if (hasVector && hasHalf)
        {
            RenderTarget2D source(
                device, kSize, kSize, false, SurfaceFormat::HalfVector4, DepthFormat::None);
            RenderTarget2D destination(
                device, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);
            device.SetRenderTarget(&source);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTarget(&destination);
            device.Clear(0.0f, 0.0f, 0.0f, 0.0f);
            SpriteBatch batch(device);
            SamplerState point = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(source, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Vector4> pixels(static_cast<std::size_t>(kSize) * kSize);
            destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
            for (const Vector4& pixel : pixels) sampledDraw = sampledDraw && Matches(pixel);

            RenderTarget2D color(
                device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
            const std::vector<RenderTargetBinding> bindings{
                RenderTargetBinding(&destination), RenderTargetBinding(&color)};
            device.SetRenderTargets(bindings);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTargets({});
            std::vector<Color> colorPixels(
                static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
            color.GetData(colorPixels.data(), static_cast<int>(colorPixels.size()));
            for (const Color& pixel : colorPixels)
                mixedMrt = mixedMrt && pixel == Color::White;
        }
        Check(!(hasVector && hasHalf) || sampledDraw,
              "G HalfVector4 sampling and Vector4 SpriteBatch draw preserve HDR values",
              hasVector && hasHalf ? "advertised and exercised" : "required formats unavailable");
        Check(!(hasVector && hasHalf) || mixedMrt,
              "H mixed Vector4 and Color MRT uses matching attachment formats",
              hasVector && hasHalf ? "advertised and exercised" : "required formats unavailable");

        bool refused = false;
        try
        {
            RenderTarget2D unsupported(
                device, kSize, kSize, false, SurfaceFormat::Dxt1, DepthFormat::None);
        }
        catch (...)
        {
            refused = true;
        }
        Check(refused, "I unsupported compressed target is refused without substitution");

        const CNA::RendererFormatSupport halfSupport =
            device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::HalfVector4);
        const bool multisampleAdvertised =
            halfSupport.Supports(CNA::RendererFormatUsage::Multisample);
        bool multisampleCreated = false;
        bool multisamplePreserved = false;
        try
        {
            RenderTarget2D multisampled(
                device, kOddWidth, kOddHeight, false, SurfaceFormat::HalfVector4,
                DepthFormat::None, 4);
            multisampleCreated = multisampled.getMultiSampleCountProperty() > 0;
            device.SetRenderTarget(&multisampled);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<HalfVector4> pixels(
                static_cast<std::size_t>(kOddWidth) * kOddHeight);
            multisampled.GetData(pixels.data(), static_cast<int>(pixels.size()));
            multisamplePreserved = true;
            for (const HalfVector4& pixel : pixels)
                multisamplePreserved = multisamplePreserved && Matches(pixel.ToVector4());
        }
        catch (...)
        {
            multisampleCreated = false;
        }
        Check(multisampleCreated == multisampleAdvertised &&
                  (!multisampleCreated || multisamplePreserved),
              "J odd-sized HalfVector4 MSAA agrees with construction and unclamped resolve",
              std::string("advertised=") + (multisampleAdvertised ? "true" : "false"));

        const bool mipAdvertised =
            halfSupport.Supports(CNA::RendererFormatUsage::Mipmapped);
        bool mipCreated = false;
        bool mipPreserved = false;
        try
        {
            RenderTarget2D mipped(
                device, kOddWidth, kOddHeight, true, SurfaceFormat::HalfVector4,
                DepthFormat::None);
            mipCreated = mipped.getLevelCountProperty() == 3;
            device.SetRenderTarget(&mipped);
            device.Clear(kRed, kGreen, kBlue, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // Integer halving follows the real chain: 7x5 -> 3x2 -> 1x1.
            std::vector<HalfVector4> levelOne(6);
            mipped.GetData(1, nullptr, levelOne.data(), 0,
                           static_cast<int>(levelOne.size()));
            mipPreserved = true;
            for (const HalfVector4& pixel : levelOne)
                mipPreserved = mipPreserved && Matches(pixel.ToVector4());
        }
        catch (...)
        {
            mipCreated = false;
        }
        Check(mipCreated == mipAdvertised && (!mipCreated || mipPreserved),
              "K odd-sized HalfVector4 mip chain agrees with generated content",
              std::string("advertised=") + (mipAdvertised ? "true" : "false"));

        bool cubeAllocationsMatch = true;
        int cubeSupportedCount = 0;
        const auto verifyCubeAllocation = [&](const SurfaceFormat surface,
                                              const VkFormat nativeFormat)
        {
            if (!device.SupportsSurfaceFormatAsRenderTargetEXT(surface)) return;
            ++cubeSupportedCount;
            try
            {
                RenderTargetCube cube(
                    device, kSize, false, surface, DepthFormat::None);
                auto* native = dynamic_cast<VulkanRenderTargetCubeRenderer*>(
                    &cube.GetRenderer());
                cubeAllocationsMatch = cubeAllocationsMatch && native != nullptr &&
                    native->GetVkFormatEXT() == nativeFormat &&
                    native->GetSurfaceFormatEXT() == static_cast<int>(surface);
            }
            catch (...)
            {
                cubeAllocationsMatch = false;
            }
        };
        verifyCubeAllocation(SurfaceFormat::Color, VK_FORMAT_R8G8B8A8_UNORM);
        for (const auto& item : expected)
            verifyCubeAllocation(item.surface, item.native);
        Check(cubeAllocationsMatch && cubeSupportedCount > 0,
              "L every advertised cube target allocates its requested VkFormat",
              std::to_string(cubeSupportedCount) + " formats supported by this device");

        const bool hasHdr =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable);
        bool cubePreserved = true;
        std::string cubeDetail = hasHdr ? "advertised and exercised" :
                                          "HdrBlendable unavailable on this device";
        if (hasHdr)
        {
            try
            {
                RenderTargetCube cube(
                    device, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);
                auto* native = dynamic_cast<VulkanRenderTargetCubeRenderer*>(
                    &cube.GetRenderer());
                cubePreserved = native != nullptr &&
                    native->GetVkFormatEXT() == VK_FORMAT_R16G16B16A16_SFLOAT &&
                    native->GetSurfaceFormatEXT() ==
                        static_cast<int>(SurfaceFormat::HdrBlendable);

                constexpr std::array<CubeMapFace, 6> faces{
                    CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                    CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                    CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
                for (std::size_t face = 0; face < faces.size(); ++face)
                {
                    device.SetRenderTarget(&cube, faces[face]);
                    device.Clear(static_cast<float>(face + 2), 1.0f, 0.5f, 1.0f);
                }
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

                for (std::size_t face = 0; face < faces.size(); ++face)
                {
                    std::vector<std::uint8_t> bytes(
                        static_cast<std::size_t>(kSize) * kSize * sizeof(std::uint64_t));
                    cubePreserved = cubePreserved && native != nullptr &&
                        native->GetNativeDataEXT(
                            static_cast<int>(faces[face]), 0, 0, 0, kSize, kSize,
                            bytes.data(), static_cast<int>(bytes.size()));
                    const std::uint64_t expected = HalfVector4(
                        static_cast<float>(face + 2), 1.0f, 0.5f, 1.0f)
                        .getPackedValueProperty();
                    for (std::size_t texel = 0;
                         texel < static_cast<std::size_t>(kSize) * kSize; ++texel)
                    {
                        std::uint64_t actual = 0;
                        for (std::size_t byte = 0; byte < sizeof(actual); ++byte)
                            actual |= static_cast<std::uint64_t>(
                                bytes[texel * sizeof(actual) + byte]) << (byte * 8u);
                        cubePreserved = cubePreserved && actual == expected;
                    }
                }
            }
            catch (const std::exception& error)
            {
                cubePreserved = false;
                cubeDetail = error.what();
            }
        }
        Check(!hasHdr || cubePreserved,
              "M HdrBlendable cube preserves six distinct unclamped HDR faces",
              cubeDetail);

        std::printf("RESULT: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    VulkanFloatRenderTargetTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const noexcept { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanFloatRenderTargetTest game;
    game.Run();
    return game.Result();
}
