#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uCrtParams;
    float uMaskType;
} pc;

vec2 applyCurvature(vec2 uv)
{
    vec2 cc = uv - 0.5;
    float dist = dot(cc, cc) * pc.uCrtParams.y;
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

    vec2 easyGlFragCoord = vec2(gl_FragCoord.x, pc.viewportSize.y - gl_FragCoord.y);
    float rowParity = mod(floor(easyGlFragCoord.y), 2.0);
    rgb *= mix(1.0, 1.0 - pc.uCrtParams.x, rowParity);

    int maskType = int(pc.uMaskType);
    if (maskType != 0)
    {
        float colBase = floor(easyGlFragCoord.x);
        if (maskType == 2)
        {
            float rowGroup = mod(floor(easyGlFragCoord.y / 2.0), 2.0);
            colBase += rowGroup * 1.5;
        }
        float col = mod(colBase, 3.0);
        vec3 mask = vec3(1.0 - pc.uCrtParams.w);
        if (col < 1.0) mask.r = 1.0;
        else if (col < 2.0) mask.g = 1.0;
        else mask.b = 1.0;
        rgb *= mask;
    }

    vec2 vc = TexCoord - 0.5;
    float vignette = 1.0 - pc.uCrtParams.z * dot(vc, vc) * 2.0;
    rgb *= clamp(vignette, 0.0, 1.0);

    FragColor = vec4(rgb, texColor.a);
}
