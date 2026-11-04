// SPDX-License-Identifier: MS-PL
//
// XNB-18B/XNB-18C: unit tests for the Decimal/DateTime/TimeSpan .xnb readers.

#include <any>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/ArgumentException.hpp"
#include "System/TimeZone.hpp"

using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class DecimalDateTimeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterDecimalDateTimeXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename F>
        std::unique_ptr<ContentReader> MakeReader(F&& write)
        {
            auto ms = std::make_unique<System::IO::MemoryStream>();
            System::IO::BinaryWriter writer(ms.get(), true);
            write(writer);
            writer.Flush();
            auto buf = ms->ToArray();
            streams_.push_back(std::make_unique<System::IO::MemoryStream>(buf.data(), (int32_t)buf.size()));
            return std::make_unique<ContentReader>(nullptr, streams_.back().get(), "test", 5, 'w');
        }

    private:
        std::vector<std::unique_ptr<System::IO::MemoryStream>> streams_;
    };

    template <typename T>
    T ReadViaRegisteredReader(const std::string& canonicalName, ContentReader& reader)
    {
        auto typeReader = ContentTypeReaderManager::CreateReader(canonicalName);
        return std::any_cast<T>(typeReader->ReadUntyped(reader, std::any{}));
    }
}

TEST_F(DecimalDateTimeReaderTest, AllThreeReadersAreRegistered)
{
#if SHARP_RUNTIME_HAS_NATIVE_INT128
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.DecimalReader"));
#endif
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.TimeSpanReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.DateTimeReader"));
}

#if SHARP_RUNTIME_HAS_NATIVE_INT128
TEST_F(DecimalDateTimeReaderTest, DecimalReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) {
        w.Write((int32_t)12345); // lo
        w.Write((int32_t)0);     // mid
        w.Write((int32_t)0);     // hi
        w.Write((int32_t)0x00020000); // flags: scale=2, positive
    });

    const auto d = ReadViaRegisteredReader<System::Decimal>(
        "Microsoft.Xna.Framework.Content.DecimalReader", *reader);

    int32_t lo, mid, hi, flags;
    System::Decimal::GetBits(d, lo, mid, hi, flags);
    EXPECT_EQ(lo, 12345);
    EXPECT_EQ((static_cast<uint32_t>(flags) >> 16) & 0xFF, 2u);
}

TEST_F(DecimalDateTimeReaderTest, DecimalReaderKeepsAllFourWordsInOrderWithTheSign)
{
    // XNA DecimalReader.Read IL_0000-0024: lo, mid, hi, flags into new Decimal(int[]), which
    // stores them as given (mscorlib Decimal.SetBits IL_002f-004c). Distinct words catch a
    // reader that swaps mid and hi or drops the sign bit.
    auto reader = MakeReader([](auto& w) {
        w.Write((int32_t)0x12345678);
        w.Write((int32_t)0x0000ABCD);
        w.Write((int32_t)0x00000001);
        w.Write((int32_t)0x800A0000); // negative, scale 10
    });

    const auto d = ReadViaRegisteredReader<System::Decimal>(
        "Microsoft.Xna.Framework.Content.DecimalReader", *reader);

    int32_t lo, mid, hi, flags;
    System::Decimal::GetBits(d, lo, mid, hi, flags);
    EXPECT_EQ(lo, 0x12345678);
    EXPECT_EQ(mid, 0x0000ABCD);
    EXPECT_EQ(hi, 0x00000001);
    EXPECT_EQ(static_cast<uint32_t>(flags), 0x800A0000u);
}

TEST_F(DecimalDateTimeReaderTest, DecimalReaderRefusesFlagsOutsideTheSignAndScale)
{
    // mscorlib 4.0 Decimal.SetBits IL_0018-002d: (flags & 0x7F00FFFF) != 0 or a scale field
    // above 28 is ArgumentException (IL_0052-0061).
    for (const uint32_t flags : {0x00000001u, 0x00008000u, 0x01000000u, 0x40000000u, 0x001D0000u, 0x00FF0000u})
    {
        SCOPED_TRACE(flags);
        auto reader = MakeReader([flags](auto& w) {
            w.Write((int32_t)1);
            w.Write((int32_t)0);
            w.Write((int32_t)0);
            w.Write(static_cast<int32_t>(flags));
        });
        EXPECT_THROW((void)ReadViaRegisteredReader<System::Decimal>(
                         "Microsoft.Xna.Framework.Content.DecimalReader", *reader),
                     System::ArgumentException);
    }
}
#endif

TEST_F(DecimalDateTimeReaderTest, TimeSpanReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int64_t)123456789LL); });

    const auto ts = ReadViaRegisteredReader<System::TimeSpan>(
        "Microsoft.Xna.Framework.Content.TimeSpanReader", *reader);

    EXPECT_EQ(ts.getTicksProperty(), 123456789LL);
}

TEST_F(DecimalDateTimeReaderTest, DateTimeReaderPreservesUtcKindAndTicks)
{
    const uint64_t ticks = 636000000000000000ULL; // an arbitrary real-looking tick count
    const uint64_t kind = 1ULL; // DateTimeKind.Utc, packed into the top 2 bits
    const uint64_t packed = ticks | (kind << 62);

    auto reader = MakeReader([packed](auto& w) { w.Write(packed); });

    const auto dt = ReadViaRegisteredReader<System::DateTime>(
        "Microsoft.Xna.Framework.Content.DateTimeReader", *reader);

    EXPECT_EQ(dt.getTicksProperty(), static_cast<int64_t>(ticks));
    EXPECT_EQ(dt.getKindProperty(), System::DateTimeKind::Utc);
}

TEST_F(DecimalDateTimeReaderTest, DateTimeReaderConvertsLocalPayloadFromUtc)
{
    constexpr std::uint64_t ticks = 639040752000000000ULL;
    auto reader = MakeReader([](auto& w) { w.Write(ticks | (std::uint64_t{2} << 62)); });
    const auto read = ReadViaRegisteredReader<System::DateTime>(
        "Microsoft.Xna.Framework.Content.DateTimeReader", *reader);
    const auto expected = System::DateTime(static_cast<SharpRuntime::longcs>(ticks), System::DateTimeKind::Utc)
        .ToLocalTime(System::TimeZone::CurrentTimeZone());
    EXPECT_EQ(read.getKindProperty(), System::DateTimeKind::Local);
    EXPECT_EQ(read.getTicksProperty(), expected.getTicksProperty());
}

TEST_F(DecimalDateTimeReaderTest, DateTimeReaderRejectsUndefinedKind)
{
    auto reader = MakeReader([](auto& w) { w.Write(std::uint64_t{3} << 62); });
    EXPECT_THROW((void)ReadViaRegisteredReader<System::DateTime>(
        "Microsoft.Xna.Framework.Content.DateTimeReader", *reader), System::ArgumentException);
}
