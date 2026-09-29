// SPDX-License-Identifier: MS-PL
// cna_guide_review OUTDIR [--sequence]
//
// Deterministic captures of CNA's system UI for review: a fixed scene stands in for a game, a fake
// CNA service supplies accounts, friends, presence, messages, achievements and an invitation, and
// each Guide surface is opened through the standard XNA Guide API (or, for the system Guide and
// notifications, as the system opens them) and captured with the game still behind it, into
// OUTDIR/<nn-name>.png. With --sequence it is instead one player's session in order, driven as a
// player drives it and never reset between captures: signing in from the Guide, editing and saving
// the avatar, a friend's card, a party invitation, a game invitation received and accepted. Run it
// on a private display (tools/platform/run_gpu_tests_private.sh --exec), never on the desktop.

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "../../src/Internal/GuideOverlay.hpp"
#include "../../src/Internal/Guide/GuideUi.hpp"
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
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <algorithm>
#include <chrono>
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
        // Something done a few frames in, once what was opened has loaded (a choice in it).
        std::function<void()> then = {};
    };

    // An achievement picture for the review: a gold medal on a ribbon, drawn here.
    std::vector<unsigned char> Picture()
    {
        constexpr int Size = 128;
        std::vector<std::uint8_t> rgba(Size * Size * 4, 0);
        for (int y = 0; y < Size; ++y)
            for (int x = 0; x < Size; ++x)
            {
                auto* p = &rgba[static_cast<std::size_t>(y * Size + x) * 4];
                const float dx = x + 0.5f - 64.0f, dy = y + 0.5f - 76.0f, d = std::sqrt(dx * dx + dy * dy);
                const bool ribbon = y < 50 && std::abs(x + 0.5f - 64.0f) < 22.0f - y * 0.2f;
                if (d < 42.0f)
                {
                    const float light = 1.0f - (dx + dy) / 120.0f;
                    const bool rim = d > 34.0f;
                    p[0] = static_cast<std::uint8_t>(std::min(255.0f, (rim ? 200.0f : 236.0f) * light));
                    p[1] = static_cast<std::uint8_t>(std::min(255.0f, (rim ? 140.0f : 176.0f) * light));
                    p[2] = static_cast<std::uint8_t>(std::min(255.0f, (rim ? 40.0f : 64.0f) * light));
                    p[3] = 255;
                }
                else if (ribbon)
                {
                    p[0] = 64;
                    p[1] = 112;
                    p[2] = 200;
                    p[3] = 255;
                }
            }
        const auto png = CNA::Internal::Graphics::ImageLoader::EncodePng(rgba.data(), Size, Size, Size, Size);
        return {png.begin(), png.end()};
    }
    std::string PictureHash()
    {
        const auto picture = Picture();
        return Avatars::sha256Hex(std::span<const std::uint8_t>(picture.data(), picture.size()));
    }

    // What a player does in the Guide, through its semantic input.
    namespace Ui = CNA::Internal::GamerServices::GuideUi;
    void Type(const std::string& text)
    {
        for (const char c : text)
            Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(static_cast<Microsoft::Xna::Framework::Input::charcs>(c));
    }
    void Send(Ui::Command command, int times = 1)
    {
        for (int i = 0; i < times; ++i)
            Ui::sendForTesting(command);
    }
    // Chooses the row whose label starts with text; offset for screens whose first label is a title.
    void Choose(const std::string& text, int offset = 0)
    {
        const auto labels = Ui::labelsForTesting();
        const auto found = std::find_if(labels.begin(), labels.end(), [&](const std::string& label) { return label.starts_with(text); });
        if (found == labels.end())
        {
            std::printf("no \"%s\" on %s\n", text.c_str(), Ui::currentScreenForTesting().c_str());
            return;
        }
        Ui::clickForTesting(static_cast<int>(found - labels.begin()) - offset);
    }

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
        std::chrono::steady_clock::duration clockOffset_{};
        bool sequence_ = false;

        SignedInGamer* Player(int index) { return (*Gamer::getSignedInGamersProperty())[static_cast<PlayerIndex>(index)]; }

        void Close()
        {
            Guide::ResetPendingMessageBoxForTestingEXT();
            Guide::ResetPendingKeyboardInputForTestingEXT();
            Service::GuideUi::closeAll();
            // Each capture starts with no notification showing.
            clockOffset_ += std::chrono::seconds(30);
            (void)Service::guideNotifications();
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
            achievements[0].picture = PictureHash();
            Service::ServiceLeaderboardFixture best;
            best.key = "BestScore";
            Service::ServiceLeaderboardFixture laps;
            laps.key = "FastestLap";
            laps.mode = 1;
            laps.ascending = true;
            for (const auto& person : people)
            {
                best.entries.push_back({person.userId, person.gamertag, 900 + static_cast<long long>(person.gamerScore) * 7, 0, {}});
                laps.entries.push_back({person.userId, person.gamertag, 58000 + static_cast<long long>(person.gamerScore % 997) * 13, 0, {}});
            }
            service_ = Service::makeFakeBackend(people, achievements, {best, laps});
            const auto picture = Picture();
            // The service's newest avatar catalog is this build's, so the avatar editor opens on it.
            std::string catalog;
            const auto newest = "v" + std::to_string(Avatars::embeddedCatalogs().back()->version) + "/catalog.json";
            for (const auto& file : Avatars::embeddedCatalogFiles())
                if (std::string_view(file.name) == newest)
                    catalog.assign(reinterpret_cast<const char*>(file.data), file.size);
            Service::setFakeAvatarCatalog(*service_, catalog, {{PictureHash(), picture}});
            Service::setBackendForTesting(service_);
        }

        // Alice has just signed in: give her friends, presence, mail and the met players.
        void Populate()
        {
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

        void BobInvitesAlice()
        {
            Service::ServiceSessionSettings settings;
            settings.maxGamers = 8;
            const auto session = service_->sessionDirectory().create("b2", {"b2"}, Service::ServiceSessionKind::PlayerMatch, settings);
            (void)service_->sessionDirectory().sendInvite("b2", session.session, "Alice");
            Service::pollInvitationsNowForTesting();
        }

        std::vector<Step> Sequence()
        {
            return {
                {"s01-home-signed-out", [] { Service::openSystemGuide(PlayerIndex::One); }, 30},
                {"s02-sign-in", [] { Choose("Sign in"); }, 30},
                {"s03-account-name", [] {
                     Choose("Sign in with a CNA account");
                     Type("Alice");
                 }, 30},
                {"s04-password", [] {
                     Type("\r");
                     Type("fixture");
                 }, 30},
                {"s05-signed-in", [] { Type("\r"); }, 40},
                {"s06-home", [this] {
                     Populate();
                     Service::openSystemGuide(PlayerIndex::One);
                 }, 60},
                {"s07-avatar-editor", [] { Choose("Edit avatar"); }, 90},
                {"s08-hair-trying", [] {
                     Send(Ui::Command::Down, 5);
                     Send(Ui::Command::Accept);
                     Send(Ui::Command::Right);
                 }, 60},
                {"s09-hair-kept", [] { Send(Ui::Command::Accept); }, 40},
                {"s10-save-question", [] { Send(Ui::Command::Back, 2); }, 30},
                {"s11-saved", [] { Choose("Save"); }, 40},
                {"s12-friends", [] {
                     Ui::closeAll();
                     Guide::ShowFriends(PlayerIndex::One);
                 }, 40},
                {"s13-gamer-card", [] { Choose("Bob"); }, 60},
                {"s14-party-invitation-sent", [] { Choose("Invite to party", 1); }, 40},
                {"s15-invitation-received", [this] {
                     Ui::closeAll();
                     BobInvitesAlice();
                 }, 60},
                {"s16-invitation-accepted", [] { Ui::clickForTesting(0); }, 40},
            };
        }

    public:
        GuideReviewGame(std::string out, bool sequence) : out_(std::move(out)), sequence_(sequence)
        {
            graphics_.setPreferredBackBufferWidthProperty(Width);
            graphics_.setPreferredBackBufferHeightProperty(Height);
            graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
            Seed();
            Service::setGuideNotificationClockForTesting([this] { return std::chrono::steady_clock::now() + clockOffset_; });
            getComponentsProperty().Add(new GamerServicesComponent(*this));
            auto signIn = [this](const char* tag, int slot) { service_->signIn(slot, tag, "fixture"); };
            steps_ = {
                {"01-sign-in", [] { Guide::ShowSignIn(1, false); }},
                {"02-signed-in-toast", [signIn] { signIn("Alice", 0); }, 30},
                {"03-system-guide", [this] {
                     Populate();
                     Service::openSystemGuide(PlayerIndex::One);
                 }},
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
                {"12-invitation-received", [this] { BobInvitesAlice(); }, 40},
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
                {"20-achievement-detail", [] { Guide::ShowAchievementsEXT(PlayerIndex::One); }, 40, [] { Service::GuideUi::clickForTesting(0); }},
                {"21-leaderboards", [] { Service::GuideUi::open(Service::GuideUi::leaderboardsScreen(PlayerIndex::One), PlayerIndex::One); }, 30},
                {"22-leaderboard-top", [] { Service::GuideUi::open(Service::GuideUi::leaderboardsScreen(PlayerIndex::One), PlayerIndex::One); }, 60,
                 [] { Service::GuideUi::clickForTesting(0); }},
                {"23-leaderboard-around-you", [] { Service::GuideUi::open(Service::GuideUi::leaderboardsScreen(PlayerIndex::One), PlayerIndex::One); }, 60,
                 [] { Service::GuideUi::clickForTesting(0); Service::GuideUi::sendForTesting(Service::GuideUi::Command::X); }},
                {"24-home-edit-avatar", [] { Service::openSystemGuide(PlayerIndex::One); }, 120, [] {
                     const auto labels = Service::GuideUi::labelsForTesting();
                     const auto edit = std::find(labels.begin(), labels.end(), "Edit avatar");
                     Service::GuideUi::clickForTesting(static_cast<int>(edit - labels.begin()));
                 }},
            };
            if (sequence_)
                steps_ = Sequence();
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
            if (step_ < steps_.size() && opened_ && frame_ == 15 && steps_[step_].then)
                steps_[step_].then();
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
                if (!sequence_)
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
    const bool sequence = argc == 3 && std::string(argv[2]) == "--sequence";
    if (argc != 2 && !sequence)
    {
        std::fprintf(stderr, "usage: cna_guide_review OUTDIR [--sequence]\n");
        return 2;
    }
    std::filesystem::create_directories(argv[1]);
    GuideReviewGame game(argv[1], sequence);
    game.Run();
    return 0;
}
