// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUOcclusionResults.hpp"
#include <gtest/gtest.h>

using CNA::Internal::Renderers::WebGPU::WebGPUOcclusionResults;

namespace
{
    void Batch(WebGPUOcclusionResults& results, std::uint64_t samples)
    {
        results.QueueDraw();
        results.RecordBatch();
        results.RecordDraw();
        results.Resolve(samples);
    }
}

// Oracle: Refresh SetRenderTargets IL_0000-0557 and Present IL_0013-00ce
// do not end/restart queries. Counts below are literal raster areas, not CNA output.
TEST(WebGPUOcclusionResultsTest, ThreeTargetBatchesAccumulateUntilEnd)
{
    WebGPUOcclusionResults results;
    results.Begin();
    Batch(results, 2048); // Half of a 64x64 viewport: 32 * 64.
    EXPECT_FALSE(results.IsComplete());
    Batch(results, 512); // A 32x16 target.
    EXPECT_FALSE(results.IsComplete());
    Batch(results, 2048);
    EXPECT_FALSE(results.IsComplete());
    results.End();
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 4608); // 2048 + 512 + 2048.
    EXPECT_EQ(results.PixelCount(true), 4608); // Reading must not consume the result.
}

TEST(WebGPUOcclusionResultsTest, ZeroSampleMiddleBatchPreservesBothSides)
{
    WebGPUOcclusionResults results;
    results.Begin();
    Batch(results, 2048);
    Batch(results, 0); // A pass whose draws fail depth/stencil.
    Batch(results, 512);
    results.End();
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 2560); // 2048 + 0 + 512.
}

TEST(WebGPUOcclusionResultsTest, EndWaitsForBothQueuedAndRecordedDraws)
{
    WebGPUOcclusionResults results;
    results.Begin();
    Batch(results, 2048); // Completed before Present or a target switch.
    results.QueueDraw();
    results.End();
    EXPECT_FALSE(results.IsComplete()); // Final pass still deferred.
    results.RecordBatch();
    results.RecordDraw();
    EXPECT_FALSE(results.IsComplete()); // Submission is not a successful readback.
    results.Resolve(2048);
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 4096); // 2048 + 2048.
}

TEST(WebGPUOcclusionResultsTest, EarlierReadbackCannotCompleteLaterQueuedWork)
{
    WebGPUOcclusionResults results;
    results.Begin();
    results.QueueDraw();
    results.RecordBatch();
    results.RecordDraw();
    results.QueueDraw(); // Next target's draw queues before the earlier batch is mapped.
    results.End();
    results.Resolve(512);
    EXPECT_FALSE(results.IsComplete());
    results.RecordBatch();
    results.RecordDraw();
    EXPECT_FALSE(results.IsComplete());
    results.Resolve(2048);
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 2560); // 512 + 2048.
}

TEST(WebGPUOcclusionResultsTest, EveryDrawInOneBatchMustLeaveTheDeferredQueue)
{
    WebGPUOcclusionResults results;
    results.Begin();
    results.QueueDraw();
    results.QueueDraw();
    results.End();
    results.RecordBatch();
    results.RecordDraw();
    results.RecordDraw();
    EXPECT_FALSE(results.IsComplete());
    results.Resolve(512); // Two coalesced draws, each covering 16 * 16 samples.
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 512);
}

TEST(WebGPUOcclusionResultsTest, MissingEarlierReadbackCannotBeHiddenByALaterSuccess)
{
    // Every segment must be available: one later successful map cannot replace a failed map.
    WebGPUOcclusionResults results;
    results.Begin();
    results.QueueDraw();
    results.RecordBatch();
    results.RecordDraw(); // No Resolve: the first batch's buffer map failed.
    Batch(results, 512);
    results.End();
    EXPECT_FALSE(results.IsComplete());
    results.Begin(); // Abandoning the failed interval permits a fresh empty one.
    results.End();
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 0);
}

TEST(WebGPUOcclusionResultsTest, BooleanReportingOrsAllBatches)
{
    // Metal visibility results are boolean; OR, not addition, preserves the any-sample contract.
    WebGPUOcclusionResults results;
    results.Begin();
    Batch(results, 1);
    Batch(results, 1);
    Batch(results, 0);
    results.End();
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(false), 1);
    results.Begin();
    Batch(results, 0);
    results.End();
    EXPECT_EQ(results.PixelCount(false), 0);
}

TEST(WebGPUOcclusionResultsTest, PreciseSumWrapsDwordWithoutLosingVisibility)
{
    // Refresh get_IsComplete IL_001e-0028 reads a DWORD, IL_003c-003e stores its Int32 bits.
    WebGPUOcclusionResults results;
    results.Begin();
    Batch(results, 0xffffffffu);
    Batch(results, 1);
    results.End();
    EXPECT_TRUE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 0); // (2^32 - 1) + 1 modulo 2^32.
    EXPECT_EQ(results.PixelCount(false), 1); // Samples were visible before the sum wrapped.
}

TEST(WebGPUOcclusionResultsTest, BeginResetsTallyAndEmptyIntervalsCompleteOnlyAfterEnd)
{
    // Refresh Begin IL_0046-005d resets the run; an empty interval has no samples to await.
    WebGPUOcclusionResults results;
    EXPECT_FALSE(results.IsComplete());
    results.Begin();
    Batch(results, 512);
    results.End();
    ASSERT_TRUE(results.IsComplete());
    results.Begin();
    EXPECT_FALSE(results.IsComplete());
    EXPECT_EQ(results.PixelCount(true), 0);
    EXPECT_EQ(results.PixelCount(false), 0);
    results.End();
    EXPECT_TRUE(results.IsComplete());
}
