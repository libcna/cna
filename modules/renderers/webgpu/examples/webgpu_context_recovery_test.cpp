// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-181: the CPU-side half of the device-loss contract, which can be
// proven without a real device destroy.
//
// Three claims, and the second is the one with teeth.
//
// A -- `CanBeginDrawEXT()` is true on a healthy device. That is the baseline the gate is measured
//   against, and on this renderer the gate matters more than on any other: `WEBGPU-180` measured
//   that `wgpuSurfaceGetCurrentTexture` on a surface whose device is lost does not return a failure
//   status but panics inside wgpu-native and aborts the process. There is nothing to catch, so
//   `CanBeginDrawEXT()` returning false is the only thing that can stop the acquire.
//
// B -- with context recovery enabled (the default), the renderer holds the SAME allocation the
//   framework does. Not an equal buffer -- the same pointer. That is what makes recovery free: the
//   whole cost is one reference count, and a test that compared CONTENTS could not tell a shared
//   buffer from a duplicated one, which is exactly the mistake worth catching here.
//
// C -- with recovery disabled before the texture is created, the renderer holds nothing, and
//   turning it back on makes the next texture share again. Disabling is forward-looking by design
//   (see `SetContextRecoveryEnabled`), so this checks the contract the interface actually states
//   rather than a stronger one it does not.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
using CNA::Internal::Renderers::WebGPU::WebGPUTextureRenderer;

namespace
{
    constexpr int kSize = 64;
    const Color kClearColor(9, 13, 17, 255);
    const Color kSprite(220, 90, 40, 255);
}

class WebGpuContextRecoveryTest : public Game
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

    /// Creates a filled 4x4 texture and reports the two pointers that must, or must not, agree.
    struct SharedPointers { const void* framework; const void* renderer; };

    [[nodiscard]] static std::unique_ptr<Texture2D> MakeSprite(GraphicsDevice& device)
    {
        auto texture = std::make_unique<Texture2D>(device, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> texels{kSprite, kSprite, kSprite, kSprite};
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

    [[nodiscard]] static SharedPointers MakeTextureAndCompare(GraphicsDevice& device)
    {
        Texture2D texture(device, 4, 4, false, SurfaceFormat::Color);
        std::array<Color, 16> texels{};
        texels.fill(Color(200, 100, 50, 255));
        texture.SetData(texels.data(), static_cast<int>(texels.size()));

        const auto frameworkPixels = texture.GetCpuPixelsWeak().lock();
        auto& textureRenderer = static_cast<WebGPUTextureRenderer&>(texture.GetRenderer());
        return SharedPointers{frameworkPixels.get(),
                              textureRenderer.SharedCpuPixelsEXT().get()};
    }

public:
    WebGpuContextRecoveryTest()
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

        // A -- the gate's baseline.
        check(renderer.CanBeginDrawEXT(),
              "a healthy device reports CanBeginDrawEXT() true");

        // B -- shared, not copied.
        check(renderer.IsContextRecoveryEnabledEXT(),
              "context recovery is ON by default");
        const SharedPointers shared = MakeTextureAndCompare(device);
        check(shared.framework != nullptr,
              "the framework kept CPU pixels for the texture");
        check(shared.renderer == shared.framework,
              "the renderer holds the SAME allocation the framework does -- shared by pointer, "
              "not copied, so recovery costs one reference count and nothing else");

        // C -- disabling is forward-looking.
        device.SetContextRecoveryEnabled(false);
        check(!renderer.IsContextRecoveryEnabledEXT(),
              "SetContextRecoveryEnabled(false) reaches the renderer");
        const SharedPointers withoutRecovery = MakeTextureAndCompare(device);
        // MEASURED, and it corrects what this test first asserted: with recovery off the FRAMEWORK
        // frees its own shadow too (Texture2D::MaybeFreeCpuPixels -- "saving ~1x texture RAM"),
        // which is the whole point of the switch. So the renderer holding nothing is a consequence
        // of there being nothing to hold, not an independent second saving; claiming otherwise
        // would have described a memory win this renderer does not separately provide.
        check(withoutRecovery.framework == nullptr,
              "with recovery off the FRAMEWORK frees its own shadow -- that is where the ~1x "
              "texture RAM saving comes from");
        check(withoutRecovery.renderer == nullptr,
              "and the renderer therefore holds nothing either");

        device.SetContextRecoveryEnabled(true);
        const SharedPointers reEnabled = MakeTextureAndCompare(device);
        check(reEnabled.renderer == reEnabled.framework && reEnabled.renderer != nullptr,
              "turning it back on makes the NEXT texture share again");

        // --- D -- WEBGPU-182: a real destroy and recreate ------------------------------------
        // Nothing between the loss and the restore may touch the device: WEBGPU-180 measured that
        // wgpuSurfaceGetCurrentTexture on a lost device aborts the process rather than returning a
        // status, which is exactly what CanBeginDrawEXT() exists to stop a caller from reaching.
        int lost = 0, resetting = 0, reset = 0;
        device.DeviceLost += [&lost](System::Object*, const System::EventArgs&) { ++lost; };
        device.DeviceResetting += [&resetting](System::Object*, const System::EventArgs&) {
            ++resetting;
        };
        device.DeviceReset += [&reset](System::Object*, const System::EventArgs&) { ++reset; };

        renderer.DebugSimulateContextLoss();
        check(!renderer.CanBeginDrawEXT(),
              "after a simulated loss CanBeginDrawEXT() reports false -- the gate that stands "
              "between a lost device and the process abort WEBGPU-180 measured");
        check(lost == 1, "the loss raised GraphicsDevice::DeviceLost exactly once");

        renderer.DebugRestoreContext();
        check(renderer.CanBeginDrawEXT(), "after a restore the device is usable again");
        check(resetting == 1 && reset == 1,
              "the restore raised DeviceResetting and DeviceReset exactly once each");

        // The device really works afterwards, not merely reports that it does: a clear, a fresh
        // texture, a sprite draw and a readback all on the NEW device.
        {
            device.Clear(kClearColor);
            auto texture = MakeSprite(device);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(*texture, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            const Color centre = ReadCentre(device);
            check(std::abs(centre.getRProperty() - kSprite.getRProperty()) <= 8,
                  "a texture created and drawn AFTER the recreate renders correctly on the new "
                  "device");
            check(errors() == baseline,
                  "and the whole loss/restore cycle raised no validation error");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    [[nodiscard]] int getResultProperty() const { return result_; }
};

int main()
{
    WebGpuContextRecoveryTest game;
    game.Run();
    return game.getResultProperty();
}
