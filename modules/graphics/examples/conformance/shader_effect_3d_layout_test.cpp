// SPDX-License-Identifier: MS-PL
// A ShaderEffect on the 3D draw routes takes its vertex inputs from the caller's
// VertexDeclarations -- one stream or several, per-vertex or per-instance -- and refuses, by name,
// a draw whose inputs it cannot find.
//
// Moved from the retired VULKAN suite (VULKAN-255, VULKAN-168). The shaders are the portable
// package common/shaders/shader_effect_3d (SPIR-V and WGSL generated from one Vulkan GLSL source by
// tools/shader_package/generate_shader_package.py), so the program runs wherever selectFor() finds
// a variant. Vertex input locations follow declaration element order, continuing across streams.
//
// The layout under test, stride 48 (no stock vertex type has it):
//   offset 0 Position Vector3, 12 Normal Vector3, 24 Tangent Vector3, 36 TextureCoordinate Vector2,
//   44 Color Color
// The shader writes one attribute per output channel -- R Normal.x, G Tangent.y, B Color.r,
// A TextureCoordinate.x -- so a wrong offset is one wrong channel. The quads are already in clip
// space and the effect's World/View/Projection are identity: an undelivered matrix collapses
// every quad and the legs read the clear colour.
//
//   A  the custom shader drives DrawPrimitives and reads each attribute from its own offset
//   B  a second buffer with other values gives another pixel (the inputs are really read)
//   C  DrawIndexedPrimitives reaches the same program
//   D  the five attributes split across two vertex streams, non-indexed
//   E  the same split on the indexed route
//   F  a vertex buffer with no VertexDeclaration is refused, naming the VertexDeclaration
//   G  DrawInstancedPrimitives feeds each instance its own per-instance record through the custom
//      shader; an extra declared instance element the shader does not read is harmless
//   H  a shader input absent from every active declaration is refused, naming its location
//   I  DrawInstancedPrimitivesBaseInstanceEXT refuses a negative first instance
//   J  ... and with first instance 1 draws instance one, not instance zero
//
// F and H count only a refusal thrown by the draw call itself: a draw that returns normally has
// told the caller it succeeded, whatever a later call throws. They run last, because a renderer
// that queues such a draw keeps rethrowing from every later flush.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display, CNA_CNAEXT off, or no package variant
// this renderer accepts). Legs D-J skip individually, with a note, when the renderer reports no
// multi-stream input, no instancing or no base-instance drawing.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "common/ShaderEffect3DShaderPackage.generated.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;

namespace
{
    constexpr int kN = 8;
    const Color kClear(0, 255, 0, 255);

#pragma pack(push, 1)
    struct CustomVertex
    {
        float px, py, pz;          // offset  0  Position
        float nx, ny, nz;          // offset 12  Normal
        float tx, ty, tz;          // offset 24  Tangent
        float u, v;                // offset 36  TextureCoordinate
        std::uint8_t r, g, b, a;   // offset 44  Color
    };
#pragma pack(pop)
    static_assert(sizeof(CustomVertex) == 48);

    struct SplitPositionNormal
    {
        float px, py, pz;
        float nx, ny, nz;
    };
    static_assert(sizeof(SplitPositionNormal) == 24);

#pragma pack(push, 1)
    struct SplitTangentTextureColor
    {
        float tx, ty, tz;
        float u, v;
        std::uint8_t r, g, b, a;
    };
#pragma pack(pop)
    static_assert(sizeof(SplitTangentTextureColor) == 24);

    template <std::size_t N>
    std::vector<std::uint8_t> Bytes(const std::uint32_t (&words)[N])
    {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
        return std::vector<std::uint8_t>(begin, begin + sizeof(words));
    }

