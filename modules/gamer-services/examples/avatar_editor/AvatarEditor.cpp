// SPDX-License-Identifier: MS-PL
// cna_avatar_editor [--capture DIR]
//
// The CNA avatar editor: a program of its own, as the console's avatar editor was, so no game needs
// customization code. The player signs in through the standard Guide (a CNA service account or an
// offline local profile), edits body, features and style against the newest catalog that account
// accepts, previews the result with the standard AvatarRenderer, and saves it to the service
// (avatars.set) or to the local profile store. Running games see the change through
// AvatarDescription.Changed.
//
// --capture DIR walks every page, changes a few rows, randomizes and saves, writing a PNG of each
// step into DIR and exiting with the result; run it on a private display with
// CNA_GAMER_SERVICES_AUTO_SIGN_IN naming a local profile (and CNA_GAMER_SERVICES_PROFILES_DIR a
// scratch store), never on the desktop.

#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Graphics/SystemFont.hpp"
#include "CNA/Internal/Runtime/IGameOverlay.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
namespace Service = CNA::Internal::GamerServices;

namespace
{
    constexpr int Width = 1280, Height = 720, PanelWidth = 500;
    const Color Background(54, 60, 72);
    const Color Panel(34, 38, 46, 235);
    const Color Text(232, 236, 242);
    const Color Dim(150, 158, 172);
    const Color Accent(96, 170, 255);
    const char* const PageNames[] = {"Body", "Features", "Style"};

    // One logical input: a key or a pad button, repeating while held.
    struct Repeater
    {
        double held = -1.0;
        int Poll(bool down, double dt)
        {
            if (!down)
            {
                held = -1.0;
                return 0;
            }
            if (held < 0.0)
            {
                held = 0.0;
                return 1;
            }
            const double before = held;
            held += dt;
            // First repeat after 0.4 s, then every 0.09 s.
            auto repeats = [](double t) { return t < 0.4 ? 0 : 1 + static_cast<int>((t - 0.4) / 0.09); };
            return repeats(held) - repeats(before);
        }
    };

    enum class Stage { SigningIn, Loading, Editing, Saving, Failed };

    // Work handed to the gamer services executor; its completion runs on this game's thread.
    struct Pending
    {
        std::mutex lock;
        std::vector<unsigned char> avatar;
        std::string catalogJson;
        std::string error;
    };

    class AvatarEditorGame : public Game
    {
        GraphicsDeviceManager graphics_{this};
        std::unique_ptr<SpriteBatch> batch_;
        std::unique_ptr<SpriteFont> font_;
        std::unique_ptr<Texture2D> white_;
        std::unique_ptr<RenderTarget2D> captureTarget_;

        Stage stage_ = Stage::SigningIn;
        bool signInShown_ = false;
        SignedInGamer* gamer_ = nullptr;
        std::string gamertag_;
        std::string userId_;
        std::string status_;
        bool quitArmed_ = false;
        std::unique_ptr<Avatars::AvatarEditorModel> model_;

        // The preview keeps drawing the last ready avatar while the next one loads.
        std::unique_ptr<AvatarDescription> shownDescription_, pendingDescription_;
        std::unique_ptr<AvatarRenderer> shown_, pending_;
        std::unique_ptr<AvatarAnimation> animation_;
        float yaw_ = 0.35f;
        float zoom_ = 0.0f;

        Repeater up_, down_, left_, right_, pageBack_, pageNext_, random_, revert_, save_, quit_;
        KeyboardState keys_;
        GamePadState pad_;

        std::string captureDir_;
        int captureStep_ = 0;
        int captureWait_ = 0;
        int failures_ = 0;

        void Fail(std::string message)
        {
            stage_ = Stage::Failed;
            status_ = std::move(message);
            std::printf("[FAIL] cna_avatar_editor: %s\n", status_.c_str());
            ++failures_;
        }

