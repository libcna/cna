// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-330 (finding F-03): SetPresentationMode must change the presented
// rectangle.
//
// The defect
// ----------
// `void SetPresentationMode(int) override {}` was the only empty override body in either renderer.
// A game selecting `CnaPresentationMode::Letterbox` got Vulkan's uniform height-derived scale
// instead -- silently, which is the same class of defect as F-31: the call is accepted and
// discarded.
//
// The scene, and why it discriminates
// -----------------------------------
// The virtual resolution is set to HALF the drawable's height, so the virtual aspect is 2:1
// against a square-ish window. Under `Letterbox` the uniform fit-scale is therefore 1.0 in x and
// the presented rectangle is (0, H/4, W, H/2) -- bars occupying the top and bottom quarters. Under
// `Stretch` there is no rectangle to centre and the whole drawable is used.
//
// So one probe near the top edge separates the two: it is BAR (clear colour) under Letterbox and
// GEOMETRY under Stretch. With the no-op body both modes behaved like Stretch, so leg B fails
// against it. Leg C is the same probe under Stretch, and it is what stops leg B passing for the
// wrong reason -- a renderer that drew nothing at all, or clipped everything away, would satisfy
// "the top edge is background" without implementing anything.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::CnaPresentationMode;

namespace {

class PresentationModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_    = 0;
    int failures_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

    /// Clears green and draws a red quad over the whole viewport, then reads two pixels.
    void DrawAndProbe(GraphicsDevice& dev, Color& centre, Color& topEdge)
    {
        static const Color kRed(255, 0, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), kRed }, { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRed }, { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRed }, { Vector3(-1.0f,  1.0f, 0.0f), kRed },
        };
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();
        const Rectangle c(W / 2, H / 2, 1, 1);
        const Rectangle t(W / 2, H / 20, 1, 1);   // well inside the top quarter
        centre = Color(0, 0, 0, 0);
        topEdge = Color(0, 0, 0, 0);
        dev.GetBackBufferData(&c, &centre, 0, 1);
        dev.GetBackBufferData(&t, &topEdge, 0, 1);
    }

    static std::string Rgb(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," +
               std::to_string(c.getGProperty()) + "," + std::to_string(c.getBProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto* vk  = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        if (vk == nullptr)
        {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }
        if (frame_ > 1) { Exit(); return; }

        const auto& vp = dev.getViewportProperty();
        // A 2:1 virtual aspect against this window, so Letterbox has bars to produce.
        vk->SetVirtualResolution(vp.getWidthProperty(), vp.getHeightProperty() / 2);

        Color centre(0, 0, 0, 0), topEdge(0, 0, 0, 0);
        if (frame_ == 0)
        {
            vk->SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
            DrawAndProbe(dev, centre, topEdge);

            int px = 0, py = 0, pw = 0, ph = 0;
            vk->GetPresentedRectEXT(px, py, pw, ph);
            check(ph > 0 && ph < static_cast<int>(vp.getHeightProperty()),
                  "A Letterbox produces a presented rectangle shorter than the drawable (" +
                      std::to_string(pw) + "x" + std::to_string(ph) + " at " +
                      std::to_string(px) + "," + std::to_string(py) + ")");
            check(centre.getRProperty() >= 200 && centre.getGProperty() <= 60,
                  "B1 Letterbox: the centre is geometry " + Rgb(centre));
            check(topEdge.getGProperty() >= 200 && topEdge.getRProperty() <= 60,
                  "B2 Letterbox: the top edge is a BAR " + Rgb(topEdge) +
                      " -- red here is the no-op body, which made every mode behave like Stretch");
        }
        else
        {
            vk->SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch));
            DrawAndProbe(dev, centre, topEdge);
            check(centre.getRProperty() >= 200 && centre.getGProperty() <= 60,
                  "C1 Stretch: the centre is geometry " + Rgb(centre));
            check(topEdge.getRProperty() >= 200 && topEdge.getGProperty() <= 60,
                  "C2 Stretch: the top edge is GEOMETRY " + Rgb(topEdge) +
                      " -- this is what stops B2 passing for a renderer that simply drew nothing");
        }
        if (frame_ == 0)
        {
            // ---- legs D and E: VULKAN-331, the consumers of that rectangle ----------
            // D: GraphicsDevice.Viewport keeps reporting the LOGICAL size while the physical
            // rectangle differs -- the split the framework's own comment describes.
            int px = 0, py = 0, pw = 0, ph = 0;
            vk->GetPresentedRectEXT(px, py, pw, ph);
            int gx = 0, gy = 0, gw = 0, gh = 0;
            vk->GetDefaultViewportRect(gx, gy, gw, gh);
            check(gx == px && gy == py && gw == pw && gh == ph,
                  "D GetDefaultViewportRect reports the presented rectangle (" +
                      std::to_string(gx) + "," + std::to_string(gy) + " " +
                      std::to_string(gw) + "x" + std::to_string(gh) + ")");

            // E: a window point at the very top maps ABOVE the logical area under Letterbox,
            // because the bar is not part of it. With the old height-only transform it mapped to
            // y >= 0 and a click in the bar would have been delivered as a click on the game.
            float lx = 0.0f, ly = 0.0f;
            const bool ok = vk->TransformWindowToLogical(
                static_cast<float>(vp.getWidthProperty()) / 2.0f, 1.0f, lx, ly);
            check(ok && ly < 0.0f,
                  std::string("E a window point inside the top BAR maps outside the logical area ") +
                      "(y=" + std::to_string(ly) +
                      ") -- the height-only transform mapped it to y>=0, so a click on the bar "
                      "was delivered as a click on the game");

            // F: and the mapping round-trips inside the presented area.
            float wx = 0.0f, wy = 0.0f, rx = 0.0f, ry = 0.0f;
            if (vk->TransformLogicalToWindow(100.0f, 50.0f, wx, wy) &&
                vk->TransformWindowToLogical(wx, wy, rx, ry))
            {
                check(std::abs(rx - 100.0f) < 1.0f && std::abs(ry - 50.0f) < 1.0f,
                      "F logical->window->logical round-trips inside the presented area (" +
                          std::to_string(rx) + "," + std::to_string(ry) + ")");
            }
            else
            {
                check(false, "F the round-trip transforms refused");
            }
        }

        ++frame_;
    }

public:
    PresentationModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    PresentationModeTest game;
    game.Run();
    return game.getResult();
}
