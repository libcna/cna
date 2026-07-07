#include "PresenceGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::GamerServices;

namespace
{
    std::unique_ptr<SpriteFont> MakeSimpleFont(GraphicsDevice& device)
    {
        const std::vector<uint8_t> px = {255, 255, 255, 255};
        Texture2D atlas = Texture2D::CreateFromPixels(device, 1, 1, px);

        std::vector<SharpRuntime::charcs> chars;
        std::vector<Rectangle> bounds;
        std::vector<Rectangle> cropping;
        std::vector<Vector3> kerning;
        for (char c = 32; c < 127; ++c)
        {
            chars.push_back(static_cast<SharpRuntime::charcs>(c));
            bounds.push_back(Rectangle(0, 0, 1, 1));
            cropping.push_back(Rectangle(0, 0, 8, 14));
            kerning.push_back(Vector3(0.0f, 8.0f, 0.0f));
        }

        return std::make_unique<SpriteFont>(atlas, bounds, cropping, chars, 16, 1.0f, kerning,
                                             static_cast<SharpRuntime::charcs>(' '));
    }

    // Every GamerPresenceMode enum name, matching GamerPresenceMode.hpp's own declaration order.
    const char* PresenceModeName(GamerPresenceMode mode)
    {
        switch (mode)
        {
#define PRESENCE_MODE_CASE(x) case GamerPresenceMode::x: return #x;
            PRESENCE_MODE_CASE(None) PRESENCE_MODE_CASE(SinglePlayer) PRESENCE_MODE_CASE(Multiplayer)
            PRESENCE_MODE_CASE(LocalCoOp) PRESENCE_MODE_CASE(LocalVersus) PRESENCE_MODE_CASE(OnlineCoOp)
            PRESENCE_MODE_CASE(OnlineVersus) PRESENCE_MODE_CASE(VersusComputer) PRESENCE_MODE_CASE(Stage)
            PRESENCE_MODE_CASE(Level) PRESENCE_MODE_CASE(CoOpStage) PRESENCE_MODE_CASE(CoOpLevel)
            PRESENCE_MODE_CASE(ArcadeMode) PRESENCE_MODE_CASE(CampaignMode) PRESENCE_MODE_CASE(ChallengeMode)
            PRESENCE_MODE_CASE(ExplorationMode) PRESENCE_MODE_CASE(PracticeMode) PRESENCE_MODE_CASE(PuzzleMode)
            PRESENCE_MODE_CASE(ScenarioMode) PRESENCE_MODE_CASE(StoryMode) PRESENCE_MODE_CASE(SurvivalMode)
            PRESENCE_MODE_CASE(TutorialMode) PRESENCE_MODE_CASE(DifficultyEasy) PRESENCE_MODE_CASE(DifficultyMedium)
            PRESENCE_MODE_CASE(DifficultyHard) PRESENCE_MODE_CASE(DifficultyExtreme) PRESENCE_MODE_CASE(Score)
            PRESENCE_MODE_CASE(VersusScore) PRESENCE_MODE_CASE(Winning) PRESENCE_MODE_CASE(Losing)
            PRESENCE_MODE_CASE(ScoreIsTied) PRESENCE_MODE_CASE(Outnumbered) PRESENCE_MODE_CASE(OnARoll)
            PRESENCE_MODE_CASE(InCombat) PRESENCE_MODE_CASE(BattlingBoss) PRESENCE_MODE_CASE(TimeAttack)
            PRESENCE_MODE_CASE(TryingForRecord) PRESENCE_MODE_CASE(FreePlay) PRESENCE_MODE_CASE(WastingTime)
            PRESENCE_MODE_CASE(StuckOnAHardBit) PRESENCE_MODE_CASE(NearlyFinished) PRESENCE_MODE_CASE(LookingForGames)
            PRESENCE_MODE_CASE(WaitingForPlayers) PRESENCE_MODE_CASE(WaitingInLobby) PRESENCE_MODE_CASE(SettingUpMatch)
            PRESENCE_MODE_CASE(PlayingWithFriends) PRESENCE_MODE_CASE(AtMenu) PRESENCE_MODE_CASE(StartingGame)
            PRESENCE_MODE_CASE(Paused) PRESENCE_MODE_CASE(GameOver) PRESENCE_MODE_CASE(WonTheGame)
            PRESENCE_MODE_CASE(ConfiguringSettings) PRESENCE_MODE_CASE(CustomizingPlayer) PRESENCE_MODE_CASE(EditingLevel)
            PRESENCE_MODE_CASE(InGameStore) PRESENCE_MODE_CASE(WatchingCutscene) PRESENCE_MODE_CASE(WatchingCredits)
            PRESENCE_MODE_CASE(PlayingMinigame) PRESENCE_MODE_CASE(FoundSecret) PRESENCE_MODE_CASE(CornflowerBlue)
#undef PRESENCE_MODE_CASE
        }
        return "?";
    }

