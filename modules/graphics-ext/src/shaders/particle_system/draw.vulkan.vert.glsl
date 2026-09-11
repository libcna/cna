#version 450

layout(std430, set = 2, binding = 7) readonly buffer CnaParticleBuffer
{
    vec4 cnaParticles[];
};

layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uParticleMatrices[72];
};
layout(set = 1, binding = 14, std140) uniform Vec3Array
{
    vec3 uParticleColours[72];
};
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uParticleScalars[72];
};

layout(location = 0) in vec3 aPos;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vViewDepth;

void main()
{
    int base = gl_InstanceIndex * 3;
    vec3 position = cnaParticles[base].xyz;
    vec4 state = cnaParticles[base + 2];
    float t = clamp(state.x / max(state.y, 1e-4), 0.0, 1.0);
    float size = mix(uParticleScalars[2], uParticleScalars[3], t);
    if (float(gl_InstanceIndex) >= uParticleScalars[4])
        size = 0.0;

    vec3 viewPosition = (uParticleMatrices[0] * vec4(position, 1.0)).xyz;
    viewPosition.xy += aPos.xy * size;
    gl_Position = uParticleMatrices[1] * vec4(viewPosition, 1.0);
    vTexCoord = aPos.xy + 0.5;
    vColor = vec4(mix(uParticleColours[0], uParticleColours[1], t),
                  mix(uParticleScalars[0], uParticleScalars[1], t));
    vViewDepth = -viewPosition.z;
}
