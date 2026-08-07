// SPDX-License-Identifier: MS-PL
// SKIA-129: public SpriteBatch mip LOD, interpolation, min/mag, addressing and transform policy.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);

    struct FilterCase final
    {
        TextureFilter filter;
        const char* name;
    };

    constexpr std::array<FilterCase, 6> kNamedMipFilters {{
        {TextureFilter::LinearMipPoint, "LinearMipPoint"},
        {TextureFilter::PointMipLinear, "PointMipLinear"},
        {TextureFilter::MinLinearMagPointMipLinear, "MinLinearMagPointMipLinear"},
        {TextureFilter::MinLinearMagPointMipPoint, "MinLinearMagPointMipPoint"},
        {TextureFilter::MinPointMagLinearMipLinear, "MinPointMagLinearMipLinear"},
        {TextureFilter::MinPointMagLinearMipPoint, "MinPointMagLinearMipPoint"},
    }};

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    [[nodiscard]] bool Near(Color actual, Color expected, int tolerance = 1)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance
            && std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance
            && std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance
            && std::abs(actual.getAProperty() - expected.getAProperty()) <= tolerance;
    }

    [[nodiscard]] SamplerState Sampler(
        TextureFilter filter, TextureAddressMode u = TextureAddressMode::Clamp,
        TextureAddressMode v = TextureAddressMode::Clamp)
    {
        SamplerState result;
        result.setFilterProperty(filter);
        result.setAddressUProperty(u);
        result.setAddressVProperty(v);
        return result;
    }

    void FillFlatLevel(Texture2D& texture, int level, int width, int height, Color color)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(width) * height, color);
        texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
    }
}

class SkiaSamplerMipmapFilterTest final : public Game
{
public:
    SkiaSamplerMipmapFilterTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        flat_ = std::make_unique<Texture2D>(device, 8, 8, true, SurfaceFormat::Color);
        FillFlatLevel(*flat_, 0, 8, 8, kRed);
        FillFlatLevel(*flat_, 1, 4, 4, kGreen);
        FillFlatLevel(*flat_, 2, 2, 2, kBlue);
        FillFlatLevel(*flat_, 3, 1, 1, kYellow);

        npot_ = std::make_unique<Texture2D>(device, 6, 6, true, SurfaceFormat::Color);
        FillFlatLevel(*npot_, 0, 6, 6, kRed);
        FillFlatLevel(*npot_, 1, 3, 3, kGreen);
        FillFlatLevel(*npot_, 2, 1, 1, kBlue);

        axes_ = std::make_unique<Texture2D>(device, 8, 1, true, SurfaceFormat::Color);
        FillFlatLevel(*axes_, 0, 8, 1, kYellow);
        FillFlatLevel(*axes_, 1, 4, 1, kGreen);
        const std::array<Color, 2> axisLevel {{kRed, kBlue}};
        axes_->SetData(2, nullptr, axisLevel.data(), 0, 2);
        FillFlatLevel(*axes_, 3, 1, 1, kYellow);

