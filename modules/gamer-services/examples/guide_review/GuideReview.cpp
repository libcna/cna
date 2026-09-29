// SPDX-License-Identifier: MS-PL
// cna_guide_review OUTDIR
//
// Deterministic captures of CNA's system UI for review: a fixed scene stands in for a game, a fake
// CNA service supplies accounts, friends, presence, messages, achievements and an invitation, and
// each Guide surface is opened through the standard XNA Guide API (or, for the system Guide and
// notifications, as the system opens them) and captured with the game still behind it, into
// OUTDIR/<nn-name>.png. Run it on a private display (tools/platform/run_gpu_tests_private.sh
// --exec), never on the desktop.

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "../../src/Internal/GuideOverlay.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
namespace Service = CNA::Internal::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;

namespace
{
    constexpr int Width = 1280, Height = 720;

    std::vector<unsigned char> RandomAvatar(unsigned seed, std::uint8_t body)
    {
        std::mt19937 random(seed);
        const auto bytes = Avatars::encode(Avatars::randomDescriptor(Avatars::newestEmbeddedManifest(), body, random));
        return {bytes.begin(), bytes.end()};
    }

    struct Step
    {
        std::string name;
        std::function<void()> open;
        int frames = 20;
    };

    class GuideReviewGame : public Game
    {
        GraphicsDeviceManager graphics_{this};
        std::string out_;
        std::shared_ptr<Service::IGamerServicesBackend> service_;
        std::vector<Step> steps_;
        std::size_t step_ = 0;
        int frame_ = 0;
        bool opened_ = false;
        std::unique_ptr<SpriteBatch> batch_;
        std::unique_ptr<Texture2D> white_;

        SignedInGamer* Player(int index) { return (*Gamer::getSignedInGamersProperty())[static_cast<PlayerIndex>(index)]; }

        void Close()
        {
            Guide::ResetPendingMessageBoxForTestingEXT();
            Guide::ResetPendingKeyboardInputForTestingEXT();
        }

        void Seed()
        {
            std::vector<Service::ServiceIdentity> people;
            const std::vector<std::tuple<const char*, const char*, const char*, std::uint8_t>> accounts{
                {"a1", "Alice", "Always one more level.", 0}, {"b2", "Bob", "Rematch?", 1}, {"c3", "Carol", "Speedrunner in training", 0},
                {"d4", "Dave", "", 1}, {"e5", "Erin", "Collector of trophies", 0}, {"f6", "Frank", "", 1}};
            unsigned seed = 11;
            for (const auto& [id, tag, motto, body] : accounts)
            {
                Service::ServiceIdentity person;
                person.userId = id;
                person.gamertag = tag;
                person.displayName = tag;
                person.motto = motto;
                person.gamerScore = static_cast<int>(seed * 97 % 4000);
                person.totalAchievements = static_cast<int>(seed % 40);
                person.titlesPlayed = static_cast<int>(seed % 9 + 1);
                person.allowOnlineSessions = true;
                person.gamerZone = static_cast<int>(seed % 5);
                person.avatar = RandomAvatar(seed++, body);
                people.push_back(person);
            }
            people[1].reputation = 4.25f;
            std::vector<Service::ServiceAchievement> achievements;
            for (const auto& [key, name, score] : std::vector<std::tuple<const char*, const char*, int>>{
                     {"first", "First Steps", 10}, {"combo", "Chain Reaction", 25}, {"secret", "Hidden Path", 50}, {"win10", "Ten Victories", 30}})
            {
                Service::ServiceAchievement achievement;
                achievement.key = key;
                achievement.name = name;
                achievement.description = std::string("Earn ") + name + ".";
                achievement.howToEarn = "Play the game.";
                achievement.score = score;
                achievements.push_back(achievement);
            }
            service_ = Service::makeFakeBackend(people, achievements);
            Service::setBackendForTesting(service_);
        }

    public:
        explicit GuideReviewGame(std::string out) : out_(std::move(out))
        {
            graphics_.setPreferredBackBufferWidthProperty(Width);
            graphics_.setPreferredBackBufferHeightProperty(Height);
            graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
            Seed();
            getComponentsProperty().Add(new GamerServicesComponent(*this));
            auto signIn = [this](const char* tag, int slot) { service_->signIn(slot, tag, "fixture"); };
            steps_ = {
                {"01-sign-in", [] { Guide::ShowSignIn(1, false); }},
                {"02-signed-in-toast", [signIn] { signIn("Alice", 0); }, 30},
                {"03-system-guide", [] { Service::openSystemGuide(PlayerIndex::One); }},
                {"04-friends", [] { Guide::ShowFriends(PlayerIndex::One); }},
                {"05-gamer-card", [this] {
                     std::unique_ptr<Gamer> bob(Gamer::GetFromGamertag("Bob"));
                     Guide::ShowGamerCard(PlayerIndex::One, bob.get());
                 }},
                {"06-messages", [] { Guide::ShowMessages(PlayerIndex::One); }},
                {"07-compose", [this] { Guide::ShowComposeMessage(PlayerIndex::One, "good game", {}); }},
                {"08-achievements", [] { Guide::ShowAchievementsEXT(PlayerIndex::One); }},
                {"09-players", [] { Guide::ShowPlayers(PlayerIndex::One); }},
                {"10-player-review", [] {
                     std::unique_ptr<Gamer> dave(Gamer::GetFromGamertag("Dave"));
                     Guide::ShowPlayerReview(PlayerIndex::One, dave.get());
                 }},
                {"11-game-invite", [] { Guide::ShowGameInvite(PlayerIndex::One, std::vector<Gamer*>{}); }},
                {"12-invitation-received", [this] {
                     Service::ServiceSessionSettings settings;
                     settings.maxGamers = 8;
                     const auto session = service_->sessionDirectory().create("b2", {"b2"}, Service::ServiceSessionKind::PlayerMatch, settings);
                     (void)service_->sessionDirectory().sendInvite("b2", session.session, "Alice");
                     Service::pollInvitationsNowForTesting();
                 }, 40},
                {"13-party", [] { Guide::ShowParty(PlayerIndex::One); }},
                {"14-marketplace", [] { Guide::ShowMarketplace(PlayerIndex::One); }},
                {"15-message-box", [] {
                     (void)Guide::BeginShowMessageBox(PlayerIndex::One, "Save game", "Overwrite the saved game in slot 2?",
                                                      {"Overwrite", "Cancel"}, 0, MessageBoxIcon::Warning, {}, {});
                 }},
                {"16-keyboard", [] {
                     (void)Guide::BeginShowKeyboardInput(PlayerIndex::One, "Name your hero", "Up to 15 characters", "Hero", {}, {});
                 }},
                {"17-toast-top-left", [] {
                     Guide::setNotificationPositionProperty(NotificationPosition::TopLeft);
                     Service::postGuideNotification("Achievement unlocked: First Steps");
                 }, 30},
                {"18-toast-center", [] {
                     Guide::setNotificationPositionProperty(NotificationPosition::Center);
                     Service::postGuideNotification("Carol is now online");
                 }, 30},
                {"19-toast-bottom-right", [] {
                     Guide::setNotificationPositionProperty(NotificationPosition::BottomRight);
                     Service::postGuideNotification("Bob invited you to a game");
                 }, 30},
            };
        }

