// SPDX-License-Identifier: MS-PL
// Built-in fixed-function vertex layouts without a packed Color channel.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 48;
    constexpr int kChecks = 4;

    Color CenterPixel(GraphicsDevice& device)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool Near(int actual, int expected, int tolerance = 3)
    {
        return actual >= expected - tolerance && actual <= expected + tolerance;
    }
}

class TinyGLFixedLayoutsTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static void Configure(BasicEffect& effect, const Vector3& diffuse,
                          Texture2D* texture = nullptr)
    {
        effect.VertexColorEnabled = false;
        effect.setDiffuseColorProperty(diffuse);
        effect.setTextureEnabledProperty(texture != nullptr);
        effect.setTextureProperty(texture);
        effect.Apply();
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        Texture2D texture(device, 2, 1, false, SurfaceFormat::Color);
        const Color texels[2] = {Color::Red, Color::Green};
        texture.SetData(texels, 2);

        const VertexPositionTexture vpt[6] = {
            {Vector3(-0.8f,  0.8f, 0), Vector2(0.0f, 0)},
            {Vector3(-0.8f, -0.8f, 0), Vector2(0.0f, 0)},
            {Vector3( 0.8f, -0.8f, 0), Vector2(0.0f, 0)},
            {Vector3(-0.8f,  0.8f, 0), Vector2(0.0f, 0)},
            {Vector3( 0.8f, -0.8f, 0), Vector2(0.0f, 0)},
            {Vector3( 0.8f,  0.8f, 0), Vector2(0.0f, 0)},
        };
        BasicEffect vptEffect(device);
        Configure(vptEffect, Vector3(0.5f, 1.0f, 1.0f), &texture);
        device.Clear(Color::Black);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vpt, 0, 2);
        Color pixel = CenterPixel(device);
        Check(Near(pixel.getRProperty(), 127) && pixel.getGProperty() <= 3 &&
                  pixel.getBProperty() <= 3,
              "VertexPositionTexture reads UV at offset 12 and applies BasicEffect diffuse tint");

        const Vector3 normal(0, 0, 1);
        const VertexPositionNormalTexture vpnt[4] = {
            {Vector3(-0.8f,  0.8f, 0), normal, Vector2(0.75f, 0)},
            {Vector3(-0.8f, -0.8f, 0), normal, Vector2(0.75f, 0)},
            {Vector3( 0.8f, -0.8f, 0), normal, Vector2(0.75f, 0)},
            {Vector3( 0.8f,  0.8f, 0), normal, Vector2(0.75f, 0)},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        BasicEffect vpntTextureEffect(device);
        Configure(vpntTextureEffect, Vector3::One, &texture);
        device.Clear(Color::Black);
        device.DrawUserPrimitives(PrimitiveType::TriangleStrip, vpnt, 0, 2);
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() <= 3 && pixel.getGProperty() >= 120 &&
                  pixel.getBProperty() <= 3,
              "VertexPositionNormalTexture reads UV at offset 24 on the non-indexed route");

        BasicEffect vpntColorEffect(device);
        Configure(vpntColorEffect, Vector3(0.0f, 0.0f, 1.0f));
        device.Clear(Color::Black);
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, vpnt, 0, 4, indices, 0, 2);
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() <= 3 && pixel.getGProperty() <= 3 &&
                  pixel.getBProperty() >= 250,
              "untextured indexed VertexPositionNormalTexture uses BasicEffect diffuse color");

        const VertexDeclaration spoofed(32, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer spoofedBuffer(device, spoofed, 4, BufferUsage::None);
        spoofedBuffer.SetData(vpnt, 4);
        device.SetVertexBuffer(&spoofedBuffer);
        Configure(vpntColorEffect, Vector3::One);
        bool rejected = false;
        try
        {
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        }
        catch (const System::NotSupportedException&)
        {
            rejected = true;
        }
        device.SetVertexBuffer(nullptr);
        Check(rejected,
              "stride 32 is refused when its declaration substitutes Tangent for Normal");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    TinyGLFixedLayoutsTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    TinyGLFixedLayoutsTest test;
    test.Run();
    return test.Result();
}
