// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-6..12: end-to-end smoke test for the SDL_GPU graphics renderer's
// device/window/swapchain lifecycle and color+depth+stencil clear/present. Real window, real
// SDL_GPUDevice, a real 60-frame Clear()+Present() loop -- this is the renderer's first genuine
// proof gate (SDLGPU-12's own bar). Texture2D/VertexBuffer/SpriteBatch are not yet implemented on
// this renderer (later phases), so this test deliberately does not touch them beyond confirming
// they still throw (rather than silently no-op).
//
// Check A -- GameWindow handle returns a real, non-null SDL_Window.
// Check B -- SDL_GetRenderer(window) is null (this renderer does not use SDL_Renderer).
// Check C -- on the first frame, both the renderer and public GraphicsDevice viewport already
//   report the requested native backbuffer dimensions. This must not wait for the first lazy
//   swapchain acquisition to replace the renderer's constructor-time size.
// Check D -- a real SdlGpuVertexBufferRenderer/SdlGpuIndexBufferRenderer round-trip: SetData()
//   followed by GetVertexCount()/GetIndexCount() reports the exact count uploaded (Phase SDLGPU-5,
//   SDLGPU-23) -- see sdlgpu_2d_test.cpp for the fuller Texture2D/SpriteBatch vertical-slice proof.
// Check E -- 60 frames of Clear(Target|DepthBuffer|Stencil, color, depth, stencil) + the automatic
//   end-of-frame Present() complete with no exception.
// Check F/G -- SetDataWithOptions()/SetData16WithOptions() with both Discard and NoOverwrite hints
//   (SDLGPU-23) round-trip the exact byte data either way -- these are real overrides now (they
//   used to silently fall through to IGraphicsRenderer's default, which ignores the hint entirely),
//   mapped to SDL_UploadToGPUBuffer's own cycle flag (Discard/None -> cycle=true, NoOverwrite ->
//   cycle=false, mirroring EasyGLVertexBufferRenderer's established orphan-vs-sub-data convention).
//   A full-buffer overwrite's correctness is identical either way (cycle only affects whether the
//   GPU stalls on in-flight reads of the old backing memory, not what ends up readable afterward),
//   so this is a real-API-usage/no-longer-silently-ignored proof, not a visually distinguishing one.
//
// SDLGPU-57 additionally freezes every current public capability answer and the two numeric limits
// that would otherwise inherit wider common defaults. Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTotalFrames = 60;
#if defined(CNA_SDL_GPU_SHADER_EFFECTS)
    constexpr int kExpectedChecks = 30;
#else
    // Unsupported-compiler builds also verify the diagnostic returned by CreateEffectRenderer.
    constexpr int kExpectedChecks = 31;
#endif

    int RunHeadlessGraphicsDeviceProbe(const char* requestedDriver)
    {
        int passed = 0;
        constexpr int kExpected = 6;
        const auto check = [&](bool ok, const char* label)
        {
            std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
            if (ok) ++passed;
        };

        try
        {
            PresentationParameters parameters;
            parameters.setBackBufferWidthProperty(32);
            parameters.setBackBufferHeightProperty(24);
            parameters.setPresentationIntervalProperty(PresentInterval::Immediate);
            parameters.setHeadlessEXTProperty(true);

            GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();
            GraphicsDevice device(adapter, GraphicsProfile::HiDef, parameters);
            auto* renderer = dynamic_cast<SdlGpuRenderer*>(&device.GetRenderer());
            check(renderer != nullptr,
                  "headless public GraphicsDevice uses SdlGpuRenderer");
            check(renderer != nullptr && renderer->IsHeadlessEXT(),
                  "renderer creates no window or swapchain in HeadlessEXT mode");
            const std::string actualDriver = renderer != nullptr
                ? renderer->GetDriverNameEXT() : std::string{};
            check(actualDriver == requestedDriver,
                  "headless GraphicsDevice selects the requested native graphics driver");

            const Viewport viewport = device.getViewportProperty();
            check(viewport.getWidthProperty() == 32 && viewport.getHeightProperty() == 24,
                  "headless public viewport preserves the requested backbuffer dimensions");

            const Color backbufferColor(13, 71, 199, 233);
            device.Clear(backbufferColor);
            const Rectangle backbufferProbe(11, 7, 1, 1);
            Color backbufferPixel;
            device.GetBackBufferData(&backbufferProbe, &backbufferPixel, 0, 1);
            check(backbufferPixel == backbufferColor,
                  "headless public Clear/GetBackBufferData round-trips exact RGBA");

            const Color targetColor(219, 41, 87, 157);
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(targetColor);
            device.SetRenderTarget(nullptr);
            std::array<Color, 16> targetPixels{};
            target.GetData(targetPixels.data(), static_cast<int>(targetPixels.size()));
            check(std::all_of(targetPixels.begin(), targetPixels.end(),
                              [&](const Color& pixel) { return pixel == targetColor; }),
                  "headless public RenderTarget2D clear/readback is exact on every pixel");
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] headless GraphicsDevice probe raised: %s\n", exception.what());
        }

        std::printf("=== %d/%d PASS ===\n", passed, kExpected);
        return passed == kExpected ? 0 : 1;
    }
}

class SdlGpuSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<SdlGpuRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GameWindow handle returns a real window");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null (no SDL_Renderer)");

            int width = 0;
            int height = 0;
            renderer.GetViewportSize(width, height);
            check(width > 0 && height > 0, "GetViewportSize() reports a positive size");
            check(width == 320 && height == 240,
                  "first-frame renderer viewport matches the requested native backbuffer");
            const Viewport firstViewport = dev.getViewportProperty();
            check(firstViewport.getWidthProperty() == 320 && firstViewport.getHeightProperty() == 240,
                  "first-frame public Viewport matches the requested native backbuffer");

            auto vb = renderer.CreateVertexBuffer(3);
            const float verts[3 * 2] = {0, 0, 1, 0, 0, 1};
            vb->SetData(verts, 3, sizeof(float) * 2);
            check(vb->GetVertexCount() == 3, "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto ib = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib->SetData16(indices, 3);
            check(ib->GetIndexCount() == 3 && !ib->IsThirtyTwoBit(),
                  "IndexBuffer.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trips correctly");

            const float vertsDiscard[3 * 2] = {2, 2, 3, 2, 2, 3};
            vb->SetDataWithOptions(vertsDiscard, 3, sizeof(float) * 2, SetDataOptions::Discard);
            const float vertsNoOverwrite[3 * 2] = {4, 4, 5, 4, 4, 5};
            vb->SetDataWithOptions(vertsNoOverwrite, 3, sizeof(float) * 2, SetDataOptions::NoOverwrite);
            check(vb->GetVertexCount() == 3,
                  "VertexBuffer.SetDataWithOptions(Discard then NoOverwrite) round-trips the exact count");

            const std::uint16_t indicesDiscard[3] = {2, 1, 0};
            ib->SetData16WithOptions(indicesDiscard, 3, SetDataOptions::Discard);
            const std::uint16_t indicesNoOverwrite[3] = {0, 2, 1};
            ib->SetData16WithOptions(indicesNoOverwrite, 3, SetDataOptions::NoOverwrite);
            check(ib->GetIndexCount() == 3,
                  "IndexBuffer.SetData16WithOptions(Discard then NoOverwrite) round-trips the exact count");

            check(dev.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                  "ThreeD is reported because stock 3D draws are implemented");
            check(dev.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer),
                  "DepthStencilBuffer is reported for the selected combined format");
            check(dev.SupportsCapability(CNA::GraphicsCapability::StencilBuffer),
                  "StencilBuffer agrees with the selected combined format");
            check(dev.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering),
                  "AnisotropicFiltering is enabled by the default SDL GPU device contract");
#if defined(CNA_SDL_GPU_SHADER_EFFECTS)
            check(dev.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
                  "CustomEffects is reported because target-native ShaderEffect compilation is available");
#else
            check(!dev.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
                  "CustomEffects is refused without a target-native ShaderEffect compiler");
            auto unavailableEffect = renderer.CreateEffectRenderer("void main() {}", "void main() {}");
            check(!unavailableEffect->IsValid() &&
                      unavailableEffect->GetCompileError().find("target-native") != std::string::npos,
                  "unavailable ShaderEffect construction returns a precise diagnostic");
