// SPDX-License-Identifier: MIT
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Adapted from Microsoft's DirectXMesh, `DirectXMesh/DirectXMeshOptimizeTVC.cpp` -- the classes
// `mesh_status`, `sim_vcache` and the function `VertexCacheStripReorderImpl` -- at revision
// bd17eb215d463d98f2b3a13082ce13979219314f (tag `oct2025`) of https://github.com/microsoft/DirectXMesh.
// Hugues Hoppe, "Optimization of mesh locality for transparent vertex caching", SIGGRAPH 1999.
// The full licence and the provenance record are in THIRD_PARTY_NOTICES.md and
// tools/provenance/derived-sources.json.
//
// What CNA changed, and nothing else:
//   * the Direct3D types are gone. `HRESULT`/`E_*` returns become a `std::vector` result, the
//     `std::unique_ptr<T[]>` scratch buffers become `std::vector`, and the allocation-failure
//     paths go with them;
//   * attribute subsets are gone. `ModelProcessor` optimises one `GeometryContent` at a time, so
//     there is exactly one subset covering every face, and `ComputeSubsets`/`setSubset`'s offset
//     arithmetic collapses to it;
//   * the `uint16_t` overload and the strip-order (`OPTFACES_V_STRIPORDER`) route are not here,
//     because CNA calls neither;
//   * the debug `assert`s are not carried over, and a face the walk never reaches -- which upstream
//     leaves out of the remap entirely -- is appended in input order instead of being dropped, so
//     that the caller's triangle count is preserved whatever it hands in.
// The decisions the algorithm makes are untouched.

#include "CNA/Internal/OptimizeFaces.hpp"

#include <algorithm>
#include <cstddef>

namespace CNA::Internal
{
    namespace
    {
        constexpr std::uint32_t kUnused = OptimizeFacesUnused;

        std::uint32_t FindEdge(const std::uint32_t* triple, std::uint32_t search) noexcept
        {
            std::uint32_t edge = 0;
            for (; edge < 3; ++edge)
            {
                if (triple[edge] == search)
                {
                    break;
                }
            }
            return edge;
        }

        /// `mesh_status`: the physical adjacency, and the buckets the seed is drawn from.
        class MeshStatus
        {
        public:
            MeshStatus(const std::vector<std::uint32_t>& indices,
                       const std::vector<std::uint32_t>& adjacency)
                : indices_(indices), faceCount_(indices.size() / 3),
                  physical_(indices.size(), kUnused)
            {
                for (std::uint32_t face = 0; face < faceCount_; ++face)
                {
                    const std::uint32_t i0 = indices_[face * 3];
                    const std::uint32_t i1 = indices_[face * 3 + 1];
                    const std::uint32_t i2 = indices_[face * 3 + 2];
                    if (i0 == kUnused || i1 == kUnused || i2 == kUnused
                        || i0 == i1 || i0 == i2 || i1 == i2)
                    {
                        // unused and degenerate faces should not have neighbours
                        for (std::uint32_t point = 0; point < 3; ++point)
                        {
                            const std::uint32_t k = adjacency[face * 3 + point];
                            if (k != kUnused && k < faceCount_)
                            {
                                for (std::uint32_t e = 0; e < 3; ++e)
                                {
                                    if (adjacency[k * 3 + e] == face)
                                    {
                                        physical_[k * 3 + e] = kUnused;
                                    }
                                }
                            }
                            physical_[face * 3 + point] = kUnused;
                        }
                        continue;
                    }
                    for (std::uint32_t n = 0; n < 3; ++n)
                    {
                        std::uint32_t neighbor = adjacency[face * 3 + n];
                        if (neighbor != kUnused)
                        {
                            if (neighbor >= faceCount_
                                || neighbor == adjacency[face * 3 + ((n + 1) % 3)]
                                || neighbor == adjacency[face * 3 + ((n + 2) % 3)])
                            {
                                // links outside the set, and duplicate neighbours, are broken
                                neighbor = kUnused;
                            }
                            else
                            {
                                const std::uint32_t edgeBack =
                                    FindEdge(&adjacency[neighbor * 3], face);
                                if (edgeBack < 3)
                                {
                                    const std::uint32_t p1 = indices_[face * 3 + n];
                                    const std::uint32_t p2 = indices_[face * 3 + ((n + 1) % 3)];
                                    const std::uint32_t pn1 = indices_[neighbor * 3 + edgeBack];
                                    const std::uint32_t pn2 =
                                        indices_[neighbor * 3 + ((edgeBack + 1) % 3)];
                                    // if the wedge is not identical on the shared edge, drop the link
                                    if (p1 != pn2 || p2 != pn1)
                                    {
                                        neighbor = kUnused;
                                    }
                                }
                                else
                                {
                                    neighbor = kUnused;
                                }
                            }
                        }
                        physical_[face * 3 + n] = neighbor;
                    }
                }
            }

