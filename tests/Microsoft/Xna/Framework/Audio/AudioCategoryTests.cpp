// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;

namespace
{
    void AppendU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

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

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
    }

    void AppendCategory(std::vector<uint8_t>& buf)
    {
        AppendU8(buf, 0xFF);   // instanceLimit
        AppendU16(buf, 0);     // fadeInMS
        AppendU16(buf, 0);     // fadeOutMS
        AppendU8(buf, 0);      // maxInstanceBehavior (skip)
        AppendU16(buf, 0xFFFF);// parentIndex (none)
        AppendU8(buf, 0xFF);   // volume byte
        AppendU8(buf, 0);      // visibility
    }

    // Minimal .xgs fixture with two categories ("Default", "Combat") and no variables.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t categoryCount    = 2;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryCount * categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset; // variableCount == 0, nothing stored there
        const std::string name0 = "Default";
        const std::string name1 = "Combat";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(name0.size()) + 1 +
            static_cast<uint32_t>(name1.size()) + 1;

        std::vector<uint8_t> data;

        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, categoryCount); // categoryCount
        AppendU16(data, 0); // variableCount
        AppendU16(data, 0); // blob1Count
        AppendU16(data, 0); // blob2Count
        AppendU16(data, 0); // rpcCount
        AppendU16(data, 0); // dspPresetCount
        AppendU16(data, 0); // dspParameterCount

        AppendU32(data, categoryOffset);
        AppendU32(data, variableOffset);
        AppendU32(data, 0); // blob1Offset
        AppendU32(data, 0); // categoryNameIndexOffset
        AppendU32(data, 0); // blob2Offset
        AppendU32(data, 0); // variableNameIndexOffset
        AppendU32(data, categoryNameOffset);
        AppendU32(data, variableNameOffset);

        AppendCategory(data); // "Default"
        AppendCategory(data); // "Combat"

        AppendCStr(data, name0);
        AppendCStr(data, name1);

        return data;
    }

    AudioEngine& SharedEngine()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_category_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xgs";
            const auto bytes = BuildXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        static AudioEngine engine(path);
        return engine;
    }
}

// ===================== Name =====================

TEST(AudioCategoryTest, NameReturnsGivenName)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_EQ(cat.getNameProperty(), "Default");
}

// ===================== Pause / Resume / SetVolume / Stop =====================

TEST(AudioCategoryTest, PauseDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Pause());
}

TEST(AudioCategoryTest, ResumeDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Resume());
}

TEST(AudioCategoryTest, SetVolumeDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.SetVolume(0.5f));
}

TEST(AudioCategoryTest, StopAsAuthoredDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Stop(AudioStopOptions::AsAuthored));
}

TEST(AudioCategoryTest, StopImmediateDoesNotThrow)
{
    AudioCategory cat = SharedEngine().GetCategory("Default");
    EXPECT_NO_THROW(cat.Stop(AudioStopOptions::Immediate));
}

// ===================== Equals / GetHashCode / operators =====================

TEST(AudioCategoryTest, EqualsTrueForSameName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    EXPECT_TRUE(a.Equals(b));
}

TEST(AudioCategoryTest, EqualsFalseForDifferentName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_FALSE(a.Equals(c));
}

TEST(AudioCategoryTest, GetHashCodeConsistentForSameName)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(AudioCategoryTest, EqualityOperatorMatchesEquals)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(AudioCategoryTest, InequalityOperatorMatchesNegatedEquals)
{
    AudioCategory a = SharedEngine().GetCategory("Default");
    AudioCategory b = SharedEngine().GetCategory("Default");
    AudioCategory c = SharedEngine().GetCategory("Combat");
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}
