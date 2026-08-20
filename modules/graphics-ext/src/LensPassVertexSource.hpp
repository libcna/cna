// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics::detail {

    /// The fullscreen quad's vertex program, shared by the lens passes so three copies of the same
    /// eight lines cannot drift apart. Internal to `src/`: it is not part of the layer's API.
    inline constexpr const char* kLensVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
