// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-025: GL sampler objects must keep each XNA texture slot independent.
// Native state snapshots cover every filter ordinal and property; rlgl's own batch is used only
// as a focused pixel oracle for filter/address behavior, never as CNA's production draw scheduler.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "RlglBridge.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;
    constexpr int kNearest = 0x2600;
    constexpr int kLinear = 0x2601;
    constexpr int kNearestMipmapNearest = 0x2700;
    constexpr int kLinearMipmapNearest = 0x2701;
    constexpr int kNearestMipmapLinear = 0x2702;
    constexpr int kLinearMipmapLinear = 0x2703;
    constexpr int kRepeat = 0x2901;
    constexpr int kClampToEdge = 0x812F;
    constexpr int kMirroredRepeat = 0x8370;

    [[nodiscard]] bool Near(const float left, const float right)
    {
        return std::fabs(left - right) < 0.001f;
    }
}

class RlglSamplerTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglSamplerTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;

        constexpr std::array<int, 9> expectedMin{
            kLinearMipmapLinear, kNearestMipmapNearest, kLinearMipmapLinear,
            kLinearMipmapNearest, kNearestMipmapLinear, kLinearMipmapLinear,
            kLinearMipmapNearest, kNearestMipmapLinear, kNearestMipmapNearest};
        constexpr std::array<int, 9> expectedMag{
            kLinear, kNearest, kLinear, kLinear, kNearest,
            kNearest, kNearest, kLinear, kLinear};

        bool filtersExact = true;
        for (int filter = 0; filter < static_cast<int>(expectedMin.size()); ++filter)
        {
            renderer.ApplySamplerState(0, filter, 0, 1, 16);
            const auto sampler = Bridge::GetBoundSamplerForTesting(0);
            const auto state = Bridge::GetSamplerSnapshotForTesting(sampler);
            filtersExact = filtersExact && state.minFilter == expectedMin[filter]
                && state.magFilter == expectedMag[filter]
                && state.wrapS == kRepeat && state.wrapT == kClampToEdge
                && state.wrapR == kRepeat && Near(state.minLod, 0.0f)
                && Near(state.maxLod, 1000.0f) && Near(state.lodBias, 0.0f)
                && state.compareMode == 0;
        }
        Check(filtersExact,
              "all nine TextureFilter ordinals map to exact GL min/mag/mip state");

        renderer.ApplySamplerState(0, 2, 0, 1, 999);
        renderer.ApplySamplerMipState(0, 3, 2.5f);
        renderer.ApplySamplerAddressW(0, 2);
        const auto customId = Bridge::GetBoundSamplerForTesting(0);
        const auto custom = Bridge::GetSamplerSnapshotForTesting(customId);
        const bool anisotropyClaim =
            device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering);
        const float anisotropyCeiling = Bridge::GetMaxSamplerAnisotropy();
        Check(custom.wrapS == kRepeat && custom.wrapT == kClampToEdge
                  && custom.wrapR == kMirroredRepeat && Near(custom.minLod, 3.0f)
                  && Near(custom.lodBias, 2.5f) && custom.compareMode == 0
                  && Near(custom.anisotropy, anisotropyClaim ? anisotropyCeiling : 1.0f),
              "mip, W-address, comparison, anisotropy, and capability state agree");

        renderer.ApplySamplerState(0, 1, 1, 1, 16);
        const auto reset = Bridge::GetSamplerSnapshotForTesting(
            Bridge::GetBoundSamplerForTesting(0));
        Check(reset.wrapR == kClampToEdge && Near(reset.minLod, 0.0f)
                  && Near(reset.lodBias, 0.0f) && Near(reset.anisotropy, 1.0f),
              "a core sampler application resets stale W, mip, bias, and anisotropy state");

        renderer.ApplySamplerState(1, 0, 2, 0, 1);
        renderer.ApplySamplerMipState(1, 2, -1.25f);
        const auto slotZeroId = Bridge::GetBoundSamplerForTesting(0);
        const auto slotOneId = Bridge::GetBoundSamplerForTesting(1);
        const auto slotZero = Bridge::GetSamplerSnapshotForTesting(slotZeroId);
        const auto slotOne = Bridge::GetSamplerSnapshotForTesting(slotOneId);
        Check(slotZeroId != 0 && slotOneId != 0 && slotZeroId != slotOneId
                  && slotZero.minFilter == kNearestMipmapNearest
                  && slotOne.minFilter == kLinearMipmapLinear
                  && slotOne.wrapS == kMirroredRepeat && slotOne.wrapT == kRepeat
                  && Near(slotOne.minLod, 2.0f) && Near(slotOne.lodBias, -1.25f),
              "sampler objects keep two texture slots independent");

        bool invalidSlotRejected = false;
        try
        {
            renderer.ApplySamplerState(16, 0, 0, 0, 1);
        }
        catch (const std::out_of_range&)
        {
            invalidSlotRejected = true;
        }
        Check(invalidSlotRejected, "sampler application rejects a slot outside XNA's range");

        Texture2D pattern(device, 2, 1);
        const std::array<Color, 2> colors{
            Color(255, 0, 0, 255), Color(0, 0, 255, 255)};
        pattern.SetData(colors.data(), static_cast<int>(colors.size()));
        pattern.GetRenderer().BindGL(0);
        pattern.GetRenderer().BindGL(1);
        Check(Bridge::GetBoundTexture2DForTesting(0) != 0
                  && Bridge::GetBoundTexture2DForTesting(0) ==
                      Bridge::GetBoundTexture2DForTesting(1)
                  && Bridge::GetBoundSamplerForTesting(0) !=
                      Bridge::GetBoundSamplerForTesting(1),
              "one texture can be bound with independent sampler objects on two slots");

        const auto sample = [&](const int filter, const int addressU,
                                const float u) -> Color {
            device.Clear(Color(0, 255, 0, 255));
            pattern.GetRenderer().BindGL(0);
            renderer.ApplySamplerState(0, filter, addressU, 1, 1);
            renderer.ApplySamplerMipState(0, 0, 0.0f);
            renderer.ApplySamplerAddressW(0, 1);
            Bridge::DrawBoundTextureSampleForTesting(u, 0.5f, kWidth, kHeight);
            Color pixel;
            const Rectangle rectangle(kWidth / 2, kHeight / 2, 1, 1);
            device.GetBackBufferData(&rectangle, &pixel, 0, 1);
            return pixel;
        };
        const auto isRed = [](const Color& color) {
            return color.getRProperty() >= 250 && color.getBProperty() <= 5;
        };
        const auto isBlue = [](const Color& color) {
            return color.getBProperty() >= 250 && color.getRProperty() <= 5;
        };

        Check(isRed(sample(1, 0, 1.25f)),
              "PointWrap samples the repeated left texel outside [0,1]");
        Check(isBlue(sample(1, 1, 1.25f)),
              "PointClamp samples the right edge outside [0,1]");
        Check(isBlue(sample(1, 2, 1.25f)),
              "PointMirror samples the reflected right texel outside [0,1]");
        Check(isRed(sample(1, 1, 0.49f)),
              "point magnification selects a stored texel without interpolation");
        const Color linear = sample(0, 1, 0.5f);
        Check(linear.getRProperty() >= 120 && linear.getRProperty() <= 135
                  && linear.getBProperty() >= 120 && linear.getBProperty() <= 135
                  && linear.getGProperty() <= 5,
              "linear magnification blends the two neighboring texels");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglSamplerTest game;
    game.Run();
    return game.getResultProperty();
}
