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

// Refresh DVB/DIB SetContentLost IL_0000-0016 stores true before every event;
// add/remove_ContentLost IL_0000-0017 compose/remove handlers without a replay.
TEST(BufferContentLostRefreshTest, RepeatedNotificationsAndRemovalPreserveSenderAndFlag)
{
    GraphicsDevice device;
    const auto declaration = PositionDeclaration();
    DynamicVertexBuffer vertex(device, declaration, 2, BufferUsage::None);
    DynamicIndexBuffer index(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const auto exercise = [](auto& buffer) {
        int events = 0;
        const auto token = buffer.ContentLost.Add([&](System::Object* sender, const System::EventArgs& args) {
            ++events;
            EXPECT_EQ(sender, &buffer);
            EXPECT_EQ(&args, &System::EventArgs::Empty);
            EXPECT_TRUE(buffer.getIsContentLostProperty());
        });
        EXPECT_EQ(events, 0);
        EXPECT_FALSE(buffer.getIsContentLostProperty());
        buffer.NotifyContentLostEXT();
        buffer.NotifyContentLostEXT();
        EXPECT_EQ(events, 2);
        EXPECT_TRUE(buffer.getIsContentLostProperty());
        buffer.ContentLost.Remove(token);
        buffer.ClearContentLostEXT();
        EXPECT_FALSE(buffer.getIsContentLostProperty());
        EXPECT_EQ(events, 2); // SetContentLost(false) never raises.
        buffer.NotifyContentLostEXT();
        EXPECT_TRUE(buffer.getIsContentLostProperty());
        EXPECT_EQ(events, 2);
        buffer.Dispose();
        EXPECT_TRUE(buffer.getIsContentLostProperty()); // disposing does not clear the latch.
    };
    exercise(vertex);
    exercise(index);
}

// Refresh get_IsContentLost IL_0000-001f latches the parent IsDeviceLost result;
// GraphicsDevice.get_IsDeviceLost IL_0000-000b returns true for a released native device.
// The query itself does not raise ContentLost, even though it sets the latch.
TEST(BufferContentLostRefreshTest, ADisposedDeviceIsDetectedByTheGetterWithoutAnEvent)
{
    GraphicsDevice device;
    const auto declaration = PositionDeclaration();
    DynamicVertexBuffer vertex(device, declaration, 2, BufferUsage::None);
    DynamicIndexBuffer index(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    int events = 0;
    vertex.ContentLost += [&](System::Object*, const System::EventArgs&) { ++events; };
    index.ContentLost += [&](System::Object*, const System::EventArgs&) { ++events; };
    EXPECT_FALSE(vertex.getIsContentLostProperty());
    EXPECT_FALSE(index.getIsContentLostProperty());
    device.Dispose();
    const auto& constVertex = vertex;
    const auto& constIndex = index;
    EXPECT_TRUE(constVertex.getIsContentLostProperty());
    EXPECT_TRUE(constIndex.getIsContentLostProperty());
    EXPECT_TRUE(vertex.getIsContentLostProperty());
    EXPECT_TRUE(index.getIsContentLostProperty());
    EXPECT_EQ(events, 0);
}
