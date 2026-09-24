#version 300 es
precision highp float;
precision highp sampler2DArray;

in float vLayer;
out vec4 FragColor;

uniform sampler2DArray uArray;

void main()
{
    // Every layer is one solid colour, so the sample point inside it does not matter -- and nor
    // does any renderer's clip-space Y convention.
    FragColor = texture(uArray, vec3(0.5, 0.5, vLayer));
}
