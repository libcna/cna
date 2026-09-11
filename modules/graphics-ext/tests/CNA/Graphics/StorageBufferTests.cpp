// SPDX-License-Identifier: MS-PL

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ConstantBuffer.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using CNA::Graphics::ComputeShader;
using CNA::Graphics::ConstantBufferT;
using CNA::Graphics::ShaderBindingRequirementEXT;
using CNA::Graphics::ShaderBindingTypeEXT;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferCpuAccess;
using CNA::Graphics::StorageBufferDescriptor;
using CNA::Graphics::StorageBufferT;
using CNA::Graphics::StorageBufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;

namespace CNA::Internal
{
    class StorageBufferGraphicsDeviceTestPeer
    {
    public:
        static void ReplaceRenderer(
            GraphicsDevice& device,
            std::unique_ptr<CNA::Internal::Renderers::IGraphicsRenderer> renderer)
        {
            device.renderer_ = std::move(renderer);
            device.InvalidateRendererCapabilityProfileEXT();
        }

        static void InvalidateCapabilityProfile(GraphicsDevice& device)
        {
            device.InvalidateRendererCapabilityProfileEXT();
        }
    };
}

namespace
{
    struct StorageBufferTestState
    {
        int factoryCalls = 0;
        int uploadCalls = 0;
        int readbackCalls = 0;
        int copyCalls = 0;
        int bindCalls = 0;
        int constantBindCalls = 0;
        std::size_t createdBytes = 0;
        std::uint32_t createdUsage = 0;
        std::uint32_t createdCpuAccess = 0;
        std::size_t lastOffset = 0;
        std::size_t lastBytes = 0;
        bool acceptFactory = true;
        bool acceptTransfer = true;
        bool acceptConstantBinding = true;
        std::vector<std::uint8_t> lastUpload;
    };

    class RecordingStorageBufferRenderer final
        : public CNA::Internal::Renderers::IStorageBufferRenderer
    {
    public:
        RecordingStorageBufferRenderer(
            const std::size_t byteSize, const std::uint32_t usage,
            const std::uint32_t cpuAccess, int& destructions,
            std::shared_ptr<StorageBufferTestState> state)
            : bytes_(byteSize)
            , usage_(usage)
            , cpuAccess_(cpuAccess)
            , destructions_(&destructions)
            , state_(std::move(state))
        {
        }

        ~RecordingStorageBufferRenderer() override { ++*destructions_; }

        void SetData(const void* data, const std::size_t byteSize) override
        {
            if (!SetDataRangeEXT(0, data, byteSize))
                throw std::runtime_error("recording upload refused");
        }

        void GetData(void* out, const std::size_t byteSize) const override
        {
            if (!GetDataRangeEXT(0, out, byteSize))
                throw std::runtime_error("recording readback refused");
        }

        bool SetDataRangeEXT(
            const std::size_t byteOffset, const void* data,
            const std::size_t byteSize) override
        {
            ++state_->uploadCalls;
            state_->lastOffset = byteOffset;
            state_->lastBytes = byteSize;
            if (!state_->acceptTransfer) return false;
            state_->lastUpload.assign(byteSize, UINT8_C(0));
            if (byteSize != 0) {
                std::memcpy(bytes_.data() + byteOffset, data, byteSize);
                std::memcpy(state_->lastUpload.data(), data, byteSize);
            }
            return true;
        }

        bool GetDataRangeEXT(
            const std::size_t byteOffset, void* out,
            const std::size_t byteSize) const override
        {
            ++state_->readbackCalls;
            state_->lastOffset = byteOffset;
            state_->lastBytes = byteSize;
            if (!state_->acceptTransfer) return false;
            if (byteSize != 0)
                std::memcpy(out, bytes_.data() + byteOffset, byteSize);
            return true;
        }

        bool CopyToEXT(
            CNA::Internal::Renderers::IStorageBufferRenderer& destination,
            const std::size_t sourceByteOffset,
            const std::size_t destinationByteOffset,
            const std::size_t byteSize) override
        {
            ++state_->copyCalls;
            auto* target = dynamic_cast<RecordingStorageBufferRenderer*>(&destination);
            if (!state_->acceptTransfer || target == nullptr) return false;
            if (byteSize != 0)
                std::memcpy(target->bytes_.data() + destinationByteOffset,
                            bytes_.data() + sourceByteOffset, byteSize);
            return true;
        }

        [[nodiscard]] std::size_t GetByteSize() const override { return bytes_.size(); }
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }

