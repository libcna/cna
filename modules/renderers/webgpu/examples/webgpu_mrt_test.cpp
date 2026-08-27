// SPDX-License-Identifier: MS-PL
// WEBGPU-85/86/87: multiple simultaneous render targets.
//
// Binds 2 and then 4 RenderTarget2D targets together and draws a full-screen quad through a custom
// WGSL ShaderEffect whose fragment writes a DISTINCT swizzle of one `uBase` colour to each
// @location output. Reading every slot back proves slot N holds slot N's content -- a mis-wired MRT
// pass (one output fanned to every attachment, or the wrong attachment order) is caught because each
// slot's expected colour is a different permutation of uBase.
//
// uBase = (200,120,40): slot0=uBase (200,120,40), slot1=.gbra (120,40,200), slot2=.brga (40,200,120),
// slot3=.rbga (200,40,120) -- all four distinct, each distinguishable from slot 0.
//
// Check A -- a 2-target bind: slot 0 reads (200,120,40), slot 1 reads (120,40,200).
// Check B -- a 4-target bind: slots 0..3 each read their own swizzle.
//
// Discrimination: making the fragment write the SAME value to every slot (o.t1 = o.t0, ...) fails
// the per-slot checks -- the test does not merely confirm "both non-black".
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    // Position-only vertex; the fragment writes uBase-derived swizzles to each slot.
    const char* const kVertWgsl = R"WGSL(
@vertex fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return vec4f(position, 1.0);
}
)WGSL";

    const char* const kFrag2Wgsl = R"WGSL(
struct U { uBase: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct FragOut {
    @location(0) t0: vec4f,
    @location(1) t1: vec4f,
};
@fragment fn fs_main() -> FragOut {
    var o: FragOut;
    o.t0 = u.uBase;
    o.t1 = u.uBase.gbra;
    return o;
}
)WGSL";

    const char* const kFrag4Wgsl = R"WGSL(
