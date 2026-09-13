// SPDX-License-Identifier: MS-PL
// SDLGPU-80: truthful ordinary-XNA OcclusionQuery behavior on SDL_gpu. The API has fences for
// command-buffer completion, but no rasterizer query/query-pool command that can count samples.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <exception>
#include <string_view>

namespace
{
    using CNA::GraphicsCapability;
    using CNA::RendererFeature;
    using CNA::RendererFeatureSupport;
    using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

    int passed = 0;
    int failed = 0;

    void Check(const bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        condition ? ++passed : ++failed;
    }
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    GraphicsDevice device;
    auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());

    Check(!device.SupportsCapability(GraphicsCapability::OcclusionQuery),
          "legacy OcclusionQuery capability is false");
    Check(device.GetRendererCapabilityProfileEXT()
              .GetFeature(RendererFeature::OcclusionQueries).support ==
              RendererFeatureSupport::Unsupported,
          "detailed capability profile reports occlusion queries unsupported");

    constexpr std::string_view expectedMessage =
        "CNA SDL_GPU: OcclusionQuery is unavailable because vendored SDL_gpu 3.5.0 exposes "
        "no occlusion-query or query-pool commands; GPU fences report only command-buffer "
        "completion and cannot count samples that pass depth/stencil.";
    bool refusedExactly = false;
    try
    {
        OcclusionQuery query(device);
        (void)query;
    }
    catch (const System::NotSupportedException& error)
    {
        refusedExactly = std::string_view(error.what()) == expectedMessage;
    }
    catch (const std::exception&)
    {
    }
    Check(refusedExactly,
          "public OcclusionQuery construction throws the exact SDL_gpu limitation");

    const std::string_view limitations = renderer.GetAdditionalLimitationsTextEXT();
    Check(limitations.find("no occlusion-query or query-pool commands") !=
              std::string_view::npos &&
              limitations.find("fences report only command-buffer completion") !=
              std::string_view::npos,
          "renderer limitations distinguish completion fences from rasterizer queries");

    // The refusal occurs before any render state changes. A real target clear/readback proves
    // the failed construction did not leave the deferred command stream or device unusable.
    RenderTarget2D target(device, 2, 2);
    device.SetRenderTarget(&target);
    device.Clear(Color::Green);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    Color pixel(0, 0, 0, 0);
    const Rectangle probe(1, 1, 1, 1);
    target.GetData(0, &probe, &pixel, 0, 1);
    Check(pixel == Color::Green,
          "device remains usable after the deterministic query refusal");

    std::printf("=== %d/%d PASS ===\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
