#version 450

layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uPrepassScalars[72];
};

layout(location = 0) in vec3 vViewNormal;
layout(location = 1) in float vViewDepth;

layout(location = 0) out vec4 FragTarget0;
layout(location = 1) out vec4 FragTarget1;

vec4 cnaPackDepth(float value)
{
    const vec4 shift = vec4(16581375.0, 65025.0, 255.0, 1.0);
    const vec4 mask = vec4(0.0, 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0);
    vec4 channels = fract(clamp(value, 0.0, 0.99999994) * shift);
    channels -= channels.xxyz * mask;
    return channels;
}

void main()
{
    vec4 depthOut = uPrepassScalars[1] >= 0.5
                  ? cnaPackDepth(vViewDepth)
                  : vec4(vViewDepth, vViewDepth, vViewDepth, 1.0);
    vec4 normalOut = vec4(vViewNormal * 0.5 + 0.5, uPrepassScalars[3]);
    int outputMode = int(uPrepassScalars[2] + 0.5);
    if (outputMode == 1)
    {
        FragTarget0 = depthOut;
        FragTarget1 = depthOut;
    }
    else if (outputMode == 2)
    {
        FragTarget0 = normalOut;
        FragTarget1 = normalOut;
    }
    else
    {
        FragTarget0 = depthOut;
        FragTarget1 = normalOut;
    }
}
