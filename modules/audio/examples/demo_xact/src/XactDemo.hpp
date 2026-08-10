#pragma once

#include <memory>
#include <optional>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

class XactDemo : public Microsoft::Xna::Framework::Game
{
public:
    XactDemo();
    ~XactDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    GetTypeNameHPP()

private:
    void GenerateXactFiles(const std::string& audioDir);
    void DrawRect(int x, int y, int w, int h, Microsoft::Xna::Framework::Color color);
    void DrawBar(int x, int y, int totalW, int h, float frac,
                 Microsoft::Xna::Framework::Color fill,
                 Microsoft::Xna::Framework::Color bg);
    bool WasJustPressed(Microsoft::Xna::Framework::Input::Keys key) const;

    // Rendering
    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch_ = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D    pixel_;

    // XACT objects
    std::unique_ptr<Microsoft::Xna::Framework::Audio::AudioEngine> audioEngine_;
    std::unique_ptr<Microsoft::Xna::Framework::Audio::WaveBank>    waveBank_;
    std::unique_ptr<Microsoft::Xna::Framework::Audio::SoundBank>   soundBank_;

    std::optional<Microsoft::Xna::Framework::Audio::AudioCategory> musicCategory_;
    std::optional<Microsoft::Xna::Framework::Audio::AudioCategory> sfxCategory_;

    // Runtime state
    float musicVolume_ = 1.0f;
    float sfxVolume_   = 1.0f;
    bool  musicPaused_ = false;
    bool  sfxPaused_   = false;
    float speedOfSound_ = 343.0f;

    // Which cue buttons were played (for visual feedback)
    bool cuePlayed_[4] = {};
    int  cuePlayFrames_[4] = {};

    std::string status_;

    Microsoft::Xna::Framework::Input::KeyboardState prevKeys_;
};
