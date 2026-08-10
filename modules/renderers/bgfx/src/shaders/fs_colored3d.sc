$input v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform vec4 u_fogColor;

void main()
{
    vec4 color = v_color0;
    // Task 888: mix toward FogColor as v_fogFactor -> 0 (matches EasyGL's established formula).
    color.rgb = mix(u_fogColor.xyz, color.rgb, v_fogFactor);
    gl_FragColor = color;
}
