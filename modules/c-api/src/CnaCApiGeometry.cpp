// SPDX-License-Identifier: MS-PL

#include "CNA/C/geometry.h"
#include "CnaCApiDetail.hpp"

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

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value)
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Vector4 ToNative(const CNA_Vector4 value)
{
    return Vector4(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Quaternion ToNative(const CNA_Quaternion value)
{
    return Quaternion(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Matrix ToNative(const CNA_Matrix value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] Plane ToNative(const CNA_Plane value)
{
    return Plane(ToNative(value.normal), value.d);
}

[[nodiscard]] Ray ToNative(const CNA_Ray value)
{
    return Ray(ToNative(value.position), ToNative(value.direction));
}

[[nodiscard]] BoundingBox ToNative(const CNA_BoundingBox value)
{
    return BoundingBox(ToNative(value.min), ToNative(value.max));
}

[[nodiscard]] BoundingSphere ToNative(const CNA_BoundingSphere value)
{
    return BoundingSphere(ToNative(value.center), value.radius);
}

[[nodiscard]] BoundingFrustum ToNative(const CNA_BoundingFrustum value)
{
    return BoundingFrustum(ToNative(value.matrix));
}

[[nodiscard]] CNA_Plane ToC(const Plane value) noexcept
{
    return CNA_Plane{{value.Normal.X, value.Normal.Y, value.Normal.Z}, value.D};
}

[[nodiscard]] CNA_Ray ToC(const Ray value) noexcept
{
    return CNA_Ray{
        {value.Position.X, value.Position.Y, value.Position.Z},
        {value.Direction.X, value.Direction.Y, value.Direction.Z}};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3 value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_BoundingBox ToC(const BoundingBox value) noexcept
{
    return CNA_BoundingBox{ToC(value.Min), ToC(value.Max)};
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result StoreOutput(
    TValue* const output,
    const char* const nullMessage,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                nullMessage);
        }
        const TValue result = std::forward<TCallable>(callable)();
        *output = result;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyFormattedString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string destination or required-byte output is invalid.");
        }
        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete formatted value.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result StoreOptionalDistance(
    CNA_Bool* const outHit,
    float* const outDistance,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHit == nullptr || outDistance == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A ray-intersection output is null.");
        }
        const std::optional<float> distance = std::forward<TCallable>(callable)();
        *outHit = distance.has_value() ? CNA_TRUE : CNA_FALSE;
        *outDistance = distance.value_or(0.0F);
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_plane_init(CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [] {
        return ToC(Plane());
    });
}

CNA_Result cna_plane_init_vector4(
    const CNA_Vector4 value,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane(ToNative(value)));
    });
}

CNA_Result cna_plane_init_normal_d(
    const CNA_Vector3 normal,
    const float d,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane(ToNative(normal), d));
    });
}

CNA_Result cna_plane_init_points(
    const CNA_Vector3 a,
    const CNA_Vector3 b,
    const CNA_Vector3 c,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane(ToNative(a), ToNative(b), ToNative(c)));
    });
}

CNA_Result cna_plane_init_abcd(
    const float a,
    const float b,
    const float c,
    const float d,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane(a, b, c, d));
    });
}

CNA_Result cna_plane_dot(
    const CNA_Plane plane,
    const CNA_Vector4 value,
    float* const outValue)
{
    return StoreOutput(outValue, "The float output is null.", [=] {
        return ToNative(plane).Dot(ToNative(value));
    });
}

CNA_Result cna_plane_dot_coordinate(
    const CNA_Plane plane,
    const CNA_Vector3 value,
    float* const outValue)
{
    return StoreOutput(outValue, "The float output is null.", [=] {
        return ToNative(plane).DotCoordinate(ToNative(value));
    });
}

CNA_Result cna_plane_dot_normal(
    const CNA_Plane plane,
    const CNA_Vector3 value,
    float* const outValue)
{
    return StoreOutput(outValue, "The float output is null.", [=] {
        return ToNative(plane).DotNormal(ToNative(value));
    });
}

CNA_Result cna_plane_normalize_in_place(CNA_Plane* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Plane is null.");
        }
        Plane native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_plane_intersects_box(
    const CNA_Plane plane,
    const CNA_BoundingBox box,
    CNA_PlaneIntersectionType* const outIntersection)
{
    return StoreOutput(outIntersection, "The intersection output is null.", [=] {
        return static_cast<CNA_PlaneIntersectionType>(ToNative(plane).Intersects(ToNative(box)));
    });
}

