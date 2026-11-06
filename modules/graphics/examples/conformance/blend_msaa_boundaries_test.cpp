// SPDX-License-Identifier: MS-PL
// MTH-CONF-005: independent blend equations and sample-mask population, including alpha.
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    const Color kDestination(40, 80, 120, 160);
    const Color kSource(200, 100, 60, 240);

    class BlendBoundaries final : public CNA::Examples::PixelTestGame
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;
        int skipped_ = 0;

        void Skip(const char* reason)
        {
            ++skipped_;
            std::printf("[SKIP] blend_msaa_boundaries/4x: %s\n", reason);
        }

        bool MaskCaseSupported(int mask, const std::string& label)
        {
            const auto& profile = getGraphicsDeviceProperty().GetRendererCapabilityProfileEXT();
            const auto& feature = profile.GetFeature(CNA::RendererFeature::MultiSampleMask);
            using Support = CNA::RendererFeatureSupport;
            if (feature.support == Support::Supported) return true;
            if (feature.support == Support::Restricted && (mask == 0 || (mask & 15) == 15))
                return true; // Restricted explicitly promises all/none for this four-sample target.
            if (!ExpectTrue("MultiSampleMask capability is classified", feature.support != Support::Unknown))
                return false;
            ++skipped_;
            std::printf("[SKIP] %s: RendererFeature::MultiSampleMask=%s; %s\n", label.c_str(),
                feature.support == Support::Restricted ? "Restricted" : "Unsupported",
                feature.support == Support::Restricted ?
                    "only all/none masks are applied; partial masks are refused" :
                    "mask is accepted with no effect");
            if (profile.GetRendererName() == "FNA3D" && feature.support == Support::Unsupported)
            {
                // Pinned FNA3D 5c0b9b8c13f5: FNA3D_Driver_SDL.c:2266 forwards the mask,
                // :1644-1645 writes SDL's reserved multisample enable_mask/sample_mask fields.
                // The 2026-11-06 fork repairs leave this reserved-field limitation open (FNA3D-3).
                std::printf("[DIAGNOSIS] FNA3D SDL GPU driver writes reserved multisample fields "
                            "(FNA3D 5c0b9b8c13f5 src/FNA3D_Driver_SDL.c:1644-1645); "
                            "the OpenGL driver has a separate Supported capability\n");
            }
            return false;
        }

        void Pixel(RenderTarget2D& target, Color expected, const std::string& label, int tolerance)
        {
            const Rectangle rect(8, 8, 1, 1);
            Color got;
            target.GetData(0, &rect, &got, 0, 1);
            std::printf("[PIXEL] %s got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) tolerance=%d\n", label.c_str(),
                got.getRProperty(), got.getGProperty(), got.getBProperty(), got.getAProperty(),
                expected.getRProperty(), expected.getGProperty(), expected.getBProperty(), expected.getAProperty(), tolerance);
            ExpectTrue(label.c_str(), std::abs(got.getRProperty()-expected.getRProperty()) <= tolerance &&
                std::abs(got.getGProperty()-expected.getGProperty()) <= tolerance &&
                std::abs(got.getBProperty()-expected.getBProperty()) <= tolerance &&
                std::abs(got.getAProperty()-expected.getAProperty()) <= tolerance);
        }

        void Draw(SpriteBatch& batch, Texture2D& white, const BlendState& blend, Color color)
        {
            batch.Begin(SpriteSortMode::Immediate, blend, &SamplerState::PointClamp,
                        &DepthStencilState::None, &RasterizerState::CullNone);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), color);
            batch.End();
        }

        void Leg(int samples)
        {
            auto& dev = getGraphicsDeviceProperty();
            if (samples && !dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing))
            { Skip("GraphicsCapability::MultiSampleAntiAliasing=false"); return; }
            RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                                  samples, RenderTargetUsage::PreserveContents);
            std::printf("[CASE] blend requested samples=%d applied=%d\n", samples, target.getMultiSampleCountProperty());
            if (samples && target.getMultiSampleCountProperty() != 4)
            { Skip("Color target did not apply 4 samples (applied count printed above)"); return; }
            Texture2D white(dev, 1, 1);
            const Color texel = Color::White;
            white.SetData(&texel, 1);
            SpriteBatch batch(dev);
            const std::string prefix = std::to_string(samples) + "x ";
            const auto begin = [&] { dev.SetRenderTarget(&target); dev.Clear(kDestination); };
            const auto end = [&](Color expected, const char* name, int tolerance = 0) {
                dev.SetRenderTarget(nullptr); Pixel(target, expected, prefix+name, tolerance);
            };

            BlendState factor = BlendState::Opaque;
            factor.setColorSourceBlendProperty(Blend::BlendFactor);
            factor.setColorDestinationBlendProperty(Blend::InverseBlendFactor);
            factor.setAlphaSourceBlendProperty(Blend::BlendFactor);
            factor.setAlphaDestinationBlendProperty(Blend::InverseBlendFactor);
            factor.setBlendFactorProperty(Color(64, 128, 192, 128));
            factor.setColorWriteChannelsProperty(ColorWriteChannels::Red | ColorWriteChannels::Alpha);
            begin();
            Draw(batch, white, factor, kSource);
            // R=(200*64+40*191)/255=80.1569; A=(240*128+160*127)/255=200.1569.
            // Disabled G/B retain the destination. One UNORM8 LSB allows blend rounding.
            end(Color(80, 80, 120, 200), "BlendState factor with R/A write mask", 1);

            begin();
            batch.Begin(SpriteSortMode::Immediate, factor, &SamplerState::PointClamp,
                        &DepthStencilState::None, &RasterizerState::CullNone);
            dev.setBlendFactorProperty(Color(192, 64, 128, 64));
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), kSource);
            batch.End();
            // Device override: R=160.4706, A=180.0784; G/B are still masked.
            end(Color(160, 80, 120, 180), "GraphicsDevice factor overrides state factor", 1);

            begin();
            Draw(batch, white, BlendState::Opaque, kSource);
            end(kSource, "opaque restores all channels and unity factors");

            for (const auto* blend : {&BlendState::AlphaBlend, &BlendState::NonPremultiplied})
                for (const int alpha : {0, 255})
                {
                    // Premultiplied transparent black is the valid zero-alpha endpoint;
                    // straight alpha uses nonzero RGB so an ignored alpha factor is visible.
                    const Color source = alpha == 0 && blend == &BlendState::AlphaBlend ?
                        Color::Transparent : Color(200, 100, 60, alpha);
                    begin();
                    Draw(batch, white, *blend, source);
                    end(alpha == 0 ? kDestination : Color(200, 100, 60, 255),
                        (std::string(blend == &BlendState::AlphaBlend ? "premultiplied" : "straight") +
                         " alpha=" + std::to_string(alpha)).c_str());
                }

            if (samples)
            {
                // Every sample is covered in the interior; resolving n of four white samples
                // over transparent black yields n*255/4 on ALL channels, independent of sample
                // positions. One LSB admits resolve rounding at .25/.5/.75, not a missing sample.
                const std::array<int, 7> masks{0, 1, 3, 7, 15, 0, 15};
                const std::array<int, 7> expected{0, 64, 128, 191, 255, 0, 255};
                for (std::size_t i = 0; i < masks.size(); ++i)
                {
                    const std::string label = "BlendState sample mask=" + std::to_string(masks[i]);
                    if (!MaskCaseSupported(masks[i], label)) continue;
                    dev.SetRenderTarget(&target);
                    dev.Clear(Color::Transparent);
                    BlendState masked = BlendState::Opaque;
                    masked.setMultiSampleMaskProperty(masks[i]);
                    Draw(batch, white, masked, Color::White);
                    end(Color(expected[i], expected[i], expected[i], expected[i]),
                        label.c_str(), 1);
                }
                if (MaskCaseSupported(3, "GraphicsDevice sample mask override"))
                {
                    dev.SetRenderTarget(&target);
                    dev.Clear(Color::Transparent);
                    batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &SamplerState::PointClamp,
                                &DepthStencilState::None, &RasterizerState::CullNone);
                    dev.setMultiSampleMaskProperty(3);
                    batch.Draw(white, Rectangle(0, 0, kSize, kSize), Color::White);
                    batch.End();
                    end(Color(128, 128, 128, 128), "GraphicsDevice sample mask override", 1);
                    dev.setMultiSampleMaskProperty(-1);
                }
            }
            dev.getTexturesProperty()(0, nullptr);
        }
    public:
        BlendBoundaries()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            manager_->setPreferredBackBufferWidthProperty(kSize);
            manager_->setPreferredBackBufferHeightProperty(kSize);
        }
        void RunTest() override
        {
            std::printf("[RENDERER] %s\n", std::string(getGraphicsDeviceProperty().GetGraphicsRendererName()).c_str());
            Leg(0);
            Leg(4);
            std::printf("[SUMMARY] capability legs skipped=%d result=%s\n", skipped_, getResultProperty() ? "FAIL" : "PASS");
        }
    };
}
int main()
{
    const auto renderer = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(renderer) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] blend_msaa_boundaries: %s makes no pixel claim\n", std::string(CNA::getGraphicsRendererName(renderer)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    return CNA::Examples::RunPixelTest<BlendBoundaries>();
}
