#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

// Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync. Combines Net + Avatar: each of two
// real processes loads its own gendered avatar (host=Male, client=Female) plus a pre-loaded copy
// of the *other* gender's content for rendering the remote peer (no asset bytes travel over the
// wire - only position/yaw/clip-index state, same as a real game would sync). Every frame sends
// local position/yaw plus the current AvatarAnimationPreset-style clip index over
// LocalNetworkGamer::SendData(SendDataOptions::InOrder) via PacketWriter; each process renders
// both its own and the remote peer's avatar in one 3D scene. Arrow keys move/rotate the local
// avatar; Space cycles its animation clip. The smallest possible proof that Net and Avatar/
// GamerServices compose the way a real game would use them together (in the spirit of
// cna-samples/ClientServerSample, but with avatars instead of tanks).
class SyncGame : public Microsoft::Xna::Framework::Game
{
public:
    explicit SyncGame(bool isHost);
    ~SyncGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

    /** @brief Forces the F1 help overlay's initial visibility - Task 8 (plans/plan_net.md Phase 8):
     *  lets a non-interactive smoke/screenshot run verify the overlay actually renders. */
    void SetShowHelpForTestingEXT(bool visible) { showHelpEXT_ = visible; }

    /** @brief Saves a PNG of the backbuffer on the final smoke frame - Task 8 (plans/plan_net.md
     *  Phase 8), reusing Task 7.1's own examples/common/ScreenshotEXT.hpp helper. */
    void SetScreenshotPathEXT(std::string path) { screenshotPathEXT_ = std::move(path); }

private:
    struct AvatarView
    {
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model;
        std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer;
        std::vector<std::string> clipNames;
    };

    void LoadAvatarView(AvatarView& view, Microsoft::Xna::Framework::GamerServices::AvatarBodyType gender);
    void OnSessionEnded(System::Object* sender, const Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs& e);

    bool isHost_;
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType localGender_;
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType remoteGender_;

    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;
    Microsoft::Xna::Framework::Net::NetworkSession* session_ = nullptr;
    Microsoft::Xna::Framework::Net::LocalNetworkGamer* localNetworkGamer_ = nullptr;

    AvatarView localView_;
    AvatarView remoteView_;

    Microsoft::Xna::Framework::Vector2 localPos_{0.0f, 0.0f}; // world X/Z
    float localYaw_ = 0.0f;
    std::size_t localClipIndex_ = 0;
    double localClipSeconds_ = 0.0;

    bool haveRemote_ = false;
    Microsoft::Xna::Framework::Vector2 remotePos_{1.5f, 0.0f};
    float remoteYaw_ = 0.0f;
    std::size_t remoteClipIndex_ = 0;
    double remoteClipSeconds_ = 0.0;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    float cameraYaw_ = 0.0f;

    int smokeFramesLeft_ = -1;
    float positionLogTimer_ = 0.0f;

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as demo_avatar's
    // own AvatarDemo (see that file's own comment for why this is a deliberate per-demo copy).
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