    protected:
        void LoadContent() override
        {
            batch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
            white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1, {255, 255, 255, 255}));
        }

        void Update(GameTime& gameTime) override
        {
            Game::Update(gameTime);
            if (step_ >= steps_.size())
            {
                Exit();
                return;
            }
            if (!opened_)
            {
                if (step_ == 2)
                {
                    // Alice signed in during the previous step: give her friends, presence, mail and the met players.
                    for (const char* tag : {"Bob", "Carol", "Dave", "Erin", "Frank"})
                    {
                        service_->changeFriend("a1", tag, "add");
                    }
                    Service::setFakeRemotePresence(*service_, "b2", true, "Racing on Canyon Loop", "online");
                    Service::setFakeRemotePresence(*service_, "c3", true, "In the menus", "away");
                    Service::setFakeRemotePresence(*service_, "d4", true, "", "busy");
                    for (const char* id : {"b2", "c3", "d4"})
                    {
                        service_->changeFriend(id, "Alice", "accept");
                    }
                    Service::setFakeRemotePresence(*service_, "d4", false);
                    service_->sendMessage("b2", {"Alice"}, "Nice run earlier! Rematch tonight?");
                    service_->sendMessage("c3", {"Alice"}, "Check out the new track.");
                    (void)service_->award("a1", "first");
                    (void)service_->award("a1", "combo");
                    Service::rememberRecentPlayer("Dave");
                    Service::rememberRecentPlayer("Frank");
                }
                try
                {
                    steps_[step_].open();
                }
                catch (const std::exception& refused)
                {
                    // Captured anyway: what the player sees when the API refuses (XNA exceptions).
                    std::printf("%s: %s\n", steps_[step_].name.c_str(), refused.what());
                }
                opened_ = true;
            }
        }

        void Draw(const GameTime& gameTime) override
        {
            // A stand-in game scene: a sky gradient, hills and a HUD, so the overlay is judged over a game.
            auto& device = getGraphicsDeviceProperty();
            device.Clear(Color(40, 90, 150));
            batch_->Begin();
            for (int band = 0; band < 18; ++band)
            {
                const int y = band * Height / 18;
                batch_->Draw(*white_, Rectangle(0, y, Width, Height / 18 + 1), Color(40 + band * 6, 100 + band * 4, 170 - band * 2));
            }
            for (int hill = 0; hill < 9; ++hill)
            {
                const int x = hill * 170 - 60, h = 140 + (hill * 53) % 120;
                batch_->Draw(*white_, Rectangle(x, Height - h, 260, h), Color(50 + hill * 8, 120 + hill * 6, 60));
            }
            batch_->Draw(*white_, Rectangle(24, 24, 260, 28), Color(0, 0, 0, 120));
            batch_->Draw(*white_, Rectangle(28, 28, 180, 20), Color(230, 70, 60));
            batch_->End();
            Game::Draw(gameTime);
        }

        void EndDraw() override
        {
            if (step_ < steps_.size() && opened_ && ++frame_ >= steps_[step_].frames)
            {
                std::vector<Color> pixels(static_cast<std::size_t>(Width) * Height);
                getGraphicsDeviceProperty().GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
                std::vector<std::uint8_t> rgba(pixels.size() * 4);
                for (std::size_t i = 0; i < pixels.size(); ++i)
                {
                    rgba[i * 4] = pixels[i].getRProperty();
                    rgba[i * 4 + 1] = pixels[i].getGProperty();
                    rgba[i * 4 + 2] = pixels[i].getBProperty();
                    rgba[i * 4 + 3] = 255;
                }
                CNA::Internal::Graphics::ImageLoader::SavePng(rgba.data(), Width, Height, out_ + "/" + steps_[step_].name + ".png");
                std::printf("captured %s\n", steps_[step_].name.c_str());
                Close();
                ++step_;
                frame_ = 0;
                opened_ = false;
            }
            Game::EndDraw();
        }
    };
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: cna_guide_review OUTDIR\n");
        return 2;
    }
    std::filesystem::create_directories(argv[1]);
    GuideReviewGame game(argv[1]);
    game.Run();
    return 0;
}
