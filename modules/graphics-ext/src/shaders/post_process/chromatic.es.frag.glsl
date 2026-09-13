#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uStrength;

void main()
{
    vec2 fromCentre = TexCoord - vec2(0.5);
    vec2 redUv = vec2(0.5) + fromCentre * (1.0 + uStrength);
    vec2 blueUv = vec2(0.5) + fromCentre * (1.0 - uStrength);
    FragColor = vec4(texture(texture1, redUv).r,
                     texture(texture1, TexCoord).g,
                     texture(texture1, blueUv).b,
                     texture(texture1, TexCoord).a) * SpriteColor;
}