    private:
        std::vector<std::uint8_t> bytes_;
        std::uint32_t usage_;
        std::uint32_t cpuAccess_;
        int* destructions_;
        std::shared_ptr<StorageBufferTestState> state_;
    };

    class RecordingComputeShaderRenderer final
        : public CNA::Internal::Renderers::IComputeShaderRenderer
    {
    public:
        explicit RecordingComputeShaderRenderer(
            std::shared_ptr<StorageBufferTestState> state)
            : state_(std::move(state))
        {
        }

        bool CompileProgram(const std::string&) override { return true; }
        void Bind() override {}
        void BindStorageBuffer(
            int, CNA::Internal::Renderers::IStorageBufferRenderer*) override
        {
            ++state_->bindCalls;
        }
        [[nodiscard]] bool BindConstantBufferEXT(
            int, CNA::Internal::Renderers::IStorageBufferRenderer*) override
        {
            ++state_->constantBindCalls;
            return state_->acceptConstantBinding;
        }
        [[nodiscard]] bool IsValid() const override { return true; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }

    private:
        std::shared_ptr<StorageBufferTestState> state_;
    };

    class StorageBufferContractRenderer final
        : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        explicit StorageBufferContractRenderer(int& destructions)
            : destructions_(&destructions)
            , state(std::make_shared<StorageBufferTestState>())
        {
        }

        void Clear(float, float, float, float) override {}
        void Present() override {}
        void GetViewportSize(int& width, int& height) override { width = height = 16; }
        void SetVirtualResolution(int, int) override {}
        void SetPresentationMode(int) override {}
        std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> CreateTexture(
            const CNA::Internal::Graphics::ImageData&) override
        {
            return nullptr;
        }
        std::unique_ptr<CNA::Internal::Renderers::ISpriteBatchRenderer> CreateSpriteBatch() override
        {
            return nullptr;
        }
        void SetRenderTargets(
            const CNA::Internal::Renderers::RenderTargetBindingDescriptor*, int) override {}
        void ClearColorAndDepth(float, float, float, float, float) override {}
        void ClearDepth(float) override {}
        void ClearStencil(int) override {}
        void ClearDepthAndStencil(float, int) override {}
        void ClearColorAndStencil(float, float, float, float, int) override {}
        void ClearColorDepthAndStencil(float, float, float, float, float, int) override {}
        void SetDepthTestEnabled(bool) override {}
        void SetBlendEnabled(bool) override {}
        void SetDepthWriteEnabled(bool) override {}
        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer> CreateVertexBuffer(
            int) override
        {
            return nullptr;
        }
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBuffer16(
            int) override
        {
            return nullptr;
        }
        void DrawColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
        void DrawIndexedColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const CNA::Internal::Renderers::IIndexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}

        [[nodiscard]] bool SupportsComputeShadersEXT() const override { return supportsCompute; }
        [[nodiscard]] bool SupportsIndirectDrawEXT() const override { return supportsIndirect; }
        [[nodiscard]] bool SupportsShaderLanguageEXT(
            const int language, const int stage) const override
        {
            return language == static_cast<int>(CNA::ShaderLanguageEXT::GlslEs) &&
                   stage == static_cast<int>(CNA::ShaderStageEXT::Compute);
        }
        [[nodiscard]] std::uint64_t GetMaxStorageBufferBytesEXT() const override
        {
            return maximumBytes;
        }
        [[nodiscard]] std::uint64_t GetMaxUniformBufferBytesEXT() const override
        {
            return maximumUniformBytes;
        }
        [[nodiscard]] std::uint64_t GetMinUniformBufferOffsetAlignmentEXT() const override
        {
            return uniformAlignment;
        }
        std::unique_ptr<CNA::Internal::Renderers::IStorageBufferRenderer>
        CreateStorageBuffer(const std::size_t byteSize) override
        {
            return Create(byteSize, UINT32_C(0x0F), UINT32_C(0x03));
        }
        std::unique_ptr<CNA::Internal::Renderers::IStorageBufferRenderer>
        CreateStorageBufferEXT(
            const std::size_t byteSize, const std::uint32_t usage,
            const std::uint32_t cpuAccess) override
        {
            return Create(byteSize, usage, cpuAccess);
        }
        std::unique_ptr<CNA::Internal::Renderers::IComputeShaderRenderer> CreateComputeShader(
            const std::string&) override
        {
            return std::make_unique<RecordingComputeShaderRenderer>(state);
        }
        void DispatchCompute(
            CNA::Internal::Renderers::IComputeShaderRenderer*, int, int, int) override {}

        std::uint64_t maximumBytes = 1024;
        std::uint64_t maximumUniformBytes = 1024;
        std::uint64_t uniformAlignment = 16;
        bool supportsCompute = true;
        bool supportsIndirect = true;
        std::shared_ptr<StorageBufferTestState> state;

    private:
        std::unique_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> Create(
            const std::size_t byteSize, const std::uint32_t usage,
            const std::uint32_t cpuAccess)
        {
            ++state->factoryCalls;
            state->createdBytes = byteSize;
            state->createdUsage = usage;
            state->createdCpuAccess = cpuAccess;
            if (!state->acceptFactory) return nullptr;
            return std::make_unique<RecordingStorageBufferRenderer>(
                byteSize, usage, cpuAccess, *destructions_, state);
        }

        int* destructions_;
    };

    class DefaultConstantBindingComputeRenderer final
        : public CNA::Internal::Renderers::IComputeShaderRenderer
    {
    public:
        bool CompileProgram(const std::string&) override { return true; }
        void Bind() override {}
        [[nodiscard]] bool IsValid() const override { return true; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
    };

    [[nodiscard]] std::unique_ptr<StorageBufferContractRenderer> MakeRenderer(
        int& destructions)
    {
        return std::make_unique<StorageBufferContractRenderer>(destructions);
    }

    constexpr StorageBufferUsage FullUsage =
        StorageBufferUsage::Storage |
        StorageBufferUsage::TransferSource |
        StorageBufferUsage::TransferDestination |
        StorageBufferUsage::IndirectArguments |
        StorageBufferUsage::Vertex |
        StorageBufferUsage::Index |
        StorageBufferUsage::Constant;

    constexpr StorageBufferCpuAccess FullCpuAccess =
        StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write;
}

