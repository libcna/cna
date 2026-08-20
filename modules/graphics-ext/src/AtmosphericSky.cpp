// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AtmosphericSky.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        // Rayleigh's wavelength dependence is the whole reason a clear sky is blue: the coefficients
        // fall as the fourth power of wavelength, so blue is scattered sideways into the eye several
        // times more than red. Mie's does not depend on wavelength at all, which is why haze is
        // white and why the glare around the sun has no colour of its own.
        //
        // The coefficients are **optical depth through one vertical column of air**, not a density
        // per metre, because everything downstream is measured in air masses rather than in metres.
        constexpr const char* kModelGlsl = R"(
const vec3  kRayleigh        = vec3(0.0464, 0.1085, 0.2650);
const float kMiePerTurbidity = 0.021;
const float kMieG            = 0.76;
const float kSkyScale        = 24.0;

float cnaRayleighPhase(float cosAngle) {
    return 0.05968310365 * (1.0 + cosAngle * cosAngle);   // 3/(16*pi) * (1 + cos^2)
}

float cnaMiePhase(float cosAngle) {
    float gg = kMieG * kMieG;
    float d = 1.0 + gg - 2.0 * kMieG * cosAngle;
    return 0.07957747155 * (1.0 - gg) / max(pow(max(d, 1e-4), 1.5) * (2.0 + gg), 1e-4);
}

/// How much air a ray looking this way passes through, relative to straight up: Kasten and Young's
/// fit, which reaches about 38 at the horizon instead of the secant's infinity. The argument is the
/// sine of the elevation, so it is a direction's y component and nothing has to compute an angle.
float cnaAirMass(float upwards) {
    float up = clamp(upwards, 0.0, 1.0);
    float zenithDegrees = degrees(acos(up));
    return 1.0 / max(up + 0.50572 * pow(max(96.07995 - zenithDegrees, 1e-3), -1.6364), 1e-4);
}

// The scattering integral with the view path supplied rather than assumed. MOD-2141: the sky is
// this function with the path set to the whole atmosphere, and aerial perspective is the same
// function with the path set to however far the geometry is -- one model, called twice, rather than
// two models that agree until someone edits one of them.
vec3 cnaScatteringAlongPath(vec3 viewDirection, vec3 sunDirection, float turbidity,
                            float viewMass) {
    vec3 view  = normalize(viewDirection);
    vec3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);

    float sunMass = cnaAirMass(toSun.y);

    // Turbidity is the ratio of the whole atmosphere's optical thickness to the molecular part
    // alone, so 1 means air with no aerosol in it at all and the Mie term has to vanish there.
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    vec3 total     = kRayleigh + vec3(mie);
    vec3 scattered = kRayleigh * cnaRayleighPhase(cosAngle) + vec3(mie * cnaMiePhase(cosAngle));

    // Single scattering, integrated along the view ray in closed form. The two lengths do separate
    // jobs and must not be added together: the view path is how much air is *lit and looked
    // through*, so a longer one is brighter, while the sun path is what the light lost on the way
    // in, so a longer one is dimmer and redder. A sunset is that second term, not a tint.
    vec3 alongView = vec3(1.0) - exp(-total * viewMass);
    vec3 sunlight  = exp(-total * sunMass);
    return scattered / total * alongView * sunlight * kSkyScale;
}

vec3 cnaSkyRadiance(vec3 viewDirection, vec3 sunDirection, float turbidity) {
    return cnaScatteringAlongPath(viewDirection, sunDirection, turbidity,
                                  cnaAirMass(normalize(viewDirection).y));
}

/// What survives of a colour after this much air, per channel.
vec3 cnaAtmosphereTransmittance(float turbidity, float viewMass) {
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    return exp(-(kRayleigh + vec3(mie)) * viewMass);
}

/// The air masses a ray of this length looking this way passes through.
///
/// The model's coefficients are optical depth through **one vertical column**, so a length divided
/// by the scale height is already in the right units and no conversion is needed. The cap is not
/// cosmetic: without it a distant enough object accumulates more air than the whole sky behind it
/// has, and comes back hazier than the horizon -- which cannot happen.
float cnaAerialAirMass(vec3 viewDirection, float distance, float scaleHeight) {
    float full = cnaAirMass(normalize(viewDirection).y);
    return min(max(distance, 0.0) / max(scaleHeight, 1e-3), full);
}

/// Geometry seen through this much air: what is left of its own colour, plus what the air adds.
vec3 cnaAerialPerspective(vec3 colour, vec3 viewDirection, vec3 sunDirection, float turbidity,
                          float viewMass) {
    return colour * cnaAtmosphereTransmittance(turbidity, viewMass)
         + cnaScatteringAlongPath(viewDirection, sunDirection, turbidity, viewMass);
}
)";

        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform mat4  uInverseViewProjection;
