// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-137 -- a VertexBuffer or IndexBuffer of ZERO elements.
//
// `vkCreateBuffer` rejects a size of 0, so this renderer allocates `max(1, size)` for an empty
// buffer. That is a real divergence in the implementation from EasyGL, which hands the size
// straight to `glBufferData`, and it had no test on either side. The divergence is only legitimate
// if it is invisible from the public API, which is what this file measures -- so it is written
// renderer-agnostic and registered on Vulkan AND on EasyGL, and the two must agree line for line.
//
// The half that cannot be asserted from inside the process is the interesting half: the padded
// allocation must not produce a Vulkan validation message, and a message emitted at buffer
// destruction arrives after this program's last statement. That is what the VULKAN-408 CTest output
// gate is for -- `Vulkan_ZeroLengthBuffers` fails on any `[Vulkan Validation]` line, whenever it is
// printed. Running the binary by hand proves the pixels; running it through ctest proves the rest.
//
// Legs:
//   A  A zero-element VertexBuffer constructs and reports a count of zero.
//   B  Binding it, and unbinding it, leaves the device able to draw normally afterwards -- the
//      leg that would catch a padded allocation being bound as if it held a vertex.
//   C  The same for a zero-element IndexBuffer.
//   D  Both dispose cleanly while the device is still alive, and a draw after that still works.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int kSize = 32;
const Color kQuad(220, 40, 40, 255);
const Color kClear(0, 0, 0, 255);
}  // namespace

class ZeroLengthBufferTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static const VertexDeclaration& Decl()
    {
        static const VertexDeclaration decl(
            static_cast<int>(sizeof(VertexPositionColor)),
            { VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
              VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0) });
        return decl;
    }

    /// Draws a full-screen quad from a real buffer and returns the centre pixel. This is the
    /// "can the device still draw" probe every leg ends with.
    Color DrawControlQuad()
    {
        auto& dev = getGraphicsDeviceProperty();
        const VertexPositionColor tri[6] = {
            { Vector3(-1.f,  1.f, 0.f), kQuad }, { Vector3( 1.f,  1.f, 0.f), kQuad },
            { Vector3(-1.f, -1.f, 0.f), kQuad }, { Vector3( 1.f,  1.f, 0.f), kQuad },
            { Vector3( 1.f, -1.f, 0.f), kQuad }, { Vector3(-1.f, -1.f, 0.f), kQuad },
        };
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(kClear);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 2);

        Color got(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &got, 0, 1);
        return got;
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // A. Construction. XNA rejects a NEGATIVE count and accepts zero, so an empty buffer is a
        //    legal object whose count is the count it was given -- not the 1 the allocation pads to.
        std::unique_ptr<VertexBuffer> emptyVB;
        try {
            emptyVB = std::make_unique<VertexBuffer>(dev, Decl(), 0, BufferUsage::None);
            check(emptyVB->getVertexCountProperty() == 0,
                  "A a zero-element VertexBuffer constructs and reports VertexCount=" +
                      std::to_string(emptyVB->getVertexCountProperty()) + " (want 0)");
        } catch (const std::exception& e) {
            check(false, std::string("A a zero-element VertexBuffer must construct, but threw: ") +
                             e.what());
        }

        // B. Bound, then unbound, then an ordinary draw. A padded allocation bound as if it held a
        //    vertex would show up here rather than in the constructor.
        if (emptyVB) {
            bool threw = false;
            try {
                dev.SetVertexBuffer(emptyVB.get());
                dev.SetVertexBuffer(nullptr);
            } catch (const std::exception&) { threw = true; }
            const Color got = DrawControlQuad();
            check(!threw && Is(got, kQuad),
                  "B binding and unbinding an empty VertexBuffer leaves the device drawable: "
                  "threw=" + std::string(threw ? "yes" : "no") + " centre=" + Text(got) +
                      " (want " + Text(kQuad) + ")");
        }

        // C. The same for an IndexBuffer, whose zero-size allocation takes the same max(1, ...)
        //    path and whose bind is a separate command.
        std::unique_ptr<IndexBuffer> emptyIB;
        {
            bool threw = false;
            std::string what;
            try {
                emptyIB = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 0,
                                                        BufferUsage::None);
                dev.setIndicesProperty(emptyIB.get());
                dev.setIndicesProperty(nullptr);
            } catch (const std::exception& e) { threw = true; what = e.what(); }
            const Color got = DrawControlQuad();
            check(!threw && emptyIB && emptyIB->getIndexCountProperty() == 0 && Is(got, kQuad),
                  "C a zero-element IndexBuffer constructs, binds and unbinds: threw=" +
                      (threw ? what : std::string("no")) + " IndexCount=" +
                      std::to_string(emptyIB ? emptyIB->getIndexCountProperty() : -1) +
                      " centre=" + Text(got));
        }

        // D. Disposal while the device is alive, and a draw after it. The empty buffers' padded
        //    allocations have to be released like any other -- on Vulkan that means they enter the
        //    same retirement lists, and anything wrong there is reported at vkDestroyDevice, after
        //    this program's last line. That half belongs to the CTest output gate, not to this
        //    assertion, which only establishes that disposal itself is clean and non-fatal.
        {
            bool threw = false;
            std::string what;
            try {
                if (emptyVB) emptyVB->Dispose();
                if (emptyIB) emptyIB->Dispose();
            } catch (const std::exception& e) { threw = true; what = e.what(); }
            const Color got = DrawControlQuad();
            check(!threw && Is(got, kQuad),
                  "D disposing both empty buffers is clean and the device still draws: threw=" +
                      (threw ? what : std::string("no")) + " centre=" + Text(got));
            emptyVB.reset();
            emptyIB.reset();
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    ZeroLengthBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    ZeroLengthBufferTest game;
    game.Run();
    return game.getResult();
}
