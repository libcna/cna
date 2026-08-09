// SPDX-License-Identifier: MS-PL
// SpriteBatch fragment shader, OpenGL flavour. Desktop GLSL has no separate texture and sampler
// objects, so the pair declared at bindings 2 and 3 in sprite2d.frag.glsl collapses into the
// single combined sampler2D below; the pipeline layout's combined-texture-sampler entry is what
// keeps the two flavours bound to the same slot. See the vertex shader for why every binding and
// location here is explicit rather than matched by name.

#version 450 core

layout(binding = 2) uniform sampler2D colorMap;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(colorMap, vTexCoord) * vColor;
}
