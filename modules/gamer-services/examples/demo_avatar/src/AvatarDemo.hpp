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

// GS-009: the standard XNA avatar API end to end -- no CNA avatar extension anywhere.
// A random avatar (AvatarDescription.CreateRandom) is drawn by AvatarRenderer with every
// AvatarAnimationPreset; the loading effect shows while its assets are assembled.
class AvatarDemo : public Microsoft::Xna::Framework::Game
{
public:
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

    GetTypeNameHPP()

private:
    void NewAvatar();
    void StartPreset(std::size_t index);

    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarDescription> description_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarAnimation> animation_;
    std::size_t presetIndex_ = 0;
    int expressionOverride_ = -1;

    float cameraYaw_ = 0.0f;
    bool fixedCameraYaw_ = false;
    int smokeFramesLeft_ = -1;
    std::string screenshotPath_;
    bool showHelp_ = false;
    bool spaceWasDown_ = false;
    bool rWasDown_ = false;
    bool gWasDown_ = false;
    bool eWasDown_ = false;
    bool f1WasDown_ = false;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
};
