#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uLutSampler;
uniform vec4 uColorGradeParams;

vec3 cnaSampleSlice(vec3 colour, float slice)
{
    float lutSize = uColorGradeParams.x;
    float sliceWidth = 1.0 / lutSize;
    float texelWidth = sliceWidth / lutSize;
    float u = slice * sliceWidth + 0.5 * texelWidth
            + colour.r * texelWidth * (lutSize - 1.0);
    float v = 0.5 / lutSize + colour.g * (lutSize - 1.0) / lutSize;
    return texture(uLutSampler, vec2(u, v)).rgb;
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec3 colour = clamp(source.rgb, 0.0, 1.0);
    float last = uColorGradeParams.x - 1.0;
    float blue = colour.b * last;
    float lower = floor(blue);
    float upper = min(lower + 1.0, last);
    vec3 graded = mix(cnaSampleSlice(colour, lower),
                      cnaSampleSlice(colour, upper),
                      blue - lower);
    FragColor = vec4(mix(source.rgb, graded, uColorGradeParams.y), source.a) * SpriteColor;
}
