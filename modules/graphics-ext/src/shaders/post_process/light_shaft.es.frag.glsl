#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 uLightShaftParams;
uniform float uDecay;
uniform vec4 uRtFlipV;

const int kStepCount = 24;

vec3 cnaBright(vec2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(uLightShaftParams.z), vec3(0.0));
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    // SpriteBatch already mirrors the quad UVs for an OpenGL render-target source. Put the
    // screen-space light in that same sampled-texture coordinate system before walking to it.
    vec2 lightPosition = vec2(uLightShaftParams.x,
        mix(uLightShaftParams.y, 1.0 - uLightShaftParams.y, uRtFlipV.x));
    vec2 outside = max(vec2(0.0) - lightPosition, lightPosition - vec2(1.0));
    float offScreen = max(max(outside.x, outside.y), 0.0);
    float reach = clamp(1.0 - offScreen * 2.0, 0.0, 1.0);
    if (reach <= 0.0)
    {
        FragColor = source * SpriteColor;
        return;
    }

    vec2 step = (lightPosition - TexCoord) / float(kStepCount);
    vec3 gathered = vec3(0.0);
    float weight = 1.0;
    vec2 uv = TexCoord;
    for (int i = 0; i < kStepCount; ++i)
    {
        uv += step;
        gathered += cnaBright(uv) * weight;
        weight *= uDecay;
    }

    FragColor = vec4(source.rgb + gathered * (uLightShaftParams.w / float(kStepCount)) * reach,
                     source.a) * SpriteColor;
}
