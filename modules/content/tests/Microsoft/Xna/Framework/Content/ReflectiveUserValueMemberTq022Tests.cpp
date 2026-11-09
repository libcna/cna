// SPDX-License-Identifier: MS-PL
//
// HND-TQ-22 (C++ halves): a reflectively-read member whose type is a game's own value type -- or
// DateTime -- is read raw through that type's reader, as Refresh's ReflectiveReaderMemberHelper
// does (.ctor IL_0006-000f resolves the member type's reader; Read IL_0061-006e calls
// ReadObject<object>(typeReader, null), and ReadObjectInternal(typeReader, existing) IL_000e-0019
// reads a value type without a dispatch index).

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/ContentReaderDiagnostics.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "System/DateTime.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReader;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReaderBuilder;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    struct Range
    {
        std::int32_t Lo = 0;
        std::int32_t Hi = 0;
    };

    struct Settings
    {
        std::int32_t Count = 0;
        Range Window;
        System::DateTime Stamp;
        std::string Name;
        std::uint32_t Sentinel = 0;
    };

    Bytes SettingsXnb()
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        const std::vector<std::string> readers = {
            "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Tq022.Settings, Tq022]]",
            "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Tq022.Range, Tq022]]",
            "Microsoft.Xna.Framework.Content.DateTimeReader",
            "Microsoft.Xna.Framework.Content.StringReader",
            "Microsoft.Xna.Framework.Content.Int32Reader",
            "Microsoft.Xna.Framework.Content.UInt32Reader",
        };
        writer.Write7BitEncodedInt(static_cast<std::int32_t>(readers.size()));
        for (const std::string& name : readers)
        {
            writer.Write(name);
            writer.Write(static_cast<std::int32_t>(0));
        }
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write(static_cast<std::int32_t>(3));                  // Count
        writer.Write(static_cast<std::int32_t>(-4));                 // Window.Lo, raw
        writer.Write(static_cast<std::int32_t>(9));                  // Window.Hi
        writer.Write(static_cast<std::uint64_t>(637'000'000'000'000'000ULL)); // Stamp, raw (Kind 0)
        writer.Write7BitEncodedInt(4);                               // Name: StringReader index
        writer.Write(std::string("tq022"));
        writer.Write(static_cast<std::uint32_t>(0xD4C3B2A1u));       // Sentinel
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
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
}

TEST(ReflectiveUserValueMemberTq022Test, AGameStructAndADateTimeMemberAreReadRaw)
{
    CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
    ReflectiveTypeReaderBuilder<Range>("Tq022.Range").Field(&Range::Lo).Field(&Range::Hi).Register();
    ReflectiveTypeReaderBuilder<Settings>("Tq022.Settings")
        .Field(&Settings::Count)
        .Field(&Settings::Window)
        .Field(&Settings::Stamp)
        .Field(&Settings::Name)
        .Field(&Settings::Sentinel)
        .Register();

    InMemoryContentManager manager;
    manager.files["settings"] = SettingsXnb();
    const Settings loaded = manager.Load<Settings>("settings");

    EXPECT_EQ(loaded.Count, 3);
    EXPECT_EQ(loaded.Window.Lo, -4);
    EXPECT_EQ(loaded.Window.Hi, 9);
    EXPECT_EQ(loaded.Stamp.getTicksProperty(), 637'000'000'000'000'000LL);
    EXPECT_EQ(loaded.Name, "tq022");
    EXPECT_EQ(loaded.Sentinel, 0xD4C3B2A1u);
    // The name a boxed Range keeps (ModelReader's Tag boxing falls back to this registry).
    EXPECT_EQ(CNA::Internal::Xnb::ContentReaderTypeName(typeid(Range)), "Tq022.Range");

    ContentTypeReaderManager::RemoveTypeCreatorEXT(ReflectiveTypeReader<Settings>::CanonicalReaderName("Tq022.Settings"));
    ContentTypeReaderManager::RemoveTypeCreatorEXT(ReflectiveTypeReader<Range>::CanonicalReaderName("Tq022.Range"));
}
