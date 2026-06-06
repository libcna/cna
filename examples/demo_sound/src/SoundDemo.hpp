#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

class SoundDemo : public Microsoft::Xna::Framework::Game {
public:
    SoundDemo();
    ~SoundDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    GetTypeNameHPP()

private:
    void DrawRect(int x, int y, int w, int h, Microsoft::Xna::Framework::Color color);
    void DrawBar(int x, int y, int totalW, int h, float fraction,
                 Microsoft::Xna::Framework::Color fillColor,
                 Microsoft::Xna::Framework::Color bgColor);

    bool WasJustPressed(Microsoft::Xna::Framework::Input::Keys key) const;

    void GenerateSineBuffer(std::vector<SharpRuntime::bytecs>& buffer,
                             float frequency, float durationSeconds,
                             int sampleRate, int channels);

    void OnDynamicBufferNeeded(System::Object* sender, const System::EventArgs& e);

private:
    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch_ = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D    pixel_;

    Microsoft::Xna::Framework::Audio::SoundEffect click_;
    Microsoft::Xna::Framework::Audio::SoundEffect tone_;
    Microsoft::Xna::Framework::Audio::SoundEffect beep_;

    std::unique_ptr<Microsoft::Xna::Framework::Audio::SoundEffectInstance> toneInstance_;
    std::unique_ptr<Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance> dynamicInstance_;

    Microsoft::Xna::Framework::Audio::AudioListener listener_;
    Microsoft::Xna::Framework::Audio::AudioEmitter  emitter_;

    Microsoft::Xna::Framework::Input::KeyboardState prevKeys_;

    float volume_     = 1.0f;
    float pitch_      = 0.0f;
    float pan_        = 0.0f;
    float emitterX_   = 0.0f;

    bool  dynamicPlaying_ = false;
    float dynamicFreq_    = 440.0f;

    std::string statusMessage_;
};
