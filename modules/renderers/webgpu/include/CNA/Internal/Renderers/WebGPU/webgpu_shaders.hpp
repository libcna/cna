// SPDX-License-Identifier: MS-PL
#pragma once

// WEBGPU-28: every WGSL shader source this renderer compiles, extracted from
// WebGPURenderer.cpp into one place so the whole set is visible and validatable
// together (see WebGPURenderer::ValidateAllShadersEXT and the WebGPU_ShaderValidation
// test). The Pbr/SkinnedPbr entries are MARKED templates expanded at runtime by
// ExpandPbrVertexColourWgslEXT into a bare and a vertex-colour variant; the others are
// compiled as-is. Text is byte-identical to the former inline literals.

namespace CNA::Internal::Renderers::WebGPU::webgpu_shaders
{
    inline constexpr char kSprite[] = R"WGSL(
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
@group(0) @binding(0) var spriteSampler: sampler;
@group(0) @binding(1) var spriteTexture: texture_2d<f32>;
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(spriteTexture, spriteSampler, input.uv) * input.color;
}
)WGSL";

    inline constexpr char kColored[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) fogFactor: f32,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.color = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    // WEBGPU-145: FNA fog keep factor. fogVector (from World*View) dotted with the object-space
    // position gives FNA's saturate(dot(pos, FogVector)); 1-that is the "keep" fraction.
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // WEBGPU-149: FNA ApplyFog is lerp(rgb, FogColor*alpha, fog); keep = 1-fog, so this is
    // mix(FogColor*alpha, rgb, keep). The FogColor is premultiplied by the OUTPUT alpha, exactly as
    // FNA's Common.fxh does (a WebGPU-only earlier version omitted the alpha and was wrong for alpha<1).
    return vec4f(mix(u.fogColor.xyz * input.color.a, input.color.rgb, input.fogFactor), input.color.a);
}
)WGSL";

    inline constexpr char kTextured[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) fogFactor: f32,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    // WEBGPU-145: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x), textureEnabled > 0.5);
    let base = sampled * u.diffuseColor;
    // WEBGPU-149: FNA ApplyFog -- mix(FogColor*base.a, rgb, keep); output alpha preserved.
    return vec4f(mix(u.fogColor.xyz * base.a, base.rgb, input.fogFactor), base.a);
}
)WGSL";

    inline constexpr char kColoredTextured[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) fogFactor: f32,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    // WEBGPU-145: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x), textureEnabled > 0.5);
    let base = sampled * input.tint;
    // WEBGPU-149: FNA ApplyFog -- mix(FogColor*base.a, rgb, keep); output alpha preserved.
    return vec4f(mix(u.fogColor.xyz * base.a, base.rgb, input.fogFactor), base.a);
}
)WGSL";

    // WEBGPU-156/157: the lit families carry a COLOUR input at location 3, and no longer require a
    // UV to be present. BasicEffect.VertexColorEnabled has a "Vc" variant of every lit family in
    // XNA (VSBasicVertexLightingVc and friends) which multiplies the lit diffuse by the per-vertex
    // colour, and the stock ModelProcessor emits exactly such a vertex (Position+Normal+Colour+UV)
    // for any mesh with a colour channel -- a mesh this family could not draw at all before. A
    // location is the input's INDEX in this program's own table
    // (WebGPURenderer::StockVertexInputsForShapeEXT's kLit = {kPos, kNormal, kUv, kColor}), so the
    // colour is 3. A declaration that names no COLOR0 -- or no TEXCOORD0, which is SAMPLE-002's
    // Position+Normal mesh -- leaves that input on the shared neutral (0,0,0,1) record, and
    // u.light0DiffuseVertexColor.w / u.light0DirTexture.w are 0, so vc is white and the sample is
    // white: every existing lit draw is byte-identical.
    inline constexpr char kLitTextured[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
    @location(3) fogFactor: f32,
    @location(4) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.tint = input.color;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    // WEBGPU-145: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    // WEBGPU-157: XNA's Vc variants multiply the vertex colour into the DIFFUSE result BEFORE the
    // specular term is added, so the highlight is scaled only through the resulting alpha.
    let vc = select(vec4f(1.0), input.tint, u.light0DiffuseVertexColor.w > 0.5);
    if (lightingEnabled <= 0.5) {
        // WEBGPU-145: fog applies in the unlit branch too (BasicEffect fog is independent of lighting).
        let unlit = u.diffuseColor * sampled * vc;
        return vec4f(mix(u.fogColor.xyz * unlit.a, unlit.rgb, input.fogFactor), unlit.a);
    }
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    // A disabled/unconfigured DirectionalLight forwards Direction=(0,0,0) (its DiffuseColor/
    // SpecularColor are what get zeroed, matching FNA's own DirectionalLight.cs -- Direction
    // itself is untouched by Enabled=false). normalize() on a true zero vector is undefined and
    // can poison the whole lightSum/specular computation with NaN on real GPU hardware, even
    // though that light's own diffuse/specular contribution is already zero -- guard it here.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRgb = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let lit = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    var color = vec4f(lit, u.diffuseColor.a) * sampled * vc;
    color = vec4f(color.rgb + specularRgb * color.a, color.a);
    // WEBGPU-149: FNA ApplyFog last, after lighting+specular; mix(FogColor*color.a, rgb, keep).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kLitTexturedVertexLit[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
    @location(3) fogFactor: f32,
    @location(4) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.tint = input.color;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let worldNormal = normalMatrix * input.normal;
    let worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    let n = normalize(worldNormal);
    let e = normalize(lp.eyePos.xyz - worldPos);
    // Same disabled-light NaN guard as the per-pixel-lit shader: a disabled DirectionalLight
    // forwards Direction=(0,0,0) (only DiffuseColor/SpecularColor are zeroed), and normalize() on
    // a true zero vector is undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    // WEBGPU-145: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    // WEBGPU-157: see the per-pixel sibling -- the vertex colour multiplies the diffuse result.
    let vc = select(vec4f(1.0), input.tint, u.light0DiffuseVertexColor.w > 0.5);
    if (lightingEnabled <= 0.5) {
        // WEBGPU-145: fog applies in the unlit branch too (BasicEffect fog is independent of lighting).
        let unlit = u.diffuseColor * sampled * vc;
        return vec4f(mix(u.fogColor.xyz * unlit.a, unlit.rgb, input.fogFactor), unlit.a);
    }
    var color = vec4f(input.litRGB, u.diffuseColor.a) * sampled * vc;
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    // WEBGPU-149: FNA ApplyFog last; mix(FogColor*color.a, rgb, keep).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kAlphaTest[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
    fogPad: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) fogFactor: f32,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    // WEBGPU-146: FNA fog keep factor (see colored3d.wgsl). fogColor/fogVector occupy floats
    // [32..39] of the 160-byte primary UBO (FillAlphaTestUniforms), so fogPad skips [28..31].
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x) * u.diffuseColor;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    // WEBGPU-149: FNA ApplyFog on the surviving pixel (fog is applied after the alpha-test discard);
    // mix(FogColor*color.a, rgb, keep) -- FogColor premultiplied by the output alpha.
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kAlphaTestColored[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
    fogPad: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) fogFactor: f32,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.extra.x;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    // WEBGPU-146: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x) * input.tint;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    // WEBGPU-149: FNA ApplyFog on the surviving pixel (fog is applied after the alpha-test discard);
    // mix(FogColor*color.a, rgb, keep) -- FogColor premultiplied by the output alpha.
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    // WEBGPU-159: DualTextureEffect consumes TEXCOORD0 and TEXCOORD1 INDEPENDENTLY -- that is what
    // the effect is for (lightmapping: a base map on the mesh's own UVs, a lightmap on a second,
    // separately unwrapped set). Both families used to declare one `uv` and sample BOTH textures
    // with it, so a second UV set could not reach the shader at all and the canonical
    // `PositionNormalDualTexture` vertex (stride 40) was refused outright by the stride table.
    // A declaration that names no TEXCOORD1 leaves location `kUv1` on the shared neutral
    // (0, 0, 0, 1) record, so uv1 is (0,0) -- the same value the reference renderer's unbound GL
    // attribute supplies, which is what keeps a single-UV dual-texture draw identical on both.
    inline constexpr char kDualTexture[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var tex0Sampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;
@group(1) @binding(3) var tex1Sampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) uv1: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) fogFactor: f32,
    @location(2) uv1: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    // WEBGPU-147: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSampleBias(tex0, tex0Sampler, input.uv, u.samplerBias.x);
    let sample1 = textureSampleBias(tex1, tex1Sampler, input.uv1, u.samplerBias.y);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    let base = sample0 * sample1 * u.diffuseColor;
    // WEBGPU-149: FNA ApplyFog -- mix(FogColor*base.a, rgb, keep); output alpha preserved.
    return vec4f(mix(u.fogColor.xyz * base.a, base.rgb, input.fogFactor), base.a);
}
)WGSL";

    inline constexpr char kDualTextureColored[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var tex0Sampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;
@group(1) @binding(3) var tex1Sampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
    @location(3) uv1: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) fogFactor: f32,
    @location(3) uv1: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    // WEBGPU-147: FNA fog keep factor (see colored3d.wgsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), u.fogVector), 0.0, 1.0);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSampleBias(tex0, tex0Sampler, input.uv, u.samplerBias.x);
    let sample1 = textureSampleBias(tex1, tex1Sampler, input.uv1, u.samplerBias.y);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    let base = sample0 * sample1 * input.tint;
    // WEBGPU-149: FNA ApplyFog -- mix(FogColor*base.a, rgb, keep); output alpha preserved.
    return vec4f(mix(u.fogColor.xyz * base.a, base.rgb, input.fogFactor), base.a);
}
)WGSL";

    inline constexpr char kEnvMap[] = R"WGSL(
