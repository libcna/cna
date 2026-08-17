// SPDX-License-Identifier: MS-PL
// REMED-GFX-054: public regression for CNA's established empty-index-buffer contract.
//
// Empty model parts are represented by a logical zero-capacity IndexBuffer.  Uploading an empty
// range, including the pointer returned by an empty std::vector (which may be null), is a true
// no-op: it must not mutate the renderer's last real upload.  The native allocation, if a renderer
// needs a non-zero minimum internally, must not make a non-empty upload legal.

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

// plan_runtimerenderer.md RTR-P9-9: a compile-time guard, because this block needs the WebGPU
// renderer's own headers. Widened from the DEFAULT-renderer macro to "compiled into this build",
// so a multi-renderer build that contains WebGPU without selecting it still compiles these tests.
#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)
#define CNA_TEST_WEBGPU_AVAILABLE 1
#endif

#ifdef CNA_TEST_WEBGPU_AVAILABLE
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#endif

using CNA::GraphicsCapability;
using CNA::Internal::Renderers::IGraphicsRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    class IndexBufferEmptyDataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from -- invoked from this ordinary
        // member function it printed "Skipped" but let the calling TEST_F body keep running into
        // a real IndexBuffer construction, which then threw on a renderer with no 3D pipeline.
        // The actual gate has to run in SetUp() (a location GoogleTest itself calls directly),
        // which is where GTEST_SKIP() genuinely prevents the test body from executing.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support index buffers";
        }

        void RequireIndexBuffers()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support index buffers";
        }
    };

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

#ifdef CNA_TEST_WEBGPU_AVAILABLE
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

    void PopAndExpectClean(CNA::Internal::Renderers::WebGPU::WebGPURenderer& renderer)
    {
        WebGpuErrorScopeState state;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnWebGpuErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(renderer.Device(), callback);
        for (int attempt = 0; attempt < 10000 && !state.completed; ++attempt)
            wgpuInstanceProcessEvents(renderer.Instance());

        ASSERT_TRUE(state.completed) << "wgpu-native did not complete the error scope";
        EXPECT_EQ(WGPUPopErrorScopeStatus_Success, state.status) << state.message;
        EXPECT_EQ(WGPUErrorType_NoError, state.type) << state.message;
        EXPECT_TRUE(state.message.empty()) << state.message;
    }
#endif
}

TEST_F(IndexBufferEmptyDataTest, ZeroCapacityConstructionPreservesLogicalCapacity)
{
    RequireIndexBuffers();

    IndexBuffer static16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer static32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_EQ(0, static16.getIndexCountProperty());
    EXPECT_EQ(0, static32.getIndexCountProperty());
    EXPECT_EQ(0, dynamic16.getIndexCountProperty());
    EXPECT_EQ(0, dynamic32.getIndexCountProperty());
}

TEST_F(IndexBufferEmptyDataTest, SharedThirtyTwoBitFactoryRejectsInsteadOfDelegatingToSixteenBits)
{
    auto& renderer = device.GetRenderer();
    try
    {
        // Qualify the call deliberately so this tests the shared default even though the active
        // SOFTWARE renderer has a valid 32-bit override of its own.
        renderer.IGraphicsRenderer::CreateIndexBuffer32(3);
        FAIL() << "the shared factory returned a disguised 16-bit buffer";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(
            "IGraphicsRenderer::CreateIndexBuffer32: 32-bit index buffers are not supported by this renderer",
            error.what());
    }
}

