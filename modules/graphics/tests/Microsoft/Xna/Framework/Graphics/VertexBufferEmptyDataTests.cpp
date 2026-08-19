// SPDX-License-Identifier: MS-PL
// REMED-GFX-103: public regression for CNA's zero-element vertex-buffer contract.
//
// A logical zero-capacity VertexBuffer is valid.  Once the public arguments have been
// validated, an empty upload is a true no-op: it must not reach pointer arithmetic, packing,
// shadow copies, or a graphics renderer, and must not replace the most recent real upload.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare.
using namespace CNA::Testing::Renderers;

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

// plan_runtimerenderer.md RTR-P9-9: this file's EasyGL block needs that renderer's own headers, so
// it stays a COMPILE-time guard -- no runtime predicate can make a type exist. What changes is the
// condition: only the DEFAULT renderer's CNA_RENDERER_<X> is defined project-wide, so in a
// multi-renderer build this block compiled to nothing even when EasyGL was in the binary. The
// PRESENT_ defines say "compiled in", and EasyGL is a family of five public identities.
#if defined(CNA_RENDERER_EASYGL) || defined(CNA_RENDERER_PRESENT_OPENGLES2) || \
    defined(CNA_RENDERER_PRESENT_OPENGLES3) || defined(CNA_RENDERER_PRESENT_OPENGL33) || \
    defined(CNA_RENDERER_PRESENT_WEBGL1) || defined(CNA_RENDERER_PRESENT_WEBGL2)
#define CNA_TEST_EASYGL_AVAILABLE 1
#endif

// Same for WebGPU, which is a single identity.
#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)
#define CNA_TEST_WEBGPU_AVAILABLE 1
#endif

#ifdef CNA_TEST_EASYGL_AVAILABLE
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#endif

#ifdef CNA_TEST_WEBGPU_AVAILABLE
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
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
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

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

    VertexDeclaration OddStrideDeclaration()
    {
        return VertexDeclaration(
            13,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
    }

    VertexDeclaration PositionTextureDeclaration()
    {
        return VertexDeclaration(
            20,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Vector2,
                    VertexElementUsage::TextureCoordinate, 0),
            });
    }

    VertexDeclaration PositionColorTextureDeclaration()
    {
        return VertexDeclaration(
            24,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(
                    16, VertexElementFormat::Vector2,
                    VertexElementUsage::TextureCoordinate, 0),
            });
    }

    VertexDeclaration PositionNormalTextureDeclaration()
    {
        return VertexDeclaration(
            32,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(
                    24, VertexElementFormat::Vector2,
                    VertexElementUsage::TextureCoordinate, 0),
            });
    }

    VertexDeclaration SkinnedDeclaration()
    {
        return VertexDeclaration(
            52,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(
                    24, VertexElementFormat::Vector2,
                    VertexElementUsage::TextureCoordinate, 0),
                VertexElement(
                    32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(
                    48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
            });
    }

    class VertexBufferEmptyDataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from -- invoked from this ordinary
        // member function it printed "Skipped" but let the calling TEST_F body keep running into
        // a real VertexBuffer construction, which then threw on a renderer with no 3D pipeline.
        // The actual gate has to run in SetUp() (a location GoogleTest itself calls directly),
        // which is where GTEST_SKIP() genuinely prevents the test body from executing.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
        }

        void RequireVertexBuffers()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
        }
    };

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

    void PopAndExpectClean(
        CNA::Internal::Renderers::WebGPU::WebGPURenderer& renderer)
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

TEST_F(VertexBufferEmptyDataTest, ZeroCapacityConstructionPreservesLogicalCapacity)
{
    RequireVertexBuffers();

    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);

    EXPECT_EQ(0, staticBuffer.getVertexCountProperty());
    EXPECT_EQ(0, dynamicBuffer.getVertexCountProperty());
    EXPECT_EQ(0, staticBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(0, dynamicBuffer.GetRenderer().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, ZeroCountAcceptsNullForStaticAndDynamicBuffers)
{
    RequireVertexBuffers();

    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);

    EXPECT_NO_THROW(
        staticBuffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0));
    EXPECT_NO_THROW(staticBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 0, 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr),
        0,
        0,
        SetDataOptions::Discard));
}

