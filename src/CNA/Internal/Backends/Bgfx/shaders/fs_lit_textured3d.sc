$input v_texcoord0, v_normal, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_emissiveColor;

void main()
{
    vec4 tex   = texture2D(s_texColor, v_texcoord0);
    vec3 N     = normalize(v_normal);
    float NdL0 = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    float NdL1 = max(dot(N, -normalize(u_light1Dir.xyz)), 0.0);
    float NdL2 = max(dot(N, -normalize(u_light2Dir.xyz)), 0.0);
    vec3 lightSum = u_ambientColor.xyz + NdL0 * u_light0Diffuse.xyz
                    + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    vec3 finalLight = mix(vec3(1.0, 1.0, 1.0), lightSum, u_lightingEnabled.x);
    // EmissiveColor is added after the light sum is multiplied by DiffuseColor (v_color0),
    // not scaled by it, then the whole result is modulated by the texture sample (matches
    // FNA's PSBasicTexture: tex2D(...) * pin.Diffuse, where pin.Diffuse already has
    // EmissiveColor baked in additively by ComputeLights()).
    vec3 litColor = v_color0.rgb * finalLight + u_emissiveColor.xyz;
    gl_FragColor = tex * vec4(litColor, v_color0.a);
}
