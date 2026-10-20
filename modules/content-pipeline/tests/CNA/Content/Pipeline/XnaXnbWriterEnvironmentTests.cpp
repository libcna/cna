// SPDX-License-Identifier: MS-PL
// ISSUE-111: RegisterXnaXnbOutput takes platform/profile from the supplied build environment.
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"

namespace
{
    namespace P = CNA::Content::Pipeline;
    namespace X = Microsoft::Xna::Framework::Content::Pipeline;
    namespace C = X::Serialization::Compiler;
    namespace Xnb = CNA::Internal::Xnb;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;

    struct EnvironmentValue
    {
        static constexpr std::string_view XnaTypeName = "CnaTests.EnvironmentValue";
        std::int32_t number = 63;
    };
    struct Target
    {
        P::ContentTargetPlatform environment;
        X::TargetPlatform writer;
        Xnb::XnbTargetPlatform container;
        char header;
    };
    constexpr std::array targets{
        Target{P::ContentTargetPlatform::Windows, X::TargetPlatform::Windows, Xnb::XnbTargetPlatform::Windows, 'w'},
        Target{P::ContentTargetPlatform::Xbox360, X::TargetPlatform::Xbox360, Xnb::XnbTargetPlatform::Xbox360, 'x'},
        Target{P::ContentTargetPlatform::WindowsPhone, X::TargetPlatform::WindowsPhone, Xnb::XnbTargetPlatform::WindowsPhone, 'm'}};
    constexpr std::array profiles{GraphicsProfile::Reach, GraphicsProfile::HiDef};

    class EnvironmentWriter final : public C::ContentTypeWriter<EnvironmentValue>
    {
    public:
        X::TargetPlatform platform = X::TargetPlatform::Windows;
        GraphicsProfile profile = GraphicsProfile::Reach;
        std::int32_t writtenValue = 0;
        std::string GetRuntimeReader(X::TargetPlatform value) const override
        { return "CnaTests.EnvironmentReader." + std::to_string(static_cast<int>(value)); }
    protected:
        void Write(C::ContentWriter& output, const EnvironmentValue& value) override
        {
            platform = output.getTargetPlatformProperty();
            profile = output.getTargetProfileProperty();
            writtenValue = value.number;
            output.Write(value.number);
        }
    };

    struct Harness
    {
        std::shared_ptr<C::ContentCompiler> compiler = std::make_shared<C::ContentCompiler>();
        std::shared_ptr<EnvironmentWriter> observed = compiler->AddTypeWriter<EnvironmentWriter>();
        P::ContentPipelineRegistry registry;
        std::shared_ptr<const P::ContentTypeWriter> writer;
        explicit Harness(const Xnb::XnbFileOptions& options)
        {
            P::RegisterXnaXnbOutput(registry, compiler, options);
            writer = registry.ResolveWriter(std::string(EnvironmentValue::XnaTypeName), {}, P::ContentOutputFormat::Xnb);
        }
        P::ContentValue Value() const
        { return P::ContentValue::Create(std::string(EnvironmentValue::XnaTypeName), EnvironmentValue{}); }
    };

    void ExpectTarget(const P::ContentWriteResult& result, const EnvironmentWriter& writer,
                      const Target& target, GraphicsProfile profile)
    {
        ASSERT_GT(result.bytes.size(), 6u);
        EXPECT_EQ(result.bytes[3], target.header);
        EXPECT_EQ((static_cast<unsigned char>(result.bytes[5]) & 1u) != 0, profile == GraphicsProfile::HiDef);
        EXPECT_EQ(writer.platform, target.writer);
        EXPECT_EQ(writer.profile, profile);
        EXPECT_EQ(writer.writtenValue, 63);
        EXPECT_EQ(result.rootReaderName, writer.GetRuntimeReader(target.writer));
    }
}

TEST(XnaXnbWriterEnvironmentTest, ExplicitEnvironmentControlsEveryPlatformAndProfilePair)
{
    Harness harness(Xnb::XnbFileOptions{});
    for (const auto& target : targets)
    {
        for (const auto profile : profiles)
        {
            P::ContentBuildEnvironment environment;
            environment.targetPlatform = target.environment;
            environment.targetProfile = profile;
            const auto result = harness.writer->Write(harness.Value(), "environment", environment);
            ExpectTarget(result, *harness.observed, target, profile);
        }
    }
}

TEST(XnaXnbWriterEnvironmentTest, EnvironmentOverridesNonDefaultRegistrationButPreservesCompression)
{
    Xnb::XnbFileOptions options;
    options.platform = Xnb::XnbTargetPlatform::Xbox360;
    options.graphicsProfile = Xnb::XnbGraphicsProfile::HiDef;
    options.compression = Xnb::XnbOutputCompression::Lzx;
    Harness harness(options);
    const auto result = harness.writer->Write(harness.Value(), "compressed", P::ContentBuildEnvironment{});
    ExpectTarget(result, *harness.observed, targets[0], GraphicsProfile::Reach);
    ASSERT_GT(result.bytes.size(), 6u);
    EXPECT_NE(static_cast<unsigned char>(result.bytes[5]) & 0x80u, 0u);
}

TEST(XnaXnbWriterEnvironmentTest, NoEnvironmentOverloadPreservesRegisteredTargets)
{
    for (const auto& target : targets)
    {
        for (const auto profile : profiles)
        {
            Xnb::XnbFileOptions options;
            options.platform = target.container;
            options.graphicsProfile = profile == GraphicsProfile::HiDef ? Xnb::XnbGraphicsProfile::HiDef : Xnb::XnbGraphicsProfile::Reach;
            Harness harness(options);
            const auto result = harness.writer->Write(harness.Value(), "registered");
            ExpectTarget(result, *harness.observed, target, profile);
        }
    }
}

TEST(XnaXnbWriterEnvironmentTest, CorrectedOutputInvalidatesLegacyWriterCacheIdentity)
{
    const Xnb::XnbFileOptions options;
    Harness harness(options);
    const auto identity = harness.writer->Identity();
    // Component versions participate in build fingerprints. Old cached Windows/Reach
    // output must not remain a cache hit after environment propagation is corrected.
    EXPECT_EQ(identity.version, "2;" + P::XnbOutputOptionsDigest(options));
    P::ContentBuildEnvironment environment;
    environment.targetPlatform = P::ContentTargetPlatform::WindowsPhone;
    environment.targetProfile = GraphicsProfile::HiDef;
    (void)harness.writer->Write(harness.Value(), "identity", environment);
    EXPECT_EQ(harness.writer->Identity(), identity);
}
