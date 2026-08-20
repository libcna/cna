$input v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_fogFactor, v_vertexColor0

#include <bgfx_shader.sh>

// plans/plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect's fragment stage. Mirrors
// EasyGLRenderer::EnsurePbrProgram()'s PbrLight()/main() exactly (real glTF 2.0 reference
// BRDF -- GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility, Schlick Fresnel, glTF Appendix
// B.3.2-B.3.4) -- see that function's own doc comment for the full rationale, and
// EnsureDefaultFlatNormalTexture()'s for why each of the 4 additional PBR maps needs its own
// "map absent" fallback constant (flat normal for s_texNormal, opaque white for the rest).

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texMetallicRoughness, 2);
SAMPLER2D(s_texEmissive, 3);
SAMPLER2D(s_texOcclusion, 4);
SAMPLER2D(s_texSpecular, 5);
SAMPLER2D(s_texSpecularColor, 6);

uniform vec4 u_diffuseColor;
// plans/plan_gltf.md GLTF-465: the shared VertexColorEnabled gate every other CNA bgfx program uses.
uniform vec4 u_vertexColorEnabled3D;
uniform vec4 u_ambientColor;
uniform vec4 u_emissiveColor;
/// PbrEffect: x = MetallicFactor, y = RoughnessFactor, z = normal scale,
/// w = occlusion strength.
uniform vec4 u_metallicRoughnessFactor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_eyePos;
uniform vec4 u_alphaTest;
uniform vec4 u_fogColor;
// x=decode base, y=decode emissive, z=encode output, w=decode specular colour.
uniform vec4 u_srgb;
// xyz=unclamped dielectric F0 before specular-colour multiplication, w=specular factor.
uniform vec4 u_dielectricFresnel;
// x=seven-bit TEXCOORD_1 selector mask.
uniform vec4 u_specularState;
// Two affine UV rows per PBR map: base, normal, metallic-roughness, emissive, occlusion.
uniform vec4 u_pbrTextureTransform[10];
// Two affine UV rows per KHR_materials_specular map: strength, colour.
uniform vec4 u_pbrSpecularTextureTransform[4];
// REMED-GFX-078: per-slot V-flip for render-target color sources (bottom-up FBO on originBottomLeft
// renderers -- see REMED-GFX-067). x=base color(0), y=normal(1), z=metallic-roughness(2),
// w=emissive(3). Slots 4-6 are intentionally not covered -- live RenderTarget2D instances in the
// occlusion/specular roles are not importer output and stay un-compensated (their binds remain
// type-safe). flip==0 leaves ordinary-texture sampling byte-identical.
uniform vec4 u_rtFlipV;

vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

vec2 pbrTransformUV(vec2 uv, int slot)
{
    vec3 value = vec3(uv, 1.0);
    return vec2(dot(value, u_pbrTextureTransform[slot * 2].xyz),
                dot(value, u_pbrTextureTransform[slot * 2 + 1].xyz));
}

vec2 pbrSpecularTransformUV(vec2 uv, int slot)
{
    vec3 value = vec3(uv, 1.0);
    return vec2(dot(value, u_pbrSpecularTextureTransform[slot * 2].xyz),
                dot(value, u_pbrSpecularTextureTransform[slot * 2 + 1].xyz));
}

vec2 pbrUV(vec2 uv0, vec2 uv1, int slot)
{
    float selector = mod(floor(u_specularState.x / exp2(float(slot))), 2.0);
    return mix(uv0, uv1, selector);
}

vec3 cnaSrgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3_splat(2.4));
    return mix(lo, hi, step(vec3_splat(0.04045), c));
}

vec3 cnaLinearToSrgb(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3_splat(0.0)), vec3_splat(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3_splat(0.0031308), c));
}

vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, vec3 F90, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    float k = (roughness + 1.0); k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3 F = F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 kd = vec3_splat(1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

void main()
{
    vec4 baseColorTex = texture2D(s_texColor, rtFlipUV(
        pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 0), 0), u_rtFlipV.x));
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), u_srgb.x);
    // plans/plan_gltf.md GLTF-465: COLOR_0 multiplies the base colour product, ALPHA INCLUDED -- the
    // alpha half is where a BLEND-mode vertex-coloured primitive's transparency comes from. The
    // colour is linear, so unlike the base-colour texture it is not sRGB-decoded.
    vec4 cnaVertexColor = u_vertexColorEnabled3D.x > 0.5 ? v_vertexColor0 : vec4(1.0, 1.0, 1.0, 1.0);
    vec3 albedo = baseColor * u_diffuseColor.rgb * cnaVertexColor.rgb;
    float alpha = baseColorTex.a * u_diffuseColor.a * cnaVertexColor.a;

    vec3 N = normalize(v_normal);
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * v_tangent.w;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture2D(s_texNormal, rtFlipUV(
        pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 1), 1), u_rtFlipV.y)).rgb
        * 2.0 - 1.0;
    sampledNormal.xy *= u_metallicRoughnessFactor.z;
    vec3 finalNormal = normalize(mul(TBN, sampledNormal));

    vec4 mr = texture2D(s_texMetallicRoughness, rtFlipUV(
        pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 2), 2), u_rtFlipV.z));
    float roughness = clamp(mr.g * u_metallicRoughnessFactor.y, 0.045, 1.0);
    float metallic  = clamp(mr.b * u_metallicRoughnessFactor.x, 0.0, 1.0);

    vec3 V = normalize(u_eyePos.xyz - v_worldPos);
    float specularWeight = u_dielectricFresnel.w * texture2D(
        s_texSpecular,
        pbrSpecularTransformUV(pbrUV(v_texcoord0, v_texcoord1, 5), 0)).a;
    vec3 specularColorTex = texture2D(
        s_texSpecularColor,
        pbrSpecularTransformUV(pbrUV(v_texcoord0, v_texcoord1, 6), 1)).rgb;
    specularColorTex = mix(specularColorTex, cnaSrgbToLinear(specularColorTex), u_srgb.w);
    vec3 dielectricF0 = min(u_dielectricFresnel.xyz * specularColorTex, vec3_splat(1.0))
        * specularWeight;
    vec3 F0 = mix(dielectricF0, albedo, metallic);
    vec3 F90 = mix(vec3_splat(specularWeight), vec3_splat(1.0), metallic);

    vec3 Lo = vec3_splat(0.0);
    Lo += PbrLight(finalNormal, V, normalize(-u_light0Dir.xyz), u_light0Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-u_light1Dir.xyz), u_light1Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-u_light2Dir.xyz), u_light2Diffuse.xyz, albedo, F0, F90, roughness, metallic);

    float occlusion = texture2D(
        s_texOcclusion, pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 4), 4)).r;
    occlusion = 1.0 + u_metallicRoughnessFactor.w * (occlusion - 1.0);
    vec3 ambient = u_ambientColor.xyz * albedo * occlusion;
    vec3 emissiveSample = texture2D(s_texEmissive, rtFlipUV(
        pbrTransformUV(pbrUV(v_texcoord0, v_texcoord1, 3), 3), u_rtFlipV.w)).rgb;
    emissiveSample = mix(emissiveSample, cnaSrgbToLinear(emissiveSample), u_srgb.y);
    vec3 emissive = u_emissiveColor.xyz * emissiveSample;

    vec4 result = vec4(ambient + Lo + emissive, alpha);

    float at = (u_alphaTest.y > 0.0)
        ? ((abs(result.a - u_alphaTest.x) < u_alphaTest.y) ? u_alphaTest.z : u_alphaTest.w)
        : ((result.a < u_alphaTest.x) ? u_alphaTest.z : u_alphaTest.w);
    if (at < 0.0) discard;

    vec3 fogLinear = mix(u_fogColor.xyz, cnaSrgbToLinear(u_fogColor.xyz), u_srgb.z);
    result.rgb = mix(fogLinear, result.rgb, v_fogFactor);
    result.rgb = mix(result.rgb, cnaLinearToSrgb(result.rgb), u_srgb.z);
    gl_FragColor = result;
}
