#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 uLensFlareParams;

vec3 cnaBright(vec2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(uLensFlareParams.x), vec3(0.0));
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 toCentre = vec2(0.5) - TexCoord;

    vec3 ghosts = vec3(0.0);
    for (int i = 1; i <= 8; ++i)
    {
        if (i > int(uLensFlareParams.w)) break;
        vec2 uv = TexCoord + toCentre * (1.0 + float(i) * uLensFlareParams.z);
        ghosts += cnaBright(uv) / float(i);
    }

    FragColor = vec4(source.rgb + ghosts * uLensFlareParams.y, source.a)
              * SpriteColor;
}
