// SPDX-License-Identifier: MS-PL
// Task 676: SDL_Renderer SpriteBatch::Begin(effect) custom-Effect behavior decision.
//
// Decision (per plans/plan_graphics.md Task 676): SDL_Renderer has no programmable shader stage at
// all, so a custom Effect passed to SpriteBatch::Begin can never actually be applied. Silently
// ignoring it (drawing with the built-in sprite blit instead) would misrender any game that
// depends on the custom shader's visual output, with no error at all -- unacceptable. Mirrors
// this renderer's own already-established ThrowNo3D convention (a genuinely-unsupported category
// of feature throws a clear std::runtime_error rather than silently no-op'ing or misrendering).
//
// SdlSpriteBatchRenderer::SetCustomEffect() now throws whenever a non-null Effect is supplied; a
// null Effect (the default "no custom effect" case used by every other SpriteBatch call in this
// project) remains a no-op, since SpriteBatch::Begin() calls SetCustomEffect() unconditionally on
// every Begin().
//
// Test: constructs a ShaderEffect (Effect itself is abstract; ShaderEffect is the minimal
// concrete subclass -- its constructor safely no-ops when the renderer's CreateEffectRenderer()
// returns nullptr, which SDL_Renderer's does by default, so no actual shader compilation is
// attempted) and confirms passing it to Begin() throws std::runtime_error, while omitting it
// (nullptr, i.e. every other Begin() overload already exercised by every other SDL_Renderer test
// in this project) does not.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlCustomEffectThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));

        // A null Effect (the overwhelmingly common case, used by every other SDL_Renderer
        // SpriteBatch test in this project) must NOT throw.
        bool threwOnNull = false;
        try
        {
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                       nullptr, nullptr, nullptr, nullptr);
            sb_->End();
        }
        catch (const std::exception&)
        {
            threwOnNull = true;
        }
        check(!threwOnNull, "Begin() with a null Effect does not throw");

        // A genuinely non-null custom Effect must throw -- there is no programmable shader
        // stage on this renderer to apply it with.
        ShaderEffect fx(dev, "", "");
        bool threwOnCustom = false;
        try
        {
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                       nullptr, nullptr, nullptr, &fx);
        }
        catch (const std::exception&)
        {
            threwOnCustom = true;
        }
        check(threwOnCustom, "Begin() with a non-null custom Effect throws");

        Exit();
    }

public:
    SdlCustomEffectThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlCustomEffectThrowsTest game;
    game.Run();
    return game.getResult();
}
