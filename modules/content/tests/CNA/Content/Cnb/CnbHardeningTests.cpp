// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-H001..CNBF-H005: the regression suite for the hardening pass.
//
// Each test here exists because a specific defect was found in the CNB implementation by review
// rather than by a failing test, which means the suite that was supposed to defend that property
// did not exist. These are those tests. They are written so that they would have FAILED against
// the code as it stood, not merely so that they pass against the code as it stands -- where that
// distinction is observable without a sanitizer it is noted in the test itself.

#include <any>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

using CNA::Content::CnbLoaderRegistry;
using CNA::Content::Cnb::CnbByteReader;
using CNA::Content::Cnb::CnbByteWriter;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbReadLimits;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::MakeChunkId;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;

namespace
{
    const CNA::Content::Cnb::CnbChunkId kPayload = MakeChunkId('H', 'A', 'R', 'D');

    /// A one-chunk file holding a single long string, and deliberately NO metadata chunk: the
    /// container decodes CMET during Parse(), so a fixture that carried one would have its type
    /// name measured against maxStringBytes too, and a test aiming a tight limit at the payload
    /// would fail on the metadata instead of on what it meant to test.
    std::vector<std::uint8_t> MakeStringChunkFile()
    {
        CnbByteWriter payload;
        payload.WriteString("a string long enough that a read limit can meaningfully forbid it");
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(kPayload, payload.Take(), CnbChunkFlags::None, 4u);
        return writer.Build();
    }

    /// Scribbles over a good deal of stack. Called between "the limits object died" and "the
    /// document is used again", so a retained pointer is reading something that has demonstrably
    /// been reused rather than something that merely happens to still look right.
    std::uint64_t ClobberStack()
    {
        volatile std::uint64_t scratch[512];
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < 512; ++i)
        {
            scratch[i] = 0xDEADBEEFCAFEF00Dull ^ static_cast<std::uint64_t>(i);
            sum += scratch[i];
        }
        return sum;
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-H001 -- read-limit lifetime
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, ADocumentParsedWithATemporaryLimitsObjectStaysUsableAfterwards)
{
    // The natural call. Before CNBF-H001 the document retained the ADDRESS of this temporary,
    // which died at the end of this full-expression -- so every later chunk read, and Limits()
    // itself, dereferenced freed stack. Under ASan this test faults; without it, the assertions
    // below on the limit VALUES are what catch a pointer into reused memory.
    const CnbDocument document =
        CnbDocument::Parse(MakeStringChunkFile(), "temp-limits.cnb", CnbReadLimits{});

    ASSERT_NE(ClobberStack(), 0u);

    EXPECT_EQ(document.Limits().maxStringBytes, CnbReadLimits{}.maxStringBytes);
    EXPECT_EQ(document.Limits().maxChunkCount, CnbReadLimits{}.maxChunkCount);
    EXPECT_EQ(document.Limits().maxFileSize, CnbReadLimits{}.maxFileSize);

    // And a cursor opened afterwards must enforce those same limits rather than whatever the
    // reused stack now holds.
    CnbByteReader reader = document.OpenChunk(document.RequireSingle(kPayload));
    EXPECT_NO_THROW((void)reader.ReadString());
}

TEST(CnbHardeningTest, ADocumentOutlivesALimitsObjectThatGoesOutOfScope)
{
    // The same defect in its other shape: a named local in a nested scope. The document is used
    // only after that scope has closed and the stack has been reused.
    std::optional<CnbDocument> document;
    {
        CnbReadLimits scoped;
        scoped.maxStringBytes = 32u;   // deliberately too small for the string in the fixture
        document = CnbDocument::Parse(MakeStringChunkFile(), "scoped-limits.cnb", scoped);
    }
    ASSERT_NE(ClobberStack(), 0u);

    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->Limits().maxStringBytes, 32u)
        << "the document must carry its own copy of the limits, not a reference to a dead one";

    // The tightened limit must still be enforced, which it cannot be if the copy was not taken.
    CnbByteReader reader = document->OpenChunk(document->RequireSingle(kPayload));
    EXPECT_THROW((void)reader.ReadString(), ContentLoadException);
}

TEST(CnbHardeningTest, AByteReaderOutlivesATemporaryLimitsObject)
{
    CnbByteWriter payload;
    payload.WriteString("abcdefghij");
    const std::vector<std::uint8_t> bytes = payload.Take();

    // Same again one level down: CnbByteReader is constructible directly, and its limits argument
    // is just as likely to be a temporary at the call site.
    CnbByteReader reader(bytes, "direct", CnbReadLimits{});
    ASSERT_NE(ClobberStack(), 0u);
    EXPECT_EQ(reader.ReadString(), "abcdefghij");
}

