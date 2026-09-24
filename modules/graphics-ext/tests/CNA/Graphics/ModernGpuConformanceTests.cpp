// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan_modern_graphics.md VMG-0012: a renderer-neutral conformance suite for the modern
// compute/buffer/storage-image surface.
//
// The shared engine-layer suite already runs almost everywhere, but six generic compute cases
// (dispatch geometry, uniforms, validation, images) only ever ran on GLSL renderers: their payload is
// a legacy GLSL ES string, so Vulkan -- which executes SPIR-V only -- skipped them. The same semantics
// are asserted here through ONE portable package per program (generated GLSL ES + SPIR-V from
// shaders/modern_conformance, tools/shader_package), so every renderer that implements the API runs
// the same observable checks, and a renderer without it skips with the capability that is missing.
//
// Nothing here is Vulkan-specific. What the renderer does internally -- barriers, descriptor reuse,
// fence-retired lifetime -- is visible only as results: a chained dispatch that reads what the
// previous one wrote, uniforms and uploads that keep call order, work that survives disposal of its
// inputs (docs/adr/0001-modern-gpu-ordering-lifetime.md). No test calls ComputeShader::barrier().
//
// Every case that runs counts the checks it executed; one that ran and checked nothing fails, and
// the process prints a one-line tally at exit, so "the binary ran" is never mistaken for "the
// assertions ran" (GTI-0007's lesson).

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "EngineTestSupport.hpp"
#include "ModernConformanceShaderPackage.generated.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using CNA::GraphicsCapability;
using CNA::GraphicsImageAccess;
using CNA::Graphics::ComputeShader;
using CNA::Graphics::ShaderBindingRequirementEXT;
using CNA::Graphics::ShaderBindingTypeEXT;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferCpuAccess;
using CNA::Graphics::StorageBufferDescriptor;
using CNA::Graphics::StorageBufferUsage;
using CNA::Graphics::StorageTexture2D;
using CNA::Graphics::StorageTexture2DDescriptor;
using CNA::Graphics::StorageTexture2DUsage;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace {

    namespace Package = CNA::Tests::ModernConformance;

    /// How many conformance cases ran, how many checks they executed, and how many skipped.
    struct Tally
    {
        int ran = 0;
        int checks = 0;
        int skipped = 0;
    };

    Tally& ConformanceTally()
    {
        static Tally tally;
        return tally;
    }

    /// Prints the tally once, at the end of the process, whichever filter selected the cases.
    class ConformanceTallyEnvironment : public ::testing::Environment
    {
    public:
        void TearDown() override
        {
            const Tally& t = ConformanceTally();
            if (t.ran + t.skipped == 0)
                return;
            std::printf("[CONFORMANCE] modern GPU: %d case(s) ran %d check(s), %d skipped\n", t.ran,
                        t.checks, t.skipped);
        }
    };

    [[maybe_unused]] ::testing::Environment* const kTallyEnvironment =
        ::testing::AddGlobalTestEnvironment(new ConformanceTallyEnvironment);

    template <std::size_t N>
    std::vector<std::uint8_t> Bytes(const std::uint32_t (&words)[N])
    {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
        return std::vector<std::uint8_t>(begin, begin + sizeof(words));
    }

    /// One compute program as a four-variant package: the generated GLSL ES and desktop GLSL
    /// text, the SPIR-V words and the WGSL the generator derives from the same Vulkan GLSL
    /// (plans/plan_webgpu_modern_graphics.md WMG-0005), so every renderer runs the same program.
    /// The desktop variant is what a desktop core context compiles
    /// (plans/plan_opengl4_modern_graphics.md GL4-0025).
    template <std::size_t N>
    ShaderPackageEXT ComputePackage(std::string_view label, std::string_view esSource,
                                    std::string_view desktopSource,
                                    const std::uint32_t (&spirv)[N], std::string_view wgsl,
                                    std::vector<ShaderBindingRequirementEXT> bindings)
    {
        return ShaderPackageEXT(
            {
                ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Compute, "main",
                              std::string(label) + ".es.comp.glsl", std::string(esSource)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop, CNA::ShaderStageEXT::Compute,
                              "main", std::string(label) + ".desktop.comp.glsl",
                              std::string(desktopSource)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute, "main",
                              std::string(label) + ".vulkan.comp.glsl", Bytes(spirv)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl, CNA::ShaderStageEXT::Compute, "main",
                              std::string(label) + ".vulkan.comp.glsl -> wgsl", std::string(wgsl)),
            },
            {CNA::ShaderStageEXT::Compute}, std::move(bindings));
    }

    ShaderBindingRequirementEXT StorageAt(const char* name, int slot)
    {
        return ShaderBindingRequirementEXT(name, slot, ShaderBindingTypeEXT::StorageBuffer,
                                           CNA::ShaderStageEXT::Compute);
    }

    class ModernGpuConformance : public ::testing::Test
    {
    protected:
        CnaTest::EngineLayer::HiDefDevice gd;
        int checks_ = 0;
        bool skipped_ = false;

        /// Counts one executed check and asserts it.
        void Check(bool condition, const std::string& what)
        {
            ++checks_;
            EXPECT_TRUE(condition) << what;
        }

        /// Skips, and records the skip, when the renderer lacks what the case exercises.
        bool Lacks(bool supported, const char* what)
        {
            if (supported)
                return false;
            skipped_ = true;
            ++ConformanceTally().skipped;
            return true;
        }

        bool LacksCompute()
        {
            return Lacks(gd.SupportsCapability(GraphicsCapability::ComputeShaders), "compute");
        }

        void TearDown() override
        {
            if (skipped_ || IsSkipped())
                return;
            ++ConformanceTally().ran;
            ConformanceTally().checks += checks_;
            EXPECT_GT(checks_, 0) << "a conformance case that ran must execute at least one check";
        }

        std::unique_ptr<StorageBuffer> Buffer(std::size_t bytes, StorageBufferUsage usage)
        {
            return std::make_unique<StorageBuffer>(
                gd, StorageBufferDescriptor(bytes, usage,
                                            StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write));
        }

        static constexpr StorageBufferUsage kStorage =
            StorageBufferUsage::Storage | StorageBufferUsage::TransferSource |
            StorageBufferUsage::TransferDestination;
    };

    template <typename T>
    void Upload(StorageBuffer& buffer, const std::vector<T>& values)
    {
        buffer.setBytes(values.data(), values.size() * sizeof(T));
    }

    template <typename T>
    std::vector<T> Download(const StorageBuffer& buffer, std::size_t count)
    {
        std::vector<T> values(count);
        buffer.getBytes(values.data(), count * sizeof(T));
        return values;
    }

} // namespace

