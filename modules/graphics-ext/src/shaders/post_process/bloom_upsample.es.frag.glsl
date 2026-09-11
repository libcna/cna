#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uSmallerSampler;
uniform vec4 uBloomParams;
uniform vec4 uRtFlipV;

vec2 cnaSecondaryUv(vec2 primaryUv)
{
    vec2 logicalUv = vec2(primaryUv.x, mix(primaryUv.y, 1.0 - primaryUv.y, uRtFlipV.x));
    return vec2(logicalUv.x, mix(logicalUv.y, 1.0 - logicalUv.y, uRtFlipV.y));
}

void main()
{
    vec2 secondaryUv = cnaSecondaryUv(TexCoord);
    vec2 smallerTexel = uBloomParams.xy;
    vec3 larger = texture(texture1, TexCoord).rgb;
    vec3 smaller;
    if (uBloomParams.z < 0.5)
    {
        smaller = texture(uSmallerSampler, secondaryUv).rgb;
    }
    else
    {
        vec2 halfTexel = smallerTexel * 0.5;
        smaller  = texture(uSmallerSampler, secondaryUv + vec2(-halfTexel.x, -halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, secondaryUv + vec2( halfTexel.x, -halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, secondaryUv + vec2(-halfTexel.x,  halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, secondaryUv + vec2( halfTexel.x,  halfTexel.y)).rgb;
        smaller *= 0.25;
    }
    FragColor = vec4(larger + smaller, 1.0) * SpriteColor;
}
