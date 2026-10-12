// SPDX-License-Identifier: MS-PL

#include "CNA/C/geometry.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::StoreOutput;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

template<typename TCallable>
[[nodiscard]] CNA_Result StoreOptionalDistance(
    CNA_Bool* const outHit,
    float* const outDistance,
    TCallable&& callable)
{
    const std::optional<float> distance = std::forward<TCallable>(callable)();
    *outHit = ToCBool(distance.has_value());
    *outDistance = distance.value_or(0.0F);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PlaneNormalizeInPlace(CNA_Plane* const value)
{
    if (value == nullptr) {
        return InvalidArgument("The Plane is null.");
    }
    Plane native = ToNative(*value);
    native.Normalize();
    *value = ToC(native);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result RayIntersectsBox(
    const CNA_Ray ray,
    const CNA_BoundingBox box,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(box));
    });
}

[[nodiscard]] CNA_Result RayIntersectsSphere(
    const CNA_Ray ray,
    const CNA_BoundingSphere sphere,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(sphere));
    });
}

[[nodiscard]] CNA_Result RayIntersectsPlane(
    const CNA_Ray ray,
    const CNA_Plane plane,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(plane));
    });
}

[[nodiscard]] CNA_Result RayIntersectsFrustum(
    const CNA_Ray ray,
    const CNA_BoundingFrustum frustum,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(frustum));
    });
}

[[nodiscard]] CNA_Result BoundingBoxCopyCorners(
    const CNA_BoundingBox value,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The corner destination or required-count output is invalid.");
    }
    constexpr uint64_t RequiredCount = CNA_BOUNDING_BOX_CORNER_COUNT;
    *outCount = RequiredCount;
    if (capacity < RequiredCount) {
        return BufferTooSmall("The destination cannot hold all BoundingBox corners.");
    }
    const std::vector<Vector3> corners = ToNative(value).GetCorners();
    for (uint64_t index = 0U; index < RequiredCount; ++index) {
        destination[index] = ToC(corners[static_cast<std::size_t>(index)]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BoundingBoxIntersectsRay(
    const CNA_BoundingBox value,
    const CNA_Ray ray,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(value).Intersects(ToNative(ray));
    });
}

[[nodiscard]] CNA_Result BoundingBoxCreateFromPoints(
    const CNA_Vector3* const points,
    const uint64_t count,
    CNA_BoundingBox* const outValue)
{
    std::size_t byteCount = 0U;
    const CNA_Result validation = CheckedElementByteCount(
        points, count, sizeof(CNA_Vector3), &byteCount);
    if (validation != CNA_RESULT_SUCCESS) {
        return Fail(
            validation,
            ErrorCategoryForResult(validation),
            "The point array or count is invalid.");
    }
    if (count == 0U) {
        return InvalidArgument("The point array is empty.");
    }
    std::vector<Vector3> nativePoints;
    nativePoints.reserve(byteCount / sizeof(CNA_Vector3));
    for (uint64_t index = 0U; index < count; ++index) {
        nativePoints.push_back(ToNative(points[index]));
    }
    const CNA_BoundingBox result = ToC(BoundingBox::CreateFromPoints(nativePoints));
    *outValue = result;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BoundingSphereCreateFromPoints(
    const CNA_Vector3* const points,
    const uint64_t count,
    CNA_BoundingSphere* const outValue)
{
    std::size_t byteCount = 0U;
    const CNA_Result validation = CheckedElementByteCount(
        points, count, sizeof(CNA_Vector3), &byteCount);
    if (validation != CNA_RESULT_SUCCESS) {
        return Fail(
            validation,
            ErrorCategoryForResult(validation),
            "The point array or count is invalid.");
    }
    if (count == 0U) {
        return InvalidArgument("The point array is empty.");
    }
    std::vector<Vector3> nativePoints;
    nativePoints.reserve(byteCount / sizeof(CNA_Vector3));
    for (uint64_t index = 0U; index < count; ++index) {
        nativePoints.push_back(ToNative(points[index]));
    }
    const CNA_BoundingSphere result = ToC(BoundingSphere::CreateFromPoints(nativePoints));
    *outValue = result;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BoundingSphereIntersectsRay(
    const CNA_BoundingSphere value,
    const CNA_Ray ray,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(value).Intersects(ToNative(ray));
    });
}

[[nodiscard]] CNA_Result BoundingFrustumContainsFrustum(
    const CNA_BoundingFrustum value,
    const CNA_BoundingFrustum frustum,
    CNA_ContainmentType* const outContainment)
{
    return StoreOutput(outContainment, "The containment output is null.", [=] {
        const BoundingFrustum nativeValue = ToNative(value);
        const BoundingFrustum nativeFrustum = ToNative(frustum);
        // C frusta are matrix values, so source reference identity maps to equal matrix values.
        if (nativeValue.Equals(nativeFrustum)) {
            return CNA_CONTAINMENT_CONTAINS;
        }
        return static_cast<CNA_ContainmentType>(nativeValue.Contains(nativeFrustum));
    });
}

[[nodiscard]] CNA_Result BoundingFrustumCopyCorners(
    const CNA_BoundingFrustum value,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The corner destination or required-count output is invalid.");
    }
    constexpr uint64_t RequiredCount = CNA_BOUNDING_FRUSTUM_CORNER_COUNT;
    *outCount = RequiredCount;
    if (capacity < RequiredCount) {
        return BufferTooSmall("The destination cannot hold all BoundingFrustum corners.");
    }
    const std::vector<Vector3> corners = ToNative(value).GetCorners();
    for (uint64_t index = 0U; index < RequiredCount; ++index) {
        destination[index] = ToC(corners[static_cast<std::size_t>(index)]);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BoundingFrustumIntersectsRay(
    const CNA_BoundingFrustum value,
    const CNA_Ray ray,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(value).Intersects(ToNative(ray));
    });
}

} // namespace

// The routes generated from modules/c-api/spec/Geometry.toml.
#include "generated/Geometry.inc"
