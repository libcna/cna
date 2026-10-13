// SPDX-License-Identifier: MS-PL
//
// A Game declares the built-in .xnb readers and the first Load<T> of a family installs that
// family's own (detail::BuiltinContent, docs/link-size-architecture.md), so a load can resolve a
// reader only through some family's registration: a built-in reader that only
// RegisterAllBuiltInXnbReaders() installs is one no game gets without asking the registry by name.
// Two checks pin the closures, both derived from the production registration functions rather than
// from a list of reader names kept here:
//
//  - the families together install exactly what RegisterAllBuiltInXnbReaders() installs;
//  - for every committed .xnb fixture whose root reader is a family's, that family alone installs
//    every CNA-owned reader the fixture's type-reader table names. An entry CNA has no reader for
//    (a game's own type, a reflective reader) is the game's to register and is not required.
//
// Each check starts from exactly a Game's state -- the built-ins declared, none installed -- and
// no family under test may install every declared reader.

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace ContentDetail = Microsoft::Xna::Framework::Content::detail;
namespace Graphics = Microsoft::Xna::Framework::Graphics;

namespace
{
    /// One asset family: the registration Load<T> runs first, and the root readers of the .xnb
    /// files such a load reads.
    struct ReaderFamily
    {
        const char* name;
        void (*registerFamily)(ContentManager&);
        std::vector<const char*> rootReaders;
    };

    /// Every CNA_BUILTIN_CONTENT specialization (detail/BuiltinContent.hpp), plus the plain-value
    /// family of the primary template (a list of strings, a number), whose root readers are
    /// whatever it installs rather than named here.
    const std::vector<ReaderFamily>& Families()
    {
        static const std::vector<ReaderFamily> families = {
            {"Texture2D", &ContentDetail::BuiltinContent<Graphics::Texture2D>::Register,
             {"Microsoft.Xna.Framework.Content.Texture2DReader"}},
            {"TextureCube", &ContentDetail::BuiltinContent<Graphics::TextureCube>::Register,
             {"Microsoft.Xna.Framework.Content.TextureCubeReader"}},
            {"Texture3D",
             &ContentDetail::BuiltinContent<std::shared_ptr<Graphics::Texture3D>>::Register,
             {"Microsoft.Xna.Framework.Content.Texture3DReader"}},
            {"SoundEffect",
             &ContentDetail::BuiltinContent<Microsoft::Xna::Framework::Audio::SoundEffect>::Register,
             {"Microsoft.Xna.Framework.Content.SoundEffectReader"}},
            {"SpriteFont", &ContentDetail::BuiltinContent<Graphics::SpriteFont>::Register,
             {"Microsoft.Xna.Framework.Content.SpriteFontReader"}},
            {"Effect", &ContentDetail::BuiltinContent<std::shared_ptr<Graphics::Effect>>::Register,
             {"Microsoft.Xna.Framework.Content.EffectReader",
              "Microsoft.Xna.Framework.Content.EffectMaterialReader",
              "Microsoft.Xna.Framework.Content.BasicEffectReader",
              "Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
              "Microsoft.Xna.Framework.Content.DualTextureEffectReader",
              "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
              "Microsoft.Xna.Framework.Content.SkinnedEffectReader"}},
            {"Model", &ContentDetail::BuiltinContent<Graphics::Model>::Register,
             {"Microsoft.Xna.Framework.Content.ModelReader"}},
            {"AnimationClipEXT", &ContentDetail::BuiltinContent<Graphics::AnimationClipEXT>::Register,
             {}},
            {"SkinnedModelEXT",
             &ContentDetail::BuiltinContent<std::shared_ptr<Graphics::SkinnedModelEXT>>::Register,
             {}},
            {"Curve", &ContentDetail::BuiltinContent<Microsoft::Xna::Framework::Curve>::Register,
             {"Microsoft.Xna.Framework.Content.CurveReader"}},
            {"Song", &ContentDetail::BuiltinContent<Microsoft::Xna::Framework::Media::Song>::Register,
             {"Microsoft.Xna.Framework.Content.SongReader"}},
            {"Video", &ContentDetail::BuiltinContent<Microsoft::Xna::Framework::Media::Video>::Register,
             {"Microsoft.Xna.Framework.Content.VideoReader"}},
            {"value", &ContentDetail::BuiltinContent<std::vector<std::string>>::Register, {}},
        };
        return families;
    }

    std::set<std::string> RegisteredNow()
    {
        const std::vector<std::string> names = ContentDetail::RegisteredReaderNames();
        return {names.begin(), names.end()};
    }