struct Transform {
    mvp: mat4x4f,
    world: mat4x4f,
};
@group(0) @binding(0) var<uniform> t: Transform;

struct EnvMapParams {
    eyePos: vec4f,
    diffuseColor: vec4f,
    emissiveAmount: vec4f,
    light0Dir: vec4f,
    light0DiffuseFresnelEn: vec4f,
    envMapSpecFresnelF: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
    // WEBGPU-205: [60]=the base texture's MipMapLevelOfDetailBias, [61]=the environment cube's.
    samplerBias: vec4f,
};
@group(0) @binding(1) var<uniform> ep: EnvMapParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;
@group(1) @binding(2) var envMap: texture_cube<f32>;
@group(1) @binding(3) var envMapSampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) eyeDir: vec3f,
    @location(2) uv: vec2f,
    @location(3) fogFactor: f32,
};

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = t.mvp * vec4f(input.position, 1.0);
    let worldPos = (t.world * vec4f(input.position, 1.0)).xyz;
    let normalMatrix = mat3x3f(ep.normalMatrixCol0.xyz, ep.normalMatrixCol1.xyz, ep.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    output.eyeDir = ep.eyePos.xyz - worldPos;
    output.uv = input.uv;
    // REMED-GFX-100: FNA view-space fog. fogVector is prepared once by the public effect from
    // World*View; all-zero disables fog and {0,0,0,1} gives the defined full-fog zero range.
    output.fogFactor = 1.0 - clamp(dot(vec4f(input.position, 1.0), ep.fogVector), 0.0, 1.0);
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(input.eyeDir);
    let texColor = textureSampleBias(tex, texSampler, input.uv, ep.samplerBias.x);
    let ndotl0 = max(dot(n, -ep.light0Dir.xyz), 0.0);
    let ndotl1 = max(dot(n, -ep.light1Dir.xyz), 0.0);
    let ndotl2 = max(dot(n, -ep.light2Dir.xyz), 0.0);
    let lightSum = ep.light0DiffuseFresnelEn.xyz * ndotl0
                 + ep.light1Diffuse.xyz * ndotl1
                 + ep.light2Diffuse.xyz * ndotl2;
    // REMED-GFX-007: FNA Lighting.fxh adds emissive UNSCALED (litRGB = lightSum*Diffuse +
    // Emissive), not (Emissive + lightSum)*Diffuse -- the latter re-scales the already
    // ambient-folded emissive by DiffuseColor a second time (and, since the CPU layer pre-folds
    // Alpha into both operands, squares Alpha too). emissiveAmount.xyz is the CPU-side pre-folded
    // (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha (EnvironmentMapEffect.cpp).
    let litRGB = lightSum * ep.diffuseColor.rgb + ep.emissiveAmount.xyz;
    let baseColor = litRGB * texColor.rgb;
    let combinedAlpha = ep.diffuseColor.a * texColor.a;
    let reflDir = reflect(-e, n);
    let envSample = textureSampleBias(envMap, envMapSampler, reflDir, ep.samplerBias.y);
    let viewAngle = dot(e, n);
    let fresnelEnabled = ep.light0DiffuseFresnelEn.w;
    let blendFactor = select(ep.emissiveAmount.w,
                             pow(max(1.0 - abs(viewAngle), 0.0), ep.envMapSpecFresnelF.w) * ep.emissiveAmount.w,
                             fresnelEnabled > 0.5);
    var rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor)
            + ep.envMapSpecFresnelF.xyz * envSample.a * combinedAlpha;
    rgb = mix(ep.fogColor.xyz * combinedAlpha, rgb, input.fogFactor);
    return vec4f(rgb, combinedAlpha);
}
)WGSL";

    inline constexpr char kInstanced[] = R"WGSL(
