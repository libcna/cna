// SPDX-License-Identifier: MS-PL
// Task 206: PrimitiveType validation — all five supported types pass through
// without throwing; an unrecognized integer cast throws InvalidOperationException.
//
// Supported types: TriangleList, TriangleStrip, LineList, LineStrip, PointListEXT.
// Unsupported:     any integer value not in the enum → InvalidOperationException
//                  ("Unrecognized primitive type!"), matching FNA's PrimitiveVerts().

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

template<typename F>
static bool throwsInvalidOp(F&& fn)
{
    try { fn(); return false; }
    catch (const System::InvalidOperationException&) { return true; }
    catch (...) { return false; }
}

// Draws one primitive of the given type via DrawUserPrimitives.
// Returns true if the call either succeeds or throws something other than
// InvalidOperationException (a GPU error is fine here — we only care that the
// PrimitiveType dispatch doesn't throw InvalidOperationException for known types).
static bool knownTypeDoesNotThrowInvalidOp(GraphicsDevice& device, PrimitiveType type)
{
    // Supply enough vertices for the worst case (TriangleList needs 3).
    VertexPositionColor verts[3]{};
    try
    {
        device.DrawUserPrimitives(type, verts, 0, 1);
        return true;
    }
    catch (const System::InvalidOperationException&) { return false; }
    catch (...) { return true; } // other errors (GPU, effect) are not our concern
}

class PrimitiveTypeValidationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();

        // Bind a BasicEffect so the effect-null guard passes.
        BasicEffect fx(device);
        fx.Apply();

        // ── Known types must NOT throw InvalidOperationException ────────────
        check(knownTypeDoesNotThrowInvalidOp(device, PrimitiveType::TriangleList),
              "TriangleList: no InvalidOperationException");
        check(knownTypeDoesNotThrowInvalidOp(device, PrimitiveType::TriangleStrip),
              "TriangleStrip: no InvalidOperationException");
        check(knownTypeDoesNotThrowInvalidOp(device, PrimitiveType::LineList),
              "LineList: no InvalidOperationException");
        check(knownTypeDoesNotThrowInvalidOp(device, PrimitiveType::LineStrip),
              "LineStrip: no InvalidOperationException");
        check(knownTypeDoesNotThrowInvalidOp(device, PrimitiveType::PointListEXT),
              "PointListEXT: no InvalidOperationException");

        // ── Unknown type must throw InvalidOperationException ───────────────
        auto bad = static_cast<PrimitiveType>(99);
        VertexPositionColor verts[3]{};
        check(throwsInvalidOp([&]{ device.DrawUserPrimitives(bad, verts, 0, 1); }),
              "Unknown PrimitiveType(99): throws InvalidOperationException");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    PrimitiveTypeValidationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    PrimitiveTypeValidationTest game;
    game.Run();
    return game.getResult();
}
