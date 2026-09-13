// SPDX-License-Identifier: MS-PL
// SDLGPU-73: exact RenderTargetCube color/depth storage and transfer proof.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderTargetCubeRenderer;

namespace
{
    constexpr int kSize = 8;
    constexpr std::array<CubeMapFace, 6> kFaces{
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    constexpr std::array<SurfaceFormat, 9> kFormats{
        SurfaceFormat::Color, SurfaceFormat::Rgba64,
        SurfaceFormat::Single, SurfaceFormat::Vector2, SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable,
    };

    struct FormatInfo
    {
        int bytesPerPixel;
        int channelCount;
        bool normalized;
        bool half;
    };

    FormatInfo Info(SurfaceFormat format)
    {
        switch (format)
        {
            case SurfaceFormat::Color:       return {4, 4, true, false};
            case SurfaceFormat::Rgba64:      return {8, 4, true, false};
            case SurfaceFormat::Single:      return {4, 1, false, false};
            case SurfaceFormat::Vector2:     return {8, 2, false, false};
            case SurfaceFormat::Vector4:     return {16, 4, false, false};
            case SurfaceFormat::HalfSingle:  return {2, 1, false, true};
            case SurfaceFormat::HalfVector2: return {4, 2, false, true};
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:return {8, 4, false, true};
            default:                         return {0, 0, false, false};
        }
    }

    std::array<float, 4> Decode(SurfaceFormat format,
                                const std::array<std::uint8_t, 16>& bytes)
    {
        const FormatInfo info = Info(format);
        std::array<float, 4> result{};
        if (format == SurfaceFormat::Color)
        {
            for (int i = 0; i < 4; ++i) result[static_cast<std::size_t>(i)] = bytes[i] / 255.0f;
        }
        else if (format == SurfaceFormat::Rgba64)
        {
            std::array<std::uint16_t, 4> packed{};
            std::memcpy(packed.data(), bytes.data(), sizeof(packed));
            for (int i = 0; i < 4; ++i)
                result[static_cast<std::size_t>(i)] = packed[static_cast<std::size_t>(i)] / 65535.0f;
        }
        else if (info.half)
        {
            std::array<std::uint16_t, 4> packed{};
            std::memcpy(packed.data(), bytes.data(), static_cast<std::size_t>(info.bytesPerPixel));
            for (int i = 0; i < info.channelCount; ++i)
                result[static_cast<std::size_t>(i)] =
                    HalfTypeHelper::Convert(packed[static_cast<std::size_t>(i)]);
        }
        else
        {
            std::memcpy(result.data(), bytes.data(), static_cast<std::size_t>(info.bytesPerPixel));
        }
        return result;
    }

    bool Close(float actual, float expected, float tolerance)
    {
        return std::abs(actual - expected) <= tolerance;
    }
}

class SdlGpuRenderTargetCubeFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passed_ = 0;
    int total_ = 0;

    void Check(bool ok, const std::string& label)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();

        int supported = 0;
        for (int ordinal = 0; ordinal < 20; ++ordinal)
        {
            const auto verdict = renderer.ClassifyRenderTargetCubeFormatEXT(ordinal);
            const bool expected = std::ranges::find(kFormats, static_cast<SurfaceFormat>(ordinal))
                               != kFormats.end();
            if (verdict == CNA::Internal::Renderers::RendererFormatVerdict::Supported) ++supported;
            Check((verdict == CNA::Internal::Renderers::RendererFormatVerdict::Supported) == expected,
                  "cube format classifier ordinal " + std::to_string(ordinal));
        }
        Check(supported == 9, "exactly nine classic cube-target formats are supported");

