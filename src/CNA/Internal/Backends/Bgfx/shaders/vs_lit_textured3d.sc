$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_color0, v_eyeDir

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_diffuseColor;
uniform vec4 u_eyePos;

void main()
{
    gl_Position   = mul(u_wvp, vec4(a_position, 1.0));
    vec3 worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    v_texcoord0   = a_texcoord0;
    v_color0      = u_diffuseColor;
    // Task 892 fix: transform by the precomputed inverse-transpose of World's upper-left 3x3
    // (cofactor/det, computed on the CPU side, mirroring EnvironmentMapEffect's Task 398 fix),
    // not the full WVP -- a WVP-based transform bakes View/Projection into the normal, which is
    // wrong under ANY non-identity camera, not just non-uniform World scale.
    v_normal      = normalize(mul(u_normalMatrix, a_normal));
    v_eyeDir      = u_eyePos.xyz - worldPos;
}