CNA_Result cna_plane_intersects_sphere(
    const CNA_Plane plane,
    const CNA_BoundingSphere sphere,
    CNA_PlaneIntersectionType* const outIntersection)
{
    return StoreOutput(outIntersection, "The intersection output is null.", [=] {
        return static_cast<CNA_PlaneIntersectionType>(ToNative(plane).Intersects(ToNative(sphere)));
    });
}

CNA_Result cna_plane_intersects_frustum(
    const CNA_Plane plane,
    const CNA_BoundingFrustum frustum,
    CNA_PlaneIntersectionType* const outIntersection)
{
    return StoreOutput(outIntersection, "The intersection output is null.", [=] {
        return static_cast<CNA_PlaneIntersectionType>(ToNative(plane).Intersects(ToNative(frustum)));
    });
}

CNA_Result cna_plane_equals(
    const CNA_Plane left,
    const CNA_Plane right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_plane_not_equals(
    const CNA_Plane left,
    const CNA_Plane right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_plane_get_hash_code(
    const CNA_Plane value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_plane_get_string_size(
    const CNA_Plane value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_plane_copy_string(
    const CNA_Plane value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_plane_normalize(
    const CNA_Plane value,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane::Normalize(ToNative(value)));
    });
}

CNA_Result cna_plane_transform_matrix(
    const CNA_Plane value,
    const CNA_Matrix matrix,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane::Transform(ToNative(value), ToNative(matrix)));
    });
}

CNA_Result cna_plane_transform_quaternion(
    const CNA_Plane value,
    const CNA_Quaternion rotation,
    CNA_Plane* const outValue)
{
    return StoreOutput(outValue, "The Plane output is null.", [=] {
        return ToC(Plane::Transform(ToNative(value), ToNative(rotation)));
    });
}

CNA_Result cna_ray_init(CNA_Ray* const outValue)
{
    return StoreOutput(outValue, "The Ray output is null.", [] {
        return ToC(Ray());
    });
}

CNA_Result cna_ray_init_position_direction(
    const CNA_Vector3 position,
    const CNA_Vector3 direction,
    CNA_Ray* const outValue)
{
    return StoreOutput(outValue, "The Ray output is null.", [=] {
        return ToC(Ray(ToNative(position), ToNative(direction)));
    });
}

CNA_Result cna_ray_equals(
    const CNA_Ray left,
    const CNA_Ray right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_ray_not_equals(
    const CNA_Ray left,
    const CNA_Ray right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_ray_get_hash_code(
    const CNA_Ray value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_ray_intersects_box(
    const CNA_Ray ray,
    const CNA_BoundingBox box,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(box));
    });
}

CNA_Result cna_ray_intersects_sphere(
    const CNA_Ray ray,
    const CNA_BoundingSphere sphere,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(sphere));
    });
}

CNA_Result cna_ray_intersects_plane(
    const CNA_Ray ray,
    const CNA_Plane plane,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(plane));
    });
}

CNA_Result cna_ray_intersects_frustum(
    const CNA_Ray ray,
    const CNA_BoundingFrustum frustum,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(ray).Intersects(ToNative(frustum));
    });
}