TEST_F(ModernGpuConformance, ADispatchCoversExactlyItsThreeDimensionalGrid)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    // Groups of 4x2x2 invocations; a 3x2x2 dispatch is a 12x4x4 grid of 192 invocations.
    constexpr int kWidth = 12, kHeight = 4, kDepth = 4;
    constexpr std::size_t kCount = kWidth * kHeight * kDepth;
    constexpr std::uint32_t kUnwritten = 0xFFFFFFFFu;
    ComputeShader grid(gd, ComputePackage("grid", Package::kGridEsSource,
                                          Package::kGridDesktopSource,
                                          Package::kGridSpirV, Package::kGridWgsl,
                                          {StorageAt("Output", 0)}));
    auto output = Buffer(kCount * sizeof(std::uint32_t), kStorage);

    Upload(*output, std::vector<std::uint32_t>(kCount, kUnwritten));
    grid.bindStorageBuffer(0, *output);
    grid.setUniform("uWidth", kWidth);
    grid.setUniform("uHeight", kHeight);
    grid.dispatch(3, 2, 2);

    const auto values = Download<std::uint32_t>(*output, kCount);
    int wrong = 0;
    for (int z = 0; z < kDepth; ++z)
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
            {
                const auto expected = (static_cast<std::uint32_t>(x) << 20) |
                                      (static_cast<std::uint32_t>(y) << 10) |
                                      static_cast<std::uint32_t>(z);
                if (values[static_cast<std::size_t>(x + y * kWidth + z * kWidth * kHeight)] != expected)
                    ++wrong;
            }
    Check(wrong == 0, std::to_string(wrong) + " of 192 invocations did not write their own global id "
                                              "at their own index");

    // A one-group dispatch writes one group's 16 invocations and nothing else.
    Upload(*output, std::vector<std::uint32_t>(kCount, kUnwritten));
    grid.dispatch(1, 1, 1);
    const auto single = Download<std::uint32_t>(*output, kCount);
    int written = 0, outside = 0;
    for (int z = 0; z < kDepth; ++z)
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
            {
                const bool inGroup = x < 4 && y < 2 && z < 2;
                const bool isWritten =
                    single[static_cast<std::size_t>(x + y * kWidth + z * kWidth * kHeight)] != kUnwritten;
                written += isWritten && inGroup ? 1 : 0;
                outside += isWritten && !inGroup ? 1 : 0;
            }
    Check(written == 16 && outside == 0,
          "a one-group dispatch wrote " + std::to_string(written) + " of its 16 invocations and " +
              std::to_string(outside) + " outside it");
}

