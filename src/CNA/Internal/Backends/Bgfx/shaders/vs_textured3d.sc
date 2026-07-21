$input a_position, a_texcoord0
$output v_texcoord0, v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

void main()
{
    gl_Position  = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
    // REMED-GFX-005: fog factor from raw object-space Z, corrected to EasyGL's Task-1111 form
    // (the prior Task-888 (FogEnd-z) form was the mirror of EasyGL's since-fixed formula, NOT
    // a match to it). u_fogParams = FNA fog vector (REMED-GFX-010): dot(vec4(pos,1), u_fogParams) = fogFactor.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(a_position, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
