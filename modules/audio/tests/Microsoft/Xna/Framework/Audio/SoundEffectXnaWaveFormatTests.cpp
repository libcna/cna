// SPDX-License-Identifier: MS-PL
// ISSUE-029: native counterparts of the 71 measured SoundEffectWaveChunksOracle checks.
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace
{
    using Bytes = std::vector<SharpRuntime::bytecs>;
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;

    void U16(Bytes& bytes, unsigned value)
    {
        bytes.push_back(static_cast<SharpRuntime::bytecs>(value));
        bytes.push_back(static_cast<SharpRuntime::bytecs>(value >> 8));
    }
    void U32(Bytes& bytes, unsigned value)
    {
        U16(bytes, value); U16(bytes, value >> 16);
    }
    Bytes Chunk(const std::string& tag, const Bytes& data)
    {
        Bytes bytes(tag.begin(), tag.end());
        U32(bytes, static_cast<unsigned>(data.size()));
        bytes.insert(bytes.end(), data.begin(), data.end());
        if (data.size() % 2 != 0) bytes.push_back(0);
        return bytes;
    }
    Bytes Format(unsigned channels, unsigned bits, unsigned rate, unsigned align)
    {
        Bytes bytes;
        U16(bytes, 1); U16(bytes, channels); U32(bytes, rate); U32(bytes, rate * align);
        U16(bytes, align); U16(bytes, bits);
        return Chunk("fmt ", bytes);
    }
    Bytes Wave(const std::vector<Bytes>& chunks)
    {
        unsigned size = 4;
        for (const auto& chunk : chunks) size += static_cast<unsigned>(chunk.size());
        Bytes bytes{'R','I','F','F'};
        U32(bytes, size);
        bytes.insert(bytes.end(), {'W','A','V','E'});
        for (const auto& chunk : chunks) bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return bytes;
    }
    Bytes Smpl(unsigned type, unsigned start, unsigned end)
    {
        Bytes bytes(28, 0);
        for (unsigned value : {1u, 0u, 0u, type, start, end, 0u, 0u}) U32(bytes, value);
        return Chunk("smpl", bytes);
    }
    Bytes Wsmp(unsigned start, unsigned length)
    {
        Bytes bytes(16, 0);
        for (unsigned value : {1u, 16u, 0u, start, length}) U32(bytes, value);
        return Chunk("wsmp", bytes);
    }
    class TrackedStream final : public System::IO::MemoryStream
    {
    public:
        int closes = 0;
        explicit TrackedStream(const Bytes& bytes)
            : MemoryStream(bytes.data(), static_cast<SharpRuntime::intcs>(bytes.size())) { }
        void Close() override { ++closes; MemoryStream::Close(); }
    };

    struct Accepted
    {
        std::string name;
        Bytes bytes;
        unsigned loopStart, loopLength;
        std::int64_t ticks;
    };
    std::vector<Accepted> AcceptedCases()
    {
        const auto pcm = Format(1, 16, 8000, 2), data = Chunk("data", Bytes(16, 0));
        return {
            {"Pcm8Mono", Wave({Format(1,8,8000,1), data}), 0, 16, 20000},
            {"Pcm8Stereo", Wave({Format(2,8,8000,2), data}), 0, 8, 10000},
            {"StereoHalfMillisecond", Wave({Format(2,16,8000,4), data}), 0, 4, 10000},
            {"Rate48000RoundsToZero", Wave({Format(1,16,48000,2), data}), 0, 8, 0},
            {"OddUnknownChunk", Wave({Chunk("JUNK", Bytes(3,0)), pcm, data}), 0, 8, 10000},
            {"PartialFinalFrame", Wave({pcm, Chunk("data", Bytes(17,0))}), 0, 8, 10000},
            {"SmplInclusiveEnd", Wave({pcm, data, Smpl(0,2,5)}), 2, 4, 10000},
            {"SmplUnsupportedType", Wave({pcm, data, Smpl(1,2,5)}), 0, 8, 10000},
            {"WsmpPreferred", Wave({pcm, data, Smpl(0,2,5), Wsmp(1,3)}), 1, 3, 10000},
            {"InvalidWsmpPreventsSmplFallback", Wave({pcm, data, Smpl(0,2,5), Wsmp(20,3)}), 0, 8, 10000},
            {"IncompleteTrailingChunk", Wave({pcm, data, Bytes{'J','U','N','K'}}), 0, 8, 10000}
        };
    }
    class SoundEffectXnaWaveFormatTest : public ::testing::TestWithParam<Accepted> { };
    TEST_P(SoundEffectXnaWaveFormatTest, PreservesMeasuredFormatLoopDurationAndClosure)
    {
        const auto& expected = GetParam();
        TrackedStream stream(expected.bytes);
        std::unique_ptr<SoundEffect> sound;
        ASSERT_NO_THROW(sound.reset(SoundEffect::FromStream(stream)));
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(stream.closes, 1);
        EXPECT_EQ(sound->getDurationProperty().getTicksProperty(), expected.ticks);
        auto instance = sound->CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), expected.loopStart);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), expected.loopLength);
    }
    INSTANTIATE_TEST_SUITE_P(GenuineWaveFormats, SoundEffectXnaWaveFormatTest,
        ::testing::ValuesIn(AcceptedCases()),
        [](const ::testing::TestParamInfo<Accepted>& info) { return info.param.name; });

    enum class Refusal { Argument, EndOfStream, InvalidOperation };
    struct Rejected { std::string name; Bytes bytes; Refusal refusal; };
    std::vector<Rejected> RejectedCases()
    {
        const auto pcm = Format(1,16,8000,2), data = Chunk("data", Bytes(16,0));
        auto wrongLength = Wave({pcm, data});
        wrongLength[4] = 0;
        return {
            {"DataPrecedesFormat", Wave({data,pcm}), Refusal::Argument},
            {"ThreeChannels", Wave({Format(3,16,8000,6),data}), Refusal::Argument},
            {"RateBelowRange", Wave({Format(1,16,7999,2),data}), Refusal::Argument},
            {"RateAboveRange", Wave({Format(1,16,48001,2),data}), Refusal::Argument},
            {"Pcm24", Wave({Format(1,24,8000,3),data}), Refusal::Argument},
            {"WrongBlockAlignment", Wave({Format(1,16,8000,1),data}), Refusal::Argument},
            {"TruncatedSmpl", Wave({pcm,data,Chunk("smpl",Bytes(20,0))}), Refusal::EndOfStream},
            {"RiffLengthMismatch", std::move(wrongLength), Refusal::InvalidOperation}
        };
    }
    class SoundEffectXnaWaveRefusalTest : public ::testing::TestWithParam<Rejected> { };
    TEST_P(SoundEffectXnaWaveRefusalTest, PreservesExactExceptionAndLeavesSourceOpen)
    {
        const auto& expected = GetParam();
        TrackedStream stream(expected.bytes);
        try
        {
            std::unique_ptr<SoundEffect> sound(SoundEffect::FromStream(stream));
            FAIL() << "Invalid WAV must be refused";
        }
        catch (const std::exception& error)
        {
            switch (expected.refusal)
            {
            case Refusal::Argument: EXPECT_EQ(typeid(error), typeid(System::ArgumentException)); break;
            case Refusal::EndOfStream: EXPECT_EQ(typeid(error), typeid(System::IO::EndOfStreamException)); break;
            case Refusal::InvalidOperation: EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException)); break;
            }
        }
        EXPECT_EQ(stream.closes, 0);
    }
    INSTANTIATE_TEST_SUITE_P(GenuineWaveRefusals, SoundEffectXnaWaveRefusalTest,
        ::testing::ValuesIn(RejectedCases()),
        [](const ::testing::TestParamInfo<Rejected>& info) { return info.param.name; });
}
