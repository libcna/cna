// SPDX-License-Identifier: MS-PL
// MTH-TEST-007: risk-based combinations, not a Cartesian-product coverage claim.
//
// S1: Opaque+Default -> Additive+None -> Opaque+Default, with both a rejected far
//     fragment and an accepted near fragment after restoration (Color, depth24, 0x).
// S2: scissor off+PointWrap -> scissor on+PointClamp -> off+PointWrap, constant UV
//     outside [0,1], so address mode and clipping have independent pixel witnesses.
// L1-L4: (Color,0,Immediate), (Color,4,Deferred), (HalfVector4,0,Deferred),
//     (HalfVector4,4,Immediate): upload and same-address texture recreation between
//     sprites, target A -> B -> A with PreserveContents, then native GetData.
//     Upload before End: Immediate keeps the first red sprite; Deferred sees green.
// E1/E2: stock -> portable custom -> compiled -> stock across a target switch and
//     readback. Unsupported custom/compiled legs are individually reported; remaining
//     legs still run. This adds target/pass interactions to the existing EasyGL cache
//     witnesses; it does not duplicate their counters, external mutation or VAO tests.
//
// Flat interior probes avoid raster edges and MSAA coverage ambiguity. All expected
// colors below are constants derived from source texels, additive blending or shader
// constants, never a renderer-generated golden or another leg's readback.

