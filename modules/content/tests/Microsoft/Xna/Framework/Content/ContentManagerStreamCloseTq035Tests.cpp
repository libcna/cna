// SPDX-License-Identifier: MS-PL
//
// HND-TQ-35 (a): ContentManager closes the stream OpenStream() returned, explicitly, as Microsoft
// XNA 4.0 Refresh does. ContentManager.ReadAsset<T> (Microsoft.Xna.Framework.dll SHA256
// 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130) opens the stream outside its
// try (IL_0027-002e) and disposes it in the outer finally (IL_0079-0082), on success and on
// failure; Stream.Dispose() calls the virtual Close(). The owner decided on 2026-11-09 that a
// throwing Close does not replace an exception already propagating from the read, and propagates
// when the read succeeded.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/IOException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    /// An uncompressed .xnb holding the Int32 7.
    Bytes Int32Xnb()
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write(static_cast<std::int32_t>(7));
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        file.insert(file.end(), body.begin(), body.end());
        const auto total = static_cast<std::uint32_t>(file.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        return file;
    }

    struct CloseLog
    {
        int closes = 0;
        bool throwOnClose = false;
    };

    /// A MemoryStream that records every Close() and can refuse it.
    class ClosingStream final : public System::IO::MemoryStream
    {
    public:
        ClosingStream(const Bytes& bytes, CloseLog& log)
            : System::IO::MemoryStream(bytes.data(), static_cast<std::int32_t>(bytes.size()), false)
            , log_(log)
        {
        }

        void Close() override
        {
            ++log_.closes;
            if (log_.throwOnClose)
            {
                throw System::IO::IOException("close refused");
            }
            System::IO::MemoryStream::Close();
        }

    private:
        CloseLog& log_;
    };

    class ClosingContentManager final : public ContentManager
    {
    public:
        explicit ClosingContentManager(const bool strictStreamRoute)
            : ContentManager(CNA::TestSupport::EmptyServices(), "Content")
        {
            RouteLoadsThroughOpenStreamEXT(strictStreamRoute);
        }

        Bytes file = Int32Xnb();
        CloseLog log;

        [[nodiscard]] std::int32_t ReadDirectly(const std::string& assetName)
        {
            return ReadAsset<std::int32_t>(assetName, {});
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            return std::make_unique<ClosingStream>(file, log);
        }
    };

    class ContentManagerStreamCloseTq035Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };

    /// Every route a Load<T> or ReadAsset<T> takes to the OpenStream() result.
    const std::vector<std::pair<const char*, std::function<std::int32_t(ClosingContentManager&)>>>&
    Routes()
    {
        static const std::vector<std::pair<const char*, std::function<std::int32_t(ClosingContentManager&)>>>
            routes{
                {"ReadAsset", [](ClosingContentManager& m) { return m.ReadDirectly("value"); }},
                {"Load", [](ClosingContentManager& m) { return m.Load<std::int32_t>("value"); }},
            };
        return routes;
    }
}

TEST_F(ContentManagerStreamCloseTq035Test, ASuccessfulReadClosesTheStreamOnce)
{
    for (const auto& [name, read] : Routes())
    {
        SCOPED_TRACE(name);
        for (const bool strict : {false, true})
        {
            ClosingContentManager manager(strict);
            EXPECT_EQ(read(manager), 7);
            EXPECT_EQ(manager.log.closes, 1);
        }
    }
}

TEST_F(ContentManagerStreamCloseTq035Test, AFailedReadStillClosesTheStream)
{
    for (const auto& [name, read] : Routes())
    {
        SCOPED_TRACE(name);
        ClosingContentManager manager(true);
        manager.file[0] = 'Y'; // BadXnbMagic
        EXPECT_THROW((void)read(manager), ContentLoadException);
        EXPECT_EQ(manager.log.closes, 1);
    }
}

TEST_F(ContentManagerStreamCloseTq035Test, AThrowingCloseDoesNotReplaceTheReadFailure)
{
    for (const auto& [name, read] : Routes())
    {
        SCOPED_TRACE(name);
        ClosingContentManager manager(true);
        manager.file[0] = 'Y';
        manager.log.throwOnClose = true;
        EXPECT_THROW((void)read(manager), ContentLoadException);
        EXPECT_EQ(manager.log.closes, 1);
    }
}

TEST_F(ContentManagerStreamCloseTq035Test, AThrowingCloseAfterASuccessfulReadPropagatesAndCachesNothing)
{
    for (const auto& [name, read] : Routes())
    {
        SCOPED_TRACE(name);
        ClosingContentManager manager(true);
        manager.log.throwOnClose = true;
        EXPECT_THROW((void)read(manager), System::IO::IOException);
        EXPECT_EQ(manager.log.closes, 1);
        // Nothing was cached: the next load opens and closes a stream again.
        manager.log.throwOnClose = false;
        EXPECT_EQ(read(manager), 7);
        EXPECT_EQ(manager.log.closes, 2);
    }
}