        void Preview()
        {
            // A description the renderer is still assembling is dropped for the newer one.
            pendingDescription_ = std::make_unique<AvatarDescription>(model_->encoded());
            pending_ = std::make_unique<AvatarRenderer>(pendingDescription_.get(), false);
            pending_->setLightDirectionProperty(Vector3::Normalize(Vector3(-0.45f, -0.55f, -0.7f)));
            pending_->setLightColorProperty(Vector3(0.78f, 0.75f, 0.70f));
            pending_->setAmbientLightColorProperty(Vector3(0.40f, 0.42f, 0.47f));
        }

        void StartEditing(std::shared_ptr<const Avatars::CatalogManifest> catalog, std::vector<unsigned char> avatar)
        {
            model_ = std::make_unique<Avatars::AvatarEditorModel>(std::move(catalog),
                std::vector<std::uint8_t>(avatar.begin(), avatar.end()), captureDir_.empty() ? std::random_device{}() : 1u);
            animation_ = std::make_unique<AvatarAnimation>(AvatarAnimationPreset::Stand0);
            stage_ = Stage::Editing;
            status_ = avatar.empty() ? "No avatar yet: this is a random one to start from." : "";
            Preview();
        }

        void Load()
        {
            gamertag_ = gamer_->getGamertagProperty();
            userId_ = Service::GamerAccess::userId(*gamer_);
            stage_ = Stage::Loading;
            if (userId_.empty())
            {
                // An offline profile edits against the newest catalog compiled into this build.
                StartEditing(Avatars::embeddedCatalogs().back(), Service::localProfileAvatar(gamertag_));
                return;
            }
            // An account edits against the newest catalog its service has imported, which is the one
            // the service validates a saved description against.
            auto pending = std::make_shared<Pending>();
            auto service = Service::backend();
            service->submit(
                [pending, userId = userId_, origin = std::weak_ptr(service)] {
                    try
                    {
                        const auto executor = origin.lock();
                        if (!executor)
                            throw std::runtime_error("gamer services were shut down");
                        auto avatars = executor->avatars({userId});
                        auto catalog = executor->avatarCatalog(0);
                        std::lock_guard guard(pending->lock);
                        pending->avatar = avatars.empty() ? std::vector<unsigned char>{} : std::move(avatars.front());
                        pending->catalogJson = std::move(catalog);
                    }
                    catch (const std::exception& error)
                    {
                        std::lock_guard guard(pending->lock);
                        pending->error = error.what();
                    }
                },
                [this, pending] {
                    std::lock_guard guard(pending->lock);
                    if (stage_ != Stage::Loading)
                        return;
                    if (!pending->error.empty())
                        return Fail("The CNA service could not be reached: " + pending->error);
                    try
                    {
                        const auto version = Avatars::parseManifest(pending->catalogJson).version;
                        auto catalog = Avatars::catalogManifest(version);
                        if (!catalog)
                            return Fail("The service's avatar catalog " + std::to_string(version) + " is unavailable.");
                        StartEditing(std::move(catalog), std::move(pending->avatar));
                    }
                    catch (const std::exception& error)
                    {
                        Fail(std::string("The service's avatar catalog is malformed: ") + error.what());
                    }
                });
        }