struct Uniforms {
    vp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
};
struct InstanceInput {
    @location(4) instCol0: vec4f,
    @location(5) instCol1: vec4f,
    @location(6) instCol2: vec4f,
    @location(7) instCol3: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput, instance: InstanceInput) -> VertexOutput {
    var output: VertexOutput;
    let world = mat4x4f(instance.instCol0, instance.instCol1, instance.instCol2, instance.instCol3);
    output.position = u.vp * world * vec4f(input.position, 1.0);
    output.color = u.diffuseColor;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

    inline constexpr char kInstancedColored[] = R"WGSL(
struct Uniforms {
    vp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
};
struct InstanceInput {
    @location(4) instCol0: vec4f,
    @location(5) instCol1: vec4f,
    @location(6) instCol2: vec4f,
    @location(7) instCol3: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput, instance: InstanceInput) -> VertexOutput {
    var output: VertexOutput;
    let world = mat4x4f(instance.instCol0, instance.instCol1, instance.instCol2, instance.instCol3);
    output.position = u.vp * world * vec4f(input.position, 1.0);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.color = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

    inline constexpr char kMipBlit[] = R"WGSL(
struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VSOut {
    var output: VSOut;
    let x = f32((vertexIndex << 1u) & 2u);
    let y = f32(vertexIndex & 2u);
    output.position = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    output.uv = vec2f(x, y);
    return output;
}
@group(0) @binding(0) var mipSampler: sampler;
@group(0) @binding(1) var mipSource: texture_2d<f32>;
@fragment fn fs_main(input: VSOut) -> @location(0) vec4f {
    return textureSample(mipSource, mipSampler, input.uv);
}
)WGSL";

    inline constexpr char kPbr[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct PbrFactors {
    metallicRoughness: vec4f,
    alphaTest: vec4f,
    // plans/plan_gltf.md GLTF-344: w decodes the specular COLOUR sample from sRGB.
    srgbFlags: vec4f,
    dielectricFresnel: vec4f,
    textureTransformRows: array<vec4f, 10>,
    // KHR_materials_specular: xyz = UNCLAMPED dielectric F0, w = specularFactor. Unclamped because
    // specularColorTexture multiplies before the min(...,1) below.
    specularFresnelInputs: vec4f,
    specularTextureTransformRows: array<vec4f, 4>,
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;
// plans/plan_gltf.md GLTF-344: KHR_materials_specular's own two inputs, at the same slots every other
// sampling renderer uses -- strength in the scalar map's ALPHA, colour in the colour map's RGB.
@group(1) @binding(6) var specularTex: texture_2d<f32>;
@group(1) @binding(7) var specularColorTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    /*CNA_PBR_COLOR_ATTRIBUTE*/
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
    /*CNA_PBR_COLOR_VARYING*/
};
fn directionHandedness(m: mat3x3f) -> f32 {
    return select(1.0, -1.0, dot(m[0], cross(m[1], m[2])) < 0.0);
}
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    // Tangent transforms as a plain direction under mat3(world) (uniform-scale assumption),
    // matching EnsurePbrProgram()'s own documented simplification.
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    output.worldTangent = worldMat3 * input.tangent.xyz;
    output.bitangentSign = input.tangent.w * directionHandedness(worldMat3);
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    /*CNA_PBR_COLOR_ASSIGN*/
    return output;
}

fn srgbToLinear(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + vec3f(0.055)) / 1.055, vec3f(2.4));
    return select(lo, hi, c >= vec3f(0.04045));
}

