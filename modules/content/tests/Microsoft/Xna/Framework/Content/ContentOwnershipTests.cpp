// SPDX-License-Identifier: MS-PL
// ContentManager disposal ordering, recorder ownership and exceptional finalization.
#include <gtest/gtest.h>
#include <array>
#include <functional>
#include <filesystem>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Content;

namespace
{
    struct OwnedItem final : System::IDisposable
    {
        int label = 0;
        int disposals = 0;
        bool throwOnDispose = false;
        std::function<void()> onDispose;
        void Dispose() override
        {
            ++disposals;
            if (onDispose) onDispose();
            if (throwOnDispose) throw System::InvalidOperationException("dispose refusal");
        }
    };
    using Owned = std::shared_ptr<OwnedItem>;
    struct OwnedRoot { std::array<Owned, 3> items; };
    class OwnedItemReader final : public ContentTypeReader<Owned>
    {
    public:
        OwnedItemReader() : ContentTypeReader<Owned>("CNA.Repair.OwnedItem") { }
        static inline std::vector<Owned> created;
        static inline bool reuseOne = false;
    protected:
        Owned Read(ContentReader& input, std::optional<Owned>) override
        {
            const auto label = input.ReadInt32();
            if (reuseOne && !created.empty()) return created.front();
            auto item = std::make_shared<OwnedItem>();
            item->label = label;
            created.push_back(item);
            return item;
        }
    };
    class OwnedRootReader final : public ContentTypeReader<OwnedRoot>
    {
    public:
        OwnedRootReader() : ContentTypeReader<OwnedRoot>("CNA.Repair.OwnedRoot") { }
    protected:
        OwnedRoot Read(ContentReader& input, std::optional<OwnedRoot>) override
        {
            OwnedRoot root;
            for (auto& item : root.items) item = input.ReadObject<Owned>();
            return root;
        }
    };
    class OwnershipProbeManager final : public ContentManager
    {
    public:
        using ContentManager::ReadAsset;
        using ContentManager::Dispose;
        explicit OwnershipProbeManager(std::vector<uint8_t> bytes, bool strict = true) : bytes_(std::move(bytes))
        {
            // Isolate ISSUE-011 from the separately tracked OpenStream routing ISSUE-012.
            RouteLoadsThroughOpenStreamEXT(strict);
        }
        int opens = 0;
    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            ++opens;
            return std::make_unique<System::IO::MemoryStream>(
                bytes_.data(), static_cast<int32_t>(bytes_.size()), false);
        }
    private:
        std::vector<uint8_t> bytes_;
    };
    class ContentOwnershipCompatibilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            OwnedItemReader::created.clear();
            OwnedItemReader::reuseOne = false;
            ContentTypeReaderManager::AddTypeCreator("CNA.Repair.OwnedRootReader",
                [] { return std::make_unique<OwnedRootReader>(); });
            ContentTypeReaderManager::AddTypeCreator("CNA.Repair.OwnedItemReader",
                [] { return std::make_unique<OwnedItemReader>(); });
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            OwnedItemReader::created.clear();
        }
        static std::vector<uint8_t> Xnb(bool truncate = false)
        {
            System::IO::MemoryStream body;
            System::IO::BinaryWriter writer(&body, true);
            writer.Write7BitEncodedInt(2);
            writer.Write(std::string("CNA.Repair.OwnedRootReader")); writer.Write(int32_t{0});
            writer.Write(std::string("CNA.Repair.OwnedItemReader")); writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(0);
            writer.Write7BitEncodedInt(1);
            for (int i = 1; i <= (truncate ? 2 : 3); ++i)
            {
                writer.Write7BitEncodedInt(2);
                writer.Write(static_cast<int32_t>(i));
            }
            writer.Flush();
            const auto payload = body.ToArray();
            System::IO::MemoryStream file;
            System::IO::BinaryWriter header(&file, true);
            for (const auto value : std::array<uint8_t, 6>{'X', 'N', 'B', 'w', 5, 0})
                header.Write(value);
            header.Write(static_cast<int32_t>(payload.size() + 10));
            for (const auto value : payload) header.Write(value);
            header.Flush();
            const auto bytes = file.ToArray();
            return {bytes.begin(), bytes.end()};
        }
        static void CheckDisposed(const OwnedRoot& root, int expected)
        {
            for (const auto& item : root.items)
            {
                ASSERT_NE(item, nullptr);
                EXPECT_EQ(item->disposals, expected);
            }
        }
    };