    // A vertex/fragment package in every language the generated header carries.
    template <std::size_t N>
    ShaderPackageEXT MakePackage(const std::uint32_t (&vertexSpirV)[N], std::string_view vertexWgsl,
                                 const char* vertexLabel)
    {
        namespace P = CNA::Examples::ShaderEffect3D;
        using CNA::ShaderLanguageEXT;
        using CNA::ShaderStageEXT;
        return ShaderPackageEXT(
            {
                ShaderCodeEXT(ShaderLanguageEXT::SpirV, ShaderStageEXT::Vertex, "main", vertexLabel,
                              Bytes(vertexSpirV)),
                ShaderCodeEXT(ShaderLanguageEXT::Wgsl, ShaderStageEXT::Vertex, "main", vertexLabel,
                              std::string(vertexWgsl)),
                ShaderCodeEXT(ShaderLanguageEXT::SpirV, ShaderStageEXT::Fragment, "main",
                              "probe.frag.glsl", Bytes(P::kProbeFragmentSpirV)),
                ShaderCodeEXT(ShaderLanguageEXT::Wgsl, ShaderStageEXT::Fragment, "main",
                              "probe.frag.glsl", std::string(P::kProbeFragmentWgsl)),
            },
            {ShaderStageEXT::Vertex, ShaderStageEXT::Fragment});
    }

    std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    // Within one 8-bit step, which is all a float-to-unorm round trip promises.
    bool Near(int got, int want) { return got >= want - 1 && got <= want + 1; }

    std::string Refused(const std::string& refusal)
    {
        return refusal.empty() ? std::string() : "; REFUSED: " + refusal;
    }

    bool NearRGBA(const Color& c, int r, int g, int b, int a)
    {
        return Near(c.getRProperty(), r) && Near(c.getGProperty(), g) &&
               Near(c.getBProperty(), b) && Near(c.getAProperty(), a);
    }
}

