#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragFog;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
    // Premultiplied fog uses the completed fragment alpha; fragFog.a is the keep factor.
    outColor.rgb = mix(fragFog.rgb * outColor.a, outColor.rgb, fragFog.a);
}
