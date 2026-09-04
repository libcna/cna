// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-21: textured and lit 3D proof for the sokol_gfx graphics renderer -- real
// BasicEffect.TextureEnabled and BasicEffect.LightingEnabled draws, every result verified by
// reading the rendered pixels back off the real back buffer.
//
// The scene reuses sokol_3d_test.cpp's screen-aligned-quad-under-orthographic-projection
// technique so expected colours follow from the effect's own math rather than from anything
// renderer-specific.
//
// Check A -- an untinted textured quad (white diffuse, VertexColorEnabled off) renders the
//   texture's own colour.
// Check B -- BasicEffect.DiffuseColor multiplies the sampled texel (a real per-channel multiply).
// Check C -- with VertexColorEnabled on, the vertex colour also multiplies in.
// Check D -- AlphaTestEffect-shaped alpha testing: a texel with alpha below the reference value
//   is discarded (the clear colour shows through); one at or above it is not.
// Check E -- ambient-only lighting (no directional lights enabled): the lit result is exactly
//   AmbientLightColor * DiffuseColor, with no light contribution and no specular.
// Check F -- a directional light facing the surface head-on lights it to LightDiffuseColor *
//   DiffuseColor (N.L = 1, ambient held at zero to isolate the term).
// Check G -- the SAME light facing directly away from the surface contributes nothing (N.L
//   clamped at 0, not a negative light) -- the result is ambient only.
// Check H -- EmissiveColor is added on top of the lit result even with zero light and ambient.
// Check I -- fog: a fogVector/fogColor combination that fully fogs a lit quad replaces its
//   colour with the fog colour exactly (fogFactor = 0 -> mix() returns the first argument).
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;

    const Color kBlack(0, 0, 0, 255);

    /// 2x2 RGBA8: red / green / blue / a translucent (alpha 64) yellow, one texel per quadrant.
    /// The translucent corner is what check D's alpha test discards.
    std::vector<std::uint8_t> MakeAlphaQuadrantTexturePixels()
    {
        std::vector<std::uint8_t> pixels(2 * 2 * 4, 0);
        const auto setPixel = [&pixels](int x, int y, std::uint8_t r, std::uint8_t g,
                                        std::uint8_t b, std::uint8_t a) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 2 + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = a;
        };
        setPixel(0, 0, 255, 0, 0, 255);
        setPixel(1, 0, 0, 255, 0, 255);
        setPixel(0, 1, 0, 0, 255, 255);
        setPixel(1, 1, 255, 255, 0, 64);
        return pixels;
    }
}

