$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_color0, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform vec4 u_vertexColorEnabled3D;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

void main()
{
    gl_Position  = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    v_texcoord0  = a_texcoord0;
    // Task 889: stride-24 (VertexPositionColorTexture) variant of vs_dual_texture3d, mirroring
    // vs_alpha_test_colored3d.sc's VertexColorEnabled-gated multiply.
    vec4 vc = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 : vec4(1.0, 1.0, 1.0, 1.0);
    v_color0 = vc * u_diffuseColor;
    // Task 888: fog factor from raw object-space Z (matches EasyGL's established formula
    // exactly). u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z - u_fogParams.y, 1e-6), 0.0, 1.0)
        : 1.0;
}
