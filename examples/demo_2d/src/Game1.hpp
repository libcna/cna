#pragma once

#include <memory>
#include <vector>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

class Game1 : public Microsoft::Xna::Framework::Game {
public:
    Game1();
    ~Game1() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    GetTypeNameHPP()

private:
    struct FlyerInstance {
        float x = 0.0f;
        float y = 0.0f;

        float vx = 0.0f;
        float vy = 0.0f;

        float lifetimeSeconds = 0.0f;
        float ageSeconds = 0.0f;

        float speedMultiplier = 1.0f;
        float phase = 0.0f;
        float phaseSpeed = 0.0f;
        float size = 100.0f;

        float rotation = 0.0f;
        float rotationSpeed = 0.0f;
        float scale = 1.0f;
        float pulseSpeed = 0.0f;
        bool useSourceRect = false;
    };

private:
    int randint();
    int randint(int max);
    float randFloat01();
    float randFloat(float minValue, float maxValue);

    void SpawnFlyer();
    void UpdateBackground(float deltaTime);
    void UpdateFlyers(float deltaTime);
    void KeepFlyerInsideBounds(FlyerInstance& flyer);
    float GetDynamicSpeedFactor(const FlyerInstance& flyer) const;

private:
    Microsoft::Xna::Framework::Graphics::Texture2D playerTexture;
    Microsoft::Xna::Framework::Audio::SoundEffect flySound;
    std::unique_ptr<Microsoft::Xna::Framework::Audio::SoundEffectInstance> flySoundInstance;

    std::vector<FlyerInstance> flyers;

    float backgroundR = 0.1f;
    float backgroundG = 0.1f;
    float backgroundB = 0.1f;

    float backgroundTargetR = 0.1f;
    float backgroundTargetG = 0.1f;
    float backgroundTargetB = 0.1f;

    float backgroundChangeTimer = 0.0f;

    int minFlyers = 50;
    int maxFlyers = 100;

protected:
    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch = nullptr;
};