            void SetSubset()
            {
                processed_.assign(faceCount_, false);
                unprocessed_.assign(faceCount_, 0);
                previous_.assign(faceCount_, kUnused);
                next_.assign(faceCount_, kUnused);
                heads_.assign(4, kUnused);
                for (std::uint32_t face = 0; face < faceCount_; ++face)
                {
                    const std::uint32_t i0 = indices_[face * 3];
                    const std::uint32_t i1 = indices_[face * 3 + 1];
                    const std::uint32_t i2 = indices_[face * 3 + 2];
                    if (i0 == kUnused || i1 == kUnused || i2 == kUnused)
                    {
                        continue;                       // filter out unused triangles
                    }
                    std::uint32_t count = 0;
                    for (std::uint32_t n = 0; n < 3; ++n)
                    {
                        if (physical_[face * 3 + n] != kUnused)
                        {
                            count += 1;
                        }
                    }
                    unprocessed_[face] = count;
                    PushFront(face);
                }
            }

            [[nodiscard]] bool IsProcessed(std::uint32_t face) const { return processed_[face]; }

            [[nodiscard]] std::uint32_t FindInitial() const
            {
                for (std::size_t j = 0; j < 4; ++j)
                {
                    if (heads_[j] != kUnused)
                    {
                        return heads_[j];
                    }
                }
                return kUnused;
            }

            void Mark(std::uint32_t face)
            {
                processed_[face] = true;
                Remove(face);
                for (std::uint32_t n = 0; n < 3; ++n)
                {
                    const std::uint32_t neighbor = physical_[face * 3 + n];
                    if (neighbor != kUnused && !processed_[neighbor])
                    {
                        Decrement(neighbor);
                    }
                }
            }

            [[nodiscard]] std::uint32_t Neighbor(std::uint32_t face, std::uint32_t n) const
            {
                return physical_[face * 3 + n];
            }

            [[nodiscard]] const std::uint32_t* NeighborsOf(std::uint32_t face) const
            {
                return &physical_[face * 3];
            }

            [[nodiscard]] std::uint32_t FaceCount() const { return faceCount_; }

        private:
            void PushFront(std::uint32_t face)
            {
                const std::uint32_t bucket = unprocessed_[face];
                const std::uint32_t head = heads_[bucket];
                next_[face] = head;
                if (head != kUnused)
                {
                    previous_[head] = face;
                }
                heads_[bucket] = face;
                previous_[face] = kUnused;
            }

            void Remove(std::uint32_t face)
            {
                if (previous_[face] != kUnused)
                {
                    const std::uint32_t previous = previous_[face];
                    const std::uint32_t following = next_[face];
                    next_[previous] = following;
                    if (following != kUnused)
                    {
                        previous_[following] = previous;
                    }
                }
                else
                {
                    const std::uint32_t bucket = unprocessed_[face];
                    heads_[bucket] = next_[face];
                    if (heads_[bucket] != kUnused)
                    {
                        previous_[heads_[bucket]] = kUnused;
                    }
                }
                previous_[face] = kUnused;
                next_[face] = kUnused;
            }

            void Decrement(std::uint32_t face)
            {
                Remove(face);
                unprocessed_[face] -= 1;
                PushFront(face);
            }

            const std::vector<std::uint32_t>& indices_;
            std::uint32_t faceCount_;
            std::vector<std::uint32_t> physical_;
            std::vector<bool> processed_;
            std::vector<std::uint32_t> unprocessed_;
            std::vector<std::uint32_t> previous_;
            std::vector<std::uint32_t> next_;
            std::vector<std::uint32_t> heads_;
        };

        /// `sim_vcache`: a fixed-size FIFO of vertex indices; a hit does not refresh the entry.
        class SimulatedVertexCache
        {
        public:
            explicit SimulatedVertexCache(std::uint32_t size) : fifo_(size, kUnused) {}

            void Clear()
            {
                std::fill(fifo_.begin(), fifo_.end(), kUnused);
                tail_ = 0;
            }

            bool Access(std::uint32_t vertex)
            {
                for (const std::uint32_t entry : fifo_)
                {
                    if (entry == vertex)
                    {
                        return true;
                    }
                }
                fifo_[tail_] = vertex;
                tail_ += 1;
                if (tail_ == fifo_.size())
                {
                    tail_ = 0;
                }
                return false;
            }

        private:
            std::vector<std::uint32_t> fifo_;
            std::size_t tail_ = 0;
        };

        struct FaceCorner
        {
            std::uint32_t face = kUnused;
            std::uint32_t corner = kUnused;
        };

