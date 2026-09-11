// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/HdrDisplayOutput.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        // ST 2084's constants, as the standard states them: exact ratios rather than decimals,
        // because the curve is steep enough at the bottom that a rounded m1 is visible as a lifted
        // black.
        constexpr float kM1 = 2610.0f / 16384.0f;
        constexpr float kM2 = 2523.0f / 4096.0f * 128.0f;
        constexpr float kC1 = 3424.0f / 4096.0f;
        constexpr float kC2 = 2413.0f / 4096.0f * 32.0f;
        constexpr float kC3 = 2392.0f / 4096.0f * 32.0f;

    } // namespace

    HdrDisplayOutput::HdrDisplayOutput(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateHdrDisplayShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "HdrDisplayOutput", effect_.get(), logged);
        supported_ = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                  && effect_ != nullptr && effect_->IsEffectValid();
    }

    HdrDisplayOutput::~HdrDisplayOutput() = default;

    bool HdrDisplayOutput::isSupported() const { return supported_; }

    CNA::DisplayColorSpace HdrDisplayOutput::getColorSpace() const { return space_; }
    void HdrDisplayOutput::setColorSpace(const CNA::DisplayColorSpace value) { space_ = value; }

    float HdrDisplayOutput::getPaperWhiteNits() const { return paperWhiteNits_; }
    void  HdrDisplayOutput::setPaperWhiteNits(const float value)
    {
        paperWhiteNits_ = std::max(value, 1.0f);
        peakNits_ = std::max(peakNits_, paperWhiteNits_);
    }

    float HdrDisplayOutput::getPeakNits() const { return peakNits_; }
    void  HdrDisplayOutput::setPeakNits(const float value)
    {
        peakNits_ = std::max(value, paperWhiteNits_);
    }

    float HdrDisplayOutput::encodePq(const float nits)
    {
        const float l = std::clamp(nits / 10000.0f, 0.0f, 1.0f);
        const float p = std::pow(l, kM1);
        return std::pow((kC1 + kC2 * p) / (1.0f + kC3 * p), kM2);
    }

    float HdrDisplayOutput::decodePq(const float encoded)
    {
        const float e = std::clamp(encoded, 0.0f, 1.0f);
        const float p = std::pow(e, 1.0f / kM2);
        const float numerator = std::max(p - kC1, 0.0f);
        const float denominator = kC2 - kC3 * p;
        if (denominator <= 0.0f) return 10000.0f;
        return std::pow(numerator / denominator, 1.0f / kM1) * 10000.0f;
    }

    Vector3 HdrDisplayOutput::rec709ToRec2020(const Vector3& color)
    {
        return Vector3(
            0.6274039f * color.X + 0.3292830f * color.Y + 0.0433131f * color.Z,
            0.0690973f * color.X + 0.9195404f * color.Y + 0.0113623f * color.Z,
            0.0163914f * color.X + 0.0880133f * color.Y + 0.8955953f * color.Z);
    }

    float HdrDisplayOutput::rollOff(const float nits, const float peakNits)
    {
        if (nits <= 0.0f) return 0.0f;
        return peakNits * nits / (peakNits + nits);
    }

    Vector3 HdrDisplayOutput::encode(const CNA::DisplayColorSpace space, const Vector3& sceneLinear,
                                     const float paperWhiteNits, const float peakNits)
    {
        if (space == CNA::DisplayColorSpace::Srgb) return sceneLinear;
        if (space == CNA::DisplayColorSpace::Scrgb)
        {
            const float scale = paperWhiteNits / 80.0f;
            return Vector3(sceneLinear.X * scale, sceneLinear.Y * scale, sceneLinear.Z * scale);
        }

        const Vector3 nits(rollOff(sceneLinear.X * paperWhiteNits, peakNits),
                           rollOff(sceneLinear.Y * paperWhiteNits, peakNits),
                           rollOff(sceneLinear.Z * paperWhiteNits, peakNits));
        const Vector3 wide = rec709ToRec2020(nits);
        return Vector3(encodePq(wide.X), encodePq(wide.Y), encodePq(wide.Z));
    }

    void HdrDisplayOutput::draw(Texture2D* source, RenderTarget2D* destination, const int width,
                                const int height)
    {
        if (source == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::HdrDisplayOutput::draw: there is nothing to encode");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::HdrDisplayOutput::draw: the target size must be positive");

        if (!supported_)
        {
            fullscreen_->draw(source, destination, nullptr, width, height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec4("uHdrDisplayParams", static_cast<float>(space_),
                                paperWhiteNits_, peakNits_, 0.0f);
        fullscreen_->draw(source, destination, effect_.get(), width, height);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
