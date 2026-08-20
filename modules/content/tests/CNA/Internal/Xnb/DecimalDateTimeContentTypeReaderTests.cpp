// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-18B/XNB-18C: unit tests for the Decimal/DateTime/TimeSpan .xnb readers.

#include <any>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

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
#endif

TEST_F(DecimalDateTimeReaderTest, TimeSpanReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int64_t)123456789LL); });

    const auto ts = ReadViaRegisteredReader<System::TimeSpan>(
        "Microsoft.Xna.Framework.Content.TimeSpanReader", *reader);

    EXPECT_EQ(ts.getTicksProperty(), 123456789LL);
}

TEST_F(DecimalDateTimeReaderTest, DateTimeReaderExtractsTicksMaskingOutKindBits)
{
    const uint64_t ticks = 636000000000000000ULL; // an arbitrary real-looking tick count
    const uint64_t kind = 1ULL; // DateTimeKind.Utc, packed into the top 2 bits
    const uint64_t packed = ticks | (kind << 62);

    auto reader = MakeReader([packed](auto& w) { w.Write(packed); });

    const auto dt = ReadViaRegisteredReader<System::DateTime>(
        "Microsoft.Xna.Framework.Content.DateTimeReader", *reader);

    EXPECT_EQ(dt.getTicksProperty(), static_cast<int64_t>(ticks));
}