TEST_F(ContentOwnershipCompatibilityTest, UnloadDisposesMaterializedObjectsAndClearsCache)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    const auto cached = manager.Load<OwnedRoot>("asset");
    EXPECT_EQ(root.items, cached.items);
    EXPECT_EQ(manager.opens, 1);
    manager.Unload();
    CheckDisposed(root, 1);
    manager.Unload();
    CheckDisposed(root, 1);
    const auto reloaded = manager.Load<OwnedRoot>("asset");
    EXPECT_NE(root.items[0], reloaded.items[0]);
    EXPECT_EQ(manager.opens, 2);
    manager.Unload();
    CheckDisposed(reloaded, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, ExplicitDisposeUsesTheSameOwnershipRecords)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    manager.Dispose();
    CheckDisposed(root, 1);
    manager.Dispose();
    CheckDisposed(root, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, CallerRecorderOwnsDisposalAndPreservesOrder)
{
    OwnershipProbeManager manager(Xnb());
    std::vector<std::shared_ptr<System::IDisposable>> recorded;
    const auto root = manager.ReadAsset<OwnedRoot>("asset",
        [&](auto value) { recorded.push_back(std::move(value)); });
    ASSERT_EQ(recorded.size(), 3U);
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(recorded[i].get(), root.items[i].get());
        EXPECT_EQ(root.items[i]->label, static_cast<int>(i + 1));
    }
    manager.Unload();
    CheckDisposed(root, 0);
    for (const auto& item : recorded) item->Dispose();
    CheckDisposed(root, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, RepeatedMaterializationsAreNotIdentityDeduplicated)
{
    OwnershipProbeManager manager(Xnb());
    OwnedItemReader::reuseOne = true;
    const auto root = manager.ReadAsset<OwnedRoot>("asset", {});
    ASSERT_EQ(root.items[0], root.items[1]);
    ASSERT_EQ(root.items[1], root.items[2]);
    manager.Unload();
    EXPECT_EQ(root.items[0]->disposals, 3);
}

TEST_F(ContentOwnershipCompatibilityTest, FailedReadRetainsAlreadyCreatedDisposables)
{
    OwnershipProbeManager manager(Xnb(true));
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("asset", {}), ContentLoadException);
    ASSERT_EQ(OwnedItemReader::created.size(), 2U);
    manager.Unload();
    for (const auto& item : OwnedItemReader::created) EXPECT_EQ(item->disposals, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, FailedUnloadStopsThenClearsCacheAndRecords)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    root.items[0]->throwOnDispose = true;
    EXPECT_THROW(manager.Unload(), System::InvalidOperationException);
    EXPECT_EQ(root.items[0]->disposals, 1);
    EXPECT_EQ(root.items[1]->disposals, 0);
    EXPECT_EQ(root.items[2]->disposals, 0);
    EXPECT_NO_THROW(manager.Unload());
    EXPECT_EQ(root.items[0]->disposals, 1);
    EXPECT_NO_THROW(manager.setRootDirectoryProperty(std::string("after-failure")));
    const auto reloaded = manager.Load<OwnedRoot>("asset");
    EXPECT_NE(reloaded.items[0], root.items[0]);
    manager.Unload();
    CheckDisposed(reloaded, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, ThrowingCallerRecorderDoesNotTransferOwnershipBack)
{
    OwnershipProbeManager manager(Xnb());
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("asset",
        [](auto) { throw System::InvalidOperationException("recorder refusal"); }),
        System::InvalidOperationException);
    ASSERT_EQ(OwnedItemReader::created.size(), 1U);
    manager.Unload();
    EXPECT_EQ(OwnedItemReader::created.front()->disposals, 0);
}