        checker_ = std::make_unique<Texture2D>(device, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> checker {{kRed, kGreen, kGreen, kRed}};
        checker_->SetData(checker.data(), static_cast<int>(checker.size()));

        generated_ = std::make_unique<Texture2D>(device, 8, 8, true, SurfaceFormat::Color);
        std::vector<Color> generatedLevel0(64, kRed);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                generatedLevel0[static_cast<std::size_t>(y) * 8u + x]
                    = ((x + y) & 1) == 0 ? kRed : kBlue;
        generated_->SetData(generatedLevel0.data(), static_cast<int>(generatedLevel0.size()));

        crop_ = std::make_unique<Texture2D>(device, 8, 8, true, SurfaceFormat::Color);
        std::vector<Color> cropLevel0(64, kGreen);
        for (int y = 2; y < 6; ++y)
            for (int x = 2; x < 6; ++x)
                cropLevel0[static_cast<std::size_t>(y) * 8u + x] = kRed;
        crop_->SetData(0, nullptr, cropLevel0.data(), 0, static_cast<int>(cropLevel0.size()));
        std::vector<Color> cropLevel1(16, kYellow);
        for (int y = 1; y < 3; ++y)
            for (int x = 1; x < 3; ++x)
                cropLevel1[static_cast<std::size_t>(y) * 4u + x] = kBlue;
        crop_->SetData(1, nullptr, cropLevel1.data(), 0, static_cast<int>(cropLevel1.size()));
        FillFlatLevel(*crop_, 2, 2, 2, kBlack);
        FillFlatLevel(*crop_, 3, 1, 1, kBlack);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        // All six named mip filters accept and agree at integer lambda=2. This also proves the
        // old Begin-time rejection is gone without leaving SpriteBatch state behind.
        for (const FilterCase& entry : kNamedMipFilters)
        {
            const Color actual = RenderPixel(*flat_, Sampler(entry.filter),
                                             Rectangle(0, 0, 2, 2), Rectangle(0, 0, 8, 8), 1, 1);
            Check(Same(actual, kBlue), std::string(entry.name) + " selects exact level 2");
        }

        const Color pointFractional = RenderPixel(
            *flat_, Sampler(TextureFilter::LinearMipPoint),
            Rectangle(0, 0, 3, 3), Rectangle(0, 0, 8, 8), 1, 1);
        Check(Same(pointFractional, kGreen),
              "mip-point selects the nearest lower level below the half-level boundary");

        const Color linearFractional = RenderPixel(
            *flat_, Sampler(TextureFilter::PointMipLinear),
            Rectangle(0, 0, 3, 3), Rectangle(0, 0, 8, 8), 1, 1);
        const float weight = std::log2(8.0f / 3.0f) - 1.0f;
        const Color expectedLinear(0, static_cast<int>(std::lround(255.0f * (1.0f - weight))),
                                   static_cast<int>(std::lround(255.0f * weight)), 255);
        Check(Near(linearFractional, expectedLinear),
              "mip-linear blends only the two lambda-bracketing levels within one byte");

        const std::vector<Color> linearFrame = Render(
            *flat_, Sampler(TextureFilter::Linear),
            Rectangle(0, 0, 3, 3), Rectangle(0, 0, 8, 8), Matrix::getIdentityProperty());
        const std::vector<Color> anisotropicFrame = Render(
            *flat_, Sampler(TextureFilter::Anisotropic),
            Rectangle(0, 0, 3, 3), Rectangle(0, 0, 8, 8), Matrix::getIdentityProperty());
        Check(linearFrame == anisotropicFrame,
              "Anisotropic remains an exact complete-Linear fallback, including mip interpolation");
        Check(!getGraphicsDeviceProperty().SupportsCapability(
                  CNA::GraphicsCapability::AnisotropicFiltering),
              "mip-capable Linear fallback does not advertise real anisotropy");

        Check(Same(RenderPixel(*npot_, Sampler(TextureFilter::Point),
                               Rectangle(0, 0, 3, 3), Rectangle(0, 0, 6, 6), 1, 1), kGreen),
              "6x6 NPOT source minified to 3x3 selects its real 3x3 level");
        Check(Same(RenderPixel(*flat_, Sampler(TextureFilter::Point),
                               Rectangle(0, 0, 1, 1), Rectangle(2, 2, 4, 4), 0, 0), kBlue),
              "a source rectangle computes LOD from its own four-texel footprint");
        const std::vector<Color> strictCrop = Render(
            *crop_, Sampler(TextureFilter::Linear),
            Rectangle(0, 0, 3, 3), Rectangle(2, 2, 4, 4), Matrix::getIdentityProperty());
        Check(std::all_of(strictCrop.begin(), strictCrop.end(), [](Color value)
                         {
                             return value.getGProperty() == 0
                                 && value.getRProperty() > 0 && value.getBProperty() > 0;
                         }),
              "mip-linear strict source rectangles blend only their cropped texels, without atlas bleed");

        Check(Same(RenderPixel(*flat_, Sampler(TextureFilter::Point),
                               Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), 1, 1,
                               Matrix::CreateScale(0.25f)), kBlue),
              "the Begin transform participates in affine LOD selection");

        const std::vector<Color> wrapped = Render(
            *axes_, Sampler(TextureFilter::LinearMipPoint, TextureAddressMode::Wrap),
            Rectangle(0, 0, 2, 1), Rectangle(8, 0, 8, 1), Matrix::getIdentityProperty());
        const std::vector<Color> clamped = Render(
            *axes_, Sampler(TextureFilter::LinearMipPoint, TextureAddressMode::Clamp),
            Rectangle(0, 0, 2, 1), Rectangle(8, 0, 8, 1), Matrix::getIdentityProperty());
        const std::vector<Color> mirrored = Render(
            *axes_, Sampler(TextureFilter::LinearMipPoint, TextureAddressMode::Mirror),
            Rectangle(0, 0, 2, 1), Rectangle(8, 0, 8, 1), Matrix::getIdentityProperty());
        Check(wrapped.size() == 2 && Same(wrapped[0], kRed) && Same(wrapped[1], kBlue),
              "Wrap addresses the selected two-texel mip independently");
        Check(clamped.size() == 2 && Same(clamped[0], kBlue) && Same(clamped[1], kBlue),
              "Clamp holds the selected mip's final texel after source overflow");
        Check(mirrored.size() == 2 && Same(mirrored[0], kBlue) && Same(mirrored[1], kRed),
              "Mirror reflects the selected mip rather than level zero");

