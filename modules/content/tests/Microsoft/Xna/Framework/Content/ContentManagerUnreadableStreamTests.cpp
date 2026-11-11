// SPDX-License-Identifier: MS-PL
//
// KI-CONTENT-CANREAD: an OpenStream() result whose CanRead is false. Microsoft XNA 4.0 Refresh
// (Microsoft.Xna.Framework.dll SHA256 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130)
// hands it to ContentReader.Create, whose PrepareStream wraps it in a BinaryReader before reading
// anything (IL_0000-0001). .NET Framework 4.0's BinaryReader..ctor(Stream, Encoding) refuses a
// stream that cannot be read with ArgumentException(Argument_StreamNotReadable), "Stream was not
// readable." (mscorlib 4.0.30319.1, IL_0022-0039). PrepareStream catches only IOException
// (IL_00f1), Load<T> has no handler, and ReadAsset's finally closes the stream once
// (IL_0079-0082): no reader was created.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
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

    struct StreamLog
    {
        int reads = 0;
        int closes = 0;
    };

    /// A valid .xnb behind a stream that says it cannot be read, counting what is asked of it.
    class WriteOnlyStream final : public System::IO::MemoryStream
    {
    public:
        WriteOnlyStream(const Bytes& bytes, StreamLog& log, const bool readable)
            : System::IO::MemoryStream(bytes.data(), static_cast<std::int32_t>(bytes.size()), false)
            , log_(log), readable_(readable)
        {
        }

        [[nodiscard]] bool getCanReadProperty() const override { return readable_; }

        SharpRuntime::intcs Read(SharpRuntime::bytecs buffer[], const SharpRuntime::intcs offset,
                                 const SharpRuntime::intcs count) override
        {
            ++log_.reads;
            return System::IO::MemoryStream::Read(buffer, offset, count);
        }

        void Close() override
        {
            ++log_.closes;
            System::IO::MemoryStream::Close();
        }

    private:
        StreamLog& log_;
        bool readable_;
    };

    class UnreadableContentManager final : public ContentManager
    {
    public:
        UnreadableContentManager(const bool strictStreamRoute, const bool readable)
            : ContentManager(CNA::TestSupport::EmptyServices(), "Content"), readable_(readable)
        {
            RouteLoadsThroughOpenStreamEXT(strictStreamRoute);
        }

        StreamLog log;

        [[nodiscard]] std::int32_t ReadDirectly(const std::string& assetName)
        {
            return ReadAsset<std::int32_t>(assetName, {});
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            return std::make_unique<WriteOnlyStream>(file_, log, readable_);
        }

    private:
        Bytes file_ = Int32Xnb();
        bool readable_;
    };

    class ContentManagerUnreadableStreamTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };

    /// Runs @p read and expects Refresh's ArgumentException, nothing read and one Close.
    void ExpectNotReadable(UnreadableContentManager& manager, const std::function<void()>& read)
    {
        try
        {
            read();
            ADD_FAILURE() << "a stream that cannot be read was read";
        }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_EQ(error.getMessageProperty(), "Stream was not readable.");
        }
        EXPECT_EQ(manager.log.reads, 0);
        EXPECT_EQ(manager.log.closes, 1);
    }
}

TEST_F(ContentManagerUnreadableStreamTest, PositiveControlTheSameStreamLoadsWhenReadable)
{
    UnreadableContentManager manager(true, true);
    EXPECT_EQ(manager.Load<std::int32_t>("value"), 7);
    EXPECT_GT(manager.log.reads, 0);
}

TEST_F(ContentManagerUnreadableStreamTest, ReadAssetRefusesTheStreamBeforeReadingIt)
{
    UnreadableContentManager manager(false, false);
    ExpectNotReadable(manager, [&] { (void)manager.ReadDirectly("value"); });
}

TEST_F(ContentManagerUnreadableStreamTest, LoadRefusesTheStreamOnBothRoutes)
{
    for (const bool strict : {false, true})
    {
        SCOPED_TRACE(strict);
        UnreadableContentManager manager(strict, false);
        ExpectNotReadable(manager, [&] { (void)manager.Load<std::int32_t>("value"); });
    }
}

TEST_F(ContentManagerUnreadableStreamTest, LoadOfASoundEffectAndAnExternalReferenceRefuseItToo)
{
    {
        UnreadableContentManager manager(false, false);
        ExpectNotReadable(manager, [&] {
            (void)manager.Load<Microsoft::Xna::Framework::Audio::SoundEffect>("sound");
        });
    }
    {
        // A type-erased external reference loads through the same stream route.
        UnreadableContentManager manager(false, false);
        System::IO::MemoryStream referenceStream;
        System::IO::BinaryWriter writer(&referenceStream, true);
        writer.Write(std::string("other"));
        writer.Flush();
        const auto bytes = referenceStream.ToArray();
        System::IO::MemoryStream body(bytes.data(), static_cast<std::int32_t>(bytes.size()), false);
        ContentReader reader(&manager, &body, "asset", 5, 'w');
        ExpectNotReadable(manager, [&] { (void)reader.ReadExternalReference(); });
    }
}
