// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/EngineException.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Texture3D;

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

        // MOD-2131. Both interpolations, written once and given to each layout with its own
        // `cnaLutFetch`. Every read here is a `texelFetch`: an exact entry, no filtering, no
        // half-texel arithmetic -- which is what makes the two comparable, since a difference
        // between them must then be the interpolation rather than the sampler.
        // Declared ahead of the fetch snippet, which uses `uLutSize` to address the strip. GLSL has
        // no forward declarations for uniforms, so a fetch written above them compiles to
        // "undeclared" -- and because a failed compile makes this pass copy its input through, the
        // symptom is an ungraded frame rather than an error.
        constexpr const char* kLutUniformsGlsl = R"(
uniform float uLutSize;
uniform int   uTetrahedral;
)";

        constexpr const char* kInterpolationGlsl = R"(
vec3 cnaLutTrilinear(vec3 colour) {
    float last = uLutSize - 1.0;
    vec3 p = clamp(colour, 0.0, 1.0) * last;
    ivec3 i0 = ivec3(floor(p));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(int(last)));
    vec3 f = p - vec3(i0);

    vec3 c000 = cnaLutFetch(ivec3(i0.x, i0.y, i0.z));
    vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
    vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
    vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
    vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
    vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
    vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
    vec3 c111 = cnaLutFetch(ivec3(i1.x, i1.y, i1.z));

    return mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
               mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
}

vec3 cnaLutTetrahedral(vec3 colour) {
    float last = uLutSize - 1.0;
    vec3 p = clamp(colour, 0.0, 1.0) * last;
    ivec3 i0 = ivec3(floor(p));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(int(last)));
    vec3 f = p - vec3(i0);

    // The cell's two neutral corners. A colour with fr == fg == fb lies on the edge between them,
    // and every branch below reduces to exactly that mix -- which is why a grey stays grey here and
    // does not in the trilinear form, where the six coloured corners still carry weight.
    vec3 c000 = cnaLutFetch(ivec3(i0.x, i0.y, i0.z));
    vec3 c111 = cnaLutFetch(ivec3(i1.x, i1.y, i1.z));

    if (f.x > f.y) {
        if (f.y > f.z) {
            vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
            vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
            return c000 + f.x * (c100 - c000) + f.y * (c110 - c100) + f.z * (c111 - c110);
        } else if (f.x > f.z) {
            vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
            vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
            return c000 + f.x * (c100 - c000) + f.z * (c101 - c100) + f.y * (c111 - c101);
        } else {
            vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
            vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
            return c000 + f.z * (c001 - c000) + f.x * (c101 - c001) + f.y * (c111 - c101);
        }
    } else {
        if (f.z > f.y) {
            vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
            vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
            return c000 + f.z * (c001 - c000) + f.y * (c011 - c001) + f.x * (c111 - c011);
        } else if (f.z > f.x) {
            vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
            vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
            return c000 + f.y * (c010 - c000) + f.z * (c011 - c010) + f.x * (c111 - c011);
        } else {
            vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
            vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
            return c000 + f.y * (c010 - c000) + f.x * (c110 - c010) + f.z * (c111 - c110);
        }
    }
}

vec3 cnaLutLookup(vec3 colour) {
    return uTetrahedral != 0 ? cnaLutTetrahedral(colour) : cnaLutTrilinear(colour);
}
)";

        // The strip, addressed as a grid. The slice index rides in x alongside the red index, which
        // is the whole strip layout in one line.
        constexpr const char* kStripFetchGlsl = R"(
uniform sampler2D uLutSampler;
vec3 cnaLutFetch(ivec3 index) {
    int slices = int(uLutSize);
    return texelFetch(uLutSampler, ivec2(index.z * slices + index.x, index.y), 0).rgb;
}
)";

        constexpr const char* kVolumeFetchGlsl = R"(
uniform sampler3D uLutVolume;
vec3 cnaLutFetch(ivec3 index) {
    return texelFetch(uLutVolume, index, 0).rgb;
}
)";

        constexpr const char* kLookupMainGlsl = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uStrength;

void main() {
    vec4 source = texture(texture1, TexCoord);
    vec3 graded = cnaLutLookup(clamp(source.rgb, 0.0, 1.0));
    FragColor = vec4(mix(source.rgb, graded, uStrength), source.a);
}
)";

        std::string BuildSource(const char* fetch)
        {
            // GLSL ES 3.00 has no default precision for sampler3D -- unlike sampler2D, which gets
            // one from the fragment stage. Stated here for both programs; it costs the strip
            // program nothing and is the difference between the volume program compiling and not.
            std::string source =
                "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
            source += kLutUniformsGlsl;
            source += fetch;
            source += kInterpolationGlsl;
            source += kLookupMainGlsl;
            return source;
        }

    } // namespace

    ColorGradePass::ColorGradePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        // MOD-2131: the tetrahedral path needs individual entries, which the strip's filtered
        // lookup cannot give it, so it is a second program rather than a branch in the first. The
        // original filtered strip shader stays the default and is untouched -- its output is what
        // every frame graded so far looks like.
        tetrahedralStripEffect_ =
            std::make_unique<ShaderEffect>(device, kVertexSource, BuildSource(kStripFetchGlsl));
        volumeEffect_ =
            std::make_unique<ShaderEffect>(device, kVertexSource, BuildSource(kVolumeFetchGlsl));
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
        if (useVolume)                   chosen = volumeEffect_.get();
        else if (useTetrahedralStrip)    chosen = tetrahedralStripEffect_.get();
        else if (lut_ != nullptr && effect_ != nullptr && effect_->IsEffectValid())
                                         chosen = effect_.get();

        if (chosen == nullptr || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        chosen->Apply();
        if (useVolume)
        {
            chosen->SetUniformInt("uLutVolume", 1);
            chosen->SetTexture(1, *volumeLut_);
            chosen->SetUniformFloat("uLutSize", static_cast<float>(volumeLutSize_));
        }
        else
        {
            chosen->SetUniformInt("uLutSampler", 1);
            chosen->SetTexture(1, *lut_);
            chosen->SetUniformFloat("uLutSize", static_cast<float>(lutSize_));
        }
        if (chosen != effect_.get())
            chosen->SetUniformInt("uTetrahedral",
                                  interpolation_ == LutInterpolation::Tetrahedral ? 1 : 0);
        chosen->SetUniformFloat("uStrength", strength);

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

    Texture3D* ColorGradePass::getVolumeLut() const { return volumeLut_; }

    void ColorGradePass::setVolumeLut(Texture3D* lut)
    {
        if (lut == nullptr)
        {
            volumeLut_ = nullptr;
            volumeLutSize_ = 0;
            return;
        }

        const int width  = lut->getWidthProperty();
        const int height = lut->getHeightProperty();
        const int depth  = lut->getDepthProperty();
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

    void ColorGradePass::setInterpolation(const LutInterpolation value) { interpolation_ = value; }

    float ColorGradePass::getStrength() const { return strength_; }
    void  ColorGradePass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