        void Save()
        {
            if (!model_->differsFromStored())
            {
                status_ = "Nothing to save.";
                return;
            }
            const auto bytes = model_->encoded();
            if (userId_.empty())
            {
                if (Service::setLocalProfileAvatar(gamertag_, std::vector<unsigned char>(bytes.begin(), bytes.end())))
                {
                    model_->markSaved();
                    status_ = "Saved to the local profile.";
                }
                else
                {
                    status_ = "The local profile store could not be written.";
                    ++failures_;
                }
                return;
            }
            stage_ = Stage::Saving;
            status_ = "Saving...";
            auto pending = std::make_shared<Pending>();
            auto service = Service::backend();
            service->submit(
                [pending, userId = userId_, bytes, origin = std::weak_ptr(service)] {
                    try
                    {
                        const auto executor = origin.lock();
                        if (!executor)
                            throw std::runtime_error("gamer services were shut down");
                        executor->setAvatar(userId, std::vector<unsigned char>(bytes.begin(), bytes.end()));
                    }
                    catch (const Service::ServiceOperationError& error)
                    {
                        std::lock_guard guard(pending->lock);
                        pending->error = error.code == "RATE_LIMITED" ? "saved too recently, try again in a moment" : "the service refused it (" + error.code + ")";
                    }
                    catch (const std::exception& error)
                    {
                        std::lock_guard guard(pending->lock);
                        pending->error = error.what();
                    }
                },
                [this, pending] {
                    std::lock_guard guard(pending->lock);
                    stage_ = Stage::Editing;
                    if (pending->error.empty())
                    {
                        model_->markSaved();
                        status_ = "Saved to your CNA account.";
                    }
                    else
                    {
                        status_ = "Not saved: " + pending->error;
                        ++failures_;
                    }
                });
        }

        void Edit(double dt)
        {
            const bool padOn = pad_.getIsConnectedProperty();
            auto pad = [&](Buttons button) { return padOn && pad_.IsButtonDown(button); };
            const auto stick = padOn ? pad_.getThumbSticksProperty().getLeftProperty() : Vector2::Zero;
            const int rows = up_.Poll(keys_.IsKeyDown(Keys::Up) || pad(Buttons::DPadUp) || stick.Y > 0.6f, dt) * -1 +
                             down_.Poll(keys_.IsKeyDown(Keys::Down) || pad(Buttons::DPadDown) || stick.Y < -0.6f, dt);
            const int steps = left_.Poll(keys_.IsKeyDown(Keys::Left) || pad(Buttons::DPadLeft) || stick.X < -0.6f, dt) * -1 +
                              right_.Poll(keys_.IsKeyDown(Keys::Right) || pad(Buttons::DPadRight) || stick.X > 0.6f, dt);
            const int pages = pageBack_.Poll(keys_.IsKeyDown(Keys::Q) || pad(Buttons::LeftShoulder), dt) * -1 +
                              pageNext_.Poll(keys_.IsKeyDown(Keys::E) || keys_.IsKeyDown(Keys::Tab) || pad(Buttons::RightShoulder), dt);
            if (pages != 0)
                model_->turnPage(pages);
            if (rows != 0)
                model_->select(rows);
            bool changed = steps != 0 && model_->adjust(steps);
            if (random_.Poll(keys_.IsKeyDown(Keys::R) || pad(Buttons::Y), dt) == 1)
            {
                model_->randomize();
                changed = true;
            }
            if (revert_.Poll(keys_.IsKeyDown(Keys::Back) || pad(Buttons::X), dt) == 1)
            {
                model_->revert();
                changed = true;
            }
            if (changed)
            {
                quitArmed_ = false;
                status_.clear();
                Preview();
            }
            if (save_.Poll(keys_.IsKeyDown(Keys::Enter) || pad(Buttons::Start), dt) == 1)
                Save();

            // Rotate with Z/C or the right stick.
            const auto look = padOn ? pad_.getThumbSticksProperty().getRightProperty() : Vector2::Zero;
            yaw_ += static_cast<float>(dt) * 2.0f * ((keys_.IsKeyDown(Keys::C) ? 1.0f : 0.0f) - (keys_.IsKeyDown(Keys::Z) ? 1.0f : 0.0f) + look.X);
        }

        void Capture(const std::string& name)
        {
            auto& device = getGraphicsDeviceProperty();
            device.SetRenderTarget(captureTarget_.get());
            DrawFrame();
            // What the player sees includes the Guide's notifications ("Editor signed in").
            if (auto* overlay = getServicesProperty().GetService<CNA::Internal::Runtime::IGameOverlay>())
                overlay->draw();
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(static_cast<std::size_t>(Width) * Height);
            captureTarget_->GetData(pixels.data(), static_cast<int>(pixels.size()));
            std::vector<std::uint8_t> rgba(pixels.size() * 4);
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                rgba[i * 4] = pixels[i].getRProperty();
                rgba[i * 4 + 1] = pixels[i].getGProperty();
                rgba[i * 4 + 2] = pixels[i].getBProperty();
                rgba[i * 4 + 3] = 255;
            }
            CNA::Internal::Graphics::ImageLoader::SavePng(rgba.data(), Width, Height, captureDir_ + "/" + name + ".png");
        }

