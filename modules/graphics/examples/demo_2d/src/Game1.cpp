#include "Game1.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "CNA/GraphicsRendererType.hpp"
#include "System/TimeSpan.hpp"

Game1::Game1()
    : playerTexture(),
      flySound("")
{
    backgroundR = randFloat01();
    backgroundG = randFloat01();
    backgroundB = randFloat01();

    backgroundTargetR = randFloat01();
    backgroundTargetG = randFloat01();
    backgroundTargetB = randFloat01();

    // This demo is deliberately a demanding GPU scene by default (50–100 large, rotating,
    // alpha-blended sprites).  GDI is a CPU rasterizer with GDI presentation, so using that same
    // load at 60 FPS consumes a whole core without providing a more useful visual demonstration.
    // Keep a lively, representative scene for the GDI visual gate while respecting the renderer's
    // intended modest-workload scope.  Other renderers retain the existing 60 FPS stress profile.
    const bool gdiCompatibilityProfile =
        CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::Gdi;
    if (gdiCompatibilityProfile)
    {
        minFlyers = 12;
        maxFlyers = 20;
    }

    const double targetFramesPerSecond = gdiCompatibilityProfile ? 30.0 : 60.0;
    System::TimeSpan targetElapsedTime = System::TimeSpan(
        System::TimeSpan::FromMilliseconds(1000.0 / targetFramesPerSecond).getTicksProperty());
    Game::setTargetElapsedTimeProperty(targetElapsedTime);
    Game::getWindowProperty().setTitleProperty(
        gdiCompatibilityProfile ? "CNA 2D Demo - GDI compatibility profile" : "CNA 2D Demo");
}

Game1::~Game1()
{
    delete spriteBatch;
    spriteBatch = nullptr;
}

int Game1::randint()
{
    return rand();
}

int Game1::randint(int max)
{
    if (max <= 0) {
        return 0;
    }
    return randint() % max;
}