static_assert(std::is_base_of_v<
              Microsoft::Xna::Framework::Graphics::GraphicsResource, StorageBuffer>);
static_assert(!std::is_copy_constructible_v<StorageBuffer>);
static_assert(!std::is_copy_assignable_v<StorageBuffer>);
static_assert(!std::is_move_constructible_v<StorageBuffer>);
static_assert(!std::is_move_assignable_v<StorageBuffer>);

namespace
{
    struct ConstantValue
    {
        float values[4];
    };

    struct NonStandardConstantBase
    {
        std::uint32_t first;
    };

    struct NonStandardConstant : NonStandardConstantBase
    {
        std::uint32_t second;
    };

    struct NonTrivialConstant
    {
        ~NonTrivialConstant() {}
        std::uint32_t value;
    };

    template<typename T>
    concept ConstantBufferValueAccepted = requires { typename ConstantBufferT<T>; };

    static_assert(ConstantBufferValueAccepted<ConstantValue>);
    static_assert(!ConstantBufferValueAccepted<NonStandardConstant>);
    static_assert(!ConstantBufferValueAccepted<NonTrivialConstant>);
}

TEST(StorageBufferDescriptorTest, RetainsEveryFieldAndComposesEveryFlag)
{
    const StorageBufferDescriptor descriptor(257, FullUsage, FullCpuAccess);
    EXPECT_EQ(descriptor.getByteSize(), 257U);
    EXPECT_EQ(descriptor.getUsage(), FullUsage);
    EXPECT_EQ(descriptor.getCpuAccess(), FullCpuAccess);
    EXPECT_EQ(FullUsage & StorageBufferUsage::Storage, StorageBufferUsage::Storage);
    EXPECT_EQ(FullUsage & StorageBufferUsage::TransferSource,
              StorageBufferUsage::TransferSource);
    EXPECT_EQ(FullUsage & StorageBufferUsage::TransferDestination,
              StorageBufferUsage::TransferDestination);
    EXPECT_EQ(FullUsage & StorageBufferUsage::IndirectArguments,
              StorageBufferUsage::IndirectArguments);
    EXPECT_EQ(FullUsage & StorageBufferUsage::Vertex, StorageBufferUsage::Vertex);
    EXPECT_EQ(FullUsage & StorageBufferUsage::Index, StorageBufferUsage::Index);
    EXPECT_EQ(FullUsage & StorageBufferUsage::Constant,
              StorageBufferUsage::Constant);
    EXPECT_EQ(FullCpuAccess & StorageBufferCpuAccess::Read,
              StorageBufferCpuAccess::Read);
    EXPECT_EQ(FullCpuAccess & StorageBufferCpuAccess::Write,
              StorageBufferCpuAccess::Write);
}

