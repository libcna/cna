// SPDX-License-Identifier: MS-PL
// Genuine 32-bit size_t coverage for the Software renderer's CPU framebuffer layout arithmetic.

#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"

#include <cstddef>
#include <cstdio>

using namespace CNA::Internal::Renderers::Software;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }
}

int main()
{
    bool ok = true;
    ok &= Expect(sizeof(std::size_t) == 4,
                 "framebuffer arithmetic harness is a genuine 32-bit binary");

    // SOFTWARE-110 made 4x stencil sample-correct: four 8-bit stencil samples per pixel join the
    // four RGBA8 colour samples. This harness was not updated with it -- its CI job had stopped
    // compiling -- and still expected the earlier 174,182,400-byte total (plans/plan_apple_m4.md
    // AM4-239).
    const SoftwareFramebufferAllocationLayout normal =
        PlanSoftwareFramebufferAllocation({3840, 2160, false, true, 4});
    ok &= Expect(normal.IsValid() && normal.pixelCount == 8294400u &&
                     normal.colorBytes == 33177600u &&
                     normal.stencilBytes == 8294400u &&
                     normal.multiSampleBytes == 132710400u &&
                     normal.multiSampleStencilBytes == 33177600u &&
                     normal.totalBytes == 207360000u,
                 "32-bit planner accepts a budgeted 4K 4x framebuffer without depth exactly");

    const SoftwareFramebufferAllocationLayout overflow =
        PlanSoftwareFramebufferAllocation(
            {SoftwareFramebufferMaxDimension, SoftwareFramebufferMaxDimension,
             false, true, 4});
    ok &= Expect(overflow.error == SoftwareFramebufferAllocationError::ArithmeticOverflow,
                 "32-bit planner catches sample-plane multiplication before wraparound");

    const SoftwareFramebufferAllocationLayout overBudget =
        PlanSoftwareFramebufferAllocation({11000, 11000, false, true, 0});
    ok &= Expect(overBudget.error == SoftwareFramebufferAllocationError::ByteBudgetExceeded,
                 "32-bit planner distinguishes a safe product above the byte budget");
    ok &= Expect(PlanSoftwareFramebufferAllocation(
                     {10000, 10000, false, true, 0}).IsValid() &&
                     PlanSoftwareFramebufferAllocation(
                         {10000, 10000, false, true, 0, true}).error ==
                         SoftwareFramebufferAllocationError::ByteBudgetExceeded,
                 "32-bit planner includes generated mip storage in the resource budget");
    return ok ? 0 : 1;
}
