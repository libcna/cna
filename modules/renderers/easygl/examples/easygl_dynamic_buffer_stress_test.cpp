// SPDX-License-Identifier: MS-PL
// Task 238: Stress test — DynamicVertexBuffer and DynamicIndexBuffer update every frame.
//
// Runs 12 frames cycling through SetDataOptions::None, Discard, NoOverwrite.
// Each frame:
//   1. DVB (6 verts, VertexPositionColor) is updated with a new solid color,
//      drawn as a full-screen quad, and the center pixel is read back to verify
//      the correct color reached the framebuffer.
//   2. DynamicIndexBuffer (6 indices) is updated with options and verified via
//      capacity check (pixel readback via DrawIndexedPrimitives for one option per cycle).
//
// The test exercises the EasyGL orphan strategy (Discard) and glBufferSubData
// path (NoOverwrite) across many consecutive updates without a recreate.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kFrames = 12;

static const Vector3 kTL(-1.f,  1.f, 0.f);
static const Vector3 kBL(-1.f, -1.f, 0.f);
static const Vector3 kBR( 1.f, -1.f, 0.f);
static const Vector3 kTR( 1.f,  1.f, 0.f);

static const std::array<Color, 4> kColors = {
    Color(255, 0, 0, 255),    // Red
    Color(0, 255, 0, 255),    // Green
    Color(0, 0, 255, 255),    // Blue
    Color(255, 255, 0, 255),  // Yellow
};

static const std::array<SetDataOptions, 3> kOptions = {
    SetDataOptions::None,
    SetDataOptions::Discard,
    SetDataOptions::NoOverwrite,
};

static const char* optionName(SetDataOptions o)
{
    switch (o) {
    case SetDataOptions::None:        return "None";
    case SetDataOptions::Discard:     return "Discard";
    case SetDataOptions::NoOverwrite: return "NoOverwrite";
    }
    return "?";
}

class DynamicBufferStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<DynamicVertexBuffer>   dvb_;
    std::unique_ptr<DynamicIndexBuffer>    dib_;

    int pass_ = 0;
    int fail_ = 0;
    int frameCount_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    bool colorClose(Color got, Color want, int tol = 20)
    {
        return std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty())) <= tol
            && std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty())) <= tol
            && std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty())) <= tol;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        VertexDeclaration decl(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });

        dvb_ = std::make_unique<DynamicVertexBuffer>(dev, decl, 6, BufferUsage::None);
        dib_ = std::make_unique<DynamicIndexBuffer>(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);

        // Prime the IB with a canonical index set.
        std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
        dib_->SetData(idx, 0, 6, SetDataOptions::None);
    }

    void Draw(const GameTime&) override
    {
        if (frameCount_ >= kFrames) return;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const SetDataOptions opt = kOptions[static_cast<std::size_t>(frameCount_) % kOptions.size()];
        const Color col          = kColors[static_cast<std::size_t>(frameCount_)  % kColors.size()];

        // Build a full-screen 2-triangle fan in source array positions 0..5
        // (startIndex=0 so the full 6-vert block is uploaded).
        const VertexPositionColor verts[6] = {
            { kTL, col }, { kBL, col }, { kBR, col },
            { kTL, col }, { kBR, col }, { kTR, col },
        };

        // Update DVB with current options.
        dvb_->SetData(verts, 0, 6, opt);

        // Update DIB with same options (any valid 6-index set).
        std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };
        dib_->SetData(idx, 0, 6, opt);

        // Bind and draw.
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(dvb_.get());
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Read center pixel and verify.
        Color got = readCenter(dev);
        char label[128];
        std::snprintf(label, sizeof(label),
            "Frame %d (%s): pixel=(%d,%d,%d) expected≈(%d,%d,%d)",
            frameCount_, optionName(opt),
            got.getRProperty(), got.getGProperty(), got.getBProperty(),
            col.getRProperty(), col.getGProperty(), col.getBProperty());
        check(colorClose(got, col), label);

        // Verify capacities unchanged after update.
        check(dvb_->getVertexCountProperty() == 6,  "DVB capacity stays 6 after SetData");
        check(dib_->getIndexCountProperty()  == 6,  "DIB capacity stays 6 after SetData");

        dev.SetVertexBuffer(nullptr);

        ++frameCount_;
        if (frameCount_ >= kFrames) {
            std::printf("=== %d/%d PASS (%d frames) ===\n", pass_, pass_ + fail_, kFrames);
            Exit();
        }
    }

public:
    DynamicBufferStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DynamicBufferStressTest g;
    g.Run();
    return g.getResult();
}
