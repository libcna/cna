$input v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB

#include <bgfx_shader.sh>

// Task 1104: per-vertex-lit sibling of fs_skinned3d.sc -- lighting (v_litRGB/v_specularRGB)
// arrives already Gouraud-interpolated from the vertex stage. Non-lighting math (texture sample,
// fog) is otherwise identical to the per-pixel-lit sibling.

SAMPLER2D(s_texColor, 0);
uniform vec4 u_fogColor;

void main()
{
    vec4 tex = texture2D(s_texColor, v_texcoord0);
    // Matches fs_skinned3d.sc's own formula exactly: tex * diffuse * finalLight, then specular,
    // then fog -- v_litRGB already IS finalLight here.
    gl_FragColor = tex * v_color0 * vec4(v_litRGB, 1.0);
    gl_FragColor.rgb += v_specularRGB * gl_FragColor.a;
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
