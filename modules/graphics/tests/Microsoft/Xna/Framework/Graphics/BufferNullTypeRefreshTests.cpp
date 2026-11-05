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

// Refresh FromType IL_0000-0018 runs before the VB/DVB count check.
TEST(BufferNullTypeRefreshTest, NullVertexTypePrecedesTheCount)
{
    GraphicsDevice device;
    for (int count : {-1, 0, 1}) {
        const System::Type nullType;
        ExpectArgument<System::ArgumentNullException>([&] {
            VertexBuffer b(device, nullType, count, BufferUsage::None);
        }, "vertexType", "This method does not accept null for this parameter.");
        ExpectArgument<System::ArgumentNullException>([&] {
            DynamicVertexBuffer b(device, nullType, count, BufferUsage::WriteOnly);
        }, "vertexType", "This method does not accept null for this parameter.");
    }
}

// Refresh IB/DIB Type .ctor IL_0021-0022 calls Marshal.SizeOf(Type);
// pinned Microsoft mscorlib Marshal.SizeOf(Type) IL_0000-0013 names "t".
TEST(BufferNullTypeRefreshTest, PositiveIndexCountReachesMarshalsNullTypeRefusal)
{
    GraphicsDevice device;
    const System::Type nullType;
    ExpectArgument<System::ArgumentNullException>([&] {
        IndexBuffer b(device, nullType, 1, BufferUsage::None);
    }, "t", "Value cannot be null.");
    ExpectArgument<System::ArgumentNullException>([&] {
        DynamicIndexBuffer b(device, nullType, 1, BufferUsage::WriteOnly);
    }, "t", "Value cannot be null.");
}
