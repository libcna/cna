// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int High = 2147483644;
    constexpr int Low = (-2147483647 - 1);
    static_assert(std::numeric_limits<int>::max() == 2147483647);
    static_assert(std::numeric_limits<int>::min() == Low);

    VertexElement Element(int offset, VertexElementFormat format = VertexElementFormat::Vector4,
                          VertexElementUsage usage = VertexElementUsage::Normal, int index = 3)
    {
        return VertexElement(offset, format, usage, index);
    }

    // Routes 0/1 infer the stride; routes 2/3 retain the explicit stride.
    VertexDeclaration Single(int route, int stride, const VertexElement& e)
    {
        switch (route)
        {
            case 0: return VertexDeclaration(std::vector<VertexElement>{e});
            case 1: return VertexDeclaration({e});
            case 2: return VertexDeclaration(stride, std::vector<VertexElement>{e});
            default: return VertexDeclaration(stride, {e});
        }
    }

    VertexDeclaration Pair(int route, int stride, const VertexElement& a, const VertexElement& b)
    {
        switch (route)
        {
            case 0: return VertexDeclaration(std::vector<VertexElement>{a, b});
            case 1: return VertexDeclaration({a, b});
            case 2: return VertexDeclaration(stride, std::vector<VertexElement>{a, b});
            default: return VertexDeclaration(stride, {a, b});
        }
    }

    template<class Action>
    void StrideError(Action&& action)
    {
        try { action(); ADD_FAILURE() << "expected vertexStride refusal"; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "vertexStride");
            // Refresh Validate IL_0004-000e: BCL parameter-only constructor.
            const auto message = error.getMessageProperty();
            EXPECT_TRUE(message == "Specified argument was out of the range of valid values.\nParameter name: vertexStride" ||
                        message == "Specified argument was out of the range of valid values. (Parameter 'vertexStride')") << message;
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    }

    template<class Action>
    void LayoutError(Action&& action, const char* message)
    {
        try { action(); ADD_FAILURE() << "expected structural refusal"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_TRUE(error.getParamNameProperty().empty());
            EXPECT_EQ(error.getMessageProperty(), message);
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    }

    constexpr const char* Alignment =
        "Invalid VertexDeclaration. Vertex stride and VertexElement.Offset must be multiples of four.";
    constexpr const char* Outside =
        "Invalid VertexDeclaration. Element Normal3 does not fit within the specified vertex stride.";
}

// Refresh GetTypeSize IL_003a-0053: sizes 4,8,12,16,4,4,4,8,4,8,4,8.
// GetVertexStride IL_0023-002a: unchecked add followed by SIGNED max with zero.
// High + each size wraps to -2147483648,-2147483644,-2147483640,-2147483636;
// INT_MAX + each size is also negative. Both inferred routes therefore validate stride 0.
TEST(VertexDeclarationArithmeticTest, EveryFormatOverflowInfersZeroBeforeElementValidation)
{
    for (int ordinal = 0; ordinal < 12; ++ordinal)
        for (int offset : {High, 2147483647})
            for (int route : {0, 1})
            {
                SCOPED_TRACE(::testing::Message() << ordinal << '/' << offset << '/' << route);
                StrideError([&] { (void)Single(route, 0, Element(offset, static_cast<VertexElementFormat>(ordinal))); });
            }
}

// Refresh GetVertexStride IL_0023-002a, Validate IL_00c6-00cc/0242-0248:
// max(0, wrapped negative end, 0+4) = 4; high offset >= negative end skips
// the byte-owner loop completely. Exercise both element orders and all constructor routes.
TEST(VertexDeclarationArithmeticTest, WrappedElementsLeaveTheRealMaximumAndOccupyNoBytes)
{
    for (int ordinal = 0; ordinal < 12; ++ordinal)
        for (int route = 0; route < 4; ++route)
            for (bool reversed : {false, true})
            {
                SCOPED_TRACE(::testing::Message() << ordinal << '/' << route << '/' << reversed);
                const auto high = Element(High, static_cast<VertexElementFormat>(ordinal));
                const auto real = Element(0, VertexElementFormat::Single, VertexElementUsage::Position, 0);
                const auto first = reversed ? real : high;
                const auto second = reversed ? high : real;
                const auto declaration = Pair(route, 4, first, second);
                EXPECT_EQ(declaration.getVertexStrideProperty(), 4);
                EXPECT_EQ(declaration.GetVertexElements(), (std::vector<VertexElement>{first, second}));
            }
}

// Refresh Validate IL_00c1-0127/0242-0248: the wrapped end fits a positive stride;
// offset is aligned and the loop is empty, even when the format size exceeds stride 4.
TEST(VertexDeclarationArithmeticTest, ExplicitSmallStrideAcceptsEveryWrappedFormat)
{
    for (int ordinal = 0; ordinal < 12; ++ordinal)
        for (int route : {2, 3})
        {
            SCOPED_TRACE(::testing::Message() << ordinal << '/' << route);
            const auto e = Element(High, static_cast<VertexElementFormat>(ordinal));
            const auto declaration = Single(route, 4, e);
            EXPECT_EQ(declaration.getVertexStrideProperty(), 4);
            EXPECT_EQ(declaration.GetVertexElements(), (std::vector<VertexElement>{e}));
        }
}

