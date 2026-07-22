$input v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB, v_vertexColor0

#include <bgfx_shader.sh>

// Task 1104: per-vertex-lit sibling of fs_skinned3d.sc -- lighting (v_litRGB/v_specularRGB)
// arrives already Gouraud-interpolated from the vertex stage. Non-lighting math (texture sample,
// fog) is otherwise identical to the per-pixel-lit sibling.

SAMPLER2D(s_texColor, 0);
uniform vec4 u_fogColor;
// CNB-67 (Phase 13C) Bgfx port: see fs_skinned3d.sc's identical comment.
uniform vec4 u_vertexColorEnabled3D;
uniform vec4 u_rtFlipV;

// REMED-GFX-078: flip V for a render-target color source (bottom-up FBO on originBottomLeft
// renderers -- see REMED-GFX-067); flip==0 leaves ordinary-texture sampling byte-identical.
vec2 rtFlipUV(vec2 uv, float flip) { return vec2(uv.x, mix(uv.y, 1.0 - uv.y, flip)); }

void main()
{
    vec4 tex = texture2D(s_texColor, rtFlipUV(v_texcoord0, u_rtFlipV.x));
    vec4 vc = mix(vec4(1.0, 1.0, 1.0, 1.0), v_vertexColor0, u_vertexColorEnabled3D.x);
    // REMED-GFX-008: v_litRGB already bakes in DiffuseColor (lightSum*DiffuseColor + EmissiveColor),
    // so it must NOT be multiplied by v_color0 (=DiffuseColor) again; v_color0.a still carries alpha.
    gl_FragColor = vec4(tex.rgb * v_litRGB, tex.a * v_color0.a);
    gl_FragColor.a *= vc.a;
    gl_FragColor.rgb += v_specularRGB * gl_FragColor.a;
    // See fs_skinned3d.sc's identical comment: vertex color modulates the whole combined
    // diffuse+specular output, applied after the specular add.
    gl_FragColor.rgb *= vc.rgb;
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
