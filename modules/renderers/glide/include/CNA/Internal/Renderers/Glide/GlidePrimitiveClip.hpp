#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace CNA::Internal::Renderers::Glide
{
    /** Small positive W floor retained by segment clipping so Glide Q/STW remain representable. */
    inline constexpr float kGlideMinimumPositiveClipW = 0.00001f;

    /** A transformed fixed-function vertex before the homogeneous perspective divide. */
    struct GlideClipVertex
    {
        float clipX = 0.0f;
        float clipY = 0.0f;
        float clipZ = 0.0f;
        float clipW = 0.0f;
        float r = 255.0f;
        float g = 255.0f;
        float b = 255.0f;
        float a = 255.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct GlideClipSegment
    {
        GlideClipVertex first;
        GlideClipVertex second;
    };

    [[nodiscard]] inline GlideClipVertex InterpolateGlideClipVertex(
        const GlideClipVertex& from, const GlideClipVertex& to, float amount)
    {
        const auto lerp = [amount](float a, float b) { return a + (b - a) * amount; };
        return {
            lerp(from.clipX, to.clipX), lerp(from.clipY, to.clipY),
            lerp(from.clipZ, to.clipZ), lerp(from.clipW, to.clipW),
            lerp(from.r, to.r), lerp(from.g, to.g), lerp(from.b, to.b), lerp(from.a, to.a),
            lerp(from.u, to.u), lerp(from.v, to.v)};
    }

    [[nodiscard]] inline bool HasFiniteGlideClipCoordinates(const GlideClipVertex& vertex)
    {
        return std::isfinite(vertex.clipX) && std::isfinite(vertex.clipY) &&
               std::isfinite(vertex.clipZ) && std::isfinite(vertex.clipW) &&
               std::isfinite(vertex.r) && std::isfinite(vertex.g) &&
               std::isfinite(vertex.b) && std::isfinite(vertex.a) &&
               std::isfinite(vertex.u) && std::isfinite(vertex.v);
    }

    /** Returns whether a point belongs to D3D/XNA clip space and can be perspective divided. */
    [[nodiscard]] inline bool IsGlidePointInsideFrustum(const GlideClipVertex& vertex)
    {
        return HasFiniteGlideClipCoordinates(vertex) && vertex.clipW >= kGlideMinimumPositiveClipW &&
               vertex.clipW + vertex.clipX >= 0.0f && vertex.clipW - vertex.clipX >= 0.0f &&
               vertex.clipW + vertex.clipY >= 0.0f && vertex.clipW - vertex.clipY >= 0.0f &&
               vertex.clipZ >= 0.0f && vertex.clipW - vertex.clipZ >= 0.0f;
    }

    template <typename Distance>
    [[nodiscard]] inline std::optional<GlideClipSegment> ClipGlideSegmentToHalfSpace(
        GlideClipSegment segment, Distance distance)
    {
        const float firstDistance = distance(segment.first);
        const float secondDistance = distance(segment.second);
        const bool firstInside = firstDistance >= 0.0f;
        const bool secondInside = secondDistance >= 0.0f;
        if (!firstInside && !secondInside)
        {
            return std::nullopt;
        }
        if (firstInside && secondInside)
        {
            return segment;
        }
        const float denominator = firstDistance - secondDistance;
        if (!std::isfinite(denominator) || std::abs(denominator) <= 0.0000001f)
        {
            return std::nullopt;
        }
        const GlideClipVertex intersection = InterpolateGlideClipVertex(
            segment.first, segment.second, std::clamp(firstDistance / denominator, 0.0f, 1.0f));
        if (firstInside)
        {
            segment.second = intersection;
        }
        else
        {
            segment.first = intersection;
        }
        return segment;
    }

    /** Clips a segment to D3D/XNA homogeneous clip space before the perspective divide. */
    [[nodiscard]] inline std::optional<GlideClipSegment> ClipGlideSegmentToFrustum(
        GlideClipVertex first, GlideClipVertex second)
    {
        if (!HasFiniteGlideClipCoordinates(first) || !HasFiniteGlideClipCoordinates(second))
        {
            return std::nullopt;
        }
        std::optional<GlideClipSegment> result = GlideClipSegment{first, second};
        const auto clip = [&result](auto distance)
        {
            if (result)
            {
                result = ClipGlideSegmentToHalfSpace(*result, distance);
            }
        };
        // Keep a small positive W margin. A segment crossing the eye plane otherwise produces a
        // valid mathematical endpoint with W=0, which cannot be represented by Glide's Q/STW.
        clip([](const GlideClipVertex& vertex) { return vertex.clipW - kGlideMinimumPositiveClipW; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipW + vertex.clipX; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipW - vertex.clipX; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipW + vertex.clipY; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipW - vertex.clipY; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipZ; });
        clip([](const GlideClipVertex& vertex) { return vertex.clipW - vertex.clipZ; });
        return result;
    }

    /** Sutherland-Hodgman half-space clip of a (possibly non-triangular) polygon fan. */
    template <typename Distance>
    [[nodiscard]] inline std::vector<GlideClipVertex> ClipGlidePolygonToHalfSpace(
        const std::vector<GlideClipVertex>& input, Distance distance)
    {
        std::vector<GlideClipVertex> output;
        if (input.empty())
        {
            return output;
        }
        GlideClipVertex previous = input.back();
        float previousDistance = distance(previous);
        bool previousInside = previousDistance >= 0.0f;
        for (const GlideClipVertex& current : input)
        {
            const float currentDistance = distance(current);
            const bool currentInside = currentDistance >= 0.0f;
            if (currentInside != previousInside)
            {
                const float denominator = previousDistance - currentDistance;
                if (std::abs(denominator) > 0.0000001f)
                {
                    output.push_back(InterpolateGlideClipVertex(
                        previous, current, previousDistance / denominator));
                }
            }
            if (currentInside)
            {
                output.push_back(current);
            }
            previous = current;
            previousDistance = currentDistance;
            previousInside = currentInside;
        }
        return output;
    }

    /** Clips a polygon to D3D/XNA homogeneous clip space before the perspective divide. */
    [[nodiscard]] inline std::vector<GlideClipVertex> ClipGlidePolygonToFrustum(std::vector<GlideClipVertex> polygon)
    {
        // D3D/XNA clip space: -w <= x,y <= w and 0 <= z <= w. Clip before dividing by w: it is
        // the only safe way to split triangles that cross the eye/near/far planes. Keep the same
        // small positive-W margin as segment/point clipping: a triangle crossing the eye plane can
        // otherwise survive these six nominal planes with a vertex at or below W=0, which cannot
        // be represented by Glide's Q/STW.
        polygon = ClipGlidePolygonToHalfSpace(
            polygon, [](const GlideClipVertex& v) { return v.clipW - kGlideMinimumPositiveClipW; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipW + v.clipX; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipW - v.clipX; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipW + v.clipY; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipW - v.clipY; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipZ; });
        polygon = ClipGlidePolygonToHalfSpace(polygon, [](const GlideClipVertex& v) { return v.clipW - v.clipZ; });
        return polygon;
    }
} // namespace CNA::Internal::Renderers::Glide
