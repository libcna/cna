// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-85: the writer's ceilings are only ceilings if they are
// validated once, where they enter the system.

#include "CNA/Internal/Xnb/XnbWriteLimits.hpp"

#include <algorithm>
#include <string>

#include "CNA/Internal/Xnb/XnbByteWriter.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        void RequirePositive(const std::int32_t value, const char* field)
        {
            if (value <= 0)
            {
                throw XnbWriteException(
                    std::string("XNB write limit '") + field + "' is " + std::to_string(value) +
                    "; every limit must be positive. A non-positive limit does not mean 'small' "
                    "-- the checks that consult it widen it to an unsigned type, so it means "
                    "'unbounded'.");
            }
        }
    }

    void ValidateXnbWriteLimits(const XnbWriteLimits& limits)
    {
        RequirePositive(limits.maxFileSize, "maxFileSize");
        RequirePositive(limits.maxPayloadSize, "maxPayloadSize");
        RequirePositive(limits.maxStringBytes, "maxStringBytes");
        RequirePositive(limits.maxTypeWriterCount, "maxTypeWriterCount");
        RequirePositive(limits.maxSharedResourceCount, "maxSharedResourceCount");
        RequirePositive(limits.maxCollectionElementCount, "maxCollectionElementCount");
        RequirePositive(limits.maxObjectNestingDepth, "maxObjectNestingDepth");

        constexpr std::int32_t kHeaderBytes = 10;
        if (limits.maxFileSize <= kHeaderBytes)
        {
            throw XnbWriteException(
                "XNB write limit 'maxFileSize' is " + std::to_string(limits.maxFileSize) +
                "; a file smaller than its own ten-byte header cannot exist.");
        }
    }

    std::int32_t EffectiveXnbPayloadCeiling(const XnbWriteLimits& limits)
    {
        constexpr std::int32_t kHeaderBytes = 10;
        return std::min(limits.maxPayloadSize, limits.maxFileSize - kHeaderBytes);
    }
}