TEST_F(ModernGpuConformance, UniformsAndUploadsKeepCallOrderAcrossReadModifyWriteDispatches)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    // Three rounds of acc += input * scale with a new upload and a new scale each round and no read
    // in between: the result is exact only if every dispatch saw the upload and the uniform issued
    // immediately before it -- not a later one, not an earlier one.
    constexpr std::size_t kCount = 256;
    constexpr int kActive = 200;
    ComputeShader accumulate(gd, ComputePackage("accumulate", Package::kAccumulateEsSource,
                                                Package::kAccumulateDesktopSource,
                                                Package::kAccumulateSpirV, Package::kAccumulateWgsl,
                                                {StorageAt("Input", 0), StorageAt("Accumulator", 1)}));
    auto inputs = Buffer(kCount * sizeof(float), kStorage);
    auto acc = Buffer(kCount * sizeof(float), kStorage);
    Upload(*acc, std::vector<float>(kCount, 1.0f));
    accumulate.bindStorageBuffer(0, *inputs);
    accumulate.bindStorageBuffer(1, *acc);
    accumulate.setUniform("uCount", kActive);

    const float scales[3] = {0.5f, 1.5f, 2.5f};
    std::vector<float> expected(kCount, 1.0f);
    for (int round = 0; round < 3; ++round)
    {
        std::vector<float> in(kCount);
        for (std::size_t i = 0; i < kCount; ++i)
            in[i] = static_cast<float>((static_cast<int>(i) % 7) + round * 3);
        Upload(*inputs, in);
        accumulate.setUniform("uScale", scales[round]);
        accumulate.dispatch(static_cast<int>(kCount / 64));
        for (std::size_t i = 0; i < static_cast<std::size_t>(kActive); ++i)
            expected[i] += in[i] * scales[round];
    }

    const auto result = Download<float>(*acc, kCount);
    int wrong = 0, touchedBeyond = 0;
    for (std::size_t i = 0; i < kCount; ++i)
    {
        if (i < static_cast<std::size_t>(kActive) && result[i] != expected[i])
            ++wrong;
        if (i >= static_cast<std::size_t>(kActive) && result[i] != 1.0f)
            ++touchedBeyond;
    }
    Check(wrong == 0, std::to_string(wrong) + " accumulated elements differ from the in-order sum");
    Check(touchedBeyond == 0, std::to_string(touchedBeyond) +
                                  " elements past the uCount uniform were modified");
}

