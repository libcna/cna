// SPDX-License-Identifier: MS-PL

#include "CNA/C/math.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
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
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::StoreOutput;
using CNA::C::Detail::ToCBool;
using Microsoft::Xna::Framework::MathHelper;
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

CNA_Result cna_math_closest_msaa_power(
    const int32_t value,
    int32_t* const outValue)
{
    if (outValue == nullptr || value < 0) {
        return InvalidArgument("The MSAA input is negative or its output is null.");
    }
    if (value <= 1) {
        *outValue = 0;
        return CNA_RESULT_SUCCESS;
    }

    uint32_t power = static_cast<uint32_t>(value);
    power |= power >> 1U;
    power |= power >> 2U;
    power |= power >> 4U;
    power |= power >> 8U;
    power |= power >> 16U;
    power -= power >> 1U;
    *outValue = static_cast<int32_t>(power);
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_point_get_zero(CNA_Point* const outValue)
{
    return cna_point_init(outValue);
}

CNA_Result cna_point_divide(
    const CNA_Point left,
    const CNA_Point right,
    CNA_Point* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr || right.x == 0 || right.y == 0) {
            return InvalidArgument("The point output is null or a divisor component is zero.");
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

CNA_Result cna_rectangle_get_empty(CNA_Rectangle* const outValue)
{
    return cna_rectangle_init(outValue);
}

CNA_Result cna_rectangle_set_location(
    CNA_Rectangle* const value,
    const CNA_Point location)
{
    if (value == nullptr) {
        return InvalidArgument("The rectangle is null.");
    }
    value->x = location.x;
    value->y = location.y;
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_rectangle_contains_point(
    const CNA_Rectangle value,
    const CNA_Point point,
    CNA_Bool* const outContains)
{
    return cna_rectangle_contains_xy(value, point.x, point.y, outContains);
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
        return InvalidArgument("The rectangle is null.");
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
        return InvalidArgument("The rectangle is null.");
    }
    value->x = WrapSubtract(value->x, horizontalValue);
    value->y = WrapSubtract(value->y, verticalValue);
    value->width = WrapAdd(value->width, WrapMultiply(horizontalValue, 2));
    value->height = WrapAdd(value->height, WrapMultiply(verticalValue, 2));
    return CNA_RESULT_SUCCESS;
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

// The routes generated from modules/c-api/spec/Math.toml.
#include "generated/Math.inc"
