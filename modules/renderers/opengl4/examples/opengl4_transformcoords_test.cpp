// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-28: real TransformWindowToLogical/TransformLogicalToWindow for the OpenGL4
// graphics renderer -- previously unoverridden (inherited the default no-op `return false`), so
// Mouse::SetPosition (which calls TransformLogicalToWindow to place the OS cursor) and
// SdlInputBridge (which calls TransformWindowToLogical to map incoming physical mouse events)
// silently failed to scale coordinates on this renderer whenever a non-default virtual resolution
// was in play. Ported EasyGLRenderer's own pure-uniform-scale (no offset) formula exactly:
// scale = virtualHeight / physicalWindowHeight -- exact for this renderer's own default
// FixedHeightDynamicWidth presentation, where the logical viewport always fills the whole
// physical window (no letterbox bars to offset for).
//
// The test window is created at a fixed 64x64 physical size, then GraphicsDevice.GetRenderer()'s
// own SetVirtualResolution() is called directly with a distinct 128x128 virtual resolution (a
// clean, deterministic 2x scale factor that doesn't depend on any DPI/fullscreen-specific
// environment behavior) to force a genuinely non-trivial (not accidentally 1:1) mapping.
//
// Check A -- TransformWindowToLogical: a physical point at the window's own centre (32,32) must
//   map to the virtual space's own centre (64,64) -- the 2x scale factor genuinely applied.
// Check B -- TransformWindowToLogical at a NON-centre point (10,10) must map to (20,20), not
//   (10,10) -- proves this is a real scale, not an accidental identity/no-op pass-through.
// Check C -- TransformLogicalToWindow is the exact inverse of Check A: (64,64) maps back to
//   (32,32).
// Check D -- TransformLogicalToWindow at a non-centre point (20,20) maps back to (10,10) --
//   round-trips Check B exactly.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kPhysicalSize = 64;
    constexpr int kVirtualSize = 128;

    bool NearlyEqual(float a, float b, float tol = 0.5f)
    {
        return std::abs(a - b) <= tol;
    }
}

class OpenGL4TransformCoordsTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = dev.GetRenderer();

        // Force a genuinely non-trivial (not accidentally 1:1) virtual<->physical mismatch,
        // independent of any DPI/fullscreen-specific environment behavior.
        renderer.SetVirtualResolution(kVirtualSize, kVirtualSize);

        float logX = 0.0f, logY = 0.0f;
        const bool okA = renderer.TransformWindowToLogical(32.0f, 32.0f, logX, logY);
        Check(okA && NearlyEqual(logX, 64.0f) && NearlyEqual(logY, 64.0f),
              "Check A: TransformWindowToLogical maps the window's own centre (32,32) to the virtual space's own centre (64,64)");

        float logX2 = 0.0f, logY2 = 0.0f;
        const bool okB = renderer.TransformWindowToLogical(10.0f, 10.0f, logX2, logY2);
        Check(okB && NearlyEqual(logX2, 20.0f) && NearlyEqual(logY2, 20.0f),
              "Check B: TransformWindowToLogical at (10,10) maps to (20,20) -- a real 2x scale, not identity");

        float winX = 0.0f, winY = 0.0f;
        const bool okC = renderer.TransformLogicalToWindow(64.0f, 64.0f, winX, winY);
        Check(okC && NearlyEqual(winX, 32.0f) && NearlyEqual(winY, 32.0f),
              "Check C: TransformLogicalToWindow is the exact inverse of Check A ((64,64) -> (32,32))");

        float winX2 = 0.0f, winY2 = 0.0f;
        const bool okD = renderer.TransformLogicalToWindow(20.0f, 20.0f, winX2, winY2);
        Check(okD && NearlyEqual(winX2, 10.0f) && NearlyEqual(winY2, 10.0f),
              "Check D: TransformLogicalToWindow round-trips Check B exactly ((20,20) -> (10,10))");
    }

public:
    OpenGL4TransformCoordsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kPhysicalSize);
        gdm_->setPreferredBackBufferHeightProperty(kPhysicalSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4TransformCoordsTest>();
}
