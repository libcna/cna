#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/CNAHelper.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// GS-009: the standard XNA avatar API end to end -- no CNA avatar extension anywhere.
// Random avatars (AvatarDescription.CreateRandom), or one given as its description bytes, are drawn
// by AvatarRenderer with every AvatarAnimationPreset; the loading effect shows while their assets
// are assembled. For looking at avatars closely: a lineup of several, camera views of the whole
// body, the head, the hands and the feet, and lighting a game might use.
class AvatarDemo : public Microsoft::Xna::Framework::Game
{
public:
    /** @brief Where the camera looks. */
    enum class View { Body, Head, Hands, Feet };

    explicit AvatarDemo(Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType);
    ~AvatarDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Exit cleanly after @p n frames (smoke runs). */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }
    /** @brief Fixes the orbiting camera's yaw in radians. */
    void SetFixedCameraYawEXT(float yawRadians) { cameraYaw_ = yawRadians; fixedCameraYaw_ = true; }
    /** @brief Saves the last smoke frame as a PNG. */
    void SetScreenshotPathEXT(std::string path) { screenshotPath_ = std::move(path); }
    /** @brief Shows the help overlay from the start. */
    void SetShowHelpForTestingEXT(bool visible) { showHelp_ = visible; }
    /** @brief Starts with the named preset (e.g. "Wave"); unknown names are ignored. */
    void SetInitialPresetEXT(const std::string& name);
    /** @brief Draws this description (its 1021 bytes) instead of a random avatar. */
    void SetDescriptionEXT(std::vector<SharpRuntime::bytecs> bytes) { given_ = std::move(bytes); }
    /** @brief Starts with a lineup of @p count random avatars (1 for a single one). */
    void SetLineupEXT(int count) { lineup_ = count; focus_ = count / 2; }
    /** @brief Starts with the camera on a part of the avatar. */
    void SetViewEXT(View view) { view_ = view; }

    GetTypeNameHPP()

private:
    struct Shown {
        std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarDescription> description;
        std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer;
    };
    void NewAvatars();
    void StartPreset(std::size_t index);
    void ApplyLighting(Microsoft::Xna::Framework::GamerServices::AvatarRenderer& renderer) const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 FocusOf(const Shown& shown, float x) const;
    [[nodiscard]] float StandAt(int index) const;

    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType_;
    std::optional<std::vector<SharpRuntime::bytecs>> given_;
    std::vector<Shown> shown_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarAnimation> animation_;
    std::size_t presetIndex_ = 0;
    int expressionOverride_ = -1;
    int lineup_ = 1;
    int focus_ = 0;
    int lighting_ = 0;
    View view_ = View::Body;
    bool paused_ = false;

    Microsoft::Xna::Framework::Vector3 target_;
    bool haveTarget_ = false;
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.0f;
    float zoom_ = 1.0f;
    bool fixedCameraYaw_ = false;
    int smokeFramesLeft_ = -1;
    std::string screenshotPath_;
    bool showHelp_ = false;
    bool spaceWasDown_ = false;
    bool backWasDown_ = false;
    bool rWasDown_ = false;
    bool gWasDown_ = false;
    bool eWasDown_ = false;
    bool lWasDown_ = false;
    bool tabWasDown_ = false;
    bool vWasDown_ = false;
    bool kWasDown_ = false;
    bool pWasDown_ = false;
    bool f1WasDown_ = false;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
};
