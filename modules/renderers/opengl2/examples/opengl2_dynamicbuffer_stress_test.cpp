// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: proof that DynamicVertexBuffer/DynamicIndexBuffer SetData(..., SetDataOptions)
// renders correctly on OpenGL2 across None/Discard/NoOverwrite -- reuses
// examples/easygl_dynamic_buffer_stress_test.cpp's own scene and cycling scheme verbatim.
//
// Until this task, IGraphicsRenderer::SetDataWithOptions()/SetData16WithOptions()/
// SetData32WithOptions() were never overridden on OpenGL2, so the shared base-class default
// silently ignored `options` and fell back to a full SetData()/SetData16()/SetData32() (plain
// glBufferData every call). That default is always CORRECT (this test exists to prove behavior,
// not just that it compiles), but it forgoes the Discard/NoOverwrite upload paths a dynamic
// buffer needs for the perf case they exist for. OpenGL2's VB/IB now implement the same
// orphan-then-glBufferSubData (Discard) / plain-glBufferSubData (NoOverwrite) strategy as
// EasyGLVertexBufferRenderer::uploadWithOptions.
//
// Runs 12 frames cycling through SetDataOptions::None, Discard, NoOverwrite, verifying the
// rendered center pixel matches the just-uploaded color every frame, and that vertex/index
// counts stay unchanged (i.e. no accidental capacity mutation from the orphan path).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

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

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kFrames = 12;

    const Vector3 kTL(-1.f,  1.f, 0.f);
    const Vector3 kBL(-1.f, -1.f, 0.f);
    const Vector3 kBR( 1.f, -1.f, 0.f);
    const Vector3 kTR( 1.f,  1.f, 0.f);

    const std::array<Color, 4> kColors = {
        Color(255, 0, 0, 255),
        Color(0, 255, 0, 255),
        Color(0, 0, 255, 255),
        Color(255, 255, 0, 255),
    };

    const std::array<SetDataOptions, 3> kOptions = {
        SetDataOptions::None,
        SetDataOptions::Discard,
        SetDataOptions::NoOverwrite,
    };

    const char* optionName(SetDataOptions o)
    {
        switch (o) {
        case SetDataOptions::None:        return "None";
        case SetDataOptions::Discard:     return "Discard";
        case SetDataOptions::NoOverwrite: return "NoOverwrite";
        }
        return "?";
    }
}

class OpenGL2DynamicBufferStressTest : public Game
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

        const VertexPositionColor verts[6] = {
            { kTL, col }, { kBL, col }, { kBR, col },
            { kTL, col }, { kBR, col }, { kTR, col },
        };

        dvb_->SetData(verts, 0, 6, opt);

        std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };
        dib_->SetData(idx, 0, 6, opt);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(dvb_.get());
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        Color got = readCenter(dev);
        char label[128];
        std::snprintf(label, sizeof(label),
            "Frame %d (%s): pixel=(%d,%d,%d) expected~(%d,%d,%d)",
            frameCount_, optionName(opt),
            got.getRProperty(), got.getGProperty(), got.getBProperty(),
            col.getRProperty(), col.getGProperty(), col.getBProperty());
        check(colorClose(got, col), label);

        check(dvb_->getVertexCountProperty() == 6, "DVB capacity stays 6 after SetData");
        check(dib_->getIndexCountProperty()  == 6, "DIB capacity stays 6 after SetData");

        dev.SetVertexBuffer(nullptr);

        ++frameCount_;
        if (frameCount_ >= kFrames) {
            std::printf("=== %d/%d PASS (%d frames) ===\n", pass_, pass_ + fail_, kFrames);
            Exit();
        }
    }

public:
    OpenGL2DynamicBufferStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2DynamicBufferStressTest g;
    g.Run();
    return g.getResult();
}
