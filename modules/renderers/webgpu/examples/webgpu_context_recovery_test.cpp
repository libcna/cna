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
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());

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