fn linearToSrgb(c: vec3f) -> vec3f {
    let lo = c * 12.92;
    let hi = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - vec3f(0.055);
    return select(lo, hi, c >= vec3f(0.0031308));
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, f90: vec3f, roughness: f32, metallic: f32) -> vec3f {
    let h = normalize(v + l);
    let ndotl = max(dot(n, l), 0.0);
    let ndotv = max(dot(n, v), 1e-4);
    let ndoth = max(dot(n, h), 0.0);
    let vdoth = max(dot(v, h), 0.0);
    let a2 = pow(roughness, 4.0);
    let dTerm = ndoth * ndoth * (a2 - 1.0) + 1.0;
    let d = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    var k = roughness + 1.0;
    k = k * k / 8.0;
    let g = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));
    let f = f0 + (f90 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

fn pbrSpecularTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.specularTextureTransformRows[slot * 2u].xyz),
                 dot(value, pf.specularTextureTransformRows[slot * 2u + 1u].xyz));
}

fn pbrTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.textureTransformRows[slot * 2u].xyz),
                 dot(value, pf.textureTransformRows[slot * 2u + 1u].xyz));
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u));
    let baseColor = select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5);
    // plans/plan_gltf.md GLTF-465: glTF 3.9.2 makes COLOR_0 an additional LINEAR multiplier on the whole
    // base-colour product, alpha included -- the alpha half is where a BLEND-mode vertex-coloured
    // primitive's transparency comes from. Expanded to the opaque-white identity in the variants
    // whose vertex layout supplies no colour, so one fragment body serves both.
    let cnaVertexColor = /*CNA_PBR_COLOR_VALUE*/vec4f(1.0)/**/;
    let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;
    let useTolerance = pf.alphaTest.y > 0.0;
    let lessTest = alpha < pf.alphaTest.x;
    let toleranceTest = abs(alpha - pf.alphaTest.x) < pf.alphaTest.y;
    let passesAlphaTest = select(lessTest, toleranceTest, useTolerance);
    let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest);
    if (alphaWeight < 0.0) {
        discard;
    }

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    var sampledNormal = textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0;
    sampledNormal.x *= pf.metallicRoughness.z;
    sampledNormal.y *= pf.metallicRoughness.z;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u));
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    // plans/plan_gltf.md GLTF-344: KHR_materials_specular. strength comes from the scalar map's ALPHA and
    // colour from the colour map's sRGB-decoded RGB, each through its own affine transform; the
    // dielectric F0 is min(unclampedF0 * colourSample, 1) * strength, which is the extension's own
    // ordering and the reason the unclamped value is uploaded. A material without either map samples
    // the white identity, so the product collapses to the factor alone.
    let specularStrength = pf.specularFresnelInputs.w
        * textureSample(specularTex, texSampler, pbrSpecularTransformUv(input.uv, 0u)).a;
    let specularColorSample = textureSample(specularColorTex, texSampler,
                                            pbrSpecularTransformUv(input.uv, 1u)).rgb;
    let specularColorLinear = select(specularColorSample, srgbToLinear(specularColorSample),
                                     pf.srgbFlags.w > 0.5);
    let dielectricF0 = min(pf.specularFresnelInputs.xyz * specularColorLinear, vec3f(1.0))
        * specularStrength;
    let f0 = mix(dielectricF0, albedo, metallic);
    let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic);

    // Same disabled-light NaN guard as lit_textured3d.wgsl: a disabled DirectionalLight forwards
    // Direction=(0,0,0) (only DiffuseColor is zeroed), and normalize() on a true zero vector is
    // undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, f90, roughness, metallic);

    let occlusionSample = textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r;
    let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissiveSample = textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u)).rgb;
    let emissiveLinear = select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5);
    let emissive = lp.emissiveColor.xyz * emissiveLinear;

    let linearRgb = ambient + lo + emissive;
    let outputRgb = select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5);
    return vec4f(outputRgb, alpha);
}
)WGSL";

    inline constexpr char kSkinned[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
    @location(3) fogFactor: f32,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

fn skinNormal(m: mat3x3f, n: vec3f) -> vec3f {
    let c0 = m[0];
    let c1 = m[1];
    let c2 = m[2];
    let co0 = cross(c1, c2);
    let co1 = cross(c2, c0);
    let co2 = cross(c0, c1);
    let det = dot(c0, co0);
    let transformed = mat3x3f(co0, co1, co2) * n;
    if (abs(det) > 1e-6) {
        return transformed * select(-1.0, 1.0, det >= 0.0);
    }
    return m * n;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    // WEBGPU-148: FNA fog keep factor from the SKINNED view-space position (matches FNA
    // SkinnedEffect, which skins the position before ComputeCommonVSOutput, and Vulkan's
    // skinned3d.vert.glsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(skinnedPos.xyz, 1.0), u.fogVector), 0.0, 1.0);
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). The world factor was missing entirely (audit
    // Variant A), so any rotated or non-uniformly-scaled skinned model was lit as if World were
    // identity. The fragment stage re-normalizes worldNormal.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * skinNormal(skinMat3, input.normal));
    output.worldPos = (lp.world * skinnedPos).xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    // Same disabled-light NaN guard as lit_textured3d.wgsl/pbr3d.wgsl: a disabled DirectionalLight
    // forwards Direction=(0,0,0) (only DiffuseColor/SpecularColor are zeroed), and normalize() on a
    // true zero vector is undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    let litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let texColor = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x);
    var color = vec4f(litRGB * texColor.rgb, u.diffuseColor.a * texColor.a);
    color = vec4f(color.rgb + specularRGB * color.a, color.a);
    // WEBGPU-148: ApplyFog last (matches FNA's ApplyFog ordering).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kSkinnedColor[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
    @location(5) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
    @location(3) color: vec4f,
    @location(4) fogFactor: f32,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    // WEBGPU-148: FNA fog keep factor from the SKINNED view-space position (matches FNA
    // SkinnedEffect, which skins the position before ComputeCommonVSOutput, and Vulkan's
    // skinned3d.vert.glsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(skinnedPos.xyz, 1.0), u.fogVector), 0.0, 1.0);
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). The world factor was missing entirely (audit
    // Variant A), so any rotated or non-uniformly-scaled skinned model was lit as if World were
    // identity. The fragment stage re-normalizes worldNormal.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * (skinMat3 * input.normal));
    output.worldPos = (lp.world * skinnedPos).xyz;
    output.color = input.color;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    let litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let texColor = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    let vc = select(vec4f(1.0, 1.0, 1.0, 1.0), input.color, vertexColorEnabled > 0.5);
    var color = vec4f(litRGB * texColor.rgb, u.diffuseColor.a * texColor.a * vc.a);
    color = vec4f(color.rgb + specularRGB * color.a, color.a);
    color = vec4f(color.rgb * vc.rgb, color.a);
    // WEBGPU-148: ApplyFog last (matches FNA's ApplyFog ordering).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kSkinnedVertexLit[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
    @location(3) fogFactor: f32,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    // WEBGPU-148: FNA fog keep factor from the SKINNED view-space position (matches FNA
    // SkinnedEffect, which skins the position before ComputeCommonVSOutput, and Vulkan's
    // skinned3d.vert.glsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(skinnedPos.xyz, 1.0), u.fogVector), 0.0, 1.0);
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). This per-vertex-lit sibling had the identical
    // missing-world-factor defect (audit Variant A); lighting is evaluated in this stage, so the
    // world-transformed normal is re-normalized here.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let n = normalize(normalMatrix * (skinMat3 * input.normal));
    let worldPos = (lp.world * skinnedPos).xyz;
    let e = normalize(lp.eyePos.xyz - worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x);
    var color = vec4f(input.litRGB * texColor.rgb, u.diffuseColor.a * texColor.a);
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    // WEBGPU-148: ApplyFog last (matches FNA's ApplyFog ordering).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kSkinnedVertexLitColor[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
    fogColor: vec4f,
    fogVector: vec4f,
    // WEBGPU-205: [40]=slot 0's MipMapLevelOfDetailBias, [41]=slot 1's. WGPUSamplerDescriptor has
    // no lodBias field at all -- addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp,
    // lodMaxClamp, compare and maxAnisotropy, and nothing else -- so the state travels here and is
    // applied by textureSampleBias below. An absent sampler field is not an absent capability.
    samplerBias: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(2) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) blendWeight: vec4f,
    @location(4) blendIndices: vec4<u32>,
    @location(5) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
    @location(3) color: vec4f,
    @location(4) fogFactor: f32,
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    output.uv = input.uv;
    // WEBGPU-148: FNA fog keep factor from the SKINNED view-space position (matches FNA
    // SkinnedEffect, which skins the position before ComputeCommonVSOutput, and Vulkan's
    // skinned3d.vert.glsl).
    output.fogFactor = 1.0 - clamp(dot(vec4f(skinnedPos.xyz, 1.0), u.fogVector), 0.0, 1.0);
    output.color = input.color;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    // REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
    // (inverse-transpose of World, CPU-precomputed into lp.normalMatrixCol* by
    // FillLitLightUniforms and previously unused). This per-vertex-lit sibling had the identical
    // missing-world-factor defect (audit Variant A); lighting is evaluated in this stage, so the
    // world-transformed normal is re-normalized here.
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let n = normalize(normalMatrix * (skinMat3 * input.normal));
    let worldPos = (lp.world * skinnedPos).xyz;
    let e = normalize(lp.eyePos.xyz - worldPos);
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = ndotl0 * u.light0DiffuseVertexColor.xyz + ndotl1 * lp.light1Diffuse.xyz
                  + ndotl2 * lp.light2Diffuse.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    return output;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSampleBias(tex, texSampler, input.uv, u.samplerBias.x);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    let vc = select(vec4f(1.0, 1.0, 1.0, 1.0), input.color, vertexColorEnabled > 0.5);
    var color = vec4f(input.litRGB * texColor.rgb, u.diffuseColor.a * texColor.a * vc.a);
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    color = vec4f(color.rgb * vc.rgb, color.a);
    // WEBGPU-148: ApplyFog last (matches FNA's ApplyFog ordering).
    return vec4f(mix(u.fogColor.xyz * color.a, color.rgb, input.fogFactor), color.a);
}
)WGSL";

    inline constexpr char kSkinnedPbr[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct PbrFactors {
    metallicRoughness: vec4f,
    alphaTest: vec4f,
    // plans/plan_gltf.md GLTF-344: w decodes the specular COLOUR sample from sRGB.
    srgbFlags: vec4f,
    dielectricFresnel: vec4f,
    textureTransformRows: array<vec4f, 10>,
    // KHR_materials_specular: xyz = UNCLAMPED dielectric F0, w = specularFactor. Unclamped because
    // specularColorTexture multiplies before the min(...,1) below.
    specularFresnelInputs: vec4f,
    specularTextureTransformRows: array<vec4f, 4>,
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

struct SkinningParams {
    weightsPerVertex: vec4f,
    bones: array<mat4x4f, 72>,
};
@group(0) @binding(3) var<uniform> sk: SkinningParams;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;
// plans/plan_gltf.md GLTF-344: KHR_materials_specular's own two inputs, at the same slots every other
// sampling renderer uses -- strength in the scalar map's ALPHA, colour in the colour map's RGB.
@group(1) @binding(6) var specularTex: texture_2d<f32>;
@group(1) @binding(7) var specularColorTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) blendWeight: vec4f,
    @location(5) blendIndices: vec4<u32>,
    /*CNA_PBR_COLOR_ATTRIBUTE*/
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
    /*CNA_PBR_COLOR_VARYING*/
};