TEST(CnbHardeningTest, EveryChunkCursorInheritsTheDocumentsOwnLimits)
{
    CnbReadLimits tight;
    tight.maxStringBytes = 8u;
    const CnbDocument document =
        CnbDocument::Parse(MakeStringChunkFile(), "tight.cnb", tight);

    CnbByteReader reader = document.OpenChunk(document.RequireSingle(kPayload));
    EXPECT_THROW((void)reader.ReadString(), ContentLoadException)
        << "a cursor opened from the document must enforce the document's limits";
}

// --------------------------------------------------------------------------------------------
// CNBF-H002 -- custom asset type identity
// --------------------------------------------------------------------------------------------

namespace
{
    struct AlphaLevel { int marker = 0; };
    struct BetaLevel { int marker = 0; };

    /// Builds a custom-typed .cnb declaring `declaredTypeName`, under `assetTypeId`. The two are
    /// passed separately on purpose: a collision is exactly the case where the number matches and
    /// the name does not.
    std::vector<std::uint8_t> BuildCustomFile(std::uint32_t assetTypeId,
                                              const std::string& declaredTypeName,
                                              bool includeMetadata = true)
    {
        // Written through the raw chunk API rather than CnbWriter's own custom-type checks, so a
        // test can construct the very files CnbWriter now refuses to produce -- which is what a
        // reader has to survive receiving from somewhere else.
        CnbByteWriter meta;
        meta.WriteU32(0u);
        meta.WriteString(declaredTypeName);
        meta.WriteString("Levels/first");

        CnbByteWriter payload;
        payload.WriteU32(7u);

        CnbWriter writer(CnbAssetTypeId::Curve, 1u);  // placeholder; patched below
        writer.AddChunk(kPayload, payload.Take(), CnbChunkFlags::Mandatory, 4u);
        if (includeMetadata)
        {
            writer.AddChunk(CNA::Content::Cnb::CnbContainerChunk::Metadata, meta.Take(),
                            CnbChunkFlags::None, 4u);
        }
        std::vector<std::uint8_t> bytes = writer.Build();

        // Patch the header's asset type in place and repair the two structural checksums, which
        // is how a file gets a custom identifier without going through CnbWriter's guard.
        for (int i = 0; i < 4; ++i)
        {
            bytes[12u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((assetTypeId >> (8 * i)) & 0xFFu);
        }
        const std::uint32_t headerChecksum = CNA::Content::Cnb::Crc32c(
            std::span<const std::uint8_t>(bytes).first(
                CNA::Content::Cnb::Format::HeaderChecksumCoverage));
        for (int i = 0; i < 4; ++i)
        {
            bytes[44u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((headerChecksum >> (8 * i)) & 0xFFu);
        }
        return bytes;
    }

    class CustomTypeFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            alphaId = CNA::Content::Cnb::CnbAssetTypeIdFromName("HardeningGame.Alpha");
            CnbLoaderRegistry::Remove(alphaId);
            CnbLoaderRegistry::Register(
                alphaId, "HardeningGame.Alpha",
                [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
                { return std::any(AlphaLevel{42}); });
        }
        void TearDown() override { CnbLoaderRegistry::Remove(alphaId); }

        std::uint32_t alphaId = 0u;
    };
}

TEST_F(CustomTypeFixture, AMatchingCustomIdAndNameLoads)
{
    const CnbDocument document =
        CnbDocument::Parse(BuildCustomFile(alphaId, "HardeningGame.Alpha"), "alpha.cnb");
    ASSERT_EQ(document.AssetTypeId(), alphaId);

    const CnbLoaderRegistry::LoaderFn loader = CnbLoaderRegistry::ResolveForDocument(document);
    ASSERT_TRUE(static_cast<bool>(loader));
}

TEST_F(CustomTypeFixture, TheSameNumericIdWithADifferentNameIsRefused)
{
    // The defect CNBF-H002 exists for. A 31-bit hash means two unrelated game types can share an
    // identifier; before the fix the numeric match alone dispatched, so this file would have been
    // decoded by HardeningGame.Alpha's loader and silently misinterpreted.
    const CnbDocument document =
        CnbDocument::Parse(BuildCustomFile(alphaId, "OtherGame.Beta"), "collide.cnb");
    ASSERT_EQ(document.AssetTypeId(), alphaId) << "the fixture must actually collide numerically";

    try
    {
        (void)CnbLoaderRegistry::ResolveForDocument(document);
        FAIL() << "expected the collision to be refused";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("OtherGame.Beta"), std::string::npos) << message;
        EXPECT_NE(message.find("HardeningGame.Alpha"), std::string::npos) << message;
        EXPECT_NE(message.find("collide"), std::string::npos) << message;
    }
}

