// SPDX-License-Identifier: MS-PL
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

namespace
{
    class GraphicsProfileDrawValidationTest : public testing::TestWithParam<GraphicsProfile>
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
            device = std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                GetParam(), PresentationParameters::CreateWindowedEXT(8, 8));
            effect = std::make_unique<BasicEffect>(*device);
            device->setBlendStateProperty(BlendState::Opaque);
            device->setDepthStencilStateProperty(DepthStencilState::None);
            device->setRasterizerStateProperty(RasterizerState::CullNone);
            effect->setVertexColorEnabledProperty(true);
            effect->Apply();
        }
        bool Reach() const { return GetParam() == GraphicsProfile::Reach; }
        void Draw()
        {
            const std::array<VertexPositionTexture, 3> vertices{
                VertexPositionTexture(Vector3(-1, -1, 0), Vector2::Zero),
                VertexPositionTexture(Vector3(1, -1, 0), Vector2::Zero),
                VertexPositionTexture(Vector3(-1, 1, 0), Vector2::Zero)};
            device->DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
        }
        std::unique_ptr<GraphicsDevice> device;
        std::unique_ptr<BasicEffect> effect;
    };
    INSTANTIATE_TEST_SUITE_P(Xna, GraphicsProfileDrawValidationTest,
        testing::Values(GraphicsProfile::Reach, GraphicsProfile::HiDef),
        [](const testing::TestParamInfo<GraphicsProfile>& info) {
            return info.param == GraphicsProfile::Reach ? "Reach" : "HiDef";
        });
}

TEST_P(GraphicsProfileDrawValidationTest, PrimitiveCeilingsAcceptRealBufferedAndUserDraws)
{
    // XNA .cctor IL_0047/027e: 65535/1048575 primitives. Existing ceiling tests
    // stop at a missing-effect error; these provide complete buffers and actually draw at N.
    // TriangleStrip needs N+2 vertices/indices. All triangles are deliberately outside the
    // clip volume, keeping the CPU test cheap without reading beyond a resource.
    const int maximum = Reach() ? 65'535 : 1'048'575;
    const std::vector<VertexPositionColor> data(maximum + 2,
        VertexPositionColor(Vector3(3, 3, 0), Color::Red));
    const std::vector<std::uint16_t> indexData(maximum + 2, 0);
    VertexBuffer vertices(*device, VertexPositionColor::getVertexDeclarationStatic(),
                          maximum + 2, BufferUsage::None);
    vertices.SetData(data.data(), maximum + 2);
    IndexBuffer indices(*device, IndexElementSize::SixteenBits, maximum + 2, BufferUsage::None);
    indices.SetData(indexData.data(), maximum + 2);
    device->SetVertexBuffer(&vertices);
    device->SetIndexBuffer(&indices);
    EXPECT_NO_THROW(device->DrawPrimitives(PrimitiveType::TriangleStrip, 0, maximum));
    EXPECT_THROW(device->DrawPrimitives(PrimitiveType::TriangleStrip, 0, maximum + 1), System::NotSupportedException);
    EXPECT_NO_THROW(device->DrawIndexedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum));
    EXPECT_THROW(device->DrawIndexedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum + 1), System::NotSupportedException);
    EXPECT_NO_THROW(device->DrawUserPrimitives(PrimitiveType::TriangleStrip, data.data(), 0, maximum));
    EXPECT_THROW(device->DrawUserPrimitives(PrimitiveType::TriangleStrip, data.data(), 0, maximum + 1), System::NotSupportedException);
    EXPECT_NO_THROW(device->DrawUserIndexedPrimitives(PrimitiveType::TriangleStrip, data.data(), 0, 1,
                                                     indexData.data(), 0, maximum));
    EXPECT_THROW(device->DrawUserIndexedPrimitives(PrimitiveType::TriangleStrip, data.data(), 0, 1,
                                                   indexData.data(), 0, maximum + 1), System::NotSupportedException);
    // TQ-021: successful user draws clear persistent vertex and index bindings.
    EXPECT_TRUE(device->GetVertexBuffers().empty());
    EXPECT_EQ(device->getIndicesProperty(), nullptr);
}

