// SPDX-License-Identifier: MS-PL
// REMED-GFX-097: canonical SDL_GPU render-pass/pipeline compatibility reproducer.
//
// A depthless RenderTargetCube face is a valid public SpriteBatch destination. The selected face
// must receive the opaque draw, every other face must remain unchanged, and the SDL_GPU Vulkan
// route must not report VUID-vkCmdDraw-renderPass-02684. CTest makes that exact VUID fatal; the
// checks below independently prove target/face identity and pixel correctness.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 16;
    constexpr CubeMapFace kSelectedFace = CubeMapFace::NegativeZ;
    const Color kMarker(211, 73, 29, 255);
    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    const std::array<Color, 6> kInitialColors = {
        Color(11, 21, 31, 255), Color(41, 51, 61, 255),
        Color(71, 81, 91, 255), Color(101, 111, 121, 255),
        Color(131, 141, 151, 255), Color(161, 171, 181, 255),
    };

    bool Matches(const Color& got, const Color& expected)
    {
        return std::abs(got.getRProperty() - expected.getRProperty()) <= 8
            && std::abs(got.getGProperty() - expected.getGProperty()) <= 8
            && std::abs(got.getBProperty() - expected.getBProperty()) <= 8
            && std::abs(got.getAProperty() - expected.getAProperty()) <= 8;
    }
}

class SdlGpuDepthlessCubeSpriteBatchCompatibilityTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> marker_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        ++total_;
        if (ok)
            ++passed_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        marker_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1,
            std::vector<std::uint8_t>{
                static_cast<std::uint8_t>(kMarker.getRProperty()),
                static_cast<std::uint8_t>(kMarker.getGProperty()),
                static_cast<std::uint8_t>(kMarker.getBProperty()),
                static_cast<std::uint8_t>(kMarker.getAProperty()),
            }));
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Check(cube.getDepthStencilFormatProperty() == DepthFormat::None,
              "public cube target is genuinely depthless");

        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            device.SetRenderTarget(&cube, kFaces[i]);
            device.Clear(kInitialColors[i]);
        }

        device.SetRenderTarget(&cube, kSelectedFace);
        device.setViewportProperty(Viewport(0, 0, kCubeSize, kCubeSize));
        device.setScissorRectangleProperty(Rectangle(0, 0, kCubeSize, kCubeSize));

        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                        &noDepth, &noCull);
        sprites_->Draw(*marker_, Rectangle(0, 0, kCubeSize, kCubeSize),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        bool facesCorrect = true;
        const Rectangle centre(kCubeSize / 2, kCubeSize / 2, 1, 1);
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            Color got(0, 0, 0, 0);
            cube.GetData(kFaces[i], 0, &centre, &got, 0, 1);
            const Color& expected = kFaces[i] == kSelectedFace ? kMarker : kInitialColors[i];
            const bool ok = Matches(got, expected);
            facesCorrect = facesCorrect && ok;
            std::printf("  [%s] face %zu got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", i,
                        got.getRProperty(), got.getGProperty(),
                        got.getBProperty(), got.getAProperty(),
                        expected.getRProperty(), expected.getGProperty(),
                        expected.getBProperty(), expected.getAProperty());
        }
        Check(facesCorrect, "only NegativeZ receives the opaque SpriteBatch result");

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SdlGpuDepthlessCubeSpriteBatchCompatibilityTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuDepthlessCubeSpriteBatchCompatibilityTest game;
    game.Run();
    return game.Result();
}