TEST_F(VertexBufferEmptyDataTest, EmptyVectorAndSourceEndSlicesAreTrueNoOps)
{
    RequireVertexBuffers();

    std::vector<VertexPositionColor> emptyTyped;
    std::vector<std::uint8_t> emptyRaw;
    const std::array<VertexPositionColor, 2> source{
        VertexPositionColor(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7)),
        VertexPositionColor(Vector3(8.0f, 9.0f, 10.0f), Color(11, 12, 13, 14)),
    };
    VertexBuffer typedBuffer(device, PositionColorDeclaration(), 2, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 2, BufferUsage::None);
    VertexBuffer rawBuffer(device, OddStrideDeclaration(), 2, BufferUsage::None);

    EXPECT_NO_THROW(typedBuffer.SetData(emptyTyped.data(), 0));
    EXPECT_NO_THROW(typedBuffer.SetData(emptyTyped.data(), 0, 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        emptyTyped.data(), 0, 0, SetDataOptions::Discard));
    EXPECT_NO_THROW(rawBuffer.SetDataRaw(emptyRaw.data(), 0, 13));

    typedBuffer.SetData(source.data(), static_cast<int>(source.size()));
    dynamicBuffer.SetData(
        source.data(), 0, static_cast<int>(source.size()), SetDataOptions::Discard);
    ASSERT_EQ(2, typedBuffer.GetRenderer().GetVertexCount());
    ASSERT_EQ(2, dynamicBuffer.GetRenderer().GetVertexCount());

    // A zero-length slice whose source start is exactly one-past-the-source end is valid.
    EXPECT_NO_THROW(typedBuffer.SetData(
        source.data(), static_cast<int>(source.size()), 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        source.data(), static_cast<int>(source.size()), 0,
        SetDataOptions::NoOverwrite));
    EXPECT_NO_THROW(typedBuffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0));
    EXPECT_EQ(2, typedBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(2, dynamicBuffer.GetRenderer().GetVertexCount());

    std::array<VertexPositionColor, 2> staticResult{};
    std::array<VertexPositionColor, 2> dynamicResult{};
    typedBuffer.GetData(staticResult.data(), 2);
    dynamicBuffer.GetData(dynamicResult.data(), 2);
    EXPECT_EQ(source, staticResult);
    EXPECT_EQ(source, dynamicResult);
}

