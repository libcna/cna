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

#include <memory>
#include <string>

// GS-009: the renderer's state and skeleton boundary with the standard XNA API. Prints the
// Loading -> Ready transition, ParentBones and the BindPose, then draws custom bones -- a preset
// with the head and left arm posed by the game -- and exits after a few seconds.
class BoundaryDemo : public Microsoft::Xna::Framework::Game
{
public:
    BoundaryDemo();
    ~BoundaryDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Shows the help overlay from the start. */
    void SetShowHelpForTestingEXT(bool visible) { showHelp_ = visible; }
    /** @brief Saves the last frame as a PNG. */
    void SetScreenshotPathEXT(std::string path) { screenshotPath_ = std::move(path); }

    GetTypeNameHPP()

private:
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarDescription> description_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarAnimation> animation_;
    int frame_ = 0;
    int readyFrame_ = -1;
    double seconds_ = 0.0;
    std::string screenshotPath_;
    bool showHelp_ = false;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
};
