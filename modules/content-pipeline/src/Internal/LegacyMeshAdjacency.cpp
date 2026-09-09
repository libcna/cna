// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.
//
// `D3DXOptimizeFaces` took an index buffer and no adjacency; DirectX::OptimizeFaces takes the
// adjacency as an argument. The rule below is CNA's own measurement of what the legacy entry point
// worked from, made against the genuine D3DX9 redistributable and genuine XNA 4.0 -- 1,210 probes,
// 594 of them designed to put three and four faces on one edge, with no disagreement. Nothing here
// is derived from any Microsoft implementation.

#include "CNA/Internal/OptimizeFaces.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace CNA::Internal
{
    std::vector<std::uint32_t> BuildLegacyMeshAdjacency(const std::vector<std::uint32_t>& indices)
    {
        const std::size_t faceCount = indices.size() / 3;
        std::vector<std::uint32_t> adjacency(faceCount * 3, OptimizeFacesUnused);
        if (faceCount == 0)
        {
            return adjacency;
        }

        // Every corner that carries an undirected edge, gathered in input order. A face's own
        // three corners are visited in edge order, so a degenerate face that carries the same edge
        // twice appears twice, which is what keeps a pairing from consuming it against itself.
        using Slot = std::pair<std::uint32_t, std::uint32_t>;      // face, edge
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<Slot>> onEdge;
        for (std::uint32_t face = 0; face < faceCount; ++face)
        {
            for (std::uint32_t edge = 0; edge < 3; ++edge)
            {
                const std::uint32_t a = indices[face * 3 + edge];
                const std::uint32_t b = indices[face * 3 + ((edge + 1) % 3)];
                onEdge[std::make_pair(std::min(a, b), std::max(a, b))].emplace_back(face, edge);
            }
        }

        for (const auto& entry : onEdge)
        {
            const std::vector<Slot>& slots = entry.second;
            std::vector<bool> paired(slots.size(), false);
            for (std::size_t p = 0; p < slots.size(); ++p)
            {
                if (paired[p])
                {
                    continue;
                }
                const std::uint32_t face = slots[p].first;
                const std::uint32_t edge = slots[p].second;
                const std::uint32_t a = indices[face * 3 + edge];
                const std::uint32_t b = indices[face * 3 + ((edge + 1) % 3)];
                for (std::size_t q = slots.size(); q-- > 0;)
                {
                    if (q == p || paired[q])
                    {
                        continue;
                    }
                    const std::uint32_t other = slots[q].first;
                    const std::uint32_t otherEdge = slots[q].second;
                    if (other == face)
                    {
                        continue;
                    }
                    if (indices[other * 3 + otherEdge] == b
                        && indices[other * 3 + ((otherEdge + 1) % 3)] == a)
                    {
                        paired[p] = true;
                        paired[q] = true;
                        adjacency[face * 3 + edge] = other;
                        adjacency[other * 3 + otherEdge] = face;
                        break;
                    }
                }
            }
        }
        return adjacency;
    }
}