uniform vec3  uSunDirection;
uniform float uTurbidity;
uniform float uIntensity;

void main() {
    vec4 ray = uInverseViewProjection * vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(ray.xyz / ray.w);
    FragColor = vec4(cnaSkyRadiance(direction, uSunDirection, uTurbidity) * uIntensity, 1.0);
}
)";

        Matrix RotationOnly(const Matrix& view)
        {
            Matrix rotation = view;
            rotation.M41 = 0.0f;
            rotation.M42 = 0.0f;
            rotation.M43 = 0.0f;
            return rotation;
        }

        Vector3 Normalized(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (length <= 1e-6f) return fallback;
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

        float AirMass(const float upwards)
        {
            const float up = std::clamp(upwards, 0.0f, 1.0f);
            const float zenithDegrees = std::acos(up) * 57.2957795f;
            return 1.0f / std::max(up + 0.50572f * std::pow(std::max(96.07995f - zenithDegrees, 1e-3f),
                                                            -1.6364f),
                                   1e-4f);
        }

    } // namespace

    AtmosphericSky::AtmosphericSky(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        std::string source = "#version 300 es\nprecision highp float;\n";
        source += kModelGlsl;
        source += kFragmentBody;
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, source);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "AtmosphericSky", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid();

        // A one-pixel source, because the sky is computed rather than sampled and FullscreenPass
        // still needs something to draw with.
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        white_->SetData(&white, 1);
    }

    AtmosphericSky::~AtmosphericSky() = default;

    bool AtmosphericSky::isSupported() const { return supported_; }

    std::string AtmosphericSky::getModelGlsl()
    {
        return std::string(kModelGlsl);
    }

    Vector3 AtmosphericSky::radiance(const Vector3& viewDirection, const Vector3& sunDirection,
                                     const float turbidity)
    {
        const Vector3 view = Normalized(viewDirection, Vector3(0.0f, 1.0f, 0.0f));
        const Vector3 toSun = Normalized(Vector3(-sunDirection.X, -sunDirection.Y, -sunDirection.Z),
                                         Vector3(0.0f, 1.0f, 0.0f));
        const float cosAngle = view.X * toSun.X + view.Y * toSun.Y + view.Z * toSun.Z;

        const float viewMass = AirMass(view.Y);
        const float sunMass  = AirMass(toSun.Y);

        const Vector3 rayleigh(0.0464f, 0.1085f, 0.2650f);
        const float mie = 0.021f * std::max(turbidity - 1.0f, 0.0f);

        const float rayleighPhase = 0.05968310365f * (1.0f + cosAngle * cosAngle);
        const float gg = 0.76f * 0.76f;
        const float d  = 1.0f + gg - 2.0f * 0.76f * cosAngle;
        const float miePhase = 0.07957747155f * (1.0f - gg) /
                               std::max(std::pow(std::max(d, 1e-4f), 1.5f) * (2.0f + gg), 1e-4f);

        Vector3 result(0.0f, 0.0f, 0.0f);
        float* out = &result.X;
        const float* base = &rayleigh.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float total = base[channel] + mie;
            const float scattered = base[channel] * rayleighPhase + mie * miePhase;
            const float alongView = 1.0f - std::exp(-total * viewMass);
            const float sunlight  = std::exp(-total * sunMass);
            out[channel] = scattered / total * alongView * sunlight * 24.0f;
        }
        return result;
    }

    void AtmosphericSky::draw(const Matrix& view, const Matrix& projection, const int width,
                              const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::AtmosphericSky::draw: the target size must be positive");
        if (!supported_) return;

        const Matrix inverse = Matrix::Invert(RotationOnly(view) * projection);
        effect_->Apply();
        effect_->SetUniformMat4("uInverseViewProjection", &inverse.M11);
        effect_->SetUniformVec3("uSunDirection", sunDirection_.X, sunDirection_.Y, sunDirection_.Z);
        effect_->SetUniformFloat("uTurbidity", turbidity_);
        effect_->SetUniformFloat("uIntensity", intensity_);

        fullscreen_->drawOverCurrentTarget(white_.get(), effect_.get(), width, height);
    }

    Vector3 AtmosphericSky::getSunDirection() const { return sunDirection_; }
    void    AtmosphericSky::setSunDirection(const Vector3& value)
    {
        const float length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        if (length > 1e-6f) sunDirection_ = Vector3(value.X / length, value.Y / length, value.Z / length);
    }

    float AtmosphericSky::getTurbidity() const { return turbidity_; }
    void  AtmosphericSky::setTurbidity(const float value)
    {
        turbidity_ = std::clamp(value, 1.0f, 10.0f);
    }

    float AtmosphericSky::getIntensity() const { return intensity_; }
    void  AtmosphericSky::setIntensity(const float value) { if (value >= 0.0f) intensity_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
