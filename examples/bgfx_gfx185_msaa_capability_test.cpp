// SPDX-License-Identifier: MS-PL
// REMED-GFX-185: bgfx may reduce a BGFX_TEXTURE_RT_MSAA_Xn request to the active renderer's
// native ceiling. CNA must report that selected count, and must encode the same count on colour
// and depth attachments for RenderTarget2D and RenderTargetCube.

#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers::Bgfx;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    int ClosestMsaaPower(int value)
    {
        if (value <= 1) return 0;
        unsigned int result = static_cast<unsigned int>(value) - 1;
        result |= result >> 1;
        result |= result >> 2;
        result |= result >> 4;
        result |= result >> 8;
        result |= result >> 16;
        result += 1;
        if (static_cast<int>(result) == value) return static_cast<int>(result);
        return static_cast<int>(result >> 1);
    }

    int NormalizeBgfxLimit(uint32_t raw)
    {
        const uint32_t capped = std::min<uint32_t>(raw, 16);
        if (capped >= 16) return 16;
        if (capped >= 8) return 8;
        if (capped >= 4) return 4;
        if (capped >= 2) return 2;
        return 0;
    }

    bool MapDepth(DepthFormat depth, bgfx::TextureFormat::Enum& format)
    {
        switch (depth)
        {
        case DepthFormat::Depth16:         format = bgfx::TextureFormat::D16; return true;
        case DepthFormat::Depth24:         format = bgfx::TextureFormat::D24; return true;
        case DepthFormat::Depth24Stencil8: format = bgfx::TextureFormat::D24S8; return true;
        case DepthFormat::None:            return false;
        }
        return false;
    }

    int CapabilityLimit(DepthFormat depth)
    {
        const bgfx::Caps* caps = bgfx::getCaps();
        constexpr uint32_t required = BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA;
        if ((caps->formats[bgfx::TextureFormat::RGBA8] & required) == 0) return 0;
        bgfx::TextureFormat::Enum format;
        if (MapDepth(depth, format) && (caps->formats[format] & required) == 0) return 0;
        return NormalizeBgfxLimit(caps->limits.maxMsaa);
    }

    int ExpectedApplied(int requested, DepthFormat depth)
    {
        return std::min(ClosestMsaaPower(requested), CapabilityLimit(depth));
    }

    int DecodeRtSamples(uint64_t flags)
    {
        const uint64_t field =
            (flags & BGFX_TEXTURE_RT_MSAA_MASK) >> BGFX_TEXTURE_RT_MSAA_SHIFT;
        if (field <= 1) return 0;
        return 1 << static_cast<int>(field - 1);
    }

    struct EdgeResult
    {
        int black = 0;
        int white = 0;
        int intermediate = 0;
    };

    EdgeResult Classify(const std::vector<Color>& pixels)
    {
        EdgeResult result;
        for (const Color& pixel : pixels)
        {
            const int red = pixel.getRProperty();
            if (red <= 20) ++result.black;
            else if (red >= 235) ++result.white;
            else ++result.intermediate;
        }
        return result;
    }
}

