#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform vec4 uCrtParams;
uniform int uMaskType;

vec2 applyCurvature(vec2 uv)
{
    vec2 cc = uv - 0.5;
    float dist = dot(cc, cc) * uCrtParams.y;
    return uv + cc * dist;
}

void main()
{
    vec2 uv = applyCurvature(TexCoord);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 texColor = texture(texture1, uv) * SpriteColor;
    vec3 rgb = texColor.rgb;

    float rowParity = mod(floor(gl_FragCoord.y), 2.0);
    rgb *= mix(1.0, 1.0 - uCrtParams.x, rowParity);

    if (uMaskType != 0)
    {
        float colBase = floor(gl_FragCoord.x);
        if (uMaskType == 2)
        {
            float rowGroup = mod(floor(gl_FragCoord.y / 2.0), 2.0);
            colBase += rowGroup * 1.5;
        }
        float col = mod(colBase, 3.0);
        vec3 mask = vec3(1.0 - uCrtParams.w);
        if (col < 1.0) mask.r = 1.0;
        else if (col < 2.0) mask.g = 1.0;
        else mask.b = 1.0;
        rgb *= mask;
    }

    vec2 vc = TexCoord - 0.5;
    float vignette = 1.0 - uCrtParams.z * dot(vc, vc) * 2.0;
    rgb *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(rgb, texColor.a);
}
