#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform sampler2D uVelocitySampler;
uniform mat4 uMotionMatrices[3];
uniform float uMotionScalars[6];

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uMotionScalars[5] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec2 cnaDecodeVelocity(vec4 channels)
{
    return (channels.xy - 0.5) * 2.0;
}

vec4 cnaGather(vec4 source, vec2 velocity, int sampleCount)
{
    float distance = length(velocity);
    if (distance > uMotionScalars[3])
        velocity *= uMotionScalars[3] / distance;

    vec3 sum = source.rgb;
    float weight = 1.0;
    for (int i = 1; i <= 16; ++i)
    {
        if (i >= sampleCount)
            break;
        vec2 uv = TexCoord - velocity * (float(i) / float(sampleCount - 1));
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            continue;
        sum += texture(texture1, uv).rgb;
        weight += 1.0;
    }
    return vec4(sum / weight, source.a);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    int sampleCount = int(uMotionScalars[4] + 0.5);

    if (uMotionScalars[0] > 0.5)
    {
        vec4 stored = texture(uVelocitySampler, TexCoord);
        if (stored.a < 0.5)
        {
            FragColor = cnaGather(source,
                                  cnaDecodeVelocity(stored) * uMotionScalars[2],
                                  sampleCount) * SpriteColor;
            return;
        }
    }

    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
    if (depth <= 0.0 || depth >= 0.999)
    {
        FragColor = source * SpriteColor;
        return;
    }

    vec4 clip = vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray = uMotionMatrices[0] * clip;
    vec3 direction = ray.xyz / ray.w;
    vec3 viewPosition = direction * (depth / max(-direction.z, 1e-6))
                      * uMotionScalars[1];
    vec4 world = uMotionMatrices[1] * vec4(viewPosition, 1.0);
    vec4 previousClip = uMotionMatrices[2] * world;
    if (previousClip.w <= 0.0)
    {
        FragColor = source * SpriteColor;
        return;
    }

    vec2 previousUv = (previousClip.xy / previousClip.w) * 0.5 + 0.5;
    vec2 velocity = (TexCoord - previousUv) * uMotionScalars[2];
    FragColor = cnaGather(source, velocity, sampleCount) * SpriteColor;
}
