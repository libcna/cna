// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Texture3D;

    namespace {

        class ScopedSamplerStateOverride final
        {
        public:
            ScopedSamplerStateOverride(GraphicsDevice& device, const int slot,
                                       const SamplerState& replacement)
                : device_(device), slot_(slot), previous_(device.getSamplerStatesProperty()[slot])
            {
                device_.getSamplerStatesProperty()[slot_] = replacement;
            }

            ~ScopedSamplerStateOverride()
            {
                device_.getSamplerStatesProperty()[slot_] = previous_;
            }

            ScopedSamplerStateOverride(const ScopedSamplerStateOverride&) = delete;
            ScopedSamplerStateOverride& operator=(const ScopedSamplerStateOverride&) = delete;

        private:
            GraphicsDevice& device_;
            int slot_;
            SamplerState previous_;
        };

    } // namespace

    ColorGradePass::ColorGradePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const auto makeEffect = [&device](const ShaderPackageEXT& package) {
            return package.selectFor(device).isUsable()
                ? std::make_unique<ShaderEffect>(device, package)
                : nullptr;
        };
        effect_ = makeEffect(detail::CreateColorGradeStripShaderPackage());
        tetrahedralStripEffect_ =
            makeEffect(detail::CreateColorGradeInterpolatedStripShaderPackage());
        volumeEffect_ = makeEffect(detail::CreateColorGradeVolumeShaderPackage());

        bool logged = false;
        detail::reportShaderCompileFailure(device, "ColorGradePass", effect_.get(), logged);
        detail::reportShaderCompileFailure(device, "ColorGradePass (tetrahedral strip)",
                                           tetrahedralStripEffect_.get(), logged);
        detail::reportShaderCompileFailure(device, "ColorGradePass (volume)", volumeEffect_.get(),
                                           logged);
    }

    ColorGradePass::~ColorGradePass() = default;

    int ColorGradePass::lutSizeForStrip(const int width, const int height)
    {
        if (height < 2 || height > kMaxLutSize) return 0;
        if (width != height * height) return 0;
        return height;
    }

    std::unique_ptr<Texture2D> ColorGradePass::createIdentityLut(GraphicsDevice& device,
                                                                 const int size)
    {
        if (size < 2 || size > kMaxLutSize)
            throw std::invalid_argument(
                "CNA::Graphics::ColorGradePass::createIdentityLut: the slice count must be between "
                "2 and 64 -- a table smaller than two entries cannot interpolate, and one larger "
                "than 64 needs a strip wider than 4096 texels");

        const int width = size * size;
        auto texture = std::make_unique<Texture2D>(device, width, size);

        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * size);
        const float last = static_cast<float>(size - 1);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < width; ++x)
            {
                const int slice = x / size;
                const int red = x % size;
                const int green = y;
                texels.emplace_back(
                    static_cast<int>(static_cast<float>(red) / last * 255.0f + 0.5f),
                    static_cast<int>(static_cast<float>(green) / last * 255.0f + 0.5f),
                    static_cast<int>(static_cast<float>(slice) / last * 255.0f + 0.5f), 255);
            }
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

    void ColorGradePass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float strength = settings != nullptr ? settings->getColorGradeStrength() : strength_;

        // A volume table wins over a strip when both are set: it is the same table in a layout that
        // needs no arithmetic to address, so preferring the strip would be preferring the harder of
        // two identical answers.
        const bool useVolume = volumeLut_ != nullptr && volumeEffect_ != nullptr
                            && volumeEffect_->IsEffectValid();
        const bool useTetrahedralStrip = !useVolume && lut_ != nullptr
                                      && interpolation_ == LutInterpolation::Tetrahedral
                                      && tetrahedralStripEffect_ != nullptr
                                      && tetrahedralStripEffect_->IsEffectValid();

        ShaderEffect* chosen = nullptr;
        if (useVolume) chosen = volumeEffect_.get();
        else if (useTetrahedralStrip) chosen = tetrahedralStripEffect_.get();
        else if (lut_ != nullptr && effect_ != nullptr && effect_->IsEffectValid())
            chosen = effect_.get();

        if (chosen == nullptr || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        // The filtered strip requires linear sampling inside each red/green slice. Exact strip and
        // volume paths use texelFetch, but point-clamp still states their no-cross-cell contract and
        // prevents the application's secondary sampler state from becoming part of this pass.
        const SamplerState& lutSampler =
            chosen == effect_.get() ? SamplerState::LinearClamp : SamplerState::PointClamp;
        ScopedSamplerStateOverride samplerScope(
            *chosen->getGraphicsDeviceProperty(), 1, lutSampler);

        chosen->Apply();
        const float lutSize = static_cast<float>(useVolume ? volumeLutSize_ : lutSize_);
        const float tetrahedral = interpolation_ == LutInterpolation::Tetrahedral ? 1.0f : 0.0f;
        chosen->SetUniformVec4("uColorGradeParams", lutSize, strength, tetrahedral, 0.0f);
        if (useVolume)
        {
            chosen->SetUniformInt("uLutVolume", 1);
            chosen->SetTexture(1, *volumeLut_);
        }
        else
        {
            chosen->SetUniformInt("uLutSampler", 1);
            chosen->SetTexture(1, *lut_);
        }

        fullscreen_->draw(context.source, context.destination, chosen,
                          context.width, context.height);
    }

    const std::string& ColorGradePass::getName() const
    {
        static const std::string name = "ColorGrade";
        return name;
    }

    bool ColorGradePass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    Texture2D* ColorGradePass::getLut() const { return lut_; }

    void ColorGradePass::setLut(Texture2D* lut)
    {
        if (lut == nullptr)
        {
            lut_ = nullptr;
            lutSize_ = 0;
            return;
        }

        const int size = lutSizeForStrip(lut->getWidthProperty(), lut->getHeightProperty());
        if (size == 0)
            throw std::invalid_argument(
                "CNA::Graphics::ColorGradePass::setLut: a lookup table must be a strip of N slices "
                "of N by N, so its width must be the square of its height -- refused rather than "
                "sampled, because a strip read at the wrong slice count grades the frame into "
                "colours nothing in the table names");

        lut_ = lut;
        lutSize_ = size;
    }

    Texture3D* ColorGradePass::getVolumeLut() const { return volumeLut_; }

    void ColorGradePass::setVolumeLut(Texture3D* lut)
    {
        if (lut == nullptr)
        {
            volumeLut_ = nullptr;
            volumeLutSize_ = 0;
            return;
        }

        const int width = lut->getWidthProperty();
        const int height = lut->getHeightProperty();
        const int depth = lut->getDepthProperty();
        if (width != height || width != depth)
            throw std::invalid_argument(
                "CNA::Graphics::ColorGradePass::setVolumeLut: a lookup volume must be a cube -- "
                "one entry per (red, green, blue) triple and the same count on every axis");
        if (width < 2 || width > kMaxLutSize)
            throw std::invalid_argument(
                "CNA::Graphics::ColorGradePass::setVolumeLut: the edge length must be between 2 "
                "and 64");

        volumeLut_ = lut;
        volumeLutSize_ = width;
    }

    LutInterpolation ColorGradePass::getInterpolation() const { return interpolation_; }

    void ColorGradePass::setInterpolation(const LutInterpolation value)
    {
        interpolation_ = value;
    }

    float ColorGradePass::getStrength() const { return strength_; }

    void ColorGradePass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
