// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/HdrDisplayOutput.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

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

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        // ST 2084's constants, as the standard states them: exact ratios rather than decimals,
        // because the curve is steep enough at the bottom that a rounded m1 is visible as a lifted
        // black.
        constexpr float kM1 = 2610.0f / 16384.0f;
        constexpr float kM2 = 2523.0f / 4096.0f * 128.0f;
        constexpr float kC1 = 3424.0f / 4096.0f;
        constexpr float kC2 = 2413.0f / 4096.0f * 32.0f;
        constexpr float kC3 = 2392.0f / 4096.0f * 32.0f;

        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform int   uSpace;          // 0 = sRGB, 1 = scRGB, 2 = HDR10
uniform float uPaperWhiteNits;
uniform float uPeakNits;

const float kM1 = 0.1593017578125;
const float kM2 = 78.84375;
const float kC1 = 0.8359375;
const float kC2 = 18.8515625;
const float kC3 = 18.6875;

vec3 cnaEncodePq(vec3 nits) {
    vec3 l = clamp(nits / 10000.0, 0.0, 1.0);
    vec3 p = pow(l, vec3(kM1));
    return pow((kC1 + kC2 * p) / (1.0 + kC3 * p), vec3(kM2));
}

vec3 cnaRec709ToRec2020(vec3 c) {
    return vec3(
        dot(c, vec3(0.6274039, 0.3292830, 0.0433131)),
        dot(c, vec3(0.0690973, 0.9195404, 0.0113623)),
        dot(c, vec3(0.0163914, 0.0880133, 0.8955953)));
}

float cnaRollOff(float nits, float peak) {
    // Reinhard against the peak: monotonic, never reaches it, and leaves everything well below the
    // peak essentially untouched -- which is what stops a bright highlight becoming a flat shape.
    return nits <= 0.0 ? 0.0 : peak * nits / (peak + nits);
}

void main() {
    vec4 source = texture(texture1, TexCoord);

    // The identity, and it has to be exact: SDR output must be the frame the pipeline already
    // produced, or the pass cannot be left in the chain.
    if (uSpace == 0) { FragColor = source; return; }

    if (uSpace == 1) {
        // scRGB: still linear Rec. 709, still the same primaries -- only the scale changes, because
        // 1.0 means 80 nits there rather than "as bright as the display goes".
        FragColor = vec4(source.rgb * (uPaperWhiteNits / 80.0), source.a);
        return;
    }

    vec3 nits = source.rgb * uPaperWhiteNits;
    nits = vec3(cnaRollOff(nits.r, uPeakNits), cnaRollOff(nits.g, uPeakNits),
                cnaRollOff(nits.b, uPeakNits));
    FragColor = vec4(cnaEncodePq(cnaRec709ToRec2020(nits)), source.a);
}
)";

    } // namespace

    HdrDisplayOutput::HdrDisplayOutput(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "HdrDisplayOutput", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid() &&
                     device.ExecutesShaderEffectSourceEXT();
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
        effect_->SetUniformInt("uSpace", static_cast<int>(space_));
        effect_->SetUniformFloat("uPaperWhiteNits", paperWhiteNits_);
        effect_->SetUniformFloat("uPeakNits", peakNits_);
        fullscreen_->draw(source, destination, effect_.get(), width, height);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