TEST_P(GraphicsProfileDrawValidationTest, InstancingChecksPrimitiveAndInstanceCeilingsIndependently)
{
    // XNA DrawInstancedPrimitives IL_003d-0064 and IL_007c-00a3 compare EACH count
    // against MaxPrimitiveCount, rather than their product. Desktop Reach has no separate
    // instancing prohibition. TQ-021's two-rate-stream requirement remains in force.
    const int maximum = Reach() ? 65'535 : 1'048'575;
    const std::vector<std::uint16_t> indexData(maximum + 2, 0);
    const VertexPositionColor vertex(Vector3(3, 3, 0), Color::Red);
    VertexBuffer vertices(*device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    vertices.SetData(&vertex, 1);
    IndexBuffer indices(*device, IndexElementSize::SixteenBits, maximum + 2, BufferUsage::None);
    indices.SetData(indexData.data(), maximum + 2);
    const VertexDeclaration matrixLayout(64, {
        VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
        VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
        VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4)});
    VertexBuffer instances(*device, matrixLayout, 1, BufferUsage::None);
    const Matrix identity = Matrix::getIdentityProperty();
    instances.SetDataRaw(&identity, 1, 64);
    device->SetVertexBuffers({VertexBufferBinding(&vertices),
        VertexBufferBinding(&instances, 0, std::numeric_limits<int>::max())});
    device->SetIndexBuffer(&indices);
    EXPECT_NO_THROW(device->DrawInstancedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum, 1));
    EXPECT_THROW(device->DrawInstancedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum + 1, 1), System::NotSupportedException);
    EXPECT_NO_THROW(device->DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 1, 0, 1, maximum));
    EXPECT_THROW(device->DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 1, 0, 1, maximum + 1), System::NotSupportedException);
    ASSERT_EQ(device->GetVertexBuffers().size(), 2u);
    EXPECT_EQ(device->getIndicesProperty(), &indices);
}

TEST_P(GraphicsProfileDrawValidationTest, UserIndexWidthIsProfileRestrictedBeforeDrawing)
{
    // XNA DrawUserIndexedPrimitives 32-bit core's IndexElementSize32 gate. A 32-bit
    // array of low indices must still be refused on Reach (not only values over 65535).
    const std::array<VertexPositionColor, 3> data{
        VertexPositionColor(Vector3(-1, -1, 0), Color::Red),
        VertexPositionColor(Vector3(1, -1, 0), Color::Red),
        VertexPositionColor(Vector3(-1, 1, 0), Color::Red)};
    const std::array<std::uint16_t, 3> narrow{0, 1, 2};
    const std::array<std::uint32_t, 3> wide{0, 1, 2};
    RenderTarget2D target(*device, 8, 8);
    device->SetRenderTarget(&target);
    device->Clear(Color::Black);
    EXPECT_NO_THROW(device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data.data(), 0, 3, narrow.data(), 0, 1));
    if (Reach())
        EXPECT_THROW(device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data.data(), 0, 3, wide.data(), 0, 1), System::NotSupportedException);
    else
        EXPECT_NO_THROW(device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data.data(), 0, 3, wide.data(), 0, 1));
    device->SetRenderTarget(nullptr);
    std::array<Color, 64> pixels{};
    target.GetData(pixels.data(), 64);
    // Identity transform maps the red triangle over the left half; (1,6) lies inside it.
    EXPECT_EQ(pixels[6 * 8 + 1], Color::Red);
}

TEST_P(GraphicsProfileDrawValidationTest, NpotAddressingIsDeferredToDrawAndChecksBothAxes)
{
    // XNA VerifyCanDraw IL_01ad-01db, ProfileCapabilities NonPow2Unconditional.
    // W does not address a Texture2D; U and V are independent restrictions.
    Texture2D npot(*device, 3, 5, false, SurfaceFormat::Color);
    Texture2D pot(*device, 4, 8, false, SurfaceFormat::Color);
    effect->setTextureEnabledProperty(true);
    effect->setVertexColorEnabledProperty(false);
    for (const auto mode : {TextureAddressMode::Wrap, TextureAddressMode::Mirror, TextureAddressMode::Clamp})
    {
        for (int axis = 0; axis != 3; ++axis)
        {
            SCOPED_TRACE(static_cast<int>(mode));
            SCOPED_TRACE(axis);
            SamplerState sampler = SamplerState::PointClamp;
            if (axis == 0) sampler.setAddressUProperty(mode);
            if (axis == 1) sampler.setAddressVProperty(mode);
            if (axis == 2) sampler.setAddressWProperty(mode);
            // Binding a sampler or texture is legal; the forbidden combination fails on draw.
            device->getSamplerStatesProperty()[0] = sampler;
            effect->setTextureProperty(&npot);
            effect->Apply();
            if (Reach() && axis != 2 && mode != TextureAddressMode::Clamp)
                EXPECT_THROW(Draw(), System::NotSupportedException);
            else
                EXPECT_NO_THROW(Draw());
            effect->setTextureProperty(&pot);
            effect->Apply();
            EXPECT_NO_THROW(Draw()); // POT control detects an overbroad Reach wrap refusal.
        }
    }
}

