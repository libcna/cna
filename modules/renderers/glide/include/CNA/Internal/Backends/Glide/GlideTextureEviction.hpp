#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    /** A resident-texture candidate as seen by the LRU eviction selector. */
    struct GlideResidentTextureView
    {
        const void* identity = nullptr;
        bool isResident = false;
        std::uint64_t lastUsedCounter = 0;
    };

    /**
     * Picks the least-recently-used eviction victim out of a candidate list, or nullptr if none
     * is eligible.
     *
     * `requester` (the texture currently asking for TMU0 memory) is always excluded, even if it
     * happens to already be resident (e.g. only partially rebuilt after its own prior eviction) --
     * a texture must never evict tiles it is in the middle of allocating for itself. A
     * non-resident candidate (already evicted, nothing left to free) is also skipped. Ties in
     * `lastUsedCounter` keep the first candidate encountered, so selection is a pure function of
     * the input order and counters -- deterministic and reproducible for the same inputs.
     */
    [[nodiscard]] inline const void* SelectGlideEvictionVictim(
        const std::vector<GlideResidentTextureView>& candidates, const void* requester)
    {
        const void* victim = nullptr;
        std::uint64_t oldestUse = 0;
        bool haveVictim = false;
        for (const GlideResidentTextureView& candidate : candidates)
        {
            if (candidate.identity == requester || !candidate.isResident)
            {
                continue;
            }
            if (!haveVictim || candidate.lastUsedCounter < oldestUse)
            {
                victim = candidate.identity;
                oldestUse = candidate.lastUsedCounter;
                haveVictim = true;
            }
        }
        return victim;
    }
} // namespace CNA::Internal::Backends::Glide
