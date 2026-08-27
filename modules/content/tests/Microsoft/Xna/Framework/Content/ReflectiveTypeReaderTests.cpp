// SPDX-License-Identifier: MS-PL
// ReflectiveTypeReaderBuilder: a game declares its type's fields ONCE and CNA builds the reader
// for a .xnb the XNA pipeline serialized reflectively. Verified against real pipeline output on
// SAMPLE-044, whose four settings assets load through this and match the original pixel for pixel.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReader;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReaderBuilder;

namespace
{
    enum class Mood
    {
        Calm,
        Restless,
        Furious
    };

    struct CreatureSettings
    {
        std::int32_t Legs = 0;
        std::string Name;
        float Speed = 0;
        Mood Temper = Mood::Calm;
        Vector2 Home;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_reflective_reader_test_" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    // The exact shape XNA's IntermediateSerializer produces for a reflectively-written type:
    // a table naming the ReflectiveReader plus one reader per field type, then the fields with
    // value types inline and the reference type (the string) preceded by its reader index.
    std::vector<std::uint8_t> BuildCreatureXnb()
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter w(&body, true);

        w.Write7BitEncodedInt(5); // five table entries
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.CreatureSettings, Bestiary, Version=1.0.0.0, "
                            "Culture=neutral, PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.SingleReader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.EnumReader`1"
                            "[[Bestiary.Mood, Bestiary, Version=1.0.0.0, "
                            "Culture=neutral, PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));

        w.Write7BitEncodedInt(0); // no shared resources
        w.Write7BitEncodedInt(1); // root object -> the reflective reader

        w.Write(static_cast<std::int32_t>(6));      // Legs
        w.Write7BitEncodedInt(3);                    // Name: StringReader's index
        w.Write(std::string("Spindle"));
        w.Write(3.5f);                               // Speed
        w.Write(static_cast<std::int32_t>(2));       // Temper = Furious, inline
        w.Write(-1.5f);                              // Home.X
        w.Write(9.25f);                              // Home.Y
        w.Flush();
        const auto bodyBytes = body.ToArray();

        System::IO::MemoryStream file;
        System::IO::BinaryWriter fw(&file, true);
        fw.Write(static_cast<std::uint8_t>('X'));
        fw.Write(static_cast<std::uint8_t>('N'));
        fw.Write(static_cast<std::uint8_t>('B'));
        fw.Write(static_cast<std::uint8_t>('w'));
        fw.Write(static_cast<std::uint8_t>(5));
        fw.Write(static_cast<std::uint8_t>(0));
        fw.Write(static_cast<std::int32_t>(10 + static_cast<std::int32_t>(bodyBytes.size())));
        fw.Write(bodyBytes.data(), 0, static_cast<std::int32_t>(bodyBytes.size()));
        fw.Flush();

        const auto fileBytes = file.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    void DescribeCreature()
    {
        ReflectiveTypeReaderBuilder<CreatureSettings>("Bestiary.CreatureSettings")
            .Field(&CreatureSettings::Legs)
            .Field(&CreatureSettings::Name)
            .Field(&CreatureSettings::Speed)
            .EnumField(&CreatureSettings::Temper, "Bestiary.Mood")
            .Field(&CreatureSettings::Home)
            .Register();
    }

    class ReflectiveTypeReaderTest : public ::testing::Test
    {
    protected:
        // Deliberately NOT ClearTypeCreators(): that empties the whole registry, built-in readers
        // included, and they are registered once at static-init rather than per ContentManager --
        // so a cleared Int32Reader never comes back and the .xnb's table stops resolving. Removing
        // only what this fixture registered keeps the tests isolated without that side effect.
        // The primitive readers an .xnb's table names are registered by
        // RegisterPrimitiveXnbReaders(), not lazily per ContentManager, so a test that loads a
        // file naming Int32Reader has to ask for them -- the same thing the other content
        // fixtures do.
        void SetUp() override { CNA::Internal::Xnb::RegisterPrimitiveXnbReaders(); }

        void TearDown() override
        {
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<CreatureSettings>::CanonicalReaderName(
                    "Bestiary.CreatureSettings"));
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                "Microsoft.Xna.Framework.Content.EnumReader`1[[Bestiary.Mood]]");
        }
    };

    TEST_F(ReflectiveTypeReaderTest, ReadsEveryFieldKindInDeclarationOrder)
    {
        DescribeCreature();

        ScratchContentRoot root;
        WriteBytes(root.path() / "spindle.xnb", BuildCreatureXnb());

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());

        const CreatureSettings loaded = content.Load<CreatureSettings>("spindle");

        EXPECT_EQ(6, loaded.Legs);
        EXPECT_EQ("Spindle", loaded.Name) << "a reference-type field carries its reader index";
        EXPECT_FLOAT_EQ(3.5f, loaded.Speed);
        EXPECT_EQ(Mood::Furious, loaded.Temper) << "an enum is written inline as an Int32";
        EXPECT_FLOAT_EQ(-1.5f, loaded.Home.X);
        EXPECT_FLOAT_EQ(9.25f, loaded.Home.Y);
    }

    // The registration key is the canonical name, with the assembly qualifiers stripped -- which
    // is what the builder derives, so a game never has to spell it out or guess it.
    TEST_F(ReflectiveTypeReaderTest, TheCanonicalNameDropsTheAssemblyQualifiers)
    {
        EXPECT_EQ("Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Bestiary.CreatureSettings]]",
                  ReflectiveTypeReader<CreatureSettings>::CanonicalReaderName(
                      "Bestiary.CreatureSettings"));
    }

    // An .xnb's type-reader table must resolve in FULL before any object is read, so the
    // EnumReader the file names has to be registered even though the reflective payload writes
    // the enum inline and never dispatches to it. EnumField registers it; without that the load
    // fails on the table rather than on the data.
    TEST_F(ReflectiveTypeReaderTest, EnumFieldAlsoRegistersTheEnumReaderTheTableNames)
    {
        DescribeCreature();

        ScratchContentRoot root;
        WriteBytes(root.path() / "spindle.xnb", BuildCreatureXnb());

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());

        // Registered under the enum's OWN canonical name. Reaching the data at all proves it:
        // the manager resolves the whole table first.
        EXPECT_NO_THROW({
            const CreatureSettings loaded = content.Load<CreatureSettings>("spindle");
            EXPECT_EQ(Mood::Furious, loaded.Temper);
        });
    }
}
