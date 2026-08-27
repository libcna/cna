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
#include <array>
#include <atomic>
#include <bit>
#include <limits>
#include <utility>
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
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
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
            // The built-ins are normally put in place by any ContentManager constructor, so a test
            // that runs inside a process where one already exists sees them for free. ctest runs
            // each case in its OWN process, where nothing has -- and a test that quietly depended
            // on a sibling to register them would pass under a filtered run and fail under ctest.
            // It did exactly that; asking for them explicitly is the fix.
            CnbLoaderRegistry::RegisterBuiltIns();

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

// --------------------------------------------------------------------------------------------
// CNBF-H008 -- bone and skeleton graph ordering
// --------------------------------------------------------------------------------------------

namespace
{
    CNA::Content::Cnb::CnbModelPart MakeTrivialPart()
    {
        CNA::Content::Cnb::CnbModelPart part;
        part.name = "Only";
        part.vertexStride = 16u;
        part.vertexCount = 3u;
        part.indexCount = 3u;
        part.indexElementSize = 2u;
        part.primitiveTopology = 4u;
        part.primitiveCount = 1u;
        part.vertexBytes.assign(16u * 3u, 0u);
        part.indexBytes.assign(2u * 3u, 0u);
        return part;
    }

    CNA::Content::Cnb::CnbModelData MakeModelWithBones(
        const std::vector<std::pair<std::string, std::int32_t>>& bones)
    {
        CNA::Content::Cnb::CnbModelData model;
        for (const auto& [name, parent] : bones)
        {
            CNA::Content::Cnb::CnbModelBone bone;   // never braced -- see CNBF-H009 below
            bone.name = name;
            bone.parent = parent;
            model.bones.push_back(std::move(bone));
        }
        model.hasBoneHierarchy = model.bones.size() > 1u;
        model.parts = {MakeTrivialPart()};
        CNA::Content::Cnb::CnbModelMesh mesh;
        mesh.name = "Only";
        mesh.parentBone = model.bones.empty() ? -1 : 0;
        mesh.partIndices = {0u};
        model.meshes = {mesh};
        return model;
    }
}

TEST(CnbHardeningTest, ABoneTableThatIsNotParentBeforeChildIsRefused)
{
    // Not a tidiness rule. Model::CopyAbsoluteBoneTransformsTo composes world transforms in ONE
    // ascending pass, reading dest[parentIndex] as it goes -- so a bone whose parent comes later
    // reads a slot that has not been written yet and silently places geometry somewhere it does
    // not belong. It would not crash, which is exactly why the format has to refuse it.
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(
                     MakeModelWithBones({{"Root", -1}, {"Child", 2}, {"Later", 0}})),
                 ContentLoadException);

    // A two-bone cycle is the same defect in its worst form, and the same check rules it out.
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(
                     MakeModelWithBones({{"Root", -1}, {"A", 2}, {"B", 1}})),
                 ContentLoadException);

    // A bone that is its own parent, likewise.
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(
                     MakeModelWithBones({{"Root", -1}, {"Self", 1}})),
                 ContentLoadException);

    // The ordinary shape still encodes.
    EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(
        MakeModelWithBones({{"Root", -1}, {"Hips", 0}, {"Head", 1}})));
}

TEST(CnbHardeningTest, ACorruptBoneParentInAFileIsRefusedByTheDecoder)
{
    // The encoder check above protects a caller building a model in memory. A file arriving from
    // elsewhere gets the same guarantee, which is the one that matters for untrusted input.
    const std::vector<std::uint8_t> good = CNA::Content::Cnb::EncodeModelToCnb(
        MakeModelWithBones({{"Root", -1}, {"Hips", 0}, {"Head", 1}}));
    const CnbDocument source = CnbDocument::Parse(good, "bones.cnb");

    // Rebuild the file with bone 1's parent rewritten to point forward at bone 2.
    const std::size_t bonesChunk = source.RequireSingle(CNA::Content::Cnb::CnbModelChunk::Bones);
    const auto data = source.ChunkData(bonesChunk);
    std::vector<std::uint8_t> bones(data.begin(), data.end());
    ASSERT_EQ(bones.size(), 3u * CNA::Content::Cnb::CnbModelBoneStride);
    // Bone 1's parent field: one stride in, then past the u32 name index.
    const std::size_t parentAt = CNA::Content::Cnb::CnbModelBoneStride + 4u;
    bones[parentAt] = 0x02u;
    bones[parentAt + 1] = bones[parentAt + 2] = bones[parentAt + 3] = 0x00u;

    CnbWriter rebuilt(CnbAssetTypeId::Model, 1u);
    for (std::size_t i = 0; i < source.ChunkCount(); ++i)
    {
        const auto& entry = source.ChunkAt(i);
        if (entry.type == CNA::Content::Cnb::CnbContainerChunk::Metadata) { continue; }
        const auto chunk = source.ChunkData(i);
        rebuilt.AddChunk(entry.type,
                         entry.type == CNA::Content::Cnb::CnbModelChunk::Bones
                             ? bones
                             : std::vector<std::uint8_t>(chunk.begin(), chunk.end()),
                         entry.flags, entry.alignment);
    }

    const CnbDocument corrupted = CnbDocument::Parse(rebuilt.Build(), "forward-parent.cnb");
    EXPECT_THROW((void)CNA::Content::Cnb::DecodeModelFromCnb(corrupted), ContentLoadException);
}

