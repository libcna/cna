$input v_texcoord0, v_color0, v_fogFactor

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor,  0);
SAMPLER2D(s_texColor2, 1);
uniform vec4 u_fogColor;
uniform vec4 u_rtFlipV;

// REMED-GFX-078: per-slot V-flip for render-target color sources (bottom-up FBO on originBottomLeft
// renderers -- see REMED-GFX-067). The two layers flip INDEPENDENTLY (u_rtFlipV.x for slot 0,
// u_rtFlipV.y for slot 1) so an ordinary Texture2D on one slot and a RenderTarget2D on the other are
// each sampled upright. flip==0 leaves ordinary-texture sampling byte-identical.
vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

void main()
{
    vec4 base = texture2D(s_texColor, rtFlipUV(v_texcoord0, u_rtFlipV.x));
    base.rgb *= 2.0;
    vec4 color = base
               * texture2D(s_texColor2, rtFlipUV(v_texcoord0, u_rtFlipV.y))
               * v_color0;
    // Task 888: mix toward FogColor as v_fogFactor -> 0 (matches EasyGL's established formula).
    color.rgb = mix(u_fogColor.xyz, color.rgb, v_fogFactor);
    gl_FragColor = color;
}
