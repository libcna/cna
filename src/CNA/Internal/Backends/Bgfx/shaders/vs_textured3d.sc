$input a_position, a_texcoord0
$output v_texcoord0, v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform vec4 u_fogParams;

void main()
{
    gl_Position  = mul(u_wvp, vec4(a_position, 1.0));
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
    // Task 888: fog factor from raw object-space Z (matches EasyGL's established formula
    // exactly). u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z - u_fogParams.y, 1e-6), 0.0, 1.0)
        : 1.0;
}