fn skinMatrix(blendWeight: vec4f, blendIndices: vec4<u32>) -> mat4x4f {
    var skinMat = sk.bones[blendIndices.x] * blendWeight.x;
    if (sk.weightsPerVertex.x >= 2.0) {
        skinMat = skinMat + sk.bones[blendIndices.y] * blendWeight.y;
    }
    if (sk.weightsPerVertex.x >= 4.0) {
        skinMat = skinMat + sk.bones[blendIndices.z] * blendWeight.z
                           + sk.bones[blendIndices.w] * blendWeight.w;
    }
    return skinMat;
}

fn pbrSkinNormal(m: mat3x3f, n: vec3f) -> vec3f {
    let c0 = m[0];
    let c1 = m[1];
    let c2 = m[2];
    let co0 = cross(c1, c2);
    let co1 = cross(c2, c0);
    let co2 = cross(c0, c1);
    let det = dot(c0, co0);
    let transformed = mat3x3f(co0, co1, co2) * n;
    if (abs(det) > 1e-6) {
        return transformed * select(-1.0, 1.0, det >= 0.0);
    }
    return m * n;
}

fn pbrDirectionHandedness(m: mat3x3f) -> f32 {
    return select(1.0, -1.0, dot(m[0], cross(m[1], m[2])) < 0.0);
}