TEST_F(ContentOwnershipCompatibilityTest, DisposedManagerRefusesLoadReadAndUnloadBeforeOpening)
{
    OwnershipProbeManager manager(Xnb());
    manager.Dispose();
    EXPECT_THROW((void)manager.Load<OwnedRoot>("asset"), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.Load<OwnedRoot>(""), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("asset", {}), System::ObjectDisposedException);
    EXPECT_THROW(manager.Unload(), System::ObjectDisposedException);
    EXPECT_NO_THROW(manager.Dispose());
    EXPECT_EQ(manager.opens, 0);
}

TEST_F(ContentOwnershipCompatibilityTest, FailedDisposeStopsThenPermanentlyDisposesManager)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    root.items[1]->throwOnDispose = true;
    EXPECT_THROW(manager.Dispose(), System::InvalidOperationException);
    EXPECT_EQ(root.items[0]->disposals, 1);
    EXPECT_EQ(root.items[1]->disposals, 1);
    EXPECT_EQ(root.items[2]->disposals, 0);
    EXPECT_NO_THROW(manager.Dispose());
    EXPECT_EQ(root.items[1]->disposals, 1);
    EXPECT_THROW(manager.Unload(), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.Load<OwnedRoot>("asset"), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("asset", {}), System::ObjectDisposedException);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, NonManagedDisposeDropsRecordsWithoutCallingObjects)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    manager.Dispose(false);
    CheckDisposed(root, 0);
    EXPECT_NO_THROW(manager.Dispose());
    CheckDisposed(root, 0);
    EXPECT_THROW(manager.Unload(), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.Load<OwnedRoot>("asset"), System::ObjectDisposedException);
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("asset", {}), System::ObjectDisposedException);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, LoadingDuringDisposalInvalidatesEnumerationAndClearsRecords)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("asset");
    OwnedRoot additional;
    root.items[0]->onDispose = [&] { additional = manager.ReadAsset<OwnedRoot>("additional", {}); };
    EXPECT_THROW(manager.Unload(), System::InvalidOperationException);
    EXPECT_EQ(root.items[0]->disposals, 1);
    EXPECT_EQ(root.items[1]->disposals, 0);
    EXPECT_EQ(root.items[2]->disposals, 0);
    ASSERT_EQ(manager.opens, 2);
    CheckDisposed(additional, 0);
    EXPECT_NO_THROW(manager.Unload());
    EXPECT_EQ(root.items[0]->disposals, 1);
    CheckDisposed(additional, 0);
    EXPECT_NO_THROW(manager.setRootDirectoryProperty(std::string("after-reentrant-read")));
}

