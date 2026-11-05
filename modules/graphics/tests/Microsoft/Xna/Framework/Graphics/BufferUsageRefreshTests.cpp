// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
using namespace Microsoft::Xna::Framework::Graphics;

// Oracle: Refresh ConvertXnaBufferUsageToDx IL_0000-0009 keeps only bit 0;
// ConvertDxBufferUsageToXna IL_0000-0008 returns canonical None/WriteOnly.
// CopyData tests the resulting native write-only bit before copy-window validation.
TEST(BufferUsageRefreshTest, EveryBufferNormalizesUsageAndRefusesWriteOnlyReadback)
{
    GraphicsDevice device;
    const VertexDeclaration declaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
    for (const int bits : {0,1,2,3,4,5,-1,-2})
    {
        SCOPED_TRACE(bits);
        const bool writeOnly = (bits & 1) != 0;
        const BufferUsage usage = static_cast<BufferUsage>(bits);
        VertexBuffer vertex(device, declaration, 1, usage);
        DynamicVertexBuffer dynamicVertex(device, declaration, 1, usage);
        IndexBuffer index(device, IndexElementSize::SixteenBits, 2, usage);
        DynamicIndexBuffer dynamicIndex(device, IndexElementSize::SixteenBits, 2, usage);
        const auto verify = [&](auto& buffer) {
            EXPECT_EQ(buffer.getBufferUsageProperty(),
                      writeOnly ? BufferUsage::WriteOnly : BufferUsage::None);
            const std::array<std::uint8_t, 4> source{17,34,51,68};
            buffer.SetData(source.data(), 4);
            std::array<std::uint8_t, 4> result{0xAA,0xAA,0xAA,0xAA};
            if (writeOnly)
            {
                EXPECT_THROW(buffer.GetData(result.data(), 4), System::NotSupportedException);
                EXPECT_EQ(result, (std::array<std::uint8_t, 4>{0xAA,0xAA,0xAA,0xAA}));
            }
            else
            {
                buffer.GetData(result.data(), 4);
                EXPECT_EQ(result, source);
            }
        };
        verify(vertex); verify(dynamicVertex); verify(index); verify(dynamicIndex);
    }
}