struct U { uBase: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct FragOut {
    @location(0) t0: vec4f,
    @location(1) t1: vec4f,
    @location(2) t2: vec4f,
    @location(3) t3: vec4f,
};
@fragment fn fs_main() -> FragOut {
    var o: FragOut;
    o.t0 = u.uBase;
    o.t1 = u.uBase.gbra;
    o.t2 = u.uBase.brga;
    o.t3 = u.uBase.rbga;
    return o;
}
)WGSL";

    const char* const kUniformNames[] = {"uBase"};
    const int kUniformOffsets[] = {0};

    bool colorNear(Color a, Color b, int tol = 16)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    RenderTarget2D MakeTarget(GraphicsDevice& device)
    {
        return RenderTarget2D(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    Color CenterOf(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels[(static_cast<std::size_t>(kSize) / 2) * kSize + kSize / 2];
    }

    VertexBuffer MakeQuad(GraphicsDevice& device)
    {
        VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Color w(255, 255, 255, 255);
        const VertexPositionColor quad[6] = {
            {Vector3(-1.0f, 1.0f, 0.0f), w},
            {Vector3(-1.0f, -1.0f, 0.0f), w},
            {Vector3(1.0f, -1.0f, 0.0f), w},
            {Vector3(-1.0f, 1.0f, 0.0f), w},
            {Vector3(1.0f, -1.0f, 0.0f), w},
            {Vector3(1.0f, 1.0f, 0.0f), w},
        };
        vb.SetData(quad, 6);
        return vb;
    }
}

class WebGpuMrtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        ++checkCount_;
        if (ok) ++passCount_;
    }

    // Expected colours for uBase=(200,120,40): slot i is a distinct permutation.
    static Color Expected(int slot)
    {
        switch (slot)
        {
        case 0:  return Color(200, 120, 40, 255);   // uBase
        case 1:  return Color(120, 40, 200, 255);    // .gbra
        case 2:  return Color(40, 200, 120, 255);    // .brga
        default: return Color(200, 40, 120, 255);    // .rbga
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        ShaderEffect fx2(dev, kVertWgsl, kFrag2Wgsl);
        ShaderEffect fx4(dev, kVertWgsl, kFrag4Wgsl);
        if (!fx2.IsEffectValid() || !fx4.IsEffectValid())
        {
            std::printf("[FAIL] MRT ShaderEffect compile failed: 2=%s 4=%s\n",
                        fx2.GetCompileErrorEXT().c_str(), fx4.GetCompileErrorEXT().c_str());
            std::printf("=== 0/10 PASS ===\n");
            result_ = 1;
            Exit();
            return;
        }

        // Check A: two targets.
        {
            RenderTarget2D t0 = MakeTarget(dev);
            RenderTarget2D t1 = MakeTarget(dev);
            VertexBuffer vb = MakeQuad(dev);
            dev.SetRenderTargets({RenderTargetBinding(&t0), RenderTargetBinding(&t1)});
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            dev.Clear(Color(0, 0, 0, 255));
            fx2.Apply();
            fx2.DeclareUniformBlockEXT(16, kUniformNames, kUniformOffsets, 1);
            fx2.SetUniformVec4("uBase", 200.0f / 255.0f, 120.0f / 255.0f, 40.0f / 255.0f, 1.0f);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(nullptr);

            check(colorNear(CenterOf(t0), Expected(0)), "2 targets: slot 0 holds its own content");
            check(colorNear(CenterOf(t1), Expected(1)), "2 targets: slot 1 holds its own content (not slot 0's)");
        }

        // Check B: four targets.
        {
            RenderTarget2D t0 = MakeTarget(dev);
            RenderTarget2D t1 = MakeTarget(dev);
            RenderTarget2D t2 = MakeTarget(dev);
            RenderTarget2D t3 = MakeTarget(dev);
            VertexBuffer vb = MakeQuad(dev);
            dev.SetRenderTargets({RenderTargetBinding(&t0), RenderTargetBinding(&t1),
                                  RenderTargetBinding(&t2), RenderTargetBinding(&t3)});
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            dev.Clear(Color(0, 0, 0, 255));
            fx4.Apply();
            fx4.DeclareUniformBlockEXT(16, kUniformNames, kUniformOffsets, 1);
            fx4.SetUniformVec4("uBase", 200.0f / 255.0f, 120.0f / 255.0f, 40.0f / 255.0f, 1.0f);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(nullptr);

            check(colorNear(CenterOf(t0), Expected(0)), "4 targets: slot 0 holds its own content");
            check(colorNear(CenterOf(t1), Expected(1)), "4 targets: slot 1 holds its own content");
            check(colorNear(CenterOf(t2), Expected(2)), "4 targets: slot 2 holds its own content");
            check(colorNear(CenterOf(t3), Expected(3)), "4 targets: slot 3 holds its own content");
        }

        // Check C: per-slot ColorWriteChannels (WEBGPU-143). The custom effect writes all four slots,
        // but each attachment's own BlendState mask keeps only its enabled channel(s) over the black
        // clear -- slot 0 Red, slot 1 Green, slot 2 Blue, slot 3 All.
        {
            RenderTarget2D t0 = MakeTarget(dev);
            RenderTarget2D t1 = MakeTarget(dev);
            RenderTarget2D t2 = MakeTarget(dev);
            RenderTarget2D t3 = MakeTarget(dev);
            VertexBuffer vb = MakeQuad(dev);

            BlendState bs = BlendState::Opaque;
            bs.setColorWriteChannelsProperty(ColorWriteChannels::Red);
            bs.setColorWriteChannels1Property(ColorWriteChannels::Green);
            bs.setColorWriteChannels2Property(ColorWriteChannels::Blue);
            bs.setColorWriteChannels3Property(ColorWriteChannels::All);
            dev.setBlendStateProperty(bs);

            dev.SetRenderTargets({RenderTargetBinding(&t0), RenderTargetBinding(&t1),
                                  RenderTargetBinding(&t2), RenderTargetBinding(&t3)});
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            dev.Clear(Color(0, 0, 0, 255));
            fx4.Apply();
            fx4.SetUniformVec4("uBase", 200.0f / 255.0f, 120.0f / 255.0f, 40.0f / 255.0f, 1.0f);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(nullptr);

            // slot0 uBase=(200,120,40) masked to R; slot1 .gbra=(120,40,200) masked to G;
            // slot2 .brga=(40,200,120) masked to B; slot3 .rbga full.
            check(colorNear(CenterOf(t0), Color(200, 0, 0, 255)),
                  "ColorWriteChannels: slot 0 masked to Red keeps only R");
            check(colorNear(CenterOf(t1), Color(0, 40, 0, 255)),
                  "ColorWriteChannels1: slot 1 masked to Green keeps only G");
            check(colorNear(CenterOf(t2), Color(0, 0, 120, 255)),
                  "ColorWriteChannels2: slot 2 masked to Blue keeps only B");
            check(colorNear(CenterOf(t3), Expected(3)),
                  "ColorWriteChannels3: slot 3 masked to All keeps its full content");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_ && checkCount_ == 10) ? 0 : 1;
        Exit();
    }

public:
    WebGpuMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuMrtTest game;
    game.Run();
    return game.getResult();
}
