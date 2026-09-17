// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0006: the verdicts that decide whether a Direct3D debug-layer
// message fails its test. Pure logic, so it runs on every platform, not only where a debug layer exists.

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include <string>

#include "D3DDebugLayerPolicy.hpp"

namespace
{
    using namespace CNA::Testing::D3DDebugLayer;

    constexpr const char* kAllowedTest =
        "DrawRouteValidation.EveryVertexElementFormatIsBoundOrRefusedByName";

    // What D3DDebugLayerListener does at the end of a test, minus the file report.
    void FailOnFatal(const Gate::Outcome& outcome)
    {
        if (outcome.fatalCount != 0)
            ADD_FAILURE() << DescribeFatal(outcome);
    }
}

TEST(D3DDebugLayerPolicyTest, CorruptionAndErrorAreFatalEvenForAnAllowlistedId)
{
    for (const Api api : {Api::Direct3D11, Api::Direct3D12})
    {
        EXPECT_EQ(Classify(api, Corruption, 921, "Any.Test"), Verdict::Fatal);
        EXPECT_EQ(Classify(api, Error, 1002, "Any.Test"), Verdict::Fatal);
    }
    // 245 is allowlisted as a WARNING in this very test; as an error it still fails.
    EXPECT_EQ(Classify(Api::Direct3D12, Error, 245, kAllowedTest), Verdict::Fatal);
}

TEST(D3DDebugLayerPolicyTest, TheAllowlistHoldsOnlyWarningsWithAReasonAndAScope)
{
    for (const AllowedWarning& entry : kAllowedWarnings)
    {
        SCOPED_TRACE(entry.idName);
        EXPECT_GT(std::string(entry.reason).size(), 40u) << "an allowlisted warning needs its reason";
        ASSERT_FALSE(std::string(entry.testScope).empty()) << "an allowlist entry must name its scope";
        const std::string scope = entry.testScope;
        const std::string testInScope = scope.back() == '.' ? scope + "AnyTest" : scope;
        EXPECT_EQ(Classify(entry.api, Warning, entry.id, testInScope), Verdict::Allowed);
        EXPECT_EQ(Classify(entry.api, Error, entry.id, testInScope), Verdict::Fatal);
    }
}

TEST(D3DDebugLayerPolicyTest, AWarningIsAllowedOnlyForItsApiIdAndTest)
{
    EXPECT_EQ(Classify(Api::Direct3D12, Warning, 245, kAllowedTest), Verdict::Allowed);
    EXPECT_EQ(Classify(Api::Direct3D12, Warning, 245, "DrawRouteValidation.SomeOtherTest"), Verdict::Fatal);
    EXPECT_EQ(Classify(Api::Direct3D12, Warning, 245, ""), Verdict::Fatal) << "outside any test";
    EXPECT_EQ(Classify(Api::Direct3D11, Warning, 245, kAllowedTest), Verdict::Fatal)
        << "a D3D12 id means something else to the D3D11 layer";
    EXPECT_EQ(Classify(Api::Direct3D12, Warning, 680, kAllowedTest), Verdict::Fatal);
}

TEST(D3DDebugLayerPolicyTest, SuiteScopesMatchWholeSuitesOnly)
{
    EXPECT_TRUE(InScope("Suite.", "Suite.Test"));
    EXPECT_FALSE(InScope("Suite.", "SuiteTwo.Test"));
    EXPECT_TRUE(InScope("Suite.Test", "Suite.Test"));
    EXPECT_FALSE(InScope("Suite.Test", "Suite.TestTwo"));
}

TEST(D3DDebugLayerPolicyTest, InfoAndMessageCarryNoVerdictAndUnknownSeveritiesFail)
{
    EXPECT_EQ(Classify(Api::Direct3D11, Info, 1, "Any.Test"), Verdict::Ignored);
    EXPECT_EQ(Classify(Api::Direct3D12, Message, 1016, "Any.Test"), Verdict::Ignored);
    EXPECT_EQ(Classify(Api::Direct3D12, 7, 1, "Any.Test"), Verdict::Fatal);
    EXPECT_EQ(Classify(Api::Direct3D12, -1, 1, "Any.Test"), Verdict::Fatal);
}

TEST(D3DDebugLayerPolicyTest, TheGateAttributesMessagesToTheRunningTest)
{
    Gate gate;
    gate.Observe(Api::Direct3D12, Error, 1, "before any test");

    gate.BeginTest(kAllowedTest);
    gate.Observe(Api::Direct3D12, Warning, 245, "type mismatch, well defined");
    gate.Observe(Api::Direct3D12, Info, 3, "noise");
    const Gate::Outcome clean = gate.EndTest();
    EXPECT_EQ(clean.testName, kAllowedTest);
    EXPECT_EQ(clean.fatalCount, 0u);
    EXPECT_EQ(clean.allowedCount, 1u);

    gate.BeginTest("Suite.Broken");
    gate.Observe(Api::Direct3D11, Error, 11, "invalid argument");
    gate.Observe(Api::Direct3D12, Warning, 245, "not investigated here");
    const Gate::Outcome broken = gate.EndTest();
    EXPECT_EQ(broken.fatalCount, 2u);
    ASSERT_EQ(broken.fatal.size(), 2u);
    EXPECT_EQ(broken.fatal[0], "D3D11 ERROR id 11: invalid argument");
    EXPECT_EQ(broken.fatal[1], "D3D12 WARNING id 245: not investigated here");

    gate.Observe(Api::Direct3D12, Corruption, 921, "after the test");
    const Gate::Outcome outside = gate.TakeOutside();
    EXPECT_TRUE(outside.testName.empty());
    EXPECT_EQ(outside.fatalCount, 2u);
    EXPECT_EQ(gate.TakeOutside().fatalCount, 0u);
}

TEST(D3DDebugLayerPolicyTest, TheGateKeepsCountingPastItsRetainedMessages)
{
    Gate gate;
    gate.BeginTest("Suite.Flood");
    for (std::size_t i = 0; i < Gate::kRetainedFatal + 10; ++i)
        gate.Observe(Api::Direct3D12, Error, 1, "flood");
    const Gate::Outcome outcome = gate.EndTest();
    EXPECT_EQ(outcome.fatalCount, Gate::kRetainedFatal + 10);
    EXPECT_EQ(outcome.fatal.size(), Gate::kRetainedFatal);
}

TEST(D3DDebugLayerPolicyTest, AFatalOutcomeFailsTheTestAndAnAllowedOneDoesNot)
{
    Gate gate;
    gate.BeginTest("Suite.Broken");
    gate.Observe(Api::Direct3D12, Error, 1002, "descriptor data changed");
    // EXPECT_NONFATAL_FAILURE's statement may not name a local variable.
    static Gate::Outcome broken;
    broken = gate.EndTest();
    EXPECT_NONFATAL_FAILURE(FailOnFatal(broken), "D3D12 ERROR id 1002: descriptor data changed");

    gate.BeginTest(kAllowedTest);
    gate.Observe(Api::Direct3D12, Warning, 245, "well defined");
    FailOnFatal(gate.EndTest());
}
