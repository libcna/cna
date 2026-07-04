$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_eyeDir

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform vec4 u_eyePos;

void main()
{
    gl_Position   = mul(u_wvp, vec4(a_position, 1.0));
    vec3 worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    v_normal      = mul(u_world, vec4(a_normal, 0.0)).xyz;
    v_eyeDir      = u_eyePos.xyz - worldPos;
    v_texcoord0   = a_texcoord0;
}
