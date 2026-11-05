// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <string>
#include <typeinfo>
#include <type_traits>
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/Type.hpp"
using namespace Microsoft::Xna::Framework::Graphics;
namespace {
[[maybe_unused]] VertexDeclaration PositionDeclaration()
{
    return VertexDeclaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
}
}

namespace {
template<class B>
void CheckVertex(B& buffer, GraphicsDevice& device, int count, BufferUsage usage,
                 const char* type, int stride)
{
    // Refresh get_VertexCount/get_VertexDeclaration IL_0000-0006; usage conversion
    // masks bit 0; GraphicsResource.get_GraphicsDevice IL_0000-0006 returns parent.
    EXPECT_EQ(buffer.getVertexCountProperty(), count);
    EXPECT_EQ(buffer.getBufferUsageProperty(), usage);
    EXPECT_EQ(buffer.getGraphicsDeviceProperty(), &device);
    const B& constant = buffer;
    EXPECT_EQ(&buffer.getVertexDeclarationProperty(), &constant.getVertexDeclarationProperty());
    EXPECT_EQ(constant.getVertexDeclarationProperty().getVertexStrideProperty(), stride);
    EXPECT_EQ(buffer.GetTypeName(), std::string("Microsoft.Xna.Framework.Graphics.") + type);
    EXPECT_FALSE(buffer.getIsDisposedProperty());
    EXPECT_TRUE(buffer.getNameProperty().empty()); // C++ representation of XNA's initial null name.
    EXPECT_EQ(buffer.getTagProperty(), nullptr);
}
template<class B>
void CheckIndex(B& buffer, GraphicsDevice& device, int count, BufferUsage usage,
                const char* type, IndexElementSize size)
{
    // Refresh IB getters IL_0000-000b/0011/0006 retain constructor metadata.
    EXPECT_EQ(buffer.getIndexCountProperty(), count);
    EXPECT_EQ(buffer.getIndexElementSizeProperty(), size);
    EXPECT_EQ(buffer.getBufferUsageProperty(), usage);
    EXPECT_EQ(buffer.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(buffer.GetTypeName(), std::string("Microsoft.Xna.Framework.Graphics.") + type);
    EXPECT_FALSE(buffer.getIsDisposedProperty());
    EXPECT_TRUE(buffer.getNameProperty().empty());
    EXPECT_EQ(buffer.getTagProperty(), nullptr);
}
template<class B>
void DisposeAndCheckMetadata(B& buffer, GraphicsDevice& device, const char* name)
{
    // Refresh GraphicsResource.~GraphicsResource IL_0000-0026 raises once, after
    // !GraphicsResource publishes isDisposed (IL_0000-0007). Getters have no guard.
    VertexDeclaration tag = PositionDeclaration();
    int events = 0;
    buffer.setNameProperty(name);
    buffer.setTagProperty(&tag);
    buffer.Disposing += [&](System::Object* sender, const System::EventArgs&) {
        ++events;
        EXPECT_EQ(sender, &buffer);
        EXPECT_TRUE(buffer.getIsDisposedProperty());
        EXPECT_FALSE(buffer.HasRenderer());
        EXPECT_EQ(buffer.getNameProperty(), name);
        EXPECT_EQ(buffer.getTagProperty(), &tag);
        EXPECT_EQ(buffer.getGraphicsDeviceProperty(), &device);
    };
    buffer.Dispose();
    buffer.Dispose();
    EXPECT_EQ(events, 1);
    EXPECT_TRUE(buffer.getIsDisposedProperty());
    EXPECT_FALSE(buffer.HasRenderer());
    EXPECT_EQ(buffer.getNameProperty(), name);
    EXPECT_EQ(buffer.getTagProperty(), &tag);
    EXPECT_EQ(buffer.getGraphicsDeviceProperty(), &device);
    // GraphicsResource.set_Name/set_Tag IL_0020-0027 permit writes after Dispose.
    buffer.setNameProperty("after-dispose");
    buffer.setTagProperty(nullptr);
    EXPECT_EQ(buffer.getNameProperty(), "after-dispose");
    EXPECT_EQ(buffer.ToString(), "after-dispose");
    EXPECT_EQ(buffer.getTagProperty(), nullptr);
}
}

TEST(BufferPropertiesRefreshTest, AllVertexConstructorRoutesRetainTheirOwnMetadata)
{
    GraphicsDevice device;
    const auto declaration = PositionDeclaration();
    VertexBuffer explicitLayout(device, declaration, 3, BufferUsage::WriteOnly);
    VertexBuffer typeLayout(device, System::Type::From<VertexPositionColor>(), 5, static_cast<BufferUsage>(3));
    DynamicVertexBuffer dynamicLayout(device, declaration, 7, BufferUsage::None);
    DynamicVertexBuffer dynamicType(device, System::Type::From<VertexPositionColor>(), 11, static_cast<BufferUsage>(2));
    VertexBuffer extension(device, 13);
    CheckVertex(explicitLayout, device, 3, BufferUsage::WriteOnly, "VertexBuffer", 4);
    CheckVertex(typeLayout, device, 5, BufferUsage::WriteOnly, "VertexBuffer", 16);
    CheckVertex(dynamicLayout, device, 7, BufferUsage::None, "DynamicVertexBuffer", 4);
    CheckVertex(dynamicType, device, 11, BufferUsage::None, "DynamicVertexBuffer", 16);
    CheckVertex(extension, device, 13, BufferUsage::None, "VertexBuffer", 0);
    EXPECT_FALSE(dynamicLayout.getIsContentLostProperty());
    EXPECT_FALSE(dynamicType.getIsContentLostProperty());
}

TEST(BufferPropertiesRefreshTest, AllIndexConstructorRoutesRetainTheirOwnMetadata)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    IndexBuffer explicitSize(device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::WriteOnly);
    IndexBuffer typeSize(device, System::Type::From<std::int16_t>(), 5, static_cast<BufferUsage>(3));
    DynamicIndexBuffer dynamicSize(device, IndexElementSize::SixteenBits, 7, BufferUsage::None);
    DynamicIndexBuffer dynamicType(device, System::Type::From<std::uint32_t>(), 11, static_cast<BufferUsage>(2));
    IndexBuffer extension(device, 13);
    CheckIndex(explicitSize, device, 3, BufferUsage::WriteOnly, "IndexBuffer", IndexElementSize::ThirtyTwoBits);
    CheckIndex(typeSize, device, 5, BufferUsage::WriteOnly, "IndexBuffer", IndexElementSize::SixteenBits);
    CheckIndex(dynamicSize, device, 7, BufferUsage::None, "DynamicIndexBuffer", IndexElementSize::SixteenBits);
    CheckIndex(dynamicType, device, 11, BufferUsage::None, "DynamicIndexBuffer", IndexElementSize::ThirtyTwoBits);
    CheckIndex(extension, device, 13, BufferUsage::None, "IndexBuffer", IndexElementSize::SixteenBits);
    EXPECT_FALSE(dynamicSize.getIsContentLostProperty());
    EXPECT_FALSE(dynamicType.getIsContentLostProperty());
}

