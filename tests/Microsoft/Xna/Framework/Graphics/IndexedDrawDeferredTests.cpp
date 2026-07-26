// SPDX-License-Identifier: MS-PL
// REMED-GFX-104: deferred indexed-draw correctness and WebGPU native alignment.

#include <array>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#ifdef CNA_BACKEND_WEBGPU
#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    std::array<VertexPositionColor, 3> TriangleAt(float centerX, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX, 0.45f, 0.5f), color),
        };
    }

    std::array<VertexPositionColor, 3> CenterTriangle(const Color& color)
    {
        return TriangleAt(0.0f, color);
    }

    bool ColorNear(const Color& actual, const Color& expected, int tolerance = 16)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
               std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
               std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance;
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        const Rectangle region(
            viewport.getWidthProperty() / 2,
            viewport.getHeightProperty() / 2,
            1,
            1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    Color ReadAtNdc(GraphicsDevice& device, float x)
    {
        const auto& viewport = device.getViewportProperty();
        const int pixelX = static_cast<int>(
            (x * 0.5f + 0.5f) * static_cast<float>(viewport.getWidthProperty() - 1));
        const Rectangle region(
            pixelX,
            viewport.getHeightProperty() / 2,
            1,
            1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    class IndexedDrawDeferredTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void RequireIndexedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support indexed rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };

#ifdef CNA_BACKEND_WEBGPU
    struct WebGpuErrorScopeState
    {
        bool completed = false;
        WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
        WGPUErrorType type = WGPUErrorType_Unknown;
        std::string message;
    };

    void OnWebGpuErrorScope(WGPUPopErrorScopeStatus status,
                            WGPUErrorType type,
                            WGPUStringView message,
                            void* userdata1,
                            void*)
    {
        auto& state = *static_cast<WebGpuErrorScopeState*>(userdata1);
        state.status = status;
        state.type = type;
        if (message.data != nullptr)
        {
            if (message.length == WGPU_STRLEN)
                state.message = message.data;
            else
                state.message.assign(message.data, message.length);
        }
        state.completed = true;
    }

    void PopAndExpectClean(
        CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend& backend)
    {
        WebGpuErrorScopeState state;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnWebGpuErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(backend.Device(), callback);
        for (int attempt = 0; attempt < 10000 && !state.completed; ++attempt)
            wgpuInstanceProcessEvents(backend.Instance());

        ASSERT_TRUE(state.completed) << "wgpu-native did not complete the error scope";
        EXPECT_EQ(WGPUPopErrorScopeStatus_Success, state.status) << state.message;
        EXPECT_EQ(WGPUErrorType_NoError, state.type) << state.message;
        EXPECT_TRUE(state.message.empty()) << state.message;
    }

    template<typename T>
    void ExpectExactWebGpuIndexShadow(IndexBuffer& buffer,
                                      const std::vector<T>& indices)
    {
        auto* backend =
            dynamic_cast<CNA::Internal::Backends::WebGPU::WebGPUIndexBufferBackend*>(
                &buffer.GetBackend());
        ASSERT_NE(nullptr, backend);
        ASSERT_EQ(
            indices.size() * sizeof(T),
            backend->ShadowData().size());
        EXPECT_EQ(0, std::memcmp(
            indices.data(), backend->ShadowData().data(),
            backend->ShadowData().size()));
        EXPECT_EQ(static_cast<int>(indices.size()), backend->GetIndexCount());
    }
#endif
}

#if defined(CNA_BACKEND_WEBGPU) || defined(CNA_BACKEND_VULKAN) || \
    defined(CNA_BACKEND_EASYGL) || defined(CNA_BACKEND_D3D9) || \
    defined(CNA_BACKEND_D3D11)
TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsNonzeroStartIndex)
{
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto green = CenterTriangle(Color::Lime);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], green[0], green[1], green[2],
    };
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 6);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        3,
        3,
        3,
        1);

    EXPECT_TRUE(ColorNear(ReadCenter(device), Color::Lime));
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsPositiveBaseVertex)
{
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto blue = CenterTriangle(Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], blue[0], blue[1], blue[2],
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        3,
        0,
        3,
        0,
        1);

    EXPECT_TRUE(ColorNear(ReadCenter(device), Color::Blue));
}
#endif

