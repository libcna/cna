$input a_position, a_normal, a_tangent, a_texcoord0, a_weight, a_indices
$output v_texcoord0, v_normal, v_tangent, v_worldPos, v_fogFactor

#include <bgfx_shader.sh>

// PBR + skinning combo (SkinnedPbrEffect), Bgfx port: mirrors
// EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s vertex shader exactly -- vs_skinned3d.sc's
// own bone-palette skin transform (applied to Position, Normal, and Tangent) feeding
// vs_pbr3d.sc/fs_pbr3d.sc's own BRDF fragment stage unchanged. Both this shader and
// vs_skinned3d.sc apply a World-space transform after the bone skin (post REMED-GFX-006); PBR's
// world-space BRDF needs normal/tangent in the same world space as v_worldPos/u_eyePos. The normal
// uses the World inverse-transpose (REMED-GFX-006); the tangent uses raw World (a direction).

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
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
    // REMED-GFX-006: the normal uses the World inverse-transpose (audit Variant B fix -- was raw
    // World, correct only under rotation/uniform scale, wrong under non-uniform scale). FNA's
    // WorldInverseTranspose, supplied CPU-side as u_normalMatrix (bgfx's shaderc has no in-shader
    // inverse()/transpose()). The tangent stays on raw World below: tangents transform as
    // directions, not normals (glTF/PBR convention), so the inverse-transpose must NOT be applied
    // to them.
    v_normal = normalize(mul(u_normalMatrix, skinnedNormal));

    vec3 skinnedTangent = skinMat[0].xyz * a_tangent.x
                         + skinMat[1].xyz * a_tangent.y
                         + skinMat[2].xyz * a_tangent.z;
    v_tangent = vec4(mul(u_world, vec4(skinnedTangent, 0.0)).xyz, a_tangent.w);

    v_texcoord0 = a_texcoord0;
    v_worldPos = mul(u_world, skinnedPos).xyz;
    // Task 899: fog factor from raw PRE-SKIN object-space Z, unchanged from vs_skinned3d.sc.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
