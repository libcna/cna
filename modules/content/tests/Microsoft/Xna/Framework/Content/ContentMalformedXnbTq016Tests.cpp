// SPDX-License-Identifier: MS-PL
//
// HND-TQ-16: what ContentManager::Load<T> accepts and refuses for a malformed .xnb, and with which
// exception type, following Microsoft XNA 4.0 Refresh (Microsoft.Xna.Framework.dll SHA256
// 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130).
//
// Oracles, all Refresh IL:
// - ContentReader.PrepareStream IL_009f-00cd refuses only `(Int32)(totalLength - 10)` past the
//   stream's remaining bytes (BadXnbSize); IL_00ed hands the reader the input stream itself, so an
//   uncompressed body runs to the end of the input; IL_00d2-00e5 makes `totalLength - 14` the
//   compressed byte budget.
// - ContentReader.ReadHeader IL_001b-0044 and ReadSharedResources IL_0000-0002: a shared-resource
//   count of zero or less is no shared resources and no fixup table.
// - ContentReader.ReadSharedResource<T> IL_0039: `ldlen` on that absent table,
//   NullReferenceException; IL_0036-0054 BadXnb for an index past an existing table.
// - DecompressStream.DecompressNextBuffer IL_0000-000a (nothing once decompressedTodo is spent),
//   IL_0089-0095 and IL_009c-00ad, ReadBufferFromDisk IL_0023-0030: InvalidOperationException
//   (DecompressionError), raised when the reader reaches that point.
// - ContentReader.ReadAsset<T> IL_0000-002c wraps IOException only, so the
//   InvalidOperationException and NullReferenceException above leave Load<T> unwrapped.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <any>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/LzxEncoder.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    constexpr char Int32ReaderName[] = "Microsoft.Xna.Framework.Content.Int32Reader";
    constexpr char SharedUserReaderName[] = "CNA.Test.Tq016SharedUser";
    constexpr char BufferUserReaderName[] = "CNA.Test.Tq016BufferUser";

    void PutInt32(Bytes& bytes, const std::size_t offset, const std::int32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    /// A body: one type reader @p readerName, then @p sharedCount spelled byte by byte, then the
    /// root object (type id 1) whose payload @p payload writes.
    Bytes Body(const std::string& readerName, const Bytes& sharedCount,
               const std::function<void(System::IO::BinaryWriter&)>& payload)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(readerName);
        writer.Write(static_cast<std::int32_t>(0));
        for (const std::uint8_t byte : sharedCount) { writer.Write(byte); }
        writer.Write7BitEncodedInt(1);
        payload(writer);
        writer.Flush();
        const auto bytes = stream.ToArray();
        return {bytes.begin(), bytes.end()};
    }

    /// The Int32 asset 7 with the shared-resource count spelled @p sharedCount.
    Bytes Int32Body(const Bytes& sharedCount = {0x00})
    {
        return Body(Int32ReaderName, sharedCount,
                    [](System::IO::BinaryWriter& w) { w.Write(static_cast<std::int32_t>(7)); });
    }

    /// An uncompressed container declaring @p totalLength (default: the exact file size).
    Bytes Uncompressed(const Bytes& body, std::optional<std::int32_t> totalLength = std::nullopt)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        file.insert(file.end(), body.begin(), body.end());
        PutInt32(file, 6, totalLength.value_or(static_cast<std::int32_t>(file.size())));
        return file;
    }

    /// An LZX container: @p compressed after the decompressed-size field @p declaredSize, the
    /// declared total length defaulting to the exact file size.
    Bytes Lzx(const Bytes& compressed, const std::int32_t declaredSize,
              std::optional<std::int32_t> totalLength = std::nullopt)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0x80, 0, 0, 0, 0, 0, 0, 0, 0};
        file.insert(file.end(), compressed.begin(), compressed.end());
        PutInt32(file, 6, totalLength.value_or(static_cast<std::int32_t>(file.size())));
        PutInt32(file, 10, declaredSize);
        return file;
    }

    Bytes Compress(const Bytes& body, const std::uint32_t frameSize = 0x8000u)
    {
        CNA::Internal::Xnb::LzxEncodeOptions options;
        options.frameSize = frameSize;
        return CNA::Internal::Xnb::CompressXnbLzxPayload(body, options);
    }

    /// One default-size LZX frame whose block is all zero bits: block type 0 is not an LZX
    /// block type, so the decoder refuses it.
    Bytes InvalidLzxFrame()
    {
        return {0x00, 0x08, 0, 0, 0, 0, 0, 0, 0, 0};
    }

    /// Reads one shared-resource reference into an Int32 fixup.
    struct SharedUser
    {
        std::int32_t value = -1;
    };

    class SharedUserReader final : public ContentTypeReader<std::shared_ptr<SharedUser>>
    {
    public:
        SharedUserReader() : ContentTypeReader<std::shared_ptr<SharedUser>>(SharedUserReaderName) {}

    protected:
        std::shared_ptr<SharedUser> Read(ContentReader& input,
                                         std::optional<std::shared_ptr<SharedUser>>) override
        {
            auto user = std::make_shared<SharedUser>();
            SharedUser* raw = user.get();
            input.ReadSharedResource<std::int32_t>([raw](const std::int32_t value) { raw->value = value; });
            return user;
        }
    };

    /// Reads a 4-byte count, then that many bytes through ReadBytesExactOrThrow.
    class BufferUserReader final : public ContentTypeReader<std::shared_ptr<Bytes>>
    {
    public:
        BufferUserReader() : ContentTypeReader<std::shared_ptr<Bytes>>(BufferUserReaderName) {}

    protected:
        std::shared_ptr<Bytes> Read(ContentReader& input, std::optional<std::shared_ptr<Bytes>>) override
        {
            const std::int32_t count = input.ReadInt32();
            return std::make_shared<Bytes>(input.ReadBytesExactOrThrow(count, "BufferUserReader"));
        }
    };

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

    class ContentMalformedXnbTq016Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            ContentTypeReaderManager::AddTypeCreator(
                SharedUserReaderName, [] { return std::make_unique<SharedUserReader>(); });
            ContentTypeReaderManager::AddTypeCreator(
                BufferUserReaderName, [] { return std::make_unique<BufferUserReader>(); });
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        InMemoryContentManager manager;
    };
}

