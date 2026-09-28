#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "SyncGame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "System/IServiceProvider.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/AvatarPresetNamesEXT.hpp"
#include "common/SimpleFontEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimation;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarDescription;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kPiOver4 = kPi * 0.25f;
    constexpr float kCameraDistance = 5.0f;
    constexpr float kCameraHeight = 1.3f;
    constexpr float kTargetHeight = 0.9f;

    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2: decision 5a's default text block, adapted per this task's own instruction (keep
    // the F1/Esc lines identical across every demo, customize the rest) - this demo has a
    // host/client role, so the text notes which local avatar gender that implies.
    constexpr const char* kHelpLines[] = {
        "CNA Net Avatar Sync Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "Up/Down: Move local avatar",
        "Left/Right: Rotate local avatar",
        "Space: Next animation for the local avatar",
        "",
        "Launch with --host (male avatar, creates a SystemLink session) or",
        "--join (female avatar, finds/joins the host). Each side sends its",
        "AvatarDescription bytes once, then only position/yaw/preset.",
    };
}

SyncGame::SyncGame(bool isHost)
    : isHost_(isHost)
    , localGender_(isHost ? AvatarBodyType::Male : AvatarBodyType::Female)
    , remoteGender_(isHost ? AvatarBodyType::Female : AvatarBodyType::Male)
{
    localPos_ = Vector2(isHost_ ? -1.5f : 1.5f, 0.0f);

    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

SyncGame::~SyncGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}

void SyncGame::OnSessionEnded(System::Object* /*sender*/, const NetworkSessionEndedEventArgs& /*e*/)
{
    std::printf("[NetAvatarSync] SessionEnded.\n");
}

void SyncGame::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    localGamer_ = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    if (isHost_)
    {
        session_ = NetworkSession::Create(NetworkSessionType::SystemLink, 1, 8);
        std::printf("[NetAvatarSync] Hosting as \"%s\" (%s avatar), waiting for a client...\n",
                    localGamer_->getGamertagProperty().c_str(),
                    localGender_ == AvatarBodyType::Male ? "male" : "female");
    }
    else
    {
        std::printf("[NetAvatarSync] Searching for a session to join...\n");
        AvailableNetworkSessionCollection available =
            NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        for (int attempt = 0; attempt < 100 && available.getCountProperty() == 0; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            available = NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        }
        if (available.getCountProperty() == 0)
        {
            std::printf("[NetAvatarSync] No session found after searching - is a host running?\n");
            Exit();
            return;
        }
        const auto& constAvailable = available;
        session_ = NetworkSession::Join(&constAvailable[0]);
        std::printf("[NetAvatarSync] Joined the host's session as \"%s\" (%s avatar).\n",
                    localGamer_->getGamertagProperty().c_str(),
                    localGender_ == AvatarBodyType::Male ? "male" : "female");
    }

    session_->SessionEnded += [this](System::Object* sender, const NetworkSessionEndedEventArgs& e) { OnSessionEnded(sender, e); };
    localNetworkGamer_ = session_->getLocalGamersProperty()[0];
}

// The XNA way to share avatars in a session: send AvatarDescription.Description once, and let
// every machine build its own renderer from the bytes.
void SyncGame::ShowAvatar(AvatarView& view, const std::vector<SharpRuntime::bytecs>& description)
{
    view.description = std::make_unique<AvatarDescription>(description);
    view.renderer = std::make_unique<AvatarRenderer>(view.description.get());
    view.renderer->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));
    if (!view.animation)
    {
        StartPreset(view, view.preset);
    }
}

void SyncGame::StartPreset(AvatarView& view, std::size_t preset)
{
    view.preset = preset % CNAExamplesEXT::kAvatarPresets.size();
    view.animation = std::make_unique<AvatarAnimation>(CNAExamplesEXT::kAvatarPresets[view.preset].value);
}

void SyncGame::LoadContent()
{
    ShowAvatar(localView_, AvatarDescription::CreateRandom(localGender_).getDescriptionProperty());

    // Task 8.1/8.3 (plans/plan_net.md Phase 8): F1 help overlay plumbing.
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
}

