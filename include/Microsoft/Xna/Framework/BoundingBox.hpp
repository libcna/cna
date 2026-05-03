#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework {

    /**
     * @brief Axis-aligned bounding box (min/max corner pair).
     *
     * @note Status: STUB. Only the public members `Min` and `Max` and the
     *       basic constructors are provided. XNA helpers such as
     *       `Contains`, `Intersects`, `CreateFromPoints`, ... are TODO.
     */
    struct BoundingBox {
        Vector3 Min;
        Vector3 Max;

        BoundingBox() = default;
        BoundingBox(const Vector3& min, const Vector3& max) : Min(min), Max(max) {}
    };
}
