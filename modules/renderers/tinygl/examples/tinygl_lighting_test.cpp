// SPDX-License-Identifier: MS-PL
// BasicEffect per-vertex lighting through TinyGL's fixed-function light/material pipeline.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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
    constexpr int kChecks = 13;

    Color CenterPixel(GraphicsDevice& device)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool Near(int actual, int expected, int tolerance = 4)
    {
        return actual >= expected - tolerance && actual <= expected + tolerance;
    }

    void SetLight(DirectionalLight& light, const Vector3& direction,
                  const Vector3& diffuse, const Vector3& specular = Vector3::Zero)
    {
        light.setEnabledProperty(true);
        light.setDirectionProperty(direction);
        light.setDiffuseColorProperty(diffuse);
        light.setSpecularColorProperty(specular);
    }
}

class TinyGLLightingTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static void ConfigureBase(BasicEffect& effect, const Matrix& world = Matrix::getIdentityProperty())
    {
        effect.setWorldProperty(world);
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = false;
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setTextureProperty(nullptr);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.setSpecularPowerProperty(16.0f);
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
    }

    static void DrawQuad(GraphicsDevice& device, BasicEffect& effect, const Vector3& normal,
                         bool indexed = false)
    {
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-0.8f,  0.8f, -0.5f), normal, Vector2(0, 0)},
            {Vector3(-0.8f, -0.8f, -0.5f), normal, Vector2(0, 1)},
            {Vector3( 0.8f, -0.8f, -0.5f), normal, Vector2(1, 1)},
            {Vector3(-0.8f,  0.8f, -0.5f), normal, Vector2(0, 0)},
            {Vector3( 0.8f, -0.8f, -0.5f), normal, Vector2(1, 1)},
            {Vector3( 0.8f,  0.8f, -0.5f), normal, Vector2(1, 0)},
        };
        effect.Apply();
        if (!indexed)
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
            return;
        }

        const VertexPositionNormalTexture corners[4] = {
            vertices[0], vertices[1], vertices[2], vertices[5]};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, corners, 0, 4, indices, 0, 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);

        BasicEffect ambientEffect(device);
        ConfigureBase(ambientEffect);
        ambientEffect.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 1.0f));
        ambientEffect.setAmbientLightColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        ambientEffect.setEmissiveColorProperty(Vector3(0.1f, 0.0f, 0.0f));
        device.Clear(Color::Black);
        DrawQuad(device, ambientEffect, Vector3(0, 0, 1));
        Color pixel = CenterPixel(device);
        Check(Near(pixel.getRProperty(), 89) && Near(pixel.getGProperty(), 32) &&
                  Near(pixel.getBProperty(), 127),
              "ambient*diffuse plus emissive matches BasicEffect material math");

        BasicEffect frontEffect(device);
        ConfigureBase(frontEffect);
        frontEffect.setDiffuseColorProperty(Vector3(1, 0, 0));
        SetLight(frontEffect.getDirectionalLight0Property(), Vector3(0, 0, -1), Vector3::One);
        device.Clear(Color::Black);
        DrawQuad(device, frontEffect, Vector3(0, 0, 1));
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() >= 250 && pixel.getGProperty() <= 3,
              "front-facing normal receives directional diffuse light");

        device.Clear(Color::Black);
        DrawQuad(device, frontEffect, Vector3(0, 0, -1));
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() <= 3,
              "back-facing normal receives no one-sided directional diffuse light");

        BasicEffect multiLightEffect(device);
        ConfigureBase(multiLightEffect);
        SetLight(multiLightEffect.getDirectionalLight1Property(),
                 Vector3(0, 0, -1), Vector3(1, 0, 0));
        SetLight(multiLightEffect.getDirectionalLight2Property(),
                 Vector3(0, 0, -1), Vector3(0, 0, 1));
        device.Clear(Color::Black);
        DrawQuad(device, multiLightEffect, Vector3(0, 0, 1));
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() >= 250 && pixel.getGProperty() <= 3 &&
                  pixel.getBProperty() >= 250,
              "DirectionalLight1 and DirectionalLight2 both contribute");

        Texture2D opaqueBlack(device, 1, 1, false, SurfaceFormat::Color);
        const Color opaqueBlackPixel(0, 0, 0, 255);
        opaqueBlack.SetData(&opaqueBlackPixel, 1);
        BasicEffect specularEffect(device);
        ConfigureBase(specularEffect);
        specularEffect.setDiffuseColorProperty(Vector3::Zero);
        specularEffect.setSpecularColorProperty(Vector3::One);
        specularEffect.setSpecularPowerProperty(1.0f);
        specularEffect.setTextureEnabledProperty(true);
        specularEffect.setTextureProperty(&opaqueBlack);
        SetLight(specularEffect.getDirectionalLight0Property(), Vector3(0, 0, -1),
                 Vector3::Zero, Vector3::One);
        device.Clear(Color::Black);
        DrawQuad(device, specularEffect, Vector3(0, 0, 1));
        pixel = CenterPixel(device);
        const Color opaqueSpecular = pixel;
        Check(pixel.getRProperty() >= 200 && pixel.getGProperty() >= 200 &&
                  pixel.getBProperty() >= 200,
              "specular is added after a black texture instead of being modulated by it");

        Texture2D translucentBlack(device, 1, 1, false, SurfaceFormat::Color);
        const Color translucentBlackPixel(0, 0, 0, 64);
        translucentBlack.SetData(&translucentBlackPixel, 1);
        specularEffect.setTextureProperty(&translucentBlack);
        device.Clear(Color::Black);
        DrawQuad(device, specularEffect, Vector3(0, 0, 1));
        pixel = CenterPixel(device);
        Check(Near(pixel.getRProperty(), opaqueSpecular.getRProperty() / 4, 4) &&
                  Near(pixel.getGProperty(), opaqueSpecular.getGProperty() / 4, 4) &&
                  Near(pixel.getBProperty(), opaqueSpecular.getBProperty() / 4, 4),
              "separate specular pass is multiplied by the source texture alpha");

        BasicEffect indexedEffect(device);
        ConfigureBase(indexedEffect);
        indexedEffect.setDiffuseColorProperty(Vector3(0, 1, 0));
        SetLight(indexedEffect.getDirectionalLight0Property(), Vector3(0, 0, -1), Vector3::One);
        device.Clear(Color::Black);
        DrawQuad(device, indexedEffect, Vector3(0, 0, 1), true);
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() <= 3 && pixel.getGProperty() >= 250 &&
                  pixel.getBProperty() <= 3,
              "indexed VertexPositionNormalTexture uses the same lighting path");

        specularEffect.setTextureProperty(&opaqueBlack);
        device.Clear(Color::Black);
        DrawQuad(device, specularEffect, Vector3(0, 0, 1), true);
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() >= 200 && pixel.getGProperty() >= 200 &&
                  pixel.getBProperty() >= 200,
              "indexed draws execute the separate specular pass too");

        BasicEffect normalMatrixEffect(device);
        ConfigureBase(normalMatrixEffect, Matrix::CreateScale(2.0f, 1.0f, 1.0f));
        SetLight(normalMatrixEffect.getDirectionalLight0Property(),
                 Vector3(0, -1, 0), Vector3::One);
        device.Clear(Color::Black);
        DrawQuad(device, normalMatrixEffect,
                 Vector3::Normalize(Vector3(1, 1, 0)));
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() >= 210 && pixel.getRProperty() <= 240,
              "non-uniform World scale uses the inverse-transpose normal transform");

        BasicEffect defaultLightingEffect(device);
        defaultLightingEffect.EnableDefaultLighting();
        defaultLightingEffect.setProjectionProperty(Matrix::getIdentityProperty());
        device.Clear(Color::Black);
        DrawQuad(device, defaultLightingEffect, Vector3(0, 0, 1));
        pixel = CenterPixel(device);
        Check(pixel.getRProperty() >= 80 && pixel.getGProperty() >= 80 &&
                  pixel.getBProperty() >= 80,
              "BasicEffect.EnableDefaultLighting executes without a renderer-specific workaround");

        BasicEffect perPixelEffect(device);
        ConfigureBase(perPixelEffect);
        perPixelEffect.setPreferPerPixelLightingProperty(true);
        bool rejectedPerPixel = false;
        try
        {
            DrawQuad(device, perPixelEffect, Vector3(0, 0, 1));
        }
        catch (const System::NotSupportedException&)
        {
            rejectedPerPixel = true;
        }
        Check(rejectedPerPixel,
              "PreferPerPixelLighting is refused instead of silently using per-vertex lighting");

        BasicEffect missingNormalEffect(device);
        ConfigureBase(missingNormalEffect);
        missingNormalEffect.Apply();
        const VertexPositionTexture noNormals[3] = {
            {Vector3(-0.5f, 0.5f, 0), Vector2(0, 0)},
            {Vector3(-0.5f, -0.5f, 0), Vector2(0, 1)},
            {Vector3(0.5f, -0.5f, 0), Vector2(1, 1)},
        };
        bool rejectedMissingNormal = false;
        try
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, noNormals, 0, 1);
        }
        catch (const System::NotSupportedException&)
        {
            rejectedMissingNormal = true;
        }
        Check(rejectedMissingNormal,
              "lighting without a Normal declaration is refused before TinyGL submission");

        device.setBlendStateProperty(BlendState::AlphaBlend);
        bool rejectedSpecularBlend = false;
        try
        {
            DrawQuad(device, specularEffect, Vector3(0, 0, 1));
        }
        catch (const System::NotSupportedException&)
        {
            rejectedSpecularBlend = true;
        }
        Check(rejectedSpecularBlend,
              "specular plus a non-opaque BlendState is refused before the two-pass draw");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    TinyGLLightingTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    TinyGLLightingTest test;
    test.Run();
    return test.Result();
}
