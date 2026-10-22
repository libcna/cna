// SPDX-License-Identifier: MS-PL
// Museum browser blocker: capability queries must not create a feature-gated format
// before enabling its feature. Run in a browser both normally and with tier1 hidden
// from the adapter before CNA requests its device (the device really lacks it then).
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

class OptionalFormatTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int checks_ = 0;
    int failures_ = 0;
    bool done_ = false;

    void Check(bool condition, const char* label)
    {
        ++checks_;
        failures_ += !condition;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
#if defined(__EMSCRIPTEN__)
        const bool enabled = wgpuDeviceHasFeature(renderer.Device(), WGPUFeatureName_TextureFormatsTier1);
        std::printf("[INFO] device texture-formats-tier1=%d\n", enabled);
#endif
        // This used to throw a JavaScript TypeError, escaping C++ and terminating
        // the page, even though the caller merely asked a capability question.
        const bool supported = device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rgba64);
#if defined(__EMSCRIPTEN__)
        Check(supported == enabled, "Rgba64 render-target support matches the enabled browser feature");
#endif
        Check(!device.GetRendererCapabilityReportEXT().empty(), "complete capability report returns normally");
        const auto formats = device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Rgba64);
        Check(formats.IsKnown(CNA::RendererFormatUsage::RenderTarget) &&
              formats.Supports(CNA::RendererFormatUsage::RenderTarget) == supported,
              "capability profile reports the same classified Rgba64 support");
        Check(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Color), "Color remains supported");

        bool made2D = false, refused2D = false;
        try { auto target = renderer.CreateRenderTarget2DEXT(4, 4, 0, false, false, 0,
                                                           static_cast<int>(SurfaceFormat::Rgba64)); made2D = target != nullptr; }
        catch (const System::NotSupportedException&) { refused2D = true; }
        Check(supported ? made2D && !refused2D : refused2D && !made2D,
              "exact Rgba64 2D factory creates or throws NotSupportedException according to capability");
        bool madeCube = false, refusedCube = false;
        try { auto target = renderer.CreateRenderTargetCubeEXT(4, 0, false, false, 0,
                                                             static_cast<int>(SurfaceFormat::Rgba64)); madeCube = target != nullptr; }
        catch (const System::NotSupportedException&) { refusedCube = true; }
        Check(supported ? madeCube && !refusedCube : refusedCube && !madeCube,
              "exact Rgba64 cube factory follows the same feature contract");
        // XNA RenderTarget2D.CreateRenderTarget calls GraphicsAdapter.QueryFormat and uses the
        // selected format, not the preference unconditionally. Keep that public fallback while
        // requiring the renderer's exact-format factories above to refuse an unsupported format.
        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Rgba64, DepthFormat::None);
        RenderTargetCube cube(device, 4, false, SurfaceFormat::Rgba64, DepthFormat::None);
        const auto selected = supported ? SurfaceFormat::Rgba64 : SurfaceFormat::Color;
        Check(target.getFormatProperty() == selected, "public 2D preferred-format selection is preserved");
        Check(cube.getFormatProperty() == selected, "public cube preferred-format selection is preserved");
        const Color color(17, 34, 51, 68);
        device.SetRenderTarget(&target);
        device.Clear(color);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        bool pixelsMatch = false;
        if (supported)
        {
            using PackedVector::Rgba64;
            std::array<Rgba64, 16> pixels{};
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            const Rgba64 expected(17.0f / 255, 34.0f / 255, 51.0f / 255, 68.0f / 255);
            pixelsMatch = std::ranges::all_of(pixels, [&](const auto& pixel) { return pixel == expected; });
        }
        else
        {
            std::array<Color, 16> pixels{};
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            pixelsMatch = std::ranges::all_of(pixels, [&](const auto& pixel) { return pixel == color; });
        }
        Check(pixelsMatch, "all target pixels read back exactly in the selected format");
        Check(renderer.GetUncapturedErrorCountEXT() == 0, "no uncaptured GPU validation errors");
        std::printf("OPTIONAL_FORMAT_RESULT checks=%d failures=%d\n", checks_, failures_);
        Exit();
    }

public:
    OptionalFormatTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const { return checks_ > 0 && failures_ == 0 ? 0 : 1; }
};

int main()
{
    OptionalFormatTest game;
    game.Run();
    return game.Result();
}
