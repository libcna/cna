// SPDX-License-Identifier: MS-PL
// HND-TQ-25 D9: pixels of a base-Effect or EffectMaterial clone of a stock effect on every
// rasterizing renderer family. XNA's Effect::.ctor(Effect) IL_0048-0085 clones the native stock
// effect, so the clone runs the very same stock shaders; it is not the stock class, so
// EffectPass.Apply IL_003b runs no stock OnApply for it and the shaders read the clone's raw
// parameter values. Three things are pinned per stock class:
//   1. a clone made after the source's Apply draws the source's picture, pixel for pixel;
//   2. a raw parameter written on the clone changes the clone's picture exactly as the stock
//      shader reads it -- the same picture the stock class draws when its own OnApply writes that
//      value -- and a convenience property set on the source afterwards changes nothing in it;
//   3. the source keeps drawing its own picture (the per-class suites pin its pixels; here it is
//      drawn again after its clone).
// Every value is dyadic so the stock classes' own derivations and a clone's raw values are the
// same floats; the comparisons are exact.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    using Image = std::vector<Color>;

    // Position + Normal + Colour + TextureCoordinate, the record BasicEffectVariantTest draws.
    struct BasicVertex
    {
        float x, y, z;
        float nx, ny, nz;
        std::uint32_t color;
        float u, v;
    };
    static_assert(sizeof(BasicVertex) == 36);

    struct DualUvVertex
    {
        float x, y, z;
        float u0, v0;
        float u1, v1;
    };
    static_assert(sizeof(DualUvVertex) == 28);

    // World scales by 1/2 and moves the quad off centre, View moves it 1/2 away, Projection flips
    // Z into Direct3D's [0, 1]: every product is exact, and so is the fog vector of FogStart 0,
    // FogEnd 1 -- a fog factor of exactly 1/2 at the quad.
    Matrix SceneWorld() { return Matrix::CreateScale(0.5f) * Matrix::CreateTranslation(0.25f, -0.25f, 0.0f); }
    Matrix SceneView() { return Matrix::CreateTranslation(0.0f, 0.0f, -0.5f); }
    Matrix SceneProjection() { return Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1); }

    std::string DescribeDifference(const Image& actual, const Image& expected)
    {
        int differing = 0;
        std::ostringstream first;
        for (std::size_t i = 0; i < actual.size() && i < expected.size(); ++i)
        {
            if (actual[i] == expected[i])
                continue;
            if (differing++ == 0)
            {
                const auto rgba = [](const Color& c) {
                    return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
                           "," + std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
                };
                first << " first at (" << i % kSize << "," << i / kSize << "): " << rgba(actual[i])
                      << " vs " << rgba(expected[i]);
            }
        }
        return std::to_string(differing) + " of " + std::to_string(actual.size()) +
               " pixels differ" + first.str();
    }

