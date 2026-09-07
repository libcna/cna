#version 450

// plans/plan_vulkan.md VULKAN-222: AlphaTestEffect on an instanced draw.
//
// Instancing plus alpha test is the canonical use of both together -- foliage -- and it was broken
// here: the instanced route never set `useAlphaTest`, so a transparent texel drew opaque. Measured
// on both renderers by `instanced_textured_draw_test.cpp`: with CompareFunction::Greater and
// ReferenceAlpha=128 over a 2x1 texture whose right texel has alpha 0, EasyGL discarded the right
// half instanced and not; Vulkan discarded it only when the draw was NOT instanced.
//
// A copy of alpha_test3d.vert.glsl with the per-instance columns added. It keeps that shader's
// PUSH-CONSTANT layout exactly -- so `FillAlphaTestPushConst` feeds it unchanged -- and its
// pipeline keeps `pipelineLayoutAlphaTest3D_`, so the descriptor set, the fragment stage and the
// replay branch are all the existing ones. That is why this variant needed no new plumbing, unlike
// the lit and dual-texture shapes still open in VULKAN-218.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

// Per-instance world matrix (binding = 1, VK_VERTEX_INPUT_RATE_INSTANCE, stride = 64), at the same
// locations every other instanced program in this renderer uses.
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragFogFactor;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec4  alphaTestParams;
    float vertexColorEnabled;
    float fogColorR;
    float fogColorG;
    float fogColorB;
    vec4  fogVector;
} pc;

void main() {
    mat4 instWorld = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    // VULKAN-219: the instance matrix applies INSIDE the effect's own world transform, which
    // pc.mvp already carries. `objPos` is the instance-transformed object-space position, and the
    // fog term dots THAT rather than the raw vertex -- otherwise every instance of a mesh would be
    // fogged as if it stood at the origin.
    vec4 objPos = instWorld * vec4(inPos, 1.0);
    vec4 pos = pc.mvp * objPos;
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV   = inUV;
    // plan_vulkan.md VULKAN-197 (F-36): D3D9 saturates oD0 before interpolation.
    fragTint = clamp(pc.diffuseColor, 0.0, 1.0);
    // REMED-GFX-010: view-space fog = 1 - saturate(dot(objectPos, fogVector)).
    fragFogFactor = 1.0 - clamp(dot(objPos, pc.fogVector), 0.0, 1.0);
}
