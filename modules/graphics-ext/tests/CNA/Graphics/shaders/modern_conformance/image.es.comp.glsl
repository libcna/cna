#version 310 es
precision highp float;
precision highp int;

// Writes a coordinate pattern into an rgba8 storage image: (x, y, x ^ y, 255) as bytes.
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) writeonly uniform highp image2D uImage;

void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    imageStore(uImage, p, vec4(float(p.x), float(p.y), float(p.x ^ p.y), 255.0) / 255.0);
}
