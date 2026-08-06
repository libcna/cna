// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Magnum/MagnumStockShaders.hpp"

#include <iostream>

namespace CNA::Internal::Backends::Magnum
{
    namespace
    {
        /**
         * Stock programs share one optional per-instance world matrix input. Locations 12-15 take
         * the final four slots of the guaranteed 16-attribute floor, leaving the complete XNA
         * profile budget (12 per-vertex elements plus four matrix columns) available instead of
         * colliding with an otherwise-legal extended mesh declaration. `uCnaInstanced` keeps these
         * programs equivalent for ordinary draws: only an instanced draw enables the transform and
         * binds the four attributes.
         */
        constexpr const char* kInstanceTransformDeclaration = R"(
layout(location=12) in vec4 cnaInstanceCol0;
layout(location=13) in vec4 cnaInstanceCol1;
layout(location=14) in vec4 cnaInstanceCol2;
layout(location=15) in vec4 cnaInstanceCol3;
uniform float uCnaInstanced;
mat4 cnaInstanceMatrix(){ return mat4(cnaInstanceCol0, cnaInstanceCol1, cnaInstanceCol2, cnaInstanceCol3); }
vec4 cnaInstancePosition(vec4 p){ return (uCnaInstanced > 0.5) ? cnaInstanceMatrix() * p : p; }
vec3 cnaInstanceDirection(vec3 d){ return (uCnaInstanced > 0.5) ? mat3(cnaInstanceMatrix()) * d : d; }
)";

