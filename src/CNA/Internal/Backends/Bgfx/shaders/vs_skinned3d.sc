$input a_position, a_normal, a_texcoord0, a_weight, a_indices
$output v_texcoord0, v_normal, v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform mat4 u_bones[72];
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

void main()
{
    mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x
                 + u_bones[int(a_indices.y)] * a_weight.y
                 + u_bones[int(a_indices.z)] * a_weight.z
                 + u_bones[int(a_indices.w)] * a_weight.w;
    gl_Position  = mul(u_wvp, mul(skinMat, vec4(a_position, 1.0)));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    v_normal     = normalize(skinMat[0].xyz * a_normal.x
                           + skinMat[1].xyz * a_normal.y
                           + skinMat[2].xyz * a_normal.z);
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
    // Task 899: fog factor from raw PRE-SKIN object-space Z (matches EasyGL's Task 900 formula
    // exactly, which also uses aPos.z rather than the skinned position).
    // u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z - u_fogParams.y, 1e-6), 0.0, 1.0)
        : 1.0;
}
