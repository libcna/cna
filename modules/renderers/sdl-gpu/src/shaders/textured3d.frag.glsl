#version 450

// Shared by both textured3d (stride 20) and colored_textured3d (stride 24) pipelines --
// functionally identical (tex * tint), only the vertex shader/vertex-input-state differ.
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in vec4 fragFog;    // REMED-GFX-009
layout(location = 0) out vec4 outColor;

// plans/plan_sdlgpu.md: fragment-stage sampled textures live in set 2, fragment-stage uniform buffers
// in set 3 (SDL_gpu's SPIR-V graphics-pipeline convention).
layout(set = 2, binding = 0) uniform sampler2D uTexture;

layout(set = 3, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, fragUV) : vec4(1.0);
    outColor = tex * fragTint;
    // REMED-GFX-009: blend toward FogColor (RGB only). fragFog.a = keep (1 no fog, 0 full fog).
    outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a);
}