TEST(CnbHardeningTest, ASkeletonThatIsNotParentBeforeChildIsRefused)
{
    CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}});
    CNA::Content::Cnb::CnbModelSkeleton skeleton;
    skeleton.hierarchy = {-1, 2, 0};   // joint 1 names a later joint
    skeleton.bindPose.resize(3);
    skeleton.inverseBindPose.resize(3);
    model.skeleton = skeleton;
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model), ContentLoadException);

    model.skeleton->hierarchy = {-1, 0, 1};
    EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model));
}

// --------------------------------------------------------------------------------------------
// CNBF-H009 -- the aggregate-initialisation hazard, made permanently visible
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, ADefaultConstructedBoneCarriesAnIdentityTransform)
{
    // The defect this pins was real: `CnbModelBone{name, parent, {}}` is aggregate
    // initialisation, and supplying `{}` for the transform SUPPRESSES its identity
    // default-member-initialiser and value-initialises the matrix to all zeros instead. Every
    // synthesised bone was compiled with a zero transform.
    //
    // Two assertions, because they fail for different reasons: the first breaks if someone edits
    // the default-member-initialiser, the second breaks if someone reintroduces the braced form
    // in a place this test can see.
    const CNA::Content::Cnb::CnbModelBone defaulted;
    const std::array<float, 16> identity{{1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 1.0f}};
    EXPECT_EQ(defaulted.transform, identity)
        << "CnbModelBone's default transform must be identity";

    const CNA::Content::Cnb::CnbModelBone braced{"Braced", 0, {}};
    EXPECT_NE(braced.transform, identity)
        << "if this now passes, the aggregate-initialisation hazard has been designed away and "
           "this test should be replaced by one that pins whatever replaced it";

    // ... and a default-constructed bone survives a round trip as identity, which is the property
    // the compiler actually depends on.
    CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}, {"Child", 0}});
    const CNA::Content::Cnb::CnbModelData decoded = CNA::Content::Cnb::DecodeModelFromCnb(
        CnbDocument::Parse(CNA::Content::Cnb::EncodeModelToCnb(model), "identity.cnb"));
    ASSERT_EQ(decoded.bones.size(), 2u);
    EXPECT_EQ(decoded.bones[1].transform, identity);
}

// --------------------------------------------------------------------------------------------
// CNBF-H010 -- container coverage the original suite left open
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, ManyChunksOfOneTypeAreAddressableByOrdinal)
{
    // Per-type ordinals exist so a model is not capped at a handful of primitives (that was the
    // reason the format rejected the 'VB00'/'VB01' sketch). The original suite only ever created
    // two or three chunks of a type, which does not exercise that at all.
    constexpr std::uint32_t kCount = 500u;
    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        CnbByteWriter payload;
        payload.WriteU32(i);
        writer.AddChunk(kPayload, payload.Take(), CnbChunkFlags::None, 16u);
    }

    const CnbDocument document = CnbDocument::Parse(writer.Build(), "many.cnb");
    const std::vector<std::size_t> all = document.FindAll(kPayload);
    ASSERT_EQ(all.size(), kCount);
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        CnbByteReader reader = document.OpenChunk(all[i]);
        EXPECT_EQ(reader.ReadU32(), i) << "ordinal " << i << " addressed the wrong chunk";
        EXPECT_EQ(document.ChunkAt(all[i]).offset % 16u, 0u) << "ordinal " << i;
    }
    // A type with exactly one chunk still has to be a singleton for RequireSingle's purposes.
    EXPECT_THROW((void)document.RequireSingle(kPayload), ContentLoadException);
}