TEST_F(ModernGpuConformance, ChainedDispatchesSeeEachOthersWritesWithoutABarrier)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    // A -> B -> C -> A through one program rebound between dispatches, and only the last buffer read
    // back. Each link doubles and adds one, so C = 4i + 3 and the rewritten A = 8i + 7 -- exact only
    // if every dispatch read what the previous one wrote. No ComputeShader::barrier() is called.
    constexpr std::size_t kCount = 512;
    ComputeShader chain(gd, ComputePackage("chain", Package::kChainEsSource,
                                           Package::kChainDesktopSource,
                                           Package::kChainSpirV, Package::kChainWgsl,
                                           {StorageAt("Source", 0), StorageAt("Destination", 1)}));
    auto a = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    auto b = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    auto c = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    std::vector<std::uint32_t> seed(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
        seed[i] = static_cast<std::uint32_t>(i);
    Upload(*a, seed);
    chain.setUniform("uCount", static_cast<int>(kCount));

    StorageBuffer* const links[4] = {a.get(), b.get(), c.get(), a.get()};
    for (int link = 0; link < 3; ++link)
    {
        chain.bindStorageBuffer(0, *links[link]);
        chain.bindStorageBuffer(1, *links[link + 1]);
        chain.dispatch(static_cast<int>(kCount / 64));
    }

    const auto cValues = Download<std::uint32_t>(*c, kCount);
    const auto aValues = Download<std::uint32_t>(*a, kCount);
    int wrongC = 0, wrongA = 0;
    for (std::size_t i = 0; i < kCount; ++i)
    {
        wrongC += cValues[i] != 4u * static_cast<std::uint32_t>(i) + 3u ? 1 : 0;
        wrongA += aValues[i] != 8u * static_cast<std::uint32_t>(i) + 7u ? 1 : 0;
    }
    Check(wrongC == 0, std::to_string(wrongC) + " elements of the second link missed the first's writes");
    Check(wrongA == 0, std::to_string(wrongA) + " elements of the third link (writing the first "
                                                "link's own source) missed the chain");
}

TEST_F(ModernGpuConformance, AcceptedWorkSurvivesDisposalOfItsProgramAndInputs)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    // ADR 0001: Dispose is logically immediate and never cancels accepted work. The program and the
    // input buffer are destroyed right after the dispatch, before anything waits for it.
    constexpr std::size_t kCount = 256;
    auto output = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    {
        ComputeShader chain(gd, ComputePackage("chain", Package::kChainEsSource,
                                               Package::kChainDesktopSource,
                                               Package::kChainSpirV, Package::kChainWgsl,
                                               {StorageAt("Source", 0), StorageAt("Destination", 1)}));
        auto input = Buffer(kCount * sizeof(std::uint32_t), kStorage);
        std::vector<std::uint32_t> seed(kCount);
        for (std::size_t i = 0; i < kCount; ++i)
            seed[i] = 1000u + static_cast<std::uint32_t>(i);
        Upload(*input, seed);
        chain.bindStorageBuffer(0, *input);
        chain.bindStorageBuffer(1, *output);
        chain.setUniform("uCount", static_cast<int>(kCount));
        chain.dispatch(static_cast<int>(kCount / 64));
        input->Dispose();
    }

    const auto values = Download<std::uint32_t>(*output, kCount);
    int wrong = 0;
    for (std::size_t i = 0; i < kCount; ++i)
        wrong += values[i] != (1000u + static_cast<std::uint32_t>(i)) * 2u + 1u ? 1 : 0;
    Check(wrong == 0, std::to_string(wrong) + " results were lost when the program and input were "
                                              "disposed after the dispatch was accepted");
}