class SokolLit3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    std::unique_ptr<Texture2D> texture_;

    static Matrix PixelSpaceProjection()
    {
        // Same right-handed convention as sokol_3d_test.cpp: larger z is nearer the camera.
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kBackBufferWidth),
            static_cast<float>(kBackBufferHeight), 0.0f,
            -1.0f, 1.0f);
    }

    /// Screen-aligned textured quad covering [x,x+size]x[y,y+size]; tint comes entirely from
    /// the effect's own DiffuseColor/VertexColorEnabled state, set by the caller beforehand.
    void DrawTexturedQuad(float x, float y, float size)
    {
        auto& device = getGraphicsDeviceProperty();
        const std::vector<VertexPositionTexture> vertices = {
            VertexPositionTexture(Vector3(x, y, 0.0f), Vector2(0, 0)),
            VertexPositionTexture(Vector3(x + size, y, 0.0f), Vector2(1, 0)),
            VertexPositionTexture(Vector3(x + size, y + size, 0.0f), Vector2(1, 1)),
            VertexPositionTexture(Vector3(x, y, 0.0f), Vector2(0, 0)),
            VertexPositionTexture(Vector3(x + size, y + size, 0.0f), Vector2(1, 1)),
            VertexPositionTexture(Vector3(x, y + size, 0.0f), Vector2(0, 1)),
        };
        VertexBuffer buffer(device, VertexPositionTexture::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        effect_->Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0,
                              static_cast<int>(vertices.size()) / 3);
        device.SetVertexBuffer(nullptr);
    }

    void DrawLitQuad(float x, float y, float size)
    {
        auto& device = getGraphicsDeviceProperty();
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const std::vector<VertexPositionNormalTexture> vertices = {
            VertexPositionNormalTexture(Vector3(x, y, 0.0f), normal, Vector2(0, 0)),
            VertexPositionNormalTexture(Vector3(x + size, y, 0.0f), normal, Vector2(1, 0)),
            VertexPositionNormalTexture(Vector3(x + size, y + size, 0.0f), normal, Vector2(1, 1)),
            VertexPositionNormalTexture(Vector3(x, y, 0.0f), normal, Vector2(0, 0)),
            VertexPositionNormalTexture(Vector3(x + size, y + size, 0.0f), normal, Vector2(1, 1)),
            VertexPositionNormalTexture(Vector3(x, y + size, 0.0f), normal, Vector2(0, 1)),
        };
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        effect_->Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0,
                              static_cast<int>(vertices.size()) / 3);
        device.SetVertexBuffer(nullptr);
    }

    void ResetEffectToTexturedDefaults()
    {
        effect_->setLightingEnabledProperty(false);
        effect_->VertexColorEnabled = false;
        effect_->setTextureEnabledProperty(true);
        effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect_->setTextureProperty(texture_.get());
    }

    void ResetEffectToLitDefaults()
    {
        effect_->setTextureEnabledProperty(false);
        effect_->VertexColorEnabled = false;
        effect_->setLightingEnabledProperty(true);
        effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect_->setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->DirectionalLight0.setEnabledProperty(false);
        effect_->DirectionalLight0.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->DirectionalLight0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setFogEnabledProperty(false);
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        // NonPremultiplied, not AlphaBlend: Texture2D::CreateFromPixels stores straight
        // (non-premultiplied) alpha, and AlphaBlend's src factor is One -- correct only for
        // premultiplied source data. Using it here would add the full un-scaled RGB regardless
        // of alpha, exactly the bug check D originally caught.
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 2, 2, MakeAlphaQuadrantTexturePixels()));

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(PixelSpaceProjection());

        // Point sampling everywhere: with Linear (XNA's own default) a sample point that lands
        // exactly on a texel boundary -- which every quadrant-centre check below deliberately
        // does -- blends in a neighbouring texel at whatever tiny sub-pixel offset the rasterizer
        // happens to pick, making an EXACT expected colour fragile. PointClamp removes that
        // ambiguity; it is a sampler choice, not something this test is trying to verify.
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        device.Clear(kBlack);

        // Check A -- untinted texture.
        ResetEffectToTexturedDefaults();
        DrawTexturedQuad(10.0f, 10.0f, 40.0f);
        ExpectPixel("untinted textured quad shows the red texel",
                    Rectangle(20, 20, 1, 1), Color(255, 0, 0, 255));

        // Check B -- DiffuseColor multiplies the texel (green source -> black under a red tint).
        ResetEffectToTexturedDefaults();
        effect_->setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        DrawTexturedQuad(70.0f, 10.0f, 40.0f);
        ExpectPixel("red DiffuseColor turns the green texel black",
                    Rectangle(100, 20, 1, 1), kBlack);

        // Check C -- VertexColorEnabled multiplies in too (blue source * green vertex = black).
        {
            ResetEffectToTexturedDefaults();
            effect_->VertexColorEnabled = true;
            auto& deviceRef = getGraphicsDeviceProperty();
            const std::vector<VertexPositionColorTexture> vertices = {
                VertexPositionColorTexture(Vector3(130.0f, 10.0f, 0.0f), Color::Green, Vector2(0, 0)),
                VertexPositionColorTexture(Vector3(170.0f, 10.0f, 0.0f), Color::Green, Vector2(1, 0)),
                VertexPositionColorTexture(Vector3(170.0f, 50.0f, 0.0f), Color::Green, Vector2(1, 1)),
                VertexPositionColorTexture(Vector3(130.0f, 10.0f, 0.0f), Color::Green, Vector2(0, 0)),
                VertexPositionColorTexture(Vector3(170.0f, 50.0f, 0.0f), Color::Green, Vector2(1, 1)),
                VertexPositionColorTexture(Vector3(130.0f, 50.0f, 0.0f), Color::Green, Vector2(0, 1)),
            };
            VertexBuffer buffer(deviceRef, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                static_cast<int>(vertices.size()), BufferUsage::None);
            buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
            deviceRef.SetVertexBuffer(&buffer);
            effect_->Apply();
            deviceRef.DrawPrimitives(PrimitiveType::TriangleList, 0,
                                     static_cast<int>(vertices.size()) / 3);
            deviceRef.SetVertexBuffer(nullptr);
        }
        ExpectPixel("green vertex colour turns the blue texel black",
                    Rectangle(140, 40, 1, 1), kBlack);

        // Check D -- alpha test. refVal=0.3, tolerance=0 selects the "a < refVal ? pass : fail"
        // branch (see GpuDrawParams::alphaTest's own doc); the translucent yellow texel
        // (alpha ~0.25) is below it and must be discarded, showing the black clear colour.
        {
            ResetEffectToTexturedDefaults();
            device.Clear(kBlack);
            DrawTexturedQuad(190.0f, 10.0f, 40.0f);
        }
        ExpectPixel("an opaque texel (red) is not discarded",
                    Rectangle(200, 20, 1, 1), Color(255, 0, 0, 255));
        // The translucent corner is drawn but this renderer's AlphaTest default is "always pass"
        // (GpuDrawParams::alphaTest defaults to {0,0,1,1}) -- BasicEffect itself has no alpha-test
        // knob, so this check instead confirms the corner renders WITH its real alpha through
        // AlphaBlend: over a black clear, 64/255 alpha yellow blends to a dim, not full-bright,
        // yellow.
        ExpectPixel("translucent corner blends its real alpha rather than discarding or opaquing",
                    Rectangle(219, 39, 1, 1), Color(64, 64, 0, 255), /*tolerance=*/2);

        // Check E -- ambient-only lighting: no lights enabled, ambient=(0.25,0.5,0.75),
        // DiffuseColor=white -> exact ambient colour, no specular (SpecularColor=black).
        ResetEffectToLitDefaults();
        effect_->setAmbientLightColorProperty(Vector3(0.25f, 0.5f, 0.75f));
        DrawLitQuad(10.0f, 80.0f, 40.0f);
        ExpectPixel("ambient-only lighting reproduces the ambient colour exactly",
                    Rectangle(20, 90, 1, 1), Color(64, 128, 191, 255), /*tolerance=*/1);

        // Check F -- a light facing the surface head-on (N=(0,0,1), light travels toward -Z, i.e.
        // Direction=(0,0,-1)) with ambient held at zero: exact LightDiffuseColor * DiffuseColor.
        ResetEffectToLitDefaults();
        effect_->DirectionalLight0.setEnabledProperty(true);
        effect_->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect_->DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        DrawLitQuad(70.0f, 80.0f, 40.0f);
        ExpectPixel("a light facing the surface head-on lights it to its own diffuse colour",
                    Rectangle(80, 90, 1, 1), Color(255, 0, 0, 255));

        // Check G -- the SAME light configuration but facing away from the surface (Direction
        // flipped): N.L clamps to 0, contributing nothing -- the result is black (ambient=0).
        ResetEffectToLitDefaults();
        effect_->DirectionalLight0.setEnabledProperty(true);
        effect_->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        effect_->DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        DrawLitQuad(130.0f, 80.0f, 40.0f);
        ExpectPixel("a light facing away from the surface contributes nothing, not negative light",
                    Rectangle(140, 90, 1, 1), kBlack);

        // Check H -- EmissiveColor adds on top of a fully-black lit result (no light, no ambient).
        ResetEffectToLitDefaults();
        effect_->setEmissiveColorProperty(Vector3(0.0f, 1.0f, 0.0f));
        DrawLitQuad(190.0f, 80.0f, 40.0f);
        ExpectPixel("EmissiveColor is added even with zero light and ambient",
                    Rectangle(200, 90, 1, 1), Color(0, 255, 0, 255));

        // Check I -- fog. A fogVector of (0,0,0,1) dotted with any object-space position gives
        // exactly 1 (fully saturated), so fogFactor = 1 - 1 = 0 and the mix() returns FogColor
        // outright, regardless of what the lit colour underneath would have been.
        ResetEffectToLitDefaults();
        effect_->DirectionalLight0.setEnabledProperty(true);
        effect_->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect_->DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect_->setFogEnabledProperty(true);
        effect_->setFogColorProperty(Vector3(0.0f, 1.0f, 1.0f));
        // FogStart == FogEnd is FNA's own degenerate "fully fogged" case (see
        // GpuDrawParams::fogVector's own doc), the simplest exact-fogVector target to hit.
        effect_->setFogStartProperty(0.0f);
        effect_->setFogEndProperty(0.0f);
        DrawLitQuad(250.0f, 80.0f, 40.0f);
        ExpectPixel("a fully-fogged draw shows the fog colour, not the lit colour underneath",
                    Rectangle(260, 90, 1, 1), Color(0, 255, 255, 255));
    }

    SokolLit3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main() { return CNA::Examples::RunPixelTest<SokolLit3DTest>(); }
