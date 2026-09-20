// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <any>
#include <string>
#include "AudioTestScratch.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::RendererDetail;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for RendererDetail's private constructor (see RendererDetail.hpp).
    struct RendererDetailTestAccess
    {
        static RendererDetail Make(std::string friendlyName, std::string rendererId)
        {
            return RendererDetail(std::move(friendlyName), std::move(rendererId));
        }
    };
}

namespace
{
    using Microsoft::Xna::Framework::Audio::RendererDetailTestAccess;

    RendererDetail Make(const std::string& friendlyName, const std::string& rendererId)
    {
        return RendererDetailTestAccess::Make(friendlyName, rendererId);
    }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    // Minimal parseable .xgs: zero categories/variables/rpcs, padded to ParseXgs's 0x50-byte
    // minimum (XactParser.cpp). AudioEngine's ctor now throws FileNotFoundException on a missing
    // settings file (P9-HARDWARE-003, matching FNA's TitleContainer.ReadToPointer), so this test
    // needs a real, existing, parseable fixture rather than a deliberately-nonexistent path.
    std::string MinimalXgsFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            std::vector<uint8_t> data;
            const char magic[4] = { 'X', 'G', 'S', 'F' };
            data.insert(data.end(), magic, magic + 4);
            AppendU16(data, 46); // contentVersion
            AppendU16(data, 0);  // toolVersion
            AppendU16(data, 0);  // unknown
            for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
            data.push_back(0);  // platform

            AppendU16(data, 0); // categoryCount
            AppendU16(data, 0); // variableCount
            AppendU16(data, 0); // blob1Count
            AppendU16(data, 0); // blob2Count
            AppendU16(data, 0); // rpcCount
            AppendU16(data, 0); // dspPresetCount
            AppendU16(data, 0); // dspParameterCount

            for (int i = 0; i < 9; ++i) AppendU32(data, 0); // all nine offset fields

            data.resize(0x50, 0); // ParseXgs requires >= 0x50 bytes total

            auto dir = CnaAudioTest::FixtureRoot() / "cna_renderer_detail_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xgs";
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            return file.string();
        }();
        return path;
    }
}

TEST(RendererDetailTest, ToStringReturnsFriendlyName)
{
    const RendererDetail rd = Make("SDL3 Mixer", "sdl3_mixer");
    EXPECT_EQ(rd.ToString(), "SDL3 Mixer");
}

TEST(RendererDetailTest, PropertiesRoundTrip)
{
    const RendererDetail rd = Make("SDL3 Mixer", "sdl3_mixer");
    EXPECT_EQ(rd.getFriendlyNameProperty(), "SDL3 Mixer");
    EXPECT_EQ(rd.getRendererIdProperty(), "sdl3_mixer");
}

// The friendly name is part of the identity, not decoration. FNA compares RendererId alone
// (FNA/src/Audio/RendererDetail.cs: Equals, GetHashCode and both operators all read only
// RendererId), and CNA followed it until XNA-MISSING-017. Microsoft's operator== is
// `if (left._name == right._name) { return left._id == right._id; } return false;` and its
// GetHashCode XORs the hashes of both fields, substituting 0 for an empty one
// (xna4-decomp/.../Microsoft.Xna.Framework.Xact/Microsoft.Xna.Framework.Audio/RendererDetail.cs).
// XNA is the tie-break, so these three cases assert XNA's comparison; the previous expectations
// are recorded in plans/plan_bindings_upstream.md.
TEST(RendererDetailTest, GetHashCodeCombinesBothFields)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail sameAsA = Make("Name A", "same-id");
    const RendererDetail differentName = Make("Name B", "same-id");

    EXPECT_EQ(a.GetHashCode(), sameAsA.GetHashCode());
    EXPECT_NE(a.GetHashCode(), differentName.GetHashCode());

    // An empty field contributes 0, so a detail with only a name hashes to that name's hash.
    EXPECT_EQ(Make("Name A", "").GetHashCode(), Make("", "Name A").GetHashCode());
}

TEST(RendererDetailTest, EqualsFalseForSameRendererIdButDifferentFriendlyName)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name B", "same-id");
    EXPECT_FALSE(a.Equals(b));
    EXPECT_TRUE(a.Equals(Make("Name A", "same-id")));
}

TEST(RendererDetailTest, EqualsFalseForDifferentRendererId)
{
    const RendererDetail a = Make("Name A", "id-a");
    const RendererDetail c = Make("Name A", "id-c");
    EXPECT_FALSE(a.Equals(c));
}

TEST(RendererDetailTest, EqualityOperatorMatchesEquals)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name A", "same-id");
    const RendererDetail c = Make("Name A", "id-c");
    const RendererDetail d = Make("Name B", "same-id");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
}

TEST(RendererDetailTest, InequalityOperatorMatchesNegatedEquals)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name A", "same-id");
    const RendererDetail c = Make("Name A", "id-c");
    const RendererDetail d = Make("Name B", "same-id");
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a != d);
}

TEST(RendererDetailTest, ObtainedFromAudioEngineRendererDetails)
{
    AudioEngine engine(MinimalXgsFixturePath());

    ASSERT_FALSE(engine.getRendererDetailsProperty().empty());
    const RendererDetail& rd = engine.getRendererDetailsProperty()[0];
#if defined(CNA_AUDIO_PLATFORM_ALSA)
    // plans/plan_x11.md X11-0151: an ALSA build's one backend is CNA's own mixer.
    EXPECT_EQ(rd.getFriendlyNameProperty(), "CNA mixer (ALSA)");
    EXPECT_EQ(rd.getRendererIdProperty(), "ALSA");
#else
    EXPECT_EQ(rd.getFriendlyNameProperty(), "SDL3_mixer");
    EXPECT_EQ(rd.getRendererIdProperty(), "SDL3_mixer");
#endif
}

// ---------------------------------------------------------------------------
// XNA-MISSING-017: RendererDetail.Equals(System.Object).
//
// Microsoft rejects null and any object whose runtime type differs, then compares through
// operator== -- friendly name and renderer id together
// (xna4-decomp/.../Microsoft.Xna.Framework.Xact/Microsoft.Xna.Framework.Audio/RendererDetail.cs).
// ---------------------------------------------------------------------------

TEST(RendererDetailTest, ObjectEqualityMatchesTheTypedComparison)
{
    const RendererDetail first = RendererDetailTestAccess::Make("Speakers", "id-1");
    const RendererDetail same = RendererDetailTestAccess::Make("Speakers", "id-1");
    const RendererDetail otherId = RendererDetailTestAccess::Make("Speakers", "id-2");
    const RendererDetail otherName = RendererDetailTestAccess::Make("Headphones", "id-1");

    ASSERT_TRUE(first.Equals(same));
    EXPECT_TRUE(first.Equals(std::any(same)));
    EXPECT_EQ(first.GetHashCode(), same.GetHashCode());

    // Both halves are compared: a differing id and a differing name each break equality.
    ASSERT_FALSE(first.Equals(otherId));
    EXPECT_FALSE(first.Equals(std::any(otherId)));
    ASSERT_FALSE(first.Equals(otherName));
    EXPECT_FALSE(first.Equals(std::any(otherName)));
}

TEST(RendererDetailTest, ObjectEqualityRejectsNullAndOtherTypes)
{
    const RendererDetail detail = RendererDetailTestAccess::Make("Speakers", "id-1");
    EXPECT_FALSE(detail.Equals(std::any()));
    EXPECT_FALSE(detail.Equals(std::any(0)));
    EXPECT_FALSE(detail.Equals(std::any(std::string("Speakers"))));
}
