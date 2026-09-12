// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-030: focused fixed-capacity buffer allocation and update evidence.
// Native snapshots keep the public CPU readback shadows from concealing a GPU upload defect.

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;
    using CNA::Internal::Graphics::PositionColorStream;

    [[nodiscard]] PositionColorStream Pack(const VertexPositionColor& vertex)
    {
        return {
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Color.getRProperty(), vertex.Color.getGProperty(),
            vertex.Color.getBProperty(), vertex.Color.getAProperty()};
    }

    template<typename Value>
    [[nodiscard]] bool PrefixEquals(
        const std::vector<std::uint8_t>& bytes, const Value* const values,
        const std::size_t count)
    {
        const std::size_t byteCount = sizeof(Value) * count;
        return bytes.size() >= byteCount &&
            std::memcmp(bytes.data(), values, byteCount) == 0;
    }

    [[nodiscard]] bool IsZeroTail(
        const std::vector<std::uint8_t>& bytes, const std::size_t firstByte)
    {
        return firstByte <= bytes.size() &&
            std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(firstByte), bytes.end(),
                [](const std::uint8_t value) { return value == 0u; });
    }
}

class RlglBufferTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglBufferTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(48);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());
        constexpr int kDynamicDraw = 0x88E8;

        const std::array<VertexPositionColor, 4> initialVertices{
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color(1, 2, 3, 4)),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color(5, 6, 7, 8)),
            VertexPositionColor(Vector3(1.0f, 1.0f, 0.0f), Color(9, 10, 11, 12)),
            VertexPositionColor(Vector3(-1.0f, 1.0f, 0.0f), Color(13, 14, 15, 16))};
        std::array<PositionColorStream, 4> initialPacked{};
        for (std::size_t i = 0; i < initialVertices.size(); ++i)
            initialPacked[i] = Pack(initialVertices[i]);

        VertexBuffer staticVertex(
            device, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::None);
        staticVertex.SetData(initialVertices.data(), static_cast<int>(initialVertices.size()));
        const Rlgl::BufferResourceSnapshot staticVertexSnapshot =
            Rlgl::GetBufferResourceSnapshotForTesting(staticVertex.GetRenderer());
        Check(staticVertexSnapshot.id != 0 && staticVertexSnapshot.capacity == 4 &&
                  staticVertexSnapshot.count == 4 && staticVertexSnapshot.stride == 16 &&
                  staticVertexSnapshot.nativeByteSize == 64 &&
                  staticVertexSnapshot.nativeUsage == kDynamicDraw &&
                  staticVertexSnapshot.declaration ==
                      VertexPositionColor::getVertexDeclarationStatic().GetVertexElements() &&
                  PrefixEquals(staticVertexSnapshot.cpuBytes, initialPacked.data(), 4) &&
                  PrefixEquals(staticVertexSnapshot.nativeBytes, initialPacked.data(), 4),
              "public VertexBuffer packs its declaration and exact bytes into fixed rlgl storage");

        DynamicVertexBuffer dynamicVertex(
            device, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::WriteOnly);
        dynamicVertex.SetData(
            initialVertices.data(), 0, 4, SetDataOptions::None);
        const unsigned int dynamicVertexId =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamicVertex.GetRenderer()).id;

        const std::array<VertexPositionColor, 2> noOverwriteVertices{
            VertexPositionColor(Vector3(-0.5f, 0.0f, 0.0f), Color(21, 22, 23, 24)),
            VertexPositionColor(Vector3(0.5f, 0.0f, 0.0f), Color(25, 26, 27, 28))};
        const std::array<PositionColorStream, 2> noOverwritePacked{
            Pack(noOverwriteVertices[0]), Pack(noOverwriteVertices[1])};
        dynamicVertex.SetData(
            noOverwriteVertices.data(), 0, 2, SetDataOptions::NoOverwrite);
        const Rlgl::BufferResourceSnapshot noOverwriteVertexSnapshot =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamicVertex.GetRenderer());
        Check(noOverwriteVertexSnapshot.id == dynamicVertexId &&
                  noOverwriteVertexSnapshot.nativeByteSize == 64 &&
                  noOverwriteVertexSnapshot.count == 2 &&
                  noOverwriteVertexSnapshot.ordinaryUploadCount == 1 &&
                  noOverwriteVertexSnapshot.noOverwriteUploadCount == 1 &&
                  noOverwriteVertexSnapshot.discardUploadCount == 0 &&
                  PrefixEquals(noOverwriteVertexSnapshot.nativeBytes,
                               noOverwritePacked.data(), 2) &&
                  std::memcmp(noOverwriteVertexSnapshot.nativeBytes.data() + 32,
                              initialPacked.data() + 2, 32) == 0,
              "NoOverwrite updates a vertex prefix without reallocating or shrinking storage");

        const std::array<VertexPositionColor, 2> discardedVertices{
            VertexPositionColor(Vector3(-0.25f, -0.25f, 0.0f), Color(31, 32, 33, 34)),
            VertexPositionColor(Vector3(0.25f, 0.25f, 0.0f), Color(35, 36, 37, 38))};
        const std::array<PositionColorStream, 2> discardedPacked{
            Pack(discardedVertices[0]), Pack(discardedVertices[1])};
        dynamicVertex.SetData(discardedVertices.data(), 0, 2, SetDataOptions::Discard);
        const Rlgl::BufferResourceSnapshot discardedVertexSnapshot =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamicVertex.GetRenderer());
        Check(discardedVertexSnapshot.id == dynamicVertexId &&
                  discardedVertexSnapshot.nativeByteSize == 64 &&
                  discardedVertexSnapshot.discardUploadCount == 1 &&
                  PrefixEquals(discardedVertexSnapshot.nativeBytes, discardedPacked.data(), 2) &&
                  PrefixEquals(discardedVertexSnapshot.cpuBytes, discardedPacked.data(), 2) &&
                  IsZeroTail(discardedVertexSnapshot.cpuBytes, 32),
              "Discard orphans vertex storage, retains its name/capacity, and resets recovery bytes");

        const std::vector<VertexElement> everyFormat{
            {0, VertexElementFormat::Single, VertexElementUsage::Position, 0},
            {4, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0},
            {12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0},
            {24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0},
            {40, VertexElementFormat::Color, VertexElementUsage::Color, 0},
            {44, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0},
            {48, VertexElementFormat::Short2, VertexElementUsage::TextureCoordinate, 1},
            {52, VertexElementFormat::Short4, VertexElementUsage::TextureCoordinate, 2},
            {60, VertexElementFormat::NormalizedShort2, VertexElementUsage::Normal, 1},
            {64, VertexElementFormat::NormalizedShort4, VertexElementUsage::Normal, 2},
            {72, VertexElementFormat::HalfVector2, VertexElementUsage::TextureCoordinate, 3},
            {76, VertexElementFormat::HalfVector4, VertexElementUsage::TextureCoordinate, 4}};
        const VertexDeclaration everyFormatDeclaration(84, everyFormat);
        auto customVertex = renderer.CreateVertexBuffer(2);
        customVertex->SetVertexDeclaration(everyFormatDeclaration);
        std::array<std::uint8_t, 168> customBytes{};
        for (std::size_t i = 0; i < customBytes.size(); ++i)
            customBytes[i] = static_cast<std::uint8_t>((i * 37u + 11u) & 0xFFu);
        customVertex->SetData(customBytes.data(), 2, 84);
        const Rlgl::BufferResourceSnapshot customVertexSnapshot =
            Rlgl::GetBufferResourceSnapshotForTesting(*customVertex);
        Check(customVertexSnapshot.declaration == everyFormat &&
                  customVertexSnapshot.nativeByteSize == 168 &&
                  customVertexSnapshot.nativeBytes ==
                      std::vector<std::uint8_t>(customBytes.begin(), customBytes.end()),
              "all classic VertexElementFormat declarations retain exact custom stream bytes");

        DynamicIndexBuffer dynamic16(
            device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        const std::array<std::uint16_t, 6> initial16{0, 1, 2, 2, 3, 0};
        dynamic16.SetData(initial16.data(), 0, 6, SetDataOptions::None);
        const unsigned int index16Id =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamic16.GetRenderer()).id;
        const std::array<std::uint16_t, 3> replacement16{5, 4, 3};
        dynamic16.SetData(replacement16.data(), 0, 3, SetDataOptions::NoOverwrite);
        const Rlgl::BufferResourceSnapshot noOverwrite16 =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamic16.GetRenderer());
        Check(noOverwrite16.id == index16Id && noOverwrite16.indexBuffer &&
                  !noOverwrite16.thirtyTwoBit && noOverwrite16.nativeByteSize == 12 &&
                  noOverwrite16.nativeUsage == kDynamicDraw &&
                  noOverwrite16.ordinaryUploadCount == 1 &&
                  noOverwrite16.noOverwriteUploadCount == 1 &&
                  PrefixEquals(noOverwrite16.nativeBytes, replacement16.data(), 3) &&
                  std::memcmp(noOverwrite16.nativeBytes.data() + 6,
                              initial16.data() + 3, 6) == 0,
              "16-bit NoOverwrite uses rlgl EBO updates and preserves fixed-capacity tail data");
        dynamic16.SetData(replacement16.data(), 0, 2, SetDataOptions::Discard);
        const Rlgl::BufferResourceSnapshot discarded16 =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamic16.GetRenderer());
        Check(discarded16.id == index16Id && discarded16.nativeByteSize == 12 &&
                  discarded16.discardUploadCount == 1 &&
                  PrefixEquals(discarded16.nativeBytes, replacement16.data(), 2) &&
                  IsZeroTail(discarded16.cpuBytes, 4),
              "16-bit Discard orphans EBO storage without changing capacity or resource identity");

        DynamicIndexBuffer dynamic32(
            device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::WriteOnly);
        const std::array<std::uint32_t, 4> initial32{0u, 65537u, 2u, 0xF1234567u};
        dynamic32.SetData(initial32.data(), 0, 4, SetDataOptions::None);
        const unsigned int index32Id =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamic32.GetRenderer()).id;
        const std::array<std::uint32_t, 2> replacement32{0x89ABCDEFu, 70000u};
        dynamic32.SetData(replacement32.data(), 0, 2, SetDataOptions::Discard);
        const Rlgl::BufferResourceSnapshot discarded32 =
            Rlgl::GetBufferResourceSnapshotForTesting(dynamic32.GetRenderer());
        Check(discarded32.id == index32Id && discarded32.indexBuffer &&
                  discarded32.thirtyTwoBit && discarded32.stride == 4 &&
                  discarded32.nativeByteSize == 16 &&
                  discarded32.ordinaryUploadCount == 1 &&
                  discarded32.discardUploadCount == 1 &&
                  PrefixEquals(discarded32.nativeBytes, replacement32.data(), 2) &&
                  IsZeroTail(discarded32.cpuBytes, 8),
              "32-bit index storage preserves values beyond the unsigned-short range");

        bool rejectedVertexCapacity = false;
        try
        {
            customVertex->SetData(customBytes.data(), 3, 84);
        }
        catch (const std::out_of_range&)
        {
            rejectedVertexCapacity = true;
        }
        bool rejectedVertexStride = false;
        try
        {
            customVertex->SetData(customBytes.data(), 1, 80);
        }
        catch (const std::invalid_argument&)
        {
            rejectedVertexStride = true;
        }
        bool rejectedIndexWidth = false;
        try
        {
            dynamic16.GetRenderer().SetData32(initial32.data(), 1);
        }
        catch (const std::invalid_argument&)
        {
            rejectedIndexWidth = true;
        }
        bool rejectedInvalidOption = false;
        try
        {
            customVertex->SetDataWithOptions(
                customBytes.data(), 1, 84, static_cast<SetDataOptions>(99));
        }
        catch (const std::invalid_argument&)
        {
            rejectedInvalidOption = true;
        }
        Check(rejectedVertexCapacity && rejectedVertexStride &&
                  rejectedIndexWidth && rejectedInvalidOption,
              "renderer-local resources reject capacity, stride, index-width, and option misuse");

        auto emptyVertex = renderer.CreateVertexBuffer(0);
        emptyVertex->SetData(nullptr, 0, 16);
        const Rlgl::BufferResourceSnapshot emptyVertexSnapshot =
            Rlgl::GetBufferResourceSnapshotForTesting(*emptyVertex);
        Check(emptyVertexSnapshot.id == 0 && emptyVertexSnapshot.count == 0 &&
                  emptyVertexSnapshot.nativeByteSize == 0,
              "empty vertex uploads remain allocation-free no-ops");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglBufferTest game;
    game.Run();
    return game.getResultProperty();
}
