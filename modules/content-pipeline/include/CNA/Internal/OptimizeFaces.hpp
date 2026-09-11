// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Internal
{
    /** @brief The index value that marks a corner no vertex is behind. */
    inline constexpr std::uint32_t OptimizeFacesUnused = 0xFFFFFFFFu;

    /** @brief The simulated vertex cache the legacy D3DX9 entry point used. */
    inline constexpr std::uint32_t OptimizeFacesVertexCacheDefault = 12u;

    /** @brief The restart threshold the legacy D3DX9 entry point used. */
    inline constexpr std::uint32_t OptimizeFacesRestartDefault = 7u;

    /**
     * @brief Builds the face adjacency the legacy `D3DXOptimizeFaces` worked from.
     *
     * An undirected edge holds the faces that carry it, in input order. Walking those in input
     * order, a face that is not yet paired takes the **last** still-unpaired face on that edge
     * whose own winding on it is the reverse; the two are paired and both leave the pool, and a
     * face left over gets no neighbour there. On an edge two faces meet on this is simply "link
     * them when they are wound the other way", which is why the rule is only visible where three
     * or more faces meet on one edge.
     *
     * @param indices Three vertex indices per face.
     * @return `adjacency[face * 3 + edge]`, the face across edge `edge` of `face`, or
     *         @ref OptimizeFacesUnused where that edge has no partner.
     */
    [[nodiscard]] std::vector<std::uint32_t> BuildLegacyMeshAdjacency(
        const std::vector<std::uint32_t>& indices);

    /**
     * @brief Reorders a face list for a simulated post-transform vertex cache.
     *
     * Hugues Hoppe's greedy strip-growing algorithm ("Optimization of mesh locality for transparent
     * vertex caching", SIGGRAPH 1999), in the form Microsoft's open-source DirectXMesh implements
     * it, which Microsoft documents as the same algorithm the legacy D3DX9 `D3DXOptimizeFaces`
     * used with `D3DXMESHOPT_DEVICEINDEPENDENT`.
     *
     * @param indices Three vertex indices per face.
     * @param adjacency The face across each edge, as @ref BuildLegacyMeshAdjacency returns it.
     * @param vertexCache Size of the simulated vertex cache.
     * @param restart Restart threshold; must not exceed @p vertexCache.
     * @return `faceRemap[newLocation]` is the input face that belongs at that output location.
     */
    [[nodiscard]] std::vector<std::uint32_t> OptimizeFaces(
        const std::vector<std::uint32_t>& indices,
        const std::vector<std::uint32_t>& adjacency,
        std::uint32_t vertexCache = OptimizeFacesVertexCacheDefault,
        std::uint32_t restart = OptimizeFacesRestartDefault);
}