TEST(BaseInstanceDrawTest, AnUnsupportedRendererRefusesBeforeInspectingDrawBindings)
{
    GraphicsDevice device;
    int destructions = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, MakeRenderer(destructions));

    EXPECT_FALSE(device.SupportsRendererFeatureEXT(CNA::RendererFeature::BaseInstanceDrawing));
    EXPECT_THROW(
        device.DrawInstancedPrimitivesBaseInstanceEXT(
            Microsoft::Xna::Framework::Graphics::PrimitiveType::TriangleList,
            0, 0, 3, 0, 1, 1, 1),
        System::NotSupportedException);
}

TEST(StorageBufferDescriptorTest, RejectsEveryIntrinsicInvalidDescription)
{
    EXPECT_THROW(StorageBufferDescriptor(
                     0, StorageBufferUsage::Storage, StorageBufferCpuAccess::None),
                 std::invalid_argument);
    EXPECT_THROW(StorageBufferDescriptor(
                     4, StorageBufferUsage::None, StorageBufferCpuAccess::None),
                 std::invalid_argument);
    EXPECT_THROW(StorageBufferDescriptor(
                     4, static_cast<StorageBufferUsage>(UINT32_C(1) << 31),
                     StorageBufferCpuAccess::None),
                 std::invalid_argument);
    EXPECT_THROW(StorageBufferDescriptor(
                     4, StorageBufferUsage::Storage,
                     static_cast<StorageBufferCpuAccess>(UINT32_C(1) << 31)),
                 std::invalid_argument);
}

TEST(StorageBufferTest, DescriptorConstructionValidatesLimitAndForwardsExactIntent)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    const StorageBufferDescriptor descriptor(257, FullUsage, FullCpuAccess);
    const std::size_t baseline = device.GetTrackedResourceCount();
    StorageBuffer buffer(device, descriptor);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    EXPECT_EQ(buffer.getDescriptor().getByteSize(), 257U);
    EXPECT_EQ(buffer.getByteSize(), 257U);
    EXPECT_NE(buffer.getRendererEXT(), nullptr);
    EXPECT_EQ(view->state->factoryCalls, 1);
    EXPECT_EQ(view->state->createdBytes, 257U);
    EXPECT_EQ(view->state->createdUsage, static_cast<std::uint32_t>(FullUsage));
    EXPECT_EQ(view->state->createdCpuAccess,
              static_cast<std::uint32_t>(FullCpuAccess));

    view->maximumBytes = 256;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(StorageBuffer(device, descriptor), System::NotSupportedException);
    EXPECT_EQ(view->state->factoryCalls, 1);
    view->maximumBytes = 1024;
    view->state->acceptFactory = false;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(StorageBuffer(device, descriptor), System::NotSupportedException);
    EXPECT_EQ(view->state->factoryCalls, 2);
}

