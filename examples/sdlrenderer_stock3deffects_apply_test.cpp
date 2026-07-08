// SPDX-License-Identifier: MS-PL
// Task 726: Verify all 5 stock 3D effects' Apply()+property setters do NOT throw on
// SDL_Renderer, but drawing with them DOES -- matches FNA: effects are backend-agnostic data
// objects until actually used to draw. Covers BasicEffect, AlphaTestEffect, DualTextureEffect,
// EnvironmentMapEffect, SkinnedEffect (SpriteEffect is the 2D sprite effect, not one of the 5
// stock 3D effects, and is out of scope here).
//
// Each effect's OnApply() only ever calls Texture2D::GetBackend() for an assigned texture (guarded
// by "if texture is set"), never GraphicsDevice::GetBackend() directly -- and Texture2D itself
// works fine on SDL_Renderer (only Texture3D/TextureCube/VertexBuffer/IndexBuffer are 3D-
// unsupported, Tasks 720-725). So property setters + Apply() never touch anything that could
// throw on this backend, REGARDLESS of whether a Texture2D is assigned.
//
// Deliberately does NOT assign EnvironmentMapEffect::EnvironmentMap (a TextureCube*) -- that
// type's own construction behavior on SDL_Renderer is a separate, currently-BLOCKED architecture
// decision (Task 725); this task only needs to confirm setEnvironmentMapProperty(TextureCube*)
// itself (a plain pointer store, not a construction) doesn't throw, which doesn't require ever
// constructing one.
//
// "Drawing with them does throw" is confirmed once per effect via DrawUserPrimitives (Task 721's
// established SDL_Renderer 3D-unsupported throw path), immediately after each Apply() call.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlStock3DEffectsApplyTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template <typename F>
    static bool DoesNotThrow(F&& fn)
    {
        try { fn(); return true; }
        catch (const std::exception&) { return false; }
    }

    template <typename F>
    static bool Throws(F&& fn)
    {
        try { fn(); return false; }
        catch (const std::exception&) { return true; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        static const VertexPositionColor vpc[3]{
            {Vector3::Zero, Color::White}, {Vector3::Zero, Color::White}, {Vector3::Zero, Color::White}
        };

        auto tryDraw = [&](Effect&) {
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1);
        };

        // --- BasicEffect ---
        {
            bool ctorAndSettersOk = DoesNotThrow([&] {
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setDiffuseColorProperty(Vector3::One);
                fx.setAlphaProperty(0.5f);
                fx.setLightingEnabledProperty(false);
                fx.setPreferPerPixelLightingProperty(true);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&tex);
                fx.Apply();
            });
            check(ctorAndSettersOk, "BasicEffect: construction, property setters, and Apply() do not throw on SDL_Renderer");

            BasicEffect fx2(dev);
            fx2.Apply();
            check(Throws([&] { tryDraw(fx2); }), "Drawing after BasicEffect::Apply() still throws on SDL_Renderer");
        }

        // --- AlphaTestEffect ---
        {
            bool ctorAndSettersOk = DoesNotThrow([&] {
                AlphaTestEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setDiffuseColorProperty(Vector3::One);
                fx.setAlphaProperty(1.0f);
                fx.setVertexColorEnabledProperty(true);
                fx.setTextureProperty(&tex);
                fx.setAlphaFunctionProperty(CompareFunction::Greater);
                fx.setReferenceAlphaProperty(128);
                fx.Apply();
            });
            check(ctorAndSettersOk, "AlphaTestEffect: construction, property setters, and Apply() do not throw on SDL_Renderer");

            AlphaTestEffect fx2(dev);
            fx2.Apply();
            check(Throws([&] { tryDraw(fx2); }), "Drawing after AlphaTestEffect::Apply() still throws on SDL_Renderer");
        }

        // --- DualTextureEffect ---
        {
            bool ctorAndSettersOk = DoesNotThrow([&] {
                DualTextureEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setDiffuseColorProperty(Vector3::One);
                fx.setTextureProperty(&tex);
                fx.setTexture2Property(&tex);
                fx.setVertexColorEnabledProperty(true);
                fx.Apply();
            });
            check(ctorAndSettersOk, "DualTextureEffect: construction, property setters, and Apply() do not throw on SDL_Renderer");

            DualTextureEffect fx2(dev);
            fx2.Apply();
            check(Throws([&] { tryDraw(fx2); }), "Drawing after DualTextureEffect::Apply() still throws on SDL_Renderer");
        }

        // --- EnvironmentMapEffect (EnvironmentMap TextureCube* deliberately left unset -- Task 725) ---
        {
            bool ctorAndSettersOk = DoesNotThrow([&] {
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setDiffuseColorProperty(Vector3::One);
                fx.setTextureProperty(&tex);
                fx.setEnvironmentMapProperty(nullptr);
                fx.setEnvironmentMapAmountProperty(0.5f);
                fx.setFresnelFactorProperty(1.0f);
                fx.Apply();
            });
            check(ctorAndSettersOk, "EnvironmentMapEffect: construction, property setters, and Apply() do not throw on SDL_Renderer");

            EnvironmentMapEffect fx2(dev);
            fx2.Apply();
            check(Throws([&] { tryDraw(fx2); }), "Drawing after EnvironmentMapEffect::Apply() still throws on SDL_Renderer");
        }

        // --- SkinnedEffect ---
        {
            bool ctorAndSettersOk = DoesNotThrow([&] {
                SkinnedEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setDiffuseColorProperty(Vector3::One);
                fx.setTextureProperty(&tex);
                fx.setWeightsPerVertexProperty(4);
                fx.Apply();
            });
            check(ctorAndSettersOk, "SkinnedEffect: construction, property setters, and Apply() do not throw on SDL_Renderer");

            SkinnedEffect fx2(dev);
            fx2.Apply();
            check(Throws([&] { tryDraw(fx2); }), "Drawing after SkinnedEffect::Apply() still throws on SDL_Renderer");
        }

        // --- The device must remain fully usable throughout. ---
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional throughout");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlStock3DEffectsApplyTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    // Unbuffered so a crash mid-test doesn't lose whichever PASS/FAIL lines already printed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SdlStock3DEffectsApplyTest game;
    game.Run();
    return game.getResult();
}