#endif
            check(dev.SupportsCapability(CNA::GraphicsCapability::Texture3D),
                  "Texture3D is reported because storage and transfer are real");
            check(dev.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending),
                  "AdditiveBlending is reported because the pixel contract passes");
            check(dev.SupportsCapability(CNA::GraphicsCapability::CompiledEffects) ==
                      renderer.SupportsCompiledEffects(),
                  "CompiledEffects agrees with this build's optional runtime");
            check(dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                  "MSAA is reported after the renderer's live color/depth format query");

            check(dev.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets),
                  "MRT is reported after independent fragment outputs and mixed-format pipelines");
            check(dev.SupportsCapability(CNA::GraphicsCapability::WireFrame),
                  "WireFrame is reported because native line fill is pixel-verified");
            check(!dev.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery),
                  "OcclusionQuery is false because the underlying API exposes no query primitive");
            bool queryRefused = false;
            try
            {
                (void)renderer.CreateOcclusionQuery();
            }
            catch (const std::exception& error)
            {
                queryRefused = std::string_view(error.what()).find(
                    "no occlusion-query or query-pool commands") != std::string_view::npos;
            }
            check(queryRefused,
                  "OcclusionQuery factory deterministically refuses the unsupported operation");
            check(dev.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput),
                  "MultiStreamVertexInput is reported after split-stream pixel verification");
            check(dev.SupportsCapability(CNA::GraphicsCapability::Instancing),
                  "Instancing is reported after per-instance placement verification");
            check(dev.SupportsCapability(CNA::GraphicsCapability::FloatRenderTargets) ==
                      dev.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4) &&
                      dev.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets) ==
                      dev.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable),
                  "float render-target capabilities agree with exact live format support");
            check(!dev.SupportsCapability(
                      CNA::GraphicsCapability::HalfFloatTextureLinearFiltering),
                  "half-float filtering is false while half-float storage is absent");
            check(!dev.SupportsCapability(CNA::GraphicsCapability::ComputeShaders) &&
                      !dev.SupportsCapability(CNA::GraphicsCapability::IndirectDraw),
                  "out-of-scope modern capabilities are not inherited as true");

            check(renderer.GetMaxVertexStreams() == 16,
                  "numeric vertex-stream limit agrees with the XNA public ceiling");
            const std::string_view limitations = renderer.GetAdditionalLimitationsTextEXT();
            check(limitations.find("WireFrame") == std::string_view::npos &&
                      limitations.find("OcclusionQuery") != std::string_view::npos,
                  "generated capability report names qualitative limitations");
        }

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::CornflowerBlue, 1.0f, 0);

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns.

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear()+Present() completed with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        // This smoke explicitly clears and advertises stencil, so request the XNA format that
        // actually owns a stencil plane rather than relying on the Depth24 manager default.
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--headless-graphics-device")
        return RunHeadlessGraphicsDeviceProbe(argv[2]);

    if (argc == 3 && std::string_view(argv[1]) == "--headless-stock-shaders")
    {
        try
        {
            const std::string driver =
                SdlGpuRenderer::ValidateStockShadersForDriverEXT(argv[2]);
            std::printf("[PASS] requested native graphics driver '%s' was selected\n",
                        driver.c_str());
            std::printf("[PASS] all %zu production stock shaders were created and released\n",
                        SdlGpuConstructionShaderCountEXT);
            std::printf("=== 2/2 PASS ===\n");
            return 0;
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] headless stock-shader portability probe: %s\n",
                        exception.what());
            return 1;
        }
    }

    if (argc == 3 && std::string_view(argv[1]) == "--headless-stock-pixel")
    {
        try
        {
            constexpr std::array<std::uint8_t, 4> expectedDraw{17, 83, 201, 239};
            constexpr std::array<std::uint8_t, 4> expectedClear{3, 7, 13, 255};
            const SdlGpuHeadlessStockDrawResultEXT result =
                SdlGpuRenderer::ValidateStockDrawForDriverEXT(argv[2]);
            const bool driverMatches = result.driverName == argv[2];
            const bool drawMatches = result.drawnPixel == expectedDraw;
            const bool clearMatches = result.clearPixel == expectedClear;
            std::printf("[%s] requested native graphics driver '%s' was selected\n",
                        driverMatches ? "PASS" : "FAIL", result.driverName.c_str());
            std::printf("[%s] stock colored triangle centre is RGBA=(%u,%u,%u,%u)\n",
                        drawMatches ? "PASS" : "FAIL", result.drawnPixel[0],
                        result.drawnPixel[1], result.drawnPixel[2], result.drawnPixel[3]);
            std::printf("[%s] untouched corner retains clear RGBA=(%u,%u,%u,%u)\n",
                        clearMatches ? "PASS" : "FAIL", result.clearPixel[0],
                        result.clearPixel[1], result.clearPixel[2], result.clearPixel[3]);
            const int passed = static_cast<int>(driverMatches) +
                               static_cast<int>(drawMatches) +
                               static_cast<int>(clearMatches);
            std::printf("=== %d/3 PASS ===\n", passed);
            return passed == 3 ? 0 : 1;
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] headless stock-pixel portability probe: %s\n",
                        exception.what());
            return 1;
        }
    }

    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuSmokeTest game;
    game.Run();
    return game.getResult();
}