#define EXPECT_SAME_IMAGE(actual, expected)                                                        \
    do                                                                                             \
    {                                                                                              \
        const Image& actualImage_ = (actual);                                                      \
        const Image& expectedImage_ = (expected);                                                  \
        EXPECT_TRUE(actualImage_ == expectedImage_)                                                \
            << DescribeDifference(actualImage_, expectedImage_);                                   \
    } while (false)

    class StockEffectCloneRenderingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, WebGPU, SdlGpu, DirectX9, Fna3d))
                GTEST_SKIP() << "needs a renderer that rasterizes stock effects";

            PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
            parameters.setBackBufferWidthProperty(kSize);
            parameters.setBackBufferHeightProperty(kSize);
            parameters.setDepthStencilFormatProperty(DepthFormat::None);
            device_.emplace(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                            parameters);
            // Four texels of different colour and alpha, sampled with PointClamp.
            const Color texels[4] = {Color(255, 128, 64, 255), Color(32, 224, 96, 192),
                                     Color(160, 48, 240, 64), Color(96, 96, 32, 0)};
            texture_.emplace(*device_, 2, 2, false, SurfaceFormat::Color);
            texture_->SetData(texels, 0, 4);
            const Color texels2[4] = {Color(128, 255, 255, 255), Color(255, 128, 255, 255),
                                      Color(255, 255, 128, 255), Color(128, 128, 128, 255)};
            texture2_.emplace(*device_, 2, 2, false, SurfaceFormat::Color);
            texture2_->SetData(texels2, 0, 4);
            cube_.emplace(*device_, 1, false, SurfaceFormat::Color);
            const Color faces[6] = {Color(255, 0, 0, 128), Color(0, 255, 0, 128), Color(0, 0, 255, 128),
                                    Color(255, 255, 0, 128), Color(0, 255, 255, 128), Color(255, 0, 255, 128)};
            for (int face = 0; face < 6; ++face)
                cube_->SetData(static_cast<CubeMapFace>(face), &faces[face], 1);
        }

        GraphicsDevice& Device() { return *device_; }
        Texture2D& Texture() { return *texture_; }
        Texture2D& Texture2() { return *texture2_; }
        TextureCube& Cube() { return *cube_; }

        // Clears, applies `effect`, runs `draw` and reads the whole back buffer.
        Image Render(Effect& effect, const std::function<void()>& draw)
        {
            GraphicsDevice& device = *device_;
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
            device.Clear(Color(7, 199, 53, 255));
            effect.Apply();
            draw();
            Image image(static_cast<std::size_t>(kSize * kSize));
            device.GetBackBufferData(image.data(), static_cast<int>(image.size()));
            return image;
        }

        static bool Covers(const Image& image)
        {
            int drawn = 0;
            for (const Color& pixel : image)
                drawn += pixel == Color(7, 199, 53, 255) ? 0 : 1;
            return drawn > 0 && drawn < kSize * kSize;
        }

        void DrawBasicQuad()
        {
            const std::uint32_t colour = Color(255, 128, 64, 255).getPackedValueProperty();
            const auto at = [&](float x, float y, float u, float v) {
                return BasicVertex{x, y, 0.0f, 0.0f, 0.0f, 1.0f, colour, u, v};
            };
            const BasicVertex quad[6] = {at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                                         at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
            const VertexDeclaration declaration(static_cast<int>(sizeof(BasicVertex)), {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(28, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2, declaration);
        }

        void DrawColourTexturedQuad()
        {
            const auto at = [](float x, float y, float u, float v) {
                return VertexPositionColorTexture(Vector3(x, y, 0.0f), Color(255, 128, 64, 255), Vector2(u, v));
            };
            const std::array<VertexPositionColorTexture, 6> quad = {
                at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        }

        void DrawDualQuad()
        {
            const auto at = [](float x, float y, float u, float v) {
                return DualUvVertex{x, y, 0.0f, u, v, 1.0f - u, v};
            };
            const DualUvVertex quad[6] = {at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                                          at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
            const VertexDeclaration declaration(static_cast<int>(sizeof(DualUvVertex)), {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            });
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2, declaration);
        }

        void DrawNormalQuad()
        {
            // Normals tilted away from one another, so the per-vertex reflection and Fresnel terms
            // vary across the quad.
            const auto at = [](float x, float y, float u, float v) {
                return VertexPositionNormalTexture(Vector3(x, y, 0.0f), Vector3(0.5f * x, 0.25f * y, 1.0f),
                                                   Vector2(u, v));
            };
            const std::array<VertexPositionNormalTexture, 6> quad = {
                at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        }

        // Skinned vertices go through a bound vertex buffer, the route the SkinnedEffect suites use;
        // the right half follows bone 1, the left half bone 0.
        std::function<void()> SkinnedQuad()
        {
            return [this] {
                const auto at = [](float x, float y, float u, float v) {
                    const std::uint8_t bone = x > 0.0f ? 1 : 0;
                    return VertexPositionNormalTextureSkinned(
                        Vector3(x, y, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(u, v),
                        Vector4(1.0f, 0.0f, 0.0f, 0.0f), {bone, 0, 0, 0});
                };
                const std::array<VertexPositionNormalTextureSkinned, 6> quad = {
                    at(-1, 1, 0, 0), at(1, 1, 1, 0), at(-1, -1, 0, 1),
                    at(1, 1, 1, 0),  at(1, -1, 1, 1), at(-1, -1, 0, 1)};
                skinnedBuffer_.emplace(*device_, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(),
                                       6, BufferUsage::None);
                skinnedBuffer_->SetData(quad.data(), 6);
                device_->SetVertexBuffer(&*skinnedBuffer_);
                device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                device_->SetVertexBuffer(nullptr);
            };
        }

        template <typename TEffect>
        static void Scene(TEffect& effect)
        {
            effect.setWorldProperty(SceneWorld());
            effect.setViewProperty(SceneView());
            effect.setProjectionProperty(SceneProjection());
        }

        template <typename TEffect>
        static void Fog(TEffect& effect)
        {
            effect.setFogEnabledProperty(true);
            effect.setFogColorProperty(Vector3(0.25f, 0.5f, 0.75f));
            effect.setFogStartProperty(0.0f);
            effect.setFogEndProperty(1.0f);
        }

        template <typename TEffect>
        static void ThreeLights(TEffect& effect, bool withSpecular)
        {
            DirectionalLight* lights[3] = {&effect.getDirectionalLight0Property(),
                                           &effect.getDirectionalLight1Property(),
                                           &effect.getDirectionalLight2Property()};
            const Vector3 directions[3] = {Vector3(0.0f, 0.0f, -1.0f), Vector3(1.0f, 0.0f, -1.0f),
                                           Vector3(0.0f, -1.0f, -1.0f)};
            for (int i = 0; i < 3; ++i)
            {
                lights[i]->setEnabledProperty(true);
                lights[i]->setDirectionProperty(directions[i]);
                lights[i]->setDiffuseColorProperty(Vector3(0.25f, 0.125f * (i + 1), 0.5f));
                if (withSpecular)
                    lights[i]->setSpecularColorProperty(Vector3(0.125f, 0.25f, 0.125f * (i + 1)));
            }
        }

        // Draws `source`, an EffectMaterial clone made after the source's Apply and the base Effect
        // that clone's Clone() returns, and expects one picture; then draws the source again.
        void ExpectTheClonesDrawTheSourcesPicture(Effect& source, const std::function<void()>& draw)
        {
            const Image expected = Render(source, draw);
            ASSERT_TRUE(Covers(expected)) << "the source draws a partial quad";
            EffectMaterial material(source);
            EXPECT_SAME_IMAGE(Render(material, draw), expected);
            std::unique_ptr<Effect> clone(material.Clone());
            EXPECT_SAME_IMAGE(Render(*clone, draw), expected);
            EXPECT_SAME_IMAGE(Render(source, draw), expected);
        }

    private:
        std::optional<GraphicsDevice> device_;
        std::optional<Texture2D> texture_;
        std::optional<Texture2D> texture2_;
        std::optional<TextureCube> cube_;
        std::optional<VertexBuffer> skinnedBuffer_;
    };
}

TEST_F(StockEffectCloneRenderingTest, BasicEffectClonesDrawTheSourcesPictureInEveryLightingFamily)
{
    const auto draw = [this] { DrawBasicQuad(); };
    struct Case { const char* name; bool lighting; bool perPixel; bool threeLights; bool texture; bool vertexColor; bool fog; };
    const Case cases[] = {
        {"unlit, texture, vertex colour, fog", false, false, false, true, true, true},
        {"unlit, no fog", false, false, false, false, false, false},
        {"three lights per vertex, texture, fog", true, false, true, true, false, true},
        {"one light per vertex, vertex colour", true, false, false, false, true, false},
        {"per pixel, texture, fog", true, true, true, true, false, true},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.name);
        BasicEffect source(Device());
        Scene(source);
        source.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
        source.setEmissiveColorProperty(Vector3(0.125f, 0.0f, 0.0625f));
        source.setAmbientLightColorProperty(Vector3(0.25f, 0.25f, 0.5f));
        source.setSpecularColorProperty(Vector3(0.5f, 0.5f, 0.25f));
        source.setSpecularPowerProperty(4.0f);
        source.setAlphaProperty(1.0f);
        source.setLightingEnabledProperty(c.lighting);
        source.setPreferPerPixelLightingProperty(c.perPixel);
        if (c.lighting)
        {
            ThreeLights(source, true);
            if (!c.threeLights)
            {
                source.getDirectionalLight1Property().setEnabledProperty(false);
                source.getDirectionalLight2Property().setEnabledProperty(false);
            }
        }
        source.setTextureEnabledProperty(c.texture);
        source.setTextureProperty(&Texture());
        source.setVertexColorEnabledProperty(c.vertexColor);
        if (c.fog)
            Fog(source);
        ExpectTheClonesDrawTheSourcesPicture(source, draw);
    }
}

// The stock vertex shader places the quad with the raw WorldViewProj; the clone's World, View and
// Projection do not exist and its World parameter only lights it.
TEST_F(StockEffectCloneRenderingTest, ARawWorldViewProjOnTheClonePlacesTheQuad)
{
    const auto draw = [this] { DrawBasicQuad(); };
    const Matrix placement = Matrix::CreateScale(0.25f, 0.5f, 1.0f) * Matrix::CreateTranslation(-0.5f, 0.25f, 0.0f);
    BasicEffect expected(Device());
    expected.setWorldProperty(placement);
    expected.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.75f));
    const Image picture = Render(expected, draw);
    ASSERT_TRUE(Covers(picture));

    BasicEffect source(Device());
    Scene(source);
    source.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.75f));
    const Image sourcePicture = Render(source, draw);
    EffectMaterial clone(source);
    clone.getParametersProperty()["WorldViewProj"]->SetValue(placement);
    clone.getParametersProperty()["World"]->SetValue(Matrix::CreateScale(8.0f));
    EXPECT_SAME_IMAGE(Render(clone, draw), picture);
    EXPECT_SAME_IMAGE(Render(source, draw), sourcePicture);
}

