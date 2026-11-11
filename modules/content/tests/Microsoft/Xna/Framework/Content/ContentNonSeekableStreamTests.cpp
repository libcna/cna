// SPDX-License-Identifier: MS-PL
//
// KI-CONTENT-NONSEEK: an OpenStream() result that cannot seek. Microsoft XNA 4.0 Refresh
// (Microsoft.Xna.Framework.dll SHA256 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130):
// - ContentReader.PrepareStream IL_009f-00a5 asks the stream's CanSeek and compares the declared
//   `totalLength - 10` with Length - Position (IL_00a7-00cd, BadXnbSize) only when it can; a stream
//   that cannot seek is never asked its Length and its declaration is never checked.
// - An uncompressed reader then reads the stream itself to its end (IL_00ed).
// - An LZX body is DecompressStream over `totalLength - 14` compressed bytes (IL_00d2-00e5), whose
//   ReadBufferFromDisk (IL_0000-0042) fills min(64 KiB, compressedTodo) bytes per chunk and throws
//   InvalidOperationException (DecompressionError, IL_0023-0030) when the stream ends inside a
//   chunk -- before any byte of that chunk is decoded.
// CNA's own bounds stay: the 64 MiB file limit, and MonoGame's LZ4 body, which has no Refresh
// counterpart, is refused when it declares more than the stream holds.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/LzxEncoder.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    void PutInt32(Bytes& bytes, const std::size_t offset, const std::int32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    /// The body of the Int32 asset 7, followed by @p padding bytes no reader reaches.
    Bytes Int32Body(const std::size_t padding = 0)
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
        // Incompressible, so the compressed body is as long as it is.
        std::mt19937 random(20261111u);
        for (std::size_t i = 0; i < padding; ++i)
        {
            body.push_back(static_cast<std::uint8_t>(random() & 0xFFu));
        }
        return body;
    }

    Bytes Uncompressed(const Bytes& body, std::optional<std::int32_t> totalLength = std::nullopt)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        file.insert(file.end(), body.begin(), body.end());
        PutInt32(file, 6, totalLength.value_or(static_cast<std::int32_t>(file.size())));
        return file;
    }

    Bytes Compressed(const std::uint8_t flags, const Bytes& payload, const std::int32_t declaredSize,
                     std::optional<std::int32_t> totalLength = std::nullopt)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, flags, 0, 0, 0, 0, 0, 0, 0, 0};
        file.insert(file.end(), payload.begin(), payload.end());
        PutInt32(file, 6, totalLength.value_or(static_cast<std::int32_t>(file.size())));
        PutInt32(file, 10, declaredSize);
        return file;
    }

    Bytes Lzx(const Bytes& compressed, const std::int32_t declaredSize,
              std::optional<std::int32_t> totalLength = std::nullopt)
    {
        return Compressed(0x80, compressed, declaredSize, totalLength);
    }

    Bytes CompressLzx(const Bytes& body)
    {
        return CNA::Internal::Xnb::CompressXnbLzxPayload(body, {});
    }

    /// One raw LZ4 block holding @p body as literals only, as MonoGame writes a short body.
    Bytes Lz4Literals(const Bytes& body)
    {
        Bytes block;
        const std::size_t length = body.size();
        if (length < 15)
        {
            block.push_back(static_cast<std::uint8_t>(length << 4));
        }
        else
        {
            block.push_back(0xF0);
            std::size_t rest = length - 15;
            for (; rest >= 255; rest -= 255) { block.push_back(255); }
            block.push_back(static_cast<std::uint8_t>(rest));
        }
        block.insert(block.end(), body.begin(), body.end());
        return block;
    }

    /// A read-only stream that cannot seek and cannot tell its Length, like a network stream.
    class ForwardOnlyStream final : public System::IO::Stream
    {
    public:
        explicit ForwardOnlyStream(Bytes bytes) : bytes_(std::move(bytes)) {}

        SharpRuntime::intcs Read(SharpRuntime::bytecs buffer[], const SharpRuntime::intcs offset,
                                 const SharpRuntime::intcs count) override
        {
            SharpRuntime::intcs read = 0;
            // A short read each time: a reader must not assume one Read fills its request.
            const SharpRuntime::intcs limit = count < 4096 ? count : 4096;
            while (read < limit && position_ < bytes_.size())
            {
                buffer[offset + read] = bytes_[position_++];
                ++read;
            }
            return read;
        }

        void Close() override {}

        [[nodiscard]] SharpRuntime::intcs getLengthProperty() const override
        {
            throw System::NotSupportedException("This stream does not support seeking.");
        }

    private:
        Bytes bytes_;
        std::size_t position_ = 0;
    };

    class StreamShapeContentManager final : public ContentManager
    {
    public:
        StreamShapeContentManager(const bool canSeek, const bool strictStreamRoute)
            : ContentManager(CNA::TestSupport::EmptyServices(), "Content"), canSeek_(canSeek)
        {
            RouteLoadsThroughOpenStreamEXT(strictStreamRoute);
        }

        Bytes file;

        [[nodiscard]] std::int32_t ReadDirectly(const std::string& assetName)
        {
            return ReadAsset<std::int32_t>(assetName, {});
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            if (canSeek_)
            {
                return std::make_unique<System::IO::MemoryStream>(
                    file.data(), static_cast<std::int32_t>(file.size()), false);
            }
            return std::make_unique<ForwardOnlyStream>(file);
        }

    private:
        bool canSeek_;
    };

    class ContentNonSeekableStreamTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };

    using Route = std::function<std::int32_t(StreamShapeContentManager&)>;

    /// Every route a Load<T> or ReadAsset<T> takes to an OpenStream() result, by stream shape.
    void ForEachRoute(const bool canSeek, const Bytes& file,
                      const std::function<void(const Route&, StreamShapeContentManager&)>& check)
    {
        const std::vector<std::pair<const char*, Route>> routes{
            {"ReadAsset", [](StreamShapeContentManager& m) { return m.ReadDirectly("value"); }},
            {"Load", [](StreamShapeContentManager& m) { return m.Load<std::int32_t>("value"); }},
        };
        for (const auto& [name, read] : routes)
        {
            for (const bool strict : {false, true})
            {
                SCOPED_TRACE(std::string(name) + (strict ? " strict" : " ordinary") +
                             (canSeek ? " seekable" : " forward-only"));
                StreamShapeContentManager manager(canSeek, strict);
                manager.file = file;
                check(read, manager);
            }
        }
    }

    void ExpectLoads(const bool canSeek, const Bytes& file)
    {
        ForEachRoute(canSeek, file, [](const Route& read, StreamShapeContentManager& manager) {
            EXPECT_EQ(read(manager), 7);
        });
    }

    template <typename Exception>
    void ExpectRefusal(const bool canSeek, const Bytes& file)
    {
        ForEachRoute(canSeek, file, [](const Route& read, StreamShapeContentManager& manager) {
            EXPECT_THROW((void)read(manager), Exception);
        });
    }
}

