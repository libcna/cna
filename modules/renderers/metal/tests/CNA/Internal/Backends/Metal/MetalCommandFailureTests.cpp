// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Metal/MetalCommandFailure.hpp"

using CNA::Internal::Backends::Metal::MetalCommandFailureLatch;
using namespace CNA::Internal::Backends::Metal;

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