TEST_P(GraphicsProfileDrawValidationTest, EveryFloatFormatRequiresExactlyPointFilteringAtDraw)
{
    // XNA .cctor InvalidFilterFormats IL_0606-0660 lists all seven float/half formats.
    // VerifyCanDraw IL_0195-01a8 refuses every non-Point TextureFilter, including the
    // mixed min/mag/mip filters. Reach cannot construct any of these formats.
    if (Reach()) return;
    effect->setTextureEnabledProperty(true);
    effect->setVertexColorEnabledProperty(false);
    for (const auto format : {SurfaceFormat::Single, SurfaceFormat::Vector2, SurfaceFormat::Vector4,
             SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2, SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable})
    {
        SCOPED_TRACE(static_cast<int>(format));
        Texture2D texture(*device, 2, 2, false, format);
        effect->setTextureProperty(&texture);
        effect->Apply();
        for (int ordinal = 0; ordinal != 9; ++ordinal)
        {
            SCOPED_TRACE(ordinal);
            SamplerState sampler = SamplerState::PointClamp;
            sampler.setFilterProperty(static_cast<TextureFilter>(ordinal));
            device->getSamplerStatesProperty()[0] = sampler;
            if (ordinal == static_cast<int>(TextureFilter::Point)) EXPECT_NO_THROW(Draw());
            else EXPECT_THROW(Draw(), System::NotSupportedException);
        }
    }
}

TEST_P(GraphicsProfileDrawValidationTest, NonBlendableTargetsAreValidatedAtDraw)
{
    // XNA .cctor InvalidBlendFormats IL_0661-06ae excludes HdrBlendable; VerifyCanDraw
    // IL_00f3-011d refuses blending/masked writes after a legal SetRenderTarget.
    if (Reach()) return;
    effect->setVertexColorEnabledProperty(false);
    effect->Apply();
    for (const auto format : {SurfaceFormat::Single, SurfaceFormat::Vector2, SurfaceFormat::Vector4,
             SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2, SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable})
    {
        SCOPED_TRACE(static_cast<int>(format));
        RenderTarget2D target(*device, 8, 8, false, format, DepthFormat::None);
        device->SetRenderTarget(&target);
        device->setBlendStateProperty(BlendState::Opaque);
        EXPECT_NO_THROW(Draw());
        device->setBlendStateProperty(BlendState::AlphaBlend);
        if (format == SurfaceFormat::HdrBlendable) EXPECT_NO_THROW(Draw());
        else EXPECT_THROW(Draw(), System::NotSupportedException);
        BlendState masked = BlendState::Opaque;
        masked.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        device->setBlendStateProperty(masked);
        if (format == SurfaceFormat::HdrBlendable) EXPECT_NO_THROW(Draw());
        else EXPECT_THROW(Draw(), System::NotSupportedException);
        device->SetRenderTarget(nullptr);
    }
}

TEST_P(GraphicsProfileDrawValidationTest, NpotRefusalsReachBufferedIndexedInstancedAndUserRoutes)
{
    // XNA VerifyCanDraw IL_01ad-01db is shared by all five public draw routes.
    // A legal Clamp control must paint a known texel after each refused Wrap draw.
    const std::array<VertexPositionTexture, 3> data{
        VertexPositionTexture(Vector3(-1, -1, 0), Vector2::Zero),
        VertexPositionTexture(Vector3(1, -1, 0), Vector2::Zero),
        VertexPositionTexture(Vector3(-1, 1, 0), Vector2::Zero)};
    const std::array<std::uint16_t, 3> indexData{0, 1, 2};
    VertexBuffer vertices(*device, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
    vertices.SetData(data.data(), 3);
    IndexBuffer indices(*device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indices.SetData(indexData.data(), 3);
    const VertexDeclaration matrixLayout(64, {
        VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
        VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
        VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4)});
    VertexBuffer instances(*device, matrixLayout, 1, BufferUsage::None);
    const Matrix identity = Matrix::getIdentityProperty();
    instances.SetDataRaw(&identity, 1, 64);
    Texture2D texture(*device, 3, 5, false, SurfaceFormat::Color);
    const std::array<Color, 15> red{
        Color::Red, Color::Red, Color::Red, Color::Red, Color::Red,
        Color::Red, Color::Red, Color::Red, Color::Red, Color::Red,
        Color::Red, Color::Red, Color::Red, Color::Red, Color::Red};
    texture.SetData(red.data(), 15);
    RenderTarget2D target(*device, 8, 8);
    device->SetRenderTarget(&target);
    effect->setVertexColorEnabledProperty(false);
    effect->setTextureEnabledProperty(true);
    effect->setTextureProperty(&texture);
    effect->Apply();
    for (int route = 0; route != 5; ++route)
    {
        SCOPED_TRACE(route);
        auto draw = [&]
        {
            device->SetVertexBuffer(&vertices);
            device->SetIndexBuffer(&indices);
            switch (route)
            {
                case 0: device->DrawPrimitives(PrimitiveType::TriangleList, 0, 1); break;
                case 1: device->DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1); break;
                case 2:
                    device->SetVertexBuffers({VertexBufferBinding(&vertices), VertexBufferBinding(&instances, 0, 1)});
                    device->DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1); break;
                case 3: device->DrawUserPrimitives(PrimitiveType::TriangleList, data.data(), 0, 1); break;
                case 4: device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data.data(), 0, 3, indexData.data(), 0, 1); break;
            }
        };
        device->Clear(Color::Black);
        device->getSamplerStatesProperty()[0] = SamplerState::PointWrap;
        if (Reach())
        {
            try { draw(); ADD_FAILURE() << "Reach NPOT Wrap draw accepted"; }
            catch (const System::NotSupportedException& error)
            {
                EXPECT_EQ(error.getMessageProperty(), "XNA Framework Reach profile requires TextureAddressMode to be Clamp when using texture sizes that are not powers of two.");
            }
        }
        else EXPECT_NO_THROW(draw());
        device->getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        ASSERT_NO_THROW(draw());
        device->SetRenderTarget(nullptr);
        std::array<Color, 64> pixels{};
        target.GetData(pixels.data(), 64);
        // Identity viewport maps (-1,-1),(1,-1),(-1,1) to (0,8),(8,8),(0,0).
        // (1.5,6.5) lies inside; sampling uniform red returns exactly Red.
        EXPECT_EQ(pixels[6 * 8 + 1], Color::Red);
        device->SetRenderTarget(&target);
    }
    device->SetRenderTarget(nullptr);
}

