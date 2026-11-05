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

#include <string>
#include <type_traits>
namespace {
constexpr const char* NullMessage = "This method does not accept null for this parameter.";
constexpr const char* IndexMessage = "This parameter must be a valid index within the array.";
constexpr const char* SizeMessage = "The array is not the correct size for the amount of data requested.";
constexpr const char* StrideMessage = "The vertex stride is too small for the type of data requested. This is not allowed.";
constexpr const char* InUseMessage = "You may not call SetData on a resource while it is actively set on the GraphicsDevice. Unset it from the device before calling SetData.";
constexpr const char* WriteOnlyMessage = "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.";
template<class E, class F>
void Refusal(F&& action, const char* body, const char* parameter = "")
{
    try { action(); ADD_FAILURE() << "accepted invalid transfer"; }
    catch (const E& error) {
        EXPECT_EQ(typeid(error), typeid(E));
        const std::string message = error.getMessageProperty();
        if constexpr (std::is_base_of_v<System::ArgumentException, E>) {
            EXPECT_EQ(error.getParamNameProperty(), parameter);
            const std::string name(parameter);
            // SharpRuntime supports .NET Framework and modern .NET suffixes. The XNA-owned
            // message body is exact in both modes; no inferred/substring message assertion.
            EXPECT_TRUE(message == std::string(body) + "\nParameter name: " + name ||
                        message == std::string(body) + " (Parameter '" + name + "')") << message;
        } else EXPECT_EQ(message, body);
        if constexpr (std::is_same_v<E, System::ArgumentOutOfRangeException>)
            EXPECT_TRUE(error.getActualValueProperty().empty()); // XNA uses the two-argument ctor.
    }
    catch (...) { ADD_FAILURE() << "wrong exception type"; }
}
template<class T>
void CheckVertexRefusals(VertexBuffer& buffer)
{
    const int stride = std::is_same_v<T, VertexPositionColor> ? 16 : 4;
    std::array<T, 3> data{};
    Refusal<System::ArgumentNullException>([&] { buffer.SetData(static_cast<const T*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::ArgumentNullException>([&] { buffer.GetData(static_cast<T*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.SetData(-1, data.data(), -1, 0, 1); }, IndexMessage, "dataIndex");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.GetData(-1, data.data(), -1, 0, 1); }, IndexMessage, "dataIndex");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.SetData(16, data.data(), 0, 0, 1); }, IndexMessage, "elementCount");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.GetData(16, data.data(), 0, 0, 1); }, IndexMessage, "elementCount");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.SetData(16, data.data(), 0, 1, stride-1); }, StrideMessage, "vertexStride");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.GetData(16, data.data(), 0, 1, stride-1); }, StrideMessage, "vertexStride");
    Refusal<System::InvalidOperationException>([&] { buffer.SetData(16, data.data(), 0, 1, stride); }, SizeMessage);
    Refusal<System::InvalidOperationException>([&] { buffer.GetData(16, data.data(), 0, 1, stride); }, SizeMessage);
}
}

// Refresh VB.CopyData<T>: null IL_0015-0023/02be-02cd; window IL_009f-00a4;
// stride IL_00b7-00d6; size IL_00eb-0100. Helpers.ValidateCopyParameters
// (Framework.dll) IL_0000-0041 uses MustBeValidIndex and no ActualValue.
// Message bodies are the shipped FrameworkResources strings, retained in lane resources.json.
TEST(BufferRefreshDiagnosticTest, GenericAndPackedVertexPathsUseExactMessagesAndPrecedence)
{
    GraphicsDevice device;
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    CheckVertexRefusals<std::uint32_t>(buffer);
    CheckVertexRefusals<VertexPositionColor>(buffer);
}

