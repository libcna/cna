// SPDX-License-Identifier: MS-PL
// Task 240: Verify that disposed VertexBuffer / IndexBuffer cannot be
// rebound or updated — all such attempts must throw ObjectDisposedException.

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
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DisposedBufferTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template<class Fn>
    bool throws(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const System::ObjectDisposedException&) { return true; }
        catch (...) { return false; }
    }

    static VertexDeclaration posColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        VertexPositionColor verts[3] = {};
        std::uint16_t idx16[3] = { 0, 1, 2 };
        std::uint32_t idx32[3] = { 0, 1, 2 };

        // ── VertexBuffer: SetData after Dispose ───────────────────────────
        {
            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, 3, BufferUsage::None);
            vb.Dispose();

            check(throws([&]{ vb.SetData(verts, 3); }),
                  "VB disposed: SetData(VPC*, count) throws");
            check(throws([&]{ vb.SetData(verts, 0, 3); }),
                  "VB disposed: SetData(VPC*, startIndex, count) throws");
            check(throws([&]{ vb.SetDataRaw(verts, 3, 16); }),
                  "VB disposed: SetDataRaw throws");
        }

        // ── DynamicVertexBuffer: SetData with options after Dispose ───────
        {
            auto decl = posColorDecl();
            DynamicVertexBuffer dvb(dev, decl, 3, BufferUsage::None);
            dvb.Dispose();

            check(throws([&]{ dvb.SetData(verts, 0, 3, SetDataOptions::None); }),
                  "DVB disposed: SetData(None) throws");
            check(throws([&]{ dvb.SetData(verts, 0, 3, SetDataOptions::Discard); }),
                  "DVB disposed: SetData(Discard) throws");
            check(throws([&]{ dvb.SetData(verts, 0, 3, SetDataOptions::NoOverwrite); }),
                  "DVB disposed: SetData(NoOverwrite) throws");
        }

        // ── GraphicsDevice::SetVertexBuffer after Dispose ─────────────────
        {
            auto decl = posColorDecl();
            VertexBuffer vb(dev, decl, 3, BufferUsage::None);
            vb.Dispose();

            check(throws([&]{ dev.SetVertexBuffer(&vb); }),
                  "SetVertexBuffer(disposed) throws");
        }

        // ── IndexBuffer (16-bit): SetData after Dispose ───────────────────
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.Dispose();

            check(throws([&]{ ib.SetData(idx16, 3); }),
                  "IB disposed: SetData(uint16*, count) throws");
            check(throws([&]{ ib.SetData(idx16, 0, 3); }),
                  "IB disposed: SetData(uint16*, startIndex, count) throws");
        }

        // ── IndexBuffer (32-bit): SetData after Dispose ───────────────────
        {
            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
            ib.Dispose();

            check(throws([&]{ ib.SetData(idx32, 3); }),
                  "IB disposed: SetData(uint32*, count) throws");
            check(throws([&]{ ib.SetData(idx32, 0, 3); }),
                  "IB disposed: SetData(uint32*, startIndex, count) throws");
        }

        // ── DynamicIndexBuffer: SetData with options after Dispose ────────
        {
            DynamicIndexBuffer dib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            dib.Dispose();

            check(throws([&]{ dib.SetData(idx16, 0, 3, SetDataOptions::None); }),
                  "DIB disposed: SetData(None) throws");
            check(throws([&]{ dib.SetData(idx16, 0, 3, SetDataOptions::Discard); }),
                  "DIB disposed: SetData(Discard) throws");
            check(throws([&]{ dib.SetData(idx16, 0, 3, SetDataOptions::NoOverwrite); }),
                  "DIB disposed: SetData(NoOverwrite) throws");
        }

        // ── GraphicsDevice::SetIndexBuffer after Dispose ──────────────────
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.Dispose();

            check(throws([&]{ dev.SetIndexBuffer(&ib); }),
                  "SetIndexBuffer(disposed) throws");
        }

        // ── SetVertexBuffer(nullptr) must NOT throw ────────────────────────
        {
            bool ok = true;
            try { dev.SetVertexBuffer(nullptr); }
            catch (...) { ok = false; }
            check(ok, "SetVertexBuffer(nullptr) does not throw");
        }

        // ── SetIndexBuffer(nullptr) must NOT throw ─────────────────────────
        {
            bool ok = true;
            try { dev.SetIndexBuffer(nullptr); }
            catch (...) { ok = false; }
            check(ok, "SetIndexBuffer(nullptr) does not throw");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DisposedBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DisposedBufferTest g;
    g.Run();
    return g.getResult();
}
