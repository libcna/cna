// SPDX-License-Identifier: MS-PL
//
// plan_sokol.md SOKOL-14: the Sokol backend's built-in SpriteBatch program, written in
// sokol-shdc's annotated GLSL (Vulkan syntax -- separate texture and sampler uniforms).
// Compiled offline by compile_shaders.py into sokol_shaders.hpp, which is checked in.
//
// Vertex layout matches SokolSpriteBatchBackend::Vertex exactly: vec2 position, vec2 texcoord0,
// vec4 color0 (32 bytes, tightly packed). `mvp` is the SpriteBatch transform matrix multiplied
// by CreateOrthographicOffCenter(0, viewportW, viewportH, 0, -1, 1), so vertex positions are
// supplied in XNA's own top-left-origin pixel space.

@module cna

@vs sprite_vs
layout(binding = 0) uniform sprite_vs_params {
    mat4 mvp;
};

in vec2 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 tint;

void main() {
    gl_Position = mvp * vec4(position, 0.0, 1.0);
    uv = texcoord0;
    tint = color0;
}
@end

@fs sprite_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;

in vec2 uv;
in vec4 tint;

out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * tint;
}
@end

@program sprite sprite_vs sprite_fs
