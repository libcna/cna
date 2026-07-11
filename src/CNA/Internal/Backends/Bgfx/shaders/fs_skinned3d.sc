$input v_texcoord0, v_normal, v_color0, v_fogFactor

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_emissiveColor;
uniform vec4 u_fogColor;

void main()
{
    vec4 tex  = texture2D(s_texColor, v_texcoord0);
    vec3 N    = normalize(v_normal);
    float NdL0 = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    float NdL1 = max(dot(N, -normalize(u_light1Dir.xyz)), 0.0);
    float NdL2 = max(dot(N, -normalize(u_light2Dir.xyz)), 0.0);
    vec3 lightSum = NdL0 * u_light0Diffuse.xyz + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    // Task 899: EmissiveColor was a total GPU no-op on Bgfx (u_emissiveColor was never even
    // declared here) -- found while writing this task's fog test, which (matching EasyGL's Task
    // 900 test) uses EmissiveColor as the sole non-fog material-color signal to isolate fog
    // cleanly. C++'s FillGpuDrawParams() already pre-combines AmbientLightColor*DiffuseColor into
    // emissiveColor (u_ambientColor is never separately populated for SkinnedEffect, always 0),
    // matching EasyGL's already-working (uEmissiveColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb.
    vec3 lit  = u_emissiveColor.xyz + u_ambientColor.xyz + lightSum;
    vec3 finalLight = mix(vec3(1.0, 1.0, 1.0), lit, u_lightingEnabled.x);
    gl_FragColor = tex * v_color0 * vec4(finalLight, 1.0);
    // Task 899: mix toward FogColor as v_fogFactor -> 0 (matches Task 888's established formula).
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
