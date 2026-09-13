// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-132 (finding F-12): does the deferred draw model make a
// dynamic-buffer update observable between two draws in one frame?
//
// The question
// ------------
// F-12 reasoned that it must: Vulkan overrides none of `SetDataWithOptions`/`SetData16WithOptions`/
// `SetData32WithOptions`, its buffers are single persistently-mapped allocations, and this renderer
// records draws for replay at `Present()`. If a queued draw referenced the buffer, two draws with a
// `SetData` between them would both replay against the LAST upload, where EasyGL's immediate draws
// each read the data current at their own call.
//
// What this file measures
// -----------------------
// Draw a quad on the left from a `DynamicVertexBuffer`, `SetData` new geometry with
// `SetDataOptions::Discard`, draw a quad on the right, then read back BOTH regions in the same
// frame. If the deferred model collapsed the two draws onto the last upload, the left region would
// be missing its quad -- the first draw would have been replayed with the second's data.
//
// The two probes are the point. Reading only the right region cannot tell "both draws are correct"
// from "the first draw was overwritten", because the right one is right either way.
//
// Leg C is the control. It proves the readback can see a quad at all in the left region, by drawing
// one there in a frame with no intervening SetData; without it, a left region that is empty for
// some unrelated reason would be reported as F-12 confirmed.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {

/// Two triangles filling the NDC box [x0,x1] x [-0.8,0.8], in the given colour.
std::array<VertexPositionColor, 6> Quad(float x0, float x1, Color c)
{
    return {{
        { Vector3(x0, -0.8f, 0.0f), c }, { Vector3(x1, -0.8f, 0.0f), c },
        { Vector3(x0,  0.8f, 0.0f), c }, { Vector3(x1, -0.8f, 0.0f), c },
        { Vector3(x1,  0.8f, 0.0f), c }, { Vector3(x0,  0.8f, 0.0f), c },
    }};
}

class DynamicBufferUpdateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int failures_ = 0;
    Color leftControl_{0, 0, 0, 0};

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

    static Color ReadAt(GraphicsDevice& dev, float ndcX)
    {
        const auto& vp = dev.getViewportProperty();
        const int px = static_cast<int>((ndcX * 0.5f + 0.5f) *
                                        static_cast<float>(vp.getWidthProperty()));
        const Rectangle r(px, vp.getHeightProperty() / 2, 1, 1);
        Color out(0, 0, 0, 0);
        dev.GetBackBufferData(&r, &out, 0, 1);
        return out;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (frame_ > 1) { Exit(); return; }

        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        DynamicVertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6,
                               BufferUsage::WriteOnly);

        const auto left  = Quad(-0.9f, -0.1f, Color(255, 0, 0, 255));
        const auto right = Quad( 0.1f,  0.9f, Color(0, 0, 255, 255));

        if (frame_ == 0)
        {
            // ---- leg C: the control ------------------------------------------
            // One upload, one draw, in the left region. Establishes that the readback below can
            // see a quad there at all.
            vb.SetData(left.data(), 0, 6, SetDataOptions::Discard);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            leftControl_ = ReadAt(dev, -0.5f);
            check(leftControl_.getRProperty() >= 200 && leftControl_.getGProperty() <= 60,
                  "C control: a single draw puts a red quad in the left region (" +
                      std::to_string(leftControl_.getRProperty()) + "," +
                      std::to_string(leftControl_.getGProperty()) + "," +
                      std::to_string(leftControl_.getBProperty()) + ")");
            ++frame_;
            return;
        }

        // ---- the measurement --------------------------------------------------
        // Draw left, re-upload with Discard, draw right -- all in one frame.
        vb.SetData(left.data(), 0, 6, SetDataOptions::Discard);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        vb.SetData(right.data(), 0, 6, SetDataOptions::Discard);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        const Color l = ReadAt(dev, -0.5f);
        const Color r = ReadAt(dev,  0.5f);

        check(l.getRProperty() >= 200 && l.getGProperty() <= 60,
              "A the FIRST draw kept its own data: left region is red (" +
                  std::to_string(l.getRProperty()) + "," + std::to_string(l.getGProperty()) + "," +
                  std::to_string(l.getBProperty()) +
                  ") -- green here would be F-12 confirmed, the first draw replayed with the "
                  "second upload");
        check(r.getBProperty() >= 200 && r.getGProperty() <= 60,
              "B the SECOND draw used the new data: right region is blue (" +
                  std::to_string(r.getRProperty()) + "," + std::to_string(r.getGProperty()) + "," +
                  std::to_string(r.getBProperty()) + ")");

        ++frame_;
    }

public:
    DynamicBufferUpdateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    DynamicBufferUpdateTest game;
    game.Run();
    return game.getResult();
}