TEST(CnbHardeningTest, AChunkOverlappingTheTableOfContentsIsRefused)
{
    // The suite tested a table of contents overlapping the header, and chunks overlapping each
    // other, but not a chunk reaching back into the table. The region partition covers it; this
    // proves it does.
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(kPayload, {1, 2, 3, 4}, CnbChunkFlags::None, 4u);
    std::vector<std::uint8_t> bytes = writer.Build();

    // Point the single chunk at the table of contents rather than at its own payload.
    const std::size_t entryAt = 64u;
    const std::uint64_t tocOffset = 64u;
    for (int i = 0; i < 8; ++i)
    {
        bytes[entryAt + 8u + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((tocOffset >> (8 * i)) & 0xFFu);
    }
    // Repair the chunk checksum for its new location, then the two structural checksums, so the
    // ONLY thing wrong with the file is the overlap.
    const std::uint32_t chunkCrc = CNA::Content::Cnb::Crc32c(
        std::span<const std::uint8_t>(bytes).subspan(static_cast<std::size_t>(tocOffset), 4u));
    for (int i = 0; i < 4; ++i)
    {
        bytes[entryAt + 32u + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((chunkCrc >> (8 * i)) & 0xFFu);
    }
    const std::uint32_t tocCrc = CNA::Content::Cnb::Crc32c(
        std::span<const std::uint8_t>(bytes).subspan(64u, 48u));
    for (int i = 0; i < 4; ++i)
    {
        bytes[40u + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((tocCrc >> (8 * i)) & 0xFFu);
    }
    const std::uint32_t headerCrc = CNA::Content::Cnb::Crc32c(
        std::span<const std::uint8_t>(bytes).first(44u));
    for (int i = 0; i < 4; ++i)
    {
        bytes[44u + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((headerCrc >> (8 * i)) & 0xFFu);
    }

    EXPECT_THROW((void)CnbDocument::Parse(bytes, "chunk-in-toc.cnb"), ContentLoadException);
}

TEST(CnbHardeningTest, FloatingPointValuesRoundTripBitForBit)
{
    // CNB stores IEEE-754 bit patterns and does not normalise them. NaN, both infinities, negative
    // zero and a denormal must therefore come back EXACTLY, which value comparison cannot check --
    // NaN != NaN, and -0.0 == 0.0. Compared as bits.
    const std::vector<float> floats = {
        0.0f, -0.0f,
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::min(), std::numeric_limits<float>::max(),
        3.14159265f, -1.0f,
    };
    const std::vector<double> doubles = {
        0.0, -0.0,
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), 2.718281828459045,
    };

    CnbByteWriter w;
    for (const float value : floats) { w.WriteF32(value); }
    for (const double value : doubles) { w.WriteF64(value); }
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbByteReader reader(bytes, "floats");
    for (const float expected : floats)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(reader.ReadF32()),
                  std::bit_cast<std::uint32_t>(expected));
    }
    for (const double expected : doubles)
    {
        EXPECT_EQ(std::bit_cast<std::uint64_t>(reader.ReadF64()),
                  std::bit_cast<std::uint64_t>(expected));
    }
    EXPECT_NO_THROW(reader.RequireExhausted());

    // -0.0f must be distinguishable from 0.0f in the encoding, which is the case value comparison
    // would silently let through.
    CnbByteWriter positive;
    positive.WriteF32(0.0f);
    CnbByteWriter negative;
    negative.WriteF32(-0.0f);
    EXPECT_NE(positive.Take(), negative.Take());
}

TEST(CnbHardeningTest, AnAssetSchemaRejectsNonFiniteTimesEvenThoughThePrimitiveLayerAcceptsThem)
{
    // The two layers have different jobs, and the split is deliberate: the primitive reader stores
    // whatever bits the file holds, and the schema decides what is meaningful for its own fields.
    // A NaN duration is refused not because f64 cannot hold it but because System::TimeSpan
    // cannot -- and that refusal belongs to AnimationClip, not to ReadF64.
    CnbByteWriter header;
    header.WriteF64(std::numeric_limits<double>::quiet_NaN());
    header.WriteU32(0u);
    header.WriteU32(0u);
    header.WriteU32(0u);

    CnbWriter writer(CnbAssetTypeId::AnimationClip, 1u);
    writer.AddChunk(CNA::Content::Cnb::CnbAnimationClipChunk::Header, header.Take(),
                    CnbChunkFlags::Mandatory, 8u);
    writer.AddChunk(CNA::Content::Cnb::CnbAnimationClipChunk::Tracks, {},
                    CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbAnimationClipChunk::Keys, {},
                    CnbChunkFlags::Mandatory, 8u);

    const CnbDocument document = CnbDocument::Parse(writer.Build(), "nan-clip.cnb");
    EXPECT_THROW((void)CNA::Content::Cnb::DecodeAnimationClipFromCnb(document),
                 ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-H012 -- redundant and out-of-range geometry metadata
// --------------------------------------------------------------------------------------------

TEST(CnbHardeningTest, APrimitiveCountThatItsIndicesDoNotDescribeIsRefused)
{
    // primitiveCount is derivable from the topology and the index count, so storing it is
    // redundant -- and redundant data in a binary format is only safe if it is cross-checked. A
    // part claiming more primitives than its indices describe draws past the end of its own index
    // buffer, which is a GPU-side out-of-range read the loader has no business forwarding.
    CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}});
    model.parts[0].primitiveCount = 99u;   // 3 triangle-list indices describe exactly 1
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model), ContentLoadException);

    // Too few is just as wrong as too many: the two numbers must agree exactly.
    model.parts[0].primitiveCount = 0u;
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model), ContentLoadException);

    model.parts[0].primitiveCount = 1u;
    EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model));
}

