#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uLutSampler;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uColorGradeParams;
} pc;

vec3 cnaSampleSlice(vec3 colour, float slice)
{
    float lutSize = pc.uColorGradeParams.x;
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
    float last = pc.uColorGradeParams.x - 1.0;
    float blue = colour.b * last;
    float lower = floor(blue);
    float upper = min(lower + 1.0, last);
    vec3 graded = mix(cnaSampleSlice(colour, lower),
                      cnaSampleSlice(colour, upper),
                      blue - lower);
    FragColor = vec4(mix(source.rgb, graded, pc.uColorGradeParams.y), source.a) * SpriteColor;
}