// XNA's embedded stock effect starts with every constant zero, so a clone made before its source's
// first Apply has a zero WorldViewProj: the stock vertex shader collapses the quad and nothing is
// drawn. A clone made after that Apply draws the source's picture.
TEST_F(StockEffectCloneRenderingTest, ACloneMadeBeforeTheSourcesFirstApplyDrawsNothing)
{
    const auto draw = [this] { DrawBasicQuad(); };
    BasicEffect source(Device());
    Scene(source);
    EffectMaterial early(source);
    const Image background(static_cast<std::size_t>(kSize * kSize), Color(7, 199, 53, 255));
    EXPECT_SAME_IMAGE(Render(early, draw), background);

    const Image picture = Render(source, draw);
    ASSERT_TRUE(Covers(picture));
    EffectMaterial late(source);
    EXPECT_SAME_IMAGE(Render(late, draw), picture);
    EXPECT_SAME_IMAGE(Render(early, draw), background);
}

// The raw DiffuseColor is what the unlit stock pixel shader outputs, with no OnApply folding
// EmissiveColor or Alpha into it; a source property set afterwards does not reach the clone.
TEST_F(StockEffectCloneRenderingTest, ARawDiffuseColourOnTheCloneIsWhatTheShaderDraws)
{
    const auto draw = [this] { DrawBasicQuad(); };
    BasicEffect source(Device());
    Scene(source);
    source.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    source.setEmissiveColorProperty(Vector3(0.5f, 0.0f, 0.0f));
    source.setAlphaProperty(0.5f);
    source.Apply();
    EffectMaterial clone(source);
    const Color raw(51, 102, 204, 255);
    clone.getParametersProperty()["DiffuseColor"]->SetValue(raw.ToVector4());
    const Image picture = Render(clone, draw);

    BasicEffect expected(Device());
    Scene(expected);
    expected.setDiffuseColorProperty(raw.ToVector3());
    EXPECT_SAME_IMAGE(picture, Render(expected, draw));
    EXPECT_EQ(picture[static_cast<std::size_t>(kSize / 2 * kSize + kSize / 2 + 2)], raw)
        << "the quad's pixel right of centre";

    source.setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
    source.setTextureEnabledProperty(true);
    source.setWorldProperty(Matrix::CreateScale(0.125f));
    source.Apply();
    EXPECT_SAME_IMAGE(Render(clone, draw), picture);
}

