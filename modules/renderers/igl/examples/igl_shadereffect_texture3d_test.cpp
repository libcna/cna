// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-17/IGL-42/IGL-43/IGL-44/IGL-45/IGL-55: the first custom ShaderEffect exercise
// for this renderer family, and the only way to verify Texture3D actually works -- IGL has no
// volume-texture readback path (IglTexture3DRenderer::GetData() refuses by name, see
// IglResources.cpp), so sampling one through a real custom shader and reading the RENDERED pixel
// is the only route to a genuine proof. Mirrors easygl_shadereffect_texture3d_test.cpp's own
// technique (Task 863) adapted to IGL's direct ShaderEffect(device, vertSrc, fragSrc) constructor
// (no content-pipeline .cnj needed) and desktop GLSL 410 syntax matching IglShaderLibrary.cpp's
// own OpenGL version directive.
//
// A 1x1x2 Texture3D (slice Z=0 red, slice Z=1 blue) sampled by a trivial custom fragment shader at
// two different Z coordinates must read back two DIFFERENT colours -- proving the compiled program
// actually ran, the volume texture was genuinely bound to its sampler3D uniform, and different Z
// coordinates genuinely select different slices (not a hardcoded/stale read). A second, all-black
// decoy Texture3D is created after the real one and never bound, ruling out a residual-GL-binding
// false positive the same way the EasyGL reference test does.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // aTexCoord0 is declared (VertexPositionTexture's own layout) but not otherwise needed by this
    // shader -- it is threaded through to a real fragment-stage read (an unreachable branch, never
    // taken since a UV coordinate can never be this large) purely so no GLSL compiler treats it as
    // an inactive/dead input and strips its attribute location, which IGL's by-name attribute
    // lookup would then fail to find.
    const char* kVertSrc = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord0;
out vec2 vTexCoord0;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vTexCoord0 = aTexCoord0;
}
)";

    const char* kFragSrc = R"(#version 410 core
in vec2 vTexCoord0;
out vec4 FragColor;
uniform sampler3D VolumeSampler;
uniform vec3 coord;
void main() {
    FragColor = texture(VolumeSampler, coord);
    if (vTexCoord0.x > 1000.0) FragColor = vec4(0.0);
}
)";
}

class IglShaderEffectTexture3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> effect_;
    std::unique_ptr<VertexBuffer> vertexBuffer_;
    std::unique_ptr<IndexBuffer> indexBuffer_;
    std::unique_ptr<Texture3D> volumeTexture_;
    std::unique_ptr<Texture3D> decoyTexture_;

    Color DrawOnce(GraphicsDevice& device, const float u, const float v, const float w)
    {
        device.Clear(Color(static_cast<bytecs>(10), static_cast<bytecs>(10),
                           static_cast<bytecs>(10), static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        effect_->Apply();
        effect_->SetTexture(0, *volumeTexture_);
        effect_->SetUniformInt("VolumeSampler", 0);
        effect_->SetUniformVec3("coord", u, v, w);

        device.SetVertexBuffer(vertexBuffer_.get());
        device.setIndicesProperty(indexBuffer_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        effect_ = std::make_unique<ShaderEffect>(device, kVertSrc, kFragSrc);
        if (!effect_->IsEffectValid())
        {
            ExpectTrue("the custom ShaderEffect compiled", false);
            return;
        }

        const std::vector<VertexPositionTexture> vertices = {
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        vertexBuffer_ = std::make_unique<VertexBuffer>(
            device, VertexPositionTexture::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer_->SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                                      BufferUsage::WriteOnly);
        indexBuffer_->SetData(indices, 0, 6);

        // 1x1x2 volume: slice Z=0 red, slice Z=1 blue.
        volumeTexture_ = std::make_unique<Texture3D>(device, 1, 1, 2, false, SurfaceFormat::Color);
        const Color slices[2] = {
            Color(static_cast<bytecs>(255), static_cast<bytecs>(0), static_cast<bytecs>(0),
                  static_cast<bytecs>(255)),
            Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(255),
                  static_cast<bytecs>(255)),
        };
        volumeTexture_->SetData(slices, 2);

        // A second, all-black decoy volume created and uploaded AFTER the real one -- rules out a
        // stale/residual texture-unit binding passing this test for the wrong reason.
        decoyTexture_ = std::make_unique<Texture3D>(device, 1, 1, 2, false, SurfaceFormat::Color);
        const Color black[2] = {
            Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                  static_cast<bytecs>(255)),
            Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                  static_cast<bytecs>(255)),
        };
        decoyTexture_->SetData(black, 2);

        const Color sliceZero = DrawOnce(device, 0.5f, 0.5f, 0.25f);
        const Color sliceOne = DrawOnce(device, 0.5f, 0.5f, 0.75f);

        ExpectTrue("Z=0.25 samples the red slice", sliceZero.getRProperty() > 200 &&
                                                        sliceZero.getBProperty() < 40);
        ExpectTrue("Z=0.75 samples the blue slice", sliceOne.getBProperty() > 200 &&
                                                        sliceOne.getRProperty() < 40);
    }

public:
    IglShaderEffectTexture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglShaderEffectTexture3DTest>();
}
