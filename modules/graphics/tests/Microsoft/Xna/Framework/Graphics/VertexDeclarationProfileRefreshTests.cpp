// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <string>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
std::vector<VertexElement> PositionElements(int count)
{
    std::vector<VertexElement> elements;
    for (int i = 0; i < count; ++i)
        elements.emplace_back(4*i, VertexElementFormat::Single, VertexElementUsage::Position, i);
    return elements;
}
template<class E, class F> void ExpectProfileError(F&& action, const std::string& expected)
{
    try { action(); ADD_FAILURE() << "invalid declaration bound"; }
    catch (const E& error) {
        EXPECT_EQ(typeid(error), typeid(E));
        EXPECT_EQ(error.getMessageProperty(), expected);
    }
    catch (...) { ADD_FAILURE() << "wrong exception type"; }
}
void ExpectBothKindsToReject(GraphicsDevice& device, const VertexDeclaration& declaration, const std::string& message)
{
    ExpectProfileError<System::NotSupportedException>([&] { VertexBuffer b(device, declaration, 1, BufferUsage::None); }, message);
    ExpectProfileError<System::NotSupportedException>([&] { DynamicVertexBuffer b(device, declaration, 1, BufferUsage::None); }, message);
}
}

// Refresh profile validator IL_0007-0046 and ProfileCapabilities::.cctor:
// Reach IL_0059-0066 / HiDef IL_0290-029d: max stride 255, max elements/index bound 16.
// Constructor alignment makes 252 the largest legal stride <=255.
TEST(VertexDeclarationProfileRefreshTest, BothProfilesAccept252ByteStrideAndExactlySixteenElements)
{
    GraphicsDevice device;
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef}) {
        device.SetGraphicsProfileEXT(profile);
        const auto elements = PositionElements(16);
        const VertexDeclaration declaration(252, elements);
        VertexBuffer plain(device, declaration, 1, BufferUsage::None);
        DynamicVertexBuffer dynamic(device, declaration, 1, BufferUsage::None);
        EXPECT_EQ(plain.getVertexDeclarationProperty().getVertexStrideProperty(), 252);
        EXPECT_EQ(dynamic.getVertexDeclarationProperty().getVertexStrideProperty(), 252);
        EXPECT_EQ(plain.getVertexDeclarationProperty().GetVertexElements(), elements);
        EXPECT_EQ(dynamic.getVertexDeclarationProperty().GetVertexElements(), elements);
    }
}

// Refresh ProfileCapabilities valid formats Reach IL_01b8-022c / HiDef IL_06d4-0762;
// validator IL_004e-0091 checks format. Accepted constructions must retain exact tuples.
TEST(VertexDeclarationProfileRefreshTest, EveryAllowedFormatRetainsItsDeclarationInBothBufferKinds)
{
    GraphicsDevice device;
    constexpr int sizes[]{4, 8, 12, 16, 4, 4, 4, 8, 4, 8, 4, 8};
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef}) {
        device.SetGraphicsProfileEXT(profile);
        const int count = profile == GraphicsProfile::Reach ? 10 : 12;
        for (int ordinal = 0; ordinal < count; ++ordinal) {
            SCOPED_TRACE(ordinal);
            const std::vector<VertexElement> elements{
                VertexElement(0, static_cast<VertexElementFormat>(ordinal), VertexElementUsage::TextureCoordinate, 15)};
            const VertexDeclaration declaration(sizes[ordinal], elements);
            VertexBuffer plain(device, declaration, 1, BufferUsage::None);
            DynamicVertexBuffer dynamic(device, declaration, 1, BufferUsage::None);
            EXPECT_EQ(plain.getVertexDeclarationProperty().getVertexStrideProperty(), sizes[ordinal]);
            EXPECT_EQ(dynamic.getVertexDeclarationProperty().getVertexStrideProperty(), sizes[ordinal]);
            EXPECT_EQ(plain.getVertexDeclarationProperty().GetVertexElements(), elements);
            EXPECT_EQ(dynamic.getVertexDeclarationProperty().GetVertexElements(), elements);
        }
    }
}

// Refresh validator(profile) IL_0007-0025 stride,0026-0046 count,004e-0091 format,
//0092-00f9 usage index: earlier errors win. Message templates are shipped FrameworkResources.
TEST(VertexDeclarationProfileRefreshTest, ExactProfileDiagnosticsHonorStrideCountFormatThenUsageOrder)
{
    GraphicsDevice device;
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef}) {
        device.SetGraphicsProfileEXT(profile);
        const std::string prefix = profile == GraphicsProfile::Reach ? "XNA Framework Reach profile " : "XNA Framework HiDef profile ";
        auto seventeen = PositionElements(17); // last index=16 is also invalid at binding.
        seventeen[0].setVertexElementFormatProperty(static_cast<VertexElementFormat>(12));
        ExpectBothKindsToReject(device, VertexDeclaration(256, seventeen), prefix + "supports a maximum vertex stride of 255.");
        ExpectBothKindsToReject(device, VertexDeclaration(252, seventeen), prefix + "supports a maximum of 16 simultaneous vertex elements.");
        for (const int ordinal : {-1, 12}) {
            const VertexDeclaration unknown(4, {
                VertexElement(0, static_cast<VertexElementFormat>(ordinal), VertexElementUsage::Position, 16)});
            ExpectBothKindsToReject(device, unknown, prefix + "does not support VertexElement format " + std::to_string(ordinal) + ".");
        }
    }
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    for (const auto format : {VertexElementFormat::HalfVector2, VertexElementFormat::HalfVector4}) {
        const VertexDeclaration half(8, {VertexElement(0, format, VertexElementUsage::Normal, -1)});
        const char* name = format == VertexElementFormat::HalfVector2 ? "HalfVector2" : "HalfVector4";
        ExpectBothKindsToReject(device, half, std::string("XNA Framework Reach profile does not support VertexElement format ") + name + ".");
    }
}

// Refresh VertexDeclaration.Bind IL_0000-001c checks disposed before calling profile validation IL_002f.
TEST(VertexDeclarationProfileRefreshTest, DisposedDeclarationWinsOverEveryProfileError)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    VertexDeclaration declaration(256, PositionElements(17));
    declaration.Dispose();
    const auto verify = [&](auto&& construct) {
        try { construct(); ADD_FAILURE() << "disposed declaration bound"; }
        catch (const System::ObjectDisposedException& error) {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), "VertexDeclaration");
            EXPECT_EQ(error.getMessageProperty(), "Cannot access a disposed object.\nObject name: 'VertexDeclaration'.");
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    };
    verify([&] { VertexBuffer b(device, declaration, 1, BufferUsage::None); });
    verify([&] { DynamicVertexBuffer b(device, declaration, 1, BufferUsage::None); });
}
