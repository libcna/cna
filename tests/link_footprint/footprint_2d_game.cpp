// SPDX-License-Identifier: MS-PL
// Link-footprint consumer: a small 2D XNA game -- textures, a font, sound effects, the keyboard,
// the license check, the marketplace and a message box -- linked statically against CNA's module
// archives the way Mobile Eggbert links them.
//
// LinkFootprint_2DGame (cmake/Tests/LinkFootprint.cmake) asserts that the executable contains none
// of the subsystems this game does not use: the avatar system and its 30 MB catalog, the system
// Guide's social panels, the Gamer Services service client, the Model reader, the glTF importer,
// Draco, MediaPlayer, Video, Microphone, DynamicSoundEffectInstance and the non-default platforms
// (docs/link-size-architecture.md). It is linked, not run: with no argument main returns at once.
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

#include <memory>
#include <optional>

using namespace Microsoft::Xna::Framework;

namespace
{
    class Footprint2DGame final : public Game
    {
    public:
        Footprint2DGame() : graphics_(this) {}

    protected:
        void LoadContent() override
        {
            batch_ = std::make_unique<Graphics::SpriteBatch>(getGraphicsDeviceProperty());
            texture_.emplace(getContentProperty().Load<Graphics::Texture2D>("sprite"));
            font_.emplace(getContentProperty().Load<Graphics::SpriteFont>("font"));
            blip_ = std::make_unique<Audio::SoundEffect>(getContentProperty().Load<Audio::SoundEffect>("blip"));
        }

        void Update(GameTime& gameTime) override
        {
            if (Input::Keyboard::GetState().IsKeyDown(Input::Keys::Space))
            {
                blip_->Play();
            }
            if (Input::Keyboard::GetState().IsKeyDown(Input::Keys::B) &&
                GamerServices::Guide::getIsTrialModeProperty())
            {
                GamerServices::Guide::ShowMarketplace(PlayerIndex::One);
            }
            if (Input::Keyboard::GetState().IsKeyDown(Input::Keys::Escape))
            {
                delete GamerServices::Guide::BeginShowMessageBox(
                    PlayerIndex::One, "Quit", "Leave the game?", {"Yes", "No"}, 1,
                    GamerServices::MessageBoxIcon::None, {}, {});
            }
            Game::Update(gameTime);
        }

        void Draw(const GameTime& gameTime) override
        {
            getGraphicsDeviceProperty().Clear(Color::CornflowerBlue);
            batch_->Begin();
            batch_->Draw(*texture_, Vector2::Zero, Color::White);
            batch_->DrawString(*font_, "score", Vector2(8.0f, 8.0f), Color::White);
            batch_->End();
            Game::Draw(gameTime);
        }

    private:
        GraphicsDeviceManager graphics_;
        std::unique_ptr<Graphics::SpriteBatch> batch_;
        std::optional<Graphics::Texture2D> texture_;
        std::optional<Graphics::SpriteFont> font_;
        std::unique_ptr<Audio::SoundEffect> blip_;
    };
}

int main(int argc, char** argv)
{
    (void)argv;
    if (argc < 2)
    {
        return 0;
    }
    Footprint2DGame game;
    game.Run();
    return 0;
}
