$input a_position, a_normal, a_texcoord0, a_weight, a_indices, a_color0
$output v_texcoord0, v_normal, v_color0, v_fogFactor, v_worldPos, v_vertexColor0

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_diffuseColor;
uniform mat4 u_bones[72];
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;
uniform vec4 u_weightsPerVertex;

void main()
{
    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
    float weightsPerVertex = u_weightsPerVertex.x;
    mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x;
    if (weightsPerVertex >= 2.0) skinMat += u_bones[int(a_indices.y)] * a_weight.y;
    if (weightsPerVertex >= 4.0) skinMat += u_bones[int(a_indices.z)] * a_weight.z
                                          + u_bones[int(a_indices.w)] * a_weight.w;
    vec4 skinnedPos = mul(skinMat, vec4(a_position, 1.0));
    gl_Position  = mul(u_wvp, skinnedPos);
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    // REMED-GFX-006: FNA's Skin() applies the bone 3x3 to the normal, then Lighting.fxh applies
    // the World inverse-transpose (mul(normal, WorldInverseTranspose)). This shader previously
    // applied only the bone 3x3 (audit Variant A -- no world factor at all), which is wrong under
    // any non-identity World rotation or non-uniform scale. u_normalMatrix is the CPU-computed
    // World inverse-transpose (ComputeNormalMatrix3x3), the same uniform the lit shaders use; it is
    // applied as the outer normal matrix after the bone skin. (bgfx's shaderc does not support the
    // GLSL inverse()/transpose() builtins, so the matrix is supplied CPU-side rather than derived
    // in-shader as on the Vulkan/SdlGpu slices.)
    vec3 skinnedNormal = skinMat[0].xyz * a_normal.x
                       + skinMat[1].xyz * a_normal.y
                       + skinMat[2].xyz * a_normal.z;
    v_normal     = normalize(mul(u_normalMatrix, skinnedNormal));
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
    // CNB-67 (Phase 13C) Bgfx port: stride-56 SkinnedEffect+Color vertex color, kept in its own
    // varying (see varying.def.sc's v_vertexColor0 comment) so it can be gated by
    // u_vertexColorEnabled3D and multiplied into the final combined diffuse+specular output in
    // the fragment stage, mirroring EasyGLGraphicsBackend::EnsureSkinnedProgram()'s vColor.
    v_vertexColor0 = a_color0;
    v_worldPos   = mul(u_world, skinnedPos).xyz;
    // REMED-GFX-005: fog factor from raw PRE-SKIN object-space Z (aPos.z, not the skinned
    // position), corrected to EasyGL's Task-1111 form (the prior Task-899 (FogEnd-z) form was
    // the mirror image and wrong).
    // u_fogParams = FNA fog vector (REMED-GFX-010): dot(vec4(pos,1), u_fogParams) = fogFactor.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