        const Color magPoint = RenderPixel(
            *checker_, Sampler(TextureFilter::MinLinearMagPointMipLinear),
            Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), 3, 3);
        const Color magLinear = RenderPixel(
            *checker_, Sampler(TextureFilter::MinPointMagLinearMipPoint),
            Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), 3, 3);
        Check((Same(magPoint, kRed) || Same(magPoint, kGreen))
                  && !Same(magLinear, kRed) && !Same(magLinear, kGreen),
              "magnification keeps the ordinal's independent Point versus Linear component");

        const Color minLinear = RenderPixel(
            *checker_, Sampler(TextureFilter::MinLinearMagPointMipPoint),
            Rectangle(0, 0, 1, 1), Rectangle(0, 0, 2, 2), 0, 0);
        const Color minPoint = RenderPixel(
            *checker_, Sampler(TextureFilter::MinPointMagLinearMipLinear),
            Rectangle(0, 0, 1, 1), Rectangle(0, 0, 2, 2), 0, 0);
        Check(!Same(minLinear, kRed) && !Same(minLinear, kGreen)
                  && (Same(minPoint, kRed) || Same(minPoint, kGreen)),
              "minification keeps the ordinal's independent Linear versus Point component");

        std::vector<Color> generatedLevel2(4, Color::Transparent);
        generated_->GetData(2, nullptr, generatedLevel2.data(), 0,
                            static_cast<int>(generatedLevel2.size()));
        const std::vector<Color> generatedFrame = Render(
            *generated_, Sampler(TextureFilter::Point),
            Rectangle(0, 0, 2, 2), Rectangle(0, 0, 8, 8), Matrix::getIdentityProperty());
        Check(std::all_of(generatedFrame.begin(), generatedFrame.end(),
                          [&](Color value) { return Same(value, generatedLevel2.front()); })
                  && std::all_of(generatedLevel2.begin(), generatedLevel2.end(),
                                 [&](Color value) { return Same(value, generatedLevel2.front()); })
                  && !Same(generatedLevel2.front(), kRed) && !Same(generatedLevel2.front(), kBlue),
              "sampling consumes SKIA-128 generated bytes rather than a private level-zero chain");

        const Color explicitReplacement(197, 31, 149, 255);
        FillFlatLevel(*flat_, 1, 4, 4, explicitReplacement);
        Check(Same(RenderPixel(*flat_, Sampler(TextureFilter::Point),
                               Rectangle(0, 0, 4, 4), Rectangle(0, 0, 8, 8), 2, 2),
                   explicitReplacement),
              "a later explicit mip upload is visible without a stale image cache");

        Exit();
    }

private:
    [[nodiscard]] std::vector<Color> Render(
        Texture2D& texture, SamplerState sampler, const Rectangle& destination,
        const Rectangle& source, const Matrix& transform)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlack);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler,
                            nullptr, nullptr, nullptr, transform);
        spriteBatch_->Draw(texture, destination, source, Color::White);
        spriteBatch_->End();

        const int readWidth = std::max(1, static_cast<int>(
            std::ceil(std::abs(destination.Width * transform.M11))));
        const int readHeight = std::max(1, static_cast<int>(
            std::ceil(std::abs(destination.Height * transform.M22))));
        std::vector<Color> result(static_cast<std::size_t>(readWidth) * readHeight, Color::Transparent);
        const Rectangle readback(0, 0, readWidth, readHeight);
        device.GetBackBufferData(&readback, result.data(), 0, static_cast<int>(result.size()));
        return result;
    }

    [[nodiscard]] Color RenderPixel(
        Texture2D& texture, SamplerState sampler, const Rectangle& destination,
        const Rectangle& source, int sampleX, int sampleY,
        const Matrix& transform = Matrix::getIdentityProperty())
    {
        const std::vector<Color> pixels = Render(texture, std::move(sampler), destination, source, transform);
        const int width = std::max(1, static_cast<int>(
            std::ceil(std::abs(destination.Width * transform.M11))));
        return pixels[static_cast<std::size_t>(sampleY) * width + sampleX];
    }

    void Check(bool pass, const std::string& label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> flat_;
    std::unique_ptr<Texture2D> npot_;
    std::unique_ptr<Texture2D> axes_;
    std::unique_ptr<Texture2D> checker_;
    std::unique_ptr<Texture2D> generated_;
    std::unique_ptr<Texture2D> crop_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaSamplerMipmapFilterTest game;
    game.Run();
    return game.Result();
}