// ShaderIndex alone chooses the variant: switching the clone to the no-fog index leaves its raw
// FogVector in place, and the NoFog pixel shader ignores it.
TEST_F(StockEffectCloneRenderingTest, ARawShaderIndexOnTheCloneSelectsTheVariant)
{
    const auto draw = [this] { DrawBasicQuad(); };
    BasicEffect source(Device());
    Scene(source);
    source.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
    source.setTextureProperty(&Texture());
    Fog(source);
    source.Apply();
    ASSERT_EQ(source.getParametersProperty()["ShaderIndex"]->GetValueInt32(), 0);
    EffectMaterial clone(source);

    BasicEffect unfogged(Device());
    Scene(unfogged);
    unfogged.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
    clone.getParametersProperty()["ShaderIndex"]->SetValue(1);
    EXPECT_SAME_IMAGE(Render(clone, draw), Render(unfogged, draw));

    BasicEffect textured(Device());
    Scene(textured);
    textured.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
    textured.setTextureEnabledProperty(true);
    textured.setTextureProperty(&Texture());
    Fog(textured);
    clone.getParametersProperty()["ShaderIndex"]->SetValue(4);
    EXPECT_SAME_IMAGE(Render(clone, draw), Render(textured, draw));
}

// The stock vertex shaders light with the raw WorldInverseTranspose, not one derived from World:
// negating it on a lit clone turns every normal away from the light, leaving EmissiveColor alone --
// the picture of the source with its light switched off.
TEST_F(StockEffectCloneRenderingTest, ARawWorldInverseTransposeOnTheCloneLightsTheQuad)
{
    const auto draw = [this] { DrawBasicQuad(); };
    for (const bool perPixel : {false, true})
    {
        SCOPED_TRACE(perPixel ? "per pixel" : "per vertex");
        const auto configure = [&](BasicEffect& effect, bool lightOn) {
            Scene(effect);
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(perPixel);
            effect.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
            effect.setEmissiveColorProperty(Vector3(0.25f, 0.125f, 0.0f));
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setSpecularColorProperty(Vector3::Zero);
            DirectionalLight& light = effect.getDirectionalLight0Property();
            light.setEnabledProperty(true);
            light.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            light.setDiffuseColorProperty(lightOn ? Vector3::One : Vector3::Zero);
            light.setSpecularColorProperty(Vector3::Zero);
        };
        BasicEffect source(Device());
        configure(source, true);
        const Image lit = Render(source, draw);
        BasicEffect dark(Device());
        configure(dark, false);
        const Image unlit = Render(dark, draw);
        ASSERT_FALSE(lit == unlit);

        EffectMaterial clone(source);
        EXPECT_SAME_IMAGE(Render(clone, draw), lit);
        const Matrix flipped = Matrix::Transpose(Matrix::Invert(SceneWorld())) * Matrix::CreateScale(-1.0f);
        clone.getParametersProperty()["WorldInverseTranspose"]->SetValue(flipped);
        EXPECT_SAME_IMAGE(Render(clone, draw), unlit);
    }
}

