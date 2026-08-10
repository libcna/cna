#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.8: cna_demo_gamerservices_signin_presence. Real GamerServicesComponent registration
// (Components.Add(new GamerServicesComponent(this)) in the constructor - the actual idiomatic XNA
// pattern, unlike every earlier Phase 15 Net demo, which called GamerServicesDispatcher::Initialize
// directly), the resulting population of Gamer::SignedInGamers (4 stub gamers), SignedInGamer::
// SignedIn/SignedOut static events, and GamerPresence (PresenceMode/PresenceValue/
// SetPresenceModeStringEXT - the last being a real, confirmed no-op EXT point on this platform,
// matching FNA's own reference). Number keys 1/2/3/4 cycle that signed-in gamer's
// GamerPresenceMode forward; the HUD shows each gamer's live PresenceMode name and PresenceValue.
//
// Honest note on scope: neither FNA's real GamerPresence nor CNA's port expose a public getter
// for the internal formatted presence string PresenceMode's setter computes (it's only ever
// passed one-way into the no-op SetPresenceModeStringEXT) - so this demo's HUD shows the
// PresenceMode's own enum name and PresenceValue directly rather than reconstructing a private
// implementation detail that was never part of the public contract in either FNA or CNA.
class PresenceGame : public Microsoft::Xna::Framework::Game
{
public:
    PresenceGame();
    ~PresenceGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    Microsoft::Xna::Framework::GamerServices::GamerServicesComponent* gamerServicesComponent_ = nullptr;
    std::vector<Microsoft::Xna::Framework::GamerServices::SignedInGamer*> gamers_;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    int signInFireCount_ = 0;
    int signOutFireCount_ = 0;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
};
