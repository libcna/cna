// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Graphics/VertexTypeRegistryEXT.hpp"
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

// Refresh VB.CreateBuffer IL_00ee-00f0 stores the caller declaration reference;
// get_VertexDeclaration IL_0000-0006 returns it, not a metadata clone.
TEST(BufferDeclarationSharingRefreshTest, DeclarationMetadataAndDisposalAreSharedAcrossBuffers)
{
    GraphicsDevice device;
    auto declaration = PositionDeclaration();
    auto tag = PositionDeclaration();
    VertexBuffer plain(device, declaration, 2, BufferUsage::None);
    DynamicVertexBuffer dynamic(device, declaration, 3, BufferUsage::None);
    declaration.setNameProperty("source-layout");
    declaration.setTagProperty(&tag);
    EXPECT_EQ(plain.getVertexDeclarationProperty().getNameProperty(), "source-layout");
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getNameProperty(), "source-layout");
    EXPECT_EQ(plain.getVertexDeclarationProperty().getTagProperty(), &tag);
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getTagProperty(), &tag);
    plain.getVertexDeclarationProperty().setNameProperty("changed-through-buffer");
    dynamic.getVertexDeclarationProperty().setTagProperty(nullptr);
    EXPECT_EQ(declaration.getNameProperty(), "changed-through-buffer");
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getNameProperty(), "changed-through-buffer");
    EXPECT_EQ(declaration.getTagProperty(), nullptr);
    EXPECT_EQ(plain.getVertexDeclarationProperty().getTagProperty(), nullptr);
    dynamic.getVertexDeclarationProperty().Dispose();
    EXPECT_TRUE(declaration.getIsDisposedProperty());
    EXPECT_TRUE(plain.getVertexDeclarationProperty().getIsDisposedProperty());
    EXPECT_TRUE(dynamic.getVertexDeclarationProperty().getIsDisposedProperty());
    // Disposing a declaration does not dispose a buffer that refers to it.
    EXPECT_FALSE(plain.getIsDisposedProperty());
    EXPECT_FALSE(dynamic.getIsDisposedProperty());
    EXPECT_EQ(plain.getVertexCountProperty(), 2);
    EXPECT_EQ(dynamic.getVertexCountProperty(), 3);
}

namespace {
struct RegisteredBufferVertex : IVertexType {
    static VertexDeclaration& Declaration() {
        static auto declaration = PositionDeclaration();
        return declaration;
    }
    const VertexDeclaration& getVertexDeclarationProperty() const override { return Declaration(); }
};
}
// Refresh FromType IL_0077-007e,00d2-00d3 returns IVertexType's declaration.
// CNAEXT registration supplies that reference identity without .NET reflection.
TEST(BufferDeclarationSharingRefreshTest, TypeConstructorsShareTheRegisteredDeclarationMetadata)
{
    CNA::Graphics::VertexTypeRegistryEXT::Register<RegisteredBufferVertex>();
    GraphicsDevice device;
    auto& declaration = RegisteredBufferVertex::Declaration();
    VertexBuffer plain(device, System::Type::From<RegisteredBufferVertex>(), 2, BufferUsage::None);
    DynamicVertexBuffer dynamic(device, System::Type::From<RegisteredBufferVertex>(), 3, BufferUsage::None);
    auto tag = PositionDeclaration();
    declaration.setNameProperty("registered-layout");
    plain.getVertexDeclarationProperty().setTagProperty(&tag);
    EXPECT_EQ(plain.getVertexDeclarationProperty().getNameProperty(), "registered-layout");
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getNameProperty(), "registered-layout");
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getTagProperty(), &tag);
    EXPECT_EQ(declaration.getTagProperty(), &tag);
    plain.Dispose();
    EXPECT_FALSE(declaration.getIsDisposedProperty());
    EXPECT_FALSE(dynamic.getIsDisposedProperty());
    EXPECT_EQ(dynamic.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &device);
    declaration.setNameProperty("");
    declaration.setTagProperty(nullptr); // no dangling metadata in the process-wide registry.
}

// Refresh VB.CreateBuffer IL_0024-003e calls Bind if declaration.parent differs;
// VertexDeclaration.Bind IL_0068-006f updates that same declaration's parent.
TEST(BufferDeclarationSharingRefreshTest, BindingTheSameDeclarationOnAnotherDeviceUpdatesItsSharedParent)
{
    GraphicsDevice firstDevice;
    GraphicsDevice secondDevice;
    auto declaration = PositionDeclaration();
    VertexBuffer first(firstDevice, declaration, 2, BufferUsage::None);
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &firstDevice);
    DynamicVertexBuffer second(secondDevice, declaration, 3, BufferUsage::None);
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &secondDevice);
    EXPECT_EQ(first.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &secondDevice);
    EXPECT_EQ(second.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &secondDevice);
    EXPECT_EQ(first.getGraphicsDeviceProperty(), &firstDevice);
    EXPECT_EQ(second.getGraphicsDeviceProperty(), &secondDevice);
}
