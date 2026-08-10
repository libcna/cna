#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    /** Native GR_REFRESH_60Hz token (Glide 3.0 Reference). */
    inline constexpr std::int32_t kGlideRefresh60Hz = 0x0;

    /** One resolution+refresh candidate as returned by grQueryResolutions. */
    struct GlideResolutionCandidate
    {
        std::int32_t resolution = 0;
        std::int32_t refresh = 0;
    };

    /** A historical Glide display mode's fixed pixel dimensions. */
    struct GlideDisplayMode
    {
        std::int32_t resolution = 0;
        int width = 0;
        int height = 0;
    };

    /** Returns the fixed pixel dimensions for a historical GR_RESOLUTION_* token, {0,0,0} if unrecognized. */
    [[nodiscard]] inline GlideDisplayMode KnownGlideDisplayMode(std::int32_t resolution)
    {
        switch (resolution)
        {
            // These are the complete historical GR_RESOLUTION_* values from Glide 3.x.
            case 0x0: return GlideDisplayMode{resolution, 320, 200};
            case 0x1: return GlideDisplayMode{resolution, 320, 240};
            case 0x2: return GlideDisplayMode{resolution, 400, 256};
            case 0x3: return GlideDisplayMode{resolution, 512, 384};
            case 0x4: return GlideDisplayMode{resolution, 640, 200};
            case 0x5: return GlideDisplayMode{resolution, 640, 350};
            case 0x6: return GlideDisplayMode{resolution, 640, 400};
            case 0x7: return GlideDisplayMode{resolution, 640, 480};
            case 0x8: return GlideDisplayMode{resolution, 800, 600};
            case 0x9: return GlideDisplayMode{resolution, 960, 720};
            case 0xa: return GlideDisplayMode{resolution, 856, 480};
            case 0xb: return GlideDisplayMode{resolution, 512, 256};
            case 0xc: return GlideDisplayMode{resolution, 1024, 768};
            case 0xd: return GlideDisplayMode{resolution, 1280, 1024};
            case 0xe: return GlideDisplayMode{resolution, 1600, 1200};
            case 0xf: return GlideDisplayMode{resolution, 400, 300};
            case 0x10: return GlideDisplayMode{resolution, 1152, 864};
            case 0x11: return GlideDisplayMode{resolution, 1280, 960};
            case 0x12: return GlideDisplayMode{resolution, 1600, 1024};
            case 0x13: return GlideDisplayMode{resolution, 1792, 1344};
            case 0x14: return GlideDisplayMode{resolution, 1856, 1392};
            case 0x15: return GlideDisplayMode{resolution, 1920, 1440};
            case 0x16: return GlideDisplayMode{resolution, 2048, 1536};
            case 0x17: return GlideDisplayMode{resolution, 2048, 2048};
            default: return GlideDisplayMode{0, 0, 0};
        }
    }

    /** The winning resolution/refresh pair chosen out of a grQueryResolutions candidate list. */
    struct GlideSelectedDisplayMode
    {
        GlideDisplayMode mode;
        std::int32_t refresh = 0;
    };

    /**
     * Picks the smallest historical Glide resolution that still contains the requested virtual
     * framebuffer, paired with the exact refresh token that candidate was actually reported at.
     *
     * grQueryResolutions is queried with GR_QUERY_ANY resolution/refresh, so the same resolution
     * token can legitimately appear multiple times at different refresh rates, and the runtime may
     * not offer 60 Hz at all for the chosen dimensions. Selecting dimensions and then opening a
     * hardcoded refresh that was never actually validated against that specific candidate can
     * silently combine a resolution from one candidate with a refresh from another. Smaller-area
     * candidates always win outright; among candidates tied on area (including the same resolution
     * token repeated at another refresh rate), 60 Hz is preferred deterministically if any tied
     * candidate offers it, otherwise the first tied candidate encountered is kept.
     */
    [[nodiscard]] inline std::optional<GlideSelectedDisplayMode> SelectGlideDisplayMode(
        const std::vector<GlideResolutionCandidate>& candidates, int virtualWidth, int virtualHeight)
    {
        GlideDisplayMode best{};
        std::int32_t selectedRefresh = 0;
        bool haveCandidate = false;
        for (const GlideResolutionCandidate& candidate : candidates)
        {
            const GlideDisplayMode known = KnownGlideDisplayMode(candidate.resolution);
            if (known.width < virtualWidth || known.height < virtualHeight)
            {
                continue;
            }
            const auto area = static_cast<std::int64_t>(known.width) * known.height;
            const auto bestArea = static_cast<std::int64_t>(best.width) * best.height;
            if (!haveCandidate || area < bestArea ||
                (area == bestArea && selectedRefresh != kGlideRefresh60Hz && candidate.refresh == kGlideRefresh60Hz))
            {
                best = known;
                selectedRefresh = candidate.refresh;
                haveCandidate = true;
            }
        }
        if (!haveCandidate)
        {
            return std::nullopt;
        }
        return GlideSelectedDisplayMode{best, selectedRefresh};
    }
} // namespace CNA::Internal::Backends::Glide
