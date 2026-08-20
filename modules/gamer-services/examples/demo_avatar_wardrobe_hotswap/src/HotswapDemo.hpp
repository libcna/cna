#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

#include <memory>
#include <string>

// Task 15.16: cna_demo_avatar_wardrobe_hotswap. SkinnedModelEXT::AttachPartEXT/RemovePartEXT
// (Task 11.4/11.5) used repeatedly *at runtime* - Tab cycles live between baked-in hair,
// wardrobe/hair_Cap, and wardrobe/hair_Ponytail, removing the old hair part and re-attaching,
// with the avatar visibly changing hairstyle without restarting the process.
//
// Restoring "baked-in" hair is not a same-model AttachPartEXT call (there is no
// wardrobe/hair_baked folder to attach from, and RemovePartEXT already freed the original part's
// GPU buffers the moment it was first replaced) - it requires a genuine fresh reload of the base
// avatar asset. Confirmed ContentManager::Unload() only clears its own cache map (safe: this
// demo's own model_/renderer_ hold independent shared_ptr/owning references, so clearing the
// cache cannot dangle them) before writing this, so cycling back to state 0 calls Unload() then
// re-Loads the base avatar fresh, rebuilding the renderer around it exactly like LoadContent()
// does the first time.
class HotswapDemo : public Microsoft::Xna::Framework::Game
{
public:
    HotswapDemo();
    ~HotswapDemo() override;

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
    void ApplyHairState(int state);
    void ConfigureRenderer();

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType gender_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    int hairState_ = 0; // 0 = baked-in, 1 = Cap, 2 = Ponytail
    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    float cameraYaw_ = 0.0f;

    int smokeFramesLeft_ = -1;
    int smokeSwapsRemaining_ = 6;

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as demo_avatar's
    // own AvatarDemo (see that file's own comment for why this is a deliberate per-demo copy).
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