TEST_F(ModernGpuConformance, ManySmallUploadsInOneFrameAreEachConsumedInOrder)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    // 64 upload -> dispatch pairs with no read between them: each dispatch must see its own upload,
    // so the staging copy of each one has to stay alive until the GPU consumed it.
    constexpr std::size_t kCount = 64;
    constexpr int kRounds = 64;
    ComputeShader accumulate(gd, ComputePackage("accumulate", Package::kAccumulateEsSource,
                                                Package::kAccumulateDesktopSource,
                                                Package::kAccumulateSpirV, Package::kAccumulateWgsl,
                                                {StorageAt("Input", 0), StorageAt("Accumulator", 1)}));
    auto inputs = Buffer(kCount * sizeof(float), kStorage);
    auto acc = Buffer(kCount * sizeof(float), kStorage);
    Upload(*acc, std::vector<float>(kCount, 0.0f));
    accumulate.bindStorageBuffer(0, *inputs);
    accumulate.bindStorageBuffer(1, *acc);
    accumulate.setUniform("uCount", static_cast<int>(kCount));
    accumulate.setUniform("uScale", 1.0f);
    float expected = 0.0f;
    for (int round = 1; round <= kRounds; ++round)
    {
        Upload(*inputs, std::vector<float>(kCount, static_cast<float>(round)));
        accumulate.dispatch(1);
        expected += static_cast<float>(round);
    }
    const auto result = Download<float>(*acc, kCount);
    int wrong = 0;
    for (float v : result)
        wrong += v != expected ? 1 : 0;
    Check(wrong == 0, std::to_string(wrong) + " of 64 elements differ from the sum of 64 in-order "
                                              "uploads (" + std::to_string(expected) + ")");
}

TEST_F(ModernGpuConformance, BufferRangesCopiesAndLargeUploadsAreExact)
{
    // Transfer-only buffers need no compute; this runs wherever StorageBuffer exists at all.
    std::unique_ptr<StorageBuffer> source, destination, large;
    try
    {
        source = Buffer(4096, StorageBufferUsage::TransferSource | StorageBufferUsage::TransferDestination);
        destination = Buffer(4096, StorageBufferUsage::TransferSource | StorageBufferUsage::TransferDestination);
        large = Buffer(4u << 20, StorageBufferUsage::TransferSource | StorageBufferUsage::TransferDestination);
    }
    catch (const std::exception& e)
    {
        if (Lacks(false, "storage buffers"))
            GTEST_SKIP() << "this renderer creates no StorageBuffer: " << e.what();
    }

    std::vector<std::uint8_t> pattern(4096);
    for (std::size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xFFu);
    source->setBytes(pattern.data(), pattern.size());
    // A partial update in the middle leaves both neighbours alone.
    const std::uint8_t patch[5] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4};
    source->setBytes(1001, patch, sizeof(patch));
    std::memcpy(pattern.data() + 1001, patch, sizeof(patch));
    std::vector<std::uint8_t> back(4096);
    source->getBytes(back.data(), back.size());
    Check(back == pattern, "a partial update at an odd offset changed something other than its bytes");

    // A GPU copy between odd offsets moves exactly its range.
    std::vector<std::uint8_t> zero(4096, 0);
    destination->setBytes(zero.data(), zero.size());
    source->copyTo(*destination, 17, 333, 1500);
    std::vector<std::uint8_t> copied(4096);
    destination->getBytes(copied.data(), copied.size());
    std::vector<std::uint8_t> expected(4096, 0);
    std::memcpy(expected.data() + 333, pattern.data() + 17, 1500);
    Check(copied == expected, "copyTo(17 -> 333, 1500 bytes) did not move exactly its range");

    // A 4 MiB upload round-trips byte for byte.
    std::vector<std::uint8_t> big(4u << 20);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<std::uint8_t>((i ^ (i >> 11)) & 0xFFu);
    large->setBytes(big.data(), big.size());
    std::vector<std::uint8_t> bigBack(big.size());
    large->getBytes(bigBack.data(), bigBack.size());
    Check(bigBack == big, "a 4 MiB upload did not read back byte for byte");
}

