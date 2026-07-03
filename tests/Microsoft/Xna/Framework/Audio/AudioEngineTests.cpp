// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;

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

    void AppendF32(std::vector<uint8_t>& buf, float v)
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

    // Minimal .xgs fixture with one category ("Default") and one variable ("Volume",
    // initial value 0.5) — enough to exercise AudioEngine's valid-name lookup paths.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        constexpr uint32_t headerSize        = 65;
        constexpr uint32_t categoryDataSize  = 10;
        constexpr uint32_t variableDataSize  = 13;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + variableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset = categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "Volume";

        std::vector<uint8_t> data;

        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, 1); // categoryCount
        AppendU16(data, 1); // variableCount
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

        // Category: instanceLimit, fadeInMS, fadeOutMS, maxInstanceBehavior(skip), parentIndex, volume, visibility
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0xFFFF);
        AppendU8(data, 0xFF);
        AppendU8(data, 0);

        // Variable: accessibility, initialValue, minValue, maxValue
        AppendU8(data, 0x03);
        AppendF32(data, 0.5f);
        AppendF32(data, 0.0f);
        AppendF32(data, 1.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        return data;
    }

    const std::string& XgsFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_engine_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xgs";
            const auto bytes = BuildXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        return path;
    }
}

// ===================== ContentVersion =====================

TEST(AudioEngineTest, ContentVersionIs46)
{
    EXPECT_EQ(AudioEngine::ContentVersion, 46);
}

// ===================== Constructors =====================

TEST(AudioEngineTest, SingleArgConstructorEmptyPathThrowsArgumentNull)
{
    EXPECT_THROW(AudioEngine engine(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, TwoArgConstructorEmptyPathThrowsArgumentNull)
{
    EXPECT_THROW(AudioEngine engine("", System::TimeSpan::Zero, "renderer"), System::ArgumentNullException);
}

TEST(AudioEngineTest, SingleArgConstructorLoadsFixture)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, TwoArgConstructorLoadsFixtureWithRendererAndLookAhead)
{
    AudioEngine engine(XgsFixturePath(), System::TimeSpan::Zero, "SDL3_mixer");
    EXPECT_FALSE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, ThreeArgConstructorWithArbitraryRendererAndLookAheadBehavesLikeSingleArg)
{
    // lookAheadTime/rendererId have no effect (plan_audio.md XA-4): even a nonzero look-ahead
    // and a renderer ID that doesn't name any real backend must not throw, and the resulting
    // engine must behave identically to the single-argument constructor.
    AudioEngine engine(XgsFixturePath(),
                       System::TimeSpan::FromMilliseconds(500),
                       "TotallyBogusRendererThatDoesNotExist");

    EXPECT_FALSE(engine.getIsDisposedProperty());
    EXPECT_FALSE(engine.getRendererDetailsProperty().empty());
    EXPECT_NO_THROW((void)engine.GetCategory("Default"));
}

// ===================== IsDisposed / Dispose =====================

TEST(AudioEngineTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getIsDisposedProperty());
    engine.Dispose();
    EXPECT_TRUE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, DisposeIsIdempotent)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_NO_THROW(engine.Dispose());
    EXPECT_TRUE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, DisposeRaisesDisposingEvent)
{
    AudioEngine engine(XgsFixturePath());
    bool raised = false;
    engine.Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    engine.Dispose();
    EXPECT_TRUE(raised);
}

// ===================== RendererDetails =====================

TEST(AudioEngineTest, RendererDetailsNonEmpty)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getRendererDetailsProperty().empty());
}

// ===================== GetCategory =====================

TEST(AudioEngineTest, GetCategoryValidReturnsMatchingName)
{
    AudioEngine engine(XgsFixturePath());
    const AudioCategory cat = engine.GetCategory("Default");
    EXPECT_EQ(cat.getNameProperty(), "Default");
}

TEST(AudioEngineTest, GetCategoryEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetCategory(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, GetCategoryInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetCategory("NoSuchCategory"), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetCategoryAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW((void)engine.GetCategory("Default"), System::ObjectDisposedException);
}

// ===================== GetGlobalVariable =====================

TEST(AudioEngineTest, GetGlobalVariableValidReturnsInitialValue)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("Volume"), 0.5f);
}

TEST(AudioEngineTest, GetGlobalVariableEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, GetGlobalVariableInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable("NoSuchVariable"), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetGlobalVariableAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW((void)engine.GetGlobalVariable("Volume"), System::ObjectDisposedException);
}

// ===================== SetGlobalVariable =====================

TEST(AudioEngineTest, SetGlobalVariableValidUpdatesValue)
{
    AudioEngine engine(XgsFixturePath());
    engine.SetGlobalVariable("Volume", 0.75f);
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("Volume"), 0.75f);
}

TEST(AudioEngineTest, SetGlobalVariableEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("", 1.0f), System::ArgumentNullException);
}

TEST(AudioEngineTest, SetGlobalVariableInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("NoSuchVariable", 1.0f), System::InvalidOperationException);
}

TEST(AudioEngineTest, SetGlobalVariableAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW(engine.SetGlobalVariable("Volume", 1.0f), System::ObjectDisposedException);
}

// ===================== Update =====================

TEST(AudioEngineTest, UpdateDoesNotThrow)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_NO_THROW(engine.Update());
}

// ===================== GetTypeName =====================

TEST(AudioEngineTest, GetTypeNameIsDottedXnaName)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_EQ(engine.GetTypeName(), "Microsoft.Xna.Framework.Audio.AudioEngine");
}
