#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uLensFlareParams;
} pc;

vec3 cnaBright(vec2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(pc.uLensFlareParams.x), vec3(0.0));
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 toCentre = vec2(0.5) - TexCoord;

    vec3 ghosts = vec3(0.0);
    for (int i = 1; i <= 8; ++i)
    {
        if (i > int(pc.uLensFlareParams.w)) break;
        vec2 uv = TexCoord + toCentre * (1.0 + float(i) * pc.uLensFlareParams.z);
        ghosts += cnaBright(uv) / float(i);
    }

    FragColor = vec4(source.rgb + ghosts * pc.uLensFlareParams.y, source.a)
              * SpriteColor;
}
