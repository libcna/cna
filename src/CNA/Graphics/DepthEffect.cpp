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
    //
    // uDither selects an ordered (Bayer matrix) dither threshold added before quantization,
    // matching DitherMode: 0 = None, 1 = Bayer4x4, 2 = Bayer8x8. Error-diffusion dithering
    // (Floyd-Steinberg/Atkinson) is intentionally not offered here — it is inherently
    // sequential (each pixel's rounding error is propagated into not-yet-shaded neighbours),
    // which does not parallelize onto a single fragment-shader pass without compute-shader
    // support (see DitherMode.hpp).
    const char* const kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform int uMode;
uniform int uDither;

const float kBayer4x4[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
);

const float kBayer8x8[64] = float[64](
     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0
);

// Returns a threshold in [-0.5, 0.5) sampled from the selected Bayer matrix at this fragment's
// screen position, or 0.0 for DitherMode::None.
float ditherThreshold() {
    if (uDither == 1) {
        int x = int(mod(gl_FragCoord.x, 4.0));
        int y = int(mod(gl_FragCoord.y, 4.0));
        return (kBayer4x4[y * 4 + x] + 0.5) / 16.0 - 0.5;
    } else if (uDither == 2) {
        int x = int(mod(gl_FragCoord.x, 8.0));
        int y = int(mod(gl_FragCoord.y, 8.0));
        return (kBayer8x8[y * 8 + x] + 0.5) / 64.0 - 0.5;
    }
    return 0.0;
}

float quantizeChannel(float value, float levels) {
    float dithered = value + ditherThreshold() / (levels - 1.0);
    return floor(clamp(dithered, 0.0, 1.0) * (levels - 1.0) + 0.5) / (levels - 1.0);
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

    DitherMode DepthEffect::getDitherMode() const { return ditherMode_; }
    void DepthEffect::setDitherMode(DitherMode mode) { ditherMode_ = mode; }

    const std::string& DepthEffect::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.DepthEffect";
        return name;
    }

    void DepthEffect::OnApply()
    {
        ShaderEffect::OnApply();
        SetUniformInt("uMode", static_cast<int>(mode_));
        SetUniformInt("uDither", static_cast<int>(ditherMode_));
    }

    Effect* DepthEffect::Clone()
    {
        auto* clone = new DepthEffect(*device_);
        clone->setMode(mode_);
        clone->setDitherMode(ditherMode_);
        return clone;
    }

} // namespace CNA::Graphics

#endif // CNA_NOXNA