TEST_F(CustomTypeFixture, ACustomTypedFileWithNoCanonicalNameIsRefused)
{
    const CnbDocument document =
        CnbDocument::Parse(BuildCustomFile(alphaId, "", /*includeMetadata=*/false), "unnamed.cnb");
    EXPECT_THROW((void)CnbLoaderRegistry::ResolveForDocument(document), ContentLoadException);

    // ... and an empty name is no better than an absent chunk.
    const CnbDocument empty =
        CnbDocument::Parse(BuildCustomFile(alphaId, ""), "emptyname.cnb");
    EXPECT_THROW((void)CnbLoaderRegistry::ResolveForDocument(empty), ContentLoadException);
}

TEST_F(CustomTypeFixture, AnUnregisteredCustomIdIsRefusedByName)
{
    const std::uint32_t unknownId =
        CNA::Content::Cnb::CnbAssetTypeIdFromName("HardeningGame.NeverRegistered");
    const CnbDocument document = CnbDocument::Parse(
        BuildCustomFile(unknownId, "HardeningGame.NeverRegistered"), "unknown.cnb");

    try
    {
        (void)CnbLoaderRegistry::ResolveForDocument(document);
        FAIL() << "expected an unregistered type to be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("HardeningGame.NeverRegistered"), std::string::npos)
            << e.what();
    }
}

TEST_F(CustomTypeFixture, ABuiltInTypeDispatchesOnItsNumberAloneWhateverTheFileCallsItself)
{
    // The rule is deliberately asymmetric. CNA assigns built-in identifiers itself and freezes
    // them, so a numeric match IS a proof of identity there and the CMET name stays diagnostic.
    // Requiring a name match for built-ins would make the metadata chunk load-bearing for every
    // asset and break any file whose name string was ever tidied.
    Microsoft::Xna::Framework::Curve curve;
    curve.getKeysProperty().Add(Microsoft::Xna::Framework::CurveKey(0.0f, 1.0f));
    std::vector<std::uint8_t> bytes =
        CNA::Content::Cnb::EncodeCurveToCnb(curve, "Curves/whatever");

    const CnbDocument document = CnbDocument::Parse(bytes, "curve.cnb");
    ASSERT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Curve");
    EXPECT_NO_THROW((void)CnbLoaderRegistry::ResolveForDocument(document));
}

