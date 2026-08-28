// SPDX-License-Identifier: MS-PL
// WEBGPU-12/59: every per-draw vertex/uniform buffer used to be created fresh and released every
// draw, every frame (churn); now they are acquired from a bounded transient pool and recycled after
// submit, and the SpriteBatch dynamic vertex buffer is a 3-slot ring. This stress test renders a
// fixed scene (several colour 3D quads + several sprites) for many frames and proves the pool is
// bounded and reused.
//
// Check A -- after a warm-up, the transient-buffer CREATE count stops climbing entirely across the
//   remaining frames: a repeating scene allocates nothing new (the whole point of the pool).
// Check B -- the transient-buffer REUSE count keeps climbing over those same frames: draws are
//   genuinely served from the pool, not bypassing it.
// Check C -- zero uncaptured WebGPU validation/device errors across the whole run: recycling a
//   buffer and rewriting it in a later frame never races the GPU (queue-FIFO ordering makes it safe).
// Check D -- the scene still renders (a sampled pixel is not the clear colour), so pooling did not
//   silently break drawing.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

class WebGpuBufferPoolStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    BasicEffect* fx_ = nullptr;
    int frame_ = 0;
    std::size_t createAtWarmup_ = 0;
    std::size_t reuseAtWarmup_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    static constexpr int kWarmupFrame = 6;    ///< By here every pipeline/buffer class has been seen.
    static constexpr int kTotalFrames = 40;
    static constexpr int kQuadsPerFrame = 8;
    static constexpr int kSpritesPerFrame = 6;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void DrawColorQuad(GraphicsDevice& dev, float cx, const Color& c)
    {
        const float x0 = cx - 0.1f, x1 = cx + 0.1f;
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  0.4f, 0.3f), c }, { Vector3(x0, -0.4f, 0.3f), c },
            { Vector3(x1, -0.4f, 0.3f), c }, { Vector3(x0,  0.4f, 0.3f), c },
            { Vector3(x1, -0.4f, 0.3f), c }, { Vector3(x1,  0.4f, 0.3f), c },
        };
        fx_->VertexColorEnabled = true;
        fx_->setLightingEnabledProperty(false);
        fx_->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                std::vector<std::uint8_t>{255, 255, 255, 255});
        fx_ = new BasicEffect(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());
        if (frame_ >= kTotalFrames)
            return;

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color::Black);

        // A fixed set of colour 3D quads (each draw pools a vertex + uniform buffer).
        for (int q = 0; q < kQuadsPerFrame; ++q)
        {
            const float cx = -0.8f + 0.2f * static_cast<float>(q);
            DrawColorQuad(dev, cx, Color(20 + q * 25, 40, 200 - q * 10, 255));
        }

        // A fixed set of sprites (rotates the sprite vertex ring; pools their uniforms).
        {
            SpriteBatch batch(dev);
            SamplerState point = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            for (int s = 0; s < kSpritesPerFrame; ++s)
                batch.Draw(whiteTex_, Rectangle(4 + s * 8, 4, 6, 6), Rectangle(0, 0, 1, 1),
                           Color(200, 100 + s * 10, 50, 255));
            batch.End();
        }

        if (frame_ == kWarmupFrame)
        {
            createAtWarmup_ = renderer.GetTransientBufferCreateCountEXT();
            reuseAtWarmup_ = renderer.GetTransientBufferReuseCountEXT();
        }

        ++frame_;
        if (frame_ == kTotalFrames)
        {
            const std::size_t createEnd = renderer.GetTransientBufferCreateCountEXT();
            const std::size_t reuseEnd = renderer.GetTransientBufferReuseCountEXT();
            const std::size_t uncaptured = renderer.GetUncapturedErrorCountEXT();

            check(createEnd == createAtWarmup_,
                  "Check A: transient-buffer create count plateaus after warm-up (warmup="
                  + std::to_string(createAtWarmup_) + " end=" + std::to_string(createEnd) + ")");
            check(reuseEnd > reuseAtWarmup_ + static_cast<std::size_t>(kTotalFrames - kWarmupFrame),
                  "Check B: transient-buffer reuse count keeps climbing (warmup="
                  + std::to_string(reuseAtWarmup_) + " end=" + std::to_string(reuseEnd) + ")");
            check(uncaptured == 0,
                  "Check C: zero uncaptured WebGPU errors across the run ("
                  + std::to_string(uncaptured) + ")");

            Color center(0, 0, 0, 0);
            const Rectangle region(32, 24, 1, 1);
            dev.GetBackBufferData(&region, &center, 0, 1);
            check(center.getRProperty() + center.getGProperty() + center.getBProperty() > 0,
                  "Check D: the scene still renders (centre pixel is not the clear colour): ("
                  + std::to_string(center.getRProperty()) + "," + std::to_string(center.getGProperty())
                  + "," + std::to_string(center.getBProperty()) + ")");

            std::printf("[INFO] pool: created=%zu reused=%zu over %d frames\n",
                        createEnd, reuseEnd, kTotalFrames);
            std::printf("=== %d/4 PASS ===\n", passCount_);
            result_ = (passCount_ == 4) ? 0 : 1;
            Exit();
        }
    }

public:
    WebGpuBufferPoolStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(48);
    }

    ~WebGpuBufferPoolStressTest() override { delete fx_; }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuBufferPoolStressTest game;
    game.Run();
    return game.getResult();
}
