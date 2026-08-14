// SPDX-License-Identifier: MS-PL

#include "CNA/C/math.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::Point;
using Microsoft::Xna::Framework::Rectangle;

[[nodiscard]] int32_t WrapAdd(const int32_t left, const int32_t right) noexcept
{
    return std::bit_cast<int32_t>(
        std::bit_cast<uint32_t>(left) + std::bit_cast<uint32_t>(right));
}

[[nodiscard]] int32_t WrapSubtract(const int32_t left, const int32_t right) noexcept
{
    return std::bit_cast<int32_t>(
        std::bit_cast<uint32_t>(left) - std::bit_cast<uint32_t>(right));
}

[[nodiscard]] int32_t WrapMultiply(const int32_t left, const int32_t right) noexcept
{
    return std::bit_cast<int32_t>(
        std::bit_cast<uint32_t>(left) * std::bit_cast<uint32_t>(right));
}

[[nodiscard]] Point ToNative(const CNA_Point value)
{
    return Point(value.x, value.y);
}

[[nodiscard]] Rectangle ToNative(const CNA_Rectangle value)
{
    return Rectangle(value.x, value.y, value.width, value.height);
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

[[nodiscard]] int32_t RectangleRight(const CNA_Rectangle value) noexcept
{
    return WrapAdd(value.x, value.width);
}

[[nodiscard]] int32_t RectangleBottom(const CNA_Rectangle value) noexcept
{
    return WrapAdd(value.y, value.height);
}

[[nodiscard]] bool RectangleContainsPoint(
    const CNA_Rectangle value,
    const int32_t x,
    const int32_t y) noexcept
{
    return value.x <= x && x < RectangleRight(value) &&
        value.y <= y && y < RectangleBottom(value);
}

[[nodiscard]] bool RectanglesIntersect(
    const CNA_Rectangle left,
    const CNA_Rectangle right) noexcept
{
    return right.x < RectangleRight(left) && left.x < RectangleRight(right) &&
        right.y < RectangleBottom(left) && left.y < RectangleBottom(right);
}

} // namespace

CNA_Result cna_point_init(CNA_Point* const outValue)
{
    return StoreOutput(outValue, "The point output is null.", [] {
        return CNA_Point{0, 0};
    });
}

CNA_Result cna_point_init_xy(
    const int32_t x,
    const int32_t y,
    CNA_Point* const outValue)
{
    return StoreOutput(outValue, "The point output is null.", [=] {
        return CNA_Point{x, y};
    });
}

CNA_Result cna_point_get_zero(CNA_Point* const outValue)
{
    return cna_point_init(outValue);
}

CNA_Result cna_point_equals(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_point_not_equals(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_point_get_hash_code(
    const CNA_Point value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return ToNative(value).GetHashCode();
    });
}

CNA_Result cna_point_get_string_size(
    const CNA_Point value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_point_copy_string(
    const CNA_Point value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_point_add(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Point* const outValue)
{
    return StoreOutput(outValue, "The point output is null.", [=] {
        return CNA_Point{WrapAdd(left.x, right.x), WrapAdd(left.y, right.y)};
    });
}

CNA_Result cna_point_subtract(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Point* const outValue)
{
    return StoreOutput(outValue, "The point output is null.", [=] {
        return CNA_Point{WrapSubtract(left.x, right.x), WrapSubtract(left.y, right.y)};
    });
}

CNA_Result cna_point_multiply(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Point* const outValue)
{
    return StoreOutput(outValue, "The point output is null.", [=] {
        return CNA_Point{WrapMultiply(left.x, right.x), WrapMultiply(left.y, right.y)};
    });
}

CNA_Result cna_point_divide(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Point* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr || right.x == 0 || right.y == 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The point output is null or a divisor component is zero.");
        }
        constexpr int32_t Minimum = std::numeric_limits<int32_t>::min();
        if ((left.x == Minimum && right.x == -1) ||
            (left.y == Minimum && right.y == -1)) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The point quotient cannot be represented as a signed 32-bit integer.");
        }
        const CNA_Point result{left.x / right.x, left.y / right.y};
        *outValue = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_rectangle_init(CNA_Rectangle* const outValue)
{
    return StoreOutput(outValue, "The rectangle output is null.", [] {
        return CNA_Rectangle{0, 0, 0, 0};
    });
}

CNA_Result cna_rectangle_init_xywh(
    const int32_t x,
    const int32_t y,
    const int32_t width,
    const int32_t height,
    CNA_Rectangle* const outValue)
{
    return StoreOutput(outValue, "The rectangle output is null.", [=] {
        return CNA_Rectangle{x, y, width, height};
    });
}

CNA_Result cna_rectangle_get_empty(CNA_Rectangle* const outValue)
{
    return cna_rectangle_init(outValue);
}

CNA_Result cna_rectangle_get_left(
    const CNA_Rectangle value,
    int32_t* const outEdge)
{
    return StoreOutput(outEdge, "The edge output is null.", [=] {
        return value.x;
    });
}

CNA_Result cna_rectangle_get_right(
    const CNA_Rectangle value,
    int32_t* const outEdge)
{
    return StoreOutput(outEdge, "The edge output is null.", [=] {
        return RectangleRight(value);
    });
}

CNA_Result cna_rectangle_get_top(
    const CNA_Rectangle value,
    int32_t* const outEdge)
{
    return StoreOutput(outEdge, "The edge output is null.", [=] {
        return value.y;
    });
}

CNA_Result cna_rectangle_get_bottom(
    const CNA_Rectangle value,
    int32_t* const outEdge)
{
    return StoreOutput(outEdge, "The edge output is null.", [=] {
        return RectangleBottom(value);
    });
}

CNA_Result cna_rectangle_get_location(
    const CNA_Rectangle value,
    CNA_Point* const outLocation)
{
    return StoreOutput(outLocation, "The point output is null.", [=] {
        return CNA_Point{value.x, value.y};
    });
}

CNA_Result cna_rectangle_set_location(
    CNA_Rectangle* const value,
    const CNA_Point location)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The rectangle is null.");
    }
    value->x = location.x;
    value->y = location.y;
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_rectangle_get_center(
    const CNA_Rectangle value,
    CNA_Point* const outCenter)
{
    return StoreOutput(outCenter, "The point output is null.", [=] {
        return CNA_Point{
            WrapAdd(value.x, value.width / 2),
            WrapAdd(value.y, value.height / 2)
        };
    });
}