TEST_F(ContentOwnershipCompatibilityTest, CacheAliasesShareIdentityAndWrongTypeRefusesBeforeOpening)
{
    OwnershipProbeManager manager(Xnb());
    const auto root = manager.Load<OwnedRoot>("folder/asset");
    const auto alias = manager.Load<OwnedRoot>("FOLDER\\ASSET");
    EXPECT_EQ(alias.items, root.items);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_THROW((void)manager.Load<std::string>("folder/ASSET"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(manager.Load<OwnedRoot>("folder/asset").items, root.items);
    CheckDisposed(root, 0);
    manager.Unload();
    CheckDisposed(root, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, TypedAndExternalReferenceLoadsUseOneCacheEntry)
{
    const auto external = [](ContentManager& manager)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(std::string("asset"));
        writer.Flush();
        stream.setPositionProperty(0);
        ContentReader reader(&manager, &stream, "parent", 5, 'w');
        return std::any_cast<OwnedRoot>(reader.ReadExternalReference());
    };
    OwnershipProbeManager manager(Xnb());
    const auto typedFirst = manager.Load<OwnedRoot>("asset");
    EXPECT_EQ(external(manager).items, typedFirst.items);
    EXPECT_EQ(manager.opens, 1);
    manager.Unload();
    CheckDisposed(typedFirst, 1);
    const auto externalFirst = external(manager);
    EXPECT_EQ(manager.Load<OwnedRoot>("ASSET").items, externalFirst.items);
    EXPECT_EQ(manager.opens, 2);
    manager.Unload();
    CheckDisposed(externalFirst, 1);
}

TEST_F(ContentOwnershipCompatibilityTest, EmptyLoadRefusesBeforeOpenInBothResolutionModes)
{
    for (bool strict : {false, true})
    {
        OwnershipProbeManager manager(Xnb(), strict);
        EXPECT_THROW((void)manager.Load<OwnedRoot>(""), System::ArgumentNullException);
        EXPECT_EQ(manager.opens, 0);
        EXPECT_NO_THROW(manager.setRootDirectoryProperty(std::string("new-root")));
        manager.Dispose();
        EXPECT_THROW((void)manager.Load<OwnedRoot>(""), System::ObjectDisposedException);
        EXPECT_EQ(manager.opens, 0);
    }
}

TEST_F(ContentOwnershipCompatibilityTest, RootAssignmentRefusalPreservesCacheAndBothSetterArguments)
{
    OwnershipProbeManager manager(Xnb());
    const std::string original = "original";
    manager.setRootDirectoryProperty(original);
    const auto root = manager.Load<OwnedRoot>("asset");
    EXPECT_THROW(manager.setRootDirectoryProperty(original), System::InvalidOperationException);
    std::string proposed = "replacement";
    EXPECT_THROW(manager.setRootDirectoryProperty(std::move(proposed)), System::InvalidOperationException);
    EXPECT_EQ(proposed, "replacement");
    EXPECT_EQ(manager.getRootDirectoryProperty(), original);
    EXPECT_EQ(manager.Load<OwnedRoot>("asset").items, root.items);
    EXPECT_EQ(manager.opens, 1);
    std::vector<int> trace;
    for (const auto& item : root.items)
        item->onDispose = [&, label = item->label] { trace.push_back(label); };
    manager.Unload();
    EXPECT_EQ(trace, (std::vector<int>{1, 2, 3}));
    CheckDisposed(root, 1);
    EXPECT_NO_THROW(manager.setRootDirectoryProperty(std::move(proposed)));
    EXPECT_EQ(manager.getRootDirectoryProperty(), "replacement");
    const auto reloaded = manager.Load<OwnedRoot>("asset");
    EXPECT_NE(reloaded.items, root.items);
    EXPECT_EQ(manager.opens, 2);
    manager.Unload();
}

TEST_F(ContentOwnershipCompatibilityTest, MissingCompiledAssetFailsAtOpenWithoutCachingAndCanBeRetried)
{
    class MissingManager final : public ContentManager
    {
    public:
        using ContentManager::ReadAsset;
        MissingManager() { RouteLoadsThroughOpenStreamEXT(true); }
        int opens = 0;
    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name) override
        {
            ++opens;
            return ContentManager::OpenStream(name);
        }
    } manager;
    manager.setRootDirectoryProperty(std::filesystem::temp_directory_path().string());
    for (int attempt = 1; attempt <= 2; ++attempt)
    {
        EXPECT_THROW((void)manager.Load<OwnedRoot>("conf004-absent-content-7a5c43c9"), ContentLoadException);
        EXPECT_EQ(manager.opens, attempt);
        EXPECT_TRUE(OwnedItemReader::created.empty());
        EXPECT_NO_THROW(manager.setRootDirectoryProperty(std::filesystem::temp_directory_path().string()));
        EXPECT_NO_THROW(manager.Unload());
    }
    manager.Dispose();
    EXPECT_THROW((void)manager.ReadAsset<OwnedRoot>("missing", {}), System::ObjectDisposedException);
    EXPECT_EQ(manager.opens, 2);
}
}
