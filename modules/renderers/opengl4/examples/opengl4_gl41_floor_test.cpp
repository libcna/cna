// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0037: OpenGL4 on its 4.1 floor.
//
// Mesa normally grants this renderer's 4.1 core request a 4.6 context, so the floor is never what
// runs. This oracle is registered with MESA_GL_VERSION_OVERRIDE=4.1 (and GLSL 4.10) in its CTest
// environment, which makes the same driver hand out a real 4.1 core context, and asserts what the
// floor promises: the context is 4.1; compute -- a 4.3 feature -- is refused rather than claimed;
// texture arrays -- core since 3.0 -- are published and work, from upload and readback through a
// ShaderEffect sampling each layer; and a classic draw still reaches its target.
//
// Where the override is not honoured (a non-Mesa driver), the context is not 4.1 and the oracle
// skips (77) instead of asserting the floor on a context that is not it.

#ifndef CNA_CNAEXT
#include <cstdio>
int main()
{
    std::printf("SKIP: the engine layer is off in this build; Texture2DArray needs it\n");
    return 77;
}
#else

#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::Texture2DArray;
using CNA::Graphics::Texture2DArrayDescriptor;
using CNA::Graphics::Texture2DArrayUsage;
using CNA::Internal::Renderers::OpenGL4::OpenGL4Renderer;

namespace
{
    constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location = 0) in vec3 aPos;
out float vLayer;
void main() { gl_Position = vec4(aPos.xy, 0.0, 1.0); vLayer = aPos.z; }
)GLSL";

    constexpr const char* kFragment = R"GLSL(#version 410 core
in float vLayer;
out vec4 FragColor;
uniform sampler2DArray uArray;
void main() { FragColor = texture(uArray, vec3(0.5, 0.5, vLayer)); }
)GLSL";
}

class OpenGL4Gl41FloorTest final : public Game
{
public:
    OpenGL4Gl41FloorTest() : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        {
            std::printf("SKIP: this run selected %s, not OPENGL4\n",
                        std::string(device.GetGraphicsRendererName()).c_str());
            result_ = CNA::Examples::kSkipExitCode;
            Exit();
            return;
        }
        const auto& facts =
            static_cast<OpenGL4Renderer&>(device.GetRenderer()).GetModernCapabilitiesEXT();
        std::printf("context: OpenGL %d.%d core\n", facts.contextMajor, facts.contextMinor);
        if (facts.contextMajor != 4 || facts.contextMinor != 1)
        {
            std::printf("SKIP: MESA_GL_VERSION_OVERRIDE=4.1 was not honoured (a non-Mesa driver?)\n");
            result_ = CNA::Examples::kSkipExitCode;
            Exit();
            return;
        }

        Check(!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders) &&
                  !device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                                    CNA::ShaderStageEXT::Compute),
              "compute, a 4.3 feature, is refused on the 4.1 floor");
        const CNA::RendererLimitValue layers =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers);
        std::printf("MaxTextureArrayLayers: %llu\n",
                    static_cast<unsigned long long>(layers.known ? layers.value : 0));
        Check(layers.known && layers.value >= 256,
              "texture arrays, core since 3.0, are published on the 4.1 floor (>= 256 layers, "
              "the 3.0 minimum)");

        // Two layers, uploaded, read back and sampled.
        Texture2DArray array(device, Texture2DArrayDescriptor(
                                         2, 2, 2, 1, SurfaceFormat::Color,
                                         Texture2DArrayUsage::Sampled |
                                             Texture2DArrayUsage::TransferSource |
                                             Texture2DArrayUsage::TransferDestination));
        const std::array<Color, 2> colours{Color::Red, Color::Lime};
        bool roundTrip = true;
        for (int layer = 0; layer < 2; ++layer)
        {
            std::vector<std::uint32_t> texels(4, colours[static_cast<std::size_t>(layer)]
                                                     .getPackedValueProperty());
            array.setData(layer, 0, nullptr, texels.data(), texels.size() * 4);
            std::vector<std::uint32_t> back(4, 0);
            array.getData(layer, 0, nullptr, back.data(), back.size() * 4);
            roundTrip = roundTrip && back == texels;
        }
        Check(roundTrip, "each layer reads back exactly what was uploaded");

        ShaderEffect effect(device, kVertex, kFragment);
        effect.SetTextureArrayEXT(0, array);
        RenderTarget2D target(device, 4, 4);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        // The array does not declare Filterable usage, so it is point-sampled.
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        bool sampled = effect.IsEffectValid();
        for (int layer = 0; layer < 2 && sampled; ++layer)
        {
            const float z = static_cast<float>(layer);
            const std::array<std::array<float, 3>, 6> corners{{
                {-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, -1, z}, {1, 1, z}, {-1, 1, z}}};
            VertexBuffer quad(device,
                              VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                                   VertexElementUsage::Position,
                                                                   0)}),
                              6, BufferUsage::None);
            quad.SetDataRaw(corners.data(), 6, 12);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            effect.Apply();
            device.SetVertexBuffer(&quad);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::array<Color, 16> pixels{};
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            for (const Color& pixel : pixels)
                sampled = sampled && pixel.getPackedValueProperty() ==
                                         colours[static_cast<std::size_t>(layer)]
                                             .getPackedValueProperty();
        }
        Check(sampled, "a ShaderEffect samples each layer through texture-array unit 0");

        // A classic clear still reaches a target on the floor.
        device.SetRenderTarget(&target);
        device.Clear(Color::Blue);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::array<Color, 16> cleared{};
        target.GetData(cleared.data(), static_cast<int>(cleared.size()));
        bool blue = true;
        for (const Color& pixel : cleared)
            blue = blue && pixel.getPackedValueProperty() == Color::Blue.getPackedValueProperty();
        Check(blue, "a classic clear reaches its render target");

        Exit();
    }

private:
    void Check(const bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) result_ = 1;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int result_ = 0;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL4Gl41FloorTest game;
    game.Run();
    return game.Result();
}

#endif
