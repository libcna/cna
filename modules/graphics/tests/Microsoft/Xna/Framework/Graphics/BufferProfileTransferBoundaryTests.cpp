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

#include "System/Type.hpp"
// Refresh VertexElementValidator.Validate(profile,elements) IL_00b5-00f9 checks
// usageIndex < 0 or >= 16 at binding, rather than in VertexElement/the declaration ctor.
TEST(BufferProfileTransferBoundaryTest, UsageIndexFifteenBindsButNeighboringInvalidIndicesDoNot)
{
    GraphicsDevice device;
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef}) {
        device.SetGraphicsProfileEXT(profile);
        for (const int usageIndex : {-1,15,16}) {
            const VertexDeclaration declaration(4, {
                VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, usageIndex)});
            EXPECT_EQ(declaration.GetVertexElements()[0].getUsageIndexProperty(), usageIndex);
            if (usageIndex == 15) {
                VertexBuffer plain(device, declaration, 1, BufferUsage::None);
                DynamicVertexBuffer dynamic(device, declaration, 1, BufferUsage::None);
                EXPECT_EQ(plain.getVertexDeclarationProperty().GetVertexElements()[0].getUsageIndexProperty(), 15);
                EXPECT_EQ(dynamic.getVertexDeclarationProperty().getVertexStrideProperty(), 4);
            } else {
                const auto check = [&](auto&& construct) {
                    try { construct(); ADD_FAILURE() << "out-of-range usage index bound"; }
                    catch (const System::ArgumentException& error) {
                        EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
                        EXPECT_TRUE(error.getParamNameProperty().empty());
                        EXPECT_EQ(error.getMessageProperty(), "Invalid VertexDeclaration. Usage Position" + std::to_string(usageIndex) + " is out of range.");
                    }
                    catch (...) { ADD_FAILURE() << "wrong exception type"; }
                };
                check([&] { VertexBuffer b(device, declaration, 1, BufferUsage::None); });
                check([&] { DynamicVertexBuffer b(device, declaration, 1, BufferUsage::None); });
            }
        }
    }
}

// Refresh IndexBuffer.CreateBuffer IL_0007-0036 checks IndexElementSize32 profile capability.
// Static/dynamic enum and Type constructors all reach it; 16-bit buffers are permitted on Reach.
TEST(BufferProfileTransferBoundaryTest, ReachIndexRestrictionCoversDynamicAndTypeConstructors)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    const char* message = "XNA Framework Reach profile does not support 32 bit indices. Use IndexElementSize.SixteenBits or a type that has a size of two bytes.";
    const auto refused = [&](auto&& action) {
        try { action(); ADD_FAILURE() << "Reach accepted 32-bit indices"; }
        catch (const System::NotSupportedException& error) {
            EXPECT_EQ(typeid(error), typeid(System::NotSupportedException));
            EXPECT_EQ(error.getMessageProperty(), message);
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    };
    refused([&] { IndexBuffer b(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None); });
    refused([&] { DynamicIndexBuffer b(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None); });
    refused([&] { IndexBuffer b(device, System::Type::From<std::uint32_t>(), 1, BufferUsage::None); });
    refused([&] { DynamicIndexBuffer b(device, System::Type::From<std::uint32_t>(), 1, BufferUsage::None); });
    IndexBuffer plain(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic(device, System::Type::From<std::uint16_t>(), 1, BufferUsage::None);
    EXPECT_EQ(plain.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(dynamic.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    IndexBuffer wide(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamicWide(device, System::Type::From<std::uint32_t>(), 1, BufferUsage::None);
    EXPECT_EQ(wide.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
    EXPECT_EQ(dynamicWide.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
}