#if defined(CNA_BACKEND_WEBGPU) || defined(CNA_BACKEND_VULKAN) || \
    defined(CNA_BACKEND_EASYGL) || defined(CNA_BACKEND_D3D9) || \
    defined(CNA_BACKEND_D3D11) || defined(CNA_BACKEND_SOFTWARE)
TEST_F(IndexedDrawDeferredTest, DeferredAtoBtoACapturesDataCountsAndLifetimes)
{
    RequireIndexedRendering();

    const auto left = TriangleAt(-0.65f, Color::Red);
    const auto center = TriangleAt(0.0f, Color::Lime);
    const auto right = TriangleAt(0.65f, Color::Blue);
    std::array<VertexPositionColor, 9> vertices{
        left[0], left[1], left[2],
        center[0], center[1], center[2],
        right[0], right[1], right[2],
    };
    const std::array<std::uint16_t, 3> leftIndices{0, 1, 2};
    const std::array<std::uint16_t, 3> degenerateIndices{6, 6, 6};
    const std::array<std::uint16_t, 3> rightIndices{6, 7, 8};
    const std::array<std::uint16_t, 9> centerTriangles{
        3, 4, 5, 3, 4, 5, 3, 4, 5};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer staticA(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    DynamicIndexBuffer dynamicB(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    staticA.SetData(leftIndices.data(), 3);
    dynamicB.SetData(
        centerTriangles.data(), 0, 9, SetDataOptions::Discard);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    // Queue A, mutate its backend shadow, queue B, restore A with different data, then queue A
    // again. Each deferred command must retain its own bytes, logical count, and buffer identity.
    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 9, 0, 1);
    staticA.SetData(degenerateIndices.data(), 3);

    device.SetIndexBuffer(&dynamicB);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 0, 3);

    staticA.SetData(rightIndices.data(), 3);
    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 6, 3, 0, 1);

    // Deferred snapshots must remain valid after public objects and caller storage change.
    for (auto& vertex : vertices)
        vertex = VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black);
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    staticA.Dispose();
    dynamicB.Dispose();
    vertexBuffer.Dispose();

    EXPECT_TRUE(ColorNear(ReadAtNdc(device, -0.65f), Color::Red));
    EXPECT_TRUE(ColorNear(ReadAtNdc(device, 0.0f), Color::Lime));
    EXPECT_TRUE(ColorNear(ReadAtNdc(device, 0.65f), Color::Blue));
    EXPECT_TRUE(ColorNear(ReadAtNdc(device, -0.98f), Color::Black));
}

TEST_F(IndexedDrawDeferredTest, DrawUserIndexedCapturesOddOffsetsWidthsAndDeclaration)
{
    RequireIndexedRendering();

    auto left = TriangleAt(-0.65f, Color::Red);
    auto center = TriangleAt(0.0f, Color::Lime);
    auto right = TriangleAt(0.65f, Color::Blue);
    std::array<std::uint16_t, 5> indices16A{99, 99, 0, 1, 2};
    std::array<std::uint16_t, 4> indices16B{99, 0, 1, 2};
    std::array<std::uint32_t, 5> indices32{99, 99, 0, 1, 2};

    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);
    std::array<CompactPositionColor, 3> compactCenter{};
    for (std::size_t i = 0; i < center.size(); ++i)
    {
        compactCenter[i] = {
            center[i].Position.X,
            center[i].Position.Y,
            center[i].Position.Z,
            center[i].Color.getRProperty(),
            center[i].Color.getGProperty(),
            center[i].Color.getBProperty(),
            center[i].Color.getAProperty(),
        };
    }

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        left.data(), 0, 3,
        indices16A.data(), 2, 1);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        compactCenter.data(), 0, 3,
        indices16B.data(), 1, 1,
        PositionColorDeclaration());
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        right.data(), 0, 3,
        indices32.data(), 2, 1);

    // The temporary public arrays and temporary backend buffers are not live at replay time.
    left = CenterTriangle(Color::Black);
    center = CenterTriangle(Color::Black);
    right = CenterTriangle(Color::Black);
    indices16A.fill(0);
    indices16B.fill(0);
    indices32.fill(0);
    compactCenter.fill({});

    EXPECT_TRUE(ColorNear(ReadAtNdc(device, -0.65f), Color::Red));
    EXPECT_TRUE(ColorNear(ReadAtNdc(device, 0.0f), Color::Lime));
    EXPECT_TRUE(ColorNear(ReadAtNdc(device, 0.65f), Color::Blue));
}
#endif

