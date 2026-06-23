// SPDX-License-Identifier: MS-PL
// Task 176: SurfaceFormat validation — unsupported formats must throw
// std::runtime_error instead of silently using RGBA8.
//
// Tests Texture2D, Texture3D, and TextureCube constructors against
// representative unsupported formats (sRGB, HDR, compressed, packed).
// SurfaceFormat::Color must NOT throw.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SurfaceFormatThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;
    int pass_   = 0;
    int fail_   = 0;

    void expectThrows(const char* label, auto fn)
    {
        try
        {
            fn();
            std::printf("[FAIL] %s — expected std::runtime_error, no exception thrown\n", label);
            ++fail_;
        }
        catch (const std::runtime_error&)
        {
            std::printf("[PASS] %s\n", label);
            ++pass_;
        }
        catch (const std::exception& e)
        {
            std::printf("[FAIL] %s — wrong exception type: %s\n", label, e.what());
            ++fail_;
        }
    }

    void expectNoThrow(const char* label, auto fn)
    {
        try
        {
            fn();
            std::printf("[PASS] %s\n", label);
            ++pass_;
        }
        catch (const std::exception& e)
        {
            std::printf("[FAIL] %s — unexpected exception: %s\n", label, e.what());
            ++fail_;
        }
    }

    void Draw(const GameTime&) override {}

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        // ── SurfaceFormat::Color must succeed ────────────────────────────────

        expectNoThrow("Texture2D Color", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Color);
        });
        expectNoThrow("Texture3D Color", [&]{
            Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::Color);
        });
        expectNoThrow("TextureCube Color", [&]{
            TextureCube t(dev, 2, false, SurfaceFormat::Color);
        });

        // ── sRGB formats must throw (Task 176 focus) ─────────────────────────

        expectThrows("Texture2D ColorSrgb", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorSrgb);
        });
        expectThrows("Texture2D Dxt1Srgb", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt1Srgb);
        });
        expectThrows("Texture2D Dxt3Srgb", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt3Srgb);
        });
        expectThrows("Texture2D Dxt5Srgb", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt5Srgb);
        });
        expectThrows("Texture2D Bgr565Srgb", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565Srgb);
        });
        expectThrows("TextureCube ColorSrgb", [&]{
            TextureCube t(dev, 2, false, SurfaceFormat::ColorSrgb);
        });
        expectThrows("Texture3D ColorSrgb", [&]{
            Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::ColorSrgb);
        });

        // ── Other unsupported formats must throw ─────────────────────────────

        expectThrows("Texture2D HalfVector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfVector4);
        });
        expectThrows("Texture2D HdrBlendable", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HdrBlendable);
        });
        expectThrows("Texture2D Dxt1", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt1);
        });
        expectThrows("Texture2D Alpha8", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Alpha8);
        });
        expectThrows("Texture2D Bgr565", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565);
        });
        expectThrows("Texture2D Vector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Vector4);
        });

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        result_ = (fail_ == 0) ? 0 : 1;
        Exit();
    }

public:
    SurfaceFormatThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    SurfaceFormatThrowsTest game;
    game.Run();
    return game.getResult();
}
