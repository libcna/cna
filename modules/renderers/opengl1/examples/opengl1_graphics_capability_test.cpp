// SPDX-License-Identifier: MS-PL
// plans/plan_opengl1.md phase 11: locks in GraphicsDevice::SupportsCapability()'s truth table for the
// OPENGL1 renderer against concrete, independently-checkable evidence -- not just "it returns
// true/false", but that the *reason* it returns that value is real: AnisotropicFiltering is
// cross-checked against OpenGL1Renderer's own runtime extension detection (not a static
// guess), and the "false" capabilities are cross-checked against the shared-default behavior
// (CreateOcclusionQuery() returning nullptr) that backs each "false" claim, the same way
// sdlrenderer_graphics_capability_test.cpp/directx3_graphics_capability_test.cpp/
// canvas_graphics_capability_test.cpp do for their own (much narrower) 2D-only renderers.
//
// plans/plan_opengl1.md item 23 (EasyGL parity, found 2026-07-20): OcclusionQuery is no longer
// unconditionally false -- real ARB_occlusion_query/core-1.5 support is now cross-checked the
// same way AnisotropicFiltering already is (a raw GL_VERSION/GL_EXTENSIONS scan independent of
// OpenGL1Capabilities' own bookkeeping), and backed by a real, functionally meaningful occlusion
// query: an unoccluded draw must report a large PixelCount(), and the SAME draw fully covered by
// a nearer opaque quad (real GL_LESS depth-test rejection) must report PixelCount()==0 -- not
// just "some query object exists", but that GL_SAMPLES_PASSED genuinely reflects real per-pixel
// visibility.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    bool HasExtensionToken(const char* name)
    {
        const GLubyte* extStr = glGetString(GL_EXTENSIONS);
        const std::string padded = std::string(" ") + (extStr ? reinterpret_cast<const char*>(extStr) : "") + " ";
        return padded.find(std::string(" ") + name + " ") != std::string::npos;
    }
}

