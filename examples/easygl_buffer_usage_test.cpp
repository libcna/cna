// SPDX-License-Identifier: MS-PL
// Task 239: Validate BufferUsage::WriteOnly and BufferUsage::None flag handling.
//
// FNA contract:
//   - BufferUsage::WriteOnly → GL driver hint for write-optimised storage;
//     calling GetData() on such a buffer throws NotSupportedException.
//   - BufferUsage::None      → buffer may be written and read back.
//
// CNA deviations (documented here):
//   1. GL usage hint is NOT forwarded to the backend at buffer creation time;
//      all buffers use GL_DYNAMIC_DRAW regardless of the XNA flag.
//      Impact: minor performance difference only; correctness is unaffected.
//   2. GetData() is not implemented in CNA (no IVertexBufferBackend readback
//      method exists).  The WriteOnly restriction therefore cannot be enforced
//      at runtime.  This is tracked in docs/easygl_bugs.md.
//
// What this test does verify:
//   A. getBufferUsageProperty() returns the exact value passed to the ctor.
//   B. SetData() succeeds on both WriteOnly and None buffers.
//   C. DrawPrimitives from a WriteOnly VertexBuffer renders the correct pixel.
//   D. DrawPrimitives from a None VertexBuffer renders the correct pixel.
//   E. DynamicVertexBuffer with WriteOnly works across all SetDataOptions.
//   F. IndexBuffer WriteOnly / None: property stored and SetData succeeds.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Vector3 kTL(-1.f,  1.f, 0.f);
static const Vector3 kBL(-1.f, -1.f, 0.f);
static const Vector3 kBR( 1.f, -1.f, 0.f);
static const Vector3 kTR( 1.f,  1.f, 0.f);

class BufferUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template<class T>
    void checkEq(T actual, T expected, const char* label)
    {
        bool ok = (actual == expected);
        if (!ok)
            std::printf("[FAIL] %s (expected %d, got %d)\n", label,
                        static_cast<int>(expected), static_cast<int>(actual));
        else
            std::printf("[PASS] %s\n", label);
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
        return std::abs(int(got.getRProperty()) - int(want.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(want.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(want.getBProperty())) <= tol;
    }

    // Draws a full-screen quad from a VB and verifies the center pixel.
    void drawAndCheck(GraphicsDevice& dev, VertexBuffer& vb, Color col,
                      const char* label)
    {
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Color got = readCenter(dev);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: pixel=(%d,%d,%d) expected≈(%d,%d,%d)",
                      label,
                      got.getRProperty(), got.getGProperty(), got.getBProperty(),
                      col.getRProperty(), col.getGProperty(), col.getBProperty());
        check(colorClose(got, col), buf);
    }

    static VertexDeclaration posColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    static void fillQuad(VertexPositionColor* out, Color col)
    {
        out[0] = { kTL, col };  out[1] = { kBL, col };  out[2] = { kBR, col };
        out[3] = { kTL, col };  out[4] = { kBR, col };  out[5] = { kTR, col };
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kRed  (255,   0,   0, 255);
        const Color kGreen(  0, 255,   0, 255);
        const Color kBlue (  0,   0, 255, 255);

        // ── A: getBufferUsageProperty() round-trip ─────────────────────────
        {
            auto decl = posColorDecl();
            VertexBuffer vbW(dev, decl, 6, BufferUsage::WriteOnly);
            VertexBuffer vbN(dev, decl, 6, BufferUsage::None);
            checkEq(static_cast<int>(vbW.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "VB WriteOnly: getBufferUsageProperty() == WriteOnly");
            checkEq(static_cast<int>(vbN.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::None),
                    "VB None: getBufferUsageProperty() == None");

            IndexBuffer ibW(dev, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
            IndexBuffer ibN(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            checkEq(static_cast<int>(ibW.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "IB WriteOnly: getBufferUsageProperty() == WriteOnly");
            checkEq(static_cast<int>(ibN.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::None),
                    "IB None: getBufferUsageProperty() == None");
        }

        // ── B+C: SetData + draw succeeds on WriteOnly VertexBuffer ─────────
        {
            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, 6, BufferUsage::WriteOnly);
            VertexPositionColor verts[6];
            fillQuad(verts, kRed);
            vb.SetData(verts, 0, 6);
            check(vb.getVertexCountProperty() == 6,
                  "WriteOnly VB: capacity unchanged after SetData");
            drawAndCheck(dev, vb, kRed, "WriteOnly VB draws red quad");
        }

        // ── D: SetData + draw succeeds on None VertexBuffer ───────────────
        {
            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            VertexPositionColor verts[6];
            fillQuad(verts, kGreen);
            vb.SetData(verts, 0, 6);
            drawAndCheck(dev, vb, kGreen, "None VB draws green quad");
        }

        // ── E: DynamicVertexBuffer WriteOnly + all SetDataOptions ──────────
        {
            auto decl = posColorDecl();
            DynamicVertexBuffer dvb(dev, decl, 6, BufferUsage::WriteOnly);
            checkEq(static_cast<int>(dvb.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "DVB WriteOnly: getBufferUsageProperty() == WriteOnly");

            VertexPositionColor verts[6];
            // None
            fillQuad(verts, kBlue);
            dvb.SetData(verts, 0, 6, SetDataOptions::None);
            drawAndCheck(dev, dvb, kBlue, "DVB WriteOnly + None draws blue quad");
            // Discard
            fillQuad(verts, kRed);
            dvb.SetData(verts, 0, 6, SetDataOptions::Discard);
            drawAndCheck(dev, dvb, kRed, "DVB WriteOnly + Discard draws red quad");
            // NoOverwrite
            fillQuad(verts, kGreen);
            dvb.SetData(verts, 0, 6, SetDataOptions::NoOverwrite);
            drawAndCheck(dev, dvb, kGreen, "DVB WriteOnly + NoOverwrite draws green quad");
        }

        // ── F: IndexBuffer WriteOnly / None SetData ────────────────────────
        {
            IndexBuffer ibW(dev, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
            IndexBuffer ibN(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);

            std::uint16_t idx16[6] = { 0, 1, 2, 3, 4, 5 };
            ibW.SetData(idx16, 0, 6);
            check(ibW.getIndexCountProperty() == 6,
                  "IB WriteOnly: capacity unchanged after SetData16");

            std::uint32_t idx32[6] = { 0, 1, 2, 3, 4, 5 };
            ibN.SetData(idx32, 0, 6);
            check(ibN.getIndexCountProperty() == 6,
                  "IB None 32-bit: capacity unchanged after SetData32");
        }

        // ── F2: DynamicIndexBuffer WriteOnly + all SetDataOptions ──────────
        {
            DynamicIndexBuffer dib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
            checkEq(static_cast<int>(dib.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "DIB WriteOnly: getBufferUsageProperty() == WriteOnly");

            std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };
            dib.SetData(idx, 0, 6, SetDataOptions::None);
            dib.SetData(idx, 0, 6, SetDataOptions::Discard);
            dib.SetData(idx, 0, 6, SetDataOptions::NoOverwrite);
            check(dib.getIndexCountProperty() == 6,
                  "DIB WriteOnly: capacity unchanged after 3x SetData with options");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    BufferUsageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    BufferUsageTest g;
    g.Run();
    return g.getResult();
}
