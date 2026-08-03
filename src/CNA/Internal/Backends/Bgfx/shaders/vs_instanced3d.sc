$input a_position, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_color0

#include <bgfx_shader.sh>

uniform mat4 u_vp;
uniform vec4 u_depthBias;
uniform vec4 u_diffuseColor;
uniform vec4 u_vertexColorEnabled3D;

void main()
{
    mat4 world  = mat4(i_data0, i_data1, i_data2, i_data3);
    gl_Position = mul(u_vp, mul(world, vec4(a_position, 1.0)));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    // REMED-GFX-215: the identical BasicEffect colour calculation vs_colored3d.sc runs, because
    // BasicEffect's shader index has no instancing term -- FNA's ComputeCommonVSOutput() opens with
    // `vout.Diffuse = DiffuseColor` and VSBasicVc adds exactly one line, `vout.Diffuse *=
    // vin.Color`, gated by VertexColorEnabled. This body previously read `v_color0 = a_color0`,
    // which dropped BOTH terms: DiffuseColor never arrived (no uniform was declared) and the gate
    // did not exist, so the two settings were indistinguishable. Invisible to a white-DiffuseColor
    // oracle, for which `COLOR0 * white == COLOR0`.
    vec4 vc     = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 : vec4(1.0, 1.0, 1.0, 1.0);
    v_color0    = vc * u_diffuseColor;
}
