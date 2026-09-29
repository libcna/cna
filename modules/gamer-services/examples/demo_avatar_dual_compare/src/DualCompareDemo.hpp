#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "CNA/CNAHelper.hpp"

#include <array>
#include <memory>
#include <string>

// GS-009: a random female and a random male avatar side by side, playing the same standard
// preset (Space: next, R: new pair) -- the standard XNA avatar API only.
class DualCompareDemo : public Microsoft::Xna::Framework::Game
{
public:
    DualCompareDemo();
    ~DualCompareDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Exit cleanly after @p n frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }
    /** @brief Shows the help overlay from the start. */
    void SetShowHelpForTestingEXT(bool visible) { showHelp_ = visible; }
    /** @brief Saves the last smoke frame as a PNG. */
    void SetScreenshotPathEXT(std::string path) { screenshotPath_ = std::move(path); }

    GetTypeNameHPP()

private:
    void NewPair();
    void StartPreset(std::size_t index);

    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};
    std::array<std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarDescription>, 2> descriptions_;
    std::array<std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer>, 2> renderers_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarAnimation> animation_;
    std::size_t preset_ = 0;
    int smokeFramesLeft_ = -1;
    std::string screenshotPath_;
    bool showHelp_ = false;
    bool spaceWasDown_ = false;
    bool rWasDown_ = false;
    bool f1WasDown_ = false;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
};
