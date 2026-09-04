// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-31/IGL-42..45/IGL-55: DrawInstancedPrimitives -- a genuine per-instance vertex
// stream (igl::VertexSampleFunction::Instance + sampleRate), not just an instance count plumbed
// through unused.
//
// IGL's built-in uber-shader has no notion of "per-instance transform": vertex attribute locations
// are assigned from a fixed usage->slot table (see IglConversions.hpp's ToVertexAttributeSlot / this
// family's SlotName), one location per usage regardless of how many streams declare it, so a second
// stream re-declaring VertexElementUsage::Position (the classic D3D9 hardware-instancing shape used
// by e.g. easygl_instancedmodel_shader_test.cpp's 4-row BLENDWEIGHT matrix) cannot get its own
// distinct locations here. This test instead proves the real mechanism -- a genuinely separate,
// instance-rate-sampled vertex stream reaching the shader -- through a custom ShaderEffect using an
// otherwise-unused slot: TexCoord1 (`aTexCoord1`) carries a per-instance (x, y) NDC offset, added to
// each vertex's local position in the vertex shader. A per-vertex stream supplies a small quad's
// local corners at Instance rate 0 (ordinary per-vertex); a second stream supplies 3 offsets at
// Instance rate 1.
//
// Scene: a small quad (local half-size 0.08 NDC) drawn 3 times via one DrawInstancedPrimitives call,
// offset left/centre/right. If the per-instance stream were not actually sampled at the instance
// rate (e.g. an attribute-divisor bug advancing it per-vertex instead), the buffer holds only 3
// instance-rate records for a 4-vertex quad drawn 3 times -- the second and third instances would
// read out-of-bounds/garbage offsets rather than the intended -0.6/0.0/+0.6, so the three expected
// on-screen positions would not all show a correctly-placed quad. If the offset were never applied at
// all (e.g. the attribute silently failed to bind, the same class of bug IGL-42..45 already found and
// fixed once for texture samplers), every instance would render on top of the centre quad only, and
// the left/right positions -- and the gap beside them -- would not show the expected pattern.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const char* kVertSrc = R"(#version 410 core
in vec3 aPosition;
in vec2 aTexCoord1;
void main() {
    gl_Position = vec4(aPosition.xy + aTexCoord1, aPosition.z, 1.0);
}
)";

    const char* kFragSrc = R"(#version 410 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    // The SPIR-V variant (plans/plan_igl.md IGL-43). This effect takes no parameters, so the only
    // difference is the one SPIR-V forces: an explicit location on every user input and output.
    // The attribute locations are IGL's own usage-to-slot table, the same one the shader above
    // reaches by name, so the instanced vertex layout is unchanged between backends.
    const char* kVulkanVertSrc = R"(#version 460
layout(location = 0) in vec3 aPosition;
layout(location = 4) in vec2 aTexCoord1;
void main() {
    gl_Position = vec4(aPosition.xy + aTexCoord1, aPosition.z, 1.0);
}
)";

    const char* kVulkanFragSrc = R"(#version 460
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    /// True when this process resolved IGL's Vulkan backend, asked through the supported query.
    [[nodiscard]] bool IsVulkanDialect(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        return device.GetShaderDialectEXT() ==
               CNA::Internal::Renderers::ShaderDialectEXT::GlslVulkan;
    }

#pragma pack(push, 1)
    struct QuadVertex { float x, y, z; };
    struct InstanceOffset { float x, y; };
#pragma pack(pop)
}

class IglInstancingTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> effect_;
    std::unique_ptr<VertexBuffer> quadVb_;
    std::unique_ptr<VertexBuffer> instanceVb_;
    std::unique_ptr<IndexBuffer> indexBuffer_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        const bool vulkan = IsVulkanDialect(device);
        effect_ = std::make_unique<ShaderEffect>(device, vulkan ? kVulkanVertSrc : kVertSrc,
                                                 vulkan ? kVulkanFragSrc : kFragSrc);
        if (!effect_->IsEffectValid())
        {
            ExpectTrue("the custom ShaderEffect compiled", false);
            return;
        }

        const VertexDeclaration quadDecl(
            static_cast<int>(sizeof(QuadVertex)),
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
        quadVb_ = std::make_unique<VertexBuffer>(device, quadDecl, 4, BufferUsage::WriteOnly);
        const QuadVertex quad[4] = {
            {-0.08f, 0.08f, 0.0f}, {-0.08f, -0.08f, 0.0f}, {0.08f, -0.08f, 0.0f}, {0.08f, 0.08f, 0.0f}};
        quadVb_->SetDataRaw(quad, 4, static_cast<int>(sizeof(QuadVertex)));

        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                                      BufferUsage::WriteOnly);
        indexBuffer_->SetData(indices, 0, 6);

        // TexCoord1 (usageIndex=1) is an otherwise-unused built-in attribute slot -- see this file's
        // header comment for why a second Position-usage stream cannot be used here instead.
        const VertexDeclaration instanceDecl(
            static_cast<int>(sizeof(InstanceOffset)),
            {VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1)});
        instanceVb_ = std::make_unique<VertexBuffer>(device, instanceDecl, 3, BufferUsage::WriteOnly);
        const InstanceOffset offsets[3] = {{-0.6f, 0.0f}, {0.0f, 0.0f}, {0.6f, 0.0f}};
        instanceVb_->SetDataRaw(offsets, 3, static_cast<int>(sizeof(InstanceOffset)));

        device.Clear(Color(static_cast<bytecs>(10), static_cast<bytecs>(10), static_cast<bytecs>(10),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        effect_->Apply();

        device.SetVertexBuffer(quadVb_.get());
        const std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(quadVb_.get(), 0, 0),
            VertexBufferBinding(instanceVb_.get(), 0, 1),
        };
        device.SetVertexBuffers(bindings);
        device.setIndicesProperty(indexBuffer_.get());

        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);

        const auto& viewport = device.getViewportProperty();
        const int w = viewport.getWidthProperty();
        const int h = viewport.getHeightProperty();

        const auto readPixel = [&](const int x, const int y) {
            const Rectangle region(x, y, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            return pixel;
        };

        const Color left = readPixel(w / 6, h / 2);
        const Color centre = readPixel(w / 2, h / 2);
        const Color right = readPixel(5 * w / 6, h / 2);
        const Color gap = readPixel(w / 12, h / 2);

        ExpectTrue("the left instance (offset -0.6) rendered red at its own position",
                  left.getRProperty() > 200 && left.getGProperty() < 40);
        ExpectTrue("the centre instance (offset 0.0) rendered red at its own position",
                  centre.getRProperty() > 200 && centre.getGProperty() < 40);
        ExpectTrue("the right instance (offset +0.6) rendered red at its own position",
                  right.getRProperty() > 200 && right.getGProperty() < 40);
        ExpectTrue("the gap beside the left instance stayed the clear colour, not red",
                  gap.getRProperty() < 40);
    }

public:
    IglInstancingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglInstancingTest>();
}