    /// Everything RegisterAllBuiltInXnbReaders() installs: the readers CNA owns.
    std::set<std::string> EveryBuiltinReader()
    {
        ContentTypeReaderManager::ClearTypeCreators();
        CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        std::set<std::string> names = RegisteredNow();
        ContentTypeReaderManager::ClearTypeCreators();
        return names;
    }

    /// What one family installs from a Game's declared-only state. Fails the test if the family
    /// installed every declared reader instead of its own.
    std::set<std::string> FamilyClosure(const ReaderFamily& family)
    {
        ContentTypeReaderManager::ClearTypeCreators();
        ContentDetail::DeclareBuiltinXnbReaders();
        ContentManager manager(nullptr, "tests/assets/xnb");
        family.registerFamily(manager);
        EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending())
            << "the " << family.name << " family installed every declared reader";
        return RegisteredNow();
    }

    std::string Join(const std::set<std::string>& names)
    {
        std::ostringstream out;
        for (const std::string& name : names) out << "\n  " << name;
        return out.str();
    }

    std::set<std::string> Difference(const std::set<std::string>& from,
                                     const std::set<std::string>& remove)
    {
        std::set<std::string> result;
        std::set_difference(from.begin(), from.end(), remove.begin(), remove.end(),
                            std::inserter(result, result.end()));
        return result;
    }
}

TEST(XnbReaderFamilyClosureTest, TheFamiliesTogetherInstallEveryBuiltinReader)
{
    const std::set<std::string> everyReader = EveryBuiltinReader();
    ASSERT_FALSE(everyReader.empty());

    std::set<std::string> familyUnion;
    for (const ReaderFamily& family : Families())
    {
        const std::set<std::string> closure = FamilyClosure(family);
        familyUnion.insert(closure.begin(), closure.end());
    }
    ContentTypeReaderManager::ClearTypeCreators();

    // A built-in reader no family installs is reachable only through
    // RegisterAllBuiltInXnbReaders(): a game's load of a file naming it fails.
    EXPECT_EQ(Difference(everyReader, familyUnion), std::set<std::string>{})
        << "built-in readers no family installs -- add each to the families whose files may name it";
    // A family installing a reader the complete registration does not would be a reader
    // RegisterAllBuiltInXnbReaders() forgot.
    EXPECT_EQ(Difference(familyUnion, everyReader), std::set<std::string>{})
        << "readers a family installs that RegisterAllBuiltInXnbReaders() does not";

    RecordProperty("builtin_readers", static_cast<int>(everyReader.size()));
    RecordProperty("family_union_readers", static_cast<int>(familyUnion.size()));
}

namespace
{
    /// The committed .xnb corpora: CNA's own writer's files, MonoGame's, the genuine XNA 4.0
    /// pipeline's (tests/reference/xna40) and the video fixture. Walked recursively, so a fixture
    /// committed under any of them is covered without being named here.
    constexpr const char* kFixtureRoots[] = {
        "tests/assets/xnb",
        "tests/assets/media",
        "tests/reference/xna40",
    };

