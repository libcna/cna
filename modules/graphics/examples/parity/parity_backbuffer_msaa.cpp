// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-85: ordinary PresentationParameters.MultiSampleCount must create a
// genuinely multisampled backbuffer, not merely echo the requested integer.
//
// This is intentionally a standalone GraphicsDevice fixture rather than a ParityFixture Game.
// CNA's Game owns its GraphicsDevice before GraphicsDeviceManager exists, while EasyGL cannot
// change its construction-time MSAA mode during the manager's later Reset. Direct construction is
// the ordinary XNA API seam that both renderers implement and lets one identical source verify the
// actual initial request. SDL GPU additionally registers this binary with `--reset` to exercise its
// stronger, XNA/FNA-correct in-place Reset path without asking EasyGL to imitate behavior it lacks.
//
// The right triangle's diagonal crosses a fixed scanline through pixel centres. A single-sample
// opaque draw can produce only black or white there; a real multisample resolve necessarily
// produces at least one intermediate coverage value. The deep-interior pixel independently proves
// that the resolve contains rendered geometry rather than an uninitialized edge-like value.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 128;
    constexpr int kScanY = kSize / 2;
    int g_result = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) g_result = 1;
    }

    struct FrameResult
    {
        int appliedSampleCount = 0;
        int intermediatePixels = 0;
        Color inside{0, 0, 0, 0};
        Color outside{0, 0, 0, 0};
        std::vector<Color> pixels;
    };

    FrameResult DrawAndRead(GraphicsDevice& device, BasicEffect& effect, VertexBuffer& buffer)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));

        // Split the frame into two backbuffer segments. The second pass must LOAD the first one's
        // multisample samples after this unrelated target pass, not load undefined data or merely
        // preserve the already-resolved single-sample image.
        RenderTarget2D separator(device, 4, 4, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&separator);
        device.Clear(Color(17, 31, 47, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

        FrameResult result;
        result.appliedSampleCount = device.getPresentationParametersProperty()
                                        .getMultiSampleCountProperty();
        result.pixels.resize(static_cast<std::size_t>(kSize * kSize));
        device.GetBackBufferData(result.pixels.data(), 0,
                                 static_cast<int>(result.pixels.size()));
        result.inside = result.pixels[20 * kSize + 20];
        result.outside = result.pixels[110 * kSize + 110];
        for (int x = kSize / 2 - 10; x <= kSize / 2 + 10; ++x)
        {
            const int red = result.pixels[static_cast<std::size_t>(kScanY * kSize + x)]
                                .getRProperty();
            if (red > 20 && red < 235)
                ++result.intermediatePixels;
        }
        return result;
    }

    bool WriteDump(const char* path, const std::vector<Color>& pixels)
    {
        std::vector<unsigned char> rgba(pixels.size() * 4u);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i * 4u + 0u] = pixels[i].getRProperty();
            rgba[i * 4u + 1u] = pixels[i].getGProperty();
            rgba[i * 4u + 2u] = pixels[i].getBProperty();
            rgba[i * 4u + 3u] = pixels[i].getAProperty();
        }
        std::FILE* file = std::fopen(path, "wb");
        if (file == nullptr) return false;
        const bool complete = std::fwrite(rgba.data(), 1, rgba.size(), file) == rgba.size();
        std::fclose(file);
        if (complete)
            std::printf("[dump] wrote %zu bytes (%dx%d RGBA8) to %s\n",
                        rgba.size(), kSize, kSize, path);
        return complete;
    }
}

int main(int argc, char** argv)
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    const bool resetSequence = argc > 1 && std::strcmp(argv[1], "--reset") == 0;

    PresentationParameters parameters;
    parameters.setBackBufferWidthProperty(kSize);
    parameters.setBackBufferHeightProperty(kSize);
    parameters.setMultiSampleCountProperty(4);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);

    BasicEffect effect(device);
    effect.setVertexColorEnabledProperty(true);
    effect.setTextureEnabledProperty(false);
    effect.setLightingEnabledProperty(false);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
        0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));

    const VertexPositionColor vertices[3] = {
        {Vector3(0.0f, 0.0f, 0.0f), Color::White},
        {Vector3(static_cast<float>(kSize), 0.0f, 0.0f), Color::White},
        {Vector3(0.0f, static_cast<float>(kSize), 0.0f), Color::White},
    };
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
    buffer.SetData(vertices, 0, 3);

    FrameResult msaa = DrawAndRead(device, effect, buffer);
    Check(msaa.appliedSampleCount > 1,
          "construction reports a real device-clamped backbuffer sample count, got " +
              std::to_string(msaa.appliedSampleCount));
    Check(msaa.intermediatePixels > 0,
          "the opaque diagonal resolves to intermediate coverage pixels (count " +
              std::to_string(msaa.intermediatePixels) + ")");
    Check(msaa.inside.getRProperty() > 235 && msaa.inside.getAProperty() > 235,
          "a deep-interior pixel survives the multisample resolve as opaque white");
    Check(msaa.outside.getRProperty() < 20 && msaa.outside.getGProperty() < 20 &&
              msaa.outside.getBProperty() < 20 && msaa.outside.getAProperty() > 235,
          "an untouched pixel keeps the first backbuffer segment's clear across a target switch");

    if (resetSequence)
    {
        PresentationParameters next = device.getPresentationParametersProperty().Clone();
        next.setMultiSampleCountProperty(0);
        device.Reset(next);
        const FrameResult single = DrawAndRead(device, effect, buffer);
        Check(single.appliedSampleCount == 0,
              "Reset from MSAA to zero reports single-sample state");
        Check(single.intermediatePixels == 0,
              "after Reset to zero the same opaque edge contains no coverage blend");

        next = device.getPresentationParametersProperty().Clone();
        next.setMultiSampleCountProperty(4);
        device.Reset(next);
        msaa = DrawAndRead(device, effect, buffer);
        Check(msaa.appliedSampleCount > 1,
              "Reset back to MSAA restores a supported applied count");
        Check(msaa.intermediatePixels > 0,
              "the A-to-B-to-A reset restores real multisample edge coverage");
    }
    else if (argc > 1)
    {
        Check(WriteDump(argv[1], msaa.pixels), "the resolved RGBA8 frame dump was written");
    }

    return g_result;
}
