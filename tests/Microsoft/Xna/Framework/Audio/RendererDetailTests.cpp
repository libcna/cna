// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"

#include <filesystem>
#include <utility>

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

TEST(RendererDetailTest, GetHashCodeConsistentForEqualRendererId)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name B", "same-id");
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(RendererDetailTest, EqualsTrueForSameRendererIdRegardlessOfFriendlyName)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name B", "same-id");
    EXPECT_TRUE(a.Equals(b));
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
    const RendererDetail b = Make("Name B", "same-id");
    const RendererDetail c = Make("Name A", "id-c");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(RendererDetailTest, InequalityOperatorMatchesNegatedEquals)
{
    const RendererDetail a = Make("Name A", "same-id");
    const RendererDetail b = Make("Name B", "same-id");
    const RendererDetail c = Make("Name A", "id-c");
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(RendererDetailTest, ObtainedFromAudioEngineRendererDetails)
{
    const auto path = (std::filesystem::temp_directory_path() / "cna_renderer_detail_test_nonexistent.xgs").string();
    AudioEngine engine(path);

    ASSERT_FALSE(engine.getRendererDetailsProperty().empty());
    const RendererDetail& rd = engine.getRendererDetailsProperty()[0];
    EXPECT_EQ(rd.getFriendlyNameProperty(), "SDL3_mixer");
    EXPECT_EQ(rd.getRendererIdProperty(), "SDL3_mixer");
}
