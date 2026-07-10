$input a_position, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_color0

#include <bgfx_shader.sh>

uniform mat4 u_vp;
uniform vec4 u_depthBias;

void main()
{
    mat4 world  = mat4(i_data0, i_data1, i_data2, i_data3);
    gl_Position = mul(u_vp, mul(world, vec4(a_position, 1.0)));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    v_color0    = a_color0;
}