        FaceCorner CounterclockwiseCorner(FaceCorner corner, const MeshStatus& status)
        {
            const std::uint32_t edge = (corner.corner + 2) % 3;
            const std::uint32_t neighbor = status.Neighbor(corner.face, edge);
            const std::uint32_t point = (neighbor == kUnused)
                ? kUnused : FindEdge(status.NeighborsOf(neighbor), corner.face);
            return FaceCorner{neighbor, point};
        }
    }

    std::vector<std::uint32_t> OptimizeFaces(const std::vector<std::uint32_t>& indices,
                                            const std::vector<std::uint32_t>& adjacency,
                                            std::uint32_t vertexCache, std::uint32_t restart)
    {
        const std::uint32_t faceCount = static_cast<std::uint32_t>(indices.size() / 3);
        std::vector<std::uint32_t> faceRemap;
        if (faceCount == 0 || adjacency.size() < indices.size() || vertexCache == 0
            || restart > vertexCache)
        {
            faceRemap.resize(faceCount);
            for (std::uint32_t face = 0; face < faceCount; ++face)
            {
                faceRemap[face] = face;
            }
            return faceRemap;
        }

        MeshStatus status(indices, adjacency);
        SimulatedVertexCache vcache(vertexCache);
        std::vector<std::uint32_t> remapInverse(faceCount, kUnused);
        const std::uint32_t desired = vertexCache - restart;

        status.SetSubset();
        vcache.Clear();

        std::uint32_t locnext = 0;
        FaceCorner nextCorner;
        FaceCorner curCorner;
        std::uint32_t emitted = 0;

        for (;;)
        {
            curCorner.face = status.FindInitial();
            if (curCorner.face == kUnused)
            {
                break;
            }
            const std::uint32_t n0 = status.Neighbor(curCorner.face, 0);
            if (n0 != kUnused && !status.IsProcessed(n0))
            {
                curCorner.corner = 1;
            }
            else
            {
                const std::uint32_t n1 = status.Neighbor(curCorner.face, 1);
                curCorner.corner = (n1 != kUnused && !status.IsProcessed(n1)) ? 2 : 0;
            }

            bool stripRestart = false;
            for (;;)
            {
                // Decision: either add a ring of faces or restart the strip
                if (nextCorner.face != kUnused)
                {
                    std::uint32_t ring = 0;
                    for (FaceCorner temp = curCorner;;)
                    {
                        const FaceCorner following = CounterclockwiseCorner(temp, status);
                        if (following.face == kUnused || status.IsProcessed(following.face))
                        {
                            break;
                        }
                        ++ring;
                        temp = following;
                    }
                    if (locnext + ring > desired)
                    {
                        if (!status.IsProcessed(nextCorner.face))
                        {
                            curCorner = nextCorner;
                        }
                        nextCorner.face = kUnused;
                    }
                }

                for (;;)
                {
                    status.Mark(curCorner.face);
                    remapInverse[curCorner.face] = emitted;
                    emitted += 1;

                    for (std::uint32_t k = 0; k < 3; ++k)
                    {
                        if (!vcache.Access(indices[curCorner.face * 3 + k]))
                        {
                            locnext += 1;
                        }
                    }

                    const FaceCorner interior = CounterclockwiseCorner(curCorner, status);
                    const bool hasInterior =
                        interior.face != kUnused && !status.IsProcessed(interior.face);

                    const FaceCorner exterior = CounterclockwiseCorner(
                        FaceCorner{curCorner.face, (curCorner.corner + 2) % 3}, status);
                    const bool hasExterior =
                        exterior.face != kUnused && !status.IsProcessed(exterior.face);

                    if (hasInterior)
                    {
                        if (hasExterior && nextCorner.face == kUnused)
                        {
                            nextCorner = exterior;
                            locnext = 0;
                        }
                        curCorner = interior;
                    }
                    else if (hasExterior)
                    {
                        curCorner = exterior;
                        break;
                    }
                    else
                    {
                        curCorner = nextCorner;
                        nextCorner.face = kUnused;
                        if (curCorner.face == kUnused || status.IsProcessed(curCorner.face))
                        {
                            stripRestart = true;
                        }
                        break;
                    }
                }

                if (stripRestart)
                {
                    break;
                }
            }
        }

        faceRemap.assign(faceCount, kUnused);
        for (std::uint32_t face = 0; face < faceCount; ++face)
        {
            const std::uint32_t location = remapInverse[face];
            if (location < faceCount)
            {
                faceRemap[location] = face;
            }
        }
        // A face the walk never reached -- one whose corners are not all vertices -- keeps its
        // input order at the tail rather than disappearing from the remap.
        std::uint32_t tail = emitted;
        for (std::uint32_t face = 0; face < faceCount; ++face)
        {
            if (remapInverse[face] == kUnused && tail < faceCount)
            {
                faceRemap[tail++] = face;
            }
        }
        return faceRemap;
    }
}
