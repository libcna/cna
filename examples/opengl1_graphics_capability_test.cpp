// SPDX-License-Identifier: MS-PL
// plan_opengl1.md phase 11: locks in GraphicsDevice::SupportsCapability()'s truth table for the
// OPENGL1 backend against concrete, independently-checkable evidence -- not just "it returns
// true/false", but that the *reason* it returns that value is real: AnisotropicFiltering is
// cross-checked against OpenGL1GraphicsBackend's own runtime extension detection (not a static
// guess), and the "false" capabilities are cross-checked against the shared-default behavior
// (CreateOcclusionQuery() returning nullptr) that backs each "false" claim, the same way
// sdlrenderer_graphics_capability_test.cpp/dx3_graphics_capability_test.cpp/
// canvas_graphics_capability_test.cpp do for their own (much narrower) 2D-only backends.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

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

        // OPENGL1 is a real desktop 3D backend, not a 2D-only one -- these must be true.
        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported (glPolygonMode)");

        // AnisotropicFiltering must track the REAL driver extension, not a hardcoded guess in
        // either direction -- cross-checked directly against glGetString(GL_EXTENSIONS) here,
        // independent of OpenGL1Capabilities' own internal bookkeeping.
        const bool realAniso = HasExtensionToken("GL_EXT_texture_filter_anisotropic");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering) == realAniso,
              "AnisotropicFiltering matches real GL_EXT_texture_filter_anisotropic presence");

        // Not implemented by this backend at all -- must not over-report.
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // Back the OcclusionQuery=false claim with the actual degraded behavior it describes:
        // the backend's CreateOcclusionQuery() returns nullptr (shared IGraphicsBackend
        // default), so a real OcclusionQuery object silently no-ops instead of ever completing.
        {
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