TEST_F(CustomTypeFixture, RegisteringACustomIdUnderANameItDoesNotHashToIsRefused)
{
    // Caught at registration, where the mistake is, rather than as a baffling collision error at
    // some later load.
    const auto loader = [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
    { return std::any(BetaLevel{}); };
    EXPECT_THROW(CnbLoaderRegistry::Register(alphaId, "NotWhatThisHashesTo", loader),
                 std::invalid_argument);
}

TEST_F(CustomTypeFixture, TheWriterRefusesToProduceACustomFileThatCouldNeverBeLoaded)
{
    {
        CnbWriter writer(alphaId, 1u);   // custom id, no metadata at all
        writer.AddChunk(kPayload, {1, 2, 3}, CnbChunkFlags::Mandatory, 4u);
        EXPECT_THROW((void)writer.Build(), ContentLoadException);
    }
    {
        CnbWriter writer(alphaId, 1u);
        writer.SetMetadata("", "Levels/first");
        writer.AddChunk(kPayload, {1, 2, 3}, CnbChunkFlags::Mandatory, 4u);
        EXPECT_THROW((void)writer.Build(), ContentLoadException);
    }
    {
        CnbWriter writer(alphaId, 1u);
        writer.SetMetadata("OtherGame.Beta", "Levels/first");   // name does not hash to alphaId
        writer.AddChunk(kPayload, {1, 2, 3}, CnbChunkFlags::Mandatory, 4u);
        EXPECT_THROW((void)writer.Build(), ContentLoadException);
    }
    {
        CnbWriter writer(alphaId, 1u);
        writer.SetMetadata("HardeningGame.Alpha", "Levels/first");
        writer.AddChunk(kPayload, {1, 2, 3}, CnbChunkFlags::Mandatory, 4u);
        EXPECT_NO_THROW((void)writer.Build());
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-H003 -- registry lifetime and concurrency
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, ALookedUpLoaderSurvivesLaterRegistryMutation)
{
    // Find() used to return a pointer INTO the registry's unordered_map. Registering enough
    // further types to force a rehash, or removing the entry outright, left that pointer dangling
    // -- and the caller had no way to know. It now returns a copy, so the loader taken here stays
    // callable no matter what happens to the table afterwards.
    const std::uint32_t id = CNA::Content::Cnb::CnbAssetTypeIdFromName("HardeningGame.Survivor");
    CnbLoaderRegistry::Remove(id);
    CnbLoaderRegistry::Register(
        id, "HardeningGame.Survivor",
        [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
        { return std::any(AlphaLevel{7}); });

    std::optional<CnbLoaderRegistry::LoaderFn> loader = CnbLoaderRegistry::Find(id);
    ASSERT_TRUE(loader.has_value());

    std::vector<std::uint32_t> churn;
    for (int i = 0; i < 256; ++i)
    {
        const std::string name = "HardeningGame.Churn" + std::to_string(i);
        const std::uint32_t churnId = CNA::Content::Cnb::CnbAssetTypeIdFromName(name);
        if (CnbLoaderRegistry::IsRegistered(churnId)) { continue; }
        CnbLoaderRegistry::Register(
            churnId, name,
            [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
            { return std::any(BetaLevel{}); });
        churn.push_back(churnId);
    }
    EXPECT_TRUE(CnbLoaderRegistry::Remove(id));

    // Still callable: the copy owns its own state.
    ContentManager cm(nullptr, ".");
    const CnbDocument document =
        CnbDocument::Parse(BuildCustomFile(id, "HardeningGame.Survivor"), "survivor.cnb");
    const std::any produced = (*loader)(document, cm, "survivor");
    EXPECT_EQ(std::any_cast<AlphaLevel>(produced).marker, 7);

    for (const std::uint32_t churnId : churn) { CnbLoaderRegistry::Remove(churnId); }
}

TEST(CnbHardeningTest, ConcurrentRegistrationLookupAndContentManagerConstructionAreRaceFree)
{
    // Every ContentManager constructor registers the built-in .cnb loaders, so a program that
    // builds content managers on more than one thread was mutating an unsynchronised global map.
    // This is the shape that finds it: several threads doing all three things at once, run under
    // TSan/ASan as well as plain. It asserts only outcomes that are true regardless of
    // interleaving, so it can never be flaky.
    constexpr int kThreads = 8;
    constexpr int kIterations = 200;

    std::vector<std::uint32_t> ids;
    for (int t = 0; t < kThreads; ++t)
    {
        ids.push_back(CNA::Content::Cnb::CnbAssetTypeIdFromName(
            "HardeningGame.Thread" + std::to_string(t)));
    }
    for (const std::uint32_t id : ids) { CnbLoaderRegistry::Remove(id); }

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t, &ids, &failures]() {
            const std::string name = "HardeningGame.Thread" + std::to_string(t);
            for (int i = 0; i < kIterations; ++i)
            {
                // Constructing a ContentManager registers the built-ins from this thread.
                ContentManager cm(nullptr, ".");
                if (!CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve)) { ++failures; }

                try
                {
                    CnbLoaderRegistry::Register(
                        ids[static_cast<std::size_t>(t)], name,
                        [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
                        { return std::any(AlphaLevel{}); });
                }
                catch (...)
                {
                    ++failures;
                }

                // Each thread owns its own id, so these outcomes do not depend on interleaving.
                if (!CnbLoaderRegistry::Find(ids[static_cast<std::size_t>(t)]).has_value())
                {
                    ++failures;
                }
                if (CnbLoaderRegistry::RegisteredTypeName(ids[static_cast<std::size_t>(t)]) != name)
                {
                    ++failures;
                }
                (void)CnbLoaderRegistry::Find(CnbAssetTypeId::AnimationClip);
                CnbLoaderRegistry::Remove(ids[static_cast<std::size_t>(t)]);
            }
        });
    }
    for (std::thread& thread : threads) { thread.join(); }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve));
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::AnimationClip));
    for (const std::uint32_t id : ids) { CnbLoaderRegistry::Remove(id); }
}

// --------------------------------------------------------------------------------------------
// CNBF-H005 -- overflow contract
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, ReadCountRefusesAProductThatWouldOverflowRatherThanWrapping)
{
    // ReadCount multiplied its u32 count by a caller-supplied u64 element size directly. Every
    // caller in CNA passes a small constant, so the product provably fits -- but that is a
    // property of the callers, not of the function, while the specification promised the
    // operation itself was safe. It now goes through CheckedMultiply, which this proves by
    // handing it an element size no caller would use.
    CnbByteWriter w;
    w.WriteU32(0xFFFFFFFFu);
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbByteReader reader(bytes, "overflow");
    CnbReadLimits permissive;
    permissive.maxArrayElementCount = 0xFFFFFFFFu;   // let the count itself through
    CnbByteReader unbounded(bytes, "overflow", permissive);

    // 0xFFFFFFFF * 0xFFFFFFFFFF would wrap a 64-bit product; the checked multiply refuses it.
    EXPECT_THROW((void)unbounded.ReadCount(0xFFFFFFFFFFull, "things"), ContentLoadException);

    // The ordinary path is unaffected: the limit still fires first for a normal element size.
    EXPECT_THROW((void)reader.ReadCount(4u, "things"), ContentLoadException);
}