TEST_F(ModernGpuConformance, AStorageImageHoldsExactlyWhatComputeWrote)
{
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";

    constexpr int kSize = 16;
    std::unique_ptr<StorageTexture2D> image;
    try
    {
        image = std::make_unique<StorageTexture2D>(
            gd, StorageTexture2DDescriptor(kSize, kSize, 1, SurfaceFormat::Color,
                                           StorageTexture2DUsage::StorageWrite |
                                               StorageTexture2DUsage::TransferSource));
    }
    catch (const std::exception& e)
    {
        if (Lacks(false, "storage images"))
            GTEST_SKIP() << "this renderer has no Color storage image: " << e.what();
    }

    ComputeShader writer(gd, ComputePackage("image", Package::kImageEsSource,
                                            Package::kImageDesktopSource,
                                            Package::kImageSpirV, Package::kImageWgsl,
                                            {ShaderBindingRequirementEXT(
                                                "uImage", 0, ShaderBindingTypeEXT::StorageTexture2D,
                                                CNA::ShaderStageEXT::Compute)}));
    try
    {
        writer.bindStorageTexture(0, *image, GraphicsImageAccess::WriteOnly);
    }
    catch (const std::exception& e)
    {
        if (Lacks(false, "compute image binding"))
            GTEST_SKIP() << "this renderer cannot bind a storage image to compute: " << e.what();
    }
    writer.dispatch(kSize / 8, kSize / 8);

    std::vector<std::uint8_t> texels(static_cast<std::size_t>(kSize * kSize * 4));
    image->getData(0, nullptr, texels.data(), texels.size());
    int wrong = 0;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const std::uint8_t* t = &texels[static_cast<std::size_t>((y * kSize + x) * 4)];
            wrong += (t[0] != x || t[1] != y || t[2] != (x ^ y) || t[3] != 255) ? 1 : 0;
        }
    Check(wrong == 0, std::to_string(wrong) + " of 256 texels differ from what the compute program "
                                              "stored");
}

