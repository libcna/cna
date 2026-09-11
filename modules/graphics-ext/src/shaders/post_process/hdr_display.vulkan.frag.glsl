#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uHdrDisplayParams;
} pc;

const float kM1 = 0.1593017578125;
const float kM2 = 78.84375;
const float kC1 = 0.8359375;
const float kC2 = 18.8515625;
const float kC3 = 18.6875;

vec3 cnaEncodePq(vec3 nits)
{
    vec3 l = clamp(nits / 10000.0, 0.0, 1.0);
    vec3 p = pow(l, vec3(kM1));
    return pow((kC1 + kC2 * p) / (1.0 + kC3 * p), vec3(kM2));
}

vec3 cnaRec709ToRec2020(vec3 c)
{
    return vec3(
        dot(c, vec3(0.6274039, 0.3292830, 0.0433131)),
        dot(c, vec3(0.0690973, 0.9195404, 0.0113623)),
        dot(c, vec3(0.0163914, 0.0880133, 0.8955953)));
}

float cnaRollOff(float nits, float peak)
{
    return nits <= 0.0 ? 0.0 : peak * nits / (peak + nits);
}

void main()
{
    int space = int(pc.uHdrDisplayParams.x);
    vec4 source = texture(texture1, TexCoord);

    if (space == 0)
    {
        FragColor = source * SpriteColor;
        return;
    }

    if (space == 1)
    {
        FragColor = vec4(source.rgb * (pc.uHdrDisplayParams.y / 80.0), source.a)
                  * SpriteColor;
        return;
    }

    vec3 nits = source.rgb * pc.uHdrDisplayParams.y;
    nits = vec3(cnaRollOff(nits.r, pc.uHdrDisplayParams.z),
                cnaRollOff(nits.g, pc.uHdrDisplayParams.z),
                cnaRollOff(nits.b, pc.uHdrDisplayParams.z));
    FragColor = vec4(cnaEncodePq(cnaRec709ToRec2020(nits)), source.a) * SpriteColor;
}