@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let skinMat = skinMatrix(input.blendWeight, input.blendIndices);
    let skinnedPos = skinMat * vec4f(input.position, 1.0);
    output.position = u.mvp * skinnedPos;
    let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    // REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix
    // (lp.normalMatrixCol*), not raw worldMat3. Raw World is correct only for rotation and uniform
    // scale and diverges from FNA's mul(normal, WorldInverseTranspose) under non-uniform scale; it
    // also contradicted this renderer's own unskinned pbr3d.wgsl, which already uses the inverse
    // transpose. The tangent stays on raw worldMat3: tangents transform as directions, not as
    // normals (glTF convention, unchanged).
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalize(normalMatrix * pbrSkinNormal(skinMat3, input.normal));
    output.worldTangent = worldMat3 * (skinMat3 * input.tangent.xyz);
    output.bitangentSign = input.tangent.w * pbrDirectionHandedness(worldMat3)
                                           * pbrDirectionHandedness(skinMat3);
    output.worldPos = (lp.world * skinnedPos).xyz;
    /*CNA_PBR_COLOR_ASSIGN*/
    output.uv = input.uv;
    return output;
}

fn srgbToLinear(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + vec3f(0.055)) / 1.055, vec3f(2.4));
    return select(lo, hi, c >= vec3f(0.04045));
}

