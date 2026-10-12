// SPDX-License-Identifier: MS-PL
// Link-footprint consumer: a game that uses the heavy subsystems the 2D consumer must not link --
// 3D models (glTF, GLB and Draco reach Load<Model>), effects, avatars with the built-in catalog,
// the system Guide through GamerServicesComponent, MediaPlayer and songs, video, the microphone and
// dynamic sound effects.
//
// LinkFootprint_FullFeatures (cmake/Tests/LinkFootprint.cmake) asserts each of those is present
// when used: the link boundaries remove what a program does not use, never what it does. It is
// linked, not run: with no argument main returns at once.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

#include <memory>
#include <optional>

using namespace Microsoft::Xna::Framework;

namespace
{
    class FootprintFullGame final : public Game
    {
    public:
        FootprintFullGame() : graphics_(this)
        {
            getComponentsProperty().Add(&gamerServices_);
        }

    protected:
        void LoadContent() override
        {
            model_.emplace(getContentProperty().Load<Graphics::Model>("ship"));
            effect_ = getContentProperty().Load<std::shared_ptr<Graphics::Effect>>("glow");
            song_.emplace(getContentProperty().Load<Media::Song>("theme"));
            video_.emplace(getContentProperty().Load<Media::Video>("intro"));
            Media::MediaPlayer::Play(&*song_);

            avatar_.emplace(GamerServices::AvatarDescription::CreateRandom());
            avatarRenderer_ = std::make_unique<GamerServices::AvatarRenderer>(&*avatar_);

            if (auto* microphone = Audio::Microphone::getDefaultProperty())
            {
                microphone->Start();
            }
            stream_ = std::make_unique<Audio::DynamicSoundEffectInstance>(44100, Audio::AudioChannels::Mono);
            stream_->Play();
        }

        void Update(GameTime& gameTime) override
        {
            GamerServices::Guide::ShowFriends(PlayerIndex::One);
            Game::Update(gameTime);
        }

    private:
        GraphicsDeviceManager graphics_;
        GamerServices::GamerServicesComponent gamerServices_{*this};
        std::optional<Graphics::Model> model_;
        std::shared_ptr<Graphics::Effect> effect_;
        std::optional<Media::Song> song_;
        std::optional<Media::Video> video_;
        std::optional<GamerServices::AvatarDescription> avatar_;
        std::unique_ptr<GamerServices::AvatarRenderer> avatarRenderer_;
        std::unique_ptr<Audio::DynamicSoundEffectInstance> stream_;
    };
}

int main(int argc, char** argv)
{
    (void)argv;
    if (argc < 2)
    {
        return 0;
    }
    FootprintFullGame game;
    game.Run();
    return 0;
}