TEST_F(VertexBufferEmptyDataTest, EmptyUploadBetweenAAndBDoesNotClearEitherBufferKind)
{
    RequireVertexBuffers();

    const std::array<VertexPositionColor, 3> uploadA{
        VertexPositionColor(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7)),
        VertexPositionColor(Vector3(8.0f, 9.0f, 10.0f), Color(11, 12, 13, 14)),
        VertexPositionColor(Vector3(15.0f, 16.0f, 17.0f), Color(18, 19, 20, 21)),
    };
    const std::array<VertexPositionColor, 4> sourceB{
        VertexPositionColor(Vector3(-1.0f, -2.0f, -3.0f), Color(1, 2, 3, 4)),
        VertexPositionColor(Vector3(22.0f, 23.0f, 24.0f), Color(25, 26, 27, 28)),
        VertexPositionColor(Vector3(29.0f, 30.0f, 31.0f), Color(32, 33, 34, 35)),
        VertexPositionColor(Vector3(36.0f, 37.0f, 38.0f), Color(39, 40, 41, 42)),
    };
    const std::array<VertexPositionColor, 3> uploadB{
        sourceB[1], sourceB[2], sourceB[3],
    };
    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 3, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);

    staticBuffer.SetData(uploadA.data(), 3);
    dynamicBuffer.SetData(uploadA.data(), 0, 3, SetDataOptions::Discard);
    staticBuffer.SetData(uploadA.data(), 3, 0);
    dynamicBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 0, 0,
        SetDataOptions::NoOverwrite);

    std::array<VertexPositionColor, 3> result{};
    staticBuffer.GetData(result.data(), 3);
    EXPECT_EQ(uploadA, result);
    dynamicBuffer.GetData(result.data(), 3);
    EXPECT_EQ(uploadA, result);

    // startIndex is a source-array index. CNA's current overloads always replace from
    // implicit destination byte offset zero; GFX-025 owns adding destination-offset APIs.
    staticBuffer.SetData(sourceB.data(), 1, 3);
    dynamicBuffer.SetData(sourceB.data(), 1, 3, SetDataOptions::NoOverwrite);
    staticBuffer.GetData(result.data(), 3);
    EXPECT_EQ(uploadB, result);
    dynamicBuffer.GetData(result.data(), 3);
    EXPECT_EQ(uploadB, result);
    EXPECT_EQ(3, staticBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(3, dynamicBuffer.GetRenderer().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, NullIsLegalOnlyForEmptyUploads)
{
    RequireVertexBuffers();

    const VertexPositionColor vertex(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 1, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 1, BufferUsage::None);
    VertexBuffer rawBuffer(device, OddStrideDeclaration(), 1, BufferUsage::None);

    EXPECT_THROW(
        staticBuffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 1),
        System::ArgumentNullException);
    EXPECT_THROW(
        dynamicBuffer.SetData(
            static_cast<const VertexPositionColor*>(nullptr), 0, 1,
            SetDataOptions::NoOverwrite),
        System::ArgumentNullException);
    EXPECT_THROW(
        rawBuffer.SetDataRaw(nullptr, 1, 13),
        System::ArgumentNullException);

    EXPECT_NO_THROW(staticBuffer.SetData(&vertex, 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 1, 0,
        SetDataOptions::Discard));
    EXPECT_NO_THROW(rawBuffer.SetDataRaw(nullptr, 0, 13));
}

