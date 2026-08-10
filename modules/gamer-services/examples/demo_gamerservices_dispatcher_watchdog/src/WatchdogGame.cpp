#include "WatchdogGame.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/IServiceProvider.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Net;

namespace
{
    std::unique_ptr<SpriteFont> MakeSimpleFont(GraphicsDevice& device)
    {
        const std::vector<uint8_t> px = {255, 255, 255, 255};
        Texture2D atlas = Texture2D::CreateFromPixels(device, 1, 1, px);

        std::vector<SharpRuntime::charcs> chars;
        std::vector<Rectangle> bounds;
        std::vector<Rectangle> cropping;
        std::vector<Vector3> kerning;
        for (char c = 32; c < 127; ++c)
        {
            chars.push_back(static_cast<SharpRuntime::charcs>(c));
            bounds.push_back(Rectangle(0, 0, 1, 1));
            cropping.push_back(Rectangle(0, 0, 8, 14));
            kerning.push_back(Vector3(0.0f, 8.0f, 0.0f));
        }

        return std::make_unique<SpriteFont>(atlas, bounds, cropping, chars, 16, 1.0f, kerning,
                                             static_cast<SharpRuntime::charcs>(' '));
    }

    // GamerServicesDispatcher::Initialize() never dereferences its serviceProvider argument
    // (mirrors tools/net/gamerservices_dispatcher_harness.cpp's own NullServiceProvider).
    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };

    double MeasureMs(const std::function<void()>& fn)
    {
        const auto start = std::chrono::steady_clock::now();
        fn();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
}

WatchdogGame::WatchdogGame() = default;

void WatchdogGame::Initialize()
{
    Game::Initialize();
}

void WatchdogGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void WatchdogGame::AdvanceStateMachine()
{
    switch (step_)
    {
        case Step::WarmupInitialize:
            ++framesInStep_;
            if (framesInStep_ >= kWarmupFrames) { step_ = Step::RunInitialize; }
            break;

        case Step::RunInitialize:
        {
            NullServiceProvider services;
            initializeResult_.elapsedMs = MeasureMs([&]() {
                GamerServicesDispatcher::Initialize(services);
            });
            initializeResult_.done = true;
            std::printf("[Watchdog] GamerServicesDispatcher::Initialize() completed in %.2fms "
                        "(Task 12.1's historical hang would have meant this line never prints).\n",
                        initializeResult_.elapsedMs);
            step_ = Step::WarmupCreate;
            framesInStep_ = 0;
            break;
        }

        case Step::WarmupCreate:
            ++framesInStep_;
            if (framesInStep_ >= kWarmupFrames) { step_ = Step::RunCreate; }
            break;

        case Step::RunCreate:
        {
            NetworkSession* session = nullptr;
            createResult_.elapsedMs = MeasureMs([&]() {
                session = NetworkSession::Create(NetworkSessionType::Local, 1, 8);
            });
            createResult_.done = true;
            std::printf("[Watchdog] NetworkSession::Create() completed in %.2fms (Task 12.1's "
                        "historical hang would have meant this line never prints).\n",
                        createResult_.elapsedMs);
            if (session != nullptr)
            {
                session->Dispose();
            }
            step_ = Step::WarmupAchievements;
            framesInStep_ = 0;
            break;
        }

        case Step::WarmupAchievements:
            ++framesInStep_;
            if (framesInStep_ >= kWarmupFrames) { step_ = Step::RunAchievements; }
            break;

        case Step::RunAchievements:
        {
            SignedInGamer* gamer = (*Gamer::getSignedInGamersProperty())[0];
            achievementsResult_.elapsedMs = MeasureMs([&]() {
                [[maybe_unused]] auto unused = gamer->GetAchievements();
            });
            achievementsResult_.done = true;
            std::printf("[Watchdog] SignedInGamer::GetAchievements() completed in %.2fms (Task "
                        "7.1's historical hang would have meant this line never prints).\n",
                        achievementsResult_.elapsedMs);
            step_ = Step::AllDone;
            framesInStep_ = 0;
            break;
        }

        case Step::AllDone:
            // Exit() doesn't halt Update() immediately (several more frames may still run before
            // the game loop actually stops) - guard so this only fires once.
            if (doneGraceFrames_ < 60)
            {
                ++doneGraceFrames_;
                if (doneGraceFrames_ >= 60)
                {
                    std::printf("[Watchdog] All 3 checks completed without hanging. Exiting.\n");
                    Exit();
                }
            }
            break;
    }
}

void WatchdogGame::Update(GameTime& /*gameTime*/)
{
    AdvanceStateMachine();
}

void WatchdogGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();
    spriteBatch_->DrawString(*font_, "GamerServicesDispatcher Watchdog", Vector2(16.0f, 16.0f),
                              Color(255, 255, 255, 255));

    auto drawLine = [&](float y, const char* label, bool waitingNow, const StepResult& result) {
        char line[160];
        if (result.done)
        {
            std::snprintf(line, sizeof(line), "%-45s SUCCESS (%.2fms)", label, result.elapsedMs);
        }
        else if (waitingNow)
        {
            std::snprintf(line, sizeof(line), "%-45s waiting...", label);
        }
        else
        {
            std::snprintf(line, sizeof(line), "%-45s (pending)", label);
        }
        const Color color = result.done ? Color(120, 220, 120, 255)
                                         : (waitingNow ? Color(240, 220, 80, 255) : Color(120, 120, 120, 255));
        spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), color);
    };

    drawLine(48.0f, "GamerServicesDispatcher::Initialize()",
             step_ == Step::WarmupInitialize || step_ == Step::RunInitialize, initializeResult_);
    drawLine(68.0f, "NetworkSession::Create()",
             step_ == Step::WarmupCreate || step_ == Step::RunCreate, createResult_);
    drawLine(88.0f, "SignedInGamer::GetAchievements()",
             step_ == Step::WarmupAchievements || step_ == Step::RunAchievements, achievementsResult_);

    if (step_ == Step::AllDone)
    {
        spriteBatch_->DrawString(*font_, "ALL SUCCESS - no hangs detected", Vector2(16.0f, 116.0f),
                                  Color(120, 220, 120, 255));
    }

    spriteBatch_->End();
}
