// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumStockShaders.hpp"

#include <iostream>

namespace CNA::Internal::Renderers::Magnum
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

        /**
         * The three directional lights, declared by whichever stage evaluates them. XNA picks
         * between a per-vertex-lit and a per-pixel-lit shader from `PreferPerPixelLighting`, so the
         * two families here differ only in which stage these uniforms and the function below live
         * in -- the arithmetic is byte-identical, and any difference in the result is exactly the
         * interpolation difference the flag selects.
         *
         * @param withAmbient False for `SkinnedEffect`, which folds its ambient term into
         *                    `EmissiveColor` before the draw and so carries no separate one here.
         */
        std::string LightingUniformDeclarations(bool withAmbient)
        {
            std::string source;
            if (withAmbient)
                source += "uniform vec3 uAmbientColor;\n";
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
            return source;
        }

        /** @see LightingUniformDeclarations for why one function serves both families. */
        std::string LightingFunctionSource(bool withAmbient)
        {
            std::string source = R"(
void cnaLighting(vec3 rawNormal, vec3 worldPosition, out vec3 lightSum, out vec3 specular){
    vec3 normal = normalize(rawNormal);
    vec3 eye = normalize(uEyePosition - worldPosition);
    float dot0 = dot(normal, -uLight0Dir);
    float dot1 = dot(normal, -uLight1Dir);
    float dot2 = dot(normal, -uLight2Dir);
    lightSum = )";
            source += withAmbient ? "uAmbientColor\n        + " : "";
            source += R"(uLight0Diffuse * max(dot0, 0.0)
        + uLight1Diffuse * max(dot1, 0.0)
        + uLight2Diffuse * max(dot2, 0.0);
    vec3 half0 = normalize(eye - uLight0Dir);
    vec3 half1 = normalize(eye - uLight1Dir);
    vec3 half2 = normalize(eye - uLight2Dir);
    specular = (pow(max(dot(half0, normal), 0.0) * step(0.0, dot0), uSpecularPower) * uLight0Specular
              + pow(max(dot(half1, normal), 0.0) * step(0.0, dot1), uSpecularPower) * uLight1Specular
              + pow(max(dot(half2, normal), 0.0) * step(0.0, dot2), uSpecularPower) * uLight2Specular)
              * uSpecularColor;
}
)";
            return source;
        }

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

        bool ProgramIsVertexLit(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::PositionNormalTextureVertexLit
                || program == MagnumStockProgram::SkinnedVertexLit;
        }

        bool ProgramIsSkinned(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::Skinned
                || program == MagnumStockProgram::SkinnedVertexLit;
        }

        bool ProgramHasNormal(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::PositionNormalTexture
                || program == MagnumStockProgram::PositionNormalTextureVertexLit
                || program == MagnumStockProgram::EnvironmentMap
                || ProgramIsSkinned(program);
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
            if (ProgramIsVertexLit(program))
            {
                source += LightingUniformDeclarations(true);
                source += "out vec3 vLightSum;\n";
                source += "out vec3 vSpecular;\n";
                source += LightingFunctionSource(true);
            }
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
            if (ProgramIsVertexLit(program))
                source += "    cnaLighting(vNormal, vWorldPosition, vLightSum, vSpecular);\n";
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
            source += "uniform vec3 uEmissiveColor;\n";
            if (ProgramIsVertexLit(program))
            {
                source += "in vec3 vLightSum;\n";
                source += "in vec3 vSpecular;\n";
            }
            else
            {
                source += LightingUniformDeclarations(true);
                source += LightingFunctionSource(true);
            }
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
            source += "    if (uLightingEnabled > 0.5) {\n";
            source += "        vec3 lightSum;\n";
            source += "        vec3 specular;\n";
            // The only difference between the two families: one interpolates the light result the
            // vertex stage already computed, the other evaluates the same function here.
            source += ProgramIsVertexLit(program)
                ? "        lightSum = vLightSum; specular = vSpecular;\n"
                : "        cnaLighting(vNormal, vWorldPosition, lightSum, specular);\n";
            source += "        fragColor.rgb = fragColor.rgb * lightSum + uEmissiveColor;\n";
            source += "        fragColor.rgb += specular * fragColor.a;\n";
            source += "    }\n";
            source += kAlphaTestFragmentTerm;
            source += kFogFragmentTerm;
            source += "}\n";
            return source;
        }

        std::string SkinnedVertexShaderSource(MagnumStockProgram program)
        {
            const bool vertexLit = ProgramIsVertexLit(program);
            std::string source = "#version 330 core\n";
            source += "layout(location=0) in vec3 aPosition;\n";
            source += "layout(location=1) in vec3 aNormal;\n";
            source += "layout(location=2) in vec2 aTexCoord;\n";
            source += "layout(location=3) in vec4 aBoneWeights;\n";
            source += "layout(location=4) in uvec4 aBoneIndices;\n";
            // Location 5 belongs to the stride-56 layout's appended per-vertex colour. A stride-52
            // draw leaves it unbound, which is why the colour is APPENDED there rather than
            // inserted: locations 0..4 stay byte-identical and one program serves both strides.
            source += "layout(location=5) in vec4 aColor;\n";
            source += kInstanceTransformDeclaration;
            source += "uniform mat4 uWVP;\n";
            source += "uniform mat4 uWorld;\n";
            source += "uniform mat3 uNormalMatrix;\n";
            source += "uniform mat4 uBones[" + std::to_string(kMagnumMaxBones) + "];\n";
            source += "uniform int uWeightsPerVertex;\n";
            source += "uniform vec4 uFogVector;\n";
            source += "out vec4 vColor;\n";
            source += "out vec2 vTexCoord;\n";
            source += "out vec3 vNormal;\n";
            source += "out vec3 vWorldPosition;\n";
            source += "out float vFogFactor;\n";
            if (vertexLit)
            {
                source += LightingUniformDeclarations(false);
                source += "out vec3 vLightSum;\n";
                source += "out vec3 vSpecular;\n";
                source += LightingFunctionSource(false);
            }
            source += "void main(){\n";
            // Only the first uWeightsPerVertex pairs are summed, matching XNA's own Skin(vin, n):
            // a mesh authored for one or two bones per vertex leaves the remaining weights
            // undefined rather than zeroed, so summing all four would corrupt it.
            source += "    mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x;\n";
            source += "    if (uWeightsPerVertex >= 2) skin += uBones[aBoneIndices.y] * aBoneWeights.y;\n";
            source += "    if (uWeightsPerVertex >= 4)\n";
            source += "        skin += uBones[aBoneIndices.z] * aBoneWeights.z\n";
            source += "              + uBones[aBoneIndices.w] * aBoneWeights.w;\n";
            source += "    vec4 cnaPosition = cnaInstancePosition(skin * vec4(aPosition, 1.0));\n";
            source += "    gl_Position = uWVP * cnaPosition;\n";
            // A degenerate palette (all-zero bones, or weights summing to zero) collapses the
            // skinned normal to nothing; falling back to the unskinned one keeps such a vertex lit
            // rather than black.
            source += "    vec3 skinnedNormal = mat3(skin) * aNormal;\n";
            source += "    float skinnedNormalLength = length(skinnedNormal);\n";
            source += "    vec3 boneNormal = (skinnedNormalLength > 1e-6)\n";
            source += "        ? (skinnedNormal / skinnedNormalLength) : aNormal;\n";
            source += "    vNormal = uNormalMatrix * cnaInstanceDirection(boneNormal);\n";
            source += "    vTexCoord = aTexCoord;\n";
            source += "    vWorldPosition = (uWorld * cnaPosition).xyz;\n";
            source += "    vColor = aColor;\n";
            if (vertexLit)
                source += "    cnaLighting(vNormal, vWorldPosition, vLightSum, vSpecular);\n";
            source += kFogVertexTerm;
            source += "}\n";
            return source;
        }

        bool ProgramIsPbr(MagnumStockProgram program)
        {
            return program == MagnumStockProgram::Pbr
                || program == MagnumStockProgram::PbrSkinned;
        }

        std::string PbrVertexShaderSource(MagnumStockProgram program)
        {
            const bool skinned = program == MagnumStockProgram::PbrSkinned;
            std::string source = "#version 330 core\n";
            source += "layout(location=0) in vec3 aPosition;\n";
            source += "layout(location=1) in vec3 aNormal;\n";
            // The tangent's w carries the bitangent handedness, which is why glTF stores a float4.
            source += "layout(location=2) in vec4 aTangent;\n";
            source += "layout(location=3) in vec2 aTexCoord;\n";
            if (skinned)
            {
                source += "layout(location=4) in vec4 aBoneWeights;\n";
                source += "layout(location=5) in uvec4 aBoneIndices;\n";
            }
            source += kInstanceTransformDeclaration;
            source += "uniform mat4 uWVP;\n";
            source += "uniform mat4 uWorld;\n";
            source += "uniform mat3 uNormalMatrix;\n";
            source += "uniform vec4 uFogVector;\n";
            if (skinned)
            {
                source += "uniform mat4 uBones[" + std::to_string(kMagnumMaxBones) + "];\n";
                source += "uniform int uWeightsPerVertex;\n";
                source += "vec3 cnaSkinNormal(mat3 m,vec3 n){\n";
                source += "    vec3 c0=m[0],c1=m[1],c2=m[2];\n";
                source += "    vec3 co0=cross(c1,c2),co1=cross(c2,c0),co2=cross(c0,c1);\n";
                source += "    float det=dot(c0,co0);\n";
                source += "    vec3 transformed=mat3(co0,co1,co2)*n;\n";
                source += "    return (abs(det)>1e-6)?transformed*sign(det):m*n;\n";
                source += "}\n";
            }
            source += "out vec3 vNormal;\n";
            source += "out vec3 vTangent;\n";
            source += "out float vBitangentSign;\n";
            source += "out vec2 vTexCoord;\n";
            source += "out vec3 vWorldPosition;\n";
            source += "out float vFogFactor;\n";
            source += "void main(){\n";
            if (skinned)
            {
                // The same partial weight sum SkinnedEffect's own program uses -- see its comment.
                source += "    mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x;\n";
                source += "    if (uWeightsPerVertex >= 2) skin += uBones[aBoneIndices.y] * aBoneWeights.y;\n";
                source += "    if (uWeightsPerVertex >= 4)\n";
                source += "        skin += uBones[aBoneIndices.z] * aBoneWeights.z\n";
                source += "              + uBones[aBoneIndices.w] * aBoneWeights.w;\n";
                source += "    vec4 cnaPosition = cnaInstancePosition(skin * vec4(aPosition, 1.0));\n";
                // The tangent frame is skinned too: leaving it in bind pose would light a deformed
                // surface with the normal map of an undeformed one.
                source += "    vec3 skinnedNormal = cnaSkinNormal(mat3(skin), aNormal);\n";
                source += "    float skinnedNormalLength = length(skinnedNormal);\n";
                source += "    vec3 boneNormal = (skinnedNormalLength > 1e-6)\n";
                source += "        ? (skinnedNormal / skinnedNormalLength) : aNormal;\n";
                source += "    vec3 boneTangent = mat3(skin) * aTangent.xyz;\n";
            }
            else
            {
                source += "    vec4 cnaPosition = cnaInstancePosition(vec4(aPosition, 1.0));\n";
                source += "    vec3 boneNormal = aNormal;\n";
                source += "    vec3 boneTangent = aTangent.xyz;\n";
            }
            source += "    gl_Position = uWVP * cnaPosition;\n";
            source += "    vNormal = uNormalMatrix * cnaInstanceDirection(boneNormal);\n";
            source += "    vTangent = mat3(uWorld) * cnaInstanceDirection(boneTangent);\n";
            source += "    vBitangentSign = aTangent.w;\n";
            source += "    vTexCoord = aTexCoord;\n";
            source += "    vWorldPosition = (uWorld * cnaPosition).xyz;\n";
            source += kFogVertexTerm;
            source += "}\n";
            return source;
        }

        std::string PbrFragmentShaderSource()
        {
            std::string source = "#version 330 core\n";
            source += "in vec3 vNormal;\n";
            source += "in vec3 vTangent;\n";
            source += "in float vBitangentSign;\n";
            source += "in vec2 vTexCoord;\n";
            source += "in vec3 vWorldPosition;\n";
            source += "in float vFogFactor;\n";
            source += "uniform sampler2D uTexture;\n";
            source += "uniform sampler2D uNormalMap;\n";
            source += "uniform sampler2D uMetallicRoughnessMap;\n";
            source += "uniform sampler2D uEmissiveMap;\n";
            source += "uniform sampler2D uOcclusionMap;\n";
            source += "uniform vec4 uDiffuseColor;\n";
            source += "uniform vec3 uAmbientColor;\n";
            source += "uniform vec3 uEmissiveColor;\n";
            source += "uniform float uMetallicFactor;\n";
            source += "uniform float uRoughnessFactor;\n";
            source += "uniform float uNormalScale;\n";
            source += "uniform float uOcclusionStrength;\n";
            source += "uniform vec3 uLight0Dir;\n";
            source += "uniform vec3 uLight0Diffuse;\n";
            source += "uniform vec3 uLight1Dir;\n";
            source += "uniform vec3 uLight1Diffuse;\n";
            source += "uniform vec3 uLight2Dir;\n";
            source += "uniform vec3 uLight2Diffuse;\n";
            source += "uniform vec3 uEyePosition;\n";
            source += "uniform vec4 uAlphaTest;\n";
            source += "uniform vec3 uFogColor;\n";
            // Unit 4 (the occlusion map) needs a fifth flag, which does not fit in uRtFlipV's four
            // components -- this is the only program that samples that far.
            source += "uniform vec4 uRtFlipVHi;\n";
            source += kRenderTargetSampleDeclaration;
            source += "out vec4 fragColor;\n";
            // The glTF metallic-roughness BRDF: GGX distribution, Smith-Schlick geometry and a
            // Schlick Fresnel, with the diffuse lobe scaled away as the surface becomes metallic.
            source += R"(
vec3 cnaPbrLight(vec3 normal, vec3 view, vec3 light, vec3 lightColor,
                 vec3 albedo, vec3 f0, float roughness, float metallic){
    vec3 halfway = normalize(view + light);
    float nDotL = max(dot(normal, light), 0.0);
    float nDotV = max(dot(normal, view), 1e-4);
    float nDotH = max(dot(normal, halfway), 0.0);
    float vDotH = max(dot(view, halfway), 0.0);
    float alphaSquared = pow(roughness, 4.0);
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    float distribution = alphaSquared / (3.14159265 * denominator * denominator + 1e-7);
    float k = (roughness + 1.0);
    k = k * k / 8.0;
    float geometry = (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
    vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 1e-4);
    vec3 diffuse = albedo * (1.0 - metallic);
    return ((vec3(1.0) - fresnel) * diffuse / 3.14159265 + specular) * lightColor * nDotL;
}
)";
            source += "void main(){\n";
            source += "    vec4 baseColor = texture(uTexture, cnaSampleUV(vTexCoord, uRtFlipV.x));\n";
            source += "    vec3 albedo = baseColor.rgb * uDiffuseColor.rgb;\n";
            source += "    float alpha = baseColor.a * uDiffuseColor.a;\n";
            source += "    vec3 normal = normalize(vNormal);\n";
            // Gram-Schmidt: the interpolated tangent is no longer exactly perpendicular to the
            // interpolated normal, so it is re-orthogonalized before the basis is built.
            source += "    vec3 tangent = normalize(vTangent - normal * dot(normal, vTangent));\n";
            source += "    vec3 bitangent = cross(normal, tangent) * vBitangentSign;\n";
            source += "    mat3 tangentBasis = mat3(tangent, bitangent, normal);\n";
            source += "    vec3 sampledNormal =\n";
            source += "        texture(uNormalMap, cnaSampleUV(vTexCoord, uRtFlipV.y)).rgb * 2.0 - 1.0;\n";
            source += "    sampledNormal.xy *= uNormalScale;\n";
            source += "    vec3 shadingNormal = normalize(tangentBasis * sampledNormal);\n";
            source += "    vec4 metallicRoughness =\n";
            source += "        texture(uMetallicRoughnessMap, cnaSampleUV(vTexCoord, uRtFlipV.z));\n";
            // glTF's own packing: green is roughness, blue is metallic. The roughness floor keeps
            // the specular lobe from collapsing into a numerically undefined mirror.
            source += "    float roughness = clamp(metallicRoughness.g * uRoughnessFactor, 0.045, 1.0);\n";
            source += "    float metallic = clamp(metallicRoughness.b * uMetallicFactor, 0.0, 1.0);\n";
            source += "    vec3 view = normalize(uEyePosition - vWorldPosition);\n";
            source += "    vec3 f0 = mix(vec3(0.04), albedo, metallic);\n";
            source += "    vec3 reflected = cnaPbrLight(shadingNormal, view, normalize(-uLight0Dir),\n";
            source += "                                 uLight0Diffuse, albedo, f0, roughness, metallic)\n";
            source += "                   + cnaPbrLight(shadingNormal, view, normalize(-uLight1Dir),\n";
            source += "                                 uLight1Diffuse, albedo, f0, roughness, metallic)\n";
            source += "                   + cnaPbrLight(shadingNormal, view, normalize(-uLight2Dir),\n";
            source += "                                 uLight2Diffuse, albedo, f0, roughness, metallic);\n";
            source += "    float occlusionSample = texture(uOcclusionMap, cnaSampleUV(vTexCoord, uRtFlipVHi.x)).r;\n";
            source += "    float occlusion = 1.0 + uOcclusionStrength * (occlusionSample - 1.0);\n";
            source += "    vec3 ambient = uAmbientColor * albedo * occlusion;\n";
            source += "    vec3 emissive =\n";
            source += "        uEmissiveColor * texture(uEmissiveMap, cnaSampleUV(vTexCoord, uRtFlipV.w)).rgb;\n";
            source += "    fragColor = vec4(ambient + reflected + emissive, alpha);\n";
            source += kAlphaTestFragmentTerm;
            source += kFogFragmentTerm;
            source += "}\n";
            return source;
        }

        std::string SkinnedFragmentShaderSource(MagnumStockProgram program)
        {
            const bool vertexLit = ProgramIsVertexLit(program);
            std::string source = "#version 330 core\n";
            source += "in vec4 vColor;\n";
            source += "in vec2 vTexCoord;\n";
            source += "in vec3 vNormal;\n";
            source += "in vec3 vWorldPosition;\n";
            source += "in float vFogFactor;\n";
            source += "uniform sampler2D uTexture;\n";
            source += "uniform vec4 uDiffuseColor;\n";
            source += "uniform vec3 uEmissiveColor;\n";
            if (vertexLit)
            {
                source += "in vec3 vLightSum;\n";
                source += "in vec3 vSpecular;\n";
            }
            else
            {
                // SkinnedEffect folds its ambient term into EmissiveColor before the draw, so
                // unlike BasicEffect's stage this light sum carries no separate ambient of its own.
                source += LightingUniformDeclarations(false);
                source += LightingFunctionSource(false);
            }
            source += "uniform vec4 uAlphaTest;\n";
            source += "uniform vec3 uFogColor;\n";
            source += "uniform float uVertexColorEnabled;\n";
            source += kRenderTargetSampleDeclaration;
            source += "out vec4 fragColor;\n";
            source += "void main(){\n";
            source += "    vec3 lightSum;\n";
            source += "    vec3 specular;\n";
            source += vertexLit
                ? "    lightSum = vLightSum; specular = vSpecular;\n"
                : "    cnaLighting(vNormal, vWorldPosition, lightSum, specular);\n";
            source += "    vec3 litColor = lightSum * uDiffuseColor.rgb + uEmissiveColor;\n";
            source += "    vec4 diffuse = texture(uTexture, cnaSampleUV(vTexCoord, uRtFlipV.x));\n";
            source += "    vec4 tint = (uVertexColorEnabled > 0.5) ? vColor : vec4(1.0);\n";
            // The vertex colour multiplies AFTER the specular highlight is added, so a tinted skin
            // tints its highlight too rather than leaving a white one on a coloured surface.
            source += "    fragColor = vec4(litColor * diffuse.rgb, uDiffuseColor.a * diffuse.a * tint.a);\n";
            source += "    fragColor.rgb += specular * fragColor.a;\n";
            source += "    fragColor.rgb *= tint.rgb;\n";
            source += kAlphaTestFragmentTerm;
            source += kFogFragmentTerm;
            source += "}\n";
            return source;
        }

        std::string EnvironmentMapVertexShaderSource()
        {
            std::string source = "#version 330 core\n";
            source += "layout(location=0) in vec3 aPosition;\n";
            source += "layout(location=1) in vec3 aNormal;\n";
            source += "layout(location=2) in vec2 aTexCoord;\n";
            source += kInstanceTransformDeclaration;
            source += "uniform mat4 uWVP;\n";
            source += "uniform mat4 uWorld;\n";
            source += "uniform mat3 uNormalMatrix;\n";
            source += "uniform vec3 uEyePosition;\n";
            source += "uniform vec4 uFogVector;\n";
            source += "uniform float uEnvMapAmount;\n";
            source += "uniform float uFresnelEnabled;\n";
            source += "uniform float uFresnelFactor;\n";
            source += "out vec3 vWorldNormal;\n";
            source += "out vec3 vEyeDirection;\n";
            source += "out vec2 vTexCoord;\n";
            source += "out float vFogFactor;\n";
            source += "out float vEnvMapBlend;\n";
            source += "void main(){\n";
            source += "    vec4 cnaPosition = cnaInstancePosition(vec4(aPosition, 1.0));\n";
            source += "    gl_Position = uWVP * cnaPosition;\n";
            source += "    vec3 worldPosition = (uWorld * cnaPosition).xyz;\n";
            source += "    vWorldNormal = normalize(uNormalMatrix * cnaInstanceDirection(aNormal));\n";
            source += "    vEyeDirection = normalize(uEyePosition - worldPosition);\n";
            source += "    vTexCoord = aTexCoord;\n";
            // The Fresnel weighting is a per-vertex term in FNA's own stock effect, so it is
            // computed here and interpolated rather than re-derived per fragment.
            source += "    float viewAngle = dot(vEyeDirection, vWorldNormal);\n";
            source += "    vEnvMapBlend = (uFresnelEnabled > 0.5)\n";
            source += "        ? pow(max(1.0 - abs(viewAngle), 0.0), uFresnelFactor) * uEnvMapAmount\n";
            source += "        : uEnvMapAmount;\n";
            source += kFogVertexTerm;
            source += "}\n";
            return source;
        }

        std::string EnvironmentMapFragmentShaderSource()
        {
            std::string source = "#version 330 core\n";
            source += "in vec3 vWorldNormal;\n";
            source += "in vec3 vEyeDirection;\n";
            source += "in vec2 vTexCoord;\n";
            source += "in float vFogFactor;\n";
            source += "in float vEnvMapBlend;\n";
            source += "uniform sampler2D uTexture;\n";
            source += "uniform samplerCube uEnvMap;\n";
            source += "uniform vec4 uDiffuseColor;\n";
            source += "uniform vec3 uEmissiveColor;\n";
            source += "uniform vec3 uLight0Dir;\n";
            source += "uniform vec3 uLight0Diffuse;\n";
            source += "uniform vec3 uLight1Dir;\n";
            source += "uniform vec3 uLight1Diffuse;\n";
            source += "uniform vec3 uLight2Dir;\n";
            source += "uniform vec3 uLight2Diffuse;\n";
            source += "uniform vec3 uEnvMapSpecular;\n";
            source += "uniform vec4 uAlphaTest;\n";
            source += "uniform vec3 uFogColor;\n";
            source += kRenderTargetSampleDeclaration;
            source += "out vec4 fragColor;\n";
            source += "void main(){\n";
            source += "    vec3 normal = normalize(vWorldNormal);\n";
            source += "    vec3 eye = normalize(vEyeDirection);\n";
            // EnvironmentMapEffect has no ambient or specular light terms of its own -- the
            // reflection replaces them -- so this is the diffuse sum alone, not BasicEffect's.
            source += "    vec3 lightSum = uLight0Diffuse * max(dot(normal, -uLight0Dir), 0.0)\n";
            source += "                  + uLight1Diffuse * max(dot(normal, -uLight1Dir), 0.0)\n";
            source += "                  + uLight2Diffuse * max(dot(normal, -uLight2Dir), 0.0);\n";
            source += "    vec3 litColor = lightSum * uDiffuseColor.rgb + uEmissiveColor;\n";
            source += "    vec4 diffuse = texture(uTexture, cnaSampleUV(vTexCoord, uRtFlipV.x));\n";
            source += "    vec4 reflection = texture(uEnvMap, reflect(-eye, normal));\n";
            source += "    float alpha = uDiffuseColor.a * diffuse.a;\n";
            source += "    vec3 rgb = mix(litColor * diffuse.rgb, reflection.rgb * alpha, vEnvMapBlend)\n";
            source += "             + uEnvMapSpecular * reflection.a * alpha;\n";
            source += "    fragColor = vec4(rgb, alpha);\n";
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
        // PBR is resolved first, and its skinned combination before its plain one: the two share
        // the same material model and differ only by whether a bone palette deforms the tangent
        // frame, so `pbr && skinned` is one program rather than a choice between two.
        if (selector.pbr)
        {
            if (selector.skinned)
            {
                if (selector.strideInBytes != 68)
                    return false;
                programOut = MagnumStockProgram::PbrSkinned;
                return true;
            }
            if (selector.strideInBytes != 48)
                return false;
            programOut = MagnumStockProgram::Pbr;
            return true;
        }

        // Skinning is the most specific request of all: it is the only flag whose layout carries a
        // bone palette, so it is resolved before anything that could share a stride with it.
        if (selector.skinned)
        {
            if (selector.strideInBytes != 52 && selector.strideInBytes != 56)
                return false;
            programOut = selector.vertexLighting ? MagnumStockProgram::SkinnedVertexLit
                                                 : MagnumStockProgram::Skinned;
            return true;
        }

        // Env mapping is tested ahead of dual texturing because it is the more specific request:
        // both flags name a program over a DIFFERENT layout (32 vs 20/24), so a draw carrying both
        // can only mean the one whose layout it actually supplies.
        if (selector.envMapping)
        {
            if (selector.strideInBytes != 32)
                return false;
            programOut = MagnumStockProgram::EnvironmentMap;
            return true;
        }

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
            // Stride 32 is the only built-in layout carrying a normal, so it is the only one
            // where PreferPerPixelLighting names two different programs rather than one.
            case 32:
                programOut = selector.vertexLighting
                    ? MagnumStockProgram::PositionNormalTextureVertexLit
                    : MagnumStockProgram::PositionNormalTexture;
                return true;
            default: return false;
        }
    }

    std::string StockVertexShaderSource(MagnumStockProgram program)
    {
        // Env mapping needs its own vertex stage: the reflection basis and the Fresnel weight are
        // per-vertex terms the shared generator has no varyings for. Skinning needs one because it
        // transforms the position and normal before anything else runs.
        if (program == MagnumStockProgram::EnvironmentMap)
            return EnvironmentMapVertexShaderSource();
        if (ProgramIsSkinned(program))
            return SkinnedVertexShaderSource(program);
        if (ProgramIsPbr(program))
            return PbrVertexShaderSource(program);
        return BaseVertexShaderSource(program);
    }

    std::string StockFragmentShaderSource(MagnumStockProgram program)
    {
        if (program == MagnumStockProgram::EnvironmentMap)
            return EnvironmentMapFragmentShaderSource();
        if (ProgramIsSkinned(program))
            return SkinnedFragmentShaderSource(program);
        if (ProgramIsPbr(program))
            return PbrFragmentShaderSource();
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
