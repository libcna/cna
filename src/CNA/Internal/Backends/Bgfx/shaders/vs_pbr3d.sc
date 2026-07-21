$input a_position, a_normal, a_tangent, a_texcoord0
$output v_texcoord0, v_normal, v_tangent, v_worldPos, v_fogFactor

#include <bgfx_shader.sh>

// plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect's vertex stage. Mirrors
// EasyGLGraphicsBackend::EnsurePbrProgram()'s vertex shader exactly -- see that function's own
// doc comment for the full glTF metallic-roughness BRDF rationale.

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

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
    v_tangent = vec4(mul(u_world, vec4(a_tangent.xyz, 0.0)).xyz, a_tangent.w);
    v_texcoord0 = a_texcoord0;
    v_worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    // Task 899/1111 fog-factor convention (see vs_skinned3d.sc's identical comment for the full
    // derivation): u_fogParams = (fogEnabled, fogStart, fogEnd, unused).
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? ((abs(u_fogParams.z - u_fogParams.y) < 1e-6)
            ? 0.0
            : clamp((a_position.z + u_fogParams.z) / (u_fogParams.z - u_fogParams.y), 0.0, 1.0))
        : 1.0;
}
