$input v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB, v_vertexColor0

#include <bgfx_shader.sh>

// Task 1104: per-vertex-lit sibling of fs_skinned3d.sc -- lighting (v_litRGB/v_specularRGB)
// arrives already Gouraud-interpolated from the vertex stage. Non-lighting math (texture sample,
// fog) is otherwise identical to the per-pixel-lit sibling.

SAMPLER2D(s_texColor, 0);
uniform vec4 u_fogColor;
// CNB-67 (Phase 13C) Bgfx port: see fs_skinned3d.sc's identical comment.
uniform vec4 u_vertexColorEnabled3D;

void main()
{
    vec4 tex = texture2D(s_texColor, v_texcoord0);
    vec4 vc = mix(vec4(1.0, 1.0, 1.0, 1.0), v_vertexColor0, u_vertexColorEnabled3D.x);
    // Matches fs_skinned3d.sc's own formula exactly: tex * diffuse * finalLight, then specular,
    // then fog -- v_litRGB already IS finalLight here.
    gl_FragColor = tex * v_color0 * vec4(v_litRGB, 1.0);
    gl_FragColor.a *= vc.a;
    gl_FragColor.rgb += v_specularRGB * gl_FragColor.a;
    // See fs_skinned3d.sc's identical comment: vertex color modulates the whole combined
    // diffuse+specular output, applied after the specular add.
    gl_FragColor.rgb *= vc.rgb;
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
