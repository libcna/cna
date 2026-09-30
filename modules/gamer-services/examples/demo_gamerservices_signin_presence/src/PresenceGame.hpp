#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
// pattern), Gamer::SignedInGamers, the SignedInGamer::SignedIn/SignedOut static events, and
// GamerPresence (PresenceMode/PresenceValue). Number keys 1/2/3/4 cycle that signed-in gamer's
// GamerPresenceMode forward; the HUD shows each gamer's live PresenceMode name and PresenceValue.
//
// Nobody is signed in at startup unless profiles are configured to sign in automatically
// (CNA_GAMER_SERVICES_AUTO_SIGN_IN=<name>[,<name>...], up to four; --smoke signs in two when it is
// unset). Those appear, raising SignedIn, at the first GamerServicesDispatcher.Update; the Guide
// (Home key) signs gamers in and out while the demo runs.
//
// Setting PresenceMode also sets the gamer's presence text (the mode's display string, which
// SetPresenceModeStringEXT can replace). A gamer signed in to a CNA account service publishes that
// text at the next GamerServicesDispatcher.Update, and friends read it as FriendGamer.Presence; a
// local offline profile has no service to publish to. GamerPresence has no public getter for the
// text, in XNA as here, so the HUD shows the PresenceMode enum name and PresenceValue.
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
    // The Guide draws its panes and notifications through the game's graphics device service.
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};

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
