#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uLightShaftParams;
    float uDecay;
} pc;

const int kStepCount = 24;

vec3 cnaBright(vec2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(pc.uLightShaftParams.z), vec3(0.0));
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 lightPosition = pc.uLightShaftParams.xy;
    vec2 outside = max(vec2(0.0) - lightPosition, lightPosition - vec2(1.0));
    float offScreen = max(max(outside.x, outside.y), 0.0);
    float reach = clamp(1.0 - offScreen * 2.0, 0.0, 1.0);
    if (reach <= 0.0)
    {
        FragColor = source * SpriteColor;
        return;
    }

    vec2 step = (lightPosition - TexCoord) / float(kStepCount);
    vec3 gathered = vec3(0.0);
    float weight = 1.0;
    vec2 uv = TexCoord;
    for (int i = 0; i < kStepCount; ++i)
    {
        uv += step;
        gathered += cnaBright(uv) * weight;
        weight *= pc.uDecay;
    }

    FragColor = vec4(source.rgb
                         + gathered * (pc.uLightShaftParams.w / float(kStepCount)) * reach,
                     source.a) * SpriteColor;
}
