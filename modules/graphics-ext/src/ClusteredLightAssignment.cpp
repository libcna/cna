// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightAssignment.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    namespace {

        /// Squared distance from a point to the nearest point of a box; zero when inside. Compared
        /// against the squared radius so nothing takes a square root.
        float SquaredDistanceToBox(const BoundingBox& box, const Vector3& point)
        {
            float total = 0.0f;
            const float minimum[3] = {box.Min.X, box.Min.Y, box.Min.Z};
            const float maximum[3] = {box.Max.X, box.Max.Y, box.Max.Z};
            const float p[3] = {point.X, point.Y, point.Z};
            for (int axis = 0; axis < 3; ++axis)
            {
                if (p[axis] < minimum[axis])
                {
                    const float d = minimum[axis] - p[axis];
                    total += d * d;
                }
                else if (p[axis] > maximum[axis])
                {
                    const float d = p[axis] - maximum[axis];
                    total += d * d;
                }
            }
            return total;
        }

    } // namespace

    ClusteredLightAssignment::ClusteredLightAssignment() = default;

    void ClusteredLightAssignment::clear()
    {
        indices_.clear();
        offsets_.assign(1, 0);
        lightCount_ = 0;
        clusterCount_ = 0;
        maxPerCluster_ = 0;
    }

    void ClusteredLightAssignment::assign(const ClusteredLightGrid& grid, const Matrix& view,
                                          const std::vector<BoundingSphere>& lights)
    {
        if (static_cast<int>(lights.size()) > kMaxLights)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightAssignment::assign: more lights than the assignment "
                "accepts -- the index list is sized from this bound, and a scene needing more "
                "wants a second grid rather than a longer list");
        if (!grid.hasProjection())
            throw std::runtime_error(
                "CNA::Graphics::ClusteredLightAssignment::assign: the grid has no projection, so "
                "it has no clusters to sort into yet");

        clear();
        lightCount_   = static_cast<int>(lights.size());
        clusterCount_ = grid.getClusterCount();

        std::vector<std::vector<int>> perCluster(static_cast<std::size_t>(clusterCount_));

        for (int light = 0; light < lightCount_; ++light)
        {
            const BoundingSphere& sphere = lights[static_cast<std::size_t>(light)];
            if (!(sphere.Radius > 0.0f)) continue;

            const Vector3 centre = Vector3::Transform(sphere.Center, view);
            const float radiusSquared = sphere.Radius * sphere.Radius;

            // View distance is −z, so the sphere covers distances [−z − r, −z + r]. A light wholly
            // behind the grid's far plane, or wholly behind the camera, never enters the loop.
            const float nearest   = -centre.Z - sphere.Radius;
            const float furthest  = -centre.Z + sphere.Radius;
            if (furthest <= 0.0f || nearest >= grid.getFarPlane()) continue;

            const int firstSlice = grid.sliceForViewDistance(nearest);
            const int lastSlice  = grid.sliceForViewDistance(furthest);

            for (int slice = firstSlice; slice <= lastSlice; ++slice)
                for (int y = 0; y < grid.getTilesY(); ++y)
                    for (int x = 0; x < grid.getTilesX(); ++x)
                    {
                        const BoundingBox bounds = grid.clusterBounds(x, y, slice);
                        if (SquaredDistanceToBox(bounds, centre) > radiusSquared) continue;
                        perCluster[static_cast<std::size_t>(grid.clusterIndex(x, y, slice))]
                            .push_back(light);
                    }
        }

        offsets_.resize(static_cast<std::size_t>(clusterCount_) + 1);
        offsets_[0] = 0;
        for (int cluster = 0; cluster < clusterCount_; ++cluster)
        {
            const std::vector<int>& list = perCluster[static_cast<std::size_t>(cluster)];
            indices_.insert(indices_.end(), list.begin(), list.end());
            offsets_[static_cast<std::size_t>(cluster) + 1] = static_cast<int>(indices_.size());
            maxPerCluster_ = std::max(maxPerCluster_, static_cast<int>(list.size()));
        }
    }

    void ClusteredLightAssignment::adopt(const int lightCount, std::vector<int> offsets,
                                         std::vector<int> indices)
    {
        if (lightCount < 0 || offsets.empty() || offsets.front() != 0)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightAssignment::adopt: the offsets must begin at zero "
                "and describe at least one cluster");
        if (offsets.back() != static_cast<int>(indices.size()))
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightAssignment::adopt: the last offset must be the "
                "length of the index array");
        for (std::size_t i = 1; i < offsets.size(); ++i)
            if (offsets[i] < offsets[i - 1])
                throw std::invalid_argument(
                    "CNA::Graphics::ClusteredLightAssignment::adopt: the offsets go backwards");
        for (const int index : indices)
            if (index < 0 || index >= lightCount)
                throw std::invalid_argument(
                    "CNA::Graphics::ClusteredLightAssignment::adopt: an index names a light that "
                    "is not in the set");

        clusterCount_ = static_cast<int>(offsets.size()) - 1;
        lightCount_   = lightCount;
        offsets_      = std::move(offsets);
        indices_      = std::move(indices);

        maxPerCluster_ = 0;
        for (int cluster = 0; cluster < clusterCount_; ++cluster)
            maxPerCluster_ = std::max(maxPerCluster_,
                                      offsets_[static_cast<std::size_t>(cluster) + 1] -
                                      offsets_[static_cast<std::size_t>(cluster)]);
    }

    int ClusteredLightAssignment::getLightCount()   const { return lightCount_; }
    int ClusteredLightAssignment::getClusterCount() const { return clusterCount_; }

    std::span<const int> ClusteredLightAssignment::lightsInCluster(const int clusterIndex) const
    {
        if (clusterIndex < 0 || clusterIndex >= clusterCount_)
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightAssignment::lightsInCluster: the cluster index is "
                "outside the assigned range");
        const int begin = offsets_[static_cast<std::size_t>(clusterIndex)];
        const int end   = offsets_[static_cast<std::size_t>(clusterIndex) + 1];
        return std::span<const int>(indices_.data() + begin, static_cast<std::size_t>(end - begin));
    }

    const std::vector<int>& ClusteredLightAssignment::getIndices() const { return indices_; }
    const std::vector<int>& ClusteredLightAssignment::getOffsets() const { return offsets_; }

    int ClusteredLightAssignment::getTotalReferenceCount() const
    {
        return static_cast<int>(indices_.size());
    }

    int ClusteredLightAssignment::getMaxLightsPerCluster() const { return maxPerCluster_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
