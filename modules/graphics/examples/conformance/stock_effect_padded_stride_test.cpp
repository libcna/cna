// SPDX-License-Identifier: MS-PL
// A stock effect fed a padded VertexDeclaration fetches every record at the declaration's own
// stride, not at the stride its canonical vertex type would have.
//
// Moved from the retired VULKAN suite (VULKAN-158). The declaration says where each element sits
// INSIDE a record; VertexStride says where the next record starts. A renderer that honours the
// offsets but binds a constant stride reads vertex 0 correctly and every later vertex from the
// middle of an earlier one, so the quad collapses. That is why every leg probes TWO widely
// separated pixels: one correct vertex is exactly the symptom.
//
//   A  EnvironmentMapEffect at stride 40 (its 32-byte Position/Normal/UV record plus 8 bytes).
//   B  EnvironmentMapEffect at stride 48 with the SAME element offsets, drawn after A in the same
//      frame. A renderer that identifies the vertex layout by its offsets alone hands B the
//      stride-40 layout, and B collapses.
//   C  PbrEffect at stride 64 (Position, Normal, Tangent, UV at padded offsets).
//
// Every quad covers the whole viewport and samples the left texel of a two-texel strip, so a
// correct draw is that texel at both probes.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display or no 3D rendering).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kLeft(230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);

    struct Corner { float x, y; };
    constexpr Corner kQuad[6] = {
        { -1.f,  1.f }, { -1.f, -1.f }, {  1.f, -1.f },
        { -1.f,  1.f }, {  1.f, -1.f }, {  1.f,  1.f },
    };

    void PutFloats(std::uint8_t* at, std::initializer_list<float> values)
    {
        std::size_t offset = 0;
        for (const float value : values)
        {
            std::memcpy(at + offset, &value, sizeof(float));
            offset += sizeof(float);
        }
    }

    // A full-viewport quad of `stride`-byte records. Position is at 0; normal, tangent and UV go
    // where the caller says (a negative offset omits one). Every other byte is padding.
    std::vector<std::uint8_t> BuildRecords(int stride, int normalOffset, int tangentOffset,
                                           int uvOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        for (int i = 0; i < 6; ++i)
        {
            std::uint8_t* v = bytes.data() + i * stride;
            PutFloats(v, { kQuad[i].x, kQuad[i].y, 0.0f });
            if (normalOffset >= 0) PutFloats(v + normalOffset, { 0.0f, 0.0f, 1.0f });
            if (tangentOffset >= 0) PutFloats(v + tangentOffset, { 1.0f, 0.0f, 0.0f, 1.0f });
            if (uvOffset >= 0) PutFloats(v + uvOffset, { 0.25f, 0.5f });   // the LEFT texel
        }
        return bytes;
    }

    bool Near(const Color& got, const Color& want)
    {
        const int tolerance = 24;
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - want.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    std::vector<VertexElement> EnvironmentMapElements()
    {
        return {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        };
    }
}

class StockEffectPaddedStrideTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> strip_;
    std::unique_ptr<TextureCube> cube_;
    bool skipped_ = false;

    Color ReadAt(int x, int y)
    {
        Color got(0, 0, 0, 0);
        const Rectangle probe(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    // allowNamedRefusal: PbrEffect is a CNA extension with no XNA reference, so a renderer that
    // cannot map a declaration onto its PBR program may refuse it by name with
    // NotSupportedException (reported as SKIP) -- but never draw from the wrong bytes.
    void RunPadded(const std::string& label, int stride, const std::vector<VertexElement>& elements,
                   int normalOffset, int tangentOffset, int uvOffset, Effect& effect,
                   bool allowNamedRefusal = false)
    {
        auto& dev = getGraphicsDeviceProperty();
        VertexDeclaration declaration(stride, elements);
        VertexBuffer vb(dev, declaration, 6, BufferUsage::None);
        const auto bytes = BuildRecords(stride, normalOffset, tangentOffset, uvOffset);
        vb.SetDataRaw(bytes.data(), 6, stride);

        Color a(0, 0, 0, 0), b(0, 0, 0, 0);
        std::string refusal;
        try
        {
            dev.Clear(Color(0, 0, 0, 255));
            effect.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            a = ReadAt(8, 8);
            b = ReadAt(kSize - 8, kSize - 8);
        }
        catch (const System::NotSupportedException& e)
        {
            if (allowNamedRefusal)
            {
                std::printf("[SKIP] %s at stride %d: refused by name (%s)\n", label.c_str(), stride,
                            e.what());
                return;
            }
            refusal = e.what();
        }
        catch (const std::exception& e)
        {
            // A renderer with no metallic-roughness model may refuse the PbrEffect leg by name.
            if (allowNamedRefusal && CNA::Examples::Conformance::IsNamedPbrRefusal(e))
            {
                dev.SetVertexBuffer(nullptr);
                std::printf("[SKIP] %s at stride %d: no PBR model on this renderer (%s)\n",
                            label.c_str(), stride, e.what());
                return;
            }
            refusal = e.what();
        }
        const std::string what = label + " at stride " + std::to_string(stride) +
            " covers the whole quad, not just its first vertex: " +
            (refusal.empty() ? "(8,8)=" + Show(a) + " (56,56)=" + Show(b) + ", want " + Show(kLeft) +
                                   " at both"
                             : "refused: " + refusal);
        Check(refusal.empty() && Near(a, kLeft) && Near(b, kLeft), what.c_str());
    }

    void ConfigureEnvironmentMap(EnvironmentMapEffect& fx)
    {
        fx.setTextureProperty(strip_.get());
        fx.setEnvironmentMapProperty(cube_.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

public:
    StockEffectPaddedStrideTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("[SKIP] this renderer does not rasterize 3D stock effects\n");
            skipped_ = true;
            return;
        }
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = { kLeft, kRight };
        strip_->SetData(texels, 2);
        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Color grey(128, 128, 128, 255);
            cube_->SetData(static_cast<CubeMapFace>(face), &grey, 1);
        }

        {
            EnvironmentMapEffect fx(dev);
            ConfigureEnvironmentMap(fx);
            RunPadded("A EnvironmentMapEffect", 40, EnvironmentMapElements(), 12, -1, 24, fx);
        }
        {
            EnvironmentMapEffect fx(dev);
            ConfigureEnvironmentMap(fx);
            RunPadded("B EnvironmentMapEffect with A's element offsets", 48,
                      EnvironmentMapElements(), 12, -1, 24, fx);
        }
        {
            PbrEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            fx.setMetallicFactorProperty(0.0f);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setBaseColorTextureIsSrgbEXTProperty(false);
            fx.setEncodeOutputToSrgbEXTProperty(false);
            fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight0.setEnabledProperty(false);
            fx.DirectionalLight1.setEnabledProperty(false);
            fx.DirectionalLight2.setEnabledProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            const std::vector<VertexElement> elements{
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(52, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            };
            RunPadded("C PbrEffect", 64, elements, 16, 32, 52, fx, true);
        }
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("stock_effect_padded_stride"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    StockEffectPaddedStrideTest game;
    game.Run();
    return game.ExitCode();
}
