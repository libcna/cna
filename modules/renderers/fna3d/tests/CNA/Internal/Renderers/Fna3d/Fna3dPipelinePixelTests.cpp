// SPDX-License-Identifier: MS-PL
// Device execution is owner-only through tools/platform/run_gpu_tests_private.sh --exec.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include <cstdlib>
#include <string>
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    class Fna3dPipelinePixels : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            device_ = std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480));
            // In a multi-renderer tree another family may be the active one; these are FNA3D
            // contracts, so they run only when FNA3D is selected (CNA_GRAPHICS_RENDERER=FNA3D).
            if (dynamic_cast<CNA::Internal::Renderers::Fna3d::Fna3dRenderer*>(
                    &device_->GetRenderer()) == nullptr)
            {
                GTEST_SKIP() << "FNA3D renderer contracts; the active renderer is another family";
            }
            target_ = std::make_unique<RenderTarget2D>(*device_, 8, 8, false, SurfaceFormat::Color,
                DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        }

        void Prepare(int stencil = 0)
        {
            device_->SetRenderTarget(target_.get());
            device_->setBlendStateProperty(BlendState::Opaque);
            device_->setRasterizerStateProperty(RasterizerState::CullNone);
            device_->Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                Color::Black, 1.0f, stencil);
        }

        Color Pixel()
        {
            device_->SetRenderTarget(nullptr);
            Color result = Color::Transparent;
            // Inside the quad, away from its diagonal and all viewport edges.
            const Rectangle rectangle(2, 4, 1, 1);
            target_->GetData(0, &rectangle, &result, 0, 1);
            return result;
        }

        static std::array<VertexPositionColor, 6> Quad(float z, const Color& color)
        {
            return {VertexPositionColor(Vector3(-1, 1, z), color),
                VertexPositionColor(Vector3(1, 1, z), color),
                VertexPositionColor(Vector3(-1, -1, z), color),
                VertexPositionColor(Vector3(1, 1, z), color),
                VertexPositionColor(Vector3(1, -1, z), color),
                VertexPositionColor(Vector3(-1, -1, z), color)};
        }

        void Draw(float z, const Color& color, bool reverse = false)
        {
            BasicEffect effect(*device_);
            effect.setVertexColorEnabledProperty(true);
            effect.Apply();
            auto vertices = Quad(z, color);
            if (reverse)
                for (int triangle = 0; triangle < 2; ++triangle)
                    std::swap(vertices[triangle * 3], vertices[triangle * 3 + 1]);
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);
        }

        void Sprite(SpriteBatch& batch, Texture2D& white, Effect* effect,
                    const DepthStencilState& state, float z, const Color& color)
        {
            batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                &state, &RasterizerState::CullNone, effect);
            batch.Draw(white, Rectangle(0, 0, 8, 8), std::nullopt, color, 0,
                Vector2::Zero, SpriteEffects::None, z);
            batch.End();
        }

        std::unique_ptr<Effect> CustomSprite()
        {
            // Oracle shader: the Microsoft-compiled SpriteEffect.fxb already shipped in CNA.
            const auto path = CNA::TestSupport::CompiledEffectDirectory() / "SpriteEffect.fxb";
            std::ifstream input(path, std::ios::binary);
            if (!input) { ADD_FAILURE() << "missing " << path; return nullptr; }
            const std::vector<SharpRuntime::bytecs> bytes{std::istreambuf_iterator<char>(input), {}};
            auto effect = std::make_unique<Effect>(*device_, bytes);
            auto* transform = effect->getParametersProperty()["MatrixTransform"];
            if (!transform) { ADD_FAILURE() << "missing MatrixTransform"; return nullptr; }
            // Pixel (x,y,z) -> (x/4-1, 1-y/4, z); preserve depth to test the batch state.
            Matrix projection = Matrix::getIdentityProperty();
            projection.M11 = .25f;
            projection.M22 = -.25f;
            projection.M41 = -1;
            projection.M42 = 1;
            transform->SetValue(projection);
            return effect;
        }

        std::unique_ptr<GraphicsDevice> device_;
        std::unique_ptr<RenderTarget2D> target_;
    };
}

