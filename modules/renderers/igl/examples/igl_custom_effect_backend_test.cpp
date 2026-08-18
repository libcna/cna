// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-42/IGL-43: does the custom `ShaderEffect` path work on the VULKAN backend?
//
// Until now nothing answered that. The three tests that drive a custom effect
// (`Igl_ShaderEffectTexture3D`, `Igl_Instancing`, `Igl_SpriteBatchShaderEffect`) all run whatever
// backend the environment provides, which is OpenGL, and all three set effect PARAMETERS -- which
// design decision 8 refuses on Vulkan by name, because loose (non-block) uniforms do not exist in
// Vulkan GLSL and IGL's Vulkan encoder leaves `bindUniform` unimplemented. So on Vulkan they could
// never have got as far as reporting anything about the effect path itself.
//
// This test isolates the path from the parameter gap. It compiles, binds and draws with a custom
// effect that has NO uniforms at all, on BOTH backends, and asserts the same pixels from both:
//
//   * the effect compiles and reports itself valid,
//   * its vertex stage really runs (the geometry lands where only its own transform puts it),
//   * its fragment stage really runs (the colour is one only this shader can produce, and is not
//     the clear colour, not white, and not the vertex colour).
//
// The two sources differ because they must, and that is the finding this test pins rather than
// works around: SPIR-V requires an explicit `layout(location = ...)` on every user input and
// output including the varyings between stages, which desktop GLSL 4.10 does not, and the two
// backends do not accept the same `#version`. A single source cannot serve both -- llvmpipe here
// reports GL 4.5, so `#version 460` is not available on the OpenGL side either. An application
// targeting both IGL backends therefore has to supply two sources, and this test is the worked
// example of what each one has to look like.
//
// Deliberately uniform-free: parameters on Vulkan are IGL-43's own open item, and folding them in
// here would mean this test could not distinguish "the effect path is broken" from "parameters are
// not implemented".
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display, or no Vulkan WSI here).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    namespace Detail = CNA::Internal::Renderers::Igl::Detail;

    /// True when this process resolved IGL's Vulkan backend. The same non-throwing pre-window
    /// resolution the renderer's own descriptor uses, so the test and the device cannot disagree.
    [[nodiscard]] bool IsVulkanBackend()
    {
        return Detail::ResolveRendererBackendForWindow() == Detail::RendererBackend::Vulkan;
    }

    // Desktop GL: locations on vertex inputs and the fragment output are allowed but not required,
    // and a varying needs no location at all before GLSL 4.40.
    const char* kOpenGlVertSrc = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec4 aColor;
out vec4 vColor;
void main() {
    gl_Position = vec4(aPosition.xy * 0.5, 0.0, 1.0);
    vColor = aColor;
}
)";

    const char* kOpenGlFragSrc = R"(#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor.g, vColor.b, vColor.r, 1.0);
}
)";

    // Vulkan: every user input and output carries an explicit location, the varying included --
    // glslang rejects the shader outright otherwise ("SPIR-V requires location for user
    // input/output"). The attribute locations are IGL's own usage-to-slot table, the same one the
    // OpenGL source above names, so the vertex layout does not change between backends.
    const char* kVulkanVertSrc = R"(#version 460
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec4 aColor;
layout(location = 0) out vec4 vColor;
void main() {
    gl_Position = vec4(aPosition.xy * 0.5, 0.0, 1.0);
    vColor = aColor;
}
)";

    const char* kVulkanFragSrc = R"(#version 460
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(vColor.g, vColor.b, vColor.r, 1.0);
}
)";
}

class IglCustomEffectBackendTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const bool vulkan = IsVulkanBackend();
        std::printf("backend: %s\n", vulkan ? "Vulkan" : "OpenGL");

        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));

        ShaderEffect effect(device,
                            vulkan ? kVulkanVertSrc : kOpenGlVertSrc,
                            vulkan ? kVulkanFragSrc : kOpenGlFragSrc);
        if (!ExpectTrue("a custom ShaderEffect compiles on this backend", effect.IsEffectValid()))
            return;

        // Blue vertices, which the fragment stage rotates to green (rgb -> gbr). Neither the clear
        // colour, nor white, nor the vertex colour itself, so a pass cannot come from the shader
        // being ignored and some default path drawing instead.
        const Color vertexColor(static_cast<bytecs>(0), static_cast<bytecs>(0),
                                static_cast<bytecs>(255), static_cast<bytecs>(255));
        const std::vector<VertexPositionColor> vertices{
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), vertexColor),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), vertexColor),
            VertexPositionColor(Vector3(-1.0f, 1.0f, 0.0f), vertexColor),
            VertexPositionColor(Vector3(1.0f, 1.0f, 0.0f), vertexColor)};
        const std::vector<std::uint16_t> indices{0, 1, 2, 2, 1, 3};

        effect.Apply();
        device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices.data(),
                                         0, static_cast<int>(vertices.size()), indices.data(), 0,
                                         2);

        // The quad's own vertices span the whole surface; the vertex shader halves them, so it
        // covers the middle and leaves the border cleared. Checking both is what proves the VERTEX
        // stage ran -- a shader that merely passed the positions through would cover everything.
        ExpectPixel("the custom fragment stage rotated blue to green at the centre",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
        ExpectPixel("the custom vertex stage halved the quad, so the corner stays cleared",
                    Rectangle(2, 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
    }

public:
    IglCustomEffectBackendTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglCustomEffectBackendTest>();
}
