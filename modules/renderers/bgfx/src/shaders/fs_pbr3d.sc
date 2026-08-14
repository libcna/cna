$input v_texcoord0, v_normal, v_tangent, v_worldPos, v_fogFactor

#include <bgfx_shader.sh>

// plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect's fragment stage. Mirrors
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

uniform vec4 u_diffuseColor;
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
// x=decode base, y=decode emissive, z=encode output.
uniform vec4 u_srgb;
// REMED-GFX-078: per-slot V-flip for render-target color sources (bottom-up FBO on originBottomLeft
// renderers -- see REMED-GFX-067). x=base color(0), y=normal(1), z=metallic-roughness(2),
// w=emissive(3). The occlusion map (slot 4) is intentionally NOT covered -- a live RenderTarget2D as
// a PBR occlusion map is not a real material and is left un-compensated (its bind is still type-safe,
// no UB). flip==0 leaves ordinary-texture sampling byte-identical.
uniform vec4 u_rtFlipV;

vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

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

vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, float roughness, float metallic)
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
    vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 kd = vec3_splat(1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

void main()
{
    vec4 baseColorTex = texture2D(s_texColor, rtFlipUV(v_texcoord0, u_rtFlipV.x));
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), u_srgb.x);
    vec3 albedo = baseColor * u_diffuseColor.rgb;
    float alpha = baseColorTex.a * u_diffuseColor.a;

    vec3 N = normalize(v_normal);
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * v_tangent.w;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture2D(s_texNormal, rtFlipUV(v_texcoord0, u_rtFlipV.y)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= u_metallicRoughnessFactor.z;
    vec3 finalNormal = normalize(mul(TBN, sampledNormal));

    vec4 mr = texture2D(s_texMetallicRoughness, rtFlipUV(v_texcoord0, u_rtFlipV.z));
    float roughness = clamp(mr.g * u_metallicRoughnessFactor.y, 0.045, 1.0);
    float metallic  = clamp(mr.b * u_metallicRoughnessFactor.x, 0.0, 1.0);

    vec3 V = normalize(u_eyePos.xyz - v_worldPos);
    vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

    vec3 Lo = vec3_splat(0.0);
    Lo += PbrLight(finalNormal, V, normalize(-u_light0Dir.xyz), u_light0Diffuse.xyz, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-u_light1Dir.xyz), u_light1Diffuse.xyz, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-u_light2Dir.xyz), u_light2Diffuse.xyz, albedo, F0, roughness, metallic);

    float occlusion = texture2D(s_texOcclusion, v_texcoord0).r;
    occlusion = 1.0 + u_metallicRoughnessFactor.w * (occlusion - 1.0);
    vec3 ambient = u_ambientColor.xyz * albedo * occlusion;
    vec3 emissiveSample = texture2D(s_texEmissive, rtFlipUV(v_texcoord0, u_rtFlipV.w)).rgb;
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
