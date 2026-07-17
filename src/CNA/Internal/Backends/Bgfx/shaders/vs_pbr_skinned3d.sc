$input a_position, a_normal, a_tangent, a_texcoord0, a_weight, a_indices
$output v_texcoord0, v_normal, v_tangent, v_worldPos, v_fogFactor

#include <bgfx_shader.sh>

// PBR + skinning combo (SkinnedPbrEffect), Bgfx port: mirrors
// EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s vertex shader exactly -- vs_skinned3d.sc's
// own bone-palette skin transform (applied to Position, Normal, and Tangent) feeding
// vs_pbr3d.sc/fs_pbr3d.sc's own BRDF fragment stage unchanged. Note this applies an EXTRA
// World-space normal/tangent transform after skinning (unlike vs_skinned3d.sc's plain
// mat3(skinMat) multiply) -- an intentional divergence from the regular SkinnedEffect shader,
// matching EnsurePbrSkinnedProgram()'s own documented behavior (PBR's world-space BRDF needs
// normal/tangent in the same world space as v_worldPos/u_eyePos).

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat4 u_bones[72];
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;
uniform vec4 u_weightsPerVertex;

void main()
{
    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- unchanged from vs_skinned3d.sc.
    float weightsPerVertex = u_weightsPerVertex.x;
    mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x;
    if (weightsPerVertex >= 2.0) skinMat += u_bones[int(a_indices.y)] * a_weight.y;
    if (weightsPerVertex >= 4.0) skinMat += u_bones[int(a_indices.z)] * a_weight.z
                                          + u_bones[int(a_indices.w)] * a_weight.w;
    vec4 skinnedPos = mul(skinMat, vec4(a_position, 1.0));
    gl_Position = mul(u_wvp, skinnedPos);
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;

    vec3 skinnedNormal = skinMat[0].xyz * a_normal.x
                        + skinMat[1].xyz * a_normal.y
                        + skinMat[2].xyz * a_normal.z;
    v_normal = normalize(mul(u_world, vec4(skinnedNormal, 0.0)).xyz);

    vec3 skinnedTangent = skinMat[0].xyz * a_tangent.x
                         + skinMat[1].xyz * a_tangent.y
                         + skinMat[2].xyz * a_tangent.z;
    v_tangent = vec4(mul(u_world, vec4(skinnedTangent, 0.0)).xyz, a_tangent.w);

    v_texcoord0 = a_texcoord0;
    v_worldPos = mul(u_world, skinnedPos).xyz;
    // Task 899: fog factor from raw PRE-SKIN object-space Z, unchanged from vs_skinned3d.sc.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z - u_fogParams.y, 1e-6), 0.0, 1.0)
        : 1.0;
}
