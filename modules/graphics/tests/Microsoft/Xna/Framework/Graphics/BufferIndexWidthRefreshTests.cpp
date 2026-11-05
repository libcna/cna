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

// Refresh CreateBuffer IL_0038-0041,0125-012f rejects sizes other than two/four,
// with ArgumentException(message), not ArgumentException(message, "indexType").
// Literal marshaled widths: Byte=1, Int64=8. Float=4 is report-only (see DOWNSTREAM).
TEST(BufferIndexWidthRefreshTest, UnusablePrimitiveWidthsHaveNoParameterName)
{
    GraphicsDevice device;
    for (const auto type : {System::Type::From<std::uint8_t>(), System::Type::From<std::int64_t>()}) {
        ExpectArgument<System::ArgumentException>([&] {
            IndexBuffer b(device, type, 2, BufferUsage::None);
        }, "", "IndexBuffers may be created only with types that are 16 bit or 32 bit in size.");
        ExpectArgument<System::ArgumentException>([&] {
            DynamicIndexBuffer b(device, type, 2, BufferUsage::WriteOnly);
        }, "", "IndexBuffers may be created only with types that are 16 bit or 32 bit in size.");
    }
}