void SyncGame::Update(GameTime& gameTime)
{
    if (session_ == nullptr)
    {
        return;
    }
    session_->Update();

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    // Task 8.2: F1 toggles overlay visibility, edge-triggered.
    const bool f1Down = kb.IsKeyDown(Keys::F1);
    if (f1Down && !f1WasDownEXT_)
    {
        showHelpEXT_ = !showHelpEXT_;
    }
    f1WasDownEXT_ = f1Down;

    const float speed = 1.2f;
    const float rotSpeed = 1.6f;
    if (kb.IsKeyDown(Keys::Up))    { localPos_.Y -= speed * dt; }
    if (kb.IsKeyDown(Keys::Down))  { localPos_.Y += speed * dt; }
    if (kb.IsKeyDown(Keys::Left))  { localYaw_ -= rotSpeed * dt; }
    if (kb.IsKeyDown(Keys::Right)) { localYaw_ += rotSpeed * dt; }
    cameraYaw_ += 0.2f * dt;

    if (kb.IsKeyDown(Keys::Space) && !previousKeys_.IsKeyDown(Keys::Space))
    {
        StartPreset(localView_, localView_.preset + 1);
    }
    previousKeys_ = kb;

    // Smoke-test mode has no real keyboard driving it - deterministically nudge position and
    // cycle the clip every 30 frames (matching the established Phase 15 deterministic-nudge
    // convention). Guarded by > 0, not >= 0 (Task 15.14's own discovery of that bug class).
    if (smokeFramesLeft_ > 0)
    {
        localPos_.Y += (isHost_ ? 1.0f : -1.0f) * 0.3f * dt;
        if (smokeFramesLeft_ % 30 == 0)
        {
            StartPreset(localView_, localView_.preset + 1);
        }
    }

    localView_.animation->Update(gameTime.getElapsedGameTimeProperty(), true);
    if (remoteView_.animation)
    {
        remoteView_.animation->Update(gameTime.getElapsedGameTimeProperty(), true);
    }

    if (localNetworkGamer_ != nullptr)
    {
        // Packet 1: the avatar description, reliably and only now and then (a late joiner gets
        // the next one). Packet 2: where the avatar is and what it plays, every frame.
        if (descriptionResend_-- <= 0)
        {
            PacketWriter description;
            description.Write(static_cast<std::uint8_t>(1));
            const auto bytes = localView_.description->getDescriptionProperty();
            description.Write(bytes.data(), 0, static_cast<SharpRuntime::intcs>(bytes.size()));
            localNetworkGamer_->SendData(description, SendDataOptions::ReliableInOrder);
            descriptionResend_ = 120;
        }
        PacketWriter writer;
        writer.Write(static_cast<std::uint8_t>(2));
        writer.Write(localPos_);
        writer.Write(localYaw_);
        writer.Write(static_cast<int32_t>(localView_.preset));
        localNetworkGamer_->SendData(writer, SendDataOptions::InOrder);

        PacketReader reader;
        NetworkGamer* sender = nullptr;
        while (localNetworkGamer_->getIsDataAvailableProperty())
        {
            localNetworkGamer_->ReceiveData(reader, sender);
            if (sender == nullptr || sender->getIsLocalProperty())
            {
                continue;
            }
            if (reader.ReadByte() == 1)
            {
                const auto bytes = reader.ReadBytes(1021);
                if (!remoteView_.description || remoteView_.description->getDescriptionProperty() != bytes)
                {
                    ShowAvatar(remoteView_, bytes);
                }
                continue;
            }
            remotePos_ = reader.ReadVector2();
            remoteYaw_ = reader.ReadSingle();
            const auto preset = static_cast<std::size_t>(reader.ReadInt32());
            if (!remoteView_.animation || preset != remoteView_.preset)
            {
                StartPreset(remoteView_, preset);
            }
            haveRemote_ = remoteView_.renderer != nullptr;
        }
    }

    positionLogTimer_ += dt;
    if (positionLogTimer_ >= 1.0f)
    {
        positionLogTimer_ = 0.0f;
        std::printf("[NetAvatarSync] local=(%.2f,%.2f) preset=%s haveRemote=%s remote=(%.2f,%.2f) "
                    "remotePreset=%s\n",
                    localPos_.X, localPos_.Y, CNAExamplesEXT::kAvatarPresets[localView_.preset].name,
                    haveRemote_ ? "true" : "false", remotePos_.X, remotePos_.Y,
                    haveRemote_ ? CNAExamplesEXT::kAvatarPresets[remoteView_.preset].name : "-");
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[NetAvatarSync] Smoke test complete: haveRemote=%s localPreset=%s\n",
                        haveRemote_ ? "true" : "false", CNAExamplesEXT::kAvatarPresets[localView_.preset].name);
            Exit();
        }
    }
}

void SyncGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color::CornflowerBlue);
    device.SetDepthTestEnabled(true);

    const auto& vp = device.getViewportProperty();
    const float aspect = (vp.getHeightProperty() > 0)
                              ? static_cast<float>(vp.getWidthProperty()) / static_cast<float>(vp.getHeightProperty())
                              : 1.0f;

    const Vector3 target(0.0f, kTargetHeight, 0.0f);
    const Vector3 eye(kCameraDistance * std::sin(cameraYaw_), kCameraHeight, kCameraDistance * std::cos(cameraYaw_));
    const Matrix view = Matrix::CreateLookAt(eye, target, Vector3::Up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 100.0f);

    localView_.renderer->setWorldProperty(
        Matrix::CreateRotationY(localYaw_) * Matrix::CreateTranslation(Vector3(localPos_.X, 0.0f, localPos_.Y)));
    localView_.renderer->setViewProperty(view);
    localView_.renderer->setProjectionProperty(projection);
    localView_.renderer->Draw(localView_.animation.get());

    if (haveRemote_)
    {
        remoteView_.renderer->setWorldProperty(
            Matrix::CreateRotationY(remoteYaw_) * Matrix::CreateTranslation(Vector3(remotePos_.X, 0.0f, remotePos_.Y)));
        remoteView_.renderer->setViewProperty(view);
        remoteView_.renderer->setProjectionProperty(projection);
        remoteView_.renderer->Draw(remoteView_.animation.get());
    }

    // Task 8.2: 3D scene drawn first (above), then the 2D help overlay on top.
    if (showHelpEXT_)
    {
        constexpr int kLineCount = static_cast<int>(sizeof(kHelpLines) / sizeof(kHelpLines[0]));
        // The real 5x7 bitmap font (CNAExamplesEXT::MakeSimpleFontEXT) is drawn at 1.5x scale -
        // legible, and small enough that the longest help line still fits an 800px-wide window.
        constexpr float kTextScale = 1.5f;
        constexpr float kLineHeight = 13.0f;
        constexpr float kPadding = 12.0f;
        float longestLineWidth = 0.0f;
        for (const char* line : kHelpLines)
        {
            longestLineWidth = std::max(longestLineWidth, font_->MeasureString(line).X * kTextScale);
        }
        const Rectangle panel(8, 8, static_cast<int>(longestLineWidth + kPadding * 2.0f),
                               static_cast<int>(kLineCount * kLineHeight + kPadding * 2.0f));

        spriteBatch_->Begin();
        spriteBatch_->Draw(*whitePixel_, panel, Color(255, 255, 255, 210));
        float y = panel.Y + kPadding;
        for (const char* line : kHelpLines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(panel.X + kPadding, y), Color(0, 0, 0, 255),
                                      0.0f, Vector2::Zero, kTextScale, SpriteEffects::None, 0.0f);
            y += kLineHeight;
        }
        spriteBatch_->End();
    }

    // Task 8.5 (plans/plan_net.md Phase 8): same smokeFramesLeft_==1 timing as demo_avatar's own
    // AvatarDemo - Game::Exit() suppresses Draw() on the frame Update() actually calls it.
    if (smokeFramesLeft_ == 1 && !screenshotPathEXT_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPathEXT_);
        screenshotPathEXT_.clear();
    }
}
