$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform vec4 u_diffuseColor;
uniform vec4 u_vertexColorEnabled3D;

void main()
{
    gl_Position  = mul(u_wvp, vec4(a_position, 1.0));
    v_texcoord0  = a_texcoord0;
    // BasicEffect texture+vertex-color path (Task 367): mirrors FNA's ComputeCommonVSOutput()
    // (vout.Diffuse = DiffuseColor) plus the optional `vout.Diffuse *= vin.Color` vertex-color
    // multiply gated by VertexColorEnabled — same formula as vs_colored3d.sc's no-texture path.
    vec4 vc = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 : vec4(1.0, 1.0, 1.0, 1.0);
    v_color0 = vc * u_diffuseColor;
}
