#version 330 core
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 uTonemapParams;

vec3 reinhard(vec3 c) { return c / (1.0 + c); }

vec3 filmic(vec3 c)
{
    vec3 x = max(vec3(0.0), c - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

vec3 aces(vec3 c)
{
    const float a = 2.51, b = 0.03, cc = 2.43, d = 0.59, e = 0.14;
    return clamp((c * (a * c + b)) / (c * (cc * c + d) + e), 0.0, 1.0);
}

vec3 uncharted2Curve(vec3 x)
{
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 uncharted2(vec3 c)
{
    const float W = 11.2;
    return uncharted2Curve(c) / uncharted2Curve(vec3(W));
}

float cnaDitherHash(vec2 position)
{
    return fract(sin(dot(position, vec2(12.9898, 78.233))) * 43758.5453);
}

float cnaTriangularDither(vec2 position)
{
    return cnaDitherHash(position) - cnaDitherHash(position + vec2(17.0, 23.0));
}

void main()
{
    int mode = int(uTonemapParams.x);
    vec4 source = texture(texture1, TexCoord);
    vec3 color = source.rgb * uTonemapParams.y;

    if (mode == 1)      color = reinhard(color);
    else if (mode == 2) color = filmic(color);
    else if (mode == 3) color = aces(color);
    else if (mode == 4) color = uncharted2(color);

    color = clamp(color, 0.0, 1.0);
    if (mode != 2) color = pow(color, vec3(uTonemapParams.z));
    if (uTonemapParams.w > 0.0)
        color += vec3(cnaTriangularDither(gl_FragCoord.xy) * uTonemapParams.w);

    FragColor = vec4(color, source.a) * SpriteColor;
}
