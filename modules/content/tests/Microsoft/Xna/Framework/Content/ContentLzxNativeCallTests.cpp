// SPDX-License-Identifier: MS-PL
//
// KI-CONTENT-LZXGRAN: what a reader gets from an LZX body whose frames fail, measured on Microsoft
// XNA 4.0 Refresh. DecompressStream (Microsoft.Xna.Framework.dll SHA256 38e7093f...ca130) serves one
// XnaNative.dll Decompress call per DecompressNextBuffer, with a 64 KiB output buffer, and throws
// InvalidOperationException (DecompressionError) when the call fails (IL_0089-0095) -- so the unit
// is the native call, not the frame. A probe driving XnaNative.dll 4.0.30901.0 (the Refresh
// redistributable's) exactly as DecompressNextBuffer and ReadBufferFromDisk do, under Wine, found
// that one call decodes every frame whose bytes it has while it holds at most 64 KiB of undelivered
// output, delivers at most 64 KiB, decodes nothing after a frame whose size is not 32 KiB, and
// fails as a whole when any frame it decodes is corrupt. With every frame in the first 64 KiB
// compressed chunk, a corrupt frame 0, 1 or 2 served nothing and a corrupt frame 3 served exactly
// 65,536 bytes; a corrupt frame after a 103-byte final frame was never decoded. The five files the
// failing cases below build were run through the same probe, and served 0, 65,536, 131,072, 0
// and 55 bytes -- the counts asserted here.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/LzxEncoder.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

using CNA::Internal::Xnb::DecodeXnbContainerBody;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    constexpr std::size_t FrameSize = 0x8000;

    /// The Int32 asset 7, padded with zeros to @p frames whole 32 KiB frames.
    Bytes Int32Body(const std::size_t frames)
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
        const auto written = stream.ToArray();
        Bytes body(written.begin(), written.end());
        if (frames > 0) { body.resize(frames * FrameSize, 0); }
        return body;
    }

    /// CNA's LZX frames for @p body, without the end marker and the trailer after them.
    Bytes Frames(const Bytes& body)
    {
        Bytes payload = CNA::Internal::Xnb::CompressXnbLzxPayload(body, {});
        std::size_t position = 0;
        while (position + 2 <= payload.size())
        {
            const std::size_t hi = payload[position];
            const std::size_t lo = payload[position + 1];
            std::size_t header = 2;
            std::size_t block = (hi << 8) | lo;
            if (hi == 0xFF)
            {
                header = 5;
                block = (static_cast<std::size_t>(payload[position + 3]) << 8) | payload[position + 4];
            }
            if (block == 0) { break; }
            position += header + block;
        }
        payload.resize(position);
        return payload;
    }

    /// One 32 KiB frame whose block is all zero bits: block type 0 is not an LZX block type.
    Bytes InvalidFrame() { return {0x00, 0x08, 0, 0, 0, 0, 0, 0, 0, 0}; }

    /// The end marker and slack CNA's encoder writes after the last frame.
    Bytes Trailer() { return {0, 0, 0, 0, 0}; }

    Bytes Concat(std::initializer_list<Bytes> parts)
    {
        Bytes all;
        for (const Bytes& part : parts) { all.insert(all.end(), part.begin(), part.end()); }
        return all;
    }

    Bytes Lzx(const Bytes& payload, const std::int32_t declaredSize)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0x80, 0, 0, 0, 0, 0, 0, 0, 0};
        file.insert(file.end(), payload.begin(), payload.end());
        const auto total = static_cast<std::int32_t>(file.size());
        std::memcpy(file.data() + 6, &total, sizeof(total));
        std::memcpy(file.data() + 10, &declaredSize, sizeof(declaredSize));
        return file;
    }

    /// Decodes @p file and returns how many bytes a reader is served, expecting the read past
    /// them to raise DecompressionError.
    std::size_t ServedBeforeDecompressionError(const Bytes& file)
    {
        const auto container = DecodeXnbContainerBody(std::span<const std::uint8_t>(file.data(), file.size()), "lzx");
        EXPECT_TRUE(container.deferredReadFailure);
        if (container.deferredReadFailure)
        {
            EXPECT_THROW(std::rethrow_exception(container.deferredReadFailure), System::InvalidOperationException);
        }
        return container.Bytes().size();
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(CNA::TestSupport::EmptyServices(), "Content")
        {
            RouteLoadsThroughOpenStreamEXT(true);
        }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class ContentLzxNativeCallTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        InMemoryContentManager manager;
    };
}

