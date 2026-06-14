#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec4  fragTint;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform PC {
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

    vec4 color;
    if (pc.lightingEnabled > 0.5) {
        vec3 N  = normalize(fragNormal);
        // light0Dir points FROM the light, so negate for dot with N.
        float NdotL = max(dot(N, -normalize(pc.light0Dir)), 0.0);
        vec3  lit   = pc.ambientColor + NdotL * pc.light0Diffuse;
        color = vec4(lit, 1.0) * fragTint * tex;
    } else {
        color = fragTint * tex;
    }
    outColor = color;
}