    constexpr int kPresenceModeCount = 60;
}

PresenceGame::PresenceGame()
{
    // Real idiomatic XNA registration - Components.Add(new GamerServicesComponent(this)) in the
    // constructor, matching cna-samples' own documented real-world usage. Game::Initialize()'s own
    // per-component loop calls GamerServicesComponent::Initialize() (which populates
    // Gamer::SignedInGamers) automatically; no direct GamerServicesDispatcher::Initialize() call
    // needed, unlike every earlier Phase 15 Net demo.
    gamerServicesComponent_ = new GamerServicesComponent(*this);
    getComponentsProperty().Add(gamerServicesComponent_);

    // Subscribed before Initialize() runs so every SignedIn firing during startup is observed
    // (matches Task 9.8's own proven harness pattern).
    SignedInGamer::SignedIn += [this](System::Object*, const SignedInEventArgs& e) {
        ++signInFireCount_;
        std::printf("[Presence] SignedIn: %s\n", e.getGamerProperty()->getGamertagProperty().c_str());
    };
    SignedInGamer::SignedOut += [this](System::Object*, const SignedOutEventArgs& e) {
        ++signOutFireCount_;
        std::printf("[Presence] SignedOut: %s\n", e.getGamerProperty()->getGamertagProperty().c_str());
    };
}

PresenceGame::~PresenceGame()
{
    delete gamerServicesComponent_;
}

void PresenceGame::Initialize()
{
    Game::Initialize();

    const auto* signedIn = Gamer::getSignedInGamersProperty();
    for (int i = 0; i < signedIn->getCountProperty(); ++i)
    {
        gamers_.push_back((*signedIn)[i]);
    }
    std::printf("[Presence] %zu signed-in gamers after GamerServicesComponent::Initialize() "
                "(signInFireCount=%d)\n", gamers_.size(), signInFireCount_);
}

void PresenceGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void PresenceGame::Update(GameTime& /*gameTime*/)
{
    KeyboardState keys = Keyboard::GetState();
    const Keys numberKeys[4] = {Keys::D1, Keys::D2, Keys::D3, Keys::D4};
    for (int i = 0; i < 4 && i < static_cast<int>(gamers_.size()); ++i)
    {
        if (keys.IsKeyDown(numberKeys[i]) && !previousKeys_.IsKeyDown(numberKeys[i]))
        {
            GamerPresence& presence = gamers_[static_cast<std::size_t>(i)]->getPresenceProperty();
            const int next = (static_cast<int>(presence.getPresenceModeProperty()) + 1) % kPresenceModeCount;
            presence.setPresenceModeProperty(static_cast<GamerPresenceMode>(next));
            std::printf("[Presence] Gamer %d PresenceMode -> %s\n", i + 1,
                        PresenceModeName(static_cast<GamerPresenceMode>(next)));
        }
    }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically cycle gamer 0's presence
    // every 30 frames (matching the established Phase 15 deterministic-nudge convention).
    if (smokeFramesLeft_ >= 0 && !gamers_.empty() && smokeFramesLeft_ % 30 == 0)
    {
        GamerPresence& presence = gamers_[0]->getPresenceProperty();
        const int next = (static_cast<int>(presence.getPresenceModeProperty()) + 1) % kPresenceModeCount;
        presence.setPresenceModeProperty(static_cast<GamerPresenceMode>(next));
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Presence] Smoke test complete: signInFireCount=%d signOutFireCount=%d "
                        "gamer0PresenceMode=%s\n",
                        signInFireCount_, signOutFireCount_,
                        gamers_.empty() ? "?" : PresenceModeName(gamers_[0]->getPresenceProperty().getPresenceModeProperty()));
            Exit();
        }
    }
}

void PresenceGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();
    spriteBatch_->DrawString(*font_, "Signed-In Gamer Presence (1/2/3/4 to cycle)",
                              Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));

    float y = 48.0f;
    for (std::size_t i = 0; i < gamers_.size(); ++i)
    {
        const GamerPresence& presence = gamers_[i]->getPresenceProperty();
        char line[256];
        std::snprintf(line, sizeof(line), "P%zu %-12s PresenceMode=%-20s PresenceValue=%d",
                      i + 1, gamers_[i]->getGamertagProperty().c_str(),
                      PresenceModeName(presence.getPresenceModeProperty()), presence.getPresenceValueProperty());
        spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255));
        y += 20.0f;
    }

    spriteBatch_->End();
}
