#version 450

// One full-target quad per layer: xy is the clip-space corner, z the array layer it samples.
layout(location = 0) in vec3 aPos;
layout(location = 0) out float vLayer;

void main()
{
    gl_Position = vec4(aPos.xy, 0.5, 1.0);
    vLayer = aPos.z;
}
