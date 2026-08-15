$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_fogFactor

#include <bgfx_shader.sh>

// plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect's vertex stage. Mirrors
// EasyGLRenderer::EnsurePbrProgram()'s vertex shader exactly -- see that function's own
// doc comment for the full glTF metallic-roughness BRDF rationale.

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

float cnaDirectionHandedness(mat3 m)
{
    float det = dot(m[0], cross(m[1], m[2]));
    return det < 0.0 ? -1.0 : 1.0;
}

void main()
{
    gl_Position = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    // Task 398 fix precedent: transform by the precomputed inverse-transpose of World's
    // upper-left 3x3 (cofactor/det, computed on the CPU side), not World directly.
    v_normal = mul(u_normalMatrix, a_normal);
    // Tangent transforms as a plain direction under World (not the inverse-transpose normal
    // matrix) -- correct for uniform-scale World transforms, matching EnsurePbrProgram()'s own
    // documented simplification. mul(u_world, vec4(dir, 0.0)) drops the translation row, giving
    // the same result as a mat3(World) multiply.
    mat3 worldDirectionMat = mat3(u_world[0].xyz, u_world[1].xyz, u_world[2].xyz);
    v_tangent = vec4(mul(u_world, vec4(a_tangent.xyz, 0.0)).xyz,
                     a_tangent.w * cnaDirectionHandedness(worldDirectionMat));
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    // Task 899/1111 fog-factor convention (see vs_skinned3d.sc's identical comment for the full
    // derivation): u_fogParams = FNA fog vector (REMED-GFX-010): dot(vec4(pos,1), u_fogParams) = fogFactor.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(a_position, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