float Game1::randFloat01()
{
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

float Game1::randFloat(float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * randFloat01();
}

void Game1::LoadContent()
{
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;

    if (spriteBatch == nullptr) {
        spriteBatch = new SpriteBatch(getGraphicsDeviceProperty());
    }

    playerTexture = getContentProperty().Load<Texture2D>("images/player.png");

    if (!webGpu2DValidation_)
    {
        try {
            flySound = getContentProperty().Load<SoundEffect>("sounds/test.wav");
        } catch (...) {
            // Audio might fail in some environments, ignore for graphics demo
        }

        flySoundInstance = std::make_unique<SoundEffectInstance>(flySound.CreateInstance());

        for (int i = 0; i < minFlyers; ++i) {
            SpawnFlyer();
        }
    }
    else
    {
        getWindowProperty().setTitleProperty("CNA 2D Demo - WebGPU 2D validation");
    }
}

void Game1::SpawnFlyer()
{
    FlyerInstance flyer{};

    flyer.x = randFloat(0.0f, 700.0f);
    flyer.y = randFloat(0.0f, 500.0f);

    const float angle = randFloat(0.0f, 6.283185307f);
    const float baseSpeed = randFloat(80.0f, 220.0f);

    flyer.vx = std::cos(angle) * baseSpeed;
    flyer.vy = std::sin(angle) * baseSpeed;

    flyer.lifetimeSeconds = randFloat(40.0f, 120.0f);
    flyer.ageSeconds = 0.0f;

    flyer.speedMultiplier = randFloat(0.7f, 1.5f);
    flyer.phase = randFloat(0.0f, 6.283185307f);
    flyer.phaseSpeed = randFloat(0.7f, 2.4f);
    flyer.size = randFloat(70.0f, 130.0f);

    flyer.rotation = randFloat(0.0f, 6.28f);
    flyer.rotationSpeed = randFloat(-2.0f, 2.0f);
    flyer.pulseSpeed = randFloat(1.0f, 3.0f);
    flyer.useSourceRect = (randint(10) < 3); // 30% chance

    flyers.push_back(flyer);
}

float Game1::GetDynamicSpeedFactor(const FlyerInstance& flyer) const
{
    const float wave = std::sin(flyer.phase) * 0.35f;
    return std::max(0.2f, flyer.speedMultiplier + wave);
}

void Game1::KeepFlyerInsideBounds(FlyerInstance& flyer)
{
    constexpr float screenWidth = 800.0f;
    constexpr float screenHeight = 600.0f;

    if (flyer.x < 0.0f) {
        flyer.x = 0.0f;
        flyer.vx = -flyer.vx;
    }
    if (flyer.x > screenWidth - flyer.size) {
        flyer.x = screenWidth - flyer.size;
        flyer.vx = -flyer.vx;
    }

    if (flyer.y < 0.0f) {
        flyer.y = 0.0f;
        flyer.vy = -flyer.vy;
    }
    if (flyer.y > screenHeight - flyer.size) {
        flyer.y = screenHeight - flyer.size;
        flyer.vy = -flyer.vy;
    }
}

void Game1::UpdateBackground(float deltaTime)
{
    backgroundChangeTimer -= deltaTime;

    if (backgroundChangeTimer <= 0.0f) {
        backgroundTargetR = randFloat01();
        backgroundTargetG = randFloat01();
        backgroundTargetB = randFloat01();
        backgroundChangeTimer = randFloat(2.0f, 6.0f);
    }

    const float lerpFactor = std::min(1.0f, deltaTime * 0.5f);

    backgroundR += (backgroundTargetR - backgroundR) * lerpFactor;
    backgroundG += (backgroundTargetG - backgroundG) * lerpFactor;
    backgroundB += (backgroundTargetB - backgroundB) * lerpFactor;
}

void Game1::UpdateFlyers(float deltaTime)
{
    for (auto& flyer : flyers) {
        flyer.ageSeconds += deltaTime;
        flyer.phase += flyer.phaseSpeed * deltaTime;
        flyer.rotation += flyer.rotationSpeed * deltaTime;
        flyer.scale = 1.0f + 0.2f * std::sin(flyer.ageSeconds * flyer.pulseSpeed);

        const float dynamicSpeed = GetDynamicSpeedFactor(flyer);

        flyer.x += flyer.vx * dynamicSpeed * deltaTime;
        flyer.y += flyer.vy * dynamicSpeed * deltaTime;

        KeepFlyerInsideBounds(flyer);
    }

    flyers.erase(
        std::remove_if(
            flyers.begin(),
            flyers.end(),
            [](const FlyerInstance& flyer) {
                return flyer.ageSeconds >= flyer.lifetimeSeconds;
            }
        ),
        flyers.end()
    );

    while (static_cast<int>(flyers.size()) < minFlyers) {
        SpawnFlyer();
    }

    if (static_cast<int>(flyers.size()) < maxFlyers) {
        const float spawnChancePerFrame = 0.01f;
        if (randFloat01() < spawnChancePerFrame) {
            SpawnFlyer();
        }
    }
}

void Game1::Update(Microsoft::Xna::Framework::GameTime& gameTime)
{
    // A portable, useful default for every interactive build.  On a terminal this is also the
    // end-to-end path exercised by the pseudo-TTY integration test.
    if (Microsoft::Xna::Framework::Input::Keyboard::GetState().IsKeyDown(
            Microsoft::Xna::Framework::Input::Keys::Escape))
    {
        Exit();
        return;
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0) { Exit(); return; }
    }

    if (webGpu2DValidation_)
    {
        ++validationFrame_;
        auto& window = getWindowProperty();
        if (validationFrame_ == 20)
            window.EndScreenDeviceChange(window.getScreenDeviceNameProperty(), 960, 540);
        if (validationFrame_ == 45)
            window.MinimizeEXT();
        if (validationFrame_ == 70)
        {
            window.RestoreEXT();
            window.EndScreenDeviceChange(window.getScreenDeviceNameProperty(), 800, 600);
        }
        return;
    }

    static bool played = false;
    if (!played && flySoundInstance) {
        flySoundInstance->Play();
        played = true;
    }

    const float deltaTime =
        static_cast<float>(
            gameTime.getElapsedGameTimeProperty().getTotalMillisecondsProperty()
        ) / 1000.0f;

    UpdateBackground(deltaTime);
    UpdateFlyers(deltaTime);
}