class BgfxGfx185MsaaCapabilityTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<RenderTarget2D> held2D_;
    std::unique_ptr<RenderTargetCube> heldCube_;
    int checks_ = 0;
    int failures_ = 0;
    bool done_ = false;

    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        if (!condition) ++failures_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
    }

    void PrintCapabilities() const
    {
        const bgfx::Caps* caps = bgfx::getCaps();
        std::printf("[INFO] active renderer=%s, maxMsaa=%u, RGBA8 caps=0x%08x, "
                    "D16 caps=0x%08x, D24 caps=0x%08x, D24S8 caps=0x%08x\n",
                    bgfx::getRendererName(bgfx::getRendererType()),
                    static_cast<unsigned>(caps->limits.maxMsaa),
                    caps->formats[bgfx::TextureFormat::RGBA8],
                    caps->formats[bgfx::TextureFormat::D16],
                    caps->formats[bgfx::TextureFormat::D24],
                    caps->formats[bgfx::TextureFormat::D24S8]);
    }

    void RunCapabilityDifferentials()
    {
        struct Case { uint32_t nativeLimit; int expected; };
        constexpr Case cases[] = {
            {0, 0}, {1, 0}, {2, 2}, {3, 2}, {4, 4},
            {7, 4}, {8, 8}, {15, 8}, {16, 16}, {64, 16},
        };
        for (const Case& item : cases)
            Check(Detail::NormalizeRenderTargetMsaaLimitEXT(item.nativeLimit, true, true) ==
                      item.expected,
                  "native capability " + std::to_string(item.nativeLimit) +
                  " normalizes to " + std::to_string(item.expected));
        Check(Detail::NormalizeRenderTargetMsaaLimitEXT(16, false, true) == 0,
              "missing colour MSAA capability disables the shared attachment count");
        Check(Detail::NormalizeRenderTargetMsaaLimitEXT(16, true, false) == 0,
              "missing depth MSAA capability disables the shared attachment count");
    }

    void CheckAttachmentFlags(const BgfxRenderTargetRenderer& renderer, DepthFormat depth,
                              int applied, const std::string& label)
    {
        const int colourSamples = DecodeRtSamples(renderer.creationFlags_);
        Check(colourSamples == applied,
              label + ": 2D colour attachment selects " + std::to_string(colourSamples) + "x");

        if (depth == DepthFormat::None)
        {
            Check(renderer.depthCreationFlags_ == 0, label + ": 2D has no depth attachment");
            return;
        }

        const int depthSamples = DecodeRtSamples(renderer.depthCreationFlags_);
        Check(depthSamples == applied,
              label + ": 2D depth attachment selects " + std::to_string(depthSamples) + "x");
        Check(((renderer.depthCreationFlags_ & BGFX_TEXTURE_RT_WRITE_ONLY) != 0) == (applied > 0),
              label + ": GFX-163 write-only depth rule is unchanged");
    }

    void CheckAttachmentFlags(const BgfxRenderTargetCubeRenderer& renderer, DepthFormat depth,
                              int applied, const std::string& label)
    {
        const uint64_t producerFlags =
            applied > 0 ? renderer.msaaProducerCreationFlags_ : renderer.creationFlags_;
        const int colourSamples = DecodeRtSamples(producerFlags);
        Check(colourSamples == applied,
              label + ": cube colour producer selects " + std::to_string(colourSamples) + "x");
        if (applied > 0)
            Check(DecodeRtSamples(renderer.creationFlags_) == 0,
                  label + ": GFX-195 resolved cube destination stays single-sampled");

        if (depth == DepthFormat::None)
        {
            Check(renderer.depthCreationFlags_ == 0, label + ": cube has no depth attachment");
            return;
        }

        const int depthSamples = DecodeRtSamples(renderer.depthCreationFlags_);
        Check(depthSamples == applied,
              label + ": cube depth attachment selects " + std::to_string(depthSamples) + "x");
        Check(((renderer.depthCreationFlags_ & BGFX_TEXTURE_RT_WRITE_ONLY) != 0) == (applied > 0),
              label + ": cube keeps the GFX-163 write-only depth rule");
    }

    void RunMatrix(GraphicsDevice& device)
    {
        constexpr std::array<int, 8> requests = {
            0, 1, 2, 3, 4, 8, 16, std::numeric_limits<int>::max()
        };
        constexpr std::array<DepthFormat, 4> depths = {
            DepthFormat::None, DepthFormat::Depth16,
            DepthFormat::Depth24, DepthFormat::Depth24Stencil8
        };

        for (const bool mipMap : {false, true})
        {
            for (const DepthFormat depth : depths)
            {
                for (const int requested : requests)
                {
                    const int expected = ExpectedApplied(requested, depth);
                    const std::string label = "request=" + std::to_string(requested) +
                        ", depth=" + std::to_string(static_cast<int>(depth)) +
                        ", mipMap=" + (mipMap ? "true" : "false");

                    RenderTarget2D target2D(device, 8, 8, mipMap, SurfaceFormat::Color, depth,
                                             requested, RenderTargetUsage::DiscardContents);
                    RenderTargetCube targetCube(device, 8, mipMap, SurfaceFormat::Color, depth,
                                                requested, RenderTargetUsage::DiscardContents);
                    const int applied2D = target2D.getMultiSampleCountProperty();
                    const int appliedCube = targetCube.getMultiSampleCountProperty();

                    std::printf("[INFO] %s: capability=%d, bgfx-selected/public 2D=%d, cube=%d\n",
                                label.c_str(), CapabilityLimit(depth), applied2D, appliedCube);
                    Check(applied2D == expected,
                          label + ": RenderTarget2D reports exact applied " +
                          std::to_string(expected) + "x");
                    Check(appliedCube == expected,
                          label + ": RenderTargetCube reports exact applied " +
                          std::to_string(expected) + "x");
                    Check(applied2D == appliedCube,
                          label + ": 2D and cube share the selection mechanism");
                    if (ClosestMsaaPower(requested) > 0 && CapabilityLimit(depth) > 0)
                        Check(expected > 0, label + ": a supported count is not disabled");

                    auto* renderer2D = dynamic_cast<BgfxRenderTargetRenderer*>(
                        target2D.GetRenderTargetRenderer());
                    auto* rendererCube = dynamic_cast<BgfxRenderTargetCubeRenderer*>(
                        targetCube.GetRenderTargetCubeRenderer());
                    Check(renderer2D != nullptr && rendererCube != nullptr,
                          label + ": concrete Bgfx attachment diagnostics are available");
                    if (renderer2D && rendererCube)
                    {
                        CheckAttachmentFlags(*renderer2D, depth, expected, label);
                        CheckAttachmentFlags(*rendererCube, depth, expected, label);
                    }

                    Check(target2D.getLevelCountProperty() == (mipMap ? 4 : 1) &&
                          targetCube.getLevelCountProperty() == (mipMap ? 4 : 1),
                          label + ": mipmapped/non-mipmapped storage policy is unchanged");

                    target2D.Dispose();
                    targetCube.Dispose();
                    Check(target2D.GetRenderTargetRenderer() == nullptr &&
                          targetCube.GetRenderTargetCubeRenderer() == nullptr,
                          label + ": explicit disposal releases both target mechanisms");
                }
            }
        }
    }

    void DrawSubpixelEdge(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();

        // Put a vertical edge at x=16.6: a single sample at the pixel centre makes a binary
        // decision, while 2x/4x coverage necessarily straddles the edge and resolves grey.
        constexpr float edgeNdc = (16.6f / static_cast<float>(kSize)) * 2.0f - 1.0f;
        const Color white(255, 255, 255, 255);
        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), white},
            {Vector3(edgeNdc, 1.0f, 0.0f), white},
            {Vector3(-1.0f, -1.0f, 0.0f), white},
            {Vector3(edgeNdc, 1.0f, 0.0f), white},
            {Vector3(edgeNdc,-1.0f, 0.0f), white},
            {Vector3(-1.0f, -1.0f, 0.0f), white},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    EdgeResult Render2DEdge(GraphicsDevice& device, int requested, DepthFormat depth)
    {
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, depth,
                              requested, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        DrawSubpixelEdge(device);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(kSize * kSize, Color(7, 11, 19, 197));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return Classify(pixels);
    }

    EdgeResult RenderCubeEdge(GraphicsDevice& device, int requested, DepthFormat depth)
    {
        RenderTargetCube target(device, kSize, false, SurfaceFormat::Color, depth,
                                requested, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target, CubeMapFace::PositiveX);
        device.Clear(Color(0, 0, 0, 255));
        DrawSubpixelEdge(device);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(kSize * kSize, Color(7, 11, 19, 197));
        target.GetData(CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
        return Classify(pixels);
    }

    void RunFunctionalOracle(GraphicsDevice& device)
    {
        if (CapabilityLimit(DepthFormat::Depth24Stencil8) == 0)
        {
            Check(bgfx::getRendererType() == bgfx::RendererType::Noop,
                  "zero-capability renderer is the explicit Noop control");
            return;
        }

        const EdgeResult single2D = Render2DEdge(device, 0, DepthFormat::None);
        const EdgeResult msaa2D = Render2DEdge(device, 16, DepthFormat::Depth24Stencil8);
        const EdgeResult singleCube = RenderCubeEdge(device, 0, DepthFormat::None);
        const EdgeResult msaaCube = RenderCubeEdge(device, 16, DepthFormat::Depth24Stencil8);

        std::printf("[INFO] edge oracle: 2D 1x=(black=%d white=%d intermediate=%d), "
                    "2D MSAA=(%d,%d,%d), cube 1x=(%d,%d,%d), cube MSAA=(%d,%d,%d)\n",
                    single2D.black, single2D.white, single2D.intermediate,
                    msaa2D.black, msaa2D.white, msaa2D.intermediate,
                    singleCube.black, singleCube.white, singleCube.intermediate,
                    msaaCube.black, msaaCube.white, msaaCube.intermediate);
        Check(single2D.black > 0 && single2D.white > 0 && single2D.intermediate == 0,
              "RenderTarget2D 1x control is non-vacuous binary output");
        Check(msaa2D.black > 0 && msaa2D.white > 0 && msaa2D.intermediate > 0,
              "RenderTarget2D applied MSAA has genuinely averaged edge coverage");
        Check(singleCube.black > 0 && singleCube.white > 0 && singleCube.intermediate == 0,
              "RenderTargetCube 1x control is non-vacuous binary output");
        Check(msaaCube.black > 0 && msaaCube.white > 0 && msaaCube.intermediate > 0,
              "RenderTargetCube applied MSAA has genuinely averaged edge coverage");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        PrintCapabilities();
        RunCapabilityDifferentials();
        RunMatrix(device);
        RunFunctionalOracle(device);

        held2D_ = std::make_unique<RenderTarget2D>(
            device, 8, 8, true, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
            std::numeric_limits<int>::max(), RenderTargetUsage::DiscardContents);
        heldCube_ = std::make_unique<RenderTargetCube>(
            device, 8, true, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
            std::numeric_limits<int>::max(), RenderTargetUsage::DiscardContents);
        Check(held2D_->getMultiSampleCountProperty() ==
                  heldCube_->getMultiSampleCountProperty(),
              "resources retained to device teardown share the applied count");

        std::printf("[INFO] REMED-GFX-185: %d/%d checks passed\n",
                    checks_ - failures_, checks_);
        Exit();
    }

public:
    BgfxGfx185MsaaCapabilityTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(96);
        manager_->setPreferredBackBufferHeightProperty(96);
    }

    int Result() const { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxGfx185MsaaCapabilityTest game;
    game.Run();
    return game.Result();
}