#include "common/PixelTestGame.hpp"
#include "common/PortableTintShaderPackage.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 48;
    const Color kRed(128, 0, 0, 255);
    const Color kBlue(0, 0, 128, 255);

    class StateTransitionCombinations final : public CNA::Examples::PixelTestGame
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;
        int skipped_ = 0;

        void Skip(const char* label, const char* reason)
        {
            ++skipped_;
            std::printf("[SKIP] %s: %s\n", label, reason);
        }

        void Pixel(RenderTarget2D& target, int x, int y, Color expected,
                   const std::string& label, int tolerance = 0)
        {
            const Rectangle region(x, y, 1, 1);
            if (target.getFormatProperty() == SurfaceFormat::HalfVector4)
            {
                PackedVector::HalfVector4 packed;
                target.GetData(0, &region, &packed, 0, 1);
                const Vector4 got = packed.ToVector4();
                const Vector4 want = expected.ToVector4();
                // Binary16 rounds normalized byte values by <= 0.000245. Allow 0.001
                // for storage plus interpolation; no edge or blending is sampled here.
                const bool ok = std::abs(got.X - want.X) <= 0.001f &&
                    std::abs(got.Y - want.Y) <= 0.001f &&
                    std::abs(got.Z - want.Z) <= 0.001f &&
                    std::abs(got.W - want.W) <= 0.001f;
                std::printf("[PIXEL] %s: (%g,%g,%g,%g), expected (%g,%g,%g,%g)\n",
                    label.c_str(), got.X, got.Y, got.Z, got.W, want.X, want.Y, want.Z, want.W);
                ExpectTrue(label.c_str(), ok);
                return;
            }
            if (!ExpectTrue("readback format is Color or HalfVector4",
                            target.getFormatProperty() == SurfaceFormat::Color)) return;
            Color got(17, 19, 23, 29);
            target.GetData(0, &region, &got, 0, 1);
            std::printf("[PIXEL] %s: (%d,%d,%d,%d), expected (%d,%d,%d,%d), tolerance %d\n",
                label.c_str(), got.getRProperty(), got.getGProperty(), got.getBProperty(),
                got.getAProperty(), expected.getRProperty(), expected.getGProperty(),
                expected.getBProperty(), expected.getAProperty(), tolerance);
            ExpectTrue(label.c_str(),
                std::abs(got.getRProperty() - expected.getRProperty()) <= tolerance &&
                std::abs(got.getGProperty() - expected.getGProperty()) <= tolerance &&
                std::abs(got.getBProperty() - expected.getBProperty()) <= tolerance &&
                std::abs(got.getAProperty() - expected.getAProperty()) <= tolerance);
        }

        void Sprite(SpriteBatch& batch, Texture2D& texture, Rectangle rect, Color tint,
                    SpriteSortMode mode = SpriteSortMode::Immediate,
                    const BlendState& blend = BlendState::Opaque,
                    const DepthStencilState& depth = DepthStencilState::None,
                    float z = 0.0f, ShaderEffect* effect = nullptr)
        {
            SamplerState point = SamplerState::PointClamp;
            RasterizerState raster = RasterizerState::CullNone;
            batch.Begin(mode, blend, &point, &depth, &raster, effect);
            batch.Draw(texture, rect, Rectangle(0, 0, 1, 1), tint, 0.0f,
                       Vector2(0, 0), SpriteEffects::None, z);
            batch.End();
        }

        void BlendDepth()
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
            Texture2D white(dev, 1, 1);
            const Color texel = Color::White;
            white.SetData(&texel, 1);
            SpriteBatch batch(dev);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            Sprite(batch, white, Rectangle(0, 0, kSize, kSize), kBlue,
                   SpriteSortMode::Immediate, BlendState::Opaque, DepthStencilState::Default, 0.5f);
            Sprite(batch, white, Rectangle(0, 0, 16, kSize), kRed,
                   SpriteSortMode::Immediate, BlendState::Opaque, DepthStencilState::Default, 0.25f);
            // Additive uses SourceAlpha; alpha=255 gives exactly src + dst, clamped.
            Sprite(batch, white, Rectangle(16, 0, 16, kSize), Color(0, 64, 0, 255),
                   SpriteSortMode::Immediate, BlendState::Additive, DepthStencilState::None, 0.75f);
            Sprite(batch, white, Rectangle(32, 0, 16, kSize), kRed,
                   SpriteSortMode::Immediate, BlendState::Opaque, DepthStencilState::Default, 0.25f);
            Sprite(batch, white, Rectangle(32, 0, 16, kSize), Color::Lime,
                   SpriteSortMode::Immediate, BlendState::Opaque, DepthStencilState::Default, 0.75f);
            Sprite(batch, white, Rectangle(32, 24, 16, 24), kBlue,
                   SpriteSortMode::Immediate, BlendState::Opaque, DepthStencilState::Default, 0.125f);
            dev.SetRenderTarget(nullptr);
            // Integer texels, unity factors and fully covered interiors require exact RGBA8.
            Pixel(target, 8, 12, kRed, "S1 A: nearer opaque fragment");
            Pixel(target, 24, 12, Color(0, 64, 128, 255), "S1 B: additive far fragment with depth off");
            Pixel(target, 40, 12, kRed, "S1 A restored: far fragment rejected");
            Pixel(target, 40, 36, kBlue, "S1 A restored: near fragment replaces rather than adds");
        }

        void ScissorSampler()
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, kSize, kSize);
            Texture2D texture(dev, 2, 1);
            const std::array<Color, 2> texels{kRed, kBlue};
            texture.SetData(texels.data(), 2);
            BasicEffect effect(dev);
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(&texture);
            dev.SetRenderTarget(&target);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.setScissorRectangleProperty(Rectangle(0, 0, 24, kSize));
            RasterizerState clipped = RasterizerState::CullNone;
            clipped.setScissorTestEnableProperty(true);
            for (int row = 0; row < 3; ++row)
            {
                const bool middle = row == 1;
                dev.setRasterizerStateProperty(middle ? clipped : RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = middle ? SamplerState::PointClamp : SamplerState::PointWrap;
                const float top = 1.0f - row * (2.0f / 3.0f);
                const float bottom = top - 2.0f / 3.0f;
                // Constant u=1.25 wraps to the center of texel 0; clamp selects texel 1.
                const Vector2 uv(1.25f, 0.5f);
                const VertexPositionTexture vertices[] = {
                    {Vector3(-1, top, 0.5f), uv}, {Vector3(1, top, 0.5f), uv},
                    {Vector3(-1, bottom, 0.5f), uv}, {Vector3(-1, bottom, 0.5f), uv},
                    {Vector3(1, top, 0.5f), uv}, {Vector3(1, bottom, 0.5f), uv}};
                effect.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
            }
            dev.SetRenderTarget(nullptr);
            for (int row = 0; row < 3; ++row)
            {
                Pixel(target, 12, row * 16 + 8, row == 1 ? kBlue : kRed,
                      "S2 sampler row " + std::to_string(row));
                Pixel(target, 36, row * 16 + 8, row == 1 ? Color::Black : kRed,
                      "S2 scissor row " + std::to_string(row));
            }
            dev.getTexturesProperty()(0, nullptr);
        }

        void Lifetime(SurfaceFormat format, int samples, SpriteSortMode mode, const char* label)
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, kSize, kSize, false, format, DepthFormat::None,
                                  samples, RenderTargetUsage::PreserveContents);
            RenderTarget2D other(dev, kSize, kSize, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            std::printf("[CASE] %s: requested format=%d samples=%d, applied format=%d samples=%d\n",
                label, static_cast<int>(format), samples, static_cast<int>(target.getFormatProperty()),
                target.getMultiSampleCountProperty());
            if (target.getFormatProperty() != format)
                Skip(label, "requested format substituted; following pixels cover applied format only");
            if (samples > 1 && target.getMultiSampleCountProperty() <= 1)
                Skip(label, "4x request clamped to single sample; multisample interaction not covered");
            SpriteBatch batch(dev);
            std::optional<Texture2D> texture;
            texture.emplace(dev, 1, 1);
            texture->SetData(&kRed, 1);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState depth = DepthStencilState::None;
            RasterizerState raster = RasterizerState::CullNone;
            batch.Begin(mode, BlendState::Opaque, &point, &depth, &raster);
            batch.Draw(*texture, Rectangle(0, 0, 16, 24), Color::White);
            dev.getTexturesProperty()(0, nullptr);
            const Color green = Color::Lime;
            texture->SetData(&green, 1);
            batch.Draw(*texture, Rectangle(16, 0, 16, 24), Color::White);
            batch.End();
            dev.getTexturesProperty()(0, nullptr);
            // optional::emplace destroys and recreates at the same C++ address: cached identity
            // must follow resource lifetime, even when no native handle reuse happens this run.
            texture.emplace(dev, 1, 1);
            texture->SetData(&kBlue, 1);
            Sprite(batch, *texture, Rectangle(32, 0, 16, 24), Color::White, mode);
            dev.SetRenderTarget(&other);
            dev.Clear(Color::Yellow);
            Sprite(batch, *texture, Rectangle(0, 0, 16, 24), Color::White, mode);
            dev.SetRenderTarget(&target);
            Sprite(batch, *texture, Rectangle(0, 24, kSize, 24), Color::White, mode);
            dev.SetRenderTarget(nullptr);
            // Immediate submits Draw now; Deferred first submits at End, after the upload.
            Pixel(target, 8, 12, mode == SpriteSortMode::Immediate ? kRed : Color::Lime,
                  std::string(label) + " queued before upload follows submission mode");
            Pixel(target, 24, 12, Color::Lime, std::string(label) + " submitted after upload");
            Pixel(target, 40, 12, kBlue, std::string(label) + " recreated texture");
            Pixel(target, 24, 36, kBlue, std::string(label) + " returned target receives new draw");
            Pixel(other, 8, 12, kBlue, std::string(label) + " B draw survives return to A");
            Pixel(other, 32, 36, Color::Yellow, std::string(label) + " B untouched region");
            dev.getTexturesProperty()(0, nullptr);
        }

        void Effects()
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            RenderTarget2D other(dev, kSize, kSize);
            Texture2D white(dev, 1, 1);
            const Color texel = Color::White;
            white.SetData(&texel, 1);
            SpriteBatch batch(dev);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            Sprite(batch, white, Rectangle(0, 0, 24, 24), kRed);
            bool customRan = false;
            std::unique_ptr<ShaderEffect> custom;
