// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
template<class T>
void ExpectStaticDeclaration(const char* name, int stride, const std::vector<VertexElement>& elements)
{
    const T first;
    const T second;
    const IVertexType& firstInterface = first;
    const IVertexType& secondInterface = second;
    const auto& declaration = T::getVertexDeclarationStatic();
    // Each Refresh explicit IVertexType getter IL_0000-0005 returns the static field.
    EXPECT_EQ(&firstInterface.getVertexDeclarationProperty(), &declaration);
    EXPECT_EQ(&secondInterface.getVertexDeclarationProperty(), &declaration);
    EXPECT_EQ(declaration.getVertexStrideProperty(), stride);
    EXPECT_EQ(declaration.GetVertexElements(), elements);
    EXPECT_EQ(declaration.getNameProperty(), name);
    EXPECT_EQ(declaration.ToString(), name); // GraphicsResource.ToString IL_0027-0030.
}
}

// Refresh VertexPositionColor::.cctor IL_0000-004a: literal element tuples;
// IL_003a-0045 assigns the declaration name. Sizes sum to 16 bytes.
TEST(VertexStaticDeclarationRefreshTest, VertexPositionColorHasTheRefreshLayoutNameAndInterfaceIdentity)
{
    ExpectStaticDeclaration<VertexPositionColor>("VertexPositionColor.VertexDeclaration", 16, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
}

// Refresh VertexPositionTexture::.cctor IL_0000-004a: literal element tuples;
// IL_003a-0045 assigns the declaration name. Sizes sum to 20 bytes.
TEST(VertexStaticDeclarationRefreshTest, VertexPositionTextureHasTheRefreshLayoutNameAndInterfaceIdentity)
{
    ExpectStaticDeclaration<VertexPositionTexture>("VertexPositionTexture.VertexDeclaration", 20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
}

// Refresh VertexPositionColorTexture::.cctor IL_0000-0060: literal element tuples;
// IL_0050-005b assigns the declaration name. Sizes sum to 24 bytes.
TEST(VertexStaticDeclarationRefreshTest, VertexPositionColorTextureHasTheRefreshLayoutNameAndInterfaceIdentity)
{
    ExpectStaticDeclaration<VertexPositionColorTexture>("VertexPositionColorTexture.VertexDeclaration", 24, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
}

// Refresh VertexPositionNormalTexture::.cctor IL_0000-0060: literal element tuples;
// IL_0050-005b assigns the declaration name. Sizes sum to 32 bytes.
TEST(VertexStaticDeclarationRefreshTest, VertexPositionNormalTextureHasTheRefreshLayoutNameAndInterfaceIdentity)
{
    ExpectStaticDeclaration<VertexPositionNormalTexture>("VertexPositionNormalTexture.VertexDeclaration", 32, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
}
