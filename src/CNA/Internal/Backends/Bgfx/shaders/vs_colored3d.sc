$input a_position, a_color0
$output v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform vec4 u_vertexColorEnabled3D;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

void main()
{
    gl_Position = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation -- bgfx has no native polygon-offset
    // mechanism, so a scaled constant offset is added to clip-space Z (scaled by w so the
    // effect survives perspective divide as a roughly constant NDC-space offset).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    // BasicEffect no-texture path (Task 364): mirrors FNA's ComputeCommonVSOutput()
    // (vout.Diffuse = DiffuseColor) plus the optional `vout.Diffuse *= vin.Color`
    // vertex-color multiply gated by VertexColorEnabled.
    vec4 vc = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 : vec4(1.0, 1.0, 1.0, 1.0);
    v_color0 = vc * u_diffuseColor;
    // REMED-GFX-005: fog factor from raw object-space Z, corrected to EasyGL's Task-1111 form
    // (the prior Task-888 (FogEnd-z) form was the mirror of EasyGL's since-fixed formula, NOT
    // a match to it). u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? ((abs(u_fogParams.z - u_fogParams.y) < 1e-6)
            ? 0.0
            : clamp((a_position.z + u_fogParams.z) / (u_fogParams.z - u_fogParams.y), 0.0, 1.0))
        : 1.0;
}
