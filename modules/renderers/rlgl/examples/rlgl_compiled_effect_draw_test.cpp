// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-048/RLGL-051: public golden-pixel evidence for the ordinary
// compiled-Effect draw matrix and SpriteBatch's embedded stock vertex-effect inheritance.

#if !defined(CNA_RLGL_COMPILED_EFFECTS)
#error "This fixture requires CNA_RLGL_COMPILED_EFFECTS"
#endif

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class RlglCompiledEffectDrawTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglCompiledEffectDrawTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        if (!Check(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects),
                   "RLGL advertises compiled effects after the executable draw path is enabled"))
        {
            return;
        }

        Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        Check(parameters["Tint"] != nullptr && parameters["Transform"] != nullptr,
              "drawable fixture exposes Tint and Transform");
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

        struct ClipVertex
        {
            float x;
            float y;
            float z;
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const std::array<ClipVertex, 6> quad{{
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        }};
        const std::array<std::uint16_t, 6> indices16{{0, 1, 2, 3, 4, 5}};
        const std::array<std::uint32_t, 6> indices32{{0, 1, 2, 3, 4, 5}};

        const Color background(9, 19, 29, 255);
        const auto drawAndReadCentre =
            [&](const Vector4& tint, const std::function<void()>& draw) -> Color
        {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            parameters["Tint"]->SetValue(tint);
            pass.Apply();
            draw();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };
        const auto checkTint = [&](const Color& actual, const Vector4& tint, const char* label)
        {
            const auto channel = [](const float value)
            {
                return static_cast<int>(value * 255.0f + 0.5f);
            };
            const bool matches =
                std::abs(actual.getRProperty() - channel(tint.X)) <= 2 &&
                std::abs(actual.getGProperty() - channel(tint.Y)) <= 2 &&
                std::abs(actual.getBProperty() - channel(tint.Z)) <= 2 &&
                std::abs(actual.getAProperty() - channel(tint.W)) <= 2;
            Check(matches, label);
        };

        {
            const Vector4 tint(0.25f, 0.5f, 0.75f, 1.0f);
            VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
            buffer.SetDataRaw(quad.data(), 6, static_cast<int>(sizeof(ClipVertex)));
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.SetVertexBuffer(&buffer);
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            });
            checkTint(pixel, tint, "buffered non-indexed compiled draw uses current uniforms");
            device.SetVertexBuffer(nullptr);
        }

        {
            const Vector4 tint(0.75f, 0.25f, 0.125f, 1.0f);
            VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
            buffer.SetDataRaw(quad.data(), 6, static_cast<int>(sizeof(ClipVertex)));
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            indexBuffer.SetData(indices16.data(), 6);
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.SetVertexBuffer(&buffer);
                device.setIndicesProperty(&indexBuffer);
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2);
            });
            checkTint(pixel, tint, "buffered 16-bit indexed compiled draw");
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
        }

        {
            const Vector4 tint(0.375f, 0.875f, 0.625f, 1.0f);
            constexpr int vertexPad = 12289;
            std::vector<ClipVertex> padded(
                vertexPad + 6, ClipVertex{0.0f, 0.0f, 0.0f});
            for (int i = 0; i < 6; ++i) padded[vertexPad + i] = quad[i];
            const std::array<std::uint16_t, 12> paddedIndices{{
                0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5,
            }};
            VertexBuffer buffer(
                device, declaration, static_cast<int>(padded.size()), BufferUsage::None);
            buffer.SetDataRaw(
                padded.data(), static_cast<int>(padded.size()),
                static_cast<int>(sizeof(ClipVertex)));
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 12, BufferUsage::None);
            indexBuffer.SetData(paddedIndices.data(), 12);
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.SetVertexBuffer(&buffer);
                device.setIndicesProperty(&indexBuffer);
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList, vertexPad, 0, 6, 6, 2);
            });
            checkTint(pixel, tint, "buffered indexed compiled draw honors baseVertex/startIndex");
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
        }

        {
            const Vector4 tint(0.625f, 0.125f, 0.25f, 1.0f);
            std::array<ClipVertex, 9> padded{};
            for (int i = 0; i < 6; ++i) padded[3 + i] = quad[i];
            VertexBuffer buffer(device, declaration, 9, BufferUsage::None);
            buffer.SetDataRaw(padded.data(), 9, static_cast<int>(sizeof(ClipVertex)));
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.SetVertexBuffer(&buffer);
                device.DrawPrimitives(PrimitiveType::TriangleList, 3, 2);
            });
            checkTint(pixel, tint, "buffered compiled draw honors vertexStart");
            device.SetVertexBuffer(nullptr);
        }

        {
            const Vector4 tint(0.5f, 0.5f, 0.25f, 1.0f);
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.DrawUserPrimitives(
                    PrimitiveType::TriangleList, static_cast<const void*>(quad.data()),
                    0, 2, declaration);
            });
            checkTint(pixel, tint, "user non-indexed compiled draw");
        }

        {
            const Vector4 tint(0.125f, 0.625f, 0.875f, 1.0f);
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.DrawUserIndexedPrimitives(
                    PrimitiveType::TriangleList, static_cast<const void*>(quad.data()),
                    0, 6, indices16.data(), 0, 2, declaration);
            });
            checkTint(pixel, tint, "user 16-bit indexed compiled draw");
        }

        {
            const Vector4 tint(0.875f, 0.375f, 0.5f, 1.0f);
            std::array<VertexPositionColor, 6> typedQuad;
            for (int i = 0; i < 6; ++i)
            {
                typedQuad[i] = VertexPositionColor(
                    Vector3(quad[i].x, quad[i].y, quad[i].z), Color::White);
            }
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.DrawUserPrimitives(
                    PrimitiveType::TriangleList, typedQuad.data(), 0, 2);
            });
            checkTint(pixel, tint, "typed user non-indexed compiled draw");
        }

        {
            const Vector4 tint(0.25f, 0.75f, 0.5f, 1.0f);
            std::array<VertexPositionColor, 6> typedQuad;
            for (int i = 0; i < 6; ++i)
            {
                typedQuad[i] = VertexPositionColor(
                    Vector3(quad[i].x, quad[i].y, quad[i].z), Color::White);
            }
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.DrawUserIndexedPrimitives(
                    PrimitiveType::TriangleList, typedQuad.data(), 0, 6,
                    indices16.data(), 0, 2);
            });
            checkTint(pixel, tint, "typed user 16-bit indexed compiled draw");
        }

        {
            const Vector4 tint(0.625f, 0.75f, 0.125f, 1.0f);
            VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
            buffer.SetDataRaw(quad.data(), 6, static_cast<int>(sizeof(ClipVertex)));
            IndexBuffer indexBuffer(device, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
            indexBuffer.SetData(indices32.data(), 6);
            const Color pixel = drawAndReadCentre(tint, [&]
            {
                device.SetVertexBuffer(&buffer);
                device.setIndicesProperty(&indexBuffer);
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2);
            });
            checkTint(pixel, tint, "buffered 32-bit indexed compiled draw");
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
        }

        {
            CNA::TestSupport::SyntheticEffectOptions options;
            options.includeSampler = true;
            Effect spriteEffect(device, CNA::TestSupport::BuildSyntheticEffect(options));
            const Vector4 tint(0.25f, 0.5f, 0.75f, 1.0f);
            spriteEffect.getParametersProperty()["Tint"]->SetValue(tint);

            Texture2D sprite(device, 1, 1);
            const Color white[1] = {Color::White};
            sprite.SetData(white, 1);
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(background);
            SpriteBatch batch(device);
            batch.Begin(
                SpriteSortMode::Deferred, BlendState::Opaque,
                nullptr, nullptr, nullptr, &spriteEffect);
            batch.Draw(sprite, Rectangle(0, 0, 8, 8), Color::White);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            checkTint(
                pixel, tint,
                "pixel-only SpriteBatch effect inherits the embedded stock vertex shader");
        }
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglCompiledEffectDrawTest>();
}