class OpenGL1GraphicsCapabilityTest : public Game
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

        // OPENGL1 is a real desktop 3D renderer, not a 2D-only one -- these must be true.
        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported (glPolygonMode)");

        // AnisotropicFiltering must track the REAL driver extension, not a hardcoded guess in
        // either direction -- cross-checked directly against glGetString(GL_EXTENSIONS) here,
        // independent of OpenGL1Capabilities' own internal bookkeeping.
        const bool realAniso = HasExtensionToken("GL_EXT_texture_filter_anisotropic");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering) == realAniso,
              "AnisotropicFiltering matches real GL_EXT_texture_filter_anisotropic presence");

        // Not implemented by this renderer at all -- must not over-report.
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // plans/plan_opengl1.md item 23: OcclusionQuery must track the REAL driver version/extension,
        // not a hardcoded guess in either direction -- same cross-check style as
        // AnisotropicFiltering above, independent of OpenGL1Capabilities' own bookkeeping.
        int glMajor = 1, glMinor = 1;
        std::sscanf(reinterpret_cast<const char*>(glGetString(GL_VERSION)), "%d.%d", &glMajor, &glMinor);
        const bool realOcclusionQuery = (glMajor > 1 || (glMajor == 1 && glMinor >= 5))
            || HasExtensionToken("GL_ARB_occlusion_query");
        check(dev.SupportsCapability(GraphicsCapability::OcclusionQuery) == realOcclusionQuery,
              "OcclusionQuery matches real ARB_occlusion_query/core-1.5 presence");

        if (dev.SupportsCapability(GraphicsCapability::OcclusionQuery))
        {
            // Real, functionally meaningful occlusion query: GL_SAMPLES_PASSED must genuinely
            // reflect per-pixel depth-test visibility, not just "some query object exists".
            dev.SetDepthTestEnabled(true);
            dev.setDepthStencilStateProperty(DepthStencilState::Default); // LessEqual, write-enabled
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            const auto& vp = dev.getViewportProperty();
            const float W = (float)vp.getWidthProperty(), H = (float)vp.getHeightProperty();
            Matrix world = Matrix::getIdentityProperty();
            Matrix view = Matrix::getIdentityProperty();
            Matrix proj = Matrix::CreateOrthographicOffCenter(0.0f, W, H, 0.0f, -1.0f, 1.0f);

            auto drawFullViewportQuad = [&](float z)
            {
                const VertexPositionColor q[6] = {
                    { Vector3(0.0f, 0.0f, z), Color::White }, { Vector3(0.0f, H, z), Color::White },
                    { Vector3(W, H, z), Color::White },       { Vector3(0.0f, 0.0f, z), Color::White },
                    { Vector3(W, H, z), Color::White },       { Vector3(W, 0.0f, z), Color::White },
                };
                BasicEffect fx(dev);
                fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
                fx.VertexColorEnabled = true;
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            };

            dev.Clear(Color::Black, 1.0f);
            OcclusionQuery unoccluded(dev);
            unoccluded.Begin();
            drawFullViewportQuad(0.5f); // z=0.5, nothing else drawn -- must all pass depth test.
            unoccluded.End();
            for (int i = 0; i < 100000 && !unoccluded.getIsCompleteProperty(); ++i) {}
            check(unoccluded.getIsCompleteProperty(), "unoccluded query becomes complete (bounded poll)");
            const int unoccludedCount = unoccluded.getPixelCountProperty();
            std::printf("unoccluded PixelCount=%d (viewport=%dx%d=%d)\n",
                        unoccludedCount, vp.getWidthProperty(), vp.getHeightProperty(),
                        vp.getWidthProperty() * vp.getHeightProperty());
            check(unoccludedCount >= (vp.getWidthProperty() * vp.getHeightProperty() * 9) / 10,
                  "unoccluded full-viewport quad: PixelCount() close to the real viewport area");

            dev.Clear(Color::Black, 1.0f);
            // Matrix::CreateOrthographicOffCenter's M33/M43 (Matrix.cpp) map world Z LINEARLY to
            // clip-space Z with a NEGATIVE slope for these near/far arguments (-1,1) -- a LARGER
            // world Z produces a SMALLER (nearer, under LessEqual) resulting depth value here, the
            // opposite of the more common "larger Z = farther" intuition. Confirmed empirically
            // (0.9 in front of 0.5 correctly occludes; the reverse assignment did not).
            drawFullViewportQuad(0.9f); // real occluder (nearer), NOT wrapped in a query.
            OcclusionQuery occluded(dev);
            occluded.Begin();
            drawFullViewportQuad(0.5f); // same quad again, now entirely behind the occluder.
            occluded.End();
            for (int i = 0; i < 100000 && !occluded.getIsCompleteProperty(); ++i) {}
            check(occluded.getIsCompleteProperty(), "occluded query becomes complete (bounded poll)");
            const int occludedCount = occluded.getPixelCountProperty();
            std::printf("occluded PixelCount=%d\n", occludedCount);
            check(occludedCount == 0,
                  "fully occluded quad: PixelCount()==0 -- real GL_LESS_EQUAL depth-test "
                  "rejection, not just a nonzero placeholder");
        }
        else
        {
            // Honest degraded-behavior check for a driver that genuinely lacks the extension --
            // matches this file's own pre-item-23 assertion.
            OcclusionQuery q(dev);
            q.Begin();
            q.End();
            check(!q.getIsCompleteProperty() && q.getPixelCountProperty() == 0,
                  "OcclusionQuery=false backs a real OcclusionQuery object that never completes");
        }

        // WireFrame=true must actually work, not just report true.
        dev.SetDepthTestEnabled(false);
        RasterizerState rs;
        rs.setFillModeProperty(FillMode::WireFrame);
        dev.setRasterizerStateProperty(rs);
        GLint polyMode[2] = {0, 0};
        glGetIntegerv(GL_POLYGON_MODE, polyMode);
        check(polyMode[0] == GL_LINE && polyMode[1] == GL_LINE,
              "WireFrame=true backs a real glPolygonMode(GL_LINE) state change");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