// Refresh IB.CopyData<T> null IL_0015-001d/0196-01a5; window IL_0068-006d;
// size IL_0072-0091. All public overloads funnel to CopyData<T>.
TEST(BufferRefreshDiagnosticTest, IndexPathsUseExactMessagesAndPrecedence)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    std::array<std::uint16_t, 3> data{};
    Refusal<System::ArgumentNullException>([&] { buffer.SetData(static_cast<const std::uint16_t*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::ArgumentNullException>([&] { buffer.GetData(static_cast<std::uint16_t*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.SetData(-1, data.data(), -1, 0); }, IndexMessage, "dataIndex");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.GetData(-1, data.data(), -1, 0); }, IndexMessage, "dataIndex");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.SetData(4, data.data(), 0, 0); }, IndexMessage, "elementCount");
    Refusal<System::ArgumentOutOfRangeException>([&] { buffer.GetData(4, data.data(), 0, 0); }, IndexMessage, "elementCount");
    Refusal<System::InvalidOperationException>([&] { buffer.SetData(data.data(), 3); }, SizeMessage);
    Refusal<System::InvalidOperationException>([&] { buffer.GetData(data.data(), 3); }, SizeMessage);
}

// VB.CopyData IL_002c-009e and IB.CopyData IL_0028-0067: null precedes
// binding/WriteOnly, and resource state precedes an invalid copy window.
TEST(BufferRefreshDiagnosticTest, BoundAndWriteOnlyFailuresHaveTheRefreshDiagnostic)
{
    GraphicsDevice device;
    VertexBuffer vertex(device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::WriteOnly);
    IndexBuffer index(device, IndexElementSize::SixteenBits, 2, BufferUsage::WriteOnly);
    std::uint16_t item = 9;
    device.SetVertexBuffer(&vertex); device.SetIndexBuffer(&index);
    Refusal<System::ArgumentNullException>([&] { vertex.SetData(static_cast<const std::uint16_t*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::ArgumentNullException>([&] { index.SetData(static_cast<const std::uint16_t*>(nullptr), -1, 0); }, NullMessage, "data");
    Refusal<System::InvalidOperationException>([&] { vertex.SetData(&item, -1, 0); }, InUseMessage);
    Refusal<System::InvalidOperationException>([&] { index.SetData(&item, -1, 0); }, InUseMessage);
    Refusal<System::NotSupportedException>([&] { vertex.GetData(&item, -1, 0); }, WriteOnlyMessage);
    Refusal<System::NotSupportedException>([&] { index.GetData(&item, -1, 0); }, WriteOnlyMessage);
    EXPECT_EQ(item, 9);
}

// IB.CopyData IL_000d-0010 -> Helpers.CheckDisposed IL_000d-0018 names obj.GetType().Name.
TEST(BufferRefreshDiagnosticTest, DisposedIndexTransfersNameTheRuntimeClassBeforeNull)
{
    GraphicsDevice device;
    IndexBuffer plain(device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    DynamicIndexBuffer dynamic(device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    plain.Dispose(); dynamic.Dispose();
    const auto verify = [](auto&& action, const char* name) {
        try { action(); ADD_FAILURE() << "disposed transfer accepted"; }
        catch (const System::ObjectDisposedException& error) {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), name);
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    };
    for (IndexBuffer* buffer : {&plain, static_cast<IndexBuffer*>(&dynamic)}) {
        const char* name = buffer == &plain ? "IndexBuffer" : "DynamicIndexBuffer";
        verify([&] { buffer->SetData(static_cast<const std::uint16_t*>(nullptr), -1, 0); }, name);
        verify([&] { buffer->GetData(static_cast<std::uint16_t*>(nullptr), -1, 0); }, name);
        verify([&] { buffer->SetData(0, static_cast<const std::uint8_t*>(nullptr), -1, 0); }, name);
        verify([&] { buffer->GetData(0, static_cast<std::uint8_t*>(nullptr), -1, 0); }, name);
    }
    verify([&] { dynamic.SetData(static_cast<const std::uint16_t*>(nullptr), -1, 0, SetDataOptions::Discard); }, "DynamicIndexBuffer");
    verify([&] { dynamic.SetData(0, static_cast<const std::uint8_t*>(nullptr), -1, 0, SetDataOptions::Discard); }, "DynamicIndexBuffer");
}