        for (const SurfaceFormat format : kFormats)
        {
            RenderTargetCube cube(device, kSize, false, format, DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
            auto* nativeTarget = dynamic_cast<SdlGpuRenderTargetCubeRenderer*>(&cube.GetRenderer());
            bool exact = cube.getFormatProperty() == format && nativeTarget != nullptr;
            const FormatInfo info = Info(format);
            for (std::size_t face = 0; face < kFaces.size(); ++face)
            {
                const bool normalized = info.normalized;
                const float r = normalized ? static_cast<float>(face + 1) / 8.0f
                                           : static_cast<float>(face + 2);
                const float g = normalized ? 0.25f : 0.5f;
                const float b = normalized ? 0.5f : 0.25f;
                const float a = normalized ? 0.75f : 1.0f;
                device.SetRenderTarget(&cube, kFaces[face]);
                device.Clear(r, g, b, a);
            }
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            for (std::size_t face = 0; face < kFaces.size(); ++face)
            {
                std::array<std::uint8_t, 16> bytes{};
                exact = exact && nativeTarget->GetNativeDataEXT(
                    static_cast<int>(kFaces[face]), 0, kSize / 2, kSize / 2, 1, 1,
                    bytes.data(), info.bytesPerPixel);
                const auto value = Decode(format, bytes);
                const float expected[4] = {
                    info.normalized ? static_cast<float>(face + 1) / 8.0f
                                    : static_cast<float>(face + 2),
                    info.normalized ? 0.25f : 0.5f,
                    info.normalized ? 0.5f : 0.25f,
                    info.normalized ? 0.75f : 1.0f,
                };
                const float tolerance = format == SurfaceFormat::Color ? 1.0f / 255.0f
                                      : format == SurfaceFormat::Rgba64 ? 1.0f / 65535.0f
                                                                       : 0.0f;
                for (int channel = 0; channel < info.channelCount; ++channel)
                    exact = exact && Close(value[static_cast<std::size_t>(channel)],
                                           expected[channel], tolerance);
            }
            Check(exact, "all six faces retain exact format ordinal " +
                         std::to_string(static_cast<int>(format)));
        }

        {
            RenderTargetCube cube(device, kSize, false, SurfaceFormat::HdrBlendable,
                                  DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
            bool exact = cube.getMultiSampleCountProperty() == 0 ||
                         cube.getMultiSampleCountProperty() > 1;
            if (cube.getMultiSampleCountProperty() > 1)
            {
                device.SetRenderTarget(&cube, CubeMapFace::NegativeZ);
                device.Clear(4.0f, 2.0f, 1.0f, 1.0f);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                std::array<std::uint8_t, 16> bytes{};
                auto* nativeTarget =
                    dynamic_cast<SdlGpuRenderTargetCubeRenderer*>(&cube.GetRenderer());
                exact = exact && nativeTarget != nullptr &&
                        nativeTarget->GetNativeDataEXT(
                            5, 0, 4, 4, 1, 1, bytes.data(), 8);
                const auto value = Decode(SurfaceFormat::HdrBlendable, bytes);
                exact = exact && value[0] == 4.0f && value[1] == 2.0f;
            }
            Check(exact, "half-float cube MSAA resolves without RGBA8 clamping");
        }

        {
            RenderTargetCube cube(device, kSize, true, SurfaceFormat::HdrBlendable,
                                  DepthFormat::None);
            device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
            device.Clear(4.0f, 2.0f, 1.0f, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::array<std::uint8_t, 16> bytes{};
            auto* nativeTarget =
                dynamic_cast<SdlGpuRenderTargetCubeRenderer*>(&cube.GetRenderer());
            bool exact = nativeTarget != nullptr && nativeTarget->GetNativeDataEXT(
                2, 1, 1, 1, 1, 1, bytes.data(), 8);
            const auto value = Decode(SurfaceFormat::HdrBlendable, bytes);
            exact = exact && value[0] == 4.0f && value[1] == 2.0f;
            Check(exact, "half-float cube mip retains values above one");
        }

        for (const DepthFormat format : {DepthFormat::None, DepthFormat::Depth16,
                                         DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
        {
            RenderTargetCube cube(device, kSize, false, SurfaceFormat::Color, format);
            auto* target = cube.GetRenderTargetCubeRenderer();
            const bool wantsDepth = format != DepthFormat::None;
            const bool wantsStencil = format == DepthFormat::Depth24Stencil8;
            bool exact = target != nullptr && cube.getDepthStencilFormatProperty() == format &&
                         target->HasRealDepthBuffer(wantsDepth) == wantsDepth &&
                         target->HasRealStencilBuffer(wantsStencil) == wantsStencil;
            const int bits = target != nullptr ? target->DepthBufferBitsEXT() : -1;
            exact = exact && (format == DepthFormat::None ? bits == 0
                            : format == DepthFormat::Depth16 ? (bits == 16 || bits == 23)
                                                            : (bits == 24 || bits == 23));
            Check(exact, "cube depth facts for ordinal " +
                         std::to_string(static_cast<int>(format)) +
                         " use " + std::to_string(bits) + " effective bits");
        }

        for (const DepthFormat format : {DepthFormat::None, DepthFormat::Depth16,
                                         DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
        {
            RenderTarget2D target2D(device, kSize, kSize, false, SurfaceFormat::Color, format);
            auto* target = target2D.GetRenderTargetRenderer();
            const bool wantsDepth = format != DepthFormat::None;
            const bool wantsStencil = format == DepthFormat::Depth24Stencil8;
            bool exact = target != nullptr && target2D.getDepthStencilFormatProperty() == format &&
                         target->HasRealDepthBuffer(wantsDepth) == wantsDepth &&
                         target->HasRealStencilBuffer(wantsStencil) == wantsStencil;
            const int bits = target != nullptr ? target->DepthBufferBitsEXT() : -1;
            exact = exact && (format == DepthFormat::None ? bits == 0
                            : format == DepthFormat::Depth16 ? (bits == 16 || bits == 23)
                                                            : (bits == 24 || bits == 23));
            device.SetRenderTarget(&target2D);
            device.Clear(0.25f, 0.5f, 0.75f, 1.0f);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(exact, "2D target depth facts for ordinal " +
                         std::to_string(static_cast<int>(format)) +
                         " use " + std::to_string(bits) + " effective bits");
        }

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SdlGpuRenderTargetCubeFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const { return result_; }

private:
    int result_ = 1;
};

int main()
{
    SdlGpuRenderTargetCubeFormatTest game;
    game.Run();
    return game.Result();
}
