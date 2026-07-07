$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_eyeDir

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_eyePos;

void main()
{
    gl_Position   = mul(u_wvp, vec4(a_position, 1.0));
    vec3 worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    // Task 398 fix: transform by the precomputed inverse-transpose of World's upper-left 3x3
    // (cofactor/det, computed on the CPU side), not World directly -- a direct World multiply
    // is only correct under rotation/uniform-scale/translation, and skews the normal under
    // non-uniform scale.
    v_normal      = mul(u_normalMatrix, a_normal);
    v_eyeDir      = u_eyePos.xyz - worldPos;
    v_texcoord0   = a_texcoord0;
}
