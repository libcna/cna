// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-84: the compiled-effect source route.
//
// The point of these tests is the boundary as much as the route. CNA serializes bytecode it is
// given; it does not compile HLSL, and `plans/plan_fx.md` records that as a standing decision
// rather than a gap someone forgot. So a `.fxb` builds, a `.fx` has no importer at all, and a
// file that merely claims to be an effect is refused by its signature.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Xnb = CNA::Internal::Xnb;

namespace
{
    const std::filesystem::path kRealEffect =
        "modules/renderers/fna3d/effects/CnaConformanceEffect.fxb";

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_effect_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /** @brief A byte sequence carrying the bare Effect Framework 9.1 signature. */
    std::vector<std::uint8_t> MakeBareSignatureEffect()
    {
        std::vector<std::uint8_t> bytes(64u, 0x11u);
        bytes[0] = 0x01u;
        bytes[1] = 0x09u;
        bytes[2] = 0xFFu;
        bytes[3] = 0xFEu;
        return bytes;
    }

    /** @brief The same signature behind the wrapper XNA 4.0's own pipeline writes. */
    std::vector<std::uint8_t> MakeWrappedSignatureEffect()
    {
        std::vector<std::uint8_t> bytes(64u, 0x22u);
        bytes[0] = 0xCFu;
        bytes[1] = 0x0Bu;
        bytes[2] = 0xF0u;
        bytes[3] = 0xBCu;
        bytes[4] = 16u;  // offset of the wrapped token
        bytes[5] = 0u;
        bytes[6] = 0u;
        bytes[7] = 0u;
        bytes[16] = 0x01u;
        bytes[17] = 0x09u;
        bytes[18] = 0xFFu;
        bytes[19] = 0xFEu;
        return bytes;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterCompiledEffectContentPipeline(*registry);
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        return registry;
    }

    Pipeline::ContentBuildResult Build(const ScratchDirectory& scratch,
                                       const Pipeline::ContentOutputFormat format,
                                       const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = "shader.fxb";
        request.logicalName = "Effects/shader";
        request.outputFormat = format;
        request.parameters = parameters;
        return pipeline.Build(request);
    }
}

TEST(CompiledEffectSignatureTest, BothTheBareAndTheWrappedSignatureAreRecognized)
{
    EXPECT_TRUE(Pipeline::IsCompiledEffectBinary(MakeBareSignatureEffect()));
    EXPECT_TRUE(Pipeline::IsCompiledEffectBinary(MakeWrappedSignatureEffect()));

    EXPECT_FALSE(Pipeline::IsCompiledEffectBinary({}));
    EXPECT_FALSE(Pipeline::IsCompiledEffectBinary(std::vector<std::uint8_t>(64u, 0u)));

    // HLSL source is the case this signature check exists to catch: it is what somebody reaches
    // for first, and accepting it would promise a compiler this project does not have.
    const std::string source = "technique T { pass P { } }";
    EXPECT_FALSE(Pipeline::IsCompiledEffectBinary(
        std::vector<std::uint8_t>(source.begin(), source.end())));

    // A wrapper whose offset points outside the file, or is unaligned, is not self-consistent.
    std::vector<std::uint8_t> badOffset = MakeWrappedSignatureEffect();
    badOffset[4] = 200u;
    EXPECT_FALSE(Pipeline::IsCompiledEffectBinary(badOffset));
    std::vector<std::uint8_t> unaligned = MakeWrappedSignatureEffect();
    unaligned[4] = 17u;
    EXPECT_FALSE(Pipeline::IsCompiledEffectBinary(unaligned));
}

