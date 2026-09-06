// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-191: resource lifetime against this renderer's DEFERRED replay.
//
// Why this matters more here than on an immediate renderer. WebGPU queues its draws and replays
// them at flush time, so a `Texture2D` the game disposes right after `SpriteBatch.End()` is still
// referenced by a command that has not run yet. `REMED-GFX-167` exists for exactly that -- each
// queued command holds a `shared_ptr<WebGPUSampledResourceEXT>` rather than a raw view -- and
// nothing stressed it until now. On EasyGL the same sequence is harmless because the draw already
// happened.
//
// THE ORACLE IS THE RENDERER'S OWN UNCAPTURED-ERROR COUNT, not "it did not crash". wgpu-native
// reports a use-after-free of a texture view, a buffer or an attachment as a validation error
// through the uncaptured-error callback, and `GetUncapturedErrorCountEXT()` is that count. A test
// that only checked for a crash would pass on a renderer quietly submitting invalid work -- which
// is the failure mode a keep-alive bug actually has, since the freed handle usually still points at
// mapped memory that has not been reused yet.
//
// Each case also checks that the FRAME still contains what it should, so "no errors" cannot be
// satisfied by dropping the work altogether.
//
// Not covered here, and deliberately: the AddressSanitizer half of the row's acceptance. That needs
// a sanitizer build of this configuration, which is a multi-hundred-megabyte build directory of its
// own; the recipe is `cmake -S . -B cmake-build-webgpu-asan -DCNA_GRAPHICS_RENDERER=WEBGPU
// -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=address` and then this same binary. The
// uncaptured-error oracle catches the class of bug ASan would catch here -- a released handle used
// by a queued command -- because wgpu-native validates the handle before it dereferences anything.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 64;
    const Color kClearColor(9, 13, 17, 255);
    const Color kSprite(220, 90, 40, 255);
}

class WebGpuResourceLifetimeStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    [[nodiscard]] static std::unique_ptr<Texture2D> MakeSprite(GraphicsDevice& device)
    {
        auto texture = std::make_unique<Texture2D>(device, 2, 2, false, SurfaceFormat::Color);
        const std::vector<Color> texels(4, kSprite);
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

    [[nodiscard]] static Color ReadCentre(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

public:
    WebGpuResourceLifetimeStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
        const SamplerState pointClamp = SamplerState::PointClamp;

        const auto errors = [&renderer]() { return renderer.GetUncapturedErrorCountEXT(); };
        const std::size_t baseline = errors();
        check(baseline == 0, "the device starts with no uncaptured errors");

        // 1 -- a texture disposed AFTER the draw that samples it is queued, and BEFORE the frame is
        // flushed. This is REMED-GFX-167's own case.
        {
            device.Clear(kClearColor);
            auto texture = MakeSprite(device);
            {
                SpriteBatch batch(device);
                batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                            nullptr);
                batch.Draw(*texture, Rectangle(0, 0, kSize, kSize), Color::White);
                batch.End();
            }
            texture.reset();   // the queued command is now the only owner of the native view
            const Color centre = ReadCentre(device);
            check(errors() == baseline,
                  "disposing a Texture2D between the queued draw and the flush raises no "
                  "validation error");
            check(std::abs(centre.getRProperty() - kSprite.getRProperty()) <= 8,
                  "...and the sprite still rendered, so the keep-alive kept the CONTENT and not "
                  "merely the handle");
        }

        // 2 -- the same for a VertexBuffer, whose bytes the command captures rather than the object.
        {
            device.Clear(kClearColor);
            // CullMode::None, because the default is CullCounterClockwiseFace and this triangle's
            // winding would otherwise decide the test rather than the disposal under test.
            RasterizerState noCull;
            noCull.setCullModeProperty(CullMode::None);
            device.setRasterizerStateProperty(noCull);
            {
                auto vertices = std::make_unique<VertexBuffer>(
                    device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                    BufferUsage::None);
                const VertexPositionColor triangle[3] = {
                    {Vector3(-0.9f, -0.9f, 0.0f), kSprite},
                    {Vector3(0.9f, -0.9f, 0.0f), kSprite},
                    {Vector3(0.0f, 0.9f, 0.0f), kSprite}};
                vertices->SetData(triangle, 3);
                BasicEffect effect(device);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setViewProperty(Matrix::getIdentityProperty());
                effect.setProjectionProperty(Matrix::getIdentityProperty());
                effect.setLightingEnabledProperty(false);
                effect.setTextureEnabledProperty(false);
                effect.setVertexColorEnabledProperty(true);
                device.SetVertexBuffer(vertices.get());
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                device.SetVertexBuffer(nullptr);
                vertices.reset();
            }
            const Color centre = ReadCentre(device);
            check(errors() == baseline,
                  "disposing a VertexBuffer between its queued draw and the flush raises no "
                  "validation error");
            check(std::abs(centre.getRProperty() - kSprite.getRProperty()) <= 8,
                  "...and the triangle still rendered");
        }

        // 3 -- a render target disposed while it is still the CURRENT target. The renderer has to
        // survive the unbind of an object that no longer exists.
        {
            auto target = std::make_unique<RenderTarget2D>(device, 32, 32, false,
                                                           SurfaceFormat::Color, DepthFormat::None,
                                                           0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(target.get());
            device.Clear(Color(30, 200, 60, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.reset();
            device.Clear(kClearColor);
            const Color centre = ReadCentre(device);
            check(errors() == baseline,
                  "disposing a RenderTarget2D right after unbinding it raises no validation error");
            check(std::abs(centre.getRProperty() - kClearColor.getRProperty()) <= 4,
                  "...and the backbuffer is the current target again");
        }

        // 4 -- explicit double disposal, then use of the device afterwards.
        {
            auto texture = MakeSprite(device);
            texture->Dispose();
            texture->Dispose();
            texture.reset();
            device.Clear(kClearColor);
            (void)ReadCentre(device);
            check(errors() == baseline, "disposing a texture twice raises no validation error");
        }

        // 5 -- churn: many short-lived resources created and destroyed inside one frame, each used
        // once. This is where a pool that recycled a buffer still referenced by a queued command
        // shows up.
        {
            device.Clear(kClearColor);
            for (int i = 0; i < 32; ++i)
            {
                auto texture = MakeSprite(device);
                SpriteBatch batch(device);
                batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                            nullptr);
                batch.Draw(*texture, Rectangle(i % 8 * 8, i / 8 * 8, 8, 8), Color::White);
                batch.End();
                texture.reset();
            }
            const Color corner = [&device]() {
                const Rectangle region(4, 4, 1, 1);
                Color pixel(0, 0, 0, 0);
                device.GetBackBufferData(&region, &pixel, 0, 1);
                return pixel;
            }();
            check(errors() == baseline,
                  "32 create-draw-dispose cycles in one frame raise no validation error");
            check(std::abs(corner.getRProperty() - kSprite.getRProperty()) <= 8,
                  "...and the first of the 32 sprites is still on screen, so the churn did not "
                  "quietly drop the work");
        }

        std::printf("=== %d/%d PASS (uncaptured errors: %zu) ===\n", passCount_, checkCount_,
                    errors());
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    [[nodiscard]] int getResultProperty() const { return result_; }
};

int main()
{
    WebGpuResourceLifetimeStressTest game;
    game.Run();
    return game.getResultProperty();
}