CNA_Result cna_rectangle_get_is_empty(
    const CNA_Rectangle value,
    CNA_Bool* const outIsEmpty)
{
    return StoreOutput(outIsEmpty, "The Boolean output is null.", [=] {
        return value.x == 0 && value.y == 0 && value.width == 0 && value.height == 0
            ? CNA_TRUE
            : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_contains_xy(
    const CNA_Rectangle value,
    const int32_t x,
    const int32_t y,
    CNA_Bool* const outContains)
{
    return StoreOutput(outContains, "The Boolean output is null.", [=] {
        return RectangleContainsPoint(value, x, y) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_contains_point(
    const CNA_Rectangle value,
    const CNA_Point point,
    CNA_Bool* const outContains)
{
    return cna_rectangle_contains_xy(value, point.x, point.y, outContains);
}

CNA_Result cna_rectangle_contains_rectangle(
    const CNA_Rectangle value,
    const CNA_Rectangle candidate,
    CNA_Bool* const outContains)
{
    return StoreOutput(outContains, "The Boolean output is null.", [=] {
        return value.x <= candidate.x && RectangleRight(candidate) <= RectangleRight(value) &&
            value.y <= candidate.y && RectangleBottom(candidate) <= RectangleBottom(value)
            ? CNA_TRUE
            : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_offset_point(
    CNA_Rectangle* const value,
    const CNA_Point offset)
{
    return cna_rectangle_offset_xy(value, offset.x, offset.y);
}

CNA_Result cna_rectangle_offset_xy(
    CNA_Rectangle* const value,
    const int32_t offsetX,
    const int32_t offsetY)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The rectangle is null.");
    }
    value->x = WrapAdd(value->x, offsetX);
    value->y = WrapAdd(value->y, offsetY);
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_rectangle_inflate(
    CNA_Rectangle* const value,
    const int32_t horizontalValue,
    const int32_t verticalValue)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The rectangle is null.");
    }
    value->x = WrapSubtract(value->x, horizontalValue);
    value->y = WrapSubtract(value->y, verticalValue);
    value->width = WrapAdd(value->width, WrapMultiply(horizontalValue, 2));
    value->height = WrapAdd(value->height, WrapMultiply(verticalValue, 2));
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_rectangle_equals(
    const CNA_Rectangle left,
    const CNA_Rectangle right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_not_equals(
    const CNA_Rectangle left,
    const CNA_Rectangle right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_get_hash_code(
    const CNA_Rectangle value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return ToNative(value).GetHashCode();
    });
}

CNA_Result cna_rectangle_get_string_size(
    const CNA_Rectangle value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_rectangle_copy_string(
    const CNA_Rectangle value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_rectangle_intersects(
    const CNA_Rectangle left,
    const CNA_Rectangle right,
    CNA_Bool* const outIntersects)
{
    return StoreOutput(outIntersects, "The Boolean output is null.", [=] {
        return RectanglesIntersect(left, right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_rectangle_intersect(
    const CNA_Rectangle left,
    const CNA_Rectangle right,
    CNA_Rectangle* const outValue)
{
    return StoreOutput(outValue, "The rectangle output is null.", [=] {
        if (!RectanglesIntersect(left, right)) {
            return CNA_Rectangle{0, 0, 0, 0};
        }
        const int32_t intersectionRight = std::min(RectangleRight(left), RectangleRight(right));
        const int32_t intersectionLeft = std::max(left.x, right.x);
        const int32_t intersectionTop = std::max(left.y, right.y);
        const int32_t intersectionBottom =
            std::min(RectangleBottom(left), RectangleBottom(right));
        return CNA_Rectangle{
            intersectionLeft,
            intersectionTop,
            WrapSubtract(intersectionRight, intersectionLeft),
            WrapSubtract(intersectionBottom, intersectionTop)
        };
    });
}

CNA_Result cna_rectangle_union(
    const CNA_Rectangle left,
    const CNA_Rectangle right,
    CNA_Rectangle* const outValue)
{
    return StoreOutput(outValue, "The rectangle output is null.", [=] {
        const int32_t unionX = std::min(left.x, right.x);
        const int32_t unionY = std::min(left.y, right.y);
        return CNA_Rectangle{
            unionX,
            unionY,
            WrapSubtract(std::max(RectangleRight(left), RectangleRight(right)), unionX),
            WrapSubtract(std::max(RectangleBottom(left), RectangleBottom(right)), unionY)
        };
    });
}
