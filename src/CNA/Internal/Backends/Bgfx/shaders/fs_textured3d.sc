$input v_texcoord0, v_color0, v_fogFactor

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_fogColor;

void main()
{
    vec4 color = texture2D(s_texColor, v_texcoord0) * v_color0;
    // Task 888: mix toward FogColor as v_fogFactor -> 0 (matches EasyGL's established formula).
    color.rgb = mix(u_fogColor.xyz, color.rgb, v_fogFactor);
    gl_FragColor = color;
}
