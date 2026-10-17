// SPDX-License-Identifier: MS-PL
// BasicEffect is 32 vertex/pixel shader pairs in XNA, selected by ShaderIndex: fog off (+1), vertex
// colour (+2), texture (+4), and lighting -- none, three lights per vertex (+8), one light per
// vertex (+16) or per pixel (+24). A renderer has to draw all 32. DIRECTX9 drew 12 of them and
// refused the other 20 with a named runtime_error until d3f1a9e51, and no test asked for the rest.
//
// Every variant draws one quad through the same 36-byte Position+Normal+Colour+TextureCoordinate
// record, so only the effect state differs. The terms are chosen to be predictable everywhere:
// DiffuseColor (0.5, 0.25, 0.75), a vertex colour (0.8, 0.4, 0.2) where enabled and a texel
// (0.5, 1, 0.25) where textured; lit variants have no ambient, emissive or specular term and one
// white light shining straight onto the quad's normal, so N.L = 1 and they draw the unlit product.
// The three-light variant gets two more lights with black diffuse. FogStart == FogEnd fogs fully
// (XNA's degenerate fog vector), so every fogged variant draws FogColor exactly.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    struct Vertex
    {
        float x, y, z;
        float nx, ny, nz;
        std::uint32_t color;
        float u, v;
    };
    static_assert(sizeof(Vertex) == 36);

    enum class Lighting { None, ThreeLights, OneLight, PerPixel };

    struct Variant
    {
        bool fog;
        bool vertexColor;
        bool texture;
        Lighting lighting;
    };

    std::string Describe(const Variant& variant)
    {
        static const char* const lighting[] = {"unlit", "three lights", "one light", "per pixel"};
        return std::string(variant.fog ? "fog" : "no fog") +
               (variant.vertexColor ? ", vertex colour" : "") +
               (variant.texture ? ", texture" : "") + ", " +
               lighting[static_cast<int>(variant.lighting)];
    }

    int ExpectedShaderIndex(const Variant& variant)
    {
        int index = 0;
        if (!variant.fog) index += 1;
        if (variant.vertexColor) index += 2;
        if (variant.texture) index += 4;
        if (variant.lighting == Lighting::ThreeLights) index += 8;
        if (variant.lighting == Lighting::OneLight) index += 16;
        if (variant.lighting == Lighting::PerPixel) index += 24;
        return index;
    }

    class BasicEffectVariantTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, WebGPU, SdlGpu, DirectX9, Fna3d))
                GTEST_SKIP() << "needs a renderer that rasterizes stock effects";

            PresentationParameters parameters;
            parameters.setBackBufferWidthProperty(kSize);
            parameters.setBackBufferHeightProperty(kSize);
            parameters.setDepthStencilFormatProperty(DepthFormat::None);
            device_.emplace(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                            parameters);
            texel_.emplace(*device_, 1, 1, false, SurfaceFormat::Color);
            const Color texel(128, 255, 64, 255);
            texel_->SetData(&texel, 1);
        }

        Color Draw(const Variant& variant)
        {
            GraphicsDevice& device = *device_;
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.75f));
            effect.setEmissiveColorProperty(Vector3::Zero);
            effect.setSpecularColorProperty(Vector3::Zero);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setVertexColorEnabledProperty(variant.vertexColor);
            effect.setTextureEnabledProperty(variant.texture);
            effect.setTextureProperty(&*texel_);
            effect.setFogEnabledProperty(variant.fog);
            effect.setFogColorProperty(Vector3(0.2f, 0.6f, 0.4f));
            effect.setFogStartProperty(0.0f);
            effect.setFogEndProperty(0.0f);
            effect.setLightingEnabledProperty(variant.lighting != Lighting::None);
            effect.setPreferPerPixelLightingProperty(variant.lighting == Lighting::PerPixel);
            const auto light = [](DirectionalLight& l, bool enabled, const Vector3& diffuse) {
                l.setEnabledProperty(enabled);
                l.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
                l.setDiffuseColorProperty(diffuse);
                l.setSpecularColorProperty(Vector3::Zero);
            };
            const bool threeLights = variant.lighting == Lighting::ThreeLights;
            light(effect.getDirectionalLight0Property(), true, Vector3::One);
            light(effect.getDirectionalLight1Property(), threeLights, Vector3::Zero);
            light(effect.getDirectionalLight2Property(), threeLights, Vector3::Zero);

            const std::uint32_t color = Color(204, 102, 51, 255).getPackedValueProperty();
            const auto at = [&](float x, float y, float u, float v) {
                return Vertex{x, y, 0.0f, 0.0f, 0.0f, 1.0f, color, u, v};
            };
            const Vertex quad[6] = {at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                                    at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
            const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(28, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            });

            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            device.Clear(Color(7, 199, 53, 255));
            effect.Apply();
            EXPECT_EQ(effect.getParametersProperty()["ShaderIndex"]->GetValueInt32(),
                      ExpectedShaderIndex(variant));
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2, declaration);
            Color pixel = Color::Transparent;
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            return pixel;
        }

        static Color Expected(const Variant& variant)
        {
            if (variant.fog) return Color(51, 153, 102, 255);
            float r = 0.5f, g = 0.25f, b = 0.75f;
            if (variant.vertexColor) { r *= 0.8f; g *= 0.4f; b *= 0.2f; }
            if (variant.texture) { r *= 128.0f / 255.0f; g *= 1.0f; b *= 64.0f / 255.0f; }
            const auto byte = [](float value) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            };
            return Color(byte(r), byte(g), byte(b), byte(1.0f));
        }

    private:
        std::optional<GraphicsDevice> device_;
        std::optional<Texture2D> texel_;
    };
}

TEST_F(BasicEffectVariantTest, EveryOneOfTheThirtyTwoShaderVariantsDraws)
{
    for (const bool fog : {false, true})
        for (const bool vertexColor : {false, true})
            for (const bool texture : {false, true})
                for (const Lighting lighting : {Lighting::None, Lighting::ThreeLights,
                                                Lighting::OneLight, Lighting::PerPixel})
                {
                    const Variant variant{fog, vertexColor, texture, lighting};
                    SCOPED_TRACE(Describe(variant));
                    const Color actual = Draw(variant);
                    const Color expected = Expected(variant);
                    EXPECT_LE(std::abs(actual.getRProperty() - expected.getRProperty()), 3);
                    EXPECT_LE(std::abs(actual.getGProperty() - expected.getGProperty()), 3);
                    EXPECT_LE(std::abs(actual.getBProperty() - expected.getBProperty()), 3);
                    EXPECT_EQ(actual.getAProperty(), 255);
                }
}
