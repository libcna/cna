// SPDX-License-Identifier: MS-PL
// Task 216: Moved C++ resources must not double-free renderer handles.
//
// After std::move(src) into dst:
//   - src.HasRenderer() == false  (handle ownership transferred)
//   - dst.HasRenderer() == true
// When both go out of scope: only dst frees the handle (no double-free).
//
// Covered types: VertexBuffer, IndexBuffer, Texture2D (move-construct + move-assign).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdio>
#include <memory>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MoveSemanticsTest : public Game
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

        // ── VertexBuffer move-construct ────────────────────────────────────
        {
            VertexBuffer src(dev, 8);
            check(src.HasRenderer(), "VB move-construct: src.HasRenderer true before move");

            VertexBuffer dst(std::move(src));

            check(!src.HasRenderer(), "VB move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "VB move-construct: dst.HasRenderer true after move");
            // Both go out of scope here — only dst frees the handle (no double-free)
        }

        // ── VertexBuffer move-assign ───────────────────────────────────────
        {
            VertexBuffer src(dev, 4);
            VertexBuffer dst(dev, 4);    // dst already holds a handle (handle1)
            check(src.HasRenderer(), "VB move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "VB move-assign: dst.HasRenderer true before move");

            dst = std::move(src);        // handle1 freed, src's handle transferred to dst

            check(!src.HasRenderer(), "VB move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "VB move-assign: dst.HasRenderer true after move");
        }

        // ── IndexBuffer move-construct ─────────────────────────────────────
        {
            IndexBuffer src(dev, 12);
            check(src.HasRenderer(), "IB move-construct: src.HasRenderer true before move");

            IndexBuffer dst(std::move(src));

            check(!src.HasRenderer(), "IB move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "IB move-construct: dst.HasRenderer true after move");
        }

        // ── IndexBuffer move-assign ────────────────────────────────────────
        {
            IndexBuffer src(dev, 6);
            IndexBuffer dst(dev, 6);
            check(src.HasRenderer(), "IB move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "IB move-assign: dst.HasRenderer true before move");

            dst = std::move(src);

            check(!src.HasRenderer(), "IB move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "IB move-assign: dst.HasRenderer true after move");
        }

        // ── Texture2D move-construct ───────────────────────────────────────
        {
            Texture2D src(dev, 4, 4);
            check(src.HasRenderer(), "Tex2D move-construct: src.HasRenderer true before move");

            Texture2D dst(std::move(src));

            check(!src.HasRenderer(), "Tex2D move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "Tex2D move-construct: dst.HasRenderer true after move");
        }

        // ── Texture2D move-assign ──────────────────────────────────────────
        {
            Texture2D src(dev, 2, 2);
            Texture2D dst(dev, 2, 2);
            check(src.HasRenderer(), "Tex2D move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "Tex2D move-assign: dst.HasRenderer true before move");

            dst = std::move(src);

            check(!src.HasRenderer(), "Tex2D move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "Tex2D move-assign: dst.HasRenderer true after move");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    MoveSemanticsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    MoveSemanticsTest game;
    game.Run();
    return game.getResult();
}
