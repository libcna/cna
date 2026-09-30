// SPDX-License-Identifier: MS-PL
// cna_avatar_editor [--capture DIR]
//
// The CNA avatar editor as a program of its own, as the console's avatar editor was, so no game
// needs customization code. The player signs in through the standard Guide sign-in (a CNA service
// account or an offline local profile) and edits in the Guide's own avatar editor -- the same
// screen the Guide's Home offers inside any game -- which saves to the service (avatars.set) or to
// the local profile store. Running games see the change through AvatarDescription.Changed.
//
// --capture DIR walks the categories, tries on and keeps choices, randomizes and saves, writing a
// PNG of each step into DIR and exiting with the result; run it on a private display with
// CNA_GAMER_SERVICES_AUTO_SIGN_IN naming a local profile (and CNA_GAMER_SERVICES_PROFILES_DIR a
// scratch store), never on the desktop.

#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "../../src/Internal/Guide/GuideUi.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
namespace Service = CNA::Internal::GamerServices;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
namespace Avatars = CNA::Internal::GamerServices::Avatars;

namespace
{
    constexpr int Width = 1280, Height = 720;

    // One scripted step of --capture: commands for the editor, then a picture once it is settled.
    struct Step
    {
        std::string name;
        std::vector<Ui::Command> commands;
        std::function<void()> action;
    };

    class AvatarEditorGame : public Game
    {
        GraphicsDeviceManager graphics_{this};
        std::unique_ptr<SpriteBatch> batch_;
        std::unique_ptr<Texture2D> white_;
        bool signInShown_ = false;
        bool opened_ = false;
        bool closed_ = false;
        bool saved_ = false;
        std::string gamertag_;
        int failures_ = 0;

        std::string captureDir_;
        std::vector<Step> steps_;
        std::size_t step_ = 0;
        bool stepStarted_ = false;
        int settled_ = 0;
        int waited_ = 0;

        void Open(SignedInGamer& gamer)
        {
            gamertag_ = gamer.getGamertagProperty();
            Ui::AvatarEditorOptions options;
            options.seed = captureDir_.empty() ? 0u : 1u;
            options.closed = [this](bool saved) {
                closed_ = true;
                saved_ = saved;
            };
            Ui::open(Ui::avatarEditorScreen(gamer.getPlayerIndexProperty(), std::move(options)), gamer.getPlayerIndexProperty());
            opened_ = true;
        }

        void Fail(const std::string& message)
        {
            std::printf("[FAIL] cna_avatar_editor: %s\n", message.c_str());
            ++failures_;
        }

        void Script()
        {
            using C = Ui::Command;
            steps_ = {
                {"01-editor", {}, {}},
                {"02-body-types", {C::Accept}, {}},
                {"03-height", {C::Down, C::Right, C::Right, C::Right, C::Right}, {}},
                {"04-face-shapes", {C::Back, C::Down, C::Down, C::Accept, C::Right}, {}},
                {"05-face-chosen", {C::Accept, C::Down, C::Down, C::Down, C::Right, C::Right, C::Right}, {}},
                {"06-eyes", {C::Back, C::Down, C::Accept, C::Right}, {}},
                {"07-hair-styles", {C::Back, C::Down, C::Down, C::Accept, C::Right, C::Right}, {}},
                {"08-hair-color", {C::Accept, C::Down, C::Right, C::Right, C::Right}, {}},
                {"09-tops", {C::Accept, C::Back, C::Next, C::Next, C::Accept, C::Right}, {}},
                {"10-bottoms", {C::Accept, C::Back, C::Next, C::Accept, C::Right}, {}},
                {"11-shoes", {C::Accept, C::Back, C::Next, C::Accept, C::Right}, {}},
                {"12-headwear", {C::Accept, C::Back, C::Next, C::Next, C::Accept, C::Right, C::Right}, {}},
                {"13-randomized", {C::Back, C::Y}, {}},
                {"14-leave", {C::Back}, {}},
                {"15-saved", {}, [] { Ui::clickForTesting(0); }},
            };
        }