TEST_F(IndexBufferEmptyDataTest, ZeroCountAcceptsNullForStaticAndDynamicBuffers)
{
    RequireIndexBuffers();

    IndexBuffer static16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer static32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_NO_THROW(static16.SetData(static_cast<const std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(static32.SetData(static_cast<const std::uint32_t*>(nullptr), 0));
    EXPECT_NO_THROW(dynamic16.SetData(
        static_cast<const std::uint16_t*>(nullptr), 0, 0, SetDataOptions::Discard));
    EXPECT_NO_THROW(dynamic32.SetData(
        static_cast<const std::uint32_t*>(nullptr), 0, 0, SetDataOptions::NoOverwrite));
}

TEST_F(IndexBufferEmptyDataTest, ZeroCountAtStartAndSourceEndIsANoOpAfterARealUpload)
{
    RequireIndexBuffers();

    const std::array<std::uint16_t, 4> source16{0, 1, 2, 3};
    const std::array<std::uint32_t, 4> source32{10, 11, 12, 13};
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    buffer16.SetData(source16.data(), static_cast<int>(source16.size()));
    buffer32.SetData(source32.data(), static_cast<int>(source32.size()));

    ASSERT_EQ(4, buffer16.GetRenderer().GetIndexCount());
    ASSERT_EQ(4, buffer32.GetRenderer().GetIndexCount());

    EXPECT_NO_THROW(buffer16.SetData(source16.data(), 0));
    EXPECT_NO_THROW(buffer32.SetData(source32.data(), 0));
    EXPECT_NO_THROW(buffer16.SetData(
        source16.data(), static_cast<int>(source16.size()), 0));
    EXPECT_NO_THROW(buffer32.SetData(
        source32.data(), static_cast<int>(source32.size()), 0));
    EXPECT_NO_THROW(buffer16.SetData(static_cast<const std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(buffer32.SetData(static_cast<const std::uint32_t*>(nullptr), 0));

    EXPECT_EQ(4, buffer16.GetRenderer().GetIndexCount());
    EXPECT_EQ(4, buffer32.GetRenderer().GetIndexCount());

    std::array<std::uint16_t, 4> result16{};
    std::array<std::uint32_t, 4> result32{};
    buffer16.GetData(result16.data(), static_cast<int>(result16.size()));
    buffer32.GetData(result32.data(), static_cast<int>(result32.size()));
    EXPECT_EQ(source16, result16);
    EXPECT_EQ(source32, result32);
}

TEST_F(IndexBufferEmptyDataTest, NativePaddingDoesNotPermitNonzeroUploadIntoZeroCapacity)
{
    RequireIndexBuffers();

    const std::uint16_t source16 = 0;
    const std::uint32_t source32 = 0;
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_THROW(buffer16.SetData(&source16, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer32.SetData(&source32, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(dynamic16.SetData(&source16, 0, 1, SetDataOptions::Discard),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(dynamic32.SetData(&source32, 0, 1, SetDataOptions::NoOverwrite),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(0, buffer16.getIndexCountProperty());
    EXPECT_EQ(0, buffer32.getIndexCountProperty());
    EXPECT_EQ(0, dynamic16.getIndexCountProperty());
    EXPECT_EQ(0, dynamic32.getIndexCountProperty());
}

TEST_F(IndexBufferEmptyDataTest, NullIsLegalOnlyForEmptyRanges)
{
    RequireIndexBuffers();

    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);

    EXPECT_THROW(buffer16.SetData(static_cast<const std::uint16_t*>(nullptr), 1),
                 System::ArgumentNullException);
    EXPECT_THROW(buffer32.SetData(static_cast<const std::uint32_t*>(nullptr), 1),
                 System::ArgumentNullException);
    EXPECT_THROW(dynamic16.SetData(
                     static_cast<const std::uint16_t*>(nullptr), 0, 1, SetDataOptions::Discard),
                 System::ArgumentNullException);
    EXPECT_THROW(dynamic32.SetData(
                     static_cast<const std::uint32_t*>(nullptr), 0, 1,
                     SetDataOptions::NoOverwrite),
                 System::ArgumentNullException);
}

TEST_F(IndexBufferEmptyDataTest, EmptyRangeStillValidatesWidthAndNonnegativeArguments)
{
    RequireIndexBuffers();

    const std::uint16_t source16 = 0;
    const std::uint32_t source32 = 0;
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);

    EXPECT_THROW(buffer16.SetData(static_cast<const std::uint32_t*>(nullptr), 0),
                 System::ArgumentException);
    EXPECT_THROW(buffer32.SetData(static_cast<const std::uint16_t*>(nullptr), 0),
                 System::ArgumentException);
    EXPECT_THROW(dynamic16.SetData(
                     static_cast<const std::uint32_t*>(nullptr), 0, 0, SetDataOptions::Discard),
                 System::ArgumentException);
    EXPECT_THROW(buffer16.SetData(&source16, -1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer32.SetData(&source32, -1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer16.SetData(&source16, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer32.SetData(&source32, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((IndexBuffer(device, IndexElementSize::SixteenBits, -1, BufferUsage::None)),
                 System::ArgumentOutOfRangeException);
}

TEST_F(IndexBufferEmptyDataTest, EmptyGetDataAvoidsNullMemcpyButPreservesRangeChecks)
{
    RequireIndexBuffers();

    IndexBuffer empty16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer empty32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    EXPECT_NO_THROW(empty16.GetData(static_cast<std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(empty32.GetData(static_cast<std::uint32_t*>(nullptr), 0));

    const std::array<std::uint16_t, 4> source16{0, 1, 2, 3};
    const std::array<std::uint32_t, 4> source32{10, 11, 12, 13};
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    buffer16.SetData(source16.data(), static_cast<int>(source16.size()));
    buffer32.SetData(source32.data(), static_cast<int>(source32.size()));

    EXPECT_NO_THROW(buffer16.GetData(
        static_cast<std::uint16_t*>(nullptr), static_cast<int>(source16.size()), 0));
    EXPECT_NO_THROW(buffer32.GetData(
        static_cast<std::uint32_t*>(nullptr), static_cast<int>(source32.size()), 0));
    EXPECT_THROW(buffer16.GetData(static_cast<std::uint16_t*>(nullptr), 5, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer32.GetData(static_cast<std::uint32_t*>(nullptr), 5, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer16.GetData(static_cast<std::uint16_t*>(nullptr), 1),
                 System::ArgumentNullException);
    EXPECT_THROW(buffer32.GetData(static_cast<std::uint32_t*>(nullptr), 1),
                 System::ArgumentNullException);
}

TEST_F(IndexBufferEmptyDataTest, DisposedBuffersRejectEmptyOperationsBeforeNoOp)
{
    RequireIndexBuffers();

    IndexBuffer static16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    static16.Dispose();
    static16.Dispose();
    dynamic32.Dispose();
    dynamic32.Dispose();

    EXPECT_FALSE(static16.HasRenderer());
    EXPECT_FALSE(dynamic32.HasRenderer());
    EXPECT_THROW(static16.SetData(static_cast<const std::uint16_t*>(nullptr), 0),
                 System::ObjectDisposedException);
    EXPECT_THROW(dynamic32.SetData(
                     static_cast<const std::uint32_t*>(nullptr), 0, 0,
                     SetDataOptions::Discard),
                 System::ObjectDisposedException);
    EXPECT_THROW(static16.GetData(static_cast<std::uint16_t*>(nullptr), 0),
                 System::ObjectDisposedException);
}

TEST_F(IndexBufferEmptyDataTest, NonzeroUploadsRemainExactForBothWidthsAndBufferKinds)
{
    RequireIndexBuffers();

    const std::array<std::uint16_t, 6> source16{10, 11, 12, 13, 14, 15};
    const std::array<std::uint32_t, 6> source32{20, 21, 22, 23, 24, 25};
    const std::array<std::uint16_t, 4> expected16{11, 12, 13, 14};
    const std::array<std::uint32_t, 4> expected32{21, 22, 23, 24};
    IndexBuffer static16(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer static32(device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);

    static16.SetData(source16.data(), 1, 4);
    static32.SetData(source32.data(), 1, 4);
    dynamic16.SetData(source16.data(), 1, 4, SetDataOptions::Discard);
    dynamic32.SetData(source32.data(), 1, 4, SetDataOptions::NoOverwrite);

    std::array<std::uint16_t, 4> staticResult16{};
    std::array<std::uint32_t, 4> staticResult32{};
    std::array<std::uint16_t, 4> dynamicResult16{};
    std::array<std::uint32_t, 4> dynamicResult32{};
    static16.GetData(staticResult16.data(), 4);
    static32.GetData(staticResult32.data(), 4);
    dynamic16.GetData(dynamicResult16.data(), 4);
    dynamic32.GetData(dynamicResult32.data(), 4);
    EXPECT_EQ(expected16, staticResult16);
    EXPECT_EQ(expected32, staticResult32);
    EXPECT_EQ(expected16, dynamicResult16);
    EXPECT_EQ(expected32, dynamicResult32);

    // One 16-bit index is the smallest real write and exercises WebGPU's four-byte copy rule.
    static16.SetData(source16.data(), 1);
    static32.SetData(source32.data(), 1);
    std::uint16_t single16 = 0;
    std::uint32_t single32 = 0;
    static16.GetData(&single16, 1);
    static32.GetData(&single32, 1);
    EXPECT_EQ(source16.front(), single16);
    EXPECT_EQ(source32.front(), single32);
    EXPECT_EQ(1, static16.GetRenderer().GetIndexCount());
    EXPECT_EQ(1, static32.GetRenderer().GetIndexCount());
}

#ifdef CNA_TEST_WEBGPU_AVAILABLE
TEST_F(IndexBufferEmptyDataTest, WebGpuNativeErrorScopesStayClean)
{
    // plan_runtimerenderer.md RTR-P9-9: compiled whenever WebGPU is in the build, run only when it
    // is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    RequireIndexBuffers();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);

    // Nest the two error-filter classes accepted by the pinned wgpu-native runtime so validation
    // and allocation errors are test-fatal instead of merely reaching the uncaptured-error logger.
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    IndexBuffer empty16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer empty32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    EXPECT_NO_THROW(empty16.SetData(static_cast<const std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(empty32.SetData(static_cast<const std::uint32_t*>(nullptr), 0));

    const std::uint16_t one16 = 7;
    const std::uint32_t one32 = 9;
    IndexBuffer real16(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    IndexBuffer real32(device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
    EXPECT_NO_THROW(real16.SetData(&one16, 1));
    EXPECT_NO_THROW(real32.SetData(&one32, 1));

    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
}

TEST_F(IndexBufferEmptyDataTest, WebGpuDeferredThreeIndexDrawKeepsNativeScopeClean)
{
    RequireIndexBuffers();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    const std::array<VertexPositionColor, 3> vertices{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0), Color::Red),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0), Color::Lime),
        VertexPositionColor(Vector3(0, 0.5f, 0), Color::Blue),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.Present();

    PopAndExpectClean(*renderer);
}
#endif