TEST(CnbHardeningTest, ThePrimitiveCountRuleFollowsTheTopologyRatherThanAssumingTriangles)
{
    // Each topology counts differently, and a check that assumed triangle lists would wave every
    // strip and fan through. Six indices: 6 points, 3 lines, 5 line-strip segments, 2 triangles,
    // 4 strip/fan triangles.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> topologyAndCount = {
        {0u, 6u}, {1u, 3u}, {2u, 5u}, {3u, 5u}, {4u, 2u}, {5u, 4u}, {6u, 4u}};

    for (const auto& [topology, expected] : topologyAndCount)
    {
        CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}});
        model.parts[0].vertexCount = 6u;
        model.parts[0].vertexBytes.assign(16u * 6u, 0u);
        model.parts[0].indexCount = 6u;
        model.parts[0].indexBytes.assign(2u * 6u, 0u);
        for (std::uint8_t i = 0; i < 6u; ++i) { model.parts[0].indexBytes[i * 2u] = i; }
        model.parts[0].primitiveTopology = topology;

        model.parts[0].primitiveCount = expected;
        EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model))
            << "topology " << topology << " should accept " << expected;

        model.parts[0].primitiveCount = expected + 1u;
        EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model), ContentLoadException)
            << "topology " << topology << " should reject " << (expected + 1u);
    }
}

TEST(CnbHardeningTest, AnIndexAddressingAVertexThePartDoesNotHaveIsRefused)
{
    // Reaches the GPU as an out-of-range fetch, so it is refused at decode -- one pass over bytes
    // that are being copied anyway.
    CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}});
    // vertexCount is 3; point the last index at vertex 3.
    model.parts[0].indexBytes[4] = 0x03u;

    // The encoder does not scan index values (it trusts the caller that built the arrays), so the
    // file is produced -- and the DECODER, which is where untrusted bytes arrive, refuses it.
    const std::vector<std::uint8_t> bytes = CNA::Content::Cnb::EncodeModelToCnb(model);
    const CnbDocument document = CnbDocument::Parse(bytes, "wild-index.cnb");
    try
    {
        (void)CNA::Content::Cnb::DecodeModelFromCnb(document);
        FAIL() << "expected an out-of-range index to be refused";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("addresses vertex 3"), std::string::npos) << message;
        EXPECT_NE(message.find("only 3"), std::string::npos) << message;
    }
}

TEST(CnbHardeningTest, ThirtyTwoBitIndicesAreRangeCheckedToo)
{
    CNA::Content::Cnb::CnbModelData model = MakeModelWithBones({{"Root", -1}});
    model.parts[0].indexElementSize = 4u;
    model.parts[0].indexBytes.assign(4u * 3u, 0u);
    for (std::uint32_t i = 0; i < 3u; ++i) { model.parts[0].indexBytes[i * 4u] = static_cast<std::uint8_t>(i); }

    EXPECT_NO_THROW((void)CNA::Content::Cnb::DecodeModelFromCnb(CnbDocument::Parse(
        CNA::Content::Cnb::EncodeModelToCnb(model), "wide-ok.cnb")));

    // A value that only shows up above the low byte -- the check must read the whole element, not
    // just the first byte.
    model.parts[0].indexBytes[4u + 1u] = 0x01u;   // index 1 becomes 0x0100 = 256
    EXPECT_THROW((void)CNA::Content::Cnb::DecodeModelFromCnb(CnbDocument::Parse(
                     CNA::Content::Cnb::EncodeModelToCnb(model), "wide-bad.cnb")),
                 ContentLoadException);
}
