// SPDX-License-Identifier: MIT
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149: the third Microsoft implementation.
//
// `MeshHelper.OptimizeForCache` is measured by OptimizeForCacheOracle.cs against genuine XNA and
// by D3dxOptimizeOracle.c against the genuine D3DX9 redistributable.  This driver hands the *same*
// probe file, built into the *same* index buffer, to Microsoft's open-source DirectXMesh
// `DirectX::OptimizeFaces`, which Microsoft documents as implementing the same Hoppe algorithm as
// the legacy D3DX entry point, with D3DXMESHOPT_DEVICEINDEPENDENT-equivalent defaults.
//
// DirectXMesh is MIT licensed; see THIRD_PARTY_NOTICES.md for the pinned revision.  This file is
// CNA's own driver: it only calls the public entry point.
//
// `D3DXOptimizeFaces` computed its adjacency internally; `DirectX::OptimizeFaces` takes it as an
// argument.  Two adjacency constructions are offered, selected by the mode argument:
//
//   legacy     -- the adjacency the black-box probe campaign measured of the legacy entry point:
//                 an undirected edge holds the faces carrying it in input order, and walking those
//                 in input order, a face not yet paired takes the *last* still-unpaired face on
//                 that edge wound the other way.  Both are then out of consideration; a face left
//                 over gets no neighbour there.  On a two-face edge this is just "link them if
//                 they are wound the other way".
//   firstother -- at most one neighbour per edge, the first other face in input order, with the
//                 winding left to DirectXMesh's own wedge test.  Kept because it is the reading
//                 the earlier campaign recorded, and it differs only where an edge carries three
//                 or more faces.
//   geometric  -- DirectXMesh's own GenerateAdjacencyAndPointReps over the probe's positions.
//
// Input is the probe text file generate.py writes:
//
//   P <name> <shared:0|1> <positionCount> <faceCount>
//   V <x> <y> <z>
//   F <a> <b> <c>
//
// and one line per probe comes back, in D3dxOptimizeOracle.c's format so that the same analysis
// reads both:
//
//   <name>|<faceRemap>|
//
#include "DirectXMesh.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t kUnused = uint32_t(-1);

    // Walking an edge's faces in input order, a face not yet paired takes the last still-unpaired
    // face on that edge wound the other way; both leave the pool.  616 original probes and 594
    // designed non-manifold ones answer exactly as genuine XNA and genuine D3DX under this.
    std::vector<uint32_t> LegacyAdjacency(const std::vector<uint32_t>& indices, size_t nFaces)
    {
        std::map<std::pair<uint32_t, uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> onEdge;
        for (size_t f = 0; f < nFaces; ++f)
        {
            for (uint32_t n = 0; n < 3; ++n)
            {
                const uint32_t a = indices[f * 3 + n];
                const uint32_t b = indices[f * 3 + ((n + 1) % 3)];
                onEdge[std::make_pair(a < b ? a : b, a < b ? b : a)]
                    .push_back(std::make_pair(uint32_t(f), n));
            }
        }
        std::vector<uint32_t> adjacency(nFaces * 3, kUnused);
        for (auto& entry : onEdge)
        {
            const auto& slots = entry.second;
            std::vector<bool> paired(slots.size(), false);
            for (size_t p = 0; p < slots.size(); ++p)
            {
                if (paired[p])
                    continue;
                const uint32_t i = slots[p].first, n = slots[p].second;
                const uint32_t a = indices[i * 3 + n];
                const uint32_t b = indices[i * 3 + ((n + 1) % 3)];
                for (size_t q = slots.size(); q-- > 0; )
                {
                    if (q == p || paired[q])
                        continue;
                    const uint32_t j = slots[q].first, m = slots[q].second;
                    if (j == i)
                        continue;
                    if (indices[j * 3 + m] == b && indices[j * 3 + ((m + 1) % 3)] == a)
                    {
                        paired[p] = true;
                        paired[q] = true;
                        adjacency[i * 3 + n] = j;
                        adjacency[j * 3 + m] = i;
                        break;
                    }
                }
            }
        }
        return adjacency;
    }

    // The reading the earlier campaign recorded: for edge n of face f, the first *other* face in
    // input order that carries the same undirected edge, with the winding left to DirectXMesh.
    std::vector<uint32_t> FirstOtherAdjacency(const std::vector<uint32_t>& indices, size_t nFaces)
    {
        std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> onEdge;
        for (size_t f = 0; f < nFaces; ++f)
        {
            for (uint32_t n = 0; n < 3; ++n)
            {
                const uint32_t a = indices[f * 3 + n];
                const uint32_t b = indices[f * 3 + ((n + 1) % 3)];
                onEdge[std::make_pair(a < b ? a : b, a < b ? b : a)].push_back(uint32_t(f));
            }
        }
        std::vector<uint32_t> adjacency(nFaces * 3, kUnused);
        for (size_t f = 0; f < nFaces; ++f)
        {
            for (uint32_t n = 0; n < 3; ++n)
            {
                const uint32_t a = indices[f * 3 + n];
                const uint32_t b = indices[f * 3 + ((n + 1) % 3)];
                const auto& faces = onEdge[std::make_pair(a < b ? a : b, a < b ? b : a)];
                for (const uint32_t j : faces)
                {
                    if (j != uint32_t(f))
                    {
                        adjacency[f * 3 + n] = j;
                        break;
                    }
                }
            }
        }
        return adjacency;
    }
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: DxMeshOptimizeOracle <legacy|firstother|geometric>"
                             " <probes.txt> <out.txt>"
                             " [vertexCache] [restart]\n");
        return 2;
    }
    const std::string mode = argv[1];
    if (mode != "legacy" && mode != "firstother" && mode != "geometric")
    {
        std::fprintf(stderr, "DxMeshOptimizeOracle: unknown mode '%s'\n", argv[1]);
        return 2;
    }
    const uint32_t vertexCache = (argc > 4) ? uint32_t(std::atoi(argv[4])) : DirectX::OPTFACES_V_DEFAULT;
    const uint32_t restart = (argc > 5) ? uint32_t(std::atoi(argv[5])) : DirectX::OPTFACES_R_DEFAULT;

    std::FILE* in = std::fopen(argv[2], "rb");
    if (!in) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 3; }
    std::FILE* out = std::fopen(argv[3], "wb");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 3; }

    char line[4096];
    while (std::fgets(line, sizeof(line), in))
    {
        char name[512];
        int shared = 0, positionCount = 0, faceCount = 0;
        if (line[0] != 'P')
            continue;
        if (std::sscanf(line, "P %511s %d %d %d", name, &shared, &positionCount, &faceCount) != 4)
            continue;

        std::vector<DirectX::XMFLOAT3> positions(size_t(positionCount < 0 ? 0 : positionCount));
        for (int i = 0; i < positionCount; ++i)
        {
            if (!std::fgets(line, sizeof(line), in)) break;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::sscanf(line, "V %f %f %f", &x, &y, &z);
            positions[size_t(i)] = DirectX::XMFLOAT3(x, y, z);
        }
        std::vector<std::array<int, 3>> face(size_t(faceCount < 0 ? 0 : faceCount));
        for (int i = 0; i < faceCount; ++i)
        {
            if (!std::fgets(line, sizeof(line), in)) break;
            std::sscanf(line, "F %d %d %d", &face[size_t(i)][0], &face[size_t(i)][1], &face[size_t(i)][2]);
        }

        // The same vertex list and index buffer OptimizeForCacheOracle.cs and D3dxOptimizeOracle.c
        // build: a shared mesh gives one vertex per distinct position in first-encounter order, an
        // unshared mesh one vertex per corner.
        std::vector<int> vertexOfPosition(size_t(positionCount < 0 ? 0 : positionCount), -1);
        std::vector<uint32_t> indices(size_t(faceCount) * 3, 0);
        std::vector<int> vertexPosition;
        uint32_t vertexCount = 0;
        for (int i = 0; i < faceCount; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                const int position = face[size_t(i)][size_t(c)];
                uint32_t vertex;
                if (shared)
                {
                    if (vertexOfPosition[size_t(position)] < 0)
                    {
                        vertexOfPosition[size_t(position)] = int(vertexCount);
                        vertexPosition.push_back(position);
                        vertexCount++;
                    }
                    vertex = uint32_t(vertexOfPosition[size_t(position)]);
                }
                else
                {
                    vertex = vertexCount;
                    vertexPosition.push_back(position);
                    vertexCount++;
                }
                indices[size_t(i) * 3 + size_t(c)] = vertex;
            }
        }

        std::vector<uint32_t> adjacency;
        HRESULT hr = S_OK;
        if (mode == "legacy")
        {
            adjacency = LegacyAdjacency(indices, size_t(faceCount));
        }
        else if (mode == "firstother")
        {
            adjacency = FirstOtherAdjacency(indices, size_t(faceCount));
        }
        else
        {
            std::vector<DirectX::XMFLOAT3> vertexPositions(vertexCount);
            for (uint32_t v = 0; v < vertexCount; ++v)
                vertexPositions[v] = positions[size_t(vertexPosition[v])];
            adjacency.assign(size_t(faceCount) * 3, kUnused);
            std::vector<uint32_t> pointRep(vertexCount, kUnused);
            hr = DirectX::GenerateAdjacencyAndPointReps(indices.data(), size_t(faceCount),
                                                        vertexPositions.data(), vertexCount,
                                                        0.0f, pointRep.data(), adjacency.data());
            if (FAILED(hr))
            {
                std::fprintf(out, "%s|ERROR 0x%08lx|\n", name, (unsigned long)hr);
                continue;
            }
        }

        std::vector<uint32_t> faceRemap(size_t(faceCount), kUnused);
        hr = DirectX::OptimizeFaces(indices.data(), size_t(faceCount), adjacency.data(),
                                    faceRemap.data(), vertexCache, restart);
        if (FAILED(hr))
        {
            std::fprintf(out, "%s|ERROR 0x%08lx|\n", name, (unsigned long)hr);
            continue;
        }
        std::fprintf(out, "%s|", name);
        for (int i = 0; i < faceCount; ++i)
            std::fprintf(out, "%s%lu", i ? "," : "", (unsigned long)faceRemap[size_t(i)]);
        std::fprintf(out, "|\n");
    }
    std::fclose(in);
    std::fclose(out);
    return 0;
}
