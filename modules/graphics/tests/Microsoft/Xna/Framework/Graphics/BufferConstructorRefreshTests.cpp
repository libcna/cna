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
#include "System/AppContext.hpp"
using namespace Microsoft::Xna::Framework::Graphics;
namespace {
[[maybe_unused]] VertexDeclaration PositionDeclaration()
{
    return VertexDeclaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
}
template<class E, class F>
void ExpectArgument(F&& action, const char* parameter, const char* body)
{
    // XNA uses the .NET Framework BCL message profile. Select SharpRuntime's
    // supported profile for this refusal only; restore the caller's setting even on failure.
    struct FrameworkMessageScope {
        bool previous = false;
        FrameworkMessageScope() {
            (void)System::AppContext::TryGetSwitch(
                "SharpRuntime.UseNetFrameworkArgumentExceptionMessages", previous);
            System::AppContext::SetSwitch(
                "SharpRuntime.UseNetFrameworkArgumentExceptionMessages", true);
        }
        ~FrameworkMessageScope() {
            System::AppContext::SetSwitch(
                "SharpRuntime.UseNetFrameworkArgumentExceptionMessages", previous);
        }
    } profile;
    try { action(); ADD_FAILURE() << "expected refusal"; }
    catch (const E& error) {
        EXPECT_EQ(typeid(error), typeid(E));
        EXPECT_EQ(error.getParamNameProperty(), parameter);
        EXPECT_EQ(error.getMessageProperty(), std::string(body) +
            (std::string(parameter).empty() ? "" : "\nParameter name: " + std::string(parameter)));
        if constexpr (std::is_same_v<E, System::ArgumentOutOfRangeException>)
            EXPECT_TRUE(error.getActualValueProperty().empty());
    }
    catch (...) { ADD_FAILURE() << "wrong exception type"; }
}
}

// Refresh VB/DVB declaration .ctor IL_0019-002c and Type .ctor IL_000d-0020;
// IB/DIB .ctor (both overloads) IL_0006-0019. No ActualValue argument is supplied.
TEST(BufferConstructorRefreshTest, EveryOverloadRejectsNonpositiveCountsWithTheRefreshDiagnostic)
{
    GraphicsDevice device;
    const auto declaration = PositionDeclaration();
    for (int count : {0, -1, std::numeric_limits<int>::min()}) {
        SCOPED_TRACE(count);
        const auto check = [&](auto&& action, const char* parameter) {
            ExpectArgument<System::ArgumentOutOfRangeException>(action, parameter,
                "Resource size must be greater than zero.");
        };
        check([&] { VertexBuffer b(device, declaration, count, BufferUsage::None); }, "vertexCount");
        check([&] { DynamicVertexBuffer b(device, declaration, count, BufferUsage::WriteOnly); }, "vertexCount");
        check([&] { VertexBuffer b(device, System::Type::From<VertexPositionColor>(), count, BufferUsage::None); }, "vertexCount");
        check([&] { DynamicVertexBuffer b(device, System::Type::From<VertexPositionColor>(), count, BufferUsage::WriteOnly); }, "vertexCount");
        check([&] { IndexBuffer b(device, IndexElementSize::SixteenBits, count, BufferUsage::None); }, "indexCount");
        check([&] { DynamicIndexBuffer b(device, IndexElementSize::SixteenBits, count, BufferUsage::WriteOnly); }, "indexCount");
        check([&] { IndexBuffer b(device, System::Type::From<std::int16_t>(), count, BufferUsage::None); }, "indexCount");
        check([&] { DynamicIndexBuffer b(device, System::Type::From<std::int16_t>(), count, BufferUsage::WriteOnly); }, "indexCount");
        // CNAEXT convenience constructors share the same count rule.
        check([&] { VertexBuffer b(device, count); }, "vertexCount");
        check([&] { IndexBuffer b(device, count); }, "indexCount");
    }
}

// Refresh VB/DVB count checks precede CreateBuffer's disposed/profile checks;
// IB/DIB count checks precede Marshal.SizeOf and CreateBuffer's Reach32 check.
TEST(BufferConstructorRefreshTest, CountFailurePrecedesDisposedDeclarationIndexTypeAndReachRestrictions)
{
    GraphicsDevice device;
    auto declaration = PositionDeclaration();
    declaration.Dispose();
    for (int count : {0, -7}) {
        const auto check = [&](auto&& action, const char* parameter) {
            ExpectArgument<System::ArgumentOutOfRangeException>(action, parameter,
                "Resource size must be greater than zero.");
        };
        check([&] { VertexBuffer b(device, declaration, count, BufferUsage::None); }, "vertexCount");
        check([&] { DynamicVertexBuffer b(device, declaration, count, BufferUsage::None); }, "vertexCount");
        for (const auto type : {System::Type{}, System::Type::From<std::uint8_t>()}) {
            check([&] { IndexBuffer b(device, type, count, BufferUsage::None); }, "indexCount");
            check([&] { DynamicIndexBuffer b(device, type, count, BufferUsage::None); }, "indexCount");
        }
        check([&] { IndexBuffer b(device, static_cast<IndexElementSize>(-1), count, BufferUsage::None); }, "indexCount");
        check([&] { DynamicIndexBuffer b(device, IndexElementSize::ThirtyTwoBits, count, BufferUsage::None); }, "indexCount");
    }
}

// Refresh enum .ctors IL_0021-0029 map zero to two bytes, every other value to four.
// CreateBuffer IL_0007-0036 enforces Reach32; TQ-056 already covers the named enum.
TEST(BufferConstructorRefreshTest, UndefinedIndexSizesFollowTheNonzeroThirtyTwoBitBranch)
{
    GraphicsDevice device;
    for (int bits : {-1, 2, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        SCOPED_TRACE(bits);
        const auto size = static_cast<IndexElementSize>(bits);
        const auto refused = [&](auto&& action) {
            try { action(); ADD_FAILURE() << "Reach accepted undefined nonzero width"; }
            catch (const System::NotSupportedException& error) {
                EXPECT_EQ(typeid(error), typeid(System::NotSupportedException));
                EXPECT_EQ(error.getMessageProperty(),
                    "XNA Framework Reach profile does not support 32 bit indices. Use IndexElementSize.SixteenBits or a type that has a size of two bytes.");
            }
            catch (...) { ADD_FAILURE() << "wrong exception type"; }
        };
        device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
        refused([&] { IndexBuffer b(device, size, 3, BufferUsage::None); });
        refused([&] { DynamicIndexBuffer b(device, size, 5, BufferUsage::WriteOnly); });
        device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
        const IndexBuffer plain(device, size, 3, BufferUsage::None);
        const DynamicIndexBuffer dynamic(device, size, 5, BufferUsage::WriteOnly);
        EXPECT_EQ(plain.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
        EXPECT_EQ(dynamic.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
        EXPECT_EQ(plain.getIndexCountProperty(), 3);
        EXPECT_EQ(dynamic.getIndexCountProperty(), 5);
    }
}
