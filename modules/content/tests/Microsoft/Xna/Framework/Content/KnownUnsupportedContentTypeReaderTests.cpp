// SPDX-License-Identifier: MS-PL
// Regression coverage for the former known-unsupported EffectReader registration. The filename is
// retained to keep test discovery/history stable; the reader is now concrete.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/EffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

#include <any>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Internal::Xnb::RegisterEffectXnbReader;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    std::vector<std::uint8_t> LoadFixture()
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../renderers/fna3d/effects/BasicEffect.fxb";
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::any ReadEffectPayload(ContentManager& manager, const std::string& assetName,
                               std::int32_t declaredLength,
                               const std::vector<std::uint8_t>& payload)
    {
        System::IO::MemoryStream stream;
        {
            System::IO::BinaryWriter writer(&stream, true);
            writer.Write(declaredLength);
            if (!payload.empty())
            {
                writer.Write(payload.data(), 0,
                             static_cast<std::int32_t>(payload.size()));
            }
        }
        stream.Seek(0, System::IO::SeekOrigin::Begin);
        ContentReader input(&manager, &stream, assetName, 5, 'w');
        auto reader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.EffectReader");
        if (!reader) throw std::runtime_error("EffectReader was not registered in the test");
        return reader->ReadUntyped(input, std::any{});
    }

    class EffectContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            RegisterEffectXnbReader();
            manager.setGraphicsDevice(device);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice device;
        ContentManager manager;
    };
}

TEST_F(EffectContentTypeReaderTest, ReadsLengthPrefixedCompiledEffectAndSetsAssetName)
{
    const auto bytes = LoadFixture();
    ASSERT_FALSE(bytes.empty());

    System::IO::MemoryStream stream;
    {
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<std::int32_t>(bytes.size()));
        writer.Write(bytes.data(), 0, static_cast<std::int32_t>(bytes.size()));
    }
    stream.Seek(0, System::IO::SeekOrigin::Begin);
    ContentReader input(&manager, &stream, "effects/custom", 5, 'w');
    auto reader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.EffectReader");
    ASSERT_NE(reader, nullptr);

    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        EXPECT_THROW(reader->ReadUntyped(input, std::any{}), ContentLoadException);
        return;
    }
    auto effect = std::any_cast<std::shared_ptr<Effect>>(
        reader->ReadUntyped(input, std::any{}));
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->getNameProperty(), "effects/custom");
    EXPECT_GT(effect->getParametersProperty().getCountProperty(), 0);
}

TEST_F(EffectContentTypeReaderTest, RejectsOversizedLengthBeforeAllocating)
{
    System::IO::MemoryStream stream;
    {
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(std::numeric_limits<std::int32_t>::max());
    }
    stream.Seek(0, System::IO::SeekOrigin::Begin);
    ContentReader input(&manager, &stream, "effects/bad", 5, 'w');
    auto reader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.EffectReader");
    ASSERT_NE(reader, nullptr);

    try
    {
        reader->ReadUntyped(input, std::any{});
        FAIL() << "oversized bytecode must be rejected";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effects/bad"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("length"), std::string::npos);
    }
}

TEST_F(EffectContentTypeReaderTest, RejectsNegativeLengthWithAssetContext)
{
    try
    {
        ReadEffectPayload(manager, "effects/negative", -1, {});
        FAIL() << "negative bytecode length must be rejected";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effects/negative"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("length"), std::string::npos);
    }
}

TEST_F(EffectContentTypeReaderTest, RejectsTruncatedPayloadAsContentLoadException)
{
    try
    {
        ReadEffectPayload(manager, "effects/truncated", 8, {1, 2, 3, 4});
        FAIL() << "truncated bytecode must be rejected";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effects/truncated"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("stream ended"), std::string::npos);
    }
}

TEST_F(EffectContentTypeReaderTest, RejectsMalformedBytecodeWithAssetContext)
{
    try
    {
        ReadEffectPayload(manager, "effects/malformed", 4, {1, 2, 3, 4});
        FAIL() << "malformed bytecode must be rejected";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effects/malformed"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("effect bytecode"), std::string::npos);
    }
}

TEST_F(EffectContentTypeReaderTest, RejectsMgfxWithSpecificAssetDiagnostic)
{
    const std::vector<std::uint8_t> mgfx{'M', 'G', 'F', 'X', 1, 0, 0, 0};
    try
    {
        ReadEffectPayload(manager, "effects/monogame", 8, mgfx);
        FAIL() << "MGFX must not be accepted as XNA Effect Framework bytecode";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effects/monogame"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("MGFX"), std::string::npos);
    }
}
