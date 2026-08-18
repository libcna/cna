$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_weight, a_indices, a_color0
$output v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_fogFactor, v_vertexColor0

#include <bgfx_shader.sh>

// PBR + skinning combo (SkinnedPbrEffect), Bgfx port: mirrors
// EasyGLRenderer::EnsurePbrSkinnedProgram()'s vertex shader exactly -- vs_skinned3d.sc's
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

// GLTF-264: normals follow the inverse transpose of the complete blended joint matrix. shaderc's
// cross-platform dialect has no reliable inverse(mat3), so use the cofactor columns. Normalizing
// later cancels |determinant|; its sign preserves mirrored-joint orientation. A nearly singular
// blend retains the historical direct transform as a finite fallback.
vec3 cnaSkinNormal(mat3 m, vec3 n)
{
    vec3 c0 = m[0];
    vec3 c1 = m[1];
    vec3 c2 = m[2];
    vec3 co0 = cross(c1, c2);
    vec3 co1 = cross(c2, c0);
    vec3 co2 = cross(c0, c1);
    float det = dot(c0, co0);
    vec3 transformed = mul(mat3(co0, co1, co2), n);
    return abs(det) > 1e-6 ? transformed * (det < 0.0 ? -1.0 : 1.0) : mul(m, n);
}

float cnaDirectionHandedness(mat3 m)
{
    float det = dot(m[0], cross(m[1], m[2]));
    return det < 0.0 ? -1.0 : 1.0;
}

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

    mat3 skinDirectionMat = mat3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    vec3 skinnedNormal = cnaSkinNormal(skinDirectionMat, a_normal);
    // REMED-GFX-006: the normal uses the World inverse-transpose (audit Variant B fix -- was raw
    // World, correct only under rotation/uniform scale, wrong under non-uniform scale). FNA's
    // WorldInverseTranspose, supplied CPU-side as u_normalMatrix (bgfx's shaderc has no in-shader
    // inverse()/transpose()). The tangent stays on raw World below: tangents transform as
    // directions, not normals (glTF/PBR convention), so the inverse-transpose must NOT be applied
    // to them.
    v_normal = normalize(mul(u_normalMatrix, skinnedNormal));

    vec3 skinnedTangent = mul(skinDirectionMat, a_tangent.xyz);
    mat3 worldDirectionMat = mat3(u_world[0].xyz, u_world[1].xyz, u_world[2].xyz);
    v_tangent = vec4(mul(u_world, vec4(skinnedTangent, 0.0)).xyz,
                     a_tangent.w * cnaDirectionHandedness(worldDirectionMat)
                         * cnaDirectionHandedness(skinDirectionMat));

    v_texcoord0 = a_texcoord0;
    // plan_gltf.md GLTF-465: glTF 2.0 3.9.2 makes COLOR_0 an additional linear multiplier on base
    // colour. Carried in v_vertexColor0 rather than v_color0 for the same reason the stride-56
    // skinned path does: v_color0 already carries u_diffuseColor for other fragment shaders. Its
    // varying default is opaque white, the multiplier's identity, so a stride without a colour slot
    // is unaffected even before u_vertexColorEnabled3D gates it.
    v_vertexColor0 = a_color0;
    v_texcoord1 = a_texcoord1;
    v_worldPos = mul(u_world, skinnedPos).xyz;
    // Task 899: fog factor from raw PRE-SKIN object-space Z, unchanged from vs_skinned3d.sc.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