        // --capture: one step per settled picture.
        void CaptureUpdate()
        {
            if (step_ >= steps_.size())
                return;
            if (!stepStarted_)
            {
                for (auto command : steps_[step_].commands)
                    Ui::sendForTesting(command);
                if (steps_[step_].action)
                    steps_[step_].action();
                stepStarted_ = true;
                settled_ = 0;
                waited_ = 0;
                return;
            }
            // Settled: nothing loading and the camera has eased to its view.
            settled_ = Ui::busyForTesting() ? 0 : settled_ + 1;
            if (++waited_ > 3000)
            {
                Fail("step " + steps_[step_].name + " never settled");
                Exit();
            }
        }

    public:
        explicit AvatarEditorGame(std::string captureDir)
            : captureDir_(std::move(captureDir))
        {
            graphics_.setPreferredBackBufferWidthProperty(Width);
            graphics_.setPreferredBackBufferHeightProperty(Height);
            graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
            getComponentsProperty().Add(new GamerServicesComponent(*this));
            if (!captureDir_.empty())
                Script();
        }

        int getFailures() const { return failures_; }

    protected:
        void LoadContent() override
        {
            getWindowProperty().setTitleProperty("CNA Avatar Editor");
            batch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
            white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1, {255, 255, 255, 255}));
        }

        void Update(GameTime& gameTime) override
        {
            Game::Update(gameTime);
            const auto* gamers = Gamer::getSignedInGamersProperty();
            if (!opened_)
            {
                if (gamers && gamers->getCountProperty() > 0)
                {
                    Open(*(*gamers)[0]);
                }
                else if (!captureDir_.empty())
                {
                    if (gameTime.getTotalGameTimeProperty().getTotalSecondsProperty() > 10.0)
                    {
                        Fail("nobody signed in (set CNA_GAMER_SERVICES_AUTO_SIGN_IN)");
                        Exit();
                    }
                }
                else if (!signInShown_ && !Guide::getIsVisibleProperty())
                {
                    Guide::ShowSignIn(1, false);
                    signInShown_ = true;
                }
                else if (signInShown_ && !Guide::getIsVisibleProperty())
                {
                    // Sign-in was cancelled.
                    Exit();
                }
                return;
            }
            if (!captureDir_.empty())
            {
                CaptureUpdate();
                return;
            }
            if (closed_)
                Exit();
        }

        void Draw(const GameTime& gameTime) override
        {
            // Behind sign-in: the same deep slate the editor stands in.
            auto& device = getGraphicsDeviceProperty();
            device.Clear(Color(18, 22, 30));
            batch_->Begin();
            for (int band = 0; band < 24; ++band)
                batch_->Draw(*white_, Rectangle(0, band * Height / 24, Width, Height / 24 + 1), Color(40 - band, 49 - band, 67 - band * 2));
            batch_->End();
            Game::Draw(gameTime);
        }

        void EndDraw() override
        {
            if (!captureDir_.empty() && opened_ && step_ < steps_.size() && stepStarted_ && settled_ >= 70)
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
                CNA::Internal::Graphics::ImageLoader::SavePng(rgba.data(), Width, Height, captureDir_ + "/" + steps_[step_].name + ".png");
                std::printf("captured %s\n", steps_[step_].name.c_str());
                ++step_;
                stepStarted_ = false;
                if (step_ == steps_.size())
                {
                    // The last step saved and closed the editor: the profile holds a valid avatar.
                    const auto stored = Service::localProfileAvatar(gamertag_);
                    if (!closed_ || !saved_)
                        Fail("the editor did not close saved");
                    else if (!Avatars::decode(std::vector<std::uint8_t>(stored.begin(), stored.end())))
                        Fail("the local profile does not hold a valid avatar");
                    std::printf("[%s] cna_avatar_editor: capture of %s, %d failures\n", failures_ ? "FAIL" : "PASS", gamertag_.c_str(), failures_);
                    Exit();
                }
            }
            Game::EndDraw();
        }
    };
}

int main(int argc, char** argv)
{
    std::string capture;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--capture" && i + 1 < argc)
        {
            capture = argv[++i];
        }
        else
        {
            std::printf("usage: cna_avatar_editor [--capture DIR]\n");
            return 2;
        }
    }
    if (!capture.empty())
        std::filesystem::create_directories(capture);
    AvatarEditorGame game(capture);
    game.Run();
    return game.getFailures() == 0 ? 0 : 1;
}
