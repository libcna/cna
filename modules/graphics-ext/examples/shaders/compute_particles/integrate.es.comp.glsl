#version 310 es
precision highp float;
precision highp int;

layout(local_size_x = 64) in;
struct Particle { vec4 position; vec4 velocity; };
layout(std430, binding = 0) buffer Particles { Particle particles[]; };
layout(std140, binding = 1) uniform IntegratorParameters { vec4 uIntegrator; };

void main()
{
    uint index = gl_GlobalInvocationID.x;
    uint count = uint(max(uIntegrator.x, 0.0) + 0.5);
    if (index >= count)
        return;
    float step = uIntegrator.y;
    vec3 velocity = particles[index].velocity.xyz + vec3(0.0, -9.81, 0.0) * step;
    vec3 position = particles[index].position.xyz + velocity * step;
    if (position.y < 0.0)
    {
        position.y = -position.y;
        velocity.y = -velocity.y * 0.5;
    }
    particles[index].position = vec4(position, particles[index].position.w);
    particles[index].velocity = vec4(velocity, 0.0);
}
