#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.17: cna_demo_avatar_appearance_tint_studio. AvatarAppearanceEXT's 5 tint slots
// (Skin/Hair/Shirt/Pants/Shoes) and AvatarRenderer::SetAppearanceEXT as a live color
// customization screen. Number keys 1-5 select a slot, Up/Down cycle preset swatch colors, the
// avatar re-tints on the next DrawRealEXT call with an on-screen swatch row showing the 5
// current colors.
class TintStudioDemo : public Microsoft::Xna::Framework::Game
{
public:
    TintStudioDemo();
    ~TintStudioDemo() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    void ApplyAppearance();

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType gender_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;
    Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT appearance_;

    int selectedSlot_ = 0; // 0=Skin 1=Hair 2=Shirt 3=Pants 4=Shoes
    int paletteIndex_[5] = {0, 0, 0, 0, 0};

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    float cameraYaw_ = 0.0f;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;

    int smokeFramesLeft_ = -1;
    int smokeStep_ = 0;
};