fn linearToSrgb(c: vec3f) -> vec3f {
    let lo = c * 12.92;
    let hi = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - vec3f(0.055);
    return select(lo, hi, c >= vec3f(0.0031308));
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, f90: vec3f, roughness: f32, metallic: f32) -> vec3f {
    let h = normalize(v + l);
    let ndotl = max(dot(n, l), 0.0);
    let ndotv = max(dot(n, v), 1e-4);
    let ndoth = max(dot(n, h), 0.0);
    let vdoth = max(dot(v, h), 0.0);
    let a2 = pow(roughness, 4.0);
    let dTerm = ndoth * ndoth * (a2 - 1.0) + 1.0;
    let d = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    var k = roughness + 1.0;
    k = k * k / 8.0;
    let g = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));
    let f = f0 + (f90 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

fn pbrSpecularTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.specularTextureTransformRows[slot * 2u].xyz),
                 dot(value, pf.specularTextureTransformRows[slot * 2u + 1u].xyz));
}

fn pbrTransformUv(uv: vec2f, slot: u32) -> vec2f {
    let value = vec3f(uv, 1.0);
    return vec2f(dot(value, pf.textureTransformRows[slot * 2u].xyz),
                 dot(value, pf.textureTransformRows[slot * 2u + 1u].xyz));
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, pbrTransformUv(input.uv, 0u));
    let baseColor = select(baseColorSample.rgb, srgbToLinear(baseColorSample.rgb), pf.srgbFlags.x > 0.5);
    // plans/plan_gltf.md GLTF-465: glTF 3.9.2 makes COLOR_0 an additional LINEAR multiplier on the whole
    // base-colour product, alpha included -- the alpha half is where a BLEND-mode vertex-coloured
    // primitive's transparency comes from. Expanded to the opaque-white identity in the variants
    // whose vertex layout supplies no colour, so one fragment body serves both.
    let cnaVertexColor = /*CNA_PBR_COLOR_VALUE*/vec4f(1.0)/**/;
    let albedo = baseColor * u.diffuseColor.rgb * cnaVertexColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a * cnaVertexColor.a;
    let useTolerance = pf.alphaTest.y > 0.0;
    let lessTest = alpha < pf.alphaTest.x;
    let toleranceTest = abs(alpha - pf.alphaTest.x) < pf.alphaTest.y;
    let passesAlphaTest = select(lessTest, toleranceTest, useTolerance);
    let alphaWeight = select(pf.alphaTest.w, pf.alphaTest.z, passesAlphaTest);
    if (alphaWeight < 0.0) {
        discard;
    }

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    var sampledNormal = textureSample(normalTex, texSampler, pbrTransformUv(input.uv, 1u)).rgb * 2.0 - 1.0;
    sampledNormal.x *= pf.metallicRoughness.z;
    sampledNormal.y *= pf.metallicRoughness.z;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, pbrTransformUv(input.uv, 2u));
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    // plans/plan_gltf.md GLTF-344: KHR_materials_specular. strength comes from the scalar map's ALPHA and
    // colour from the colour map's sRGB-decoded RGB, each through its own affine transform; the
    // dielectric F0 is min(unclampedF0 * colourSample, 1) * strength, which is the extension's own
    // ordering and the reason the unclamped value is uploaded. A material without either map samples
    // the white identity, so the product collapses to the factor alone.
    let specularStrength = pf.specularFresnelInputs.w
        * textureSample(specularTex, texSampler, pbrSpecularTransformUv(input.uv, 0u)).a;
    let specularColorSample = textureSample(specularColorTex, texSampler,
                                            pbrSpecularTransformUv(input.uv, 1u)).rgb;
    let specularColorLinear = select(specularColorSample, srgbToLinear(specularColorSample),
                                     pf.srgbFlags.w > 0.5);
    let dielectricF0 = min(pf.specularFresnelInputs.xyz * specularColorLinear, vec3f(1.0))
        * specularStrength;
    let f0 = mix(dielectricF0, albedo, metallic);
    let f90 = mix(vec3f(specularStrength), vec3f(1.0), metallic);

    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, f90, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, f90, roughness, metallic);

    let occlusionSample = textureSample(occlusionTex, texSampler, pbrTransformUv(input.uv, 4u)).r;
    let occlusion = 1.0 + pf.metallicRoughness.w * (occlusionSample - 1.0);
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissiveSample = textureSample(emissiveTex, texSampler, pbrTransformUv(input.uv, 3u)).rgb;
    let emissiveLinear = select(emissiveSample, srgbToLinear(emissiveSample), pf.srgbFlags.y > 0.5);
    let emissive = lp.emissiveColor.xyz * emissiveLinear;

    let linearRgb = ambient + lo + emissive;
    let outputRgb = select(linearRgb, linearToSrgb(linearRgb), pf.srgbFlags.z > 0.5);
    return vec4f(outputRgb, alpha);
}
)WGSL";

    /// Directly-compilable WGSL sources (label, source). Pbr/SkinnedPbr are marked
    /// templates handled separately by ValidateAllShadersEXT.
    struct ShaderEntry { const char* label; const char* source; };
    inline constexpr ShaderEntry kDirectShaders[] = {
        {"SpriteBatch", kSprite},
        {"Colored3D", kColored},
        {"Textured3D", kTextured},
        {"ColoredTextured3D", kColoredTextured},
        {"LitTextured3D", kLitTextured},
        {"LitTextured3D VertexLit", kLitTexturedVertexLit},
        {"AlphaTest3D", kAlphaTest},
        {"AlphaTest3D VertexColor", kAlphaTestColored},
        {"DualTexture3D", kDualTexture},
        {"DualTexture3D VertexColor", kDualTextureColored},
        {"EnvMap3D", kEnvMap},
        {"Instanced3D", kInstanced},
        {"Instanced3D VertexColor", kInstancedColored},
        {"MipBlit", kMipBlit},
        {"Skinned3D", kSkinned},
        {"Skinned3D VertexColor", kSkinnedColor},
        {"Skinned3D VertexLit", kSkinnedVertexLit},
        {"Skinned3D VertexLitColor", kSkinnedVertexLitColor},
    };
}