TEST_F(Fna3dPipelinePixels, DisabledDepthNeitherRejectsNorWrites)
{
    // Oracle: XNA DepthBufferEnable=false disables both testing and writes, regardless of
    // DepthBufferWriteEnable. Never must not reject; .25 must not occlude the later .5 quad.
    for (bool write : {false, true, false})
    {
        SCOPED_TRACE(write);
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(write);
        state.setDepthBufferFunctionProperty(CompareFunction::Never);
        Prepare();
        device_->setDepthStencilStateProperty(state);
        Draw(.75f, Color::Red);
        EXPECT_EQ(Pixel(), Color::Red);
        Prepare();
        device_->setDepthStencilStateProperty(state);
        Draw(.25f, Color::Red);
        device_->setDepthStencilStateProperty(DepthStencilState::Default);
        Draw(.5f, Color::Lime);
        EXPECT_EQ(Pixel(), Color::Lime);
    }
}

TEST_F(Fna3dPipelinePixels, DepthWriteChangesSurviveStateReuse)
{
    // Oracle: LessEqual rejects .5 against a written .25, accepts against clear depth 1.
    for (bool write : {true, false, true})
    {
        Prepare();
        DepthStencilState state;
        state.setDepthBufferWriteEnableProperty(write);
        device_->setDepthStencilStateProperty(state);
        Draw(.25f, Color::Red);
        device_->setDepthStencilStateProperty(DepthStencilState::Default);
        Draw(.5f, Color::Lime);
        EXPECT_EQ(Pixel(), write ? Color::Red : Color::Lime) << write;
    }
}

TEST_F(Fna3dPipelinePixels, TwoSidedStencilUsesClockwiseOrdinaryState)
{
    // Oracle: XNA's ordinary stencil state is CW, CounterClockwise* is CCW.
    // Replace writes 5; Invert on clear stencil 0 writes 255.
    // Known dependency defect (TQ-017, handoff.md 5.1): FNA3D's OpenGL driver maps the ordinary
    // state to GL_FRONT but only issues glFrontFace while culling, so with CullMode.None the
    // faces follow a stale front face and the two states land on the wrong windings. The
    // assertion stands; it runs where the driver is known to honour it.
    const char* const driver = std::getenv("FNA3D_FORCE_DRIVER");
    if (driver == nullptr || std::string(driver) != "SDLGPU")
    {
        GTEST_SKIP() << "FNA3D OpenGL driver two-sided stencil defect (handoff.md 5.1); "
                        "run with FNA3D_FORCE_DRIVER=SDLGPU";
    }
    for (bool reverse : {false, true, false})
    {
        Prepare();
        DepthStencilState mark;
        mark.setDepthBufferEnableProperty(false);
        mark.setStencilEnableProperty(true);
        mark.setTwoSidedStencilModeProperty(true);
        mark.setReferenceStencilProperty(5);
        mark.setStencilPassProperty(StencilOperation::Replace);
        mark.setCounterClockwiseStencilPassProperty(StencilOperation::Invert);
        device_->setDepthStencilStateProperty(mark);
        Draw(.5f, Color::Blue, reverse);
        DepthStencilState probe;
        probe.setDepthBufferEnableProperty(false);
        probe.setStencilEnableProperty(true);
        probe.setStencilFunctionProperty(CompareFunction::Equal);
        probe.setReferenceStencilProperty(5);
        device_->setDepthStencilStateProperty(probe);
        Draw(.5f, Color::Red);
        DepthStencilState invertedProbe;
        invertedProbe.setDepthBufferEnableProperty(false);
        invertedProbe.setStencilEnableProperty(true);
        invertedProbe.setStencilFunctionProperty(CompareFunction::Equal);
        invertedProbe.setReferenceStencilProperty(255);
        device_->setDepthStencilStateProperty(invertedProbe);
        Draw(.5f, Color::Lime);
        EXPECT_EQ(Pixel(), reverse ? Color::Lime : Color::Red);
    }
}