CNA_Result cna_ray_get_string_size(
    const CNA_Ray value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_ray_copy_string(
    const CNA_Ray value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_bounding_box_init(CNA_BoundingBox* const outValue)
{
    return StoreOutput(outValue, "The BoundingBox output is null.", [] {
        return ToC(BoundingBox());
    });
}

CNA_Result cna_bounding_box_init_min_max(
    const CNA_Vector3 min,
    const CNA_Vector3 max,
    CNA_BoundingBox* const outValue)
{
    return StoreOutput(outValue, "The BoundingBox output is null.", [=] {
        return ToC(BoundingBox(ToNative(min), ToNative(max)));
    });
}

CNA_Result cna_bounding_box_contains_box(
    const CNA_BoundingBox value,
    const CNA_BoundingBox box,
    CNA_ContainmentType* const outContainment)
{
    return StoreOutput(outContainment, "The containment output is null.", [=] {
        return static_cast<CNA_ContainmentType>(ToNative(value).Contains(ToNative(box)));
    });
}

CNA_Result cna_bounding_box_contains_sphere(
    const CNA_BoundingBox value,
    const CNA_BoundingSphere sphere,
    CNA_ContainmentType* const outContainment)
{
    return StoreOutput(outContainment, "The containment output is null.", [=] {
        return static_cast<CNA_ContainmentType>(ToNative(value).Contains(ToNative(sphere)));
    });
}

CNA_Result cna_bounding_box_contains_point(
    const CNA_BoundingBox value,
    const CNA_Vector3 point,
    CNA_ContainmentType* const outContainment)
{
    return StoreOutput(outContainment, "The containment output is null.", [=] {
        return static_cast<CNA_ContainmentType>(ToNative(value).Contains(ToNative(point)));
    });
}

CNA_Result cna_bounding_box_contains_frustum(
    const CNA_BoundingBox value,
    const CNA_BoundingFrustum frustum,
    CNA_ContainmentType* const outContainment)
{
    return StoreOutput(outContainment, "The containment output is null.", [=] {
        return static_cast<CNA_ContainmentType>(ToNative(value).Contains(ToNative(frustum)));
    });
}

CNA_Result cna_bounding_box_copy_corners(
    const CNA_BoundingBox value,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The corner destination or required-count output is invalid.");
        }
        constexpr uint64_t RequiredCount = CNA_BOUNDING_BOX_CORNER_COUNT;
        *outCount = RequiredCount;
        if (capacity < RequiredCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all BoundingBox corners.");
        }
        const std::vector<Vector3> corners = ToNative(value).GetCorners();
        for (uint64_t index = 0U; index < RequiredCount; ++index) {
            destination[index] = ToC(corners[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_bounding_box_intersects_ray(
    const CNA_BoundingBox value,
    const CNA_Ray ray,
    CNA_Bool* const outHit,
    float* const outDistance)
{
    return StoreOptionalDistance(outHit, outDistance, [=] {
        return ToNative(value).Intersects(ToNative(ray));
    });
}

CNA_Result cna_bounding_box_intersects_frustum(
    const CNA_BoundingBox value,
    const CNA_BoundingFrustum frustum,
    CNA_Bool* const outIntersects)
{
    return StoreOutput(outIntersects, "The Boolean output is null.", [=] {
        return ToNative(value).Intersects(ToNative(frustum)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_bounding_box_intersects_sphere(
    const CNA_BoundingBox value,
    const CNA_BoundingSphere sphere,
    CNA_Bool* const outIntersects)
{
    return StoreOutput(outIntersects, "The Boolean output is null.", [=] {
        return ToNative(value).Intersects(ToNative(sphere)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_bounding_box_intersects_box(
    const CNA_BoundingBox value,
    const CNA_BoundingBox box,
    CNA_Bool* const outIntersects)
{
    return StoreOutput(outIntersects, "The Boolean output is null.", [=] {
        return ToNative(value).Intersects(ToNative(box)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_bounding_box_intersects_plane(
    const CNA_BoundingBox value,
    const CNA_Plane plane,
    CNA_PlaneIntersectionType* const outIntersection)
{
    return StoreOutput(outIntersection, "The intersection output is null.", [=] {
        return static_cast<CNA_PlaneIntersectionType>(ToNative(value).Intersects(ToNative(plane)));
    });
}

CNA_Result cna_bounding_box_equals(
    const CNA_BoundingBox left,
    const CNA_BoundingBox right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_bounding_box_not_equals(
    const CNA_BoundingBox left,
    const CNA_BoundingBox right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_bounding_box_get_hash_code(
    const CNA_BoundingBox value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_bounding_box_get_string_size(
    const CNA_BoundingBox value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_bounding_box_copy_string(
    const CNA_BoundingBox value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_bounding_box_create_from_points(
    const CNA_Vector3* const points,
    const uint64_t count,
    CNA_BoundingBox* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The BoundingBox output is null.");
        }
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
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The point array is empty.");
        }
        std::vector<Vector3> nativePoints;
        nativePoints.reserve(byteCount / sizeof(CNA_Vector3));
        for (uint64_t index = 0U; index < count; ++index) {
            nativePoints.push_back(ToNative(points[index]));
        }
        const CNA_BoundingBox result = ToC(BoundingBox::CreateFromPoints(nativePoints));
        *outValue = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_bounding_box_create_from_sphere(
    const CNA_BoundingSphere sphere,
    CNA_BoundingBox* const outValue)
{
    return StoreOutput(outValue, "The BoundingBox output is null.", [=] {
        return ToC(BoundingBox::CreateFromSphere(ToNative(sphere)));
    });
}

CNA_Result cna_bounding_box_create_merged(
    const CNA_BoundingBox original,
    const CNA_BoundingBox additional,
    CNA_BoundingBox* const outValue)
{
    return StoreOutput(outValue, "The BoundingBox output is null.", [=] {
        return ToC(BoundingBox::CreateMerged(ToNative(original), ToNative(additional)));
    });
}
