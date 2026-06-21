$input a_position, a_texcoord0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;

void main()
{
    gl_Position  = mul(u_wvp, vec4(a_position, 1.0));
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
}
