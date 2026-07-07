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
    // Task 887: stride-24 (VertexPositionColorTexture) variant of vs_alpha_test3d, mirroring
    // vs_colored_textured3d.sc's VertexColorEnabled-gated multiply.
    vec4 vc = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 : vec4(1.0, 1.0, 1.0, 1.0);
    v_color0 = vc * u_diffuseColor;
}
