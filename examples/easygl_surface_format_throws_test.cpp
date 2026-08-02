// SPDX-License-Identifier: MS-PL
// Task 176: SurfaceFormat validation — unsupported formats must throw
// std::runtime_error instead of silently using RGBA8.
//
// Tests Texture2D, Texture3D, and TextureCube constructors against
// representative unsupported formats (sRGB, HDR, compressed, packed).
// SurfaceFormat::Color must NOT throw; the Skia build also verifies its
// explicitly supported SKIA-135/136 packed and colour Texture2D formats.

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

        // ── sRGB / EXT formats follow backend-specific promotion gates ──────

#if defined(CNA_BACKEND_SKIA)
        expectNoThrow("Texture2D ColorSrgbEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
        });
#else
        expectThrows("Texture2D ColorSrgbEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
        });
#endif
        expectThrows("Texture2D Bc7EXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7EXT);
        });
        expectThrows("Texture2D Bc7SrgbEXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7SrgbEXT);
        });
        expectThrows("Texture2D Dxt5SrgbEXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt5SrgbEXT);
        });
#if defined(CNA_BACKEND_SKIA)
        expectNoThrow("Texture2D ColorBgraEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorBgraEXT);
        });
#else
        expectThrows("Texture2D ColorBgraEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorBgraEXT);
        });
#endif
        expectThrows("TextureCube ColorSrgbEXT", [&]{
            TextureCube t(dev, 2, false, SurfaceFormat::ColorSrgbEXT);
        });
        expectThrows("Texture3D ColorSrgbEXT", [&]{
            Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
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
#if defined(CNA_BACKEND_SKIA)
        // SKIA-135 promotes these packed formats for Texture2D only. The SKIA-136
        // colour formats were handled above; other backends retain the Color-only boundary.
        expectNoThrow("Texture2D Bgr565", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565);
        });
        expectNoThrow("Texture2D Bgra4444", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgra4444);
        });
        expectNoThrow("Texture2D Rgba1010102", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rgba1010102);
        });
#else
        expectThrows("Texture2D Bgr565", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565);
        });
#endif
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
