// SPDX-License-Identifier: MS-PL
// GDI-076: genuine 32-bit size_t coverage for CPU texture layout arithmetic.

#include "CNA/Internal/Renderers/Software/SoftwareTextureAllocation.hpp"

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
                 "texture arithmetic harness is a genuine 32-bit binary");

    const SoftwareTextureAllocationLayout normal =
        PlanSoftwareTextureAllocation({2048, 2048, 1});
    ok &= Expect(normal.IsValid() && normal.pixelCount == 4194304u &&
                     normal.baseColorBytes == 16777216u && normal.mipBytes == 0u &&
                     normal.totalBytes == 16777216u,
                 "32-bit planner accepts a budgeted single-level texture exactly");

    const SoftwareTextureAllocationLayout mipmapped =
        PlanSoftwareTextureAllocation({1024, 1024, 11});
    ok &= Expect(mipmapped.IsValid() && mipmapped.baseColorBytes == 4194304u &&
                     mipmapped.mipBytes == 1398100u && mipmapped.totalBytes == 5592404u,
                 "32-bit planner sums a full declared mip chain exactly");

    const SoftwareTextureAllocationLayout overflow =
        PlanSoftwareTextureAllocation(
            {SoftwareFramebufferMaxDimension, SoftwareFramebufferMaxDimension, 1});
    ok &= Expect(overflow.error == SoftwareTextureAllocationError::ByteBudgetExceeded,
                 "32-bit planner rejects a maximum single level via the byte budget");

    const SoftwareTextureAllocationLayout overBudget =
        PlanSoftwareTextureAllocation({11586, 11586, 1});
    ok &= Expect(overBudget.error == SoftwareTextureAllocationError::ByteBudgetExceeded,
                 "32-bit planner distinguishes a safe product above the byte budget");
    ok &= Expect(PlanSoftwareTextureAllocation({11500, 11500, 1}).IsValid() &&
                     PlanSoftwareTextureAllocation({11500, 11500, 2}).error ==
                         SoftwareTextureAllocationError::ByteBudgetExceeded,
                 "32-bit planner includes declared mip storage in the resource budget");

    ok &= Expect(PlanSoftwareTextureAllocation({0, 1, 1}).error ==
                     SoftwareTextureAllocationError::NonPositiveDimension &&
                     PlanSoftwareTextureAllocation({1, -1, 1}).error ==
                     SoftwareTextureAllocationError::NonPositiveDimension,
                 "32-bit planner rejects zero/negative dimensions before unsigned conversion");
    return ok ? 0 : 1;
}
