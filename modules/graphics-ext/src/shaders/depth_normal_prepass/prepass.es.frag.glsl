#version 300 es
precision highp float;

in vec3 vViewNormal;
in float vViewDepth;
in vec4 vCurrentClip;
in vec4 vPreviousClip;

uniform float uPrepassScalars[4];

layout(location = 0) out vec4 FragTarget0;
layout(location = 1) out vec4 FragTarget1;
layout(location = 2) out vec4 FragTarget2;

vec4 cnaPackDepth(float value)
{
    const vec4 shift = vec4(16581375.0, 65025.0, 255.0, 1.0);
    const vec4 mask = vec4(0.0, 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0);
    vec4 channels = fract(clamp(value, 0.0, 0.99999994) * shift);
    channels -= channels.xxyz * mask;
    return channels;
}

vec4 cnaEncodeVelocity(vec2 velocityUv)
{
    return vec4(clamp(velocityUv * 0.5 + 0.5, 0.0, 1.0), 0.0, 0.0);
}

vec4 cnaNoVelocity()
{
    return vec4(0.5, 0.5, 0.0, 1.0);
}

vec4 cnaVelocityOut(vec4 currentClip, vec4 previousClip)
{
    if (currentClip.w <= 0.0 || previousClip.w <= 0.0)
        return cnaNoVelocity();
    vec2 currentUv = (currentClip.xy / currentClip.w) * 0.5 + 0.5;
    vec2 previousUv = (previousClip.xy / previousClip.w) * 0.5 + 0.5;
    return cnaEncodeVelocity(currentUv - previousUv);
}

void main()
{
    vec4 depthOut = uPrepassScalars[1] >= 0.5
                  ? cnaPackDepth(vViewDepth)
                  : vec4(vViewDepth, vViewDepth, vViewDepth, 1.0);
    vec4 normalOut = vec4(vViewNormal * 0.5 + 0.5, uPrepassScalars[3]);
    vec4 velocityOut = cnaVelocityOut(vCurrentClip, vPreviousClip);
    int outputMode = int(uPrepassScalars[2] + 0.5);
    if (outputMode == 1)
    {
        FragTarget0 = depthOut;
        FragTarget1 = depthOut;
        FragTarget2 = depthOut;
    }
    else if (outputMode == 2)
    {
        FragTarget0 = normalOut;
        FragTarget1 = normalOut;
        FragTarget2 = normalOut;
    }
    else if (outputMode == 3)
    {
        FragTarget0 = velocityOut;
        FragTarget1 = velocityOut;
        FragTarget2 = velocityOut;
    }
    else
    {
        FragTarget0 = depthOut;
        FragTarget1 = normalOut;
        FragTarget2 = velocityOut;
    }
}
