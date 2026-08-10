// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/CRTEffect.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

namespace {

    // Vertex layout matches SpriteBatch's own vertex buffer (Position=0, TexCoord=1, Color=2) —
    // same convention as every other CNA ShaderEffect-based post-process (see DepthEffect.cpp).
    const char* const kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 vTexCoord;
out vec4 vColor;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";

    // uMaskType: 0 = None, 1 = ApertureGrille (fixed RGB column stripes), 2 = ShadowMask
    // (same stripes, staggered by half a triad every other row for a brick-like pattern).
    // Scanlines/mask index by gl_FragCoord (already real screen pixels), so no separate
    // resolution uniform is needed; curvature warps vTexCoord, which is resolution-independent.
    const char* const kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform float uScanlineIntensity;
uniform float uCurvature;
uniform float uVignetteIntensity;
uniform float uMaskIntensity;
uniform int uMaskType;

vec2 applyCurvature(vec2 uv) {
    vec2 cc = uv - 0.5;
    float dist = dot(cc, cc) * uCurvature;
    return uv + cc * dist;
}

void main() {
    vec2 uv = applyCurvature(vTexCoord);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 texColor = texture(uTexture, uv) * vColor;
    vec3 rgb = texColor.rgb;

    // Scanlines: darken every other physical screen row.
    float rowParity = mod(floor(gl_FragCoord.y), 2.0);
    rgb *= mix(1.0, 1.0 - uScanlineIntensity, rowParity);

    // RGB sub-pixel mask (aperture grille / shadow mask).
    if (uMaskType != 0) {
        float colBase = floor(gl_FragCoord.x);
        if (uMaskType == 2) {
            float rowGroup = mod(floor(gl_FragCoord.y / 2.0), 2.0);
            colBase += rowGroup * 1.5;
        }
        float col = mod(colBase, 3.0);
        vec3 mask = vec3(1.0 - uMaskIntensity);
        if (col < 1.0) mask.r = 1.0;
        else if (col < 2.0) mask.g = 1.0;
        else mask.b = 1.0;
        rgb *= mask;
    }

    // Corner vignette.
    vec2 vc = vTexCoord - 0.5;
    float vignette = 1.0 - uVignetteIntensity * dot(vc, vc) * 2.0;
    rgb *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(rgb, texColor.a);
}
)";

} // namespace

namespace CNA::Graphics {

    CRTEffect::CRTEffect(GraphicsDevice& device)
        : ShaderEffect(device, kVertexSource, kFragmentSource)
    {
    }

    float CRTEffect::getScanlineIntensity() const { return scanlineIntensity_; }
    void CRTEffect::setScanlineIntensity(float value) { scanlineIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getCurvature() const { return curvature_; }
    void CRTEffect::setCurvature(float value) { curvature_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getVignetteIntensity() const { return vignetteIntensity_; }
    void CRTEffect::setVignetteIntensity(float value) { vignetteIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getMaskIntensity() const { return maskIntensity_; }
    void CRTEffect::setMaskIntensity(float value) { maskIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    CRTMaskType CRTEffect::getMaskType() const { return maskType_; }
    void CRTEffect::setMaskType(CRTMaskType type) { maskType_ = type; }

    const std::string& CRTEffect::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.CRTEffect";
        return name;
    }

    void CRTEffect::OnApply()
    {
        ShaderEffect::OnApply();
        SetUniformFloat("uScanlineIntensity", scanlineIntensity_);
        SetUniformFloat("uCurvature", curvature_);
        SetUniformFloat("uVignetteIntensity", vignetteIntensity_);
        SetUniformFloat("uMaskIntensity", maskIntensity_);
        SetUniformInt("uMaskType", static_cast<int>(maskType_));
    }

    Effect* CRTEffect::Clone()
    {
        auto* clone = new CRTEffect(*device_);
        clone->setScanlineIntensity(scanlineIntensity_);
        clone->setCurvature(curvature_);
        clone->setVignetteIntensity(vignetteIntensity_);
        clone->setMaskIntensity(maskIntensity_);
        clone->setMaskType(maskType_);
        return clone;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
