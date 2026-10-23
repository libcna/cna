// SPDX-License-Identifier: MS-PL
// ISSUE-029: expectations measured by SoundEffectStreamOracle.cs against genuine XNA4.
// Native counterparts of the measured stream contract for the System::IO::Stream& overload.
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NotSupportedException.hpp"

namespace
{
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
    using SharpRuntime::bytecs;
    using SharpRuntime::intcs;

    std::vector<bytecs> Wave()
    {
        // Eight mono PCM16 frames at 8000 Hz, identical to the managed oracle.
        std::vector<bytecs> bytes{
            'R','I','F','F', 52,0,0,0, 'W','A','V','E', 'f','m','t',' ',
            16,0,0,0, 1,0, 1,0, 0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0,16,0,
            'd','a','t','a', 16,0,0,0};
        bytes.resize(60, 0);
        return bytes;
    }

    class TrackingStream final : public System::IO::MemoryStream
    {
    public:
        intcs maximumRead = std::numeric_limits<intcs>::max();
        bool refuseLength = false;
        bool readable = true;
        int closeCount = 0;
        int readCount = 0;
        intcs bytesRead = 0;

        explicit TrackingStream(const std::vector<bytecs>& bytes)
            : MemoryStream(bytes.data(), static_cast<intcs>(bytes.size())) { }
        intcs Read(bytecs buffer[], intcs offset, intcs count) override
        {
            ++readCount;
            const auto result = MemoryStream::Read(buffer, offset, std::min(count, maximumRead));
            bytesRead += result;
            return result;
        }
        intcs getLengthProperty() const override
        {
            if (refuseLength) throw System::NotSupportedException("length witness");
            return MemoryStream::getLengthProperty();
        }
        bool getCanReadProperty() const override
        {
            return readable && MemoryStream::getCanReadProperty();
        }
        void Close() override
        {
            ++closeCount;
            MemoryStream::Close();
        }
    };

    template<class Expected>
    void ExpectExactFailure(TrackingStream& stream)
    {
        try
        {
            std::unique_ptr<SoundEffect> result(SoundEffect::FromStream(stream));
            FAIL() << "Expected refusal before native sound creation";
        }
        catch (const Expected& error)
        {
            EXPECT_EQ(typeid(error), typeid(Expected));
        }
        catch (...)
        {
            FAIL() << "Wrong exception family";
        }
        EXPECT_EQ(stream.closeCount, 0);
    }

    class SoundEffectXnaShortReadTest : public ::testing::TestWithParam<intcs> { };

    TEST_P(SoundEffectXnaShortReadTest, CompletesEveryReadAndClosesOnlyAfterSuccessfulParse)
    {
        TrackingStream stream(Wave());
        stream.maximumRead = GetParam();
        std::unique_ptr<SoundEffect> sound;
        ASSERT_NO_THROW(sound.reset(SoundEffect::FromStream(stream)));
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(stream.closeCount, 1);
        EXPECT_EQ(stream.bytesRead, 60);
        if (GetParam() == 1) EXPECT_EQ(stream.readCount, 60);
        EXPECT_EQ(sound->getDurationProperty().getTicksProperty(), 10000);
        auto instance = sound->CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 8u);
    }

    INSTANTIATE_TEST_SUITE_P(WholeAndShortReads, SoundEffectXnaShortReadTest,
        ::testing::Values(std::numeric_limits<intcs>::max(), 1, 3));

    TEST(SoundEffectXnaStreamTest, EmptySourceNamesSourceAndStaysOpen)
    {
        TrackingStream stream({});
        try
        {
            std::unique_ptr<SoundEffect> sound(SoundEffect::FromStream(stream));
            FAIL() << "Empty stream must be refused";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentNullException));
            EXPECT_EQ(error.getParamNameProperty(), "source");
        }
        EXPECT_EQ(stream.closeCount, 0);
    }

    TEST(SoundEffectXnaStreamTest, UnsupportedLengthPropagatesWithoutClosing)
    {
        TrackingStream stream(Wave());
        stream.refuseLength = true;
        ExpectExactFailure<System::NotSupportedException>(stream);
        EXPECT_EQ(stream.readCount, 0);
    }

    TEST(SoundEffectXnaStreamTest, UnreadableSourceIsRefusedWithoutClosing)
    {
        TrackingStream stream(Wave());
        stream.readable = false;
        ExpectExactFailure<System::ArgumentException>(stream);
        EXPECT_EQ(stream.readCount, 0);
    }

    TEST(SoundEffectXnaStreamTest, BadRiffHeaderIsRefusedWithoutClosing)
    {
        auto bytes = Wave();
        bytes[0] = 0;
        TrackingStream stream(bytes);
        ExpectExactFailure<System::InvalidOperationException>(stream);
    }

    TEST(SoundEffectXnaStreamTest, TruncatedHeaderIsRefusedWithoutClosing)
    {
        TrackingStream stream({'R', 'I'});
        ExpectExactFailure<System::IO::EndOfStreamException>(stream);
    }

    TEST(SoundEffectXnaStreamTest, NonPcmFormatIsRefusedWithoutClosing)
    {
        auto bytes = Wave();
        bytes[20] = 3;
        TrackingStream stream(bytes);
        ExpectExactFailure<System::ArgumentException>(stream);
    }
}
