// SPDX-License-Identifier: MS-PL
//
// TQ-018: an .xnb whose external references lead back to an asset still being read.
//
// Oracle: Microsoft.Xna.Framework.dll ContentReader.ReadExternalReference<T> IL_0000-0033 reads
// the name, resolves it (GetPathToReference, TitleContainer.GetCleanPath) and calls
// ContentManager.Load<T>; Load<T> looks the cache up (IL_0038), reads (ReadAsset<T>, IL_0099) and
// only then caches (Dictionary.Add, IL_00ac), so a reference back to an asset being read recurses
// until the stack is gone, in XNA and in FNA alike. A process crash is no contract a game relies
// on; CNA refuses the cycle with ContentLoadException, Load<T>'s documented refusal, on the first
// repetition.

#include <any>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using ObjectDictionary = std::map<std::string, std::any>;

    constexpr const char* DictionaryOfObjects =
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]";
    constexpr const char* StringReader = "Microsoft.Xna.Framework.Content.StringReader";
    constexpr const char* ExternalReferenceReader = "Microsoft.Xna.Framework.Content.ExternalReferenceReader";
    constexpr const char* Int32Reader = "Microsoft.Xna.Framework.Content.Int32Reader";

    /// An uncompressed Windows .xnb around a body written by @p write.
    template<typename Write>
    Bytes Xnb(Write write)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        write(writer);
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    void Readers(System::IO::BinaryWriter& writer, std::initializer_list<const char*> names)
    {
        writer.Write7BitEncodedInt(static_cast<std::int32_t>(names.size()));
        for (const char* name : names)
        {
            writer.Write(std::string(name));
            writer.Write(static_cast<std::int32_t>(0));
        }
    }

    /// Dictionary<string, object> whose entries are external references to @p targets.
    Bytes ReferencingDictionary(std::initializer_list<const char*> targets)
    {
        return Xnb([&](System::IO::BinaryWriter& writer) {
            Readers(writer, {DictionaryOfObjects, StringReader, ExternalReferenceReader});
            writer.Write7BitEncodedInt(0); // no shared resources
            writer.Write7BitEncodedInt(1); // root: the dictionary
            writer.Write(static_cast<std::int32_t>(targets.size()));
            int key = 0;
            for (const char* target : targets)
            {
                writer.Write7BitEncodedInt(2);
                writer.Write("k" + std::to_string(key++));
                writer.Write7BitEncodedInt(3);
                writer.Write(std::string(target));
            }
        });
    }

    /// Dictionary<string, object> { "k": 7 }.
    Bytes LeafDictionary()
    {
        return Xnb([](System::IO::BinaryWriter& writer) {
            Readers(writer, {DictionaryOfObjects, StringReader, Int32Reader});
            writer.Write7BitEncodedInt(0);
            writer.Write7BitEncodedInt(1);
            writer.Write(static_cast<std::int32_t>(1));
            writer.Write7BitEncodedInt(2);
            writer.Write(std::string("k"));
            writer.Write7BitEncodedInt(3);
            writer.Write(static_cast<std::int32_t>(7));
        });
    }

    /// A manager whose assets are in memory, read through OpenStream() and counted.
    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;
        int opens = 0;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            ++opens;
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class ContentExternalReferenceCycleTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(ContentExternalReferenceCycleTest, RepeatedReferenceLoadsOnceAndResolvesBothEntries)
{
    InMemoryContentManager manager;
    manager.files["ok"] = LeafDictionary();
    manager.files["two"] = ReferencingDictionary({"ok", "ok"});

    const ObjectDictionary two = manager.Load<ObjectDictionary>("two");
    // "two" once and "ok" once: the second reference is served from the cache, so a cycle guard
    // must not mistake a repeated, already finished reference for a cycle.
    EXPECT_EQ(manager.opens, 2);
    ASSERT_EQ(two.size(), 2u);
    for (const auto& [key, value] : two)
    {
        const auto* referenced = std::any_cast<ObjectDictionary>(&value);
        ASSERT_NE(referenced, nullptr) << key;
        ASSERT_EQ(referenced->count("k"), 1u) << key;
        EXPECT_EQ(std::any_cast<std::int32_t>(referenced->at("k")), 7) << key;
    }
}

TEST_F(ContentExternalReferenceCycleTest, ReferenceToTheAssetBeingReadIsRefusedNotFollowed)
{
    InMemoryContentManager manager;
    manager.files["loop"] = ReferencingDictionary({"loop"});
    manager.files["ok"] = LeafDictionary();

    // Before the fix CNA recursed like XNA does (ASan: stack-overflow).
    EXPECT_THROW((void)manager.Load<ObjectDictionary>("loop"), ContentLoadException);
    // The outer read and the reference's read: the cycle is caught on its first repetition.
    EXPECT_EQ(manager.opens, 2);

    // Nothing of the refused read is cached, and the manager still loads.
    const ObjectDictionary leaf = manager.Load<ObjectDictionary>("ok");
    EXPECT_EQ(std::any_cast<std::int32_t>(leaf.at("k")), 7);
    EXPECT_THROW((void)manager.Load<ObjectDictionary>("loop"), ContentLoadException);
    EXPECT_EQ(manager.opens, 5);
}

TEST_F(ContentExternalReferenceCycleTest, CycleThroughASecondAssetIsRefused)
{
    InMemoryContentManager manager;
    manager.files["a"] = ReferencingDictionary({"b"});
    manager.files["b"] = ReferencingDictionary({"a"});

    EXPECT_THROW((void)manager.Load<ObjectDictionary>("a"), ContentLoadException);
    EXPECT_EQ(manager.opens, 3); // a, b, then a again -- refused there
}