TEST(StorageBufferTest, IndirectOnlyConstructionIsIndependentOfComputeAndStorageLimits)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    renderer->supportsCompute = false;
    renderer->supportsIndirect = true;
    renderer->maximumBytes = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    constexpr StorageBufferUsage usage =
        StorageBufferUsage::TransferDestination | StorageBufferUsage::IndirectArguments;
    const StorageBufferDescriptor descriptor(
        36, usage, StorageBufferCpuAccess::Write);
    StorageBuffer arguments(device, descriptor);
    EXPECT_EQ(arguments.getDescriptor().getUsage(), usage);
    EXPECT_EQ(view->state->factoryCalls, 1);
    EXPECT_EQ(view->state->createdBytes, 36U);

    view->supportsIndirect = false;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(StorageBuffer(device, descriptor), System::NotSupportedException);
    EXPECT_EQ(view->state->factoryCalls, 1)
        << "missing indirect support must refuse before renderer allocation";

    view->supportsIndirect = true;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(
        StorageBuffer(
            device, StorageBufferDescriptor(
                        36, usage | StorageBufferUsage::Storage,
                        StorageBufferCpuAccess::Write)),
        System::NotSupportedException);
    EXPECT_EQ(view->state->factoryCalls, 1)
        << "adding Storage usage must independently require compute support";
}

TEST(StorageBufferTest, LegacyDefaultAndExplicitRangesPreserveExactBytes)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    StorageBuffer source(device, 32);
    StorageBuffer destination(device, 32);
    EXPECT_EQ(source.getDescriptor().getUsage(),
              StorageBufferUsage::Storage |
                  StorageBufferUsage::TransferSource |
                  StorageBufferUsage::TransferDestination |
                  StorageBufferUsage::IndirectArguments);
    EXPECT_EQ(source.getDescriptor().getCpuAccess(), FullCpuAccess);

    const std::array<std::uint8_t, 7> bytes = {3, 5, 7, 11, 13, 17, 19};
    source.setBytes(9, bytes.data(), bytes.size());
    source.copyTo(destination, 9, 4, bytes.size());
    std::array<std::uint8_t, 7> read{};
    destination.getBytes(4, read.data(), read.size());
    EXPECT_EQ(read, bytes);
    EXPECT_EQ(view->state->uploadCalls, 1);
    EXPECT_EQ(view->state->copyCalls, 1);
    EXPECT_EQ(view->state->readbackCalls, 1);

    source.setBytes(bytes.data(), bytes.size());
    source.getBytes(read.data(), read.size());
    EXPECT_EQ(read, bytes);
}

TEST(StorageBufferTest, CpuIntentUsageAndOverflowSafeRangesRefuseBeforeRendererWork)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    StorageBuffer gpuOnly(
        device, StorageBufferDescriptor(
                    32, StorageBufferUsage::Storage |
                            StorageBufferUsage::TransferSource |
                            StorageBufferUsage::TransferDestination,
                    StorageBufferCpuAccess::None));
    std::array<std::uint8_t, 8> bytes{};
    EXPECT_THROW(gpuOnly.setBytes(bytes.data(), bytes.size()),
                 System::NotSupportedException);
    EXPECT_THROW(gpuOnly.getBytes(bytes.data(), bytes.size()),
                 System::NotSupportedException);
    StorageBuffer cpuVisible(
        device, StorageBufferDescriptor(32, FullUsage, FullCpuAccess));
    EXPECT_THROW(cpuVisible.setBytes(31, bytes.data(), 2), std::invalid_argument);
    EXPECT_THROW(cpuVisible.getBytes(
                     std::numeric_limits<std::size_t>::max(), bytes.data(), 1),
                 std::invalid_argument);
    EXPECT_THROW(cpuVisible.setBytes(0, nullptr, 1), std::invalid_argument);
    EXPECT_THROW(cpuVisible.getBytes(0, nullptr, 1), std::invalid_argument);

    StorageBuffer noSource(
        device, StorageBufferDescriptor(
                    32, StorageBufferUsage::TransferDestination,
                    StorageBufferCpuAccess::None));
    StorageBuffer noDestination(
        device, StorageBufferDescriptor(
                    32, StorageBufferUsage::TransferSource,
                    StorageBufferCpuAccess::None));
    EXPECT_THROW(noSource.copyTo(gpuOnly, 0, 0, 1), System::NotSupportedException);
    EXPECT_THROW(gpuOnly.copyTo(noDestination, 0, 0, 1), System::NotSupportedException);
    EXPECT_THROW(gpuOnly.copyTo(gpuOnly, 0, 4, 8), std::invalid_argument);
    EXPECT_THROW(gpuOnly.copyTo(gpuOnly, 31, 0, 2), std::invalid_argument);
    EXPECT_EQ(view->state->uploadCalls, 0);
    EXPECT_EQ(view->state->readbackCalls, 0);
    EXPECT_EQ(view->state->copyCalls, 0);
}

