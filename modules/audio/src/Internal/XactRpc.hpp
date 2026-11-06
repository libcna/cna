// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Audio/XactTypes.hpp"
#include <cmath>
namespace CNA::Internal::Audio
{
inline float EvaluateRpcCurve(const CNA::Internal::Audio::XgsRpc &rpc, float var)
{
    if (rpc.points.empty())
        return 0.0f;
    if (var <= rpc.points.front().x)
        return rpc.points.front().y;
    if (var >= rpc.points.back().x)
        return rpc.points.back().y;

    float result = 0.0f;
    for (std::size_t i = 0; i + 1 < rpc.points.size(); ++i)
    {
        result = rpc.points[i].y;
        if (var >= rpc.points[i].x && var <= rpc.points[i + 1].x)
        {
            const float maxX = rpc.points[i + 1].x - rpc.points[i].x;
            const float maxY = rpc.points[i + 1].y - rpc.points[i].y;
            const float t = (maxX != 0.0f) ? (var - rpc.points[i].x) / maxX : 0.0f;

            switch (rpc.points[i].type)
            {
            case 1: // FAST
                result += maxY * (1.0f - std::pow(1.0f - std::pow(t, 1.0f / 1.5f), 1.5f));
                break;
            case 2: // SLOW
                result += maxY * (1.0f - std::pow(1.0f - std::pow(t, 1.5f), 1.0f / 1.5f));
                break;
            case 3: // SINCOS
                if (maxY > 0.0f)
                    result += maxY * (1.0f - std::pow(1.0f - std::sqrt(t), 2.0f));
                else
                    result += maxY * (1.0f - std::sqrt(1.0f - std::pow(t, 2.0f)));
                break;
            default: // 0 == LINEAR
                result += maxY * t;
                break;
            }
            break;
        }
    }
    return result;
}
} // namespace CNA::Internal::Audio
