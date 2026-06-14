$input a_position, a_color0
$output v_color0

#include <bgfx_shader.sh>

uniform mat4 u_wvp;

void main()
{
    gl_Position = mul(u_wvp, vec4(a_position, 1.0));
    v_color0 = a_color0;
}
