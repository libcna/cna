$input v_texcoord0, v_normal, v_eyeDir

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLERCUBE(s_envMap, 1);

uniform vec4 u_diffuseColor;
uniform vec4 u_emissiveColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_envMapAmount;
uniform vec4 u_envMapSpecular;

void main()
{
    vec3 N        = normalize(v_normal);
    vec3 E        = normalize(v_eyeDir);
    float NdotL   = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    vec3 litRGB   = (u_emissiveColor.xyz + u_light0Diffuse.xyz * NdotL) * u_diffuseColor.xyz;
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    vec3 reflDir  = reflect(-E, N);
    vec3 envColor  = textureCube(s_envMap, reflDir).xyz;
    vec3 baseColor = litRGB * texColor.xyz;
    vec3 rgb       = mix(baseColor, envColor, u_envMapAmount.x) + u_envMapSpecular.xyz;
    gl_FragColor  = vec4(rgb, u_diffuseColor.w * texColor.w);
}