#ifdef CNA_CNAEXT
            if (!dev.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
                Skip("E1 custom", "GraphicsCapability::CustomEffects=false");
            else if (!dev.ExecutesShaderEffectSourceEXT())
                Skip("E1 custom", "renderer does not execute ShaderEffect source");
            else
            {
                const auto package = CNA::Examples::PortableTint::CreatePackage();
                // An executing renderer failing to select/compile the existing portable fixture
                // is a failure, not an unrecognized capability refusal.
                if (ExpectTrue("E1 portable package usable", package.selectFor(dev).isUsable()))
                {
                    custom = std::make_unique<ShaderEffect>(dev, package);
                    if (ExpectTrue("E1 portable package valid", custom->IsEffectValid()))
                    {
                        custom->SetUniformVec4("uColor", 0.25f, 0.5f, 0.75f, 1.0f);
                        custom->Apply();
                        Sprite(batch, white, Rectangle(24, 0, 24, 24), Color::White,
                               SpriteSortMode::Deferred, BlendState::Opaque,
                               DepthStencilState::None, 0.0f, custom.get());
                        customRan = true;
                    }
                }
            }
#else
            Skip("E1 custom", "CNA_CNAEXT is disabled");
#endif
            dev.SetRenderTarget(&other);
            dev.Clear(Color::Yellow);
            dev.SetRenderTarget(&target);
            bool compiledRan = false;
            std::unique_ptr<Effect> compiled;
            if (!dev.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
                Skip("E2 compiled", "GraphicsCapability::CompiledEffects=false (including build-time opt-out)");
            else
            {
                compiled = std::make_unique<Effect>(dev, CNA::TestSupport::BuildSyntheticDrawableEffect());
                compiled->getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
                compiled->getParametersProperty()["Tint"]->SetValue(Vector4(0.75f, 0.25f, 0.5f, 1.0f));
                compiled->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setDepthStencilStateProperty(DepthStencilState::None);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                const VertexPositionColor vertices[] = {
                    {Vector3(-1, 0, 0), Color::White}, {Vector3(0, 0, 0), Color::White},
                    {Vector3(-1, -1, 0), Color::White}, {Vector3(-1, -1, 0), Color::White},
                    {Vector3(0, 0, 0), Color::White}, {Vector3(0, -1, 0), Color::White}};
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
                compiledRan = true;
            }
            Sprite(batch, white, Rectangle(24, 24, 24, 24), kBlue);
            dev.SetRenderTarget(nullptr);
            Pixel(target, 12, 12, kRed, "E stock before alternation");
            // 0.25/0.5/0.75 * 255 are quarter/half integers. One UNORM8 LSB allows
            // round-to-nearest versus truncation; it cannot hide a stale tint or stock shader.
            if (customRan) Pixel(target, 36, 12, Color(64, 128, 191, 255), "E1 custom before target switch", 1);
            if (compiledRan) Pixel(target, 12, 36, Color(191, 64, 128, 255), "E2 compiled after target switch", 1);
            Pixel(target, 36, 36, kBlue, "E stock restored after effects");
            dev.getTexturesProperty()(0, nullptr);
        }

    public:
        StateTransitionCombinations()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            manager_->setPreferredBackBufferWidthProperty(kSize);
            manager_->setPreferredBackBufferHeightProperty(kSize);
        }

        void RunTest() override
        {
            std::printf("[RENDERER] %s\n", std::string(getGraphicsDeviceProperty().GetGraphicsRendererName()).c_str());
            BlendDepth();
            ScissorSampler();
            Lifetime(SurfaceFormat::Color, 0, SpriteSortMode::Immediate, "L1 Color/0/Immediate");
            Lifetime(SurfaceFormat::Color, 4, SpriteSortMode::Deferred, "L2 Color/4/Deferred");
            Lifetime(SurfaceFormat::HalfVector4, 0, SpriteSortMode::Deferred, "L3 Half/0/Deferred");
            Lifetime(SurfaceFormat::HalfVector4, 4, SpriteSortMode::Immediate, "L4 Half/4/Immediate");
            Effects();
            std::printf("[SUMMARY] capability sublegs skipped=%d; result=%s\n", skipped_,
                        getResultProperty() == 0 ? "PASS" : "FAIL");
        }
    };
}

int main()
{
    // Use runtime selection: the shared multi-renderer executable also runs diagnostic legs.
    const auto selected = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(selected) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] state_transition_combinations: selected %s makes no pixel claim\n",
                    std::string(CNA::getGraphicsRendererName(selected)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    return CNA::Examples::RunPixelTest<StateTransitionCombinations>();
}