TEST(StorageBufferTest, CopyRefusesForeignDeviceAndRendererTransferFailure)
{
    GraphicsDevice first;
    GraphicsDevice second;
    int firstDestructions = 0;
    int secondDestructions = 0;
    auto firstRenderer = MakeRenderer(firstDestructions);
    StorageBufferContractRenderer* const firstView = firstRenderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        first, std::move(firstRenderer));
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        second, MakeRenderer(secondDestructions));
    const StorageBufferDescriptor descriptor(
        16, StorageBufferUsage::TransferSource |
                StorageBufferUsage::TransferDestination,
        StorageBufferCpuAccess::None);
    StorageBuffer source(first, descriptor);
    StorageBuffer foreign(second, descriptor);
    EXPECT_THROW(source.copyTo(foreign, 0, 0, 4), std::invalid_argument);

    StorageBuffer local(first, descriptor);
    firstView->state->acceptTransfer = false;
    EXPECT_THROW(source.copyTo(local, 0, 0, 4), System::NotSupportedException);
    EXPECT_EQ(firstView->state->copyCalls, 1);
}

TEST(StorageBufferTest, TracksDisposesAndReleasesItsRendererRecordInDeviceOrder)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, MakeRenderer(nativeDestructions));
    const StorageBufferDescriptor descriptor(32, FullUsage, FullCpuAccess);
    const std::size_t baseline = device.GetTrackedResourceCount();
    int creations = 0;
    int destructions = 0;
    int disposingEvents = 0;
    device.ResourceCreated +=
        [&](System::Object*, const ResourceCreatedEventArgs&) { ++creations; };
    device.ResourceDestroyed +=
        [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destructions; };

    auto buffer = std::make_unique<StorageBuffer>(device, descriptor);
    buffer->Disposing +=
        [&](System::Object*, const System::EventArgs&) { ++disposingEvents; };
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    EXPECT_EQ(buffer->GetTypeName(), "CNA.Graphics.StorageBuffer");
    EXPECT_EQ(creations, 1);
    buffer->Dispose();
    EXPECT_TRUE(buffer->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(disposingEvents, 1);
    EXPECT_THROW((void)buffer->getDescriptor(), System::ObjectDisposedException);
    EXPECT_THROW((void)buffer->getByteSize(), System::ObjectDisposedException);
    EXPECT_THROW((void)buffer->getRendererEXT(), System::ObjectDisposedException);
    buffer->Dispose();
    EXPECT_EQ(nativeDestructions, 1);

    auto deviceOwned = std::make_unique<StorageBuffer>(device, descriptor);
    device.Dispose();
    EXPECT_TRUE(deviceOwned->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), 0U);
    EXPECT_EQ(nativeDestructions, 2);
    EXPECT_THROW(StorageBuffer(device, descriptor), System::ObjectDisposedException);
}

