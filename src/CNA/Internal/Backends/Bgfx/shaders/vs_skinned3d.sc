$input a_position, a_normal, a_texcoord0, a_weight, a_indices
$output v_texcoord0, v_normal, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform mat4 u_bones[72];

void main()
{
    mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x
                 + u_bones[int(a_indices.y)] * a_weight.y
                 + u_bones[int(a_indices.z)] * a_weight.z
                 + u_bones[int(a_indices.w)] * a_weight.w;
    gl_Position  = mul(u_wvp, mul(skinMat, vec4(a_position, 1.0)));
    v_normal     = normalize(skinMat[0].xyz * a_normal.x
                           + skinMat[1].xyz * a_normal.y
                           + skinMat[2].xyz * a_normal.z);
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
}