TEST_F(ContentNonSeekableStreamTest, AStreamThatCannotSeekIsNeverAskedItsLength)
{
    // IL_009f-00a5: CanSeek first, so ForwardOnlyStream's throwing Length is never reached.
    ExpectLoads(false, Uncompressed(Int32Body()));
    ExpectLoads(false, Lzx(CompressLzx(Int32Body()), static_cast<std::int32_t>(Int32Body().size())));
}

TEST_F(ContentNonSeekableStreamTest, AnUncompressedDeclarationPastTheStreamIsCheckedOnlyWhenItCanSeek)
{
    const Bytes body = Int32Body();
    const Bytes exact = Uncompressed(body);
    for (const std::int32_t past : {1, 100, 0x7FFFFFFF - static_cast<std::int32_t>(exact.size())})
    {
        SCOPED_TRACE(past);
        const Bytes file = Uncompressed(body, static_cast<std::int32_t>(exact.size()) + past);
        ExpectRefusal<ContentLoadException>(true, file);
        // IL_00ed: the reader reads the stream itself, so the whole body loads.
        ExpectLoads(false, file);
    }
}

TEST_F(ContentNonSeekableStreamTest, AnLzxChunkTheStreamEndsInsideFailsBeforeItsFramesAreDecoded)
{
    // The whole asset is one complete frame, but the declaration asks one byte more than the
    // stream holds: the first chunk, min(64 KiB, compressedTodo) bytes, cannot be filled.
    const Bytes body = Int32Body();
    const Bytes compressed = CompressLzx(body);
    ASSERT_LT(compressed.size(), 0x10000u);
    const auto declaredSize = static_cast<std::int32_t>(body.size());
    const auto oneMore = static_cast<std::int32_t>(14 + compressed.size() + 1);
    const Bytes file = Lzx(compressed, declaredSize, oneMore);
    ExpectRefusal<ContentLoadException>(true, file);
    ExpectRefusal<System::InvalidOperationException>(false, file);
    // The exact declaration is the control.
    ExpectLoads(false, Lzx(compressed, declaredSize));
}

TEST_F(ContentNonSeekableStreamTest, AnLzxBodyIsDecodedFromTheWholeChunksTheStreamHolds)
{
    // About three 32 KiB frames of incompressible data: the asset's Int32 is in the first frame,
    // which lies wholly inside the first 64 KiB chunk.
    const Bytes body = Int32Body(3 * 0x8000);
    const Bytes compressed = CompressLzx(body);
    ASSERT_GT(compressed.size(), 0x10000u + 1024u);
    const auto declaredSize = static_cast<std::int32_t>(body.size());
    const auto declaredTotal = static_cast<std::int32_t>(14 + compressed.size());

    // Everything is present but the declaration names one byte more: the first chunk is whole,
    // the second cannot be filled, and the reader never needs it.
    const Bytes generous = Lzx(compressed, declaredSize, declaredTotal + 1);
    ExpectRefusal<ContentLoadException>(true, generous);
    ExpectLoads(false, generous);

    // The stream ends inside the first chunk, after the asset's whole first frame: that chunk is
    // never handed to the decoder, so the first read fails.
    const Bytes prefix(compressed.begin(), compressed.begin() + 60000);
    const Bytes cut = Lzx(prefix, declaredSize, declaredTotal);
    ExpectRefusal<ContentLoadException>(true, cut);
    ExpectRefusal<System::InvalidOperationException>(false, cut);
}

TEST_F(ContentNonSeekableStreamTest, AnLz4DeclarationPastTheStreamIsStillRefused)
{
    const Bytes body = Int32Body();
    const Bytes block = Lz4Literals(body);
    const auto declaredSize = static_cast<std::int32_t>(body.size());
    ExpectLoads(false, Compressed(0x40, block, declaredSize));
    const auto past = static_cast<std::int32_t>(14 + block.size() + 10);
    ExpectRefusal<ContentLoadException>(true, Compressed(0x40, block, declaredSize, past));
    ExpectRefusal<ContentLoadException>(false, Compressed(0x40, block, declaredSize, past));
}
