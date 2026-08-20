// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-42: OcclusionQuery and ShaderEffect must release their raw-GL renderer objects
// during GraphicsDevice::Dispose(), before the renderer device/GL context is torn down.
//
// GraphicsDevice::Dispose() disposes every tracked GraphicsResource (calling each one's Dispose())
// BEFORE destroyNativeResources() resets its own renderer_ (which shuts sokol_gfx down and destroys
// the SDL GL context) -- see docs/graphics-resource-lifetime.md. VertexBuffer/IndexBuffer/Texture2D
// already reset their own renderer_ inside Dispose(bool) (see easygl_device_dispose_order_test.cpp,
// this test's own model). Before this fix, OcclusionQuery had no Dispose(bool) override at all, and
// ShaderEffect inherited Effect::Dispose(bool), which only reaches GraphicsResource::Dispose(bool)
// -- neither ever touched their own renderer_/effectRenderer_ unique_ptr. That member would then only
// be destroyed whenever the OcclusionQuery/ShaderEffect C++ object's own destructor happened to run
// -- which this test deliberately delays until well after GraphicsDevice::Dispose() -- calling raw
// glDeleteQueries()/glDeleteProgram() with no current GL context.
//
// Check A/B -- both resources have a real renderer before device disposal.
// Check C/D -- both are marked disposed by GraphicsDevice::Dispose().
// Check E/F -- both renderers are released (HasRenderer() == false) by that same call, not merely
//   scheduled for later release by some future destructor.
// Check G/H -- querying state after disposal returns the documented safe defaults (no renderer to
//   dereference), proving the release in E/F was real, not just a flag flip.
// Check I -- explicitly disposing the already-disposed resources afterward does not throw.
// Check J -- destroying both objects (heap-allocated, deleted here, well after device disposal)
//   raises no exception and does not crash the process -- the discriminating check: pre-fix, this
//   is where a contextless glDeleteQueries()/glDeleteProgram() call would happen.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Trivial, real GLSL -- attribute layout is irrelevant to this test (nothing is drawn), only
    // that SokolEffectRenderer genuinely compiles and links a GL program to release later.
    const char* kVertSrc = R"(#version 410 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
    const char* kFragSrc = R"(#version 410 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }
)";
}

class DisposeOrderOcclusionShaderEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Heap-allocated and deliberately kept alive well past device.Dispose() -- see check J.
        auto* query = new OcclusionQuery(dev);
        auto* effect = new ShaderEffect(dev, kVertSrc, kFragSrc);

        check(query->HasRenderer(), "A: OcclusionQuery has a renderer before device dispose");
        check(effect->HasRenderer(), "B: ShaderEffect has a renderer before device dispose");

        dev.Dispose();

        check(query->getIsDisposedProperty(), "C: OcclusionQuery disposed by device.Dispose()");
        check(effect->getIsDisposedProperty(), "D: ShaderEffect disposed by device.Dispose()");

        check(!query->HasRenderer(),
              "E: OcclusionQuery renderer released by device.Dispose(), not deferred");
        check(!effect->HasRenderer(),
              "F: ShaderEffect renderer released by device.Dispose(), not deferred");

        check(!query->getIsCompleteProperty() && query->getPixelCountProperty() == 0,
              "G: OcclusionQuery reports safe defaults once its renderer is gone");
        check(!effect->IsEffectValid(),
              "H: ShaderEffect reports safe defaults once its renderer is gone");

        bool noThrow = true;
        try
        {
            // Dispose(bool) being protected hides the public 0-arg Dispose() from OcclusionQuery's/
            // ShaderEffect's own scope (C++ name hiding) -- route through the IDisposable interface,
            // matching easygl_device_dispose_order_test.cpp's own disposeVia() precedent.
            static_cast<System::IDisposable*>(query)->Dispose();
            static_cast<System::IDisposable*>(effect)->Dispose();
        }
        catch (...) { noThrow = false; }
        check(noThrow, "I: disposing already-disposed resources does not throw");

        // The discriminating check: pre-fix, renderer_/effectRenderer_ were still alive here, so
        // these deletes would run a real glDeleteQueries()/glDeleteProgram() with no current GL
        // context (device.Dispose() already destroyed it above).
        bool destroyedCleanly = true;
        try
        {
            delete query;
            delete effect;
        }
        catch (...) { destroyedCleanly = false; }
        check(destroyedCleanly,
              "J: destroying both resources long after device disposal raises no exception");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DisposeOrderOcclusionShaderEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DisposeOrderOcclusionShaderEffectTest game;
    game.Run();
    return game.getResult();
}