        // --capture: one scripted step per ready preview.
        void CaptureStep()
        {
            if (stage_ == Stage::Saving || (pending_ && pending_->getStateProperty() == AvatarRendererState::Loading))
            {
                if (++captureWait_ > 3000)
                {
                    Fail("the preview never became ready");
                    Exit();
                }
                return;
            }
            captureWait_ = 0;
            auto pageTo = [&](Avatars::EditorCategory page) {
                while (model_->category() != page)
                    model_->turnPage(1);
            };
            switch (captureStep_++)
            {
            case 0: Capture("01-body"); pageTo(Avatars::EditorCategory::Body); model_->select(1); model_->adjust(6); model_->select(1); model_->adjust(3); Preview(); break;
            case 1: Capture("02-body-edited"); pageTo(Avatars::EditorCategory::Features); break;
            case 2: Capture("03-features"); model_->select(2); model_->adjust(5); model_->select(1); model_->adjust(-4); Preview(); break;
            case 3: Capture("04-features-edited"); pageTo(Avatars::EditorCategory::Style); model_->adjust(2); model_->select(8); model_->adjust(2); model_->select(1); model_->adjust(1); Preview(); break;
            case 4: Capture("05-style-edited"); model_->randomize(); Preview(); break;
            case 5: Capture("06-randomized"); Save(); break;
            case 6:
            {
                Capture("07-saved");
                const auto stored = userId_.empty() ? Service::localProfileAvatar(gamertag_) : std::vector<unsigned char>{};
                if (userId_.empty() && std::vector<std::uint8_t>(stored.begin(), stored.end()) != model_->encoded())
                    Fail("the local profile does not hold the saved avatar");
                std::printf("[%s] cna_avatar_editor: capture of %s, %d failures\n", failures_ ? "FAIL" : "PASS", gamertag_.c_str(), failures_);
                Exit();
                break;
            }
            }
        }

        void DrawText(const std::string& text, float x, float y, Color color, float scale = 2.0f)
        {
            batch_->DrawString(*font_, text, Vector2(x, y), color, 0.0f, Vector2::Zero, scale, SpriteEffects::None, 0.0f);
        }

