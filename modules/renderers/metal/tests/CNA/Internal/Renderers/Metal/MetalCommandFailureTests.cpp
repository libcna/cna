// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalCommandFailure.hpp"

using CNA::Internal::Renderers::Metal::MetalCommandFailureLatch;
using namespace CNA::Internal::Renderers::Metal;

TEST(MetalCommandFailure, ActiveRenderTargetReadbackSynchronizesItsExactSourceCommand)
{
    EXPECT_EQ(DescribeMetalReadbackSourcePolicy(true),
              MetalReadbackSourcePolicy::SynchronizeActiveSource);
    EXPECT_EQ(DescribeMetalReadbackSourcePolicy(false),
              MetalReadbackSourcePolicy::QueueOrderedCommittedSource);
}

TEST(MetalCommandFailure, ExactAndOlderFailuresKeepDistinctDiagnostics)
{
    EXPECT_EQ(DescribeMetalSynchronousCommandResult(true,false),
              MetalSynchronousCommandResult::Complete);
    EXPECT_EQ(DescribeMetalSynchronousCommandResult(false,false),
              MetalSynchronousCommandResult::ExactSubmissionFailed);
    EXPECT_EQ(DescribeMetalSynchronousCommandResult(true,true),
              MetalSynchronousCommandResult::OlderSubmissionFailed);
    EXPECT_EQ(DescribeMetalSynchronousCommandResult(false,true),
              MetalSynchronousCommandResult::ExactSubmissionFailed);
}

TEST(MetalCommandFailure, FailureIsLatchedUntilOneSynchronousConsumerObservesIt)
{
    MetalCommandFailureLatch latch;
    EXPECT_FALSE(latch.HasFailure());
    EXPECT_FALSE(latch.ConsumeFailure());
    latch.RecordFailure();
    EXPECT_TRUE(latch.HasFailure());
    EXPECT_TRUE(latch.ConsumeFailure());
    EXPECT_FALSE(latch.HasFailure());
    EXPECT_FALSE(latch.ConsumeFailure());
}

TEST(MetalCommandFailure, MultipleCompletionsCoalesceWithoutLosingTheFailure)
{
    MetalCommandFailureLatch latch;
    latch.RecordFailure();
    latch.RecordFailure();
    EXPECT_TRUE(latch.ConsumeFailure());
    EXPECT_FALSE(latch.ConsumeFailure());
}

// plans/plan_apple_m4.md AM4-138: the native error travels with the failure, and the first one wins.
TEST(MetalCommandFailure, TheFirstFailuresDescriptionIsReportedOnce)
{
    MetalCommandFailureLatch latch;
    latch.RecordFailure("MTLCommandBufferError 2: Caused GPU Timeout Error");
    latch.RecordFailure("MTLCommandBufferError 4: a consequence of the first");
    EXPECT_TRUE(latch.ConsumeFailure());
    EXPECT_EQ(latch.TakeFailureDetail(), "MTLCommandBufferError 2: Caused GPU Timeout Error");
    EXPECT_EQ(latch.TakeFailureDetail(), "");
    latch.RecordFailure();
    EXPECT_TRUE(latch.ConsumeFailure());
    EXPECT_EQ(latch.TakeFailureDetail(), "") << "a failure recorded without a description has none";
}
