// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FrustumCullerEXT.hpp"

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;

    FrustumCullerEXT::FrustumCullerEXT() : frustum_(Matrix::getIdentityProperty()) {}

    void FrustumCullerEXT::setViewProjection(const Matrix& viewProjection)
    {
        frustum_.setMatrixProperty(viewProjection);
    }

    void FrustumCullerEXT::setCamera(const Matrix& view, const Matrix& projection)
    {
        setViewProjection(view * projection);
    }

    const BoundingFrustum& FrustumCullerEXT::getFrustum() const { return frustum_; }

    bool FrustumCullerEXT::isVisible(const BoundingBox& box) const
    {
        return frustum_.Intersects(box);
    }

    bool FrustumCullerEXT::isVisible(const BoundingSphere& sphere) const
    {
        return frustum_.Intersects(sphere);
    }

    std::size_t FrustumCullerEXT::cull(const std::vector<BoundingBox>& bounds,
                                       std::vector<std::size_t>& visibleIndices) const
    {
        visibleIndices.clear();
        for (std::size_t i = 0; i < bounds.size(); ++i)
            if (isVisible(bounds[i]))
                visibleIndices.push_back(i);
        return visibleIndices.size();
    }

    std::size_t FrustumCullerEXT::cull(const std::vector<BoundingSphere>& bounds,
                                       std::vector<std::size_t>& visibleIndices) const
    {
        visibleIndices.clear();
        for (std::size_t i = 0; i < bounds.size(); ++i)
            if (isVisible(bounds[i]))
                visibleIndices.push_back(i);
        return visibleIndices.size();
    }

    std::size_t FrustumCullerEXT::cullTransforms(
        const std::vector<Matrix>& transforms, const std::vector<BoundingBox>& bounds,
        std::vector<Matrix>& visibleTransforms) const
    {
        visibleTransforms.clear();
        for (std::size_t i = 0; i < transforms.size(); ++i)
        {
            if (i >= bounds.size() || isVisible(bounds[i]))
                visibleTransforms.push_back(transforms[i]);
        }
        return visibleTransforms.size();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
