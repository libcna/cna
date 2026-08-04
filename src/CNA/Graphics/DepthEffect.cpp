// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthEffect.hpp"

#ifdef CNA_NOXNA

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

namespace {

    // EasyGL (OpenGL ES 3.0+) source. Vertex layout matches SpriteBatch's own vertex
    // buffer (Position=0, TexCoord=1, Color=2). uTexture is left at its GLSL-default
    // sampler unit 0, which is where SpriteBatch binds the sprite's texture for any
    // custom effect — matching the convention every other CNA ShaderEffect-based
    // post-process shader already relies on (see examples/easygl_postprocesseffect_shader_test.cpp).
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

    // uMode selects the quantization applied to the sampled*tinted colour:
    //   0 = Color16Bit   (RGB565 — 32/64/32 levels)
    //   1 = Color8Bit    (RGB332 — 8/8/4 levels)
    //   2 = Grayscale4Bit (16 luminance levels)
    //   3 = Grayscale2Bit (4 luminance levels)
    //   4 = Grayscale1Bit (2 luminance levels — pure black/white)
    // Luminance uses the BT.601 luma weights, matching this project's other greyscale
    // conversions (e.g. the ASCII backend).
    const char* const kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform int uMode;

float quantizeChannel(float value, float levels) {
    return floor(value * (levels - 1.0) + 0.5) / (levels - 1.0);
}

void main() {
    vec4 texColor = texture(uTexture, vTexCoord) * vColor;
    vec3 rgb = texColor.rgb;

    if (uMode == 0) {
        rgb.r = quantizeChannel(rgb.r, 32.0);
        rgb.g = quantizeChannel(rgb.g, 64.0);
        rgb.b = quantizeChannel(rgb.b, 32.0);
    } else if (uMode == 1) {
        rgb.r = quantizeChannel(rgb.r, 8.0);
        rgb.g = quantizeChannel(rgb.g, 8.0);
        rgb.b = quantizeChannel(rgb.b, 4.0);
    } else {
        float levels = 2.0;
        if (uMode == 2) levels = 16.0;
        else if (uMode == 3) levels = 4.0;
        float gray = dot(rgb, vec3(0.299, 0.587, 0.114));
        gray = quantizeChannel(gray, levels);
        rgb = vec3(gray);
    }

    FragColor = vec4(rgb, texColor.a);
}
)";

} // namespace

namespace CNA::Graphics {

    DepthEffect::DepthEffect(GraphicsDevice& device)
        : ShaderEffect(device, kVertexSource, kFragmentSource)
    {
    }

    DepthEffectMode DepthEffect::getMode() const { return mode_; }
    void DepthEffect::setMode(DepthEffectMode mode) { mode_ = mode; }

    const std::string& DepthEffect::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.DepthEffect";
        return name;
    }

    void DepthEffect::OnApply()
    {
        ShaderEffect::OnApply();
        SetUniformInt("uMode", static_cast<int>(mode_));
    }

    Effect* DepthEffect::Clone()
    {
        auto* clone = new DepthEffect(*device_);
        clone->setMode(mode_);
        return clone;
    }

} // namespace CNA::Graphics

#endif // CNA_NOXNA
