// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <array>

namespace CNA::Internal::Math
{
// XNA Framework's internal Gjk simplex/determinant algorithm. Plane-only
// rejection cannot distinguish a sphere missing a frustum corner. Keep the
// working simplex local to one query; no public API or shared mutable cache.
class Gjk
{
    using Vector3 = Microsoft::Xna::Framework::Vector3;
    static constexpr std::array<int, 16> Indices{0, 1,  2,  17,  3,  25,  26,  209,
                                                 4, 33, 34, 273, 35, 281, 282, 2257};
    std::array<Vector3, 4> points{};
    std::array<float, 4> lengths{};
    std::array<std::array<Vector3, 4>, 4> edges{};
    std::array<std::array<float, 4>, 4> edgeLengths{};
    std::array<std::array<float, 4>, 16> determinants{};
    int simplexBits = 0;
    Vector3 closestPoint;
    float maximumLengthSquared = 0;

    static float Dot(const Vector3 &a, const Vector3 &b)
    {
        return Vector3::Dot(a, b);
    }

    void UpdateDeterminants(int newIndex)
    {
        const int newBit = 1 << newIndex;
        determinants[newBit][newIndex] = 1;
        const int oldIndices = Indices[simplexBits];
        int count = 0;
        for (int packed = oldIndices; packed != 0; packed >>= 3, ++count)
        {
            const int a = (packed & 7) - 1, aBit = 1 << a, pair = aBit | newBit;
            determinants[pair][a] = Dot(edges[newIndex][a], points[newIndex]);
            determinants[pair][newIndex] = Dot(edges[a][newIndex], points[a]);
            int earlier = oldIndices;
            for (int i = 0; i < count; ++i, earlier >>= 3)
            {
                const int b = (earlier & 7) - 1, bBit = 1 << b, triangle = pair | bBit;
                int pivot = edgeLengths[a][b] < edgeLengths[newIndex][b] ? a : newIndex;
                determinants[triangle][b] = static_cast<float>(
                    static_cast<double>(determinants[pair][a]) * Dot(edges[pivot][b], points[a]) +
                    static_cast<double>(determinants[pair][newIndex]) *
                        Dot(edges[pivot][b], points[newIndex]));
                pivot = edgeLengths[b][a] < edgeLengths[newIndex][a] ? b : newIndex;
                determinants[triangle][a] = static_cast<float>(
                    static_cast<double>(determinants[bBit | newBit][b]) * Dot(edges[pivot][a], points[b]) +
                    static_cast<double>(determinants[bBit | newBit][newIndex]) *
                        Dot(edges[pivot][a], points[newIndex]));
                pivot = edgeLengths[a][newIndex] < edgeLengths[b][newIndex] ? a : b;
                determinants[triangle][newIndex] =
                    static_cast<float>(static_cast<double>(determinants[aBit | bBit][b]) *
                                           Dot(edges[pivot][newIndex], points[b]) +
                                       static_cast<double>(determinants[aBit | bBit][a]) *
                                           Dot(edges[pivot][newIndex], points[a]));
            }
        }
        if ((simplexBits | newBit) == 15)
        {
            for (int excluded = 0; excluded < 4; ++excluded)
            {
                std::array<int, 3> face{};
                int countFace = 0;
                for (int i = 0; i < 4; ++i)
                    if (i != excluded)
                        face[countFace++] = i;
                // The strict comparisons and their tie selection match XNA's
                // four explicit tetrahedron determinant expressions.
                const int a = face[0], b = face[1], c = face[2];
                const int pivot = edgeLengths[a][excluded] < edgeLengths[b][excluded]
                                      ? (edgeLengths[a][excluded] < edgeLengths[c][excluded] ? a : c)
                                      : (edgeLengths[b][excluded] < edgeLengths[c][excluded] ? b : c);
                const int faceBits = 15 ^ (1 << excluded);
                determinants[15][excluded] = static_cast<float>(
                    static_cast<double>(determinants[faceBits][a]) * Dot(edges[pivot][excluded], points[a]) +
                    static_cast<double>(determinants[faceBits][b]) * Dot(edges[pivot][excluded], points[b]) +
                    static_cast<double>(determinants[faceBits][c]) * Dot(edges[pivot][excluded], points[c]));
            }
        }
    }

    bool Satisfies(int candidate, int available) const
    {
        for (int packed = Indices[available]; packed != 0; packed >>= 3)
        {
            const int index = (packed & 7) - 1, bit = 1 << index;
            if ((candidate & bit) != 0)
            {
                if (determinants[candidate][index] <= 0)
                    return false;
            }
            else if (determinants[candidate | bit][index] > 0)
                return false;
        }
        return true;
    }

    Vector3 ComputeClosestPoint()
    {
        float sum = 0;
        Vector3 weighted = Vector3::Zero;
        maximumLengthSquared = 0;
        for (int packed = Indices[simplexBits]; packed != 0; packed >>= 3)
        {
            const int index = (packed & 7) - 1;
            const float weight = determinants[simplexBits][index];
            sum += weight;
            weighted += points[index] * weight;
            maximumLengthSquared =
                Microsoft::Xna::Framework::MathHelper::Max(maximumLengthSquared, lengths[index]);
        }
        return weighted / sum;
    }

  public:
    [[nodiscard]] bool FullSimplex() const
    {
        return simplexBits == 15;
    }
    [[nodiscard]] const Vector3 &ClosestPoint() const
    {
        return closestPoint;
    }
    [[nodiscard]] float MaxLengthSquared() const
    {
        return maximumLengthSquared;
    }

    void AddSupportPoint(const Vector3 &point)
    {
        const int index = (Indices[simplexBits ^ 15] & 7) - 1;
        points[index] = point;
        lengths[index] = Dot(point, point);
        for (int packed = Indices[simplexBits]; packed != 0; packed >>= 3)
        {
            const int other = (packed & 7) - 1;
            const Vector3 edge = points[other] - point;
            edges[other][index] = edge;
            edges[index][other] = -edge;
            edgeLengths[index][other] = edgeLengths[other][index] = Dot(edge, edge);
        }
        UpdateDeterminants(index);
        const int addedBit = 1 << index, available = simplexBits | addedBit;
        for (int candidate = simplexBits; candidate != 0; --candidate)
            if ((candidate & available) == candidate && Satisfies(candidate | addedBit, available))
            {
                simplexBits = candidate | addedBit;
                closestPoint = ComputeClosestPoint();
                return;
            }
        if (Satisfies(addedBit, available))
        {
            simplexBits = addedBit;
            closestPoint = points[index];
            maximumLengthSquared = lengths[index];
        }
    }
};
} // namespace CNA::Internal::Math