class ShaderEffect3DLayoutTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> fx_;
    std::unique_ptr<IndexBuffer> ib_;
    bool skipped_ = false;

    static VertexDeclaration Declaration()
    {
        return VertexDeclaration(48, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(36, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(44, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    // A clip-space quad whose four vertices carry the same probe values, so the centre texel
    // reads them back without interpolation entering the answer.
    std::unique_ptr<VertexBuffer> MakeQuad(float nx, float ty, std::uint8_t cr, float u)
    {
        auto vb = std::make_unique<VertexBuffer>(getGraphicsDeviceProperty(), Declaration(), 4,
                                                 BufferUsage::None);
        const CustomVertex v[4] = {
            { -1.f,  1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            { -1.f, -1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            {  1.f, -1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            {  1.f,  1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
        };
        vb->SetDataRaw(v, 4, sizeof(CustomVertex));
        return vb;
    }

    std::vector<Color> ReadTarget(RenderTarget2D& rt)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, kN * kN);
        return pixels;
    }

    // Binds `bindings`, draws the quad indexed or not, and returns the target's centre texel. A
    // refused draw leaves its message in `refusal` and the device unbound, as before the call.
    Color DrawQuad(const std::vector<VertexBufferBinding>& bindings, bool indexed,
                   std::string& refusal)
    {
        auto& dev = getGraphicsDeviceProperty();
        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        fx_->Apply();
        if (bindings.size() == 1)
            dev.SetVertexBuffer(bindings.front().getVertexBufferProperty());
        else
            dev.SetVertexBuffers(bindings);
        try
        {
            if (indexed)
            {
                dev.setIndicesProperty(ib_.get());
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            }
            else
            {
                dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            }
        }
        catch (const std::exception& e)
        {
            refusal = e.what();
        }
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget(rt)[kN * kN / 2];
    }

    Color DrawSplitQuad(bool indexed, std::string& refusal)
    {
        auto& dev = getGraphicsDeviceProperty();
        const VertexDeclaration positionNormal(24, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        });
        const VertexDeclaration tangentTextureColor(24, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        VertexBuffer a(dev, positionNormal, 4, BufferUsage::None);
        VertexBuffer b(dev, tangentTextureColor, 4, BufferUsage::None);
        const SplitPositionNormal av[4] = {
            { -1.f,  1.f, 0.f, 0.2f, 0.f, 0.f },
            { -1.f, -1.f, 0.f, 0.2f, 0.f, 0.f },
            {  1.f, -1.f, 0.f, 0.2f, 0.f, 0.f },
            {  1.f,  1.f, 0.f, 0.2f, 0.f, 0.f },
        };
        const SplitTangentTextureColor bv[4] = {
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
        };
        a.SetDataRaw(av, 4, sizeof(SplitPositionNormal));
        b.SetDataRaw(bv, 4, sizeof(SplitTangentTextureColor));
        return DrawQuad({ VertexBufferBinding(&a, 0, 0), VertexBufferBinding(&b, 0, 0) }, indexed,
                        refusal);
    }

    // Binds `rt`, runs `bindAndDraw`, then unbinds. A refusal counts only when the draw call itself
    // throws it: a renderer that queues the draw and throws from a later, unrelated call has
    // already told the caller the draw succeeded.
    void ExpectDrawRefused(const std::string& label, const std::string& needle, RenderTarget2D& rt,
                           const std::function<void()>& bindAndDraw)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        bool refused = false;
        std::string what;
        std::string late;
        // XNA's own refusal of a draw whose declaration lacks a shader input is an
        // InvalidOperationException; CNA's renderers use NotSupportedException. Either is a refusal.
        try { bindAndDraw(); }
        catch (const System::NotSupportedException& e) { refused = true; what = e.what(); }
        catch (const System::InvalidOperationException& e) { refused = true; what = e.what(); }
        catch (const std::exception& e) { what = std::string("the wrong exception type: ") + e.what(); }
        try
        {
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        catch (const std::exception& e)
        {
            late = e.what();
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        }
        std::string detail = what.empty() ? std::string("the draw call returned normally") : what;
        if (!late.empty())
            detail += "; the refusal surfaced only afterwards, from unbinding the target: " + late;
        Check(refused && what.find(needle) != std::string::npos && late.empty(),
              (label + ": " + detail).c_str());
    }

    void RunInstancingLegs(GraphicsDevice& dev)
    {
        namespace P = CNA::Examples::ShaderEffect3D;
        ShaderEffect instFx(dev, MakePackage(
                                     P::kInstancedVertexSpirV, P::kInstancedVertexWgsl,
                                     "instanced.vert.glsl"));
        instFx.setWorldProperty(Matrix::getIdentityProperty());
        instFx.setViewProperty(Matrix::getIdentityProperty());
        instFx.setProjectionProperty(Matrix::getIdentityProperty());

        const VertexDeclaration meshDecl(12, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        VertexBuffer meshVb(dev, meshDecl, 4, BufferUsage::None);
        const float meshData[12] = { -1.f,  1.f, 0.f,
                                     -1.f, -1.f, 0.f,
                                      1.f, -1.f, 0.f,
                                      1.f,  1.f, 0.f };
        meshVb.SetDataRaw(meshData, 4, 12);
        // The shader reads the FIRST instance element only; the second is declared and unread.
        const VertexDeclaration instDecl(32, {
            VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
        });
        VertexBuffer instVb(dev, instDecl, 2, BufferUsage::None);
        // instance 0: left half, probe (0.2, 0.8); instance 1: right half, probe (0.8, 0.2)
        const float instData[16] = {
            -0.5f, 0.0f, 0.2f, 0.8f,  11.f, 12.f, 13.f, 14.f,
             0.5f, 0.0f, 0.8f, 0.2f,  21.f, 22.f, 23.f, 24.f,
        };
        instVb.SetDataRaw(instData, 2, 32);

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<VertexBufferBinding> both{ VertexBufferBinding(&meshVb, 0, 0),
                                                     VertexBufferBinding(&instVb, 0, 1) };

        // ---- G ----------------------------------------------------------------------------------
        {
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            instFx.Apply();
            dev.SetVertexBuffers(both);
            dev.setIndicesProperty(ib_.get());
            std::string what;
            bool threw = false;
            try { dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 2); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const auto p = ReadTarget(rt);
            const Color left = p[kN * (kN / 2) + 2];
            const Color right = p[kN * (kN / 2) + 5];
            Check(!threw && Near(left.getRProperty(), 51) && Near(left.getGProperty(), 204) &&
                      Near(right.getRProperty(), 204) && Near(right.getGProperty(), 51),
                  ("G each instance reads its own per-instance record through the ShaderEffect: left=" +
                   Text(left) + " right=" + Text(right) + " (want ~(51,204,0) and ~(204,51,0))" +
                   (threw ? "; threw: " + what : std::string())).c_str());
        }

        // ---- I / J ------------------------------------------------------------------------------
        if (dev.SupportsRendererFeatureEXT(CNA::RendererFeature::BaseInstanceDrawing))
            RunBaseInstanceLegs(dev, instFx, rt, both);
        else
            std::printf("[NOTE] I/J not run: this renderer reports no base-instance drawing\n");

        // ---- H ----------------------------------------------------------------------------------
        ExpectDrawRefused("H a shader input no active declaration supplies is refused by location",
                          "location 1", rt, [&] {
                              instFx.Apply();
                              dev.SetVertexBuffer(&meshVb);
                              dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                          });
    }

    void RunBaseInstanceLegs(GraphicsDevice& dev, ShaderEffect& instFx, RenderTarget2D& rt,
                             const std::vector<VertexBufferBinding>& both)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        instFx.Apply();
        dev.SetVertexBuffers(both);
        dev.setIndicesProperty(ib_.get());
        bool negativeRefused = false;
        std::string negativeWhat = "NOT REFUSED";
        try
        {
            dev.DrawInstancedPrimitivesBaseInstanceEXT(PrimitiveType::TriangleList, 0, 0, 4, 0, 2,
                                                       1, -1);
        }
        catch (const System::ArgumentOutOfRangeException& e) { negativeRefused = true; negativeWhat = e.what(); }
        catch (const std::exception& e) { negativeWhat = std::string("the wrong exception type: ") + e.what(); }
        Check(negativeRefused, ("I a negative first instance is refused with "
                                "ArgumentOutOfRangeException: " + negativeWhat).c_str());

        bool threw = false;
        std::string what;
        try
        {
            dev.DrawInstancedPrimitivesBaseInstanceEXT(PrimitiveType::TriangleList, 0, 0, 4, 0, 2,
                                                       1, 1);
        }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto p = ReadTarget(rt);
        const Color baseLeft = p[kN * (kN / 2) + 2];
        const Color baseRight = p[kN * (kN / 2) + 5];
        Check(!threw && baseLeft == kClear && Near(baseRight.getRProperty(), 204) &&
                  Near(baseRight.getGProperty(), 51),
              ("J one instance from first instance 1 skips instance zero and draws instance one: "
               "left=" + Text(baseLeft) + " right=" + Text(baseRight) +
               " (want the clear colour and ~(204,51,0))" +
               (threw ? "; threw: " + what : std::string())).c_str());
    }

public:
    ShaderEffect3DLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        namespace P = CNA::Examples::ShaderEffect3D;
        auto& dev = getGraphicsDeviceProperty();
        const ShaderPackageEXT package = MakePackage(
            P::kCustomLayoutVertexSpirV, P::kCustomLayoutVertexWgsl, "custom_layout.vert.glsl");
        const auto selection = package.selectFor(dev);
        if (!selection.isUsable() || !dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("[SKIP] no 3D ShaderEffect route for this package: %s\n",
                        selection.getDiagnostic().c_str());
            skipped_ = true;
            return;
        }

        fx_ = std::make_unique<ShaderEffect>(dev, package);
        if (!Check(fx_->IsEffectValid(), ("the custom-layout ShaderEffect compiles" +
                                          (fx_->IsEffectValid() ? std::string()
                                                                : ": " + fx_->GetCompileErrorEXT()))
                                             .c_str()))
            return;
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());

        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        ib_ = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib_->SetData(indices.data(), 6);

        {
            auto vb = MakeQuad(0.2f, 0.4f, 153, 0.8f);
            std::string refusal;
            const Color got = DrawQuad({ VertexBufferBinding(vb.get()) }, false, refusal);
            Check(refusal.empty() && NearRGBA(got, 51, 102, 153, 204),
                  ("A DrawPrimitives through the ShaderEffect reads Normal.x, Tangent.y, Color.r and "
                   "TextureCoordinate.x each from its own offset: " + Text(got) +
                   " (want ~(51,102,153,204); " + Text(kClear) + " means nothing was drawn)" +
                   Refused(refusal)).c_str());
        }
        {
            auto vb = MakeQuad(0.8f, 0.6f, 51, 0.2f);
            std::string refusal;
            const Color got = DrawQuad({ VertexBufferBinding(vb.get()) }, false, refusal);
            Check(refusal.empty() && NearRGBA(got, 204, 153, 51, 51),
                  ("B other attribute values give another pixel: " + Text(got) +
                   " (want ~(204,153,51,51))" + Refused(refusal)).c_str());
        }
        {
            auto vb = MakeQuad(0.2f, 0.4f, 153, 0.8f);
            std::string refusal;
            const Color got = DrawQuad({ VertexBufferBinding(vb.get()) }, true, refusal);
            Check(refusal.empty() && NearRGBA(got, 51, 102, 153, 204),
                  ("C DrawIndexedPrimitives reaches the same program: " + Text(got) +
                   " (want ~(51,102,153,204))" + Refused(refusal)).c_str());
        }

        if (dev.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
        {
            std::string refusedD, refusedE;
            const Color d = DrawSplitQuad(false, refusedD);
            Check(refusedD.empty() && NearRGBA(d, 51, 102, 153, 204),
                  ("D two vertex streams feed one non-indexed draw: " + Text(d) +
                   " (want ~(51,102,153,204))" + Refused(refusedD)).c_str());
            const Color e = DrawSplitQuad(true, refusedE);
            Check(refusedE.empty() && NearRGBA(e, 51, 102, 153, 204),
                  ("E the same two streams feed an indexed draw: " + Text(e) +
                   " (want ~(51,102,153,204))" + Refused(refusedE)).c_str());
        }
        else
        {
            std::printf("[NOTE] D/E not run: this renderer reports no multi-stream vertex input\n");
        }

        if (dev.SupportsCapability(CNA::GraphicsCapability::Instancing) &&
            dev.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
            RunInstancingLegs(dev);
        else
            std::printf("[NOTE] G-J not run: this renderer reports no instancing\n");

        {
            // VertexBuffer(device, count) is the one constructor that carries no declaration.
            VertexBuffer bare(dev, 4);
            const CustomVertex v[4]{};
            bare.SetDataRaw(v, 4, sizeof(CustomVertex));
            RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            ExpectDrawRefused("F a buffer with no VertexDeclaration is refused by name",
                              "VertexDeclaration", rt, [&] {
                                  fx_->Apply();
                                  dev.SetVertexBuffer(&bare);
                                  dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                              });
        }

        fx_.reset();
        ib_.reset();
    }
};

int main()
{
    // Unbuffered, so the legs already decided stay on record if a later one aborts the process.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    ShaderEffect3DLayoutTest game;
    game.Run();
    return game.ExitCode();
}

#else // CNA_CNAEXT

int main()
{
    std::printf("[SKIP] ShaderEffect shader packages need CNA_CNAEXT=ON\n");
    return CNA::Examples::kSkipExitCode;
}

#endif // CNA_CNAEXT
