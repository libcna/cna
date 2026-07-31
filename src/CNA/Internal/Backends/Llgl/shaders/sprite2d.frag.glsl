// SPDX-License-Identifier: MS-PL
// SpriteBatch fragment shader, Vulkan flavour (SPIR-V). See sprite2d.gl.frag.glsl for the OpenGL
// flavour. Vulkan keeps texture and sampler as separate objects; the pipeline layout declares them
// at bindings 2 and 3 and pairs them back up for GLSL through a combined-texture-sampler mapping.

#version 450

layout(binding = 2) uniform texture2D colorMap;
layout(binding = 3) uniform sampler samplerState;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(sampler2D(colorMap, samplerState), vTexCoord) * vColor;
}
