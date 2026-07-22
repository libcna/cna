$input v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB

#include <bgfx_shader.sh>

// Task 1104: per-vertex-lit sibling of fs_lit_textured3d.sc -- lighting (v_litRGB/v_specularRGB)
// arrives already Gouraud-interpolated from the vertex stage instead of being computed here from
// a per-fragment normal/eye vector. Non-lighting math (texture sample, EmissiveColor add, fog) is
// otherwise identical to the per-pixel-lit sibling.

SAMPLER2D(s_texColor, 0);
uniform vec4 u_fogColor;
uniform vec4 u_emissiveColor;
uniform vec4 u_rtFlipV;

// REMED-GFX-078: flip V for a render-target color source (bottom-up FBO on originBottomLeft
// renderers -- see REMED-GFX-067); flip==0 leaves ordinary-texture sampling byte-identical.
vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

void main()
{
    vec4 tex = texture2D(s_texColor, rtFlipUV(v_texcoord0, u_rtFlipV.x));
    // Matches fs_lit_textured3d.sc's own litColor formula exactly, just with v_litRGB already
    // computed (not re-evaluated) here.
    vec3 litColor = v_color0.rgb * v_litRGB + u_emissiveColor.xyz;
    gl_FragColor = tex * vec4(litColor, v_color0.a);
    gl_FragColor.rgb += v_specularRGB * gl_FragColor.a;
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