TEST_F(StockEffectCloneRenderingTest, AlphaTestEffectClonesDrawTheSourcesPictureAndFollowARawTest)
{
    const auto draw = [this] { DrawColourTexturedQuad(); };
    for (const CompareFunction function : {CompareFunction::Greater, CompareFunction::Equal})
    {
        SCOPED_TRACE(function == CompareFunction::Greater ? "Greater" : "Equal");
        AlphaTestEffect source(Device());
        Scene(source);
        source.setTextureProperty(&Texture());
        source.setVertexColorEnabledProperty(true);
        source.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 1.0f));
        source.setAlphaFunctionProperty(function);
        source.setReferenceAlphaProperty(192);
        Fog(source);
        ExpectTheClonesDrawTheSourcesPicture(source, draw);
    }

    // The clone's raw AlphaTest and ShaderIndex: NotEqual's pair, set by hand on a Greater clone.
    AlphaTestEffect source(Device());
    Scene(source);
    source.setTextureProperty(&Texture());
    source.setAlphaFunctionProperty(CompareFunction::Greater);
    source.setReferenceAlphaProperty(64);
    source.Apply();
    AlphaTestEffect notEqual(Device());
    Scene(notEqual);
    notEqual.setTextureProperty(&Texture());
    notEqual.setAlphaFunctionProperty(CompareFunction::NotEqual);
    notEqual.setReferenceAlphaProperty(192);
    const Image expected = Render(notEqual, draw);
    EffectMaterial clone(source);
    clone.getParametersProperty()["AlphaTest"]->SetValue(
        notEqual.getParametersProperty()["AlphaTest"]->GetValueVector4());
    clone.getParametersProperty()["ShaderIndex"]->SetValue(
        notEqual.getParametersProperty()["ShaderIndex"]->GetValueInt32());
    EXPECT_SAME_IMAGE(Render(clone, draw), expected);

    // PSAlphaTestLtGt never reads AlphaTest.y: a tolerance written on a Greater clone changes
    // nothing, although the renderers' own form would read it as an equality test.
    const Image greater = Render(source, draw);
    EffectMaterial greaterClone(source);
    const Vector4 raw = source.getParametersProperty()["AlphaTest"]->GetValueVector4();
    greaterClone.getParametersProperty()["AlphaTest"]->SetValue(Vector4(raw.X, 0.5f, raw.Z, raw.W));
    EXPECT_SAME_IMAGE(Render(greaterClone, draw), greater);
}