TEST(StorageBufferTest, TypedViewChecksSizeAndForwardsExplicitIntent)
{
    GraphicsDevice device;
    int destructions = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, MakeRenderer(destructions));
    StorageBufferT<std::uint32_t> values(
        device, 4, StorageBufferUsage::Storage, FullCpuAccess);
    const std::vector<std::uint32_t> written = {2, 3, 5, 7};
    values.setData(written);
    EXPECT_EQ(values.getData(), written);
    EXPECT_EQ(values.getElementCount(), 4U);
    EXPECT_EQ(values.getBuffer().getDescriptor().getUsage(), StorageBufferUsage::Storage);
    const StorageBufferT<std::uint32_t>& constValues = values;
    EXPECT_EQ(constValues.getBuffer().getByteSize(), 16U);
    EXPECT_THROW(values.setData(std::vector<std::uint32_t>(5)), std::invalid_argument);
    EXPECT_THROW((StorageBufferT<std::uint64_t>(
                     device, std::numeric_limits<std::size_t>::max())),
                 std::invalid_argument);
}

TEST(ConstantBufferTest, TypedViewAlignsUsesSharedStorageAndZeroesEveryPaddingByte)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    renderer->uniformAlignment = 64;
    renderer->maximumUniformBytes = 64;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    struct TinyConstant
    {
        std::uint32_t value;
    };
    ConstantBufferT<TinyConstant> constants(device);
    EXPECT_EQ(constants.getBuffer().getByteSize(), 64U);
    EXPECT_EQ(constants.getBuffer().getDescriptor().getUsage(),
              StorageBufferUsage::Constant);
    EXPECT_EQ(constants.getBuffer().getDescriptor().getCpuAccess(),
              StorageBufferCpuAccess::Write);
    EXPECT_EQ(view->state->createdBytes, 64U);
    EXPECT_EQ(view->state->createdUsage,
              static_cast<std::uint32_t>(StorageBufferUsage::Constant));
    EXPECT_EQ(view->state->createdCpuAccess,
              static_cast<std::uint32_t>(StorageBufferCpuAccess::Write));

    const TinyConstant value{UINT32_C(0x12345678)};
    constants.setData(value);
    ASSERT_EQ(view->state->lastUpload.size(), 64U);
    std::uint32_t uploaded = 0;
    std::memcpy(&uploaded, view->state->lastUpload.data(), sizeof(uploaded));
    EXPECT_EQ(uploaded, value.value);
    EXPECT_TRUE(std::all_of(
        view->state->lastUpload.begin() + static_cast<std::ptrdiff_t>(sizeof(value)),
        view->state->lastUpload.end(),
        [](const std::uint8_t byte) { return byte == 0; }));

    const ConstantBufferT<TinyConstant>& readOnly = constants;
    EXPECT_EQ(readOnly.getBuffer().getByteSize(), 64U);
}

TEST(ConstantBufferTest, RawAndTypedConstructionValidateNativeRangeAndAlignment)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    renderer->maximumUniformBytes = 32;
    renderer->uniformAlignment = 16;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    StorageBuffer raw(
        device, StorageBufferDescriptor(
                    32, StorageBufferUsage::Constant,
                    StorageBufferCpuAccess::Write));
    EXPECT_EQ(raw.getByteSize(), 32U);
    EXPECT_THROW(
        StorageBuffer(
            device, StorageBufferDescriptor(
                        33, StorageBufferUsage::Constant,
                        StorageBufferCpuAccess::Write)),
        System::NotSupportedException);

    view->uniformAlignment = 64;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW((ConstantBufferT<ConstantValue>{device}), System::NotSupportedException);

    view->maximumUniformBytes = 1024;
    view->uniformAlignment = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW((ConstantBufferT<ConstantValue>{device}), System::NotSupportedException);

    view->maximumUniformBytes = 0;
    view->uniformAlignment = 16;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW((ConstantBufferT<ConstantValue>{device}), System::NotSupportedException);
}

