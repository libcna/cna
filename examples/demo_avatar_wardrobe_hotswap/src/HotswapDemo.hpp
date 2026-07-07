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
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

#include <memory>

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
};
