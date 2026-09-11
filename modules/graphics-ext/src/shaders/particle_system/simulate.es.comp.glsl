#version 310 es
precision highp float;
precision highp int;

layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer CnaParticles
{
    vec4 cnaParticles[];
};

layout(std140, binding = 1) uniform ParticleSimulationParameters
{
    vec4 uSimulation0; // active count, elapsed, cone angle, speed
    vec4 uSimulation1; // speed variance, lifetime, lifetime variance, drag
    vec4 uOrigin;
    vec4 uDirection;
    vec4 uGravity;
};

uint cnaParticleHash(uint x)
{
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u; return x;
}

float cnaParticleRandom(uint seed)
{
    return float(cnaParticleHash(seed) & 0x00ffffffu) / 16777216.0;
}

vec3 cnaNormaliseOr(vec3 value, vec3 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-6 ? value / valueLength : fallbackValue;
}

void cnaSpawn(uint index, uint generation, out vec3 position, out vec3 velocity,
              out float lifetime)
{
    uint seed = cnaParticleHash(index * 747796405u + generation * 2891336453u);
    float u = cnaParticleRandom(seed);
    float v = cnaParticleRandom(seed + 1u);
    float w = cnaParticleRandom(seed + 2u);
    float x = cnaParticleRandom(seed + 3u);

    float cosTheta = 1.0 + (cos(uSimulation0.z) - 1.0) * u;
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 6.28318530718 * v;

    vec3 axis = cnaNormaliseOr(uDirection.xyz, vec3(0.0, 1.0, 0.0));
    vec3 helper = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = cnaNormaliseOr(cross(helper, axis), vec3(1.0, 0.0, 0.0));
    vec3 up = cross(axis, right);
    vec3 direction = axis * cosTheta
                   + (right * cos(phi) + up * sin(phi)) * sinTheta;

    float speed = uSimulation0.w
                * (1.0 + uSimulation1.x * (w * 2.0 - 1.0));
    position = uOrigin.xyz;
    velocity = direction * speed;
    lifetime = max(uSimulation1.y
                       * (1.0 + uSimulation1.z * (x * 2.0 - 1.0)),
                   1e-3);
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    uint activeCount = uint(max(uSimulation0.x, 0.0) + 0.5);
    if (index >= activeCount)
        return;

    uint base = index * 3u;
    vec3 position = cnaParticles[base].xyz;
    vec3 velocity = cnaParticles[base + 1u].xyz;
    vec4 state = cnaParticles[base + 2u];
    float age = state.x;
    float lifetime = state.y;
    float generation = state.w;

    age += uSimulation0.y;
    if (age >= lifetime)
    {
        generation += 1.0;
        age -= lifetime;
        cnaSpawn(index, uint(generation), position, velocity, lifetime);
    }

    velocity += uGravity.xyz * uSimulation0.y;
    velocity -= velocity * min(uSimulation1.w * uSimulation0.y, 1.0);
    position += velocity * uSimulation0.y;

    cnaParticles[base] = vec4(position, 0.0);
    cnaParticles[base + 1u] = vec4(velocity, 0.0);
    cnaParticles[base + 2u] = vec4(age, lifetime, state.z, generation);
}