#ifdef CNA_BACKEND_WEBGPU
TEST_F(IndexedDrawDeferredTest, WebGpuNativeScopesCoverLogicalCountsAndInternalPadding)
{
    RequireIndexedRendering();

    auto* backend =
        dynamic_cast<CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend*>(
            &device.GetBackend());
    ASSERT_NE(nullptr, backend);
    const std::size_t uncapturedBefore = backend->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(backend->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(backend->Device(), WGPUErrorFilter_Validation);

    const auto vertices = CenterTriangle(Color::White);
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    const auto queue16 = [&](const std::vector<std::uint16_t>& indices,
                             PrimitiveType primitive,
                             int primitiveCount)
    {
        IndexBuffer indexBuffer(
            device, IndexElementSize::SixteenBits,
            static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
        ExpectExactWebGpuIndexShadow(indexBuffer, indices);
        device.SetIndexBuffer(&indexBuffer);
        device.DrawIndexedPrimitives(
            primitive, 0, 0, 3, 0, primitiveCount);
        device.SetIndexBuffer(nullptr);
        indexBuffer.Dispose();
    };

    queue16({0}, PrimitiveType::PointListEXT, 1);
    queue16({0, 1}, PrimitiveType::PointListEXT, 2);
    queue16({0, 1, 2}, PrimitiveType::TriangleList, 1);
    queue16({0, 1, 2, 0}, PrimitiveType::PointListEXT, 4);
    queue16({0, 1, 2, 0, 1}, PrimitiveType::PointListEXT, 5);
    queue16({0, 1, 2, 0, 1, 2}, PrimitiveType::TriangleList, 2);
    queue16({0, 1, 2, 0, 1, 2, 0, 1, 2}, PrimitiveType::TriangleList, 3);

    const std::vector<std::uint16_t> dynamicIndices{0, 1, 2, 0, 1};
    DynamicIndexBuffer dynamicBuffer(
        device, IndexElementSize::SixteenBits, 5, BufferUsage::None);
    dynamicBuffer.SetData(
        dynamicIndices.data(), 0, 5, SetDataOptions::NoOverwrite);
    ExpectExactWebGpuIndexShadow(dynamicBuffer, dynamicIndices);
    device.SetIndexBuffer(&dynamicBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::PointListEXT, 0, 0, 3, 0, 5);
    device.SetIndexBuffer(nullptr);
    dynamicBuffer.Dispose();

    const std::vector<std::uint32_t> indices32{0, 1, 2};
    IndexBuffer buffer32(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    buffer32.SetData(indices32.data(), 3);
    ExpectExactWebGpuIndexShadow(buffer32, indices32);
    device.SetIndexBuffer(&buffer32);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetIndexBuffer(nullptr);
    buffer32.Dispose();

    // DrawUser's function-local temporary buffers take the same deferred snapshot path.
    const std::array<std::uint16_t, 3> userIndices{0, 1, 2};
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        vertices.data(), 0, 3,
        userIndices.data(), 0, 1);

    device.SetVertexBuffer(nullptr);
    vertexBuffer.Dispose();
    device.Present();

    PopAndExpectClean(*backend);
    PopAndExpectClean(*backend);
    EXPECT_EQ(uncapturedBefore, backend->GetUncapturedErrorCountEXT());
}
#endif