TEST_F(VertexBufferEmptyDataTest, LogicalEndAndBeyondCapacityRemainDistinct)
{
    RequireVertexBuffers();

    const VertexPositionColor vertex(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);
    VertexBuffer rawBuffer(device, OddStrideDeclaration(), 0, BufferUsage::None);

    // These overloads have an implicit destination byte offset of zero. For a zero-capacity
    // buffer that offset is exactly the logical end, where an empty range is valid.
    EXPECT_NO_THROW(staticBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 0, 0,
        SetDataOptions::Discard));
    EXPECT_NO_THROW(rawBuffer.SetDataRaw(nullptr, 0, 13));

    // A real range extends beyond that logical end even if a native renderer pads its allocation.
    EXPECT_THROW(staticBuffer.SetData(&vertex, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(dynamicBuffer.SetData(
                     &vertex, 0, 1, SetDataOptions::NoOverwrite),
                 System::ArgumentOutOfRangeException);
    const std::array<std::uint8_t, 13> raw{};
    EXPECT_THROW(rawBuffer.SetDataRaw(raw.data(), 1, 13),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(0, staticBuffer.getVertexCountProperty());
    EXPECT_EQ(0, dynamicBuffer.getVertexCountProperty());
    EXPECT_EQ(0, rawBuffer.getVertexCountProperty());
}

TEST_F(VertexBufferEmptyDataTest, EmptyRangePreservesPublicValidationOrdering)
{
    RequireVertexBuffers();

    const VertexPositionColor vertex(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    VertexBuffer buffer(device, PositionColorDeclaration(), 1, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 1, BufferUsage::None);

    // Source ranges and declarations are checked before the empty return.
    EXPECT_THROW(buffer.SetData(&vertex, -1, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer.SetData(&vertex, -1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(dynamicBuffer.SetData(
                     &vertex, -1, 0, SetDataOptions::Discard),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW((VertexBuffer(
                     device, PositionColorDeclaration(), -1, BufferUsage::None)),
                 System::ArgumentOutOfRangeException);

    VertexBuffer rawBuffer(device, OddStrideDeclaration(), 1, BufferUsage::None);
    EXPECT_THROW(rawBuffer.SetDataRaw(nullptr, 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(rawBuffer.SetDataRaw(nullptr, 0, 16),
                 System::ArgumentException);

    const VertexDeclaration incompatible(
        16,
        {
            VertexElement(
                12, VertexElementFormat::Vector2,
                VertexElementUsage::TextureCoordinate, 0),
        });
    VertexBuffer incompatibleBuffer(device, incompatible, 1, BufferUsage::None);
    EXPECT_THROW(incompatibleBuffer.SetData(
                     static_cast<const VertexPositionColor*>(nullptr), 0),
                 System::ArgumentException);
}

TEST_F(VertexBufferEmptyDataTest, DisposedBuffersRejectEmptyUploadsBeforeNoOp)
{
    RequireVertexBuffers();

    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);
    staticBuffer.Dispose();
    staticBuffer.Dispose();
    dynamicBuffer.Dispose();
    dynamicBuffer.Dispose();

    EXPECT_FALSE(staticBuffer.HasRenderer());
    EXPECT_FALSE(dynamicBuffer.HasRenderer());
    EXPECT_THROW(staticBuffer.SetData(
                     static_cast<const VertexPositionColor*>(nullptr), -1, 0),
                 System::ObjectDisposedException);
    EXPECT_THROW(dynamicBuffer.SetData(
                     static_cast<const VertexPositionColor*>(nullptr), -1, 0,
                     SetDataOptions::Discard),
                 System::ObjectDisposedException);
}

TEST_F(VertexBufferEmptyDataTest, NonemptyTypedUploadsCoverCompactLayoutsAndSourceSlices)
{
    RequireVertexBuffers();

    const std::array<VertexPositionTexture, 3> vpt{
        VertexPositionTexture(Vector3(-1, -1, 0), Vector2(0, 0)),
        VertexPositionTexture(Vector3(1, -1, 0), Vector2(1, 0)),
        VertexPositionTexture(Vector3(0, 1, 0), Vector2(0.5f, 1)),
    };
    const std::array<VertexPositionColorTexture, 3> vpct{
        VertexPositionColorTexture(
            Vector3(-1, -1, 0), Color(255, 0, 0, 255), Vector2(0, 0)),
        VertexPositionColorTexture(
            Vector3(1, -1, 0), Color(0, 255, 0, 255), Vector2(1, 0)),
        VertexPositionColorTexture(
            Vector3(0, 1, 0), Color(0, 0, 255, 255), Vector2(0.5f, 1)),
    };
    const std::array<VertexPositionNormalTexture, 3> vpnt{
        VertexPositionNormalTexture(
            Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector2(0, 0)),
        VertexPositionNormalTexture(
            Vector3(1, -1, 0), Vector3(0, 0, 1), Vector2(1, 0)),
        VertexPositionNormalTexture(
            Vector3(0, 1, 0), Vector3(0, 0, 1), Vector2(0.5f, 1)),
    };
    const std::array<VertexPositionNormalTextureSkinned, 2> skinned{
        VertexPositionNormalTextureSkinned(
            Vector3(1, 2, 3), Vector3(0, 0, 1), Vector2(0, 0),
            Vector4(1, 0, 0, 0), std::array<std::uint8_t, 4>{1, 2, 3, 4}),
        VertexPositionNormalTextureSkinned(
            Vector3(4, 5, 6), Vector3(0, 1, 0), Vector2(1, 1),
            Vector4(0.25f, 0.25f, 0.25f, 0.25f),
            std::array<std::uint8_t, 4>{5, 6, 7, 8}),
    };

    VertexBuffer vptBuffer(device, PositionTextureDeclaration(), 2, BufferUsage::None);
    VertexBuffer vpctBuffer(
        device, PositionColorTextureDeclaration(), 2, BufferUsage::None);
    VertexBuffer vpntBuffer(
        device, PositionNormalTextureDeclaration(), 2, BufferUsage::None);
    VertexBuffer skinnedBuffer(device, SkinnedDeclaration(), 1, BufferUsage::None);
    vptBuffer.SetData(vpt.data(), 1, 2);
    vpctBuffer.SetData(vpct.data(), 1, 2);
    vpntBuffer.SetData(vpnt.data(), 1, 2);
    skinnedBuffer.SetData(skinned.data(), 1, 1);

    std::array<VertexPositionTexture, 2> vptResult{};
    std::array<VertexPositionColorTexture, 2> vpctResult{vpct[0], vpct[0]};
    std::array<VertexPositionNormalTexture, 2> vpntResult{};
    VertexPositionNormalTextureSkinned skinnedResult;
    vptBuffer.GetData(vptResult.data(), 2);
    vpctBuffer.GetData(vpctResult.data(), 2);
    vpntBuffer.GetData(vpntResult.data(), 2);
    skinnedBuffer.GetData(&skinnedResult, 1);
    EXPECT_EQ((std::array<VertexPositionTexture, 2>{vpt[1], vpt[2]}), vptResult);
    EXPECT_EQ(
        (std::array<VertexPositionColorTexture, 2>{vpct[1], vpct[2]}), vpctResult);
    EXPECT_EQ(
        (std::array<VertexPositionNormalTexture, 2>{vpnt[1], vpnt[2]}), vpntResult);
    EXPECT_EQ(skinned[1], skinnedResult);
    EXPECT_EQ(2, vptBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(2, vpctBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(2, vpntBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(1, skinnedBuffer.GetRenderer().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, DynamicPartialUploadKeepsExactCompactShadow)
{
    RequireVertexBuffers();

    const std::array<VertexPositionColorTexture, 3> source{
        VertexPositionColorTexture(
            Vector3(-1, -1, 0), Color(255, 0, 0, 255), Vector2(0, 0)),
        VertexPositionColorTexture(
            Vector3(1, -1, 0), Color(0, 255, 0, 255), Vector2(1, 0)),
        VertexPositionColorTexture(
            Vector3(0, 1, 0), Color(0, 0, 255, 255), Vector2(0.5f, 1)),
    };
    DynamicVertexBuffer buffer(
        device, PositionColorTextureDeclaration(), 2, BufferUsage::None);
    buffer.SetData(source.data(), 1, 2, SetDataOptions::NoOverwrite);

    std::array<VertexPositionColorTexture, 2> result{source[0], source[0]};
    buffer.GetData(result.data(), 2);
    EXPECT_EQ(
        (std::array<VertexPositionColorTexture, 2>{source[1], source[2]}), result);
    EXPECT_EQ(2, buffer.GetRenderer().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, RawDeclarationsAcceptOddAndAlignedStrides)
{
    RequireVertexBuffers();

    const std::array<std::uint8_t, 13> odd{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const std::array<std::uint8_t, 16> aligned{
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    VertexBuffer oddBuffer(device, OddStrideDeclaration(), 1, BufferUsage::None);
    VertexBuffer alignedBuffer(
        device, PositionColorDeclaration(), 1, BufferUsage::None);
    EXPECT_NO_THROW(oddBuffer.SetDataRaw(odd.data(), 1, 13));
    EXPECT_NO_THROW(alignedBuffer.SetDataRaw(aligned.data(), 1, 16));
    EXPECT_EQ(1, oddBuffer.GetRenderer().GetVertexCount());
    EXPECT_EQ(1, alignedBuffer.GetRenderer().GetVertexCount());

#ifdef CNA_TEST_EASYGL_AVAILABLE
    // Compiled whenever EasyGL is in the build; asserted only when it is the ACTIVE renderer --
    // otherwise the dynamic_cast below is a null check against a different renderer's object.
    if (CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2))
    {
    auto* oddEasy =
        dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLVertexBufferRenderer*>(
            &oddBuffer.GetRenderer());
    auto* alignedEasy =
        dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLVertexBufferRenderer*>(
            &alignedBuffer.GetRenderer());
    ASSERT_NE(nullptr, oddEasy);
    ASSERT_NE(nullptr, alignedEasy);
    EXPECT_EQ(OddStrideDeclaration().GetVertexElements(),
              oddEasy->GetDeclarationElements());
    EXPECT_EQ(PositionColorDeclaration().GetVertexElements(),
              alignedEasy->GetDeclarationElements());
    }
#endif
}

TEST_F(VertexBufferEmptyDataTest, UploadedVerticesSupportNormalAndIndexedDrawing)
{
    RequireVertexBuffers();

    const std::array<VertexPositionColor, 3> vertices{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0), Color(255, 0, 0, 255)),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0), Color(0, 255, 0, 255)),
        VertexPositionColor(Vector3(0, 0.5f, 0), Color(0, 0, 255, 255)),
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    EXPECT_NO_THROW(device.DrawPrimitives(
        PrimitiveType::TriangleList, 0, 1));
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1));
    EXPECT_NO_THROW(device.Present());
}

#ifdef CNA_TEST_WEBGPU_AVAILABLE
TEST_F(VertexBufferEmptyDataTest, WebGpuNativeScopesCoverEmptyOddAndAlignedUploads)
{
    // plan_runtimerenderer.md RTR-P9-9: compiled whenever WebGPU is in the build, run only when it
    // is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    RequireVertexBuffers();

    auto* graphicsRenderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, graphicsRenderer);
    wgpuDevicePushErrorScope(graphicsRenderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(graphicsRenderer->Device(), WGPUErrorFilter_Validation);

    VertexBuffer emptyBuffer(
        device, OddStrideDeclaration(), 0, BufferUsage::None);
    auto* emptyRenderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUVertexBufferRenderer*>(
            &emptyBuffer.GetRenderer());
    ASSERT_NE(nullptr, emptyRenderer);
    ASSERT_EQ(nullptr, emptyRenderer->Buffer());
    ASSERT_TRUE(emptyRenderer->ShadowData().empty());
    emptyBuffer.SetDataRaw(nullptr, 0, 13);
    EXPECT_EQ(nullptr, emptyRenderer->Buffer());
    EXPECT_TRUE(emptyRenderer->ShadowData().empty());
    EXPECT_EQ(0, emptyRenderer->GetVertexCount());
    EXPECT_EQ(0u, emptyRenderer->Stride());

    const std::array<std::uint8_t, 13> odd{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    VertexBuffer oddBuffer(device, OddStrideDeclaration(), 1, BufferUsage::None);
    oddBuffer.SetDataRaw(odd.data(), 1, 13);
    auto* oddRenderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUVertexBufferRenderer*>(
            &oddBuffer.GetRenderer());
    ASSERT_NE(nullptr, oddRenderer);
    ASSERT_NE(nullptr, oddRenderer->Buffer());
    EXPECT_EQ(1, oddRenderer->GetVertexCount());
    EXPECT_EQ(13u, oddRenderer->Stride());
    EXPECT_EQ(std::vector<std::uint8_t>(odd.begin(), odd.end()),
              oddRenderer->ShadowData());
    const auto oddShadow = oddRenderer->ShadowData();
    oddBuffer.SetDataRaw(nullptr, 0, 13);
    EXPECT_EQ(oddShadow, oddRenderer->ShadowData());
    EXPECT_EQ(1, oddRenderer->GetVertexCount());

    const std::array<std::uint8_t, 16> aligned{};
    VertexBuffer alignedBuffer(
        device, PositionColorDeclaration(), 1, BufferUsage::None);
    alignedBuffer.SetDataRaw(aligned.data(), 1, 16);
    auto* alignedRenderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUVertexBufferRenderer*>(
            &alignedBuffer.GetRenderer());
    ASSERT_NE(nullptr, alignedRenderer);
    ASSERT_NE(nullptr, alignedRenderer->Buffer());
    EXPECT_EQ(1, alignedRenderer->GetVertexCount());
    EXPECT_EQ(16u, alignedRenderer->Stride());
    EXPECT_EQ(16u, alignedRenderer->ShadowData().size());

    const VertexPositionColor typed(
        Vector3(1, 2, 3), Color(4, 5, 6, 7));
    VertexBuffer typedBuffer(
        device, PositionColorDeclaration(), 1, BufferUsage::None);
    typedBuffer.SetData(&typed, 1);
    auto* typedRenderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUVertexBufferRenderer*>(
            &typedBuffer.GetRenderer());
    ASSERT_NE(nullptr, typedRenderer);
    EXPECT_EQ(1, typedRenderer->GetVertexCount());
    EXPECT_EQ(16u, typedRenderer->Stride());
    EXPECT_EQ(16u, typedRenderer->ShadowData().size());

    PopAndExpectClean(*graphicsRenderer);
    PopAndExpectClean(*graphicsRenderer);
}
#endif

// ---------------------------------------------------------------------------------------------
// CBIND-059: the windowed raw upload and the raw readback.
//
// Every other transfer route replaces the whole buffer, and typed readback names one of the
// built-in layouts. Between them, a buffer written with a custom layout could be filled and never
// read, and one slice of a large dynamic buffer could not be rewritten without rewriting all of it.
// ---------------------------------------------------------------------------------------------

TEST_F(VertexBufferEmptyDataTest, RawReadbackAnswersExactlyWhatTheRawUploadWrote)
{
    RequireVertexBuffers();

    const std::array<std::uint8_t, 26> source{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    VertexBuffer buffer(device, OddStrideDeclaration(), 2, BufferUsage::None);
    buffer.SetDataRaw(source.data(), 2, 13);

    std::array<std::uint8_t, 26> readBack{};
    EXPECT_NO_THROW(buffer.GetDataRawEXT(0, readBack.data(), 2, 13));
    EXPECT_EQ(source, readBack);

    // A window of the readback, from a buffer-side offset.
    std::array<std::uint8_t, 13> second{};
    EXPECT_NO_THROW(buffer.GetDataRawEXT(13, second.data(), 1, 13));
    EXPECT_TRUE(std::equal(second.begin(), second.end(), source.begin() + 13));

    EXPECT_THROW(buffer.GetDataRawEXT(0, readBack.data(), 3, 13),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer.GetDataRawEXT(0, readBack.data(), 1, 0), System::ArgumentException);
    EXPECT_THROW(buffer.GetDataRawEXT(-1, readBack.data(), 1, 13),
                 System::ArgumentOutOfRangeException);
    EXPECT_NO_THROW(buffer.GetDataRawEXT(0, nullptr, 0, 13));
}

TEST_F(VertexBufferEmptyDataTest, WindowedRawUploadLeavesTheRestOfTheBufferAlone)
{
    RequireVertexBuffers();

    const std::array<std::uint8_t, 26> source{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    const std::array<std::uint8_t, 13> replacement{
        90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102};
    VertexBuffer buffer(device, OddStrideDeclaration(), 2, BufferUsage::None);
    buffer.SetDataRaw(source.data(), 2, 13);

    EXPECT_NO_THROW(buffer.SetDataRawAtEXT(13, replacement.data(), 1, 13));

    std::array<std::uint8_t, 26> readBack{};
    ASSERT_NO_THROW(buffer.GetDataRawEXT(0, readBack.data(), 2, 13));
    EXPECT_TRUE(std::equal(source.begin(), source.begin() + 13, readBack.begin()));
    EXPECT_TRUE(std::equal(replacement.begin(), replacement.end(), readBack.begin() + 13));
    // The whole buffer is still what the renderer holds, not just the window.
    EXPECT_EQ(2, buffer.GetRenderer().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, WindowedRawUploadIntoNeverWrittenBytesReadsZeroElsewhere)
{
    RequireVertexBuffers();

    // Nothing has been uploaded at all, so the shadow starts empty: the window still lands where
    // it was asked to, and everything it did not name reads as zero rather than as absent.
    const std::array<std::uint8_t, 13> window{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    VertexBuffer buffer(device, OddStrideDeclaration(), 2, BufferUsage::None);

    EXPECT_NO_THROW(buffer.SetDataRawAtEXT(13, window.data(), 1, 13));

    std::array<std::uint8_t, 26> readBack{};
    std::fill(readBack.begin(), readBack.end(), std::uint8_t{0xEE});
    ASSERT_NO_THROW(buffer.GetDataRawEXT(0, readBack.data(), 2, 13));
    EXPECT_TRUE(std::all_of(readBack.begin(), readBack.begin() + 13,
                            [](std::uint8_t value) { return value == 0; }));
    EXPECT_TRUE(std::equal(window.begin(), window.end(), readBack.begin() + 13));
}

TEST_F(VertexBufferEmptyDataTest, WindowedRawUploadRejectsAnUnusableWindow)
{
    RequireVertexBuffers();

    const std::array<std::uint8_t, 13> window{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    VertexBuffer buffer(device, OddStrideDeclaration(), 2, BufferUsage::None);

    EXPECT_THROW(buffer.SetDataRawAtEXT(26, window.data(), 1, 13),
                 System::ArgumentOutOfRangeException);
    // Not on a vertex boundary.
    EXPECT_THROW(buffer.SetDataRawAtEXT(5, window.data(), 1, 13), System::ArgumentException);
    // Not this buffer's declared stride.
    EXPECT_THROW(buffer.SetDataRawAtEXT(0, window.data(), 1, 16), System::ArgumentException);
    EXPECT_THROW(buffer.SetDataRawAtEXT(-1, window.data(), 1, 13),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer.SetDataRawAtEXT(0, nullptr, 1, 13), System::ArgumentNullException);
    // Zero vertices is a no-op wherever a zero-length transfer is.
    EXPECT_NO_THROW(buffer.SetDataRawAtEXT(0, nullptr, 0, 13));
}

TEST_F(VertexBufferEmptyDataTest, WindowedIndexUploadLeavesTheRestOfTheBufferAlone)
{
    RequireVertexBuffers();

    const std::array<std::uint16_t, 3> source{11, 22, 33};
    const std::array<std::uint16_t, 1> replacement{77};
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    buffer.SetData(source.data(), 3);

    EXPECT_NO_THROW(buffer.SetDataAtEXT(2, replacement.data(), 0, 1));

    std::array<std::uint16_t, 3> readBack{};
    ASSERT_NO_THROW(buffer.GetData(readBack.data(), 0, 3));
    EXPECT_EQ(source[0], readBack[0]);
    EXPECT_EQ(replacement[0], readBack[1]);
    EXPECT_EQ(source[2], readBack[2]);
}

TEST_F(VertexBufferEmptyDataTest, WindowedIndexUploadRejectsAnUnusableWindow)
{
    RequireVertexBuffers();

    const std::array<std::uint16_t, 1> window{77};
    const std::array<std::uint32_t, 1> wideWindow{77};
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);

    EXPECT_THROW(buffer.SetDataAtEXT(6, window.data(), 0, 1),
                 System::ArgumentOutOfRangeException);
    // Not on an index boundary.
    EXPECT_THROW(buffer.SetDataAtEXT(1, window.data(), 0, 1), System::ArgumentException);
    // Not this buffer's element width.
    EXPECT_THROW(buffer.SetDataAtEXT(0, wideWindow.data(), 0, 1), System::ArgumentException);
    EXPECT_THROW(buffer.SetDataAtEXT(-1, window.data(), 0, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(buffer.SetDataAtEXT(0, static_cast<const std::uint16_t*>(nullptr), 0, 1),
                 System::ArgumentNullException);
    EXPECT_NO_THROW(buffer.SetDataAtEXT(0, static_cast<const std::uint16_t*>(nullptr), 0, 0));
}