// The one-light vertex shaders run ComputeLights(..., 1): a colour written on the clone's
// DirLight1 changes nothing there, and lights the quad once the clone selects three lights.
TEST_F(StockEffectCloneRenderingTest, AOneLightCloneReadsDirectionalLightZeroOnly)
{
    const auto draw = [this] { DrawBasicQuad(); };
    const auto configure = [&](BasicEffect& effect, bool secondLight) {
        Scene(effect);
        effect.setLightingEnabledProperty(true);
        effect.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
        effect.setSpecularColorProperty(Vector3::Zero);
        ThreeLights(effect, false);
        effect.getDirectionalLight1Property().setEnabledProperty(secondLight);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
    };
    BasicEffect source(Device());
    configure(source, false);
    const Image oneLight = Render(source, draw);
    ASSERT_EQ(source.getParametersProperty()["ShaderIndex"]->GetValueInt32(), 17);
    BasicEffect twoLights(Device());
    configure(twoLights, true);
    const Image threeLightShader = Render(twoLights, draw);
    ASSERT_FALSE(oneLight == threeLightShader);

    EffectMaterial clone(source);
    clone.getParametersProperty()["DirLight1DiffuseColor"]->SetValue(
        twoLights.getParametersProperty()["DirLight1DiffuseColor"]->GetValueVector3());
    EXPECT_SAME_IMAGE(Render(clone, draw), oneLight);
    clone.getParametersProperty()["ShaderIndex"]->SetValue(
        twoLights.getParametersProperty()["ShaderIndex"]->GetValueInt32());
    EXPECT_SAME_IMAGE(Render(clone, draw), threeLightShader);
}

TEST_F(StockEffectCloneRenderingTest, DualTextureEffectClonesDrawTheSourcesPictureAndARawColour)
{
    const auto draw = [this] { DrawDualQuad(); };
    DualTextureEffect source(Device());
    Scene(source);
    source.setTextureProperty(&Texture());
    source.setTexture2Property(&Texture2());
    source.setDiffuseColorProperty(Vector3(0.5f, 1.0f, 0.75f));
    Fog(source);
    ExpectTheClonesDrawTheSourcesPicture(source, draw);

    DualTextureEffect expected(Device());
    Scene(expected);
    expected.setTextureProperty(&Texture());
    expected.setTexture2Property(&Texture2());
    expected.setDiffuseColorProperty(Vector3(1.0f, 0.25f, 0.5f));
    expected.setAlphaProperty(0.5f);
    Fog(expected);
    EffectMaterial clone(source);
    clone.getParametersProperty()["DiffuseColor"]->SetValue(Vector4(0.5f, 0.125f, 0.25f, 0.5f));
    EXPECT_SAME_IMAGE(Render(clone, draw), Render(expected, draw));
}

TEST_F(StockEffectCloneRenderingTest, EnvironmentMapEffectClonesDrawTheSourcesPictureAndARawAmount)
{
    const auto draw = [this] { DrawNormalQuad(); };
    for (const bool fresnel : {false, true})
    {
        SCOPED_TRACE(fresnel ? "Fresnel" : "no Fresnel");
        EnvironmentMapEffect source(Device());
        Scene(source);
        source.setTextureProperty(&Texture());
        source.setEnvironmentMapProperty(&Cube());
        source.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
        source.setAmbientLightColorProperty(Vector3(0.25f, 0.25f, 0.25f));
        source.setEnvironmentMapAmountProperty(0.5f);
        source.setEnvironmentMapSpecularProperty(Vector3(0.25f, 0.0f, 0.5f));
        source.setFresnelFactorProperty(fresnel ? 1.0f : 0.0f);
        ThreeLights(source, false);
        ExpectTheClonesDrawTheSourcesPicture(source, draw);
    }

    EnvironmentMapEffect source(Device());
    Scene(source);
    source.setTextureProperty(&Texture());
    source.setEnvironmentMapProperty(&Cube());
    source.setFresnelFactorProperty(0.0f);
    source.setEnvironmentMapAmountProperty(0.25f);
    source.Apply();
    EnvironmentMapEffect expected(Device());
    Scene(expected);
    expected.setTextureProperty(&Texture());
    expected.setEnvironmentMapProperty(&Cube());
    expected.setFresnelFactorProperty(0.0f);
    expected.setEnvironmentMapAmountProperty(0.75f);
    EffectMaterial clone(source);
    clone.getParametersProperty()["EnvironmentMapAmount"]->SetValue(0.75f);
    EXPECT_SAME_IMAGE(Render(clone, draw), Render(expected, draw));
}