TEST_F(ContentMalformedXnbTq016Test, PositiveControlsLoadUncompressedAndLzx)
{
    const Bytes body = Int32Body();
    manager.files["plain"] = Uncompressed(body);
    manager.files["lzx"] = Lzx(Compress(body), static_cast<std::int32_t>(body.size()));
    EXPECT_EQ(manager.Load<std::int32_t>("plain"), 7);
    EXPECT_EQ(manager.Load<std::int32_t>("lzx"), 7);
}

TEST_F(ContentMalformedXnbTq016Test, AnUncompressedBodyRunsPastASmallerDeclaredTotalLength)
{
    // IL_00ed: the reader reads the input stream itself; the declaration only has to fit
    // (IL_009f-00cd), so ten, zero and a negative length all load the whole body.
    for (const std::int32_t totalLength : {10, 11, 0, -7})
    {
        SCOPED_TRACE(totalLength);
        const std::string name = "short-" + std::to_string(totalLength);
        manager.files[name] = Uncompressed(Int32Body(), totalLength);
        EXPECT_EQ(manager.Load<std::int32_t>(name), 7);
    }
}

TEST_F(ContentMalformedXnbTq016Test, ADeclaredTotalLengthPastTheFileIsBadXnbSize)
{
    Bytes exact = Uncompressed(Int32Body());
    const auto size = static_cast<std::int32_t>(exact.size());
    manager.files["exact"] = exact;
    manager.files["one-past"] = Uncompressed(Int32Body(), size + 1);
    EXPECT_EQ(manager.Load<std::int32_t>("exact"), 7);
    EXPECT_THROW((void)manager.Load<std::int32_t>("one-past"), ContentLoadException);
}