// Refresh Validate IL_00ca/00cc uses signed wrapped end, then offset alignment.
// Vector4 ends: 2147483628+16=2147483644 (outside); 2147483632+16=INT_MIN;
// subsequent aligned offsets give INT_MIN+4,+8,+12 (accepted); INT_MAX+16 is
// -2147483633 (passes bounds, then fails alignment). All allocations are only stride 16.
TEST(VertexDeclarationArithmeticTest, ExplicitBoundarySeparatesOutsideWrappedAndMisaligned)
{
    for (int route : {2, 3})
    {
        SCOPED_TRACE(route);
        LayoutError([&] { (void)Single(route, 16, Element(2147483628)); }, Outside);
        for (int offset : {2147483632, 2147483636, 2147483640, High})
        {
            const auto e = Element(offset);
            const auto declaration = Single(route, 16, e);
            EXPECT_EQ(declaration.getVertexStrideProperty(), 16);
            EXPECT_EQ(declaration.GetVertexElements(), (std::vector<VertexElement>{e}));
        }
        LayoutError([&] { (void)Single(route, 16, Element(2147483647)); }, Alignment);
    }
}

// Refresh GetVertexStride IL_0023-002a and Validate IL_0000-001e/00c1-00c4:
// negative offsets do not wrap upward when adding these positive sizes. An inferred
// 3/15 fails stride alignment; 0 fails positivity; 4/12 passes stride then fails offset.
TEST(VertexDeclarationArithmeticTest, NegativeOffsetsKeepSignedMaximumAndDiagnosticOrder)
{
    struct Row { int offset; VertexElementFormat format; int stride; const char* error; };
    const Row rows[]{
        {-1, VertexElementFormat::Single, 3, Alignment},
        {-4, VertexElementFormat::Single, 0, nullptr},
        {Low, VertexElementFormat::Single, 0, nullptr},
        {Low + 4, VertexElementFormat::Single, 0, nullptr},
        {-1, VertexElementFormat::Vector4, 15, Alignment},
        {-4, VertexElementFormat::Vector4, 12, Outside},
        {-12, VertexElementFormat::Vector4, 4, Outside},
        {-16, VertexElementFormat::Vector4, 0, nullptr},
        {-17, VertexElementFormat::Vector4, 0, nullptr},
    };
    for (const auto& row : rows)
        for (int route = 0; route < 4; ++route)
        {
            SCOPED_TRACE(::testing::Message() << row.offset << '/' << row.stride << '/' << route);
            const auto e = Element(row.offset, row.format);
            if (route < 2 && row.error == nullptr)
                StrideError([&] { (void)Single(route, 4, e); });
            else
                LayoutError([&] { (void)Single(route, 4, e); }, route < 2 ? row.error : Outside);
        }
}

// Refresh Validate: stride IL_0000-001e, usage IL_0065-00c0, wrapped bounds
// IL_00c1-0116, alignment IL_0117-0127, duplicate IL_0128-01ab, in that order.
TEST(VertexDeclarationArithmeticTest, WrappedBoundsPreserveStrideUsageAlignmentAndDuplicatePriority)
{
    for (int route : {2, 3})
    {
        SCOPED_TRACE(route);
        for (int stride : {Low, Low + 4, -1, 0})
            StrideError([&] { (void)Single(route, stride, Element(High)); });
        LayoutError([&] { (void)Single(route, 2147483647, Element(High)); }, Alignment);
        LayoutError([&] { (void)Single(route, 4, Element(2147483647, VertexElementFormat::Vector4,
                                                               static_cast<VertexElementUsage>(13))); },
                    "Invalid VertexDeclaration. Usage 13 is out of range.");
        LayoutError([&] { (void)Single(route, 4, Element(2147483647)); }, Alignment);
    }
    for (int route = 0; route < 4; ++route)
        LayoutError([&] { (void)Pair(route, 4,
            Element(0, VertexElementFormat::Single, VertexElementUsage::Normal, 3), Element(High)); },
            "Invalid VertexDeclaration. Duplicate element Normal3.");
}

// Refresh GetTypeSize IL_0054-0055 returns 0: High+0 is positive, not an
// overflowing extent. Explicit stride 4 rejects it; inferred INT_MAX fails alignment.
TEST(VertexDeclarationArithmeticTest, UnknownFormatsDoNotAcquireAnArtificialOverflowExtent)
{
    for (int ordinal : {-1, 12})
    {
        SCOPED_TRACE(ordinal);
        for (int route : {2, 3})
            LayoutError([&] { (void)Single(route, 4, Element(High, static_cast<VertexElementFormat>(ordinal))); }, Outside);
        for (int route : {0, 1})
            LayoutError([&] { (void)Single(route, 0, Element(2147483647, static_cast<VertexElementFormat>(ordinal))); }, Alignment);
    }
}

// Refresh Validate IL_0242-0248 skips ONLY the wrapped element's empty byte range.
// The real Vector2 [0,8) still owns byte 4 and overlaps Color2's Single [4,8).
TEST(VertexDeclarationArithmeticTest, OrdinaryOverlapIsStillDetectedAfterAnEmptyWrappedRange)
{
    const auto high = Element(High);
    const auto real = Element(0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0);
    const auto overlap = Element(4, VertexElementFormat::Single, VertexElementUsage::Color, 2);
    const auto verify = [&](auto&& construct) {
        LayoutError(construct, "Invalid VertexDeclaration. Elements Position0 and Color2 are overlapping.");
    };
    verify([&] { VertexDeclaration d({high, real, overlap}); });
    verify([&] { VertexDeclaration d(std::vector<VertexElement>{high, real, overlap}); });
    verify([&] { VertexDeclaration d(8, {high, real, overlap}); });
    verify([&] { VertexDeclaration d(8, std::vector<VertexElement>{high, real, overlap}); });
}
