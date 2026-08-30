// SPDX-License-Identifier: MS-PL
// Task 202: GraphicsDevice null/invalid-argument validation.
//
// Verifies that GraphicsDevice throws the correct exception types for:
//   1. SetVertexBuffers with more than 16 bindings (ArgumentOutOfRangeException)
//   2. SetVertexBuffers with null entries (ArgumentNullException)
//   3. GetBackBufferData with null data pointer (std::invalid_argument)
//   4. Present() while a render target is bound (InvalidOperationException)

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DeviceValidationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // 1. SetVertexBuffers with 17 bindings must throw ArgumentOutOfRangeException
        {
            std::vector<VertexBufferBinding> tooMany(17);
            bool threw = false;
            try { device.SetVertexBuffers(tooMany); }
            catch (const System::ArgumentOutOfRangeException&) { threw = true; }
            catch (...) {}
            check(threw, "SetVertexBuffers(17) throws ArgumentOutOfRangeException");
        }

        // 2. REMED-GFX-222: 16 default/null bindings must be ACCEPTED, not rejected. FNA's own
        // PrepareVertexBindingArray carries null entries through, so a null-entry guard here would
        // reject a shape XNA allows. This assertion was written before REMED-GFX-222 landed and
        // asserted the opposite; it is restored to the current shared contract, which every
        // renderer including this one already implements.
        {
            bool threw = false;
            try { device.SetVertexBuffers(std::vector<VertexBufferBinding>(16)); }
            catch (...) { threw = true; }
            check(!threw, "SetVertexBuffers(16 null entries) is accepted");
            device.SetVertexBuffers({});
        }

        // 3. Sixteen LIVE bindings are valid on a 3D renderer. Raster-only Skia instead proves
        // its already-declared resource boundary; the count/null validation above remains shared.
#if 1
        {
            VertexBuffer buffer(device, 1);
            std::vector<VertexBufferBinding> maxOk;
            maxOk.reserve(16);
            for (int i = 0; i < 16; ++i) maxOk.emplace_back(&buffer);
            bool threw = false;
            try { device.SetVertexBuffers(maxOk); }
            catch (...) { threw = true; }
            check(!threw, "SetVertexBuffers(16 live bindings) does not throw");
            device.SetVertexBuffers({});
        }
#endif

        // 4. GetBackBufferData with null data must throw std::invalid_argument
        {
            bool threw = false;
            try { device.GetBackBufferData(static_cast<Color*>(nullptr), 0); }
            catch (const std::invalid_argument&) { threw = true; }
            catch (...) {}
            check(threw, "GetBackBufferData(nullptr) throws invalid_argument");
        }

        // 5. Present while a render target is bound must throw InvalidOperationException
        {
            auto rt = std::make_unique<RenderTarget2D>(device, 64, 64);
            device.SetRenderTarget(rt.get());
            bool threw = false;
            try { device.Present(); }
            catch (const System::InvalidOperationException&) { threw = true; }
            catch (...) {}
            device.SetRenderTarget(nullptr); // restore backbuffer
            check(threw, "Present() while RT bound throws InvalidOperationException");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DeviceValidationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DeviceValidationTest game;
    game.Run();
    return game.getResult();
}