TEST_F(ContentLzxNativeCallTest, PositiveControlTheFramesDecodeWhole)
{
    for (const std::size_t frames : {std::size_t{1}, std::size_t{2}, std::size_t{3}})
    {
        SCOPED_TRACE(frames);
        const Bytes body = Int32Body(frames);
        const Bytes file = Lzx(Concat({Frames(body), Trailer()}), static_cast<std::int32_t>(body.size()));
        const auto container = DecodeXnbContainerBody(std::span<const std::uint8_t>(file.data(), file.size()), "lzx");
        EXPECT_EQ(container.Bytes().size(), body.size());
        EXPECT_FALSE(container.deferredReadFailure);
        manager.files["value"] = file;
        EXPECT_EQ(manager.Load<std::int32_t>("value"), 7);
        manager.Unload();
    }
}

TEST_F(ContentLzxNativeCallTest, AFailingFrameLosesTheGoodFramesItsCallDecoded)
{
    // Frames 0 and 1 fill the first call's 64 KiB; frame 2 is still decoded in it and fails it.
    const Bytes body = Int32Body(2);
    const Bytes file = Lzx(Concat({Frames(body), InvalidFrame(), Trailer()}),
                           static_cast<std::int32_t>(body.size() + FrameSize));
    EXPECT_EQ(ServedBeforeDecompressionError(file), 0u);
    // The asset lies wholly in frame 0, and still cannot be read.
    manager.files["value"] = file;
    EXPECT_THROW((void)manager.Load<std::int32_t>("value"), System::InvalidOperationException);
}

TEST_F(ContentLzxNativeCallTest, TheBuffersOfTheCallsBeforeTheFailingOneAreServed)
{
    // The first call decodes frames 0-2 and delivers 64 KiB, holding frame 2; the second decodes
    // the corrupt frame 3 and fails, losing frame 2 with it.
    const Bytes body = Int32Body(3);
    const Bytes file = Lzx(Concat({Frames(body), InvalidFrame(), Trailer()}),
                           static_cast<std::int32_t>(body.size() + FrameSize));
    EXPECT_EQ(ServedBeforeDecompressionError(file), 2 * FrameSize);
    manager.files["value"] = file;
    EXPECT_EQ(manager.Load<std::int32_t>("value"), 7);

    // Five good frames: the third call fails, after 2 x 64 KiB.
    const Bytes five = Int32Body(5);
    EXPECT_EQ(ServedBeforeDecompressionError(Lzx(Concat({Frames(five), InvalidFrame(), Trailer()}),
                                                 static_cast<std::int32_t>(five.size() + FrameSize))),
              4 * FrameSize);
}

TEST_F(ContentLzxNativeCallTest, TheDeclaredSizeDoesNotStopACallFromDecodingAhead)
{
    // decompressedTodo is DecompressStream's, not the native decoder's: a body declared as frame 0
    // alone still has frame 1 decoded in the first call, and that call fails.
    const Bytes body = Int32Body(1);
    const Bytes file = Lzx(Concat({Frames(body), InvalidFrame(), Trailer()}),
                           static_cast<std::int32_t>(body.size()));
    EXPECT_EQ(ServedBeforeDecompressionError(file), 0u);
    manager.files["value"] = file;
    EXPECT_THROW((void)manager.Load<std::int32_t>("value"), System::InvalidOperationException);
}

TEST_F(ContentLzxNativeCallTest, NothingIsDecodedAfterAFrameWhoseSizeIsNot32KiB)
{
    // The measured case: a short final frame followed, without an end marker, by a corrupt one.
    const Bytes body = Int32Body(0);
    const Bytes file = Lzx(Concat({Frames(body), InvalidFrame()}),
                           static_cast<std::int32_t>(body.size() + 64));
    EXPECT_EQ(ServedBeforeDecompressionError(file), body.size());
    manager.files["value"] = file;
    EXPECT_EQ(manager.Load<std::int32_t>("value"), 7);
}