TEST_F(ContentMalformedXnbTq016Test, TheDeclaredLengthIsComparedInInt32Arithmetic)
{
    // IL_00a7-00ab: `totalLength - 10` is an Int32 subtraction before the widening. int.MinValue
    // + 3 wraps to int.MaxValue - 6, far past the file; int.MinValue + 10 is int.MinValue.
    constexpr std::int32_t min = std::numeric_limits<std::int32_t>::min();
    manager.files["wraps"] = Uncompressed(Int32Body(), min + 3);
    manager.files["does-not-wrap"] = Uncompressed(Int32Body(), min + 10);
    EXPECT_THROW((void)manager.Load<std::int32_t>("wraps"), ContentLoadException);
    EXPECT_EQ(manager.Load<std::int32_t>("does-not-wrap"), 7);
}

TEST_F(ContentMalformedXnbTq016Test, ANegativeSharedResourceCountMeansNoSharedResources)
{
    // FF FF FF FF 0F is -1 and 80 80 80 80 08 is int.MinValue.
    manager.files["minus-one"] = Uncompressed(Int32Body({0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
    manager.files["min-value"] = Uncompressed(Int32Body({0x80, 0x80, 0x80, 0x80, 0x08}));
    EXPECT_EQ(manager.Load<std::int32_t>("minus-one"), 7);
    EXPECT_EQ(manager.Load<std::int32_t>("min-value"), 7);
}

TEST_F(ContentMalformedXnbTq016Test, ASharedReferenceWithoutAResourceTableIsNullReference)
{
    const auto reference = [](const std::int32_t index) {
        return [index](System::IO::BinaryWriter& w) { w.Write7BitEncodedInt(index); };
    };
    // No table: a count of zero, and a negative count (IL_001b-001d).
    manager.files["zero"] = Uncompressed(Body(SharedUserReaderName, {0x00}, reference(1)));
    manager.files["negative"] =
        Uncompressed(Body(SharedUserReaderName, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, reference(1)));
    EXPECT_THROW((void)manager.Load<std::shared_ptr<SharedUser>>("zero"), System::NullReferenceException);
    EXPECT_THROW((void)manager.Load<std::shared_ptr<SharedUser>>("negative"), System::NullReferenceException);

    // Controls: a null reference reads nothing, and an index past an existing table is BadXnb.
    manager.files["null"] = Uncompressed(Body(SharedUserReaderName, {0x00}, reference(0)));
    manager.files["past"] = Uncompressed(Body(SharedUserReaderName, {0x01}, [](System::IO::BinaryWriter& w) {
        w.Write7BitEncodedInt(2);
        w.Write7BitEncodedInt(0);
    }));
    EXPECT_EQ(manager.Load<std::shared_ptr<SharedUser>>("null")->value, -1);
    EXPECT_THROW((void)manager.Load<std::shared_ptr<SharedUser>>("past"), ContentLoadException);
}

TEST_F(ContentMalformedXnbTq016Test, AnLzxDecoderFailureLeavesLoadAsInvalidOperation)
{
    manager.files["invalid-frame"] = Lzx(InvalidLzxFrame(), 64);
    EXPECT_THROW((void)manager.Load<std::int32_t>("invalid-frame"), System::InvalidOperationException);
}

TEST_F(ContentMalformedXnbTq016Test, ADeclaredLengthThatCutsTheCompressedFramesIsInvalidOperation)
{
    // ReadBufferFromDisk reads only `totalLength - 14` compressed bytes; the frame then runs
    // past them. The bytes are still in the file: only the declaration cuts them.
    const Bytes body = Int32Body();
    const Bytes compressed = Compress(body);
    const auto cut = static_cast<std::int32_t>(14 + compressed.size() / 2);
    manager.files["cut"] = Lzx(compressed, static_cast<std::int32_t>(body.size()), cut);
    EXPECT_THROW((void)manager.Load<std::int32_t>("cut"), System::InvalidOperationException);

    // Below fourteen there is no compressed input at all (ReadNextBufferFromDisk IL_0000-0009).
    manager.files["no-input"] = Lzx(compressed, static_cast<std::int32_t>(body.size()), 13);
    EXPECT_THROW((void)manager.Load<std::int32_t>("no-input"), System::InvalidOperationException);
}

TEST_F(ContentMalformedXnbTq016Test, MissingDecodedBytesFailOnlyWhenTheReaderReachesThem)
{
    const Bytes body = Int32Body();
    // A declared size beyond the frames: Refresh asks the decoder for more only when the reader
    // runs out, so an asset that ends inside the decoded bytes loads.
    manager.files["generous"] = Lzx(Compress(body), static_cast<std::int32_t>(body.size()) + 100);
    EXPECT_EQ(manager.Load<std::int32_t>("generous"), 7);

    // The same declaration over a body cut before its Int32 fails where the reader needs it.
    const Bytes truncated(body.begin(), body.end() - 2);
    manager.files["needs-more"] = Lzx(Compress(truncated), static_cast<std::int32_t>(body.size()));
    EXPECT_THROW((void)manager.Load<std::int32_t>("needs-more"), System::InvalidOperationException);

    // A valid first frame followed by an invalid one: the asset ends in the first.
    Bytes valid = Compress(body);
    const Bytes invalid = InvalidLzxFrame();
    valid.insert(valid.end(), invalid.begin(), invalid.end());
    manager.files["valid-then-invalid"] = Lzx(valid, static_cast<std::int32_t>(body.size()) + 64);
    EXPECT_EQ(manager.Load<std::int32_t>("valid-then-invalid"), 7);
}

TEST_F(ContentMalformedXnbTq016Test, AByteBufferReadSurfacesTheDecompressionFailure)
{
    // ReadByteBuffer IL_0011-0031 reads until a Read returns nothing; on a compressed body that
    // Read is where DecompressStream fails. Sixteen bytes are promised, eight were encoded.
    const Bytes body = Body(BufferUserReaderName, {0x00}, [](System::IO::BinaryWriter& w) {
        w.Write(static_cast<std::int32_t>(16));
        for (std::uint8_t i = 0; i < 8; ++i) { w.Write(i); }
    });
    manager.files["short-buffer"] = Lzx(Compress(body), static_cast<std::int32_t>(body.size()) + 8);
    EXPECT_THROW((void)manager.Load<std::shared_ptr<Bytes>>("short-buffer"), System::InvalidOperationException);

    // The same body uncompressed simply ends: EndOfStream, which ReadAsset wraps as BadXnb.
    manager.files["short-plain"] = Uncompressed(body);
    EXPECT_THROW((void)manager.Load<std::shared_ptr<Bytes>>("short-plain"), ContentLoadException);
}

TEST_F(ContentMalformedXnbTq016Test, TheFrameThatSpendsTheDeclaredSizeIsServedWhole)
{
    // DecompressNextBuffer serves everything the call that spends decompressedTodo produced.
    const Bytes body = Int32Body();
    manager.files["smaller"] = Lzx(Compress(body), static_cast<std::int32_t>(body.size()) - 3);
    EXPECT_EQ(manager.Load<std::int32_t>("smaller"), 7);
}

TEST_F(ContentMalformedXnbTq016Test, ADeclaredSizeOfZeroOrLessIsAnEmptyBody)
{
    // IL_0000-000a: nothing is decompressed, so the first read ends the stream -- EndOfStream,
    // wrapped as BadXnb -- and the frames are never decoded, valid or not.
    const Bytes body = Int32Body();
    manager.files["zero"] = Lzx(Compress(body), 0);
    manager.files["negative"] = Lzx(InvalidLzxFrame(), -1);
    EXPECT_THROW((void)manager.Load<std::int32_t>("zero"), ContentLoadException);
    EXPECT_THROW((void)manager.Load<std::int32_t>("negative"), ContentLoadException);
}
