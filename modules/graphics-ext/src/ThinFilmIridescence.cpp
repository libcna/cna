// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ThinFilmIridescence.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;

    namespace {

        constexpr float kPi = 3.14159265359f;

        float Square(const float value) { return value * value; }

        Vector3 Square(const Vector3& v) { return Vector3(v.X * v.X, v.Y * v.Y, v.Z * v.Z); }

        float SchlickScalar(const float f0, const float cosTheta)
        {
            return f0 + (1.0f - f0) * std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
        }

        float IorToFresnel0(const float transmitted, const float incident)
        {
            return Square((transmitted - incident) / (transmitted + incident));
        }

        /// The eye's response to a given optical path difference, as three Gaussians per channel
        /// fitted to the CIE curves and converted to Rec. 709. This is the step that turns an
        /// interference pattern over wavelength into a colour.
        Vector3 Sensitivity(const float opd, const Vector3& shift)
        {
            const float phase = 2.0f * kPi * opd * 1.0e-9f;
            const float value[3] = {5.4856e-13f, 4.4201e-13f, 5.2481e-13f};
            const float position[3] = {1.6810e+06f, 1.7953e+06f, 2.2084e+06f};
            const float variance[3] = {4.3278e+09f, 9.3046e+09f, 6.6121e+09f};

            float xyz[3];
            const float shifts[3] = {shift.X, shift.Y, shift.Z};
            for (int channel = 0; channel < 3; ++channel)
                xyz[channel] = value[channel] * std::sqrt(2.0f * kPi * variance[channel]) *
                               std::cos(position[channel] * phase + shifts[channel]) *
                               std::exp(-Square(phase) * variance[channel]);

            xyz[0] += 9.7470e-14f * std::sqrt(2.0f * kPi * 4.5282e+09f) *
                      std::cos(2.2399e+06f * phase + shifts[0]) *
                      std::exp(-4.5282e+09f * Square(phase));
            for (float& component : xyz) component /= 1.0685e-7f;

            // XYZ to Rec. 709, written out rather than through a matrix type, so the two languages
            // hold the same twelve digits in the same order.
            return Vector3(3.2404542f * xyz[0] - 1.5371385f * xyz[1] - 0.4985314f * xyz[2],
                           -0.9692660f * xyz[0] + 1.8760108f * xyz[1] + 0.0415560f * xyz[2],
                           0.0556434f * xyz[0] - 0.2040259f * xyz[1] + 1.0572252f * xyz[2]);
        }

    } // namespace

    Vector3 ThinFilmIridescence::evaluate(const float outsideIor, const float filmIor,
                                          const float cosTheta, const float thicknessNm,
                                          const Vector3& baseF0)
    {
        const float cosTheta1 = std::clamp(cosTheta, 0.0f, 1.0f);

        // A film of no thickness returns the base's own Schlick reflectance exactly, and that is a
        // deliberate departure from the reference implementation. There, the compound reflectance
        // clamps the product `R12 * R23` to a floor of 1e-5 rather than to zero, so at zero
        // thickness the first interference order survives as a residue of about 0.007 -- small, but
        // *coloured*, and it means a material carrying the extension with the film switched off is
        // not the material without it. Answering the base directly makes "off" mean off.
        if (!(thicknessNm > 0.0f))
        {
            const float schlick = std::pow(std::clamp(1.0f - cosTheta1, 0.0f, 1.0f), 5.0f);
            return Vector3(baseF0.X + (1.0f - baseF0.X) * schlick,
                           baseF0.Y + (1.0f - baseF0.Y) * schlick,
                           baseF0.Z + (1.0f - baseF0.Z) * schlick);
        }

        // A film of no thickness is not a film: the index is faded back to the surrounding medium
        // over the first few nanometres so that a material with the extension present and the
        // thickness at zero is exactly the material without it.
        const float smooth = std::clamp(thicknessNm / 0.03f, 0.0f, 1.0f);
        const float smoothstep = smooth * smooth * (3.0f - 2.0f * smooth);
        const float iridescenceIor = outsideIor + (filmIor - outsideIor) * smoothstep;

        const float sinTheta2Squared = Square(outsideIor / iridescenceIor) *
                                       (1.0f - Square(cosTheta1));
        const float cosTheta2Squared = 1.0f - sinTheta2Squared;
        // Total internal reflection: nothing enters the film, so everything comes back.
        if (cosTheta2Squared < 0.0f) return Vector3(1.0f, 1.0f, 1.0f);
        const float cosTheta2 = std::sqrt(cosTheta2Squared);

        const float r0 = IorToFresnel0(iridescenceIor, outsideIor);
        const float r12 = SchlickScalar(r0, cosTheta1);
        const float t121 = 1.0f - r12;
        const float phi12 = iridescenceIor < outsideIor ? kPi : 0.0f;
        const float phi21 = kPi - phi12;

        Vector3 r23(0.0f, 0.0f, 0.0f);
        Vector3 phi23(0.0f, 0.0f, 0.0f);
        {
            const float* base = &baseF0.X;
            float* out23 = &r23.X;
            float* outPhi = &phi23.X;
            for (int channel = 0; channel < 3; ++channel)
            {
                const float clamped = std::clamp(base[channel], 0.0f, 0.9999f);
                const float root = std::sqrt(clamped);
                const float baseIor = (1.0f + root) / (1.0f - root);
                out23[channel] = SchlickScalar(IorToFresnel0(baseIor, iridescenceIor), cosTheta2);
                outPhi[channel] = baseIor < iridescenceIor ? kPi : 0.0f;
            }
        }

        const float opd = 2.0f * iridescenceIor * thicknessNm * cosTheta2;

        // Written to mirror the emitted GLSL line for line, because the two are compared for
        // agreement and a restructured copy is a copy that can drift.
        Vector3 r123(0.0f, 0.0f, 0.0f);
        Vector3 rs(0.0f, 0.0f, 0.0f);
        Vector3 result(0.0f, 0.0f, 0.0f);
        Vector3 cm(0.0f, 0.0f, 0.0f);
        for (int channel = 0; channel < 3; ++channel)
        {
            const float r23c = (&r23.X)[channel];
            const float value = std::clamp(r12 * r23c, 1e-5f, 0.9999f);
            (&r123.X)[channel] = value;
            (&rs.X)[channel] = Square(t121) * r23c / (1.0f - value);
            (&result.X)[channel] = r12 + (&rs.X)[channel];
            (&cm.X)[channel] = (&rs.X)[channel] - t121;
        }

        const Vector3 phi(phi21 + phi23.X, phi21 + phi23.Y, phi21 + phi23.Z);

        // One pair of diracs per interference order. Two is what the reference model keeps; the
        // third is below the noise of the Gaussian fit.
        for (int order = 1; order <= 2; ++order)
        {
            for (int channel = 0; channel < 3; ++channel)
                (&cm.X)[channel] *= std::sqrt((&r123.X)[channel]);

            const float scale = static_cast<float>(order);
            const Vector3 sensitivity =
                Sensitivity(scale * opd, Vector3(scale * phi.X, scale * phi.Y, scale * phi.Z));
            result.X += cm.X * 2.0f * sensitivity.X;
            result.Y += cm.Y * 2.0f * sensitivity.Y;
            result.Z += cm.Z * 2.0f * sensitivity.Z;
        }

        return Vector3(std::max(result.X, 0.0f), std::max(result.Y, 0.0f),
                       std::max(result.Z, 0.0f));
    }

    std::string ThinFilmIridescence::getGlsl()
    {
        return R"(
const float kCnaFilmPi = 3.14159265359;

float cnaFilmSquare(float v) { return v * v; }
vec3  cnaFilmSquare(vec3 v) { return v * v; }

float cnaFilmSchlick(float f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 cnaFilmSchlick(vec3 f0, float cosTheta) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float cnaFilmIorToFresnel0(float transmitted, float incident) {
    return cnaFilmSquare((transmitted - incident) / (transmitted + incident));
}

vec3 cnaFilmIorToFresnel0(vec3 transmitted, float incident) {
    return cnaFilmSquare((transmitted - vec3(incident)) / (transmitted + vec3(incident)));
}

vec3 cnaFilmFresnel0ToIor(vec3 fresnel0) {
    vec3 root = sqrt(clamp(fresnel0, 0.0, 0.9999));
    return (vec3(1.0) + root) / (vec3(1.0) - root);
}

/// The eye's response to an optical path difference, as Gaussians fitted to the CIE curves and
/// converted to Rec. 709. This is what turns interference over wavelength into a colour.
vec3 cnaFilmSensitivity(float opd, vec3 shift) {
    float phase = 2.0 * kCnaFilmPi * opd * 1.0e-9;
    vec3 value = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 position = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 variance = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);

    vec3 xyz = value * sqrt(2.0 * kCnaFilmPi * variance) * cos(position * phase + shift)
             * exp(-cnaFilmSquare(phase) * variance);
    xyz.x += 9.7470e-14 * sqrt(2.0 * kCnaFilmPi * 4.5282e+09)
           * cos(2.2399e+06 * phase + shift.x) * exp(-4.5282e+09 * cnaFilmSquare(phase));
    xyz /= 1.0685e-7;

    return vec3(3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
               -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
                0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z);
}

vec3 cnaThinFilmIridescence(float outsideIor, float filmIor, float cosTheta, float thicknessNm,
                            vec3 baseF0) {
    float cosTheta1 = clamp(cosTheta, 0.0, 1.0);

    // A film of no thickness returns the base's own Schlick reflectance exactly -- see the C++
    // mirror for why the reference implementation does not, and why "off" should mean off.
    if (thicknessNm <= 0.0) return cnaFilmSchlick(baseF0, cosTheta1);

    // A film of no thickness is not a film: the index fades back to the surrounding medium over the
    // first few nanometres, so the extension present with a zero thickness is the material without.
    float iridescenceIor = mix(outsideIor, filmIor, smoothstep(0.0, 0.03, thicknessNm));

    float sinTheta2Squared = cnaFilmSquare(outsideIor / iridescenceIor)
                           * (1.0 - cnaFilmSquare(cosTheta1));
    float cosTheta2Squared = 1.0 - sinTheta2Squared;
    if (cosTheta2Squared < 0.0) return vec3(1.0);   // total internal reflection
    float cosTheta2 = sqrt(cosTheta2Squared);

    float r0 = cnaFilmIorToFresnel0(iridescenceIor, outsideIor);
    float r12 = cnaFilmSchlick(r0, cosTheta1);
    float t121 = 1.0 - r12;
    float phi12 = iridescenceIor < outsideIor ? kCnaFilmPi : 0.0;
    float phi21 = kCnaFilmPi - phi12;

    vec3 baseIor = cnaFilmFresnel0ToIor(baseF0);
    vec3 r23 = cnaFilmSchlick(cnaFilmIorToFresnel0(baseIor, iridescenceIor), cosTheta2);
    vec3 phi23 = vec3(baseIor.x < iridescenceIor ? kCnaFilmPi : 0.0,
                      baseIor.y < iridescenceIor ? kCnaFilmPi : 0.0,
                      baseIor.z < iridescenceIor ? kCnaFilmPi : 0.0);

    float opd = 2.0 * iridescenceIor * thicknessNm * cosTheta2;
    vec3 phi = vec3(phi21) + phi23;

    vec3 r123 = clamp(vec3(r12) * r23, 1e-5, 0.9999);
    vec3 rs = cnaFilmSquare(vec3(t121)) * r23 / (vec3(1.0) - r123);

    vec3 result = vec3(r12) + rs;
    vec3 cm = rs - vec3(t121);
    for (int order = 1; order <= 2; ++order) {
        cm *= sqrt(r123);
        result += cm * 2.0 * cnaFilmSensitivity(float(order) * opd, float(order) * phi);
    }
    return max(result, vec3(0.0));
}
)";
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
