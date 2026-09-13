#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uParticleScalars[72];
};

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vViewDepth;
layout(location = 0) out vec4 FragColor;

float cnaUnpackDepth(vec4 channels)
{
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

void main()
{
    vec4 colour = texture(texture1, vTexCoord) * vColor;
    if (uParticleScalars[5] > 0.5 && uParticleScalars[6] > 0.0)
    {
        vec2 viewportSize = max(vec2(uParticleScalars[8], uParticleScalars[9]), vec2(1.0));
        vec2 uv = gl_FragCoord.xy / viewportSize;
        vec4 depthTexel = texture(uSceneDepth, uv);
        float linearDepth = mix(depthTexel.r, cnaUnpackDepth(depthTexel),
                                clamp(uParticleScalars[10], 0.0, 1.0));
        float behind = linearDepth * uParticleScalars[7];
        colour.a *= clamp((behind - vViewDepth) / uParticleScalars[6], 0.0, 1.0);
    }
    FragColor = colour;
}
