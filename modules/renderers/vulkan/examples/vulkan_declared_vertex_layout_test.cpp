// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-146 (finding F-15): the BasicEffect pipelines take their vertex
// attribute offsets from the caller's VertexDeclaration, not from the buffer's byte stride.
//
// The two declarations below are BOTH 24 bytes. The stride table has one entry for 24 -- position,
// colour, uv at 0/12/16 -- so before this row they were the same pipeline, and the second one drew
// its colour from the bytes holding its UV. They are not the same vertex:
//
//   A (canonical VertexPositionColorTexture)   position@0   colour@12  uv@16
//   B (the same semantics, moved)              position@0   uv@12      colour@20
//
// Each buffer is filled to match its OWN declaration, and each carries a colour no other leg uses,
// so a draw that read the wrong offset cannot land on the right pixel by accident: at offset 12 of
// B sit two floats of UV data, whose bytes as an R8G8B8A8_UNORM colour are nothing like the colour
// stored at 20.
//
//   A  Declaration A draws its own colour. This is the case that already worked, kept so a
//      regression in the conversion is distinguishable from the feature not landing.
//   B  Declaration B draws its own colour. Before this row the draw was REFUSED outright by the
//      declaration guard (the honest failure mode F-15 credits), and had the guard not caught it
//      the pipeline would have sampled UV bytes as a colour.
//   C  The two do not share a pipeline. Read from the renderer's own cache cardinality: binding
//      the second declaration must ADD an entry, because two different attribute layouts cannot be
//      one VkPipeline. This is the leg that fails if the layout reaches the pipeline but not its
//      key -- a defect that would otherwise show up only as whichever declaration drew first
//      winning for the rest of the process.
//   D  A buffer with NO declaration still draws. That is VertexBuffer(device, count), the
//      convenience constructor much of this suite uses, and it must keep the stride-derived layout.
//   E  A Position+Colour vertex PADDED to 32 bytes renders its colour. Stride 32 is
//      VertexPositionNormalTexture's, and the lit programs take {aPos, aNormal, aUV} with no
//      colour input at all -- so this one cannot be fixed by moving offsets, only by asking the
//      declaration which program it is. That is REMED-GFX-234's rule, which EasyGL already
//      applies; VULKAN-146 brings Vulkan to it. Before this row the draw was refused.
//   F  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;

    // A full-screen quad's six corners, in the winding the rest of this suite uses with CullNone.
    struct Corner { float x, y; float u, v; };
    constexpr Corner kQuad[6] = {
        { -1.f,  1.f, 0.f, 0.f }, { -1.f, -1.f, 0.f, 1.f }, {  1.f, -1.f, 1.f, 1.f },
        { -1.f,  1.f, 0.f, 0.f }, {  1.f, -1.f, 1.f, 1.f }, {  1.f,  1.f, 1.f, 0.f },
    };

    void PutFloat3(std::uint8_t* at, float a, float b, float c)
    {
        const float v[3] = { a, b, c };
        std::memcpy(at, v, sizeof(v));
    }
    void PutFloat2(std::uint8_t* at, float a, float b)
    {
        const float v[2] = { a, b };
        std::memcpy(at, v, sizeof(v));
    }
    void PutColor(std::uint8_t* at, const Color& c)
    {
        at[0] = c.getRProperty(); at[1] = c.getGProperty();
        at[2] = c.getBProperty(); at[3] = c.getAProperty();
    }
}

class VulkanDeclaredVertexLayoutTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    /// Draws `vb` full-screen through BasicEffect's stride-24 colour+texture path and returns the
    /// centre pixel. A white texture, so the pixel is the vertex colour and nothing else.
    Color DrawAndRead(GraphicsDevice& dev, VertexBuffer& vb)
    {
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setTextureProperty(white_.get());
        fx.setTextureEnabledProperty(true);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    static bool Matches(const Color& got, const Color& want)
    {
        const int tol = 12;
        return std::abs(int(got.getRProperty()) - int(want.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(want.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(want.getBProperty())) <= tol;
    }

    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        white_ = std::make_unique<Texture2D>(dev, 1, 1);
        Color w(255, 255, 255, 255);
        white_->SetData(&w, 1);

        // ---- A: the canonical 24-byte layout ------------------------------------------------
        const Color colourA(220, 40, 60, 255);
        VertexDeclaration declA(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        std::vector<std::uint8_t> bytesA(6 * 24, 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytesA.data() + i * 24;
            PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
            PutColor (v + 12, colourA);
            PutFloat2(v + 16, kQuad[i].u, kQuad[i].v);
        }
        VertexBuffer vbA(dev, declA, 6, BufferUsage::None);
        vbA.SetDataRaw(bytesA.data(), 6, 24);

        const std::size_t pipesBeforeA = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        const Color gotA = DrawAndRead(dev, vbA);
        const std::size_t pipesAfterA = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        check(Matches(gotA, colourA), "A the canonical 24-byte declaration draws its own colour",
              Show(gotA) + " expected " + Show(colourA));

        // ---- B: the same semantics, at different offsets, same stride -------------------------
        const Color colourB(30, 200, 90, 255);
        VertexDeclaration declB(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
        });
        std::vector<std::uint8_t> bytesB(6 * 24, 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytesB.data() + i * 24;
            PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
            PutFloat2(v + 12, kQuad[i].u, kQuad[i].v);
            PutColor (v + 20, colourB);
        }
        VertexBuffer vbB(dev, declB, 6, BufferUsage::None);
        vbB.SetDataRaw(bytesB.data(), 6, 24);

        bool refused = false;
        std::string how;
        Color gotB(0, 0, 0, 0);
        try {
            gotB = DrawAndRead(dev, vbB);
        } catch (const std::exception& e) {
            refused = true;
            how = std::string("refused: ") + e.what();
        }
        const std::size_t pipesAfterB = Renderer().GetGraphicsPipelineCacheEntryCountEXT();

        check(!refused && Matches(gotB, colourB),
              "B a 24-byte declaration with colour at offset 20 draws its own colour",
              refused ? how : (Show(gotB) + " expected " + Show(colourB)));

        // ---- C: and it did not do so by reusing A's pipeline ----------------------------------
        check(pipesAfterB > pipesAfterA,
              "C the second declaration got a pipeline of its own",
              std::to_string(pipesBeforeA) + " -> " + std::to_string(pipesAfterA) + " -> "
                  + std::to_string(pipesAfterB) + " cache entries");

        // ---- D: a buffer with no declaration still draws --------------------------------------
        // VertexBuffer(device, count) has an intentionally empty declaration, so the factory must
        // keep its stride-derived layout. Same bytes as A, which is what stride 24 means there.
        const Color colourD(70, 90, 240, 255);
        std::vector<std::uint8_t> bytesD(6 * 24, 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytesD.data() + i * 24;
            PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
            PutColor (v + 12, colourD);
            PutFloat2(v + 16, kQuad[i].u, kQuad[i].v);
        }
        VertexBuffer vbD(dev, 6);
        vbD.SetDataRaw(bytesD.data(), 6, 24);
        const Color gotD = DrawAndRead(dev, vbD);
        check(Matches(gotD, colourD),
              "D a buffer with no declaration keeps the stride-derived layout",
              Show(gotD) + " expected " + Show(colourD));

        // ---- E: Position+Colour padded to 32, which stride 32 alone reads as a lit vertex ----
        const Color colourE(240, 170, 20, 255);
        VertexDeclaration declE(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
        });
        std::vector<std::uint8_t> bytesE(6 * 32, 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytesE.data() + i * 32;
            PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
            PutColor (v + 12, colourE);
        }
        VertexBuffer vbE(dev, declE, 6, BufferUsage::None);
        vbE.SetDataRaw(bytesE.data(), 6, 32);
        bool refusedE = false;
        std::string howE;
        Color gotE(0, 0, 0, 0);
        try {
            gotE = DrawAndRead(dev, vbE);
        } catch (const std::exception& e) {
            refusedE = true;
            howE = std::string("refused: ") + e.what();
        }
        check(!refusedE && Matches(gotE, colourE),
              "E Position+Colour padded to stride 32 renders its colour, not a lit vertex",
              refusedE ? howE : (Show(gotE) + " expected " + Show(colourE)));

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "F no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanDeclaredVertexLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDeclaredVertexLayoutTest g;
    g.Run();
    return g.getResult();
}
