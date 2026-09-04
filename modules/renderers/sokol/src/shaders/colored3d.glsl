// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-20: the Sokol renderer's vertex-coloured 3D program, written in sokol-shdc's
// annotated GLSL. Compiled offline by compile_shaders.py into sokol_shaders.hpp, which is
// checked in.
//
// `color0` is deliberately the second attribute so a vertex declaration without a Color element
// can simply omit it: sokol_gfx only requires a pipeline's attribute slots to be continuous, and
// an unbound GL attribute reads its generic default. `flags.x` (BasicEffect.VertexColorEnabled)
// is 0 in exactly that case, which multiplies the unread attribute out of the result.

@module cna

@vs colored3d_vs
layout(binding = 0) uniform colored3d_vs_params {
    mat4 mvp;
    vec4 diffuse;
    // x = VertexColorEnabled (0 or 1); y, z, w reserved for the lighting phase (SOKOL-21).
    vec4 flags;
};

in vec3 position;
in vec4 color0;

out vec4 tint;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    tint = mix(vec4(1.0), color0, flags.x) * diffuse;
}
@end

@fs colored3d_fs
in vec4 tint;

out vec4 frag_color;

void main() {
    frag_color = tint;
}
@end

@program colored3d colored3d_vs colored3d_fs
