#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
out float vDistance;
void main()
{
    vec4 lightSpace = uLightViewProjection * uWorld * vec4(aPosition, 1.0);
    gl_Position = lightSpace;
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
