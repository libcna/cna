#include "SyncGame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
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
#include "common/SimpleFontEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
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

    std::vector<std::string> ClipNamesForGender(AvatarBodyType gender)
    {
        std::vector<std::string> names = {"Stand0", "Stand1", "Stand2", "Stand3", "Stand4",
                                           "Stand5", "Stand6", "Stand7", "Wave", "Clap", "Celebrate"};
        if (gender == AvatarBodyType::Female)
        {
            for (const char* n : {"FemaleIdleCheckNails", "FemaleIdleLookAround", "FemaleIdleShiftWeight",
                                   "FemaleIdleFixShoe", "FemaleAngry", "FemaleConfused", "FemaleLaugh",
                                   "FemaleCry", "FemaleShocked", "FemaleYawn"})
            {
                names.emplace_back(n);
            }
        }
        else
        {
            for (const char* n : {"MaleIdleLookAround", "MaleIdleStretch", "MaleIdleShiftWeight",
                                   "MaleIdleCheckHand", "MaleAngry", "MaleConfused", "MaleLaugh",
                                   "MaleCry", "MaleSurprised", "MaleYawn"})
            {
                names.emplace_back(n);
            }
        }
        return names;
    }

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
        "Launch with --host (Male avatar, creates a SystemLink session) or",
        "--join (Female avatar, finds/joins the host). Only position/yaw/",
        "clip-index sync over the wire - no asset bytes.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
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

void SyncGame::LoadAvatarView(AvatarView& view, AvatarBodyType gender)
{
    auto& content = getContentProperty();
    view.model = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(gender));

    auto& device = getGraphicsDeviceProperty();
    view.renderer = std::make_unique<AvatarRenderer>(nullptr);
    view.renderer->EnableRealRenderingEXT(device, view.model);

    AvatarAppearanceEXT appearance;
    if (gender == AvatarBodyType::Male)
    {
        appearance.setSkinColorProperty(Color(210, 170, 130, 255));
        appearance.setHairColorProperty(Color(40, 25, 15, 255));
    }
    else
    {
        appearance.setSkinColorProperty(Color(235, 200, 170, 255));
        appearance.setHairColorProperty(Color(200, 60, 30, 255));
    }
    view.renderer->SetAppearanceEXT(appearance);

    view.renderer->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    view.renderer->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    view.renderer->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));

    view.clipNames = ClipNamesForGender(gender);
}

void SyncGame::LoadContent()
{
    LoadAvatarView(localView_, localGender_);
    LoadAvatarView(remoteView_, remoteGender_);

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
        localClipIndex_ = (localClipIndex_ + 1) % localView_.clipNames.size();
        localClipSeconds_ = 0.0;
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
            localClipIndex_ = (localClipIndex_ + 1) % localView_.clipNames.size();
            localClipSeconds_ = 0.0;
        }
    }

    localClipSeconds_ += static_cast<double>(dt);
    remoteClipSeconds_ += static_cast<double>(dt);

    if (localNetworkGamer_ != nullptr)
    {
        PacketWriter writer;
        writer.Write(localPos_);
        writer.Write(localYaw_);
        writer.Write(static_cast<int32_t>(localClipIndex_));
        localNetworkGamer_->SendData(writer, SendDataOptions::InOrder);

        PacketReader reader;
        NetworkGamer* sender = nullptr;
        while (localNetworkGamer_->getIsDataAvailableProperty())
        {
            localNetworkGamer_->ReceiveData(reader, sender);
            if (sender != nullptr && !sender->getIsLocalProperty())
            {
                remotePos_ = reader.ReadVector2();
                remoteYaw_ = reader.ReadSingle();
                const auto newClipIndex = static_cast<std::size_t>(reader.ReadInt32());
                if (newClipIndex != remoteClipIndex_)
                {
                    remoteClipIndex_ = newClipIndex % remoteView_.clipNames.size();
                    remoteClipSeconds_ = 0.0;
                }
                haveRemote_ = true;
            }
        }
    }

    positionLogTimer_ += dt;
    if (positionLogTimer_ >= 1.0f)
    {
        positionLogTimer_ = 0.0f;
        std::printf("[NetAvatarSync] local=(%.2f,%.2f) clip=%s haveRemote=%s remote=(%.2f,%.2f) "
                    "remoteClip=%s\n",
                    localPos_.X, localPos_.Y, localView_.clipNames[localClipIndex_].c_str(),
                    haveRemote_ ? "true" : "false", remotePos_.X, remotePos_.Y,
                    haveRemote_ ? remoteView_.clipNames[remoteClipIndex_].c_str() : "-");
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[NetAvatarSync] Smoke test complete: haveRemote=%s localClip=%s\n",
                        haveRemote_ ? "true" : "false", localView_.clipNames[localClipIndex_].c_str());
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
    localView_.renderer->DrawRealEXT(localView_.clipNames[localClipIndex_],
                                      System::TimeSpan::FromSeconds(localClipSeconds_), /*loop=*/true);

    if (haveRemote_)
    {
        remoteView_.renderer->setWorldProperty(
            Matrix::CreateRotationY(remoteYaw_) * Matrix::CreateTranslation(Vector3(remotePos_.X, 0.0f, remotePos_.Y)));
        remoteView_.renderer->setViewProperty(view);
        remoteView_.renderer->setProjectionProperty(projection);
        remoteView_.renderer->DrawRealEXT(remoteView_.clipNames[remoteClipIndex_],
                                           System::TimeSpan::FromSeconds(remoteClipSeconds_), /*loop=*/true);
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