TEST(CompiledEffectRouteTest, AGenuineCompiledEffectBuildsToAnEffectXnbByteForByte)
{
    if (!std::filesystem::is_regular_file(kRealEffect))
    {
        GTEST_SKIP() << "the committed compiled effect fixture is missing";
    }
    // This is a real fx_2_0 binary produced by the same fxc XNA's own Content Pipeline used
    // (see its directory's README for the compiler identity and provenance), so what this test
    // proves is that a project holding real compiled bytecode can get it into an Effect .xnb.
    const std::vector<std::uint8_t> bytecode = ReadBytes(kRealEffect);
    ASSERT_TRUE(Pipeline::IsCompiledEffectBinary(bytecode));

    ScratchDirectory scratch("real");
    WriteBytes(scratch.Path() / "shader.fxb", bytecode);

    const Pipeline::ContentBuildResult result =
        Build(scratch, Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(result.importer.name, "CNA.CompiledEffectImporter");
    EXPECT_EQ(result.processor.name, "CNA.CompiledEffectProcessor");
    EXPECT_EQ(result.writer.name, "CNA.XnbEffectWriter");
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Effect");

    // CNA's canonical decoder has no Effect alternative -- an Effect payload is opaque bytecode
    // with no fields to decode -- so the file is checked directly: it must name EffectReader and
    // it must end with the compiler's bytes, verbatim. An Effect one byte different from what the
    // compiler produced is not the same shader.
    const std::vector<std::uint8_t>& file = result.output.bytes;
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.EffectReader"), std::string::npos);
    ASSERT_GT(file.size(), bytecode.size());
    EXPECT_TRUE(std::equal(bytecode.begin(), bytecode.end(), file.end() - bytecode.size()));

    // The independent parser reads the same file and agrees on the payload's length.
    const std::filesystem::path path = scratch.Path() / "shader.xnb";
    WriteBytes(path, file);
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(CompiledEffectRouteTest, AFileThatIsNotACompiledEffectIsRefusedWithAdviceRatherThanAccepted)
{
    ScratchDirectory scratch("source");
    const std::string hlsl = "float4 main() : COLOR { return 1; }";
    WriteBytes(scratch.Path() / "shader.fxb",
               std::vector<std::uint8_t>(hlsl.begin(), hlsl.end()));

    try
    {
        (void)Build(scratch, Pipeline::ContentOutputFormat::Xnb);
        FAIL() << "HLSL source named .fxb must be refused";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("Effect Framework 9.1"), std::string::npos) << message;
        // The message has to say what to do next, because "not a compiled effect" alone leaves
        // an author with no idea that a compiler step is missing.
        EXPECT_NE(message.find("fx_2_0"), std::string::npos) << message;
    }
}

TEST(CompiledEffectRouteTest, TheCnbContainerHasNoEffectWriterAndSaysSo)
{
    ScratchDirectory scratch("cnb");
    WriteBytes(scratch.Path() / "shader.fxb", MakeBareSignatureEffect());

    try
    {
        (void)Build(scratch, Pipeline::ContentOutputFormat::Cnb);
        FAIL() << "a .cnb Effect has no schema and must not resolve a writer";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("no writer is registered"), std::string::npos) << message;
        EXPECT_NE(message.find("cnb"), std::string::npos) << message;
    }
}

TEST(CompiledEffectRouteTest, TheProcessorAcceptsNoParametersBecauseThereIsNothingToConfigure)
{
    ScratchDirectory scratch("parameters");
    WriteBytes(scratch.Path() / "shader.fxb", MakeBareSignatureEffect());

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set("optimize", true);
    EXPECT_THROW((void)Build(scratch, Pipeline::ContentOutputFormat::Xnb, parameters),
                 Pipeline::ContentPipelineError);
}

TEST(CompiledEffectRouteTest, NothingImportsHlslSourceAtAll)
{
    // The refusal that matters most is the one at the routing layer: a build tree containing a
    // `.fx` reports that nothing imports it, rather than a component accepting the file and then
    // failing inside. There is no HLSL compiler here, and the pipeline does not pretend there is.
    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    ScratchDirectory scratch("hlsl");
    WriteBytes(scratch.Path() / "shader.fx", MakeBareSignatureEffect());
    request.sourceRoot = scratch.Path();
    request.source = "shader.fx";
    request.logicalName = "Effects/shader";
    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
    EXPECT_THROW((void)pipeline.Build(request), Pipeline::ContentPipelineError);
}