        void DrawAvatar()
        {
            auto& device = getGraphicsDeviceProperty();
            const auto whole = device.getViewportProperty();
            const Viewport area(PanelWidth, 0, Width - PanelWidth, Height);
            device.setViewportProperty(area);
            const float height = model_->descriptor().heightMillimeters / 1000.0f;
            // The features page looks at the face; the others at the whole avatar.
            const float focus = zoom_;
            const Vector3 target(0.0f, (height * 0.5f) * (1.0f - focus) + (height - 0.13f) * focus, 0.0f);
            const float distance = (1.2f + 1.25f * height) * (1.0f - focus) + 0.95f * focus;
            const Vector3 eye = target + Vector3(0.0f, 0.1f * (1.0f - focus), distance);
            auto& renderer = *shown_;
            renderer.setWorldProperty(Matrix::CreateRotationY(yaw_));
            renderer.setViewProperty(Matrix::CreateLookAt(eye, target, Vector3::Up));
            renderer.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(0.62f,
                static_cast<float>(area.getWidthProperty()) / static_cast<float>(area.getHeightProperty()), 0.05f, 20.0f));
            renderer.Draw(animation_.get());
            device.setViewportProperty(whole);
        }

        void DrawPanel()
        {
            batch_->Draw(*white_, Rectangle(0, 0, PanelWidth, Height), Panel);
            DrawText("CNA Avatar Editor", 28, 24, Text, 3.0f);
            DrawText(gamertag_.empty() ? std::string("Sign in to edit your avatar") :
                     gamertag_ + (userId_.empty() ? "  (local profile)" : "  (CNA account)"), 28, 58, Dim);
            if (!model_)
            {
                DrawText(status_, 28, 110, Text);
                return;
            }
            float x = 28;
            for (int page = 0; page < Avatars::EditorCategoryCount; ++page)
            {
                const bool active = static_cast<int>(model_->category()) == page;
                const float w = std::string(PageNames[page]).size() * 12.0f + 20.0f;
                batch_->Draw(*white_, Rectangle(static_cast<int>(x), 92, static_cast<int>(w), 30), active ? Accent : Color(60, 66, 78));
                DrawText(PageNames[page], x + 10, 100, active ? Color(16, 20, 28) : Text);
                x += w + 8;
            }
            // Pages longer than the panel scroll to keep the selected row in view.
            constexpr std::size_t Visible = 13;
            const std::size_t count = model_->fieldCount();
            const std::size_t first = count <= Visible ? 0 : std::min(count - Visible, model_->selection() > Visible / 2 ? model_->selection() - Visible / 2 : 0);
            if (first > 0)
                DrawText("...", 32, 128, Dim, 1.5f);
            if (first + Visible < count)
                DrawText("...", 32, 140 + Visible * 32 - 10, Dim, 1.5f);
            float y = 140;
            for (std::size_t row = first; row < std::min(count, first + Visible); ++row)
            {
                const auto field = model_->field(row);
                const bool selected = row == model_->selection();
                if (selected)
                    batch_->Draw(*white_, Rectangle(20, static_cast<int>(y) - 6, PanelWidth - 40, 30), Color(70, 96, 140));
                DrawText(field.label, 32, y, selected ? Color::White : Text);
                const float valueX = 270;
                if (field.swatch)
                {
                    const auto c = *field.swatch;
                    batch_->Draw(*white_, Rectangle(static_cast<int>(valueX), static_cast<int>(y) - 2, 40, 22), Color(12, 12, 14));
                    batch_->Draw(*white_, Rectangle(static_cast<int>(valueX) + 2, static_cast<int>(y), 36, 18), Color(c.r, c.g, c.b));
                    DrawText(field.value, valueX + 52, y, selected ? Color::White : Dim);
                }
                else if (field.slider)
                {
                    const int bar = 120, left = static_cast<int>(valueX);
                    batch_->Draw(*white_, Rectangle(left, static_cast<int>(y) + 6, bar, 4), Color(90, 96, 110));
                    batch_->Draw(*white_, Rectangle(left + bar / 2 - 1, static_cast<int>(y) + 2, 2, 12), Color(130, 136, 150));
                    const int knob = left + static_cast<int>((*field.slider + 1.0f) * 0.5f * bar);
                    batch_->Draw(*white_, Rectangle(knob - 4, static_cast<int>(y), 8, 16), selected ? Accent : Text);
                    DrawText(field.value, valueX + bar + 16, y, selected ? Color::White : Dim);
                }
                else
                {
                    DrawText((selected ? "< " : "  ") + field.value + (selected ? " >" : ""), valueX - 24, y, selected ? Color::White : Text);
                }
                y += 32;
            }
            const std::string state = stage_ == Stage::Saving ? "" : model_->differsFromStored() ? "Unsaved changes" : "Saved";
            DrawText(status_.empty() ? state : status_, 28, Height - 112, status_.empty() ? Dim : Accent);
            DrawText("Up/Down choose   Left/Right change", 28, Height - 80, Dim, 1.5f);
            DrawText("Q/E (LB/RB) page   R (Y) random   Backspace (X) undo all", 28, Height - 62, Dim, 1.5f);
            DrawText("Enter (Start) save   Z/C (right stick) turn   Esc (Back) quit", 28, Height - 44, Dim, 1.5f);
        }

        void DrawFrame()
        {
            auto& device = getGraphicsDeviceProperty();
            device.Clear(Background);
            if (shown_ && animation_)
                DrawAvatar();
            batch_->Begin();
            DrawPanel();
            batch_->End();
        }

    public:
        explicit AvatarEditorGame(std::string captureDir)
            : captureDir_(std::move(captureDir))
        {
            graphics_.setPreferredBackBufferWidthProperty(Width);
            graphics_.setPreferredBackBufferHeightProperty(Height);
            graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
            getComponentsProperty().Add(new GamerServicesComponent(*this));
        }

        int getFailures() const { return failures_; }

    protected:
        void LoadContent() override
        {
            getWindowProperty().setTitleProperty("CNA Avatar Editor");
            auto& device = getGraphicsDeviceProperty();
            batch_ = std::make_unique<SpriteBatch>(device);
            font_ = CNA::Internal::Graphics::makeSystemFont(device);
            white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
            if (!captureDir_.empty())
                captureTarget_ = std::make_unique<RenderTarget2D>(device, Width, Height, false, SurfaceFormat::Color, DepthFormat::Depth24);
        }

        void Update(GameTime& gameTime) override
        {
            Game::Update(gameTime);
            const double dt = gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty();
            keys_ = Keyboard::GetState();
            pad_ = GamePad::GetState(PlayerIndex::One);
            const bool quit = quit_.Poll(keys_.IsKeyDown(Keys::Escape) || (pad_.getIsConnectedProperty() &&
                (pad_.IsButtonDown(Buttons::Back) || pad_.IsButtonDown(Buttons::B))), dt) == 1;

            if (pending_ && pending_->getStateProperty() != AvatarRendererState::Loading)
            {
                shown_ = std::move(pending_);
                shownDescription_ = std::move(pendingDescription_);
            }
            if (animation_)
                animation_->Update(gameTime.getElapsedGameTimeProperty(), true);
            if (model_)
            {
                const float goal = model_->category() == Avatars::EditorCategory::Features ? 1.0f : 0.0f;
                zoom_ = captureDir_.empty() ? zoom_ + (goal - zoom_) * std::min(1.0f, static_cast<float>(dt) * 6.0f) : goal;
            }

            switch (stage_)
            {
            case Stage::SigningIn:
            {
                const auto* gamers = Gamer::getSignedInGamersProperty();
                if (gamers && gamers->getCountProperty() > 0)
                {
                    gamer_ = (*gamers)[0];
                    Load();
                }
                else if (!captureDir_.empty())
                {
                    if (gameTime.getTotalGameTimeProperty().getTotalSecondsProperty() > 10.0)
                    {
                        Fail("nobody signed in (set CNA_GAMER_SERVICES_AUTO_SIGN_IN)");
                        Exit();
                    }
                }
                else if (!Guide::getIsVisibleProperty() && !signInShown_)
                {
                    Guide::ShowSignIn(1, false);
                    signInShown_ = true;
                }
                else if (signInShown_ && !Guide::getIsVisibleProperty())
                {
                    Exit();
                }
                break;
            }
            case Stage::Editing:
                if (Guide::getIsVisibleProperty())
                    break;
                if (!captureDir_.empty())
                    break;
                if (quit)
                {
                    if (model_->differsFromStored() && !quitArmed_)
                    {
                        quitArmed_ = true;
                        status_ = "Unsaved changes: Esc again to leave without saving.";
                    }
                    else
                    {
                        Exit();
                    }
                    break;
                }
                Edit(dt);
                break;
            case Stage::Saving:
                break;
            case Stage::Failed:
                if (quit || !captureDir_.empty())
                    Exit();
                break;
            case Stage::Loading:
                break;
            }
        }

        void Draw(const GameTime& gameTime) override
        {
            if (!captureDir_.empty() && (stage_ == Stage::Editing || stage_ == Stage::Saving))
                CaptureStep();
            DrawFrame();
            Game::Draw(gameTime);
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
