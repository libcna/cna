// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-25 follow-up: DualTextureEffect, the first of the four still-open BasicEffect-
// family stock effects to land on the LLGL renderer.
//
// Reuses the plain textured/colored_textured vertex shader as-is: DualTextureEffect samples its
// second texture through the SAME texture coordinate as the first (no separate UV set), so the
// vertex-side behaviour is identical to a single-texture draw and only the fragment shader
// (dual_textured3d.frag.glsl) and its two-sampler pipeline layout are new. FNA's own PSDualTexture
// computes `base = SAMPLE(Texture, uv); base.rgb *= 2; color = base * overlay * tint`, where
// `overlay = SAMPLE(Texture2, uv)` -- constants and expected values below are cross-checkable
// against examples/dualtextureeffect_vertexcolor_test.cpp (the shared EasyGL/Vulkan/Bgfx source
// this mirrors; not reused verbatim here because it drives GraphicsDevice::DrawUserPrimitives(),
// whose internal renderer_->CreateVertexBuffer(int)-based buffer carries no attribute layout at
// all -- a real, pre-existing LLGL gap affecting every DrawUserPrimitives typed overload, not
// specific to DualTextureEffect, and out of this task's scope).
//
// Check A -- VertexColorEnabled=true: RGB == VertexColor * DiffuseColor * Alpha (the half-gray
//   base texture's *2 and the white overlay both act as identity factors).
// Check B -- VertexColorEnabled=false: RGB == DiffuseColor * Alpha only (the vertex colour factor
//   drops out).
// Check C -- the two textures are genuinely INDEPENDENT samples, not the same texture bound twice:
//   a white base and a solid red overlay produce a red-tinted result, not white.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // Identical constants to examples/dualtextureeffect_vertexcolor_test.cpp (and, per that file's
    // own header comment, to Task 377/887's AlphaTestEffect vertex-colour tests) -- so the expected
    // values are directly cross-checkable against that established precedent.
    const Color kHalfGray(128, 128, 128, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kVertexColor(200, 100, 50, 200);
    const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
    constexpr float kEffectAlpha = 0.8f;

    // VertexColorEnabled=true: BaseTexture(~0.502*2~=1.0) * Overlay(1,1,1) * VertexColor(200,100,50)/255
    // * DiffuseColor * Alpha.
    const Color kExpectedEnabled(96, 32, 32, 255);
    // VertexColorEnabled=false: DiffuseColor * Alpha only (vertex colour factor dropped).
    const Color kExpectedDisabled(122, 82, 163, 255);

    std::vector<VertexPositionColorTexture> MakeQuad(int size, const Color& color)
    {
        const auto vertex = [&color](float x, float y, float u, float v) {
            return VertexPositionColorTexture(Vector3(x, y, 0.0f), color, Vector2(u, v));
        };
        const float s = static_cast<float>(size);
        return {
            vertex(0.0f, 0.0f, 0.0f, 0.0f), vertex(s, 0.0f, 1.0f, 0.0f), vertex(0.0f, s, 0.0f, 1.0f),
            vertex(0.0f, s, 0.0f, 1.0f),    vertex(s, 0.0f, 1.0f, 0.0f), vertex(s, s, 1.0f, 1.0f),
        };
    }
}

class LlglDualTextureTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    void DrawFullQuad(GraphicsDevice& device, DualTextureEffect& effect, const Color& vertexColor)
    {
        const std::vector<VertexPositionColorTexture> quad = MakeQuad(kSize, vertexColor);
        VertexBuffer buffer(device, VertexPositionColorTexture::getVertexDeclarationStatic(),
                            static_cast<int>(quad.size()), BufferUsage::None);
        buffer.SetData(quad.data(), static_cast<int>(quad.size()));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

public:
    LlglDualTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Llgl::LlglRenderer&>(
            device.GetRenderer());
        int logicalWidth = 0, logicalHeight = 0;
        renderer.GetViewportSize(logicalWidth, logicalHeight);

        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight), 0.0f, 0.0f, 1.0f);

        Texture2D baseTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{kHalfGray.getRProperty(), kHalfGray.getGProperty(),
                                                     kHalfGray.getBProperty(), kHalfGray.getAProperty()});
        Texture2D overlayTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{kWhite.getRProperty(), kWhite.getGProperty(),
                                                     kWhite.getBProperty(), kWhite.getAProperty()});

        DualTextureEffect effect(device);
        effect.setTextureProperty(&baseTexture);
        effect.setTexture2Property(&overlayTexture);
        effect.setDiffuseColorProperty(kDiffuse);
        effect.setAlphaProperty(kEffectAlpha);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(projection);

        // --- Check A: VertexColorEnabled=true --------------------------------------------------
        effect.setVertexColorEnabledProperty(true);
        device.Clear(Color(0, 0, 0, 255));
        DrawFullQuad(device, effect, kVertexColor);
        ExpectPixel("VertexColorEnabled=true: RGB == VertexColor*DiffuseColor*Alpha",
                    Rectangle(kSize / 2, kSize / 2, 1, 1), kExpectedEnabled, /*tolerance=*/8);

        // --- Check B: VertexColorEnabled=false --------------------------------------------------
        effect.setVertexColorEnabledProperty(false);
        device.Clear(Color(0, 0, 0, 255));
        DrawFullQuad(device, effect, kVertexColor);
        ExpectPixel("VertexColorEnabled=false: RGB == DiffuseColor*Alpha only",
                    Rectangle(kSize / 2, kSize / 2, 1, 1), kExpectedDisabled, /*tolerance=*/8);

        // --- Check C: the two textures are genuinely independent samples -----------------------
        {
            Texture2D whiteBase = Texture2D::CreateFromPixels(device, 1, 1,
                std::vector<std::uint8_t>{255, 255, 255, 255});
            Texture2D redOverlay = Texture2D::CreateFromPixels(device, 1, 1,
                std::vector<std::uint8_t>{255, 0, 0, 255});

            DualTextureEffect independenceEffect(device);
            independenceEffect.setTextureProperty(&whiteBase);
            independenceEffect.setTexture2Property(&redOverlay);
            independenceEffect.setVertexColorEnabledProperty(false);
            independenceEffect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            independenceEffect.setAlphaProperty(1.0f);
            independenceEffect.setWorldProperty(Matrix::getIdentityProperty());
            independenceEffect.setViewProperty(Matrix::getIdentityProperty());
            independenceEffect.setProjectionProperty(projection);

            device.Clear(Color(0, 0, 0, 255));
            DrawFullQuad(device, independenceEffect, Color::White);
            // white*2 clamps to white, so a bug that bound texture1 to BOTH slots would still read
            // white here -- only a genuinely independent second sampler produces red.
            ExpectPixel("Texture and Texture2 sample independently (white base + red overlay -> red)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), Color(255, 0, 0, 255), /*tolerance=*/10);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglDualTextureTest>();
}
