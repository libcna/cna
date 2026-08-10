// SPDX-License-Identifier: MS-PL
// Task 213: Double Dispose() on all major resource types must not crash.
//
// For every GraphicsResource-derived type, calls Dispose() twice and verifies:
//   (a) no exception or crash on the second call
//   (b) getIsDisposedProperty() returns true afterwards
//
// Types tested: BlendState, DepthStencilState, RasterizerState, SamplerState,
//               VertexDeclaration, VertexBuffer, IndexBuffer, Texture2D,
//               RenderTarget2D, BasicEffect, SpriteBatch.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DoubleDisposeTest : public Game
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

    // Returns true if double-Dispose does not throw.
    static bool doubleDispose(System::IDisposable& res)
    {
        try
        {
            res.Dispose();
            res.Dispose();
            return true;
        }
        catch (...) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── State objects (no device at construction) ─────────────────────
        {
            BlendState bs;
            check(doubleDispose(bs),                    "BlendState: double-Dispose does not crash");
            check(bs.getIsDisposedProperty(),           "BlendState: IsDisposed after double-Dispose");
        }
        {
            DepthStencilState dss;
            check(doubleDispose(dss),                   "DepthStencilState: double-Dispose does not crash");
            check(dss.getIsDisposedProperty(),          "DepthStencilState: IsDisposed after double-Dispose");
        }
        {
            RasterizerState rs;
            check(doubleDispose(rs),                    "RasterizerState: double-Dispose does not crash");
            check(rs.getIsDisposedProperty(),           "RasterizerState: IsDisposed after double-Dispose");
        }
        {
            SamplerState ss;
            check(doubleDispose(ss),                    "SamplerState: double-Dispose does not crash");
            check(ss.getIsDisposedProperty(),           "SamplerState: IsDisposed after double-Dispose");
        }
        {
            VertexDeclaration vd;
            check(doubleDispose(vd),                    "VertexDeclaration: double-Dispose does not crash");
            check(vd.getIsDisposedProperty(),           "VertexDeclaration: IsDisposed after double-Dispose");
        }

        // ── Resources that require a graphics device ──────────────────────
        {
            VertexBuffer vb(dev, 4);
            check(doubleDispose(vb),                    "VertexBuffer: double-Dispose does not crash");
            check(vb.getIsDisposedProperty(),           "VertexBuffer: IsDisposed after double-Dispose");
        }
        {
            IndexBuffer ib(dev, 4);
            check(doubleDispose(ib),                    "IndexBuffer: double-Dispose does not crash");
            check(ib.getIsDisposedProperty(),           "IndexBuffer: IsDisposed after double-Dispose");
        }
        {
            Texture2D tex(dev, 1, 1);
            check(doubleDispose(tex),                   "Texture2D: double-Dispose does not crash");
            check(tex.getIsDisposedProperty(),          "Texture2D: IsDisposed after double-Dispose");
        }
        {
            RenderTarget2D rt(dev, 4, 4);
            check(doubleDispose(rt),                    "RenderTarget2D: double-Dispose does not crash");
            check(rt.getIsDisposedProperty(),           "RenderTarget2D: IsDisposed after double-Dispose");
        }
        {
            BasicEffect fx(dev);
            check(doubleDispose(fx),                    "BasicEffect: double-Dispose does not crash");
            check(fx.getIsDisposedProperty(),           "BasicEffect: IsDisposed after double-Dispose");
        }
        {
            SpriteBatch sb(dev);
            check(doubleDispose(sb),                    "SpriteBatch: double-Dispose does not crash");
            check(sb.getIsDisposedProperty(),           "SpriteBatch: IsDisposed after double-Dispose");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DoubleDisposeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DoubleDisposeTest game;
    game.Run();
    return game.getResult();
}
