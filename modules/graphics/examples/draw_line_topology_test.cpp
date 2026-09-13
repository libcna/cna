// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-78: discriminating LineList/LineStrip coverage for every ordinary
// GraphicsDevice draw family. The older primitive-type test only proved these enum values did not
// throw, so a backend that silently submitted both as the same topology could pass it.
//
// Each route receives two decoy vertices and two decoy indices before the real range. Correct
// offset handling produces only green. LineList draws two separated horizontal segments and must
// leave the centre empty; LineStrip draws a three-segment zigzag whose middle segment crosses the
// centre. This distinguishes the two native topologies as well as vertexStart, vertexOffset,
// startIndex and baseVertex. The same source is run by EasyGL and SDL GPU.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kPrefix = 2;
    const Color kBackground(9, 13, 17, 255);

    enum class Route
    {
        UserNonIndexed,
        UserIndexed16,
        UserIndexed32,
        BufferNonIndexed,
        BufferIndexed16,
        BufferIndexed32,
    };

    [[nodiscard]] std::array<VertexPositionColor, 4> Geometry(PrimitiveType topology)
    {
        if (topology == PrimitiveType::LineList)
        {
            return {{
                {Vector3(-0.8f, -0.5f, 0.0f), Color::Lime},
                {Vector3( 0.8f, -0.5f, 0.0f), Color::Lime},
                {Vector3(-0.8f,  0.5f, 0.0f), Color::Lime},
                {Vector3( 0.8f,  0.5f, 0.0f), Color::Lime},
            }};
        }
        return {{
            {Vector3(-0.8f, -0.5f, 0.0f), Color::Lime},
            {Vector3(-0.2f,  0.5f, 0.0f), Color::Lime},
            {Vector3( 0.2f, -0.5f, 0.0f), Color::Lime},
            {Vector3( 0.8f,  0.5f, 0.0f), Color::Lime},
        }};
    }

    [[nodiscard]] int PrimitiveCount(PrimitiveType topology)
    {
        return topology == PrimitiveType::LineList ? 2 : 3;
    }

    [[nodiscard]] const char* RouteName(Route route)
    {
        switch (route)
        {
            case Route::UserNonIndexed: return "DrawUserPrimitives";
            case Route::UserIndexed16: return "DrawUserIndexedPrimitives/u16";
            case Route::UserIndexed32: return "DrawUserIndexedPrimitives/u32";
            case Route::BufferNonIndexed: return "DrawPrimitives";
            case Route::BufferIndexed16: return "DrawIndexedPrimitives/u16";
            case Route::BufferIndexed32: return "DrawIndexedPrimitives/u32";
        }
        return "unknown";
    }
}

class DrawLineTopologyTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> manager_;

    void CheckZeroSizedBuffers(GraphicsDevice& device)
    {
        bool staticVertexOk = true;
        bool dynamicVertexOk = true;
        bool staticIndexOk = true;
        bool dynamicIndexOk = true;
        try
        {
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 0,
                                BufferUsage::None);
            buffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0);
            staticVertexOk = buffer.getVertexCountProperty() == 0;
        }
        catch (...) { staticVertexOk = false; }
        try
        {
            DynamicVertexBuffer buffer(device,
                                       VertexPositionColor::getVertexDeclarationStatic(), 0,
                                       BufferUsage::None);
            buffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0, 0,
                           SetDataOptions::Discard);
            dynamicVertexOk = buffer.getVertexCountProperty() == 0;
        }
        catch (...) { dynamicVertexOk = false; }
        try
        {
            IndexBuffer buffer(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
            buffer.SetData(static_cast<const std::uint16_t*>(nullptr), 0);
            IndexBuffer wide(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
            wide.SetData(static_cast<const std::uint32_t*>(nullptr), 0);
            staticIndexOk = buffer.getIndexCountProperty() == 0 &&
                            wide.getIndexCountProperty() == 0;
        }
        catch (...) { staticIndexOk = false; }
        try
        {
            DynamicIndexBuffer buffer(device, IndexElementSize::SixteenBits, 0,
                                      BufferUsage::None);
            buffer.SetData(static_cast<const std::uint16_t*>(nullptr), 0, 0,
                           SetDataOptions::NoOverwrite);
            dynamicIndexOk = buffer.getIndexCountProperty() == 0;
        }
        catch (...) { dynamicIndexOk = false; }

        Check(staticVertexOk, "zero-sized VertexBuffer accepts an empty upload");
        Check(dynamicVertexOk, "zero-sized DynamicVertexBuffer accepts an empty Discard upload");
        Check(staticIndexOk, "zero-sized 16/32-bit IndexBuffers accept empty uploads");
        Check(dynamicIndexOk, "zero-sized DynamicIndexBuffer accepts an empty NoOverwrite upload");
    }

    void Issue(GraphicsDevice& device, BasicEffect& effect, Route route, PrimitiveType topology)
    {
        const auto real = Geometry(topology);
        std::array<VertexPositionColor, kPrefix + 4> vertices{};
        // If any vertex offset is lost, a saturated red line appears across the centre.
        vertices[0] = VertexPositionColor(Vector3(-0.9f, 0.0f, 0.0f), Color::Red);
        vertices[1] = VertexPositionColor(Vector3( 0.9f, 0.0f, 0.0f), Color::Red);
        for (int i = 0; i < 4; ++i)
            vertices[static_cast<std::size_t>(kPrefix + i)] = real[static_cast<std::size_t>(i)];

        const int primitiveCount = PrimitiveCount(topology);
        effect.Apply();
        switch (route)
        {
            case Route::UserNonIndexed:
                device.DrawUserPrimitives(topology, vertices.data(), kPrefix, primitiveCount);
                return;
            case Route::UserIndexed16:
            {
                const std::array<std::uint16_t, kPrefix + 4> indices{0, 1, 0, 1, 2, 3};
                device.DrawUserIndexedPrimitives(topology, vertices.data(), kPrefix, 4,
                                                 indices.data(), kPrefix, primitiveCount);
                return;
            }
            case Route::UserIndexed32:
            {
                const std::array<std::uint32_t, kPrefix + 4> indices{0, 1, 0, 1, 2, 3};
                device.DrawUserIndexedPrimitives(topology, vertices.data(), kPrefix, 4,
                                                 indices.data(), kPrefix, primitiveCount);
                return;
            }
            case Route::BufferNonIndexed:
            {
                VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(),
                                static_cast<int>(vertices.size()), BufferUsage::None);
                vb.SetData(vertices.data(), static_cast<int>(vertices.size()));
                device.SetVertexBuffer(&vb);
                device.DrawPrimitives(topology, kPrefix, primitiveCount);
                device.SetVertexBuffer(nullptr);
                return;
            }
            case Route::BufferIndexed16:
            {
                const std::array<std::uint16_t, kPrefix + 4> indices{0, 1, 0, 1, 2, 3};
                VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(),
                                static_cast<int>(vertices.size()), BufferUsage::None);
                IndexBuffer ib(device, IndexElementSize::SixteenBits,
                               static_cast<int>(indices.size()), BufferUsage::None);
                vb.SetData(vertices.data(), static_cast<int>(vertices.size()));
                ib.SetData(indices.data(), static_cast<int>(indices.size()));
                device.SetVertexBuffer(&vb);
                device.setIndicesProperty(&ib);
                device.DrawIndexedPrimitives(topology, kPrefix, 0, 4, kPrefix, primitiveCount);
                device.SetVertexBuffer(nullptr);
                device.setIndicesProperty(nullptr);
                return;
            }
            case Route::BufferIndexed32:
            {
                const std::array<std::uint32_t, kPrefix + 4> indices{0, 1, 0, 1, 2, 3};
                VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(),
                                static_cast<int>(vertices.size()), BufferUsage::None);
                IndexBuffer ib(device, IndexElementSize::ThirtyTwoBits,
                               static_cast<int>(indices.size()), BufferUsage::None);
                vb.SetData(vertices.data(), static_cast<int>(vertices.size()));
                ib.SetData(indices.data(), static_cast<int>(indices.size()));
                device.SetVertexBuffer(&vb);
                device.setIndicesProperty(&ib);
                device.DrawIndexedPrimitives(topology, kPrefix, 0, 4, kPrefix, primitiveCount);
                device.SetVertexBuffer(nullptr);
                device.setIndicesProperty(nullptr);
                return;
            }
        }
    }

    void CheckSignature(GraphicsDevice& device, Route route, PrimitiveType topology)
    {
        device.Clear(kBackground);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        effect.setDiffuseColorProperty(Vector3::One);

        Issue(device, effect, route, topology);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color::Transparent);
        device.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
        int green = 0;
        int red = 0;
        int centreGreen = 0;
        int changed = 0;
        int maxGreen = 0;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const Color& pixel = pixels[static_cast<std::size_t>(y * kSize + x)];
                const bool isGreen = pixel.getGProperty() > 200 && pixel.getRProperty() < 20 &&
                                     pixel.getBProperty() < 20;
                const bool isRed = pixel.getRProperty() > 200 && pixel.getGProperty() < 20 &&
                                   pixel.getBProperty() < 20;
                green += isGreen ? 1 : 0;
                red += isRed ? 1 : 0;
                changed += (pixel != kBackground) ? 1 : 0;
                maxGreen = std::max(maxGreen, static_cast<int>(pixel.getGProperty()));
                if (x >= 28 && x <= 36 && y >= 28 && y <= 36 && isGreen)
                    ++centreGreen;
            }
        }

        const std::string prefix = std::string(RouteName(route)) +
            (topology == PrimitiveType::LineList ? " LineList" : " LineStrip");
        std::printf("[INFO] %s: green=%d red=%d centreGreen=%d changed=%d maxGreen=%d\n",
                    prefix.c_str(), green, red, centreGreen, changed, maxGreen);
        Check(green >= 70 && green <= 150,
              (prefix + " has the expected thin-line coverage").c_str());
        Check(red == 0, (prefix + " skips every decoy offset").c_str());
        if (topology == PrimitiveType::LineList)
            Check(centreGreen == 0, (prefix + " leaves its separated centre empty").c_str());
        else
            Check(centreGreen >= 5, (prefix + " draws the connecting centre segment").c_str());
    }

public:
    DrawLineTopologyTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
        manager_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);

        CheckZeroSizedBuffers(device);

        const std::array<std::pair<Route, PrimitiveType>, 6> cases{{
            {Route::UserNonIndexed, PrimitiveType::LineList},
            {Route::UserIndexed16, PrimitiveType::LineStrip},
            {Route::UserIndexed32, PrimitiveType::LineList},
            {Route::BufferNonIndexed, PrimitiveType::LineStrip},
            {Route::BufferIndexed16, PrimitiveType::LineList},
            {Route::BufferIndexed32, PrimitiveType::LineStrip},
        }};
        for (const auto& [route, topology] : cases)
            CheckSignature(device, route, topology);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<DrawLineTopologyTest>();
}
