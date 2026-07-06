$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor,  0);
SAMPLER2D(s_texColor2, 1);

void main()
{
    vec4 base = texture2D(s_texColor, v_texcoord0);
    base.rgb *= 2.0;
    gl_FragColor = base
                 * texture2D(s_texColor2, v_texcoord0)
                 * v_color0;
}
