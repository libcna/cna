// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightGrid.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;

    namespace {

        Vector3 Unproject(const Matrix& inverseProjection, const float ndcX, const float ndcY,
                          const float ndcZ)
        {
            const Vector4 p = Vector4::Transform(Vector4(ndcX, ndcY, ndcZ, 1.0f), inverseProjection);
            if (std::fabs(p.W) <= 1e-9f) return Vector3(p.X, p.Y, p.Z);
            return Vector3(p.X / p.W, p.Y / p.W, p.Z / p.W);
        }

        /// The point on the ray through a tile corner that sits at a given view distance. Written as
        /// an interpolation between the near and far unprojections rather than as a ray scaled by
        /// 1/z, because view-space z is linear along that segment for a perspective *and* an
        /// orthographic projection, and the scaling form is only correct for the first.
        Vector3 AtDistance(const Vector3& atNear, const Vector3& atFar, const float distance)
        {
            const float span = atNear.Z - atFar.Z;
            if (std::fabs(span) <= 1e-9f) return atNear;
            const float t = (atNear.Z + distance) / span;
            return Vector3(atNear.X + (atFar.X - atNear.X) * t,
                           atNear.Y + (atFar.Y - atNear.Y) * t,
                           -distance);
        }

    } // namespace

    ClusteredLightGrid::ClusteredLightGrid()
        : ClusteredLightGrid(kDefaultTilesX, kDefaultTilesY, kDefaultSliceCount)
    {
    }

    ClusteredLightGrid::ClusteredLightGrid(const int tilesX, const int tilesY, const int sliceCount)
        : tilesX_(tilesX), tilesY_(tilesY), sliceCount_(sliceCount)
    {
        if (tilesX < 1 || tilesX > kMaxTilesPerAxis || tilesY < 1 || tilesY > kMaxTilesPerAxis ||
            sliceCount < 1 || sliceCount > kMaxSliceCount)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightGrid: a grid dimension is outside its range -- the "
                "screen axes take 1 to 128 tiles and the depth axis 1 to 256 slices, because the "
                "cluster count is what the light-index list is sized from");
    }

    int ClusteredLightGrid::getTilesX() const { return tilesX_; }
    int ClusteredLightGrid::getTilesY() const { return tilesY_; }
    int ClusteredLightGrid::getSliceCount() const { return sliceCount_; }
    int ClusteredLightGrid::getClusterCount() const { return tilesX_ * tilesY_ * sliceCount_; }

    int ClusteredLightGrid::clusterIndex(const int x, const int y, const int slice) const
    {
        if (x < 0 || x >= tilesX_ || y < 0 || y >= tilesY_ || slice < 0 || slice >= sliceCount_)
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightGrid::clusterIndex: the coordinate is outside the "
                "grid");
        return (slice * tilesY_ + y) * tilesX_ + x;
    }

    void ClusteredLightGrid::setProjection(const Matrix& projection, const float nearPlane,
                                           const float farPlane)
    {
        if (!(nearPlane > 0.0f) || !(farPlane > nearPlane))
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightGrid::setProjection: the near distance must be "
                "positive and the far distance must exceed it -- the slice spacing is a ratio of "
                "the two, so a zero near plane has no logarithm and an inverted pair has no grid");

        const Matrix inverse = Matrix::Invert(projection);
        // A projection whose inverse is not finite is a caller mistake worth naming here rather
        // than letting every cluster come back as NaN and every light land in every cluster.
        for (int i = 0; i < 16; ++i)
        {
            const float value = (&inverse.M11)[i];
            if (!std::isfinite(value))
                throw std::invalid_argument(
                    "CNA::Graphics::ClusteredLightGrid::setProjection: the projection matrix could "
                    "not be inverted");
        }

        inverseProjection_ = inverse;
        nearPlane_ = nearPlane;
        farPlane_  = farPlane;
        hasProjection_ = true;
    }

    float ClusteredLightGrid::getNearPlane() const { return nearPlane_; }
    float ClusteredLightGrid::getFarPlane()  const { return farPlane_; }
    bool  ClusteredLightGrid::hasProjection() const { return hasProjection_; }

    Matrix ClusteredLightGrid::getInverseProjection() const
    {
        return hasProjection_ ? inverseProjection_ : Matrix::getIdentityProperty();
    }

    float ClusteredLightGrid::sliceDistance(const int slice) const
    {
        if (slice < 0 || slice > sliceCount_)
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightGrid::sliceDistance: the slice is outside the grid");
        if (!hasProjection_) return 0.0f;
        if (slice == 0) return nearPlane_;
        if (slice == sliceCount_) return farPlane_;

        const float ratio = farPlane_ / nearPlane_;
        return nearPlane_ * std::pow(ratio, static_cast<float>(slice) /
                                            static_cast<float>(sliceCount_));
    }

    int ClusteredLightGrid::sliceForViewDistance(const float viewDistance) const
    {
        if (!hasProjection_ || viewDistance <= nearPlane_) return 0;
        if (viewDistance >= farPlane_) return sliceCount_ - 1;

        const float ratio = std::log(viewDistance / nearPlane_) / std::log(farPlane_ / nearPlane_);
        const int slice = static_cast<int>(std::floor(ratio * static_cast<float>(sliceCount_)));
        return std::clamp(slice, 0, sliceCount_ - 1);
    }

    BoundingBox ClusteredLightGrid::clusterBounds(const int x, const int y, const int slice) const
    {
        if (x < 0 || x >= tilesX_ || y < 0 || y >= tilesY_ || slice < 0 || slice >= sliceCount_)
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightGrid::clusterBounds: the coordinate is outside the "
                "grid");
        if (!hasProjection_)
            throw std::runtime_error(
                "CNA::Graphics::ClusteredLightGrid::clusterBounds: no projection has been set, so "
                "the grid has no shape yet");

        const float u0 = 2.0f * static_cast<float>(x)     / static_cast<float>(tilesX_) - 1.0f;
        const float u1 = 2.0f * static_cast<float>(x + 1) / static_cast<float>(tilesX_) - 1.0f;
        const float v0 = 2.0f * static_cast<float>(y)     / static_cast<float>(tilesY_) - 1.0f;
        const float v1 = 2.0f * static_cast<float>(y + 1) / static_cast<float>(tilesY_) - 1.0f;

        const float distances[2] = {sliceDistance(slice), sliceDistance(slice + 1)};
        const float us[2] = {u0, u1};
        const float vs[2] = {v0, v1};

        Vector3 minimum( 3.4028235e38f,  3.4028235e38f,  3.4028235e38f);
        Vector3 maximum(-3.4028235e38f, -3.4028235e38f, -3.4028235e38f);

        for (const float u : us)
            for (const float v : vs)
            {
                // XNA's projection puts the near plane at NDC z 0 and the far plane at 1; both are
                // unprojected once per corner and every slice interpolates between them.
                const Vector3 atNear = Unproject(inverseProjection_, u, v, 0.0f);
                const Vector3 atFar  = Unproject(inverseProjection_, u, v, 1.0f);
                for (const float distance : distances)
                {
                    const Vector3 p = AtDistance(atNear, atFar, distance);
                    minimum.X = std::min(minimum.X, p.X);
                    minimum.Y = std::min(minimum.Y, p.Y);
                    minimum.Z = std::min(minimum.Z, p.Z);
                    maximum.X = std::max(maximum.X, p.X);
                    maximum.Y = std::max(maximum.Y, p.Y);
                    maximum.Z = std::max(maximum.Z, p.Z);
                }
            }

        return BoundingBox(minimum, maximum);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