TEST_F(Fna3dPipelinePixels, CustomSpriteHonoursDepthFunction)
{
    auto custom = CustomSprite();
    ASSERT_NE(custom, nullptr);
    Texture2D white(*device_, 1, 1);
    const Color value = Color::White;
    white.SetData(&value, 1);
    SpriteBatch batch(*device_);
    // Oracle: FNA SpriteBatch.PrepRenderState installs the supplied depth/stencil state
    // before either stock or custom passes. Always/ Never have identical geometry.
    for (Effect* effect : {static_cast<Effect*>(nullptr), custom.get()})
        for (bool never : {true, false, true})
        {
            Prepare();
            DepthStencilState state;
            state.setDepthBufferFunctionProperty(never ? CompareFunction::Never : CompareFunction::Always);
            Sprite(batch, white, effect, state, .5f, Color::Red);
            EXPECT_EQ(Pixel(), never ? Color::Black : Color::Red) << never << '/' << (effect != nullptr);
        }
}

TEST_F(Fna3dPipelinePixels, CustomSpriteHonoursStencilFunctionAndReference)
{
    auto custom = CustomSprite();
    ASSERT_NE(custom, nullptr);
    Texture2D white(*device_, 1, 1);
    const Color value = Color::White;
    white.SetData(&value, 1);
    SpriteBatch batch(*device_);
    // Oracle: stencil Equal compares reference & mask to attachment & mask. Clear=5;
    // refs 5,7,5 alternate pass/fail/pass; Never rejects even a matching reference.
    for (Effect* effect : {static_cast<Effect*>(nullptr), custom.get()})
    {
        for (int reference : {5, 7, 5})
        {
            Prepare(5);
            DepthStencilState state;
            state.setDepthBufferEnableProperty(false);
            state.setStencilEnableProperty(true);
            state.setStencilFunctionProperty(CompareFunction::Equal);
            state.setReferenceStencilProperty(reference);
            Sprite(batch, white, effect, state, .5f, Color::Red);
            EXPECT_EQ(Pixel(), reference == 5 ? Color::Red : Color::Black)
                << reference << '/' << (effect != nullptr);
        }
        Prepare(5);
        DepthStencilState never;
        never.setDepthBufferEnableProperty(false);
        never.setStencilEnableProperty(true);
        never.setStencilFunctionProperty(CompareFunction::Never);
        never.setReferenceStencilProperty(5);
        Sprite(batch, white, effect, never, .5f, Color::Red);
        EXPECT_EQ(Pixel(), Color::Black);
    }
}

TEST_F(Fna3dPipelinePixels, CustomSpriteHonoursDepthWrites)
{
    auto custom = CustomSprite();
    ASSERT_NE(custom, nullptr);
    Texture2D white(*device_, 1, 1);
    const Color value = Color::White;
    white.SetData(&value, 1);
    SpriteBatch batch(*device_);
    // Oracle: LessEqual, .25 write occludes .5; read-only .25 leaves clear depth 1.
    for (Effect* effect : {static_cast<Effect*>(nullptr), custom.get()})
        for (bool write : {true, false, true})
        {
            Prepare();
            DepthStencilState state;
            state.setDepthBufferWriteEnableProperty(write);
            Sprite(batch, white, effect, state, .25f, Color::Red);
            Sprite(batch, white, effect, DepthStencilState::Default, .5f, Color::Lime);
            EXPECT_EQ(Pixel(), write ? Color::Red : Color::Lime) << write << '/' << (effect != nullptr);
        }
}