        /**
         * A render target's colour texture stores its image bottom-up, so a shader sampling one
         * mirrors V. The flag is per texture unit and supplied by the draw, not baked in, because
         * the same program samples ordinary textures too.
         */
        constexpr const char* kRenderTargetSampleDeclaration = R"(
uniform vec4 uRtFlipV;
vec2 cnaSampleUV(vec2 uv, float flip){ return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }
)";

        /**
         * FNA's stock-effect fog term. `uFogVector` bakes the third column of World*View, so
         * `dot(objectPosition, uFogVector)` is a true view-space Z fog factor; the varying carries
         * its inverse ("keep"), which is 1 -- a genuine no-op -- whenever fog is disabled and the
         * vector is all zero.
         */
        constexpr const char* kFogVertexTerm =
            "    vFogFactor = 1.0 - clamp(dot(cnaPosition, uFogVector), 0.0, 1.0);\n";

        /**
         * Alpha test, evaluated exactly as the interface documents: the pass/fail weights are
         * selected by an equality-with-tolerance test when a tolerance is given and by a plain
         * less-than otherwise, and a negative result discards. The default (0,0,1,1) always passes.
         */
        constexpr const char* kAlphaTestFragmentTerm = R"(
    float cnaAlphaTest = (uAlphaTest.y > 0.0)
        ? ((abs(fragColor.a - uAlphaTest.x) < uAlphaTest.y) ? uAlphaTest.z : uAlphaTest.w)
        : ((fragColor.a < uAlphaTest.x) ? uAlphaTest.z : uAlphaTest.w);
    if (cnaAlphaTest < 0.0) discard;
)";

        constexpr const char* kFogFragmentTerm =
            "    fragColor.rgb = mix(uFogColor, fragColor.rgb, vFogFactor);\n";

        bool ProgramHasColor(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::PositionColor
                || program == MagnumStockProgram::PositionColorTexture
                || program == MagnumStockProgram::DualTextureColored;
        }

        bool ProgramHasTexture(MagnumStockProgram program)
        {
            return program != MagnumStockProgram::PositionColor;
        }

        bool ProgramHasNormal(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::PositionNormalTexture;
        }

        bool ProgramIsDualTexture(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::DualTexture
                || program == MagnumStockProgram::DualTextureColored;
        }

        /// Shader location the texture coordinate occupies, which depends only on whether the
        /// layout also carries a colour or a normal ahead of it.
        int TexCoordLocation(MagnumStockProgram program)
        {
            return (ProgramHasColor(program) || ProgramHasNormal(program)) ? 2 : 1;
        }

        std::string BaseVertexShaderSource(MagnumStockProgram program)
        {
            std::string source = "#version 330 core\n";
            source += "layout(location=0) in vec3 aPosition;\n";
            if (ProgramHasColor(program))
                source += "layout(location=1) in vec4 aColor;\n";
            if (ProgramHasNormal(program))
                source += "layout(location=1) in vec3 aNormal;\n";
            if (ProgramHasTexture(program))
            {
                source += "layout(location=" + std::to_string(TexCoordLocation(program))
                        + ") in vec2 aTexCoord;\n";
            }

            source += kInstanceTransformDeclaration;
            source += "uniform mat4 uWVP;\n";
            source += "uniform mat4 uWorld;\n";
            source += "uniform mat3 uNormalMatrix;\n";
            source += "uniform vec4 uFogVector;\n";
            source += "out vec4 vColor;\n";
            source += "out vec2 vTexCoord;\n";
            source += "out vec3 vNormal;\n";
            source += "out vec3 vWorldPosition;\n";
            source += "out float vFogFactor;\n";
            source += "void main(){\n";
            source += "    vec4 cnaPosition = cnaInstancePosition(vec4(aPosition, 1.0));\n";
            source += "    gl_Position = uWVP * cnaPosition;\n";
            source += ProgramHasColor(program) ? "    vColor = aColor;\n"
                                               : "    vColor = vec4(1.0);\n";
            source += ProgramHasTexture(program) ? "    vTexCoord = aTexCoord;\n"
                                                 : "    vTexCoord = vec2(0.0);\n";
            source += ProgramHasNormal(program)
                ? "    vNormal = uNormalMatrix * cnaInstanceDirection(aNormal);\n"
                : "    vNormal = vec3(0.0, 0.0, 1.0);\n";
            source += "    vWorldPosition = (uWorld * cnaPosition).xyz;\n";
            source += kFogVertexTerm;
            source += "}\n";
            return source;
        }

        std::string BaseFragmentShaderSource(MagnumStockProgram program)
        {
            std::string source = "#version 330 core\n";
            source += "in vec4 vColor;\n";
            source += "in vec2 vTexCoord;\n";
            source += "in vec3 vNormal;\n";
            source += "in vec3 vWorldPosition;\n";
            source += "in float vFogFactor;\n";
            source += "uniform sampler2D uTexture;\n";
            source += "uniform vec4 uDiffuseColor;\n";
            source += "uniform vec3 uAmbientColor;\n";
            source += "uniform vec3 uEmissiveColor;\n";
            source += "uniform vec3 uLight0Dir;\n";
            source += "uniform vec3 uLight0Diffuse;\n";
            source += "uniform vec3 uLight0Specular;\n";
            source += "uniform vec3 uLight1Dir;\n";
            source += "uniform vec3 uLight1Diffuse;\n";
            source += "uniform vec3 uLight1Specular;\n";
            source += "uniform vec3 uLight2Dir;\n";
            source += "uniform vec3 uLight2Diffuse;\n";
            source += "uniform vec3 uLight2Specular;\n";
            source += "uniform vec3 uSpecularColor;\n";
            source += "uniform float uSpecularPower;\n";
            source += "uniform vec3 uEyePosition;\n";
            source += "uniform vec4 uAlphaTest;\n";
            source += "uniform vec3 uFogColor;\n";
            source += "uniform float uVertexColorEnabled;\n";
            source += "uniform float uTextureEnabled;\n";
            source += "uniform float uLightingEnabled;\n";
            source += kRenderTargetSampleDeclaration;
            source += "out vec4 fragColor;\n";
            source += "void main(){\n";
            source += "    vec4 material = uDiffuseColor;\n";
            source += "    if (uVertexColorEnabled > 0.5) material *= vColor;\n";
            if (ProgramHasTexture(program))
            {
                source += "    if (uTextureEnabled > 0.5)\n";
                source += "        material *= texture(uTexture, cnaSampleUV(vTexCoord, uRtFlipV.x));\n";
            }
            source += "    fragColor = material;\n";

            // Lighting stays a uniform gate rather than a separate program: the same layout is drawn
            // both lit and unlit by ordinary XNA content (BasicEffect.LightingEnabled is a per-draw
            // property), and a gate keeps one compiled program serving both instead of doubling the
            // stock program count for a branch every driver folds away on a uniform.
            source += R"(
    if (uLightingEnabled > 0.5) {
        vec3 normal = normalize(vNormal);
        vec3 eye = normalize(uEyePosition - vWorldPosition);
        float dot0 = dot(normal, -uLight0Dir);
        float dot1 = dot(normal, -uLight1Dir);
        float dot2 = dot(normal, -uLight2Dir);
        vec3 lightSum = uAmbientColor
            + uLight0Diffuse * max(dot0, 0.0)
            + uLight1Diffuse * max(dot1, 0.0)
            + uLight2Diffuse * max(dot2, 0.0);
        vec3 half0 = normalize(eye - uLight0Dir);
        vec3 half1 = normalize(eye - uLight1Dir);
        vec3 half2 = normalize(eye - uLight2Dir);
        vec3 specular = (pow(max(dot(half0, normal), 0.0) * step(0.0, dot0), uSpecularPower) * uLight0Specular
                       + pow(max(dot(half1, normal), 0.0) * step(0.0, dot1), uSpecularPower) * uLight1Specular
                       + pow(max(dot(half2, normal), 0.0) * step(0.0, dot2), uSpecularPower) * uLight2Specular)
                       * uSpecularColor;
        fragColor.rgb = fragColor.rgb * lightSum + uEmissiveColor;
        fragColor.rgb += specular * fragColor.a;
    }
)";
            source += kAlphaTestFragmentTerm;
            source += kFogFragmentTerm;
            source += "}\n";
            return source;
        }

        std::string DualTextureFragmentShaderSource(MagnumStockProgram program)
        {
            std::string source = "#version 330 core\n";
            source += "in vec4 vColor;\n";
            source += "in vec2 vTexCoord;\n";
            source += "in float vFogFactor;\n";
            source += "uniform sampler2D uTexture;\n";
            source += "uniform sampler2D uTexture2;\n";
            source += "uniform vec4 uDiffuseColor;\n";
            source += "uniform vec4 uAlphaTest;\n";
            source += "uniform vec3 uFogColor;\n";
            source += "uniform float uVertexColorEnabled;\n";
            source += kRenderTargetSampleDeclaration;
            source += "out vec4 fragColor;\n";
            source += "void main(){\n";
            // The x2 on the base layer is DualTextureEffect's own overbright convention: the second
            // layer is a modulate-2x lightmap, so a 0.5 texel is neutral rather than a halving.
            source += "    vec4 base = texture(uTexture, cnaSampleUV(vTexCoord, uRtFlipV.x));\n";
            source += "    base.rgb *= 2.0;\n";
            source += "    vec4 overlay = texture(uTexture2, cnaSampleUV(vTexCoord, uRtFlipV.y));\n";
            if (ProgramHasColor(program))
            {
                source += "    vec4 tint = (uVertexColorEnabled > 0.5) ? vColor : vec4(1.0);\n";
                source += "    fragColor = base * overlay * tint * uDiffuseColor;\n";
            }
            else
            {
                source += "    fragColor = base * overlay * uDiffuseColor;\n";
            }
            source += kAlphaTestFragmentTerm;
            source += kFogFragmentTerm;
            source += "}\n";
            return source;
        }
    }

    bool SelectStockProgram(const MagnumStockSelector& selector, MagnumStockProgram& programOut)
    {
        if (selector.dualTexture)
        {
            // Stride 24 carries a vertex colour the two-layer result must be tinted by; stride 20
            // has none, so it keeps the colour-free program rather than reading an absent input.
            if (selector.strideInBytes == 24)
            {
                programOut = MagnumStockProgram::DualTextureColored;
                return true;
            }
            if (selector.strideInBytes == 20)
            {
                programOut = MagnumStockProgram::DualTexture;
                return true;
            }
            return false;
        }

        switch (selector.strideInBytes)
        {
            case 16: programOut = MagnumStockProgram::PositionColor;         return true;
            case 20: programOut = MagnumStockProgram::PositionTexture;       return true;
            case 24: programOut = MagnumStockProgram::PositionColorTexture;  return true;
            case 32: programOut = MagnumStockProgram::PositionNormalTexture; return true;
            default: return false;
        }
    }

    std::string StockVertexShaderSource(MagnumStockProgram program)
    {
        return BaseVertexShaderSource(program);
    }

    std::string StockFragmentShaderSource(MagnumStockProgram program)
    {
        if (ProgramIsDualTexture(program))
            return DualTextureFragmentShaderSource(program);
        return BaseFragmentShaderSource(program);
    }

    std::string SpriteVertexShaderSource()
    {
        return R"(#version 330 core
layout(location=0) in vec2 aPosition;
layout(location=1) in vec2 aTexCoord;
layout(location=2) in vec4 aColor;

uniform mat4 projection;

out vec2 vTexCoord;
out vec4 vColor;

void main(){
    gl_Position = projection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";
    }

    std::string SpriteFragmentShaderSource()
    {
        return R"(#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D texture1;

out vec4 fragColor;

void main(){
    fragColor = texture(texture1, vTexCoord) * vColor;
}
)";
    }

    MagnumProgram* MagnumStockShaderCache::ForProgram(MagnumStockProgram program)
    {
        const int key = static_cast<int>(program);
        const auto existing = programs_.find(key);
        if (existing != programs_.end())
            return existing->second->IsValid() ? existing->second.get() : nullptr;

        auto compiled = std::make_unique<MagnumProgram>();
        if (!compiled->CompileAndLink(StockVertexShaderSource(program),
                                      StockFragmentShaderSource(program)))
        {
            std::cerr << "CNA: Magnum stock shader (program " << key << ") failed to build:\n"
                      << compiled->GetLog() << std::endl;
        }
        MagnumProgram* raw = compiled.get();
        programs_.emplace(key, std::move(compiled));
        return raw->IsValid() ? raw : nullptr;
    }

    MagnumProgram* MagnumStockShaderCache::Sprite()
    {
        if (!sprite_)
        {
            sprite_ = std::make_unique<MagnumProgram>();
            if (!sprite_->CompileAndLink(SpriteVertexShaderSource(), SpriteFragmentShaderSource()))
            {
                std::cerr << "CNA: Magnum SpriteBatch shader failed to build:\n"
                          << sprite_->GetLog() << std::endl;
            }
        }
        return sprite_->IsValid() ? sprite_.get() : nullptr;
    }
}
