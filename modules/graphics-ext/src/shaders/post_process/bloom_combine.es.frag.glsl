#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uBloomSampler;
uniform vec4 uBloomParams;
uniform vec4 uRtFlipV;

vec2 cnaBloomUv(vec2 sceneUv)
{
    vec2 logicalUv = vec2(sceneUv.x, mix(sceneUv.y, 1.0 - sceneUv.y, uRtFlipV.x));
    return vec2(logicalUv.x, mix(logicalUv.y, 1.0 - logicalUv.y, uRtFlipV.y));
}

void main()
{
    vec4 scene = texture(texture1, TexCoord);
    vec3 bloom = texture(uBloomSampler, cnaBloomUv(TexCoord)).rgb;
    FragColor = vec4(scene.rgb + bloom * uBloomParams.x, scene.a) * SpriteColor;
}