TEST_F(StockEffectCloneRenderingTest, SkinnedEffectClonesDrawTheSourcesPictureAndARawBone)
{
    const std::function<void()> draw = SkinnedQuad();
    const std::vector<Matrix> bones = {Matrix::getIdentityProperty(),
                                       Matrix::CreateTranslation(0.0f, 0.5f, 0.0f)};
    for (const bool perPixel : {false, true})
    {
        SCOPED_TRACE(perPixel ? "per pixel" : "per vertex");
        SkinnedEffect source(Device());
        Scene(source);
        source.setTextureProperty(&Texture());
        source.setDiffuseColorProperty(Vector3(0.5f, 0.75f, 0.25f));
        source.setAmbientLightColorProperty(Vector3(0.25f, 0.25f, 0.25f));
        source.setPreferPerPixelLightingProperty(perPixel);
        source.setWeightsPerVertexProperty(1);
        source.SetBoneTransforms(bones);
        ThreeLights(source, true);
        ExpectTheClonesDrawTheSourcesPicture(source, draw);
    }

    SkinnedEffect source(Device());
    Scene(source);
    source.setTextureProperty(&Texture());
    source.setWeightsPerVertexProperty(1);
    source.SetBoneTransforms(bones);
    source.Apply();
    SkinnedEffect expected(Device());
    Scene(expected);
    expected.setTextureProperty(&Texture());
    expected.setWeightsPerVertexProperty(1);
    const Matrix moved = Matrix::CreateTranslation(-0.5f, -0.25f, 0.0f);
    expected.SetBoneTransforms({Matrix::getIdentityProperty(), moved});
    EffectMaterial clone(source);
    clone.getParametersProperty()["Bones"]->getElementsProperty()[1]->SetValue(moved);
    EXPECT_SAME_IMAGE(Render(clone, draw), Render(expected, draw));
}

// XNA's SpriteBatch applies a custom effect's pass after its own, so a clone of a stock effect
// places the sprites with its raw WorldViewProj exactly as the stock source places them with
// World*View*Projection (the route AStockEffectInBeginPlacesTheSpritesByItsMatrices pins).
TEST_F(StockEffectCloneRenderingTest, ASpriteBatchDrawsThroughAStockClonesShaders)
{
    if (!CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, Fna3d))
        GTEST_SKIP() << "the SpriteBatch stock-effect route is pinned on these renderers";

    Texture2D white(Device(), 1, 1, false, SurfaceFormat::Color);
    const Color whiteTexel = Color::White;
    white.SetData(&whiteTexel, 1);
    const auto spritePicture = [&](Effect& effect) {
        GraphicsDevice& device = Device();
        device.Clear(Color::Black);
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, nullptr, &DepthStencilState::None,
                    &RasterizerState::CullNone, &effect);
        batch.Draw(white, Vector2(0.0f, 0.0f), std::nullopt, Color::Red, 0.0f, Vector2(0.0f, 0.0f),
                   1.0f, SpriteEffects::None, -1.0f);
        batch.End();
        Image image(static_cast<std::size_t>(kSize * kSize));
        device.GetBackBufferData(image.data(), static_cast<int>(image.size()));
        return image;
    };

    BasicEffect source(Device());
    source.setTextureEnabledProperty(true);
    source.setVertexColorEnabledProperty(true);
    source.setWorldProperty(Matrix::CreateScale(1.0f, -1.0f, 1.0f));
    source.setProjectionProperty(Matrix::CreateOrthographicOffCenter(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f));
    const Image expected = spritePicture(source);
    ASSERT_EQ(expected[static_cast<std::size_t>(kSize * 3 / 4 * kSize + kSize * 3 / 4)], Color::Red);
    EffectMaterial clone(source);
    EXPECT_SAME_IMAGE(spritePicture(clone), expected);
}