TEST_F(ModernGpuConformance, ClassicDrawingIsUnchangedByInterleavedCompute)
{
    // The two APIs share one device and one renderer (ADR 0001: one observable order). A frame of
    // ordinary XNA drawing -- additive blending, no culling, depth on, a vertex-coloured BasicEffect
    // into a render target -- must produce the same pixels and leave the same device state whether
    // or not compute work is recorded before, between and after its draws.
    //
    // A dispatch inside a bind cycle ends the native render pass on renderers that cannot compute
    // inside one, so the cycle is resumed afterwards. Every render-target shape is exercised, and
    // the NEAR triangle is drawn first: a resumed cycle that dropped its depth would let the far
    // triangle through where they overlap, and one that dropped its colour would lose the first
    // draw entirely.
    if (LacksCompute())
        GTEST_SKIP() << "this renderer has no compute shaders";
    if (Lacks(gd.SupportsCapability(GraphicsCapability::ThreeD), "3D"))
        GTEST_SKIP() << "this renderer has no 3D pipeline";

    constexpr int kSize = 32;
    constexpr std::size_t kCount = 256;
    BasicEffect effect(gd);
    effect.VertexColorEnabled = true;
    effect.setLightingEnabledProperty(false);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::getIdentityProperty());
    const VertexPositionColor farTriangle[3] = {
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.5f), Color(200, 0, 0)),
        VertexPositionColor(Vector3(0.6f, -1.0f, 0.5f), Color(200, 0, 0)),
        VertexPositionColor(Vector3(-1.0f, 1.0f, 0.5f), Color(200, 0, 0))};
    const VertexPositionColor nearTriangle[3] = {
        VertexPositionColor(Vector3(1.0f, 1.0f, 0.25f), Color(0, 60, 180)),
        VertexPositionColor(Vector3(-0.6f, 1.0f, 0.25f), Color(0, 60, 180)),
        VertexPositionColor(Vector3(1.0f, -1.0f, 0.25f), Color(0, 60, 180))};

    ComputeShader chain(gd, ComputePackage("chain", Package::kChainEsSource,
                                           Package::kChainDesktopSource,
                                           Package::kChainSpirV, Package::kChainWgsl,
                                           {StorageAt("Source", 0), StorageAt("Destination", 1)}));
    auto a = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    auto b = Buffer(kCount * sizeof(std::uint32_t), kStorage);
    Upload(*a, std::vector<std::uint32_t>(kCount, 5u));
    chain.bindStorageBuffer(0, *a);
    chain.bindStorageBuffer(1, *b);
    chain.setUniform("uCount", static_cast<int>(kCount));

    const auto check = [&](const std::string& shape, RenderTarget2D& target,
                           const std::vector<RenderTargetBinding>& bindings) {
        const auto frame = [&](bool withCompute) {
            if (withCompute) chain.dispatch(static_cast<int>(kCount / 64));
            gd.SetRenderTargets(bindings);
            gd.Clear(Color(10, 20, 30, 255));
            gd.setBlendStateProperty(BlendState::Additive);
            gd.setRasterizerStateProperty(RasterizerState::CullNone);
            gd.setDepthStencilStateProperty(DepthStencilState::Default);
            effect.Apply();
            gd.DrawUserPrimitives(PrimitiveType::TriangleList, nearTriangle, 0, 1);
            if (withCompute) chain.dispatch(static_cast<int>(kCount / 64));
            effect.Apply();
            gd.DrawUserPrimitives(PrimitiveType::TriangleList, farTriangle, 0, 1);
            if (withCompute) chain.dispatch(static_cast<int>(kCount / 64));
            gd.SetRenderTarget(nullptr);
            std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        };

        const auto plain = frame(false);
        const auto mixed = frame(true);
        int differing = 0;
        for (std::size_t i = 0; i < plain.size(); ++i)
            differing += plain[i] != mixed[i] ? 1 : 0;
        Check(differing == 0, shape + ": " + std::to_string(differing) +
                                  " pixels differ once compute is interleaved with the classic "
                                  "draws");
        // The frame must have drawn both triangles and depth-rejected the far one where they
        // overlap, or the comparison proves nothing about either attachment.
        int red = 0, blue = 0, both = 0;
        for (const Color& c : plain)
        {
            const bool r = c.getRProperty() > 150;
            const bool bl = c.getBProperty() > 150;
            red += r ? 1 : 0;
            blue += bl ? 1 : 0;
            both += (r && bl) ? 1 : 0;
        }
        Check(red > 0 && blue > 0 && both == 0,
              shape + ": the classic frame drew both triangles with the far one depth-tested away "
                      "(red " + std::to_string(red) + ", blue " + std::to_string(blue) +
                  ", both " + std::to_string(both) + " pixels)");
    };

    {
        RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24);
        check("single-sample DiscardContents", target, {RenderTargetBinding(&target)});
    }
    {
        RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24,
                              0, RenderTargetUsage::PreserveContents);
        check("single-sample PreserveContents", target, {RenderTargetBinding(&target)});
    }
    {
        RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24,
                              4);
        check("multisampled DiscardContents", target, {RenderTargetBinding(&target)});
    }
    if (gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets))
    {
        RenderTarget2D first(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24);
        RenderTarget2D second(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
        check("two render targets", first,
              {RenderTargetBinding(&first), RenderTargetBinding(&second)});
    }

    Check(gd.getBlendStateProperty().getColorSourceBlendProperty() ==
              BlendState::Additive.getColorSourceBlendProperty() &&
              gd.getRasterizerStateProperty().getCullModeProperty() ==
                  RasterizerState::CullNone.getCullModeProperty(),
          "compute work changed the device's classic blend or rasterizer state");
    const auto chained = Download<std::uint32_t>(*b, kCount);
    Check(chained.front() == 11u && chained.back() == 11u,
          "the interleaved compute work itself produced its result (5 * 2 + 1)");
}

#endif // CNA_CNAEXT
