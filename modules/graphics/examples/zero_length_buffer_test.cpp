// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-137 -- a VertexBuffer or IndexBuffer of ZERO elements.
//
// `vkCreateBuffer` rejects a size of 0, so this renderer allocates `max(1, size)` for an empty
// buffer, and this file was written to prove that padding invisible from the public API.
//
// plans/plan_vulkan_parity.md VKPAR-0022: the premise changed underneath it. Microsoft XNA rejects
// a zero count, not only a negative one -- `vertexCount <= 0` and `indexCount <= 0` in every
// constructor (plans/plan_software.md SOFTWARE-204) -- so an empty buffer is no longer a legal
// object and the padding is unreachable through the public API. What is left to prove, on every
// renderer this is registered for, is that the refusal comes first: XNA's exception, raised before
// any native allocation, with the device still drawing afterwards. The CTest output gate still
// fails the run on any `[Vulkan Validation]` line, which is what shows nothing native was touched.
//
// Legs:
//   A  A zero-element VertexBuffer is refused with ArgumentOutOfRangeException, naming the count,
//      and the device still draws.
//   C  The same for a zero-element IndexBuffer.
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

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

        // A. Construction is refused, by XNA's exception and before any allocation.
        {
            bool refused = false;
            std::string what = "no exception";
            try {
                VertexBuffer emptyVB(dev, Decl(), 0, BufferUsage::None);
            } catch (const System::ArgumentOutOfRangeException& e) {
                refused = true; what = e.what();
            } catch (const std::exception& e) {
                what = std::string("wrong type: ") + e.what();
            }
            const Color got = DrawControlQuad();
            check(refused && what.find("vertexCount") != std::string::npos && Is(got, kQuad),
                  "A a zero-element VertexBuffer is refused by name and the device still draws: " +
                      what + " centre=" + Text(got));
        }

        // C. The same for an IndexBuffer.
        {
            bool refused = false;
            std::string what = "no exception";
            try {
                IndexBuffer emptyIB(dev, IndexElementSize::SixteenBits, 0, BufferUsage::None);
            } catch (const System::ArgumentOutOfRangeException& e) {
                refused = true; what = e.what();
            } catch (const std::exception& e) {
                what = std::string("wrong type: ") + e.what();
            }
            const Color got = DrawControlQuad();
            check(refused && what.find("indexCount") != std::string::npos && Is(got, kQuad),
                  "C a zero-element IndexBuffer is refused by name and the device still draws: " +
                      what + " centre=" + Text(got));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    ZeroLengthBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // VKPAR-0022: GetBackBufferData, which every leg's probe uses, is HiDef-only.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
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
