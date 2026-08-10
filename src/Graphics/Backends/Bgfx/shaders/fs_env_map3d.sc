$input v_texcoord0, v_normal, v_eyeDir, v_fogFactor

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLERCUBE(s_envMap, 1);

uniform vec4 u_diffuseColor;
uniform vec4 u_emissiveColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_envMapAmount;
uniform vec4 u_envMapSpecular;
uniform vec4 u_fogColor;
uniform vec4 u_rtFlipV;

// REMED-GFX-078: flip V for the 2D base texture when it is a render-target color source (bottom-up
// FBO on originBottomLeft renderers -- see REMED-GFX-067); flip==0 leaves ordinary-texture sampling
// byte-identical. The cube env-map is sampled by a reflection direction, not a 2D V, so it is not
// flipped here (its orientation convention is separate -- see REMED-GFX-067 Phase 22 / GFX-078 cube).
vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

void main()
{
    vec3 N        = normalize(v_normal);
    vec3 E        = normalize(v_eyeDir);
    float NdotL0  = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    float NdotL1  = max(dot(N, -normalize(u_light1Dir.xyz)), 0.0);
    float NdotL2  = max(dot(N, -normalize(u_light2Dir.xyz)), 0.0);
    vec3 lightSum = u_light0Diffuse.xyz * NdotL0 + u_light1Diffuse.xyz * NdotL1 + u_light2Diffuse.xyz * NdotL2;
    // REMED-GFX-007: FNA's Lighting.fxh adds EmissiveColor UNSCALED (lightSum*DiffuseColor +
    // Emissive), not (Emissive + lightSum)*DiffuseColor. u_emissiveColor is already the pre-folded
    // (EmissiveColor + AmbientLightColor*DiffuseColor)*alpha from EnvironmentMapEffect.cpp, so
    // re-scaling it by DiffuseColor double-applied the diffuse tint.
    vec3 litRGB   = lightSum * u_diffuseColor.xyz + u_emissiveColor.xyz;
    vec4 texColor  = texture2D(s_texColor, rtFlipUV(v_texcoord0, u_rtFlipV.x));
    vec3 reflDir   = reflect(-E, N);
    vec4 envSample = textureCube(s_envMap, reflDir);
    vec3 baseColor = litRGB * texColor.xyz;
    float combinedAlpha = u_diffuseColor.w * texColor.w;
    float viewAngle = dot(E, N);
    float blendFactor = (u_envMapAmount.y > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), u_envMapSpecular.w) * u_envMapAmount.x
        : u_envMapAmount.x;
    // Task 891: FNA's real PSEnvMap/PSEnvMapSpecular scale the whole `envmap` sample (both
    // the base lerp target and the specular term, the latter already fixed by Task 395) by
    // combinedAlpha before use -- the base lerp's envSample.xyz was still unscaled here.
    vec3 rgb = mix(baseColor, envSample.xyz * combinedAlpha, blendFactor) + u_envMapSpecular.xyz * envSample.w * combinedAlpha;
    // Task 899: mix toward FogColor as v_fogFactor -> 0 (matches Task 888's established formula).
    rgb = mix(u_fogColor.xyz, rgb, v_fogFactor);
    gl_FragColor  = vec4(rgb, combinedAlpha);
}