TEST_F(Fna3dPipelinePixels, IndexElementWidthPreservesHighVertexIndices)
{
    // Oracle: 32-bit indices 65536..65541 must not truncate to 0..5. All other vertices
    // are degenerate blue triangles, so a red interior pixel proves the intended range was read.
    for (bool wide : {false, true, false})
    {
        const int first = wide ? 65536 : 4;
        std::vector<VertexPositionColor> vertices(first + 6,
            VertexPositionColor(Vector3::Zero, Color::Blue));
        const auto quad = Quad(.5f, Color::Red);
        std::copy(quad.begin(), quad.end(), vertices.begin() + first);
        VertexBuffer vb(*device_, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vb.SetData(vertices.data(), static_cast<int>(vertices.size()));
        IndexBuffer ib(*device_, wide ? IndexElementSize::ThirtyTwoBits : IndexElementSize::SixteenBits,
            6, BufferUsage::WriteOnly);
        if (wide)
        {
            const std::uint32_t indices[]{65536, 65537, 65538, 65539, 65540, 65541};
            ib.SetData(indices, 6);
        }
        else
        {
            const std::uint16_t indices[]{4, 5, 6, 7, 8, 9};
            ib.SetData(indices, 6);
        }
        Prepare();
        device_->setDepthStencilStateProperty(DepthStencilState::None);
        BasicEffect effect(*device_);
        effect.setVertexColorEnabledProperty(true);
        effect.Apply();
        device_->SetVertexBuffer(&vb);
        device_->setIndicesProperty(&ib);
        device_->DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
            static_cast<int>(vertices.size()), 0, 2);
        EXPECT_EQ(Pixel(), Color::Red) << wide;
        device_->SetVertexBuffer(nullptr);
        device_->setIndicesProperty(nullptr);
    }
}

TEST_F(Fna3dPipelinePixels, MinMaxBlendOperationsRemainDistinct)
{
    // Oracle: min(red,green)=black, max(red,green)=yellow, independent of factors.
    // Use One/One (XNA disallows Min/Max with the One/Zero opaque shortcut).
    for (BlendFunction function : {BlendFunction::Min, BlendFunction::Max, BlendFunction::Min})
    {
        Prepare();
        device_->Clear(Color::Lime);
        BlendState blend;
        blend.setColorSourceBlendProperty(Blend::One);
        blend.setColorDestinationBlendProperty(Blend::One);
        blend.setColorBlendFunctionProperty(function);
        device_->setBlendStateProperty(blend);
        device_->setDepthStencilStateProperty(DepthStencilState::None);
        Draw(.5f, Color::Red);
        EXPECT_EQ(Pixel(), function == BlendFunction::Min ? Color::Black : Color::Yellow);
    }
}


TEST_F(Fna3dPipelinePixels, ColdNullSecondTexturePreservesFirstTextureAlpha)
{
    // Oracle: DualTextureEffect.fx PSDualTextureNoFog: RGB = 2*T0.rgb*T1.rgb,
    // alpha = T0.a*T1.a. XNA's null sampler is opaque black (null-texture oracle).
    // SetUp creates a fresh device and performs no draws, so iteration zero is the first
    // use of its null fallback. A nonopaque T0 exposes lazy T1 creation stealing unit 0.
    struct Vertex { float x, y, z, u, v, u2, v2; };
    const Vertex vertices[]{
        {-1, 1, 0, 0, 0, 0, 0}, {1, 1, 0, 1, 0, 1, 0}, {-1, -1, 0, 0, 1, 0, 1},
        {1, 1, 0, 1, 0, 1, 0}, {1, -1, 0, 1, 1, 1, 1}, {-1, -1, 0, 0, 1, 0, 1}};
    const VertexDeclaration declaration(28, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1)});
    Texture2D texture(*device_, 1, 1);
    const Color texel(64, 128, 192, 96);
    texture.SetData(&texel, 1);
    DualTextureEffect effect(*device_);
    effect.setTextureProperty(&texture);
    effect.setTexture2Property(nullptr);
    for (int draw = 0; draw < 2; ++draw)
    {
        Prepare();
        device_->setDepthStencilStateProperty(DepthStencilState::None);
        effect.Apply();
        device_->DrawUserPrimitives(PrimitiveType::TriangleList,
            static_cast<const void*>(vertices), 0, 2, declaration);
        const Color pixel = Pixel();
        EXPECT_EQ(pixel, Color(0, 0, 0, 96)) << (draw == 0 ? "cold fallback" : "warm fallback");
        EXPECT_EQ(static_cast<int>(pixel.getAProperty()), 96)
            << (draw == 0 ? "cold fallback" : "warm fallback");
    }
}
#endif
