// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/TransparentDrawList.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    TransparentDrawList::TransparentDrawList()  = default;
    TransparentDrawList::~TransparentDrawList() = default;

    void TransparentDrawList::clear() { entries_.clear(); }

    int TransparentDrawList::getCount() const { return static_cast<int>(entries_.size()); }

    void TransparentDrawList::submit(const BoundingBox& bounds, std::function<void()> draw)
    {
        if (!draw)
            throw std::invalid_argument(
                "CNA::Graphics::TransparentDrawList::submit: there is nothing to draw");
        entries_.push_back(Entry{bounds, std::move(draw)});
    }

    Vector3 TransparentDrawList::cameraPositionOf(const Matrix& view)
    {
        // The eye is the inverse view's translation. Taken through Invert rather than by negating
        // and rotating the translation row by hand, because that shortcut is only correct for a
        // rigid view matrix and a game is free to hand this one that has a scale in it.
        const Matrix inverse = Matrix::Invert(view);
        return Vector3(inverse.M41, inverse.M42, inverse.M43);
    }

    float TransparentDrawList::sortKey(const BoundingBox& bounds, const Vector3& cameraPosition)
    {
        // The nearest point of the box is the camera position clamped into it, so the distance to
        // that point is zero on every axis the camera is already between.
        const float x = std::clamp(cameraPosition.X, bounds.Min.X, bounds.Max.X) - cameraPosition.X;
        const float y = std::clamp(cameraPosition.Y, bounds.Min.Y, bounds.Max.Y) - cameraPosition.Y;
        const float z = std::clamp(cameraPosition.Z, bounds.Min.Z, bounds.Max.Z) - cameraPosition.Z;
        return std::sqrt(x * x + y * y + z * z);
    }

    std::vector<int> TransparentDrawList::getSortedOrderEXT(const Matrix& view) const
    {
        const Vector3 eye = cameraPositionOf(view);

        std::vector<int> order(entries_.size());
        std::iota(order.begin(), order.end(), 0);

        std::vector<float> keys;
        keys.reserve(entries_.size());
        for (const Entry& entry : entries_) keys.push_back(sortKey(entry.Bounds, eye));

        // stable_sort, and on the distance alone: two draws at the same distance keep submission
        // order, on every run and every standard library. An unstable tie-break would make a frame
        // flicker between orderings that are each individually defensible, which is worse than
        // either -- and it would do it only where two surfaces are exactly as far away, which is
        // exactly where a game has aligned its geometry on purpose.
        std::stable_sort(order.begin(), order.end(), [&keys](const int left, const int right) {
            return keys[static_cast<std::size_t>(left)] > keys[static_cast<std::size_t>(right)];
        });
        return order;
    }

    void TransparentDrawList::drawSorted(const Matrix& view)
    {
        for (const int index : getSortedOrderEXT(view))
        {
            const Entry& entry = entries_[static_cast<std::size_t>(index)];
            if (entry.Draw) entry.Draw();
        }
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
