// SPDX-License-Identifier: MS-PL
// SAMPLE-073: DualTextureEffect must consume TEXCOORD0 and TEXCOORD1 independently.

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct PositionNormalDualTexture
    {
        Vector3 position;
        Vector3 normal;
        Vector2 textureCoordinate0;
        Vector2 textureCoordinate1;
    };

    static_assert(std::is_trivially_copyable_v<PositionNormalDualTexture>);
    static_assert(sizeof(PositionNormalDualTexture) == 40);

    const VertexDeclaration kDeclaration(
        40,
        {
            VertexElement(0, VertexElementFormat::Vector3,
                          VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
        });

    class IndependentUvTest final : public Game
    {
    public:
        IndependentUvTest()
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(64);
        }

        [[nodiscard]] int getResult() const { return result_; }

    protected:
        void Draw(const GameTime&) override
        {
            if (done_) return;
            done_ = true;

            GraphicsDevice& device = getGraphicsDeviceProperty();
            const Color texture0Pixels[2] = {
                Color(128, 0, 0, 255),
                Color(0, 128, 0, 255),
            };
            const Color texture1Pixels[2] = {
                Color(0, 0, 255, 255),
                Color(255, 255, 0, 255),
            };
            Texture2D texture0(device, 2, 1);
            Texture2D texture1(device, 2, 1);
            texture0.SetData(texture0Pixels, 2);
            texture1.SetData(texture1Pixels, 2);

            const Vector3 normal = Vector3::Up;
            const Vector2 uv0(0.25f, 0.5f);
            const Vector2 uv1(0.75f, 0.5f);
            const std::array<PositionNormalDualTexture, 6> quad{{
                {Vector3(-1.0f,  1.0f, 0.0f), normal, uv0, uv1},
                {Vector3(-1.0f, -1.0f, 0.0f), normal, uv0, uv1},
                {Vector3( 1.0f, -1.0f, 0.0f), normal, uv0, uv1},
                {Vector3(-1.0f,  1.0f, 0.0f), normal, uv0, uv1},
                {Vector3( 1.0f, -1.0f, 0.0f), normal, uv0, uv1},
                {Vector3( 1.0f,  1.0f, 0.0f), normal, uv0, uv1},
            }};

            VertexBuffer vertexBuffer(
                device, kDeclaration, static_cast<int>(quad.size()), BufferUsage::None);
            vertexBuffer.SetData(quad.data(), static_cast<int>(quad.size()));

            DualTextureEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setTextureProperty(&texture0);
            effect.setTexture2Property(&texture1);

            device.Clear(Color::Black);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
            device.SetVertexBuffer(&vertexBuffer);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            Color center = Color::Black;
            const Rectangle region(32, 32, 1, 1);
            device.GetBackBufferData(&region, &center, 0, 1);

            // texture0 uses its left red texel; Texture2 uses its right yellow texel.
            // FNA's 2x base formula therefore yields red. Reusing TEXCOORD0 for both
            // instead samples Texture2's blue texel and yields black.
            const bool pass = center.getRProperty() >= 240 &&
                              center.getGProperty() <= 15 &&
                              center.getBProperty() <= 15;
            std::printf("[%s] DualTextureEffect independent UV: (%u,%u,%u)\n",
                        pass ? "PASS" : "FAIL",
                        static_cast<unsigned>(center.getRProperty()),
                        static_cast<unsigned>(center.getGProperty()),
                        static_cast<unsigned>(center.getBProperty()));
            result_ = pass ? 0 : 1;
            Exit();
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
        bool done_ = false;
        int result_ = 1;
    };
}

int main()
{
    IndependentUvTest game;
    game.Run();
    return game.getResult();
}