void Game1::DrawWebGpu2DValidationScene()
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    const Rectangle bounds = playerTexture.getBoundsProperty();
    const Rectangle upperLeft{0, 0, bounds.Width / 2, bounds.Height / 2};
    // Deliberately extends beyond the right edge: the clamp, wrap and mirror batches below make
    // the sampler address modes visibly distinct while exercising UVs outside [0, 1].
    const Rectangle repeated{0, 0, bounds.Width * 2, bounds.Height};

    spriteBatch->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                       const_cast<SamplerState*>(&SamplerState::LinearClamp), nullptr, nullptr);
    spriteBatch->Draw(playerTexture, Rectangle{40, 45, 180, 140}, upperLeft, Color::White);
    spriteBatch->Draw(playerTexture, Rectangle{300, 110, 180, 140}, upperLeft,
                      Color{255, 90, 60, 150}, 0.55f,
                      Vector2{static_cast<float>(upperLeft.Width) / 2.0f, static_cast<float>(upperLeft.Height) / 2.0f},
                      SpriteEffects::None, 0.0f);
    spriteBatch->Draw(playerTexture, Rectangle{530, 45, 180, 140}, upperLeft, Color{80, 210, 255, 255},
                      0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
    spriteBatch->Draw(playerTexture, Rectangle{530, 215, 180, 140}, upperLeft, Color{160, 255, 100, 255},
                      0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);
    spriteBatch->Draw(playerTexture, Rectangle{40, 390, 210, 130}, repeated, Color::White);
    spriteBatch->End();

    spriteBatch->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                       const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
    spriteBatch->Draw(playerTexture, Rectangle{280, 390, 210, 130}, repeated, Color::White);
    spriteBatch->End();

    spriteBatch->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                       const_cast<SamplerState*>(&SamplerState::LinearWrap), nullptr, nullptr);
    spriteBatch->Draw(playerTexture, Rectangle{520, 390, 210, 130}, repeated, Color::White);
    spriteBatch->End();

    SamplerState linearMirror;
    linearMirror.setAddressUProperty(TextureAddressMode::Mirror);
    linearMirror.setAddressVProperty(TextureAddressMode::Mirror);
    spriteBatch->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &linearMirror, nullptr, nullptr);
    spriteBatch->Draw(playerTexture, Rectangle{280, 535, 210, 55}, repeated, Color::White);
    spriteBatch->End();
}

void Game1::Draw(const Microsoft::Xna::Framework::GameTime& gameTime)
{
    (void)gameTime;

    getGraphicsDeviceProperty().Clear(backgroundR, backgroundG, backgroundB, 1.0f);

    if (webGpu2DValidation_)
    {
        DrawWebGpu2DValidationScene();
        return;
    }

    spriteBatch->Begin();

    for (const auto& flyer : flyers) {
        Microsoft::Xna::Framework::Rectangle sourceRect;
        if (flyer.useSourceRect) {
            sourceRect = Microsoft::Xna::Framework::Rectangle(0, 0, 50, 50);
        } else {
            auto bounds = playerTexture.getBoundsProperty();
            sourceRect = Microsoft::Xna::Framework::Rectangle(0, 0, bounds.Width, bounds.Height);
        }

        float displayWidth = flyer.size * flyer.scale;
        float displayHeight = flyer.size * flyer.scale;

        Microsoft::Xna::Framework::Rectangle destRect(
            static_cast<int>(flyer.x),
            static_cast<int>(flyer.y),
            static_cast<int>(displayWidth),
            static_cast<int>(displayHeight)
        );

        Microsoft::Xna::Framework::Vector2 origin(
            static_cast<float>(sourceRect.Width) / 2.0f,
            static_cast<float>(sourceRect.Height) / 2.0f
        );

        spriteBatch->Draw(
            playerTexture,
            destRect,
            sourceRect,
            Microsoft::Xna::Framework::Color(255, 255, 255, 255),
            flyer.rotation,
            origin,
            Microsoft::Xna::Framework::Graphics::SpriteEffects::None,
            0.0f
        );
    }

    spriteBatch->End();
}

GetTypeNameCPP(Game1, "Game1")
