$input v_texcoord0, v_normal, v_eyeDir, v_fogFactor

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLERCUBE(s_envMap, 1);

uniform vec4 u_diffuseColor;
uniform vec4 u_emissiveColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_envMapAmount;
uniform vec4 u_envMapSpecular;
uniform vec4 u_fogColor;

void main()
{
    vec3 N        = normalize(v_normal);
    vec3 E        = normalize(v_eyeDir);
    float NdotL   = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    vec3 litRGB   = (u_emissiveColor.xyz + u_light0Diffuse.xyz * NdotL) * u_diffuseColor.xyz;
    vec4 texColor  = texture2D(s_texColor, v_texcoord0);
    vec3 reflDir   = reflect(-E, N);
    vec4 envSample = textureCube(s_envMap, reflDir);
    vec3 baseColor = litRGB * texColor.xyz;
    float combinedAlpha = u_diffuseColor.w * texColor.w;
    float viewAngle = dot(E, N);
    float blendFactor = (u_envMapAmount.y > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), u_envMapSpecular.w) * u_envMapAmount.x
        : u_envMapAmount.x;
    vec3 rgb = mix(baseColor, envSample.xyz, blendFactor) + u_envMapSpecular.xyz * envSample.w * combinedAlpha;
    // Task 899: mix toward FogColor as v_fogFactor -> 0 (matches Task 888's established formula).
    rgb = mix(u_fogColor.xyz, rgb, v_fogFactor);
    gl_FragColor  = vec4(rgb, combinedAlpha);
}
