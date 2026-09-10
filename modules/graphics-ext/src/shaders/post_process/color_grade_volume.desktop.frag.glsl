#version 330 core
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler3D uLutVolume;
uniform vec4 uColorGradeParams;

vec3 cnaLutFetch(ivec3 index)
{
    return texelFetch(uLutVolume, index, 0).rgb;
}

vec3 cnaLutTrilinear(vec3 colour)
{
    float last = uColorGradeParams.x - 1.0;
    vec3 p = clamp(colour, 0.0, 1.0) * last;
    ivec3 i0 = ivec3(floor(p));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(int(last)));
    vec3 f = p - vec3(i0);
    vec3 c000 = cnaLutFetch(ivec3(i0.x, i0.y, i0.z));
    vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
    vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
    vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
    vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
    vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
    vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
    vec3 c111 = cnaLutFetch(ivec3(i1.x, i1.y, i1.z));
    return mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
               mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
}

vec3 cnaLutTetrahedral(vec3 colour)
{
    float last = uColorGradeParams.x - 1.0;
    vec3 p = clamp(colour, 0.0, 1.0) * last;
    ivec3 i0 = ivec3(floor(p));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(int(last)));
    vec3 f = p - vec3(i0);
    vec3 c000 = cnaLutFetch(ivec3(i0.x, i0.y, i0.z));
    vec3 c111 = cnaLutFetch(ivec3(i1.x, i1.y, i1.z));
    if (f.x > f.y)
    {
        if (f.y > f.z)
        {
            vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
            vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
            return c000 + f.x * (c100 - c000) + f.y * (c110 - c100)
                        + f.z * (c111 - c110);
        }
        if (f.x > f.z)
        {
            vec3 c100 = cnaLutFetch(ivec3(i1.x, i0.y, i0.z));
            vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
            return c000 + f.x * (c100 - c000) + f.z * (c101 - c100)
                        + f.y * (c111 - c101);
        }
        vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
        vec3 c101 = cnaLutFetch(ivec3(i1.x, i0.y, i1.z));
        return c000 + f.z * (c001 - c000) + f.x * (c101 - c001)
                    + f.y * (c111 - c101);
    }
    if (f.z > f.y)
    {
        vec3 c001 = cnaLutFetch(ivec3(i0.x, i0.y, i1.z));
        vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
        return c000 + f.z * (c001 - c000) + f.y * (c011 - c001)
                    + f.x * (c111 - c011);
    }
    if (f.z > f.x)
    {
        vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
        vec3 c011 = cnaLutFetch(ivec3(i0.x, i1.y, i1.z));
        return c000 + f.y * (c010 - c000) + f.z * (c011 - c010)
                    + f.x * (c111 - c011);
    }
    vec3 c010 = cnaLutFetch(ivec3(i0.x, i1.y, i0.z));
    vec3 c110 = cnaLutFetch(ivec3(i1.x, i1.y, i0.z));
    return c000 + f.y * (c010 - c000) + f.x * (c110 - c010)
                + f.z * (c111 - c110);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec3 colour = clamp(source.rgb, 0.0, 1.0);
    vec3 graded = uColorGradeParams.z >= 0.5
                ? cnaLutTetrahedral(colour) : cnaLutTrilinear(colour);
    FragColor = vec4(mix(source.rgb, graded, uColorGradeParams.y), source.a) * SpriteColor;
}