TEST_P(GraphicsProfileDrawValidationTest, NonBlendableMrtFormatsAreCheckedBeyondTheFirstSlot)
{
    if (Reach()) return;
    // XNA willItBlend is derived from EVERY target. Colour and Single are both 32-bit,
    // so all four slots are legal to bind before the draw-time format restriction.
    RenderTarget2D color0(*device, 8, 8), color1(*device, 8, 8), color2(*device, 8, 8);
    RenderTarget2D single(*device, 8, 8, false, SurfaceFormat::Single, DepthFormat::None);
    device->SetRenderTargets({RenderTargetBinding(&color0), RenderTargetBinding(&color1),
                              RenderTargetBinding(&color2), RenderTargetBinding(&single)});
    effect->setVertexColorEnabledProperty(false);
    effect->Apply();
    device->setBlendStateProperty(BlendState::Opaque);
    EXPECT_NO_THROW(Draw());
    device->setBlendStateProperty(BlendState::AlphaBlend);
    EXPECT_THROW(Draw(), System::NotSupportedException);
    BlendState masked = BlendState::Opaque;
    masked.setColorWriteChannels3Property(ColorWriteChannels::Red);
    device->setBlendStateProperty(masked);
    EXPECT_THROW(Draw(), System::NotSupportedException);
    device->SetRenderTargets({});
}

TEST_P(GraphicsProfileDrawValidationTest, DrawCountAndIndexWidthErrorsUseXnaDiagnostics)
{
    // XNA ProfileMaxPrimitiveCount / ProfileNoIndexElementSize32 resources, used by
    // DrawPrimitives IL_0027-0048, DrawIndexedPrimitives IL_003d-005f and both user cores.
    const int maximum = Reach() ? 65'535 : 1'048'575;
    const std::string prefix = std::string("XNA Framework ") + (Reach() ? "Reach" : "HiDef") + " profile ";
    const std::string countMessage = prefix + "supports a maximum of " + std::to_string(maximum) + " primitives per draw call.";
    auto refused = [&](auto&& action, const std::string& expected)
    {
        try { action(); ADD_FAILURE() << "Profile violation accepted"; }
        catch (const System::NotSupportedException& error) { EXPECT_EQ(error.getMessageProperty(), expected); }
    };
    const VertexPositionColor vertex(Vector3::Zero, Color::Red);
    const std::uint16_t index = 0;
    refused([&] { device->DrawPrimitives(PrimitiveType::TriangleStrip, 0, maximum + 1); }, countMessage);
    refused([&] { device->DrawIndexedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum + 1); }, countMessage);
    refused([&] { device->DrawInstancedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, maximum + 1, 1); }, countMessage);
    refused([&] { device->DrawInstancedPrimitives(PrimitiveType::TriangleStrip, 0, 0, 1, 0, 1, maximum + 1); }, countMessage);
    refused([&] { device->DrawUserPrimitives(PrimitiveType::TriangleStrip, &vertex, 0, maximum + 1); }, countMessage);
    refused([&] { device->DrawUserIndexedPrimitives(PrimitiveType::TriangleStrip, &vertex, 0, 1, &index, 0, maximum + 1); }, countMessage);
    if (Reach())
    {
        const std::uint32_t wide = 0;
        refused([&] { device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, &vertex, 0, 1, &wide, 0, 1); },
            prefix + "does not support 32 bit indices. Use IndexElementSize.SixteenBits or a type that has a size of two bytes.");
    }
}