    std::vector<std::filesystem::path> CommittedFixtures()
    {
        std::vector<std::filesystem::path> fixtures;
        for (const char* root : kFixtureRoots)
        {
            EXPECT_TRUE(std::filesystem::is_directory(root)) << root << " (run from the source root)";
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".xnb")
                    fixtures.push_back(entry.path());
            }
        }
        std::sort(fixtures.begin(), fixtures.end());
        return fixtures;
    }

    struct FixtureTable
    {
        std::vector<CNA::Internal::Xnb::XnbTypeReaderTableEntry> entries;
        /// The root object's reader, canonical; empty when the root object is null.
        std::string rootReader;
    };

    /// The type-reader table and root reader of one .xnb, read as ContentManager reads the file
    /// up to its first object: header, LZX or LZ4 payload, table, shared-resource count, root
    /// reference. No reader is created and no asset is built.
    FixtureTable ReadFixtureTable(const std::filesystem::path& path)
    {
        using namespace CNA::Internal::Xnb;
        const std::string origin = path.generic_string();

        std::ifstream stream(path, std::ios::binary);
        const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(stream)),
                                             std::istreambuf_iterator<char>());
        if (file.size() < 14)
            throw ContentLoadException("'" + origin + "' is shorter than an .xnb header.");

        System::IO::MemoryStream headerStream(file.data(), static_cast<std::int32_t>(file.size()));
        System::IO::BinaryReader headerReader(&headerStream, true);
        const XnbHeader header = ParseXnbHeader(headerReader, origin);

        std::vector<std::uint8_t> body;
        if (header.compression == XnbCompression::None)
        {
            body.assign(file.begin() + 10, file.end());
        }
        else
        {
            System::IO::MemoryStream sizeStream(file.data() + 10, 4);
            System::IO::BinaryReader sizeReader(&sizeStream, true);
            const std::int32_t decompressedSize = sizeReader.ReadInt32();
            const std::int32_t compressedSize = static_cast<std::int32_t>(file.size()) - 14;
            if (header.compression == XnbCompression::Lzx)
                body = DecompressXnbPayload(file.data() + 14, compressedSize, decompressedSize, origin);
            else if (header.compression == XnbCompression::Lz4)
                body = DecompressXnbLz4Payload(file.data() + 14, compressedSize, decompressedSize, origin);
            else
                throw ContentLoadException("'" + origin + "' has an unrecognized compression flag combination.");
        }

        System::IO::MemoryStream bodyStream(body.data(), static_cast<std::int32_t>(body.size()));
        System::IO::BinaryReader bodyReader(&bodyStream, true);
        FixtureTable table;
        table.entries = ParseXnbTypeReaderTable(bodyReader, origin);
        (void)bodyReader.Read7BitEncodedInt(); // shared resources
        const std::int32_t rootId = bodyReader.Read7BitEncodedInt();
        if (rootId < 0 || rootId > static_cast<std::int32_t>(table.entries.size()))
            throw ContentLoadException("'" + origin + "' has an invalid root reader reference.");
        if (rootId > 0)
            table.rootReader = table.entries[static_cast<std::size_t>(rootId) - 1].normalizedName;
        return table;
    }

    /// The family whose Load<T> reads a file rooted at @p rootReader, or null when no family's
    /// does (a game's own root type).
    const ReaderFamily* FamilyForRoot(const std::string& rootReader,
                                      const std::set<std::string>& valueReaders)
    {
        for (const ReaderFamily& family : Families())
        {
            for (const char* root : family.rootReaders)
                if (rootReader == root) return &family;
        }
        if (valueReaders.count(rootReader) != 0)
            return &Families().back();
        return nullptr;
    }

    std::string DescribeTable(const FixtureTable& table, const std::set<std::string>& builtin,
                              const std::set<std::string>& installed)
    {
        std::ostringstream out;
        for (std::size_t i = 0; i < table.entries.size(); ++i)
        {
            const std::string& name = table.entries[i].normalizedName;
            out << "\n  [" << (i + 1) << "] " << name << " -- "
                << (builtin.count(name) == 0 ? "not CNA's"
                    : installed.count(name) != 0 ? "installed" : "MISSING");
        }
        return out.str();
    }
}

TEST(XnbFixtureFamilyCoverageTest, EachFixturesRootFamilyInstallsEveryBuiltinReaderItNames)
{
    const std::set<std::string> builtin = EveryBuiltinReader();
    ASSERT_FALSE(builtin.empty());
    const std::set<std::string> valueReaders = FamilyClosure(Families().back());

    const std::vector<std::filesystem::path> fixtures = CommittedFixtures();
    ASSERT_FALSE(fixtures.empty());

    std::map<std::string, int> checkedPerFamily;
    std::map<std::string, int> skippedPerRoot; // fixtures rooted at no family's reader
    for (const std::filesystem::path& path : fixtures)
    {
        SCOPED_TRACE(path.generic_string());
        FixtureTable table;
        try
        {
            table = ReadFixtureTable(path);
        }
        catch (const std::exception& ex)
        {
            ADD_FAILURE() << "cannot read the type-reader table: " << ex.what();
            continue;
        }

        const ReaderFamily* family = FamilyForRoot(table.rootReader, valueReaders);
        if (family == nullptr)
        {
            ++skippedPerRoot[table.rootReader.empty() ? "null" : table.rootReader];
            continue;
        }

        // Only the root's family, from a Game's declared-only state.
        const std::set<std::string> installed = FamilyClosure(*family);
        std::set<std::string> missing;
        for (const auto& entry : table.entries)
        {
            if (builtin.count(entry.normalizedName) != 0 && installed.count(entry.normalizedName) == 0)
                missing.insert(entry.normalizedName);
        }
        EXPECT_TRUE(missing.empty())
            << "root reader " << table.rootReader << ", family " << family->name
            << " -- the family's registration lacks:" << Join(missing)
            << "\ntype-reader table:" << DescribeTable(table, builtin, installed);
        ++checkedPerFamily[family->name];
    }
    ContentTypeReaderManager::ClearTypeCreators();

    int checked = 0;
    for (const auto& [name, count] : checkedPerFamily)
    {
        RecordProperty("fixtures_" + name, count);
        checked += count;
    }
    RecordProperty("fixtures_checked", checked);
    std::ostringstream skipped;
    for (const auto& [root, count] : skippedPerRoot) skipped << root << ":" << count << " ";
    RecordProperty("fixtures_skipped_by_root", skipped.str());
    EXPECT_GT(checked, 0);
}
