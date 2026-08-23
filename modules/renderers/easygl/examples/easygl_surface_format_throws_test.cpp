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
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"

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

    /// A renderer without real volume-texture storage refuses EVERY Texture3D construction up
    /// front with System::NotSupportedException (REMED-CONTENT-004's gate), before any format
    /// validation runs -- the capability-false arms below expect exactly that refusal.
    void expectThrowsNotSupported(const char* label, auto fn)
    {
        try
        {
            fn();
            std::printf("[FAIL] %s — expected System::NotSupportedException, no exception thrown\n", label);
            ++fail_;
        }
        catch (const System::NotSupportedException&)
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
        // Texture3D legs are capability-conditional: a profile without real volume storage
        // (e.g. OPENGLES2 -- core OpenGL ES 2.0 has no 3D textures,
        // docs/opengles2-renderer.md) refuses construction before format validation can run.
        const bool hasTexture3D = dev.SupportsCapability(CNA::GraphicsCapability::Texture3D);
        if (hasTexture3D)
        {
            expectNoThrow("Texture3D Color", [&]{
                Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::Color);
            });
        }
        else
        {
            expectThrowsNotSupported("Texture3D Color (refused: no volume storage)", [&]{
                Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::Color);
            });
        }
        expectNoThrow("TextureCube Color", [&]{
            TextureCube t(dev, 2, false, SurfaceFormat::Color);
        });

        // ── sRGB / EXT formats follow renderer-specific promotion gates ──────

#if defined(CNA_RENDERER_SKIA)
        expectNoThrow("Texture2D ColorSrgbEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
        });
#else
        expectThrows("Texture2D ColorSrgbEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
        });
#endif
#if defined(CNA_RENDERER_SKIA)
        // SKIA-141 promotes Bc7EXT/Bc7SrgbEXT with a native BC7 decoder (no third-party decoder
        // dependency). Dxt5SrgbEXT below remains refused pending a task that scopes it.
        expectNoThrow("Texture2D Bc7EXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7EXT);
        });
        expectNoThrow("Texture2D Bc7SrgbEXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7SrgbEXT);
        });
#else
        expectThrows("Texture2D Bc7EXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7EXT);
        });
        expectThrows("Texture2D Bc7SrgbEXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Bc7SrgbEXT);
        });
#endif
        expectThrows("Texture2D Dxt5SrgbEXT", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt5SrgbEXT);
        });
#if defined(CNA_RENDERER_SKIA)
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
        if (hasTexture3D)
        {
            expectThrows("Texture3D ColorSrgbEXT", [&]{
                Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
            });
        }
        else
        {
            expectThrowsNotSupported("Texture3D ColorSrgbEXT (refused: no volume storage)", [&]{
                Texture3D t(dev, 2, 2, 2, false, SurfaceFormat::ColorSrgbEXT);
            });
        }

        // ── Other unsupported formats must throw ─────────────────────────────

#if defined(CNA_RENDERER_SKIA)
        expectNoThrow("Texture2D Bgra5551", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgra5551);
        });
        expectNoThrow("Texture2D NormalizedByte2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::NormalizedByte2);
        });
        expectNoThrow("Texture2D NormalizedByte4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::NormalizedByte4);
        });
        expectNoThrow("Texture2D Single", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Single);
        });
        expectNoThrow("Texture2D Vector2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Vector2);
        });
        expectNoThrow("Texture2D Vector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Vector4);
        });
        expectNoThrow("Texture2D HalfSingle", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfSingle);
        });
        expectNoThrow("Texture2D HalfVector2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfVector2);
        });
        expectNoThrow("Texture2D HalfVector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfVector4);
        });
        expectNoThrow("Texture2D HdrBlendable", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HdrBlendable);
        });
#else
        expectThrows("Texture2D Bgra5551", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgra5551);
        });
        expectThrows("Texture2D NormalizedByte2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::NormalizedByte2);
        });
#if defined(CNA_GL_PROFILE_OPENGLES3) || defined(CNA_GL_PROFILE_OPENGL33) || defined(CNA_GL_PROFILE_WEBGL2)
        expectNoThrow("Texture2D NormalizedByte4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::NormalizedByte4);
        });
#else
        expectThrows("Texture2D NormalizedByte4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::NormalizedByte4);
        });
#endif
        expectThrows("Texture2D Single", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Single);
        });
        expectThrows("Texture2D Vector2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Vector2);
        });
        expectThrows("Texture2D Vector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Vector4);
        });
        expectThrows("Texture2D HalfSingle", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfSingle);
        });
        expectThrows("Texture2D HalfVector2", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfVector2);
        });
        expectThrows("Texture2D HalfVector4", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HalfVector4);
        });
        expectThrows("Texture2D HdrBlendable", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::HdrBlendable);
        });
#endif
#if defined(CNA_RENDERER_SKIA)
        // SKIA-140 promotes Dxt1/Dxt3/Dxt5 with preserved compressed CPU blocks; Dxt5SrgbEXT
        // above remains refused pending a task that scopes it.
        expectNoThrow("Texture2D Dxt1", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt1);
        });
        expectNoThrow("Texture2D Dxt3", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt3);
        });
        expectNoThrow("Texture2D Dxt5", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt5);
        });
#else
        expectThrows("Texture2D Dxt1", [&]{
            Texture2D t(dev, 4, 4, false, SurfaceFormat::Dxt1);
        });
#endif
#if defined(CNA_RENDERER_SKIA)
        // SKIA-135–139 promote these exact transfer/sampling formats for Texture2D only.
        // The SKIA-136 colour, SKIA-138 float and SKIA-139 shadow formats were handled above;
        // render targets remain independently gated and other renderers retain Color-only.
        expectNoThrow("Texture2D Bgr565", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565);
        });
        expectNoThrow("Texture2D Bgra4444", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgra4444);
        });
        expectNoThrow("Texture2D Rgba1010102", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rgba1010102);
        });
        expectNoThrow("Texture2D Rg32", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rg32);
        });
        expectNoThrow("Texture2D Rgba64", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rgba64);
        });
        expectNoThrow("Texture2D Alpha8", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Alpha8);
        });
        expectNoThrow("Texture2D ByteEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ByteEXT);
        });
        expectNoThrow("Texture2D UShortEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::UShortEXT);
        });
#else
        expectThrows("Texture2D Bgr565", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Bgr565);
        });
        expectThrows("Texture2D Alpha8", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Alpha8);
        });
        expectThrows("Texture2D Rg32", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rg32);
        });
        expectThrows("Texture2D Rgba64", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::Rgba64);
        });
        expectThrows("Texture2D ByteEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::ByteEXT);
        });
        expectThrows("Texture2D UShortEXT", [&]{
            Texture2D t(dev, 2, 2, false, SurfaceFormat::UShortEXT);
        });
#endif
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
