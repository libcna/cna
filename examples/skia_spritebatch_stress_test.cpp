// SPDX-License-Identifier: MS-PL
// SKIA-40: public 2D stress probe for repeated frames, batching, texture selection, and targets.
//
// A fixed twelve-texture/two-target fixture is created once. Each of 64 real Game::Draw frames
// performs 26 separate Begin/Draw/End blocks and exactly three target changes (A -> B ->
// backbuffer). The complete backbuffer is hashed after every frame and must retain the first
// frame's hash. Keeping the fixture lifetime outside the frame loop also makes any backend cache
// growth caused by this fixed workload observable to leak/resource-checking configurations.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetWidth = 48;
    constexpr int kTargetHeight = 32;
    constexpr int kBackbufferWidth = kTargetWidth * 2;
    constexpr int kFrames = 64;
    const std::array<Color, 12> kColours = {
        Color(255,   0,   0, 255), Color(  0, 255,   0, 255), Color(  0,   0, 255, 255),
        Color(255, 255,   0, 255), Color(255,   0, 255, 255), Color(  0, 255, 255, 255),
        Color(160,  80,   0, 255), Color(255, 128,   0, 255), Color(128,   0, 255, 255),
        Color(  0, 128, 128, 255), Color(128, 128, 128, 255), Color(255, 255, 255, 255),
    };

    [[nodiscard]] bool same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    [[nodiscard]] std::uint64_t hashPixels(const std::vector<Color>& pixels)
    {
        std::uint64_t hash = 1469598103934665603ULL; // FNV-1a 64-bit offset basis.
        for (const Color pixel : pixels)
        {
            const std::array<unsigned char, 4> channels = {
                pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(), pixel.getAProperty()
            };
            for (const unsigned char channel : channels)
            {
                hash ^= channel;
                hash *= 1099511628211ULL;
            }
        }
        return hash;
    }
}

class SkiaSpriteBatchStressTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::array<std::unique_ptr<Texture2D>, kColours.size()> textures_;
    std::unique_ptr<RenderTarget2D> targetA_;
    std::unique_ptr<RenderTarget2D> targetB_;
    std::uint64_t expectedHash_ = 0;
    int frame_ = 0;
    int failures_ = 0;

    void beginOpaquePoint()
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
    }

    void drawGrid(RenderTarget2D& target, bool reverseTextures)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        for (int slot = 0; slot < static_cast<int>(textures_.size()); ++slot)
        {
            const int textureIndex = reverseTextures
                ? static_cast<int>(textures_.size()) - 1 - slot
                : slot;
            const Rectangle cell((slot % 4) * 12, (slot / 4) * 11, 12, 11);
            beginOpaquePoint();
            spriteBatch_->Draw(*textures_[static_cast<std::size_t>(textureIndex)], cell,
                               Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();
        }
    }

    void verifyFrame()
    {
        auto& device = getGraphicsDeviceProperty();
        std::vector<Color> pixels(kBackbufferWidth * kTargetHeight, Color(0, 0, 0, 0));
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        const std::uint64_t frameHash = hashPixels(pixels);
        const Color aTopLeft = pixels[5 * kBackbufferWidth + 6];
        const Color bTopLeft = pixels[5 * kBackbufferWidth + kTargetWidth + 6];
        if (!same(aTopLeft, kColours.front()))
        {
            std::printf("[FAIL] target A sample in frame %d is not the first texture\n", frame_ + 1);
            ++failures_;
        }
        if (!same(bTopLeft, kColours.back()))
        {
            std::printf("[FAIL] target B sample in frame %d is not the reversed first texture\n", frame_ + 1);
            ++failures_;
        }
        if (frame_ == 0)
        {
            expectedHash_ = frameHash;
            std::printf("[PASS] baseline frame hash: 0x%016llx\n",
                        static_cast<unsigned long long>(expectedHash_));
        }
        else if (frameHash != expectedHash_)
        {
            std::printf("[FAIL] frame %d hash changed: got=0x%016llx want=0x%016llx\n", frame_ + 1,
                        static_cast<unsigned long long>(frameHash),
                        static_cast<unsigned long long>(expectedHash_));
            ++failures_;
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        for (std::size_t i = 0; i < textures_.size(); ++i)
        {
            textures_[i] = std::make_unique<Texture2D>(device, 1, 1);
            textures_[i]->SetData(&kColours[i], 1);
        }
        targetA_ = std::make_unique<RenderTarget2D>(device, kTargetWidth, kTargetHeight, false,
                                                     SurfaceFormat::Color, DepthFormat::None, 0,
                                                     RenderTargetUsage::PreserveContents);
        targetB_ = std::make_unique<RenderTarget2D>(device, kTargetWidth, kTargetHeight, false,
                                                     SurfaceFormat::Color, DepthFormat::None, 0,
                                                     RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        drawGrid(*targetA_, false);
        drawGrid(*targetB_, true);

        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(nullptr);
        device.Clear(Color(0, 0, 0, 255));
        beginOpaquePoint();
        spriteBatch_->Draw(*targetA_, Rectangle(0, 0, kTargetWidth, kTargetHeight),
                           Rectangle(0, 0, kTargetWidth, kTargetHeight), Color::White);
        spriteBatch_->End();
        beginOpaquePoint();
        spriteBatch_->Draw(*targetB_, Rectangle(kTargetWidth, 0, kTargetWidth, kTargetHeight),
                           Rectangle(0, 0, kTargetWidth, kTargetHeight), Color::White);
        spriteBatch_->End();

        verifyFrame();
        ++frame_;
        if (frame_ == kFrames)
        {
            if (failures_ == 0)
                std::printf("[PASS] %d stable frames: 1,664 Begin/Draw/End blocks, 192 target changes\n",
                            kFrames);
            Exit();
        }
    }

public:
    SkiaSpriteBatchStressTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kBackbufferWidth);
        graphics_->setPreferredBackBufferHeightProperty(kTargetHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    SkiaSpriteBatchStressTest game;
    game.Run();
    return game.Result();
}
