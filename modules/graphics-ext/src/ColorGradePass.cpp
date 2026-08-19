// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

        // The strip lookup, and every term in it is about staying off the edges of a texel. A 3D
        // table sampled as a 2D strip has no filtering across slices -- the neighbouring slice is
        // half a table away in u -- so blue is interpolated by hand between two lookups while red
        // and green ride the sampler's own bilinear filtering inside a slice.
        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uLutSampler;
uniform float uLutSize;
uniform float uStrength;

vec3 cnaSampleSlice(vec3 colour, float slice) {
    float sliceWidth = 1.0 / uLutSize;              // one slice, in u
    float texelWidth = sliceWidth / uLutSize;       // one texel of that slice, in u
    // Half a texel in from each end: sampling the outermost texel centres is what keeps the first
    // and last entries of the table from being averaged with their neighbours.
    float u = slice * sliceWidth + 0.5 * texelWidth + colour.r * texelWidth * (uLutSize - 1.0);
    float v = 0.5 / uLutSize + colour.g * (uLutSize - 1.0) / uLutSize;
    return texture(uLutSampler, vec2(u, v)).rgb;
}

void main() {
    vec4 source = texture(texture1, TexCoord);
    vec3 colour = clamp(source.rgb, 0.0, 1.0);

    float blue = colour.b * (uLutSize - 1.0);
    float lower = floor(blue);
    float upper = min(lower + 1.0, uLutSize - 1.0);

    vec3 graded = mix(cnaSampleSlice(colour, lower),
                      cnaSampleSlice(colour, upper),
                      blue - lower);

    FragColor = vec4(mix(source.rgb, graded, uStrength), source.a);
}
)";

    } // namespace

    ColorGradePass::ColorGradePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "ColorGradePass", effect_.get(), logged);
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

        const int width  = size * size;
        auto texture = std::make_unique<Texture2D>(device, width, size);

        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * size);
        const float last = static_cast<float>(size - 1);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < width; ++x)
            {
                const int slice  = x / size;
                const int red    = x % size;
                const int green  = y;
                texels.emplace_back(static_cast<int>(static_cast<float>(red) / last * 255.0f + 0.5f),
                                    static_cast<int>(static_cast<float>(green) / last * 255.0f + 0.5f),
                                    static_cast<int>(static_cast<float>(slice) / last * 255.0f + 0.5f),
                                    255);
            }
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

    void ColorGradePass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float strength = settings != nullptr ? settings->getColorGradeStrength() : strength_;

        const bool ready = effect_ != nullptr && effect_->IsEffectValid() && lut_ != nullptr;
        if (!ready || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformInt("uLutSampler", 1);
        effect_->SetTexture(1, *lut_);
        effect_->SetUniformFloat("uLutSize", static_cast<float>(lutSize_));
        effect_->SetUniformFloat("uStrength", strength);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& ColorGradePass::getName() const
    {
        static const std::string name = "ColorGrade";
        return name;
    }

    bool ColorGradePass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
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

    float ColorGradePass::getStrength() const { return strength_; }
    void  ColorGradePass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
