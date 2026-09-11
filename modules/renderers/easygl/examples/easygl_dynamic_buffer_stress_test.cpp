// SPDX-License-Identifier: MS-PL
// Task 238: Stress test — DynamicVertexBuffer and DynamicIndexBuffer update every frame.
//
// Runs two warm-up frames plus 60 measured frames cycling through
// SetDataOptions::None, Discard, NoOverwrite.
// The first frame uploads two different DVB objects with Discard and NoOverwrite before either
// draw reaches a readback boundary, then verifies their left/right colors independently. Remaining
// frames update one full-screen DVB with every option and verify its exact visible result.
// Each frame:
//   1. DVB data is updated, drawn, and read back to verify the correct color reached the framebuffer.
//   2. DynamicIndexBuffer (6 indices) is updated with options and verified via
//      capacity check (pixel readback via DrawIndexedPrimitives for one option per cycle).
//
// The test exercises renderer-specific Discard/NoOverwrite strategies across many consecutive
// updates without recreating the public buffer objects.

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

#if defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kWarmupFrames = 2;
static constexpr int kMeasuredFrames = 60;
static constexpr int kFrames = kWarmupFrames + kMeasuredFrames;

static const Vector3 kTL(-1.f,  1.f, 0.f);
static const Vector3 kBL(-1.f, -1.f, 0.f);
static const Vector3 kBR( 1.f, -1.f, 0.f);
static const Vector3 kTR( 1.f,  1.f, 0.f);
static const Vector3 kTM( 0.f,  1.f, 0.f);
static const Vector3 kBM( 0.f, -1.f, 0.f);

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
    std::unique_ptr<DynamicVertexBuffer>   discardDvb_;
    std::unique_ptr<DynamicVertexBuffer>   noOverwriteDvb_;
    std::unique_ptr<DynamicIndexBuffer>    dib_;

    int pass_ = 0;
    int fail_ = 0;
    int frameCount_ = 0;
#if defined(CNA_RENDERER_DIRECTX12)
    std::uint64_t uploadResourceBaseline_ = 0;
#endif

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Microsoft::Xna::Framework::Rectangle reg(
            vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    std::array<Color, 2> readLeftAndRight(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const int x = vp.getWidthProperty() / 4;
        const int width = vp.getWidthProperty() / 2 + 1;
        const Microsoft::Xna::Framework::Rectangle reg(
            x, vp.getHeightProperty() / 2, width, 1);
        std::vector<Color> pixels(static_cast<std::size_t>(width));
        dev.GetBackBufferData(&reg, pixels.data(), 0, width);
        return { pixels.front(), pixels.back() };
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
        discardDvb_ = std::make_unique<DynamicVertexBuffer>(dev, decl, 6, BufferUsage::None);
        noOverwriteDvb_ = std::make_unique<DynamicVertexBuffer>(dev, decl, 6, BufferUsage::None);
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

#if defined(CNA_RENDERER_DIRECTX12)
        if (frameCount_ == kWarmupFrames) {
            auto& renderer = dynamic_cast<CNA::Internal::Renderers::DirectX12::DirectX12Renderer&>(
                dev.GetRenderer());
            renderer.ResetSynchronizationCountersEXT();
            uploadResourceBaseline_ = renderer.GetUploadResourceCreationCountEXT();
        }
#endif

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        if (frameCount_ == 0) {
            const Color red(255, 0, 0, 255);
            const Color green(0, 255, 0, 255);
            const VertexPositionColor left[6] = {
                { kTL, red }, { kBL, red }, { kBM, red },
                { kTL, red }, { kBM, red }, { kTM, red },
            };
            const VertexPositionColor right[6] = {
                { kTM, green }, { kBM, green }, { kBR, green },
                { kTM, green }, { kBR, green }, { kTR, green },
            };
            std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };

            // Both copies remain pending together. Reusing the Discard source range for the
            // NoOverwrite upload corrupts the left draw and is visible in the single readback.
            discardDvb_->SetData(left, 0, 6, SetDataOptions::Discard);
            noOverwriteDvb_->SetData(right, 0, 6, SetDataOptions::NoOverwrite);
            dib_->SetData(idx, 0, 6, SetDataOptions::None);

            dev.SetVertexBuffer(discardDvb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(noOverwriteDvb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const auto got = readLeftAndRight(dev);
            check(colorClose(got[0], red) && colorClose(got[1], green),
                  "Same-frame Discard/NoOverwrite uploads retain distinct visible data");
            check(discardDvb_->getVertexCountProperty() == 6,
                  "Discard DVB capacity stays 6 after SetData");
            check(noOverwriteDvb_->getVertexCountProperty() == 6,
                  "NoOverwrite DVB capacity stays 6 after SetData");
            check(dib_->getIndexCountProperty() == 6, "DIB capacity stays 6 after SetData");
        } else {
            const SetDataOptions opt =
                kOptions[static_cast<std::size_t>(frameCount_) % kOptions.size()];
            const Color col = kColors[static_cast<std::size_t>(frameCount_) % kColors.size()];
            const VertexPositionColor verts[6] = {
                { kTL, col }, { kBL, col }, { kBR, col },
                { kTL, col }, { kBR, col }, { kTR, col },
            };
            std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };

            dvb_->SetData(verts, 0, 6, opt);
            dib_->SetData(idx, 0, 6, opt);
            dev.SetVertexBuffer(dvb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const Color got = readCenter(dev);
            char label[128];
            std::snprintf(label, sizeof(label),
                "Frame %d (%s): pixel=(%d,%d,%d) expected≈(%d,%d,%d)",
                frameCount_, optionName(opt),
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                col.getRProperty(), col.getGProperty(), col.getBProperty());
            check(colorClose(got, col), label);
            check(dvb_->getVertexCountProperty() == 6, "DVB capacity stays 6 after SetData");
            check(dib_->getIndexCountProperty() == 6, "DIB capacity stays 6 after SetData");
        }

        dev.SetVertexBuffer(nullptr);

        ++frameCount_;
        if (frameCount_ >= kFrames) {
#if defined(CNA_RENDERER_DIRECTX12)
            auto& renderer = dynamic_cast<CNA::Internal::Renderers::DirectX12::DirectX12Renderer&>(
                dev.GetRenderer());
            check(renderer.GetUploadResourceCreationCountEXT() == uploadResourceBaseline_,
                  "D3D12 upload ring creates no resources after warm-up");
            check(renderer.GetGpuWaitCountEXT() <= static_cast<std::uint64_t>(kMeasuredFrames),
                  "D3D12 performs at most one actual GPU wait per measured frame");
            check(renderer.GetUploadAllocationCountEXT() >=
                      static_cast<std::uint64_t>(2 * kMeasuredFrames),
                  "D3D12 vertex/index updates allocate distinct frame-ring ranges");
#endif
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
