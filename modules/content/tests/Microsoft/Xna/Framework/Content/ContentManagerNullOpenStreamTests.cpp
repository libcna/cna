// SPDX-License-Identifier: MS-PL
//
// KI-CONTENT-NULLSTREAM: an OpenStream() override that returns null. Microsoft XNA 4.0 Refresh
// (Microsoft.Xna.Framework.dll SHA256 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130)
// passes the null stream on: ContentManager.ReadAsset<T> IL_0027-0033 calls ContentReader.Create,
// whose PrepareStream IL_0000-0001 constructs a BinaryReader over it, and .NET Framework 4.0's
// BinaryReader..ctor(Stream, Encoding) IL_0006-0013 throws ArgumentNullException("input")
// (mscorlib 4.0.30319.1, SHA256 5634668d4775b0113f08ea31093b281fea69bfc4e99227f5ca761b4ed98acc63).
// PrepareStream catches only IOException (IL_00f1) and Load<T> IL_0096-00c4 has no handler, so
// that exception is what every route reports. The finally of ReadAsset skips a null stream
// (IL_0079-007a): there is nothing to close.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    /// Answers every OpenStream() with null and counts the calls.
    class NullStreamContentManager final : public ContentManager
    {
    public:
        explicit NullStreamContentManager(const bool strictStreamRoute)
            : ContentManager(CNA::TestSupport::EmptyServices(), "Content")
        {
            RouteLoadsThroughOpenStreamEXT(strictStreamRoute);
        }

        int opens = 0;

        [[nodiscard]] std::int32_t ReadDirectly(const std::string& assetName)
        {
            return ReadAsset<std::int32_t>(assetName, {});
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            ++opens;
            return nullptr;
        }
    };

    /// A body stream holding one external-reference string, as a reader meets it.
    std::vector<std::uint8_t> ReferenceBytes(const std::string& reference)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(reference);
        writer.Flush();
        const auto bytes = stream.ToArray();
        return {bytes.begin(), bytes.end()};
    }

    class ContentManagerNullOpenStreamTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };

    /// Runs @p read and expects Refresh's ArgumentNullException for the parameter `input`.
    void ExpectInputArgumentNull(const std::function<void()>& read)
    {
        try
        {
            read();
            FAIL() << "a null OpenStream() result was accepted";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "input");
        }
    }
}

TEST_F(ContentManagerNullOpenStreamTest, ReadAssetReportsTheNullStreamAsArgumentNullInput)
{
    NullStreamContentManager manager(false);
    ExpectInputArgumentNull([&] { (void)manager.ReadDirectly("value"); });
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(ContentManagerNullOpenStreamTest, LoadReportsTheNullStreamOnBothRoutes)
{
    for (const bool strict : {false, true})
    {
        SCOPED_TRACE(strict);
        NullStreamContentManager manager(strict);
        ExpectInputArgumentNull([&] { (void)manager.Load<std::int32_t>("value"); });
        EXPECT_EQ(manager.opens, 1);
        // Nothing was cached: the next load asks the override again.
        ExpectInputArgumentNull([&] { (void)manager.Load<std::int32_t>("value"); });
        EXPECT_EQ(manager.opens, 2);
    }
}

TEST_F(ContentManagerNullOpenStreamTest, LoadOfASoundEffectReportsTheNullStreamOnBothRoutes)
{
    for (const bool strict : {false, true})
    {
        SCOPED_TRACE(strict);
        NullStreamContentManager manager(strict);
        ExpectInputArgumentNull(
            [&] { (void)manager.Load<Microsoft::Xna::Framework::Audio::SoundEffect>("sound"); });
        EXPECT_EQ(manager.opens, 1);
    }
}

TEST_F(ContentManagerNullOpenStreamTest, ExternalReferencesReportTheNullStreamTypedAndErased)
{
    // ContentReader.ReadExternalReference<T> IL_0028-0033 loads through Load<T>, so a reference
    // reaches the same null stream.
    NullStreamContentManager manager(false);
    const std::vector<std::uint8_t> bytes = ReferenceBytes("other");
    {
        System::IO::MemoryStream body(bytes.data(), static_cast<std::int32_t>(bytes.size()), false);
        ContentReader reader(&manager, &body, "asset", 5, 'w');
        ExpectInputArgumentNull([&] { (void)reader.ReadExternalReference<std::int32_t>(); });
    }
    {
        System::IO::MemoryStream body(bytes.data(), static_cast<std::int32_t>(bytes.size()), false);
        ContentReader reader(&manager, &body, "asset", 5, 'w');
        ExpectInputArgumentNull([&] { (void)reader.ReadExternalReference(); });
    }
    EXPECT_EQ(manager.opens, 2);
}