TEST(BufferPropertiesRefreshTest, DisposalRetainsAllFourBuffersMetadataAndLeavesDeclarationsAlive)
{
    GraphicsDevice device;
    auto declaration = PositionDeclaration();
    VertexBuffer vertex(device, declaration, 3, BufferUsage::WriteOnly);
    DynamicVertexBuffer dynamicVertex(device, declaration, 5, BufferUsage::None);
    IndexBuffer index(device, IndexElementSize::SixteenBits, 7, BufferUsage::WriteOnly);
    DynamicIndexBuffer dynamicIndex(device, IndexElementSize::SixteenBits, 11, BufferUsage::None);
    DisposeAndCheckMetadata(vertex, device, "vertex");
    DisposeAndCheckMetadata(dynamicVertex, device, "dynamic-vertex");
    DisposeAndCheckMetadata(index, device, "index");
    DisposeAndCheckMetadata(dynamicIndex, device, "dynamic-index");
    EXPECT_EQ(vertex.getVertexCountProperty(), 3);
    EXPECT_EQ(dynamicVertex.getVertexCountProperty(), 5);
    EXPECT_EQ(index.getIndexCountProperty(), 7);
    EXPECT_EQ(dynamicIndex.getIndexCountProperty(), 11);
    EXPECT_EQ(vertex.getBufferUsageProperty(), BufferUsage::WriteOnly);
    EXPECT_EQ(dynamicVertex.getBufferUsageProperty(), BufferUsage::None);
    EXPECT_EQ(index.getBufferUsageProperty(), BufferUsage::WriteOnly);
    EXPECT_EQ(dynamicIndex.getBufferUsageProperty(), BufferUsage::None);
    EXPECT_EQ(index.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(dynamicIndex.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(vertex.getVertexDeclarationProperty().getVertexStrideProperty(), 4);
    EXPECT_EQ(dynamicVertex.getVertexDeclarationProperty().getVertexStrideProperty(), 4);
    EXPECT_FALSE(declaration.getIsDisposedProperty());
    EXPECT_FALSE(vertex.getVertexDeclarationProperty().getIsDisposedProperty());
    EXPECT_FALSE(dynamicVertex.getVertexDeclarationProperty().getIsDisposedProperty());
    EXPECT_FALSE(dynamicVertex.getIsContentLostProperty());
    EXPECT_FALSE(dynamicIndex.getIsContentLostProperty());
    // Exact disposed transfer diagnostics are reused from TQ-056, not repeated here.
}

// Refresh IB/DIB Type .ctor uses Marshal.SizeOf(Type), independent of signedness.
TEST(BufferPropertiesRefreshTest, DynamicSignedIndexTypesKeepTheirLiteralWidthAndCount)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    DynamicIndexBuffer narrow(device, System::Type::From<std::int16_t>(), 17, BufferUsage::WriteOnly);
    DynamicIndexBuffer wide(device, System::Type::From<std::int32_t>(), 19, BufferUsage::None);
    CheckIndex(narrow, device, 17, BufferUsage::WriteOnly, "DynamicIndexBuffer", IndexElementSize::SixteenBits);
    CheckIndex(wide, device, 19, BufferUsage::None, "DynamicIndexBuffer", IndexElementSize::ThirtyTwoBits);
}
