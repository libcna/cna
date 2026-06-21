$input v_texcoord0, v_normal, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_lightingEnabled;

void main()
{
    vec4 tex  = texture2D(s_texColor, v_texcoord0);
    vec3 N    = normalize(v_normal);
    float NdL = max(dot(N, -normalize(u_light0Dir.xyz)), 0.0);
    vec3 lit  = u_ambientColor.xyz + NdL * u_light0Diffuse.xyz;
    vec3 finalLight = mix(vec3(1.0, 1.0, 1.0), lit, u_lightingEnabled.x);
    gl_FragColor = tex * v_color0 * vec4(finalLight, 1.0);
}
