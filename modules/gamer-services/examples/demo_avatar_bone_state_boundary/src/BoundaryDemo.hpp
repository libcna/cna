#pragma once

#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// Task 15.20: cna_demo_avatar_bone_state_boundary. Documents the real, verified
// AvatarRenderer skeleton-API boundary via console output, contrasted against the working
// SkinnedModelEXT EXT path demo_avatar actually uses. Minimal window (no meaningful rendering);
// all the actual content is printed to console during Initialize(), then the demo exits itself
// after a few frames (or on Esc/F1 interaction extending it - see Update()).
class BoundaryDemo : public Microsoft::Xna::Framework::Game
{
public:
    BoundaryDemo() = default;

    void Initialize() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Forces the F1 help overlay's initial visibility - Task 8 (plans/plan_net.md Phase 8):
     *  lets a non-interactive smoke/screenshot run verify the overlay actually renders. */
    void SetShowHelpForTestingEXT(bool visible) { showHelpEXT_ = visible; }

    /** @brief Saves a PNG of the backbuffer on the final smoke frame - Task 8 (plans/plan_net.md
     *  Phase 8), reusing Task 7.1's own examples/common/ScreenshotEXT.hpp helper. */
    void SetScreenshotPathEXT(std::string path) { screenshotPathEXT_ = std::move(path); }

private:
    int framesBeforeExit_ = 30;

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as demo_avatar's
    // own AvatarDemo (see that file's own comment for why this is a deliberate per-demo copy).
    // This demo has no other keyboard handling (it self-exits after a fixed frame count), so F1
    // and Esc are added here purely for the overlay, matching every other avatar demo's controls.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