TEST(ConstantBufferTest, ComputeBindingValidatesBothOverloadsAndBackendAcceptance)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    ComputeShader shader(device, "recording compute");

    ConstantBufferT<ConstantValue> typed(device);
    shader.bindConstantBuffer(3, typed);
    EXPECT_EQ(view->state->constantBindCalls, 1);

    StorageBuffer raw(
        device, StorageBufferDescriptor(
                    16, StorageBufferUsage::Constant,
                    StorageBufferCpuAccess::Write));
    shader.bindConstantBuffer(4, raw);
    EXPECT_EQ(view->state->constantBindCalls, 2);
    EXPECT_THROW(shader.bindConstantBuffer(-1, raw), std::invalid_argument);

    StorageBuffer storageOnly(
        device, StorageBufferDescriptor(
                    16, StorageBufferUsage::Storage,
                    StorageBufferCpuAccess::None));
    EXPECT_THROW(shader.bindConstantBuffer(0, storageOnly),
                 System::NotSupportedException);

    view->state->acceptConstantBinding = false;
    EXPECT_THROW(shader.bindConstantBuffer(0, raw), System::NotSupportedException);
    EXPECT_EQ(view->state->constantBindCalls, 3);

    raw.Dispose();
    EXPECT_THROW(shader.bindConstantBuffer(0, raw),
                 System::ObjectDisposedException);

    GraphicsDevice other;
    int otherDestructions = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        other, MakeRenderer(otherDestructions));
    ConstantBufferT<ConstantValue> foreign(other);
    EXPECT_THROW(shader.bindConstantBuffer(0, foreign), std::invalid_argument);
}

TEST(ConstantBufferTest, RendererNeutralBindingDefaultRefusesExplicitly)
{
    DefaultConstantBindingComputeRenderer renderer;
    EXPECT_FALSE(renderer.BindConstantBufferEXT(0, nullptr));
}

TEST(ConstantBufferTest, PackageSelectionRefusesAnUnpublishedUniformRange)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    renderer->maximumUniformBytes = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    const ShaderPackageEXT package(
        {ShaderCodeEXT(
            CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Compute,
            "main", "constant.comp", "void main() {}")},
        {CNA::ShaderStageEXT::Compute},
        {ShaderBindingRequirementEXT(
            "Parameters", 0, ShaderBindingTypeEXT::ConstantBuffer,
            CNA::ShaderStageEXT::Compute)});
    const auto selection = package.selectFor(device);

    EXPECT_FALSE(selection.isUsable());
    EXPECT_NE(selection.getDiagnostic().find("requires constant buffers"),
              std::string::npos);
}

TEST(StorageBufferTest, ComputeBindingRequiresLiveSameDeviceStorageUsage)
{
    GraphicsDevice device;
    int destructions = 0;
    auto renderer = MakeRenderer(destructions);
    StorageBufferContractRenderer* const view = renderer.get();
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    ComputeShader shader(device, "recording compute");
    StorageBuffer storage(
        device, StorageBufferDescriptor(
                    16, StorageBufferUsage::Storage, StorageBufferCpuAccess::None));
    shader.bindStorageBuffer(7, storage);
    EXPECT_EQ(view->state->bindCalls, 1);
    EXPECT_THROW(shader.bindStorageBuffer(-1, storage), std::invalid_argument);

    StorageBuffer transferOnly(
        device, StorageBufferDescriptor(
                    16, StorageBufferUsage::TransferSource,
                    StorageBufferCpuAccess::None));
    EXPECT_THROW(shader.bindStorageBuffer(0, transferOnly), System::NotSupportedException);
    transferOnly.Dispose();
    EXPECT_THROW(shader.bindStorageBuffer(0, transferOnly),
                 System::ObjectDisposedException);

    GraphicsDevice other;
    int otherDestructions = 0;
    CNA::Internal::StorageBufferGraphicsDeviceTestPeer::ReplaceRenderer(
        other, MakeRenderer(otherDestructions));
    StorageBuffer foreign(
        other, StorageBufferDescriptor(
                   16, StorageBufferUsage::Storage, StorageBufferCpuAccess::None));
    EXPECT_THROW(shader.bindStorageBuffer(0, foreign), std::invalid_argument);
    EXPECT_EQ(view->state->bindCalls, 1);
}

#endif // CNA_CNAEXT
