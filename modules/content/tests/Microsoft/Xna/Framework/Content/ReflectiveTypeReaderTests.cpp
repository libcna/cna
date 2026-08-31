// SPDX-License-Identifier: MS-PL
// ReflectiveTypeReaderBuilder: a game declares its type's fields ONCE and CNA builds the reader
// for a .xnb the XNA pipeline serialized reflectively. Verified against real pipeline output on
// SAMPLE-044, whose four settings assets load through this and match the original pixel for pixel.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/Object.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::ReflectiveSharedTypeReader;
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
    // --- A reflectively-serialized type that is a REFERENCE type ------------------------------
    //
    // Every C# class is one, and where it sits decides how XNA writes it: read directly as an
    // asset's root, but preceded by its own 1-based reader index when a collection or a Model.Tag
    // dispatches to it. RegisterShared() is the registration for the second case, and it is not
    // interchangeable with Register(): the value-shaped reader would leave the index unconsumed
    // and desynchronise the rest of the payload.

    class Creature : public System::Object
    {
    public:
        std::int32_t Legs = 0;
        std::string Name;

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "Bestiary.Creature";
            return name;
        }
    };

    struct Herd
    {
        std::vector<std::shared_ptr<Creature>> Members;
        std::vector<Matrix> Poses;
        std::vector<std::int32_t> Parents;
    };

    class SharedPayload : public System::Object
    {
    public:
        std::int32_t Value = 0;

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "Bestiary.SharedPayload";
            return name;
        }
    };

    class SharedOwner : public System::Object
    {
    public:
        std::shared_ptr<SharedPayload> Payload;

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "Bestiary.SharedOwner";
            return name;
        }
    };

    class AbstractAnimal : public System::Object
    {
    public:
        std::int32_t Legs = 0;

        virtual std::string Speak() const = 0;
    };

    class NamedAnimal final : public AbstractAnimal
    {
    public:
        std::string Name;

        [[nodiscard]] std::string Speak() const override { return Name; }

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "Bestiary.NamedAnimal";
            return name;
        }
    };

    std::vector<std::uint8_t> BuildInheritedAnimalXnb(bool dispatchAbstractRoot)
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter w(&body, true);

        w.Write7BitEncodedInt(4);
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.NamedAnimal, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.AbstractAnimal, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write(static_cast<std::int32_t>(0));

        w.Write7BitEncodedInt(0);
        w.Write7BitEncodedInt(dispatchAbstractRoot ? 2 : 1);
        if (!dispatchAbstractRoot)
        {
            w.Write(static_cast<std::int32_t>(4));
            w.Write7BitEncodedInt(4);
            w.Write(std::string("Bear"));
        }
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

    class ReflectiveInheritanceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();

            ReflectiveTypeReaderBuilder<AbstractAnimal> base("Bestiary.AbstractAnimal");
            base.Field(&AbstractAnimal::Legs);
            base.RegisterAbstract();

            ReflectiveTypeReaderBuilder<NamedAnimal>("Bestiary.NamedAnimal")
                .Base(base)
                .Field(&NamedAnimal::Name)
                .RegisterShared<AbstractAnimal>();
        }

        void TearDown() override
        {
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<AbstractAnimal>::CanonicalReaderName(
                    "Bestiary.AbstractAnimal"));
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<NamedAnimal>::CanonicalReaderName("Bestiary.NamedAnimal"));
        }
    };

    TEST_F(ReflectiveInheritanceTest, ReadsBaseMembersBeforeDerivedMembersIntoTheSameObject)
    {
        ScratchContentRoot root;
        WriteBytes(root.path() / "named-animal.xnb", BuildInheritedAnimalXnb(false));

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());
        const auto loaded = content.Load<std::shared_ptr<AbstractAnimal>>("named-animal");

        ASSERT_NE(nullptr, loaded);
        EXPECT_EQ(4, loaded->Legs);
        EXPECT_EQ("Bear", loaded->Speak());
    }

    TEST_F(ReflectiveInheritanceTest, AbstractReaderResolvesTheTableButRejectsDispatch)
    {
        ScratchContentRoot root;
        WriteBytes(root.path() / "abstract-animal.xnb", BuildInheritedAnimalXnb(true));

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());
        EXPECT_THROW(
            {
                const auto ignored =
                    content.Load<std::shared_ptr<AbstractAnimal>>("abstract-animal");
                (void) ignored;
            },
            Microsoft::Xna::Framework::Content::ContentLoadException);
    }

    // The three collection shapes a custom model processor writes on a Model.Tag: a list of its
    // own class, a list of Matrix (a bind pose) and a list of int (a skeleton hierarchy).
    std::vector<std::uint8_t> BuildHerdXnb()
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter w(&body, true);

        w.Write7BitEncodedInt(8);
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.Herd, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ListReader`1"
                            "[[Bestiary.Creature, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.Creature, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ListReader`1"
                            "[[Microsoft.Xna.Framework.Matrix, Microsoft.Xna.Framework, "
                            "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.MatrixReader"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ListReader`1"
                            "[[System.Int32, mscorlib, Version=4.0.0.0, Culture=neutral, "
                            "PublicKeyToken=b77a5c561934e089]]"));
        w.Write(static_cast<std::int32_t>(0));

        w.Write7BitEncodedInt(0);
        w.Write7BitEncodedInt(1); // root -> the Herd reader

        w.Write7BitEncodedInt(2);                 // Members: the ListReader's index
        w.Write(static_cast<std::int32_t>(2));    // two of them
        for (const auto& creature : {std::pair<std::int32_t, std::string>{4, "Bear"},
                                     std::pair<std::int32_t, std::string>{2, "Crane"}})
        {
            w.Write7BitEncodedInt(3);             // each element carries the Creature reader index
            w.Write(creature.first);
            w.Write7BitEncodedInt(4);             // Name: StringReader's index
            w.Write(creature.second);
        }

        w.Write7BitEncodedInt(6);                 // Poses: the ListReader<Matrix> index
        w.Write(static_cast<std::int32_t>(2));
        for (float first : {1.0f, 100.0f})
        {
            for (int i = 0; i < 16; ++i) w.Write(first + static_cast<float>(i));
        }

        w.Write7BitEncodedInt(8);                 // Parents: the ListReader<Int32> index
        w.Write(static_cast<std::int32_t>(3));
        w.Write(static_cast<std::int32_t>(-1));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(static_cast<std::int32_t>(1));
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

    // A root reference object whose only member is [ContentSerializer(SharedResource = true)].
    // The member stores shared-resource index 1; the payload itself follows the entire root.
    std::vector<std::uint8_t> BuildSharedOwnerXnb()
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter w(&body, true);

        w.Write7BitEncodedInt(3);
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.SharedOwner, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.ReflectiveReader`1"
                            "[[Bestiary.SharedPayload, Bestiary, Version=1.0.0.0, Culture=neutral, "
                            "PublicKeyToken=null]]"));
        w.Write(static_cast<std::int32_t>(0));
        w.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        w.Write(static_cast<std::int32_t>(0));

        w.Write7BitEncodedInt(1);                 // one shared resource
        w.Write7BitEncodedInt(1);                 // root -> SharedOwner reader
        w.Write7BitEncodedInt(1);                 // Payload -> shared resource 1
        w.Write7BitEncodedInt(2);                 // shared resource -> SharedPayload reader
        w.Write(static_cast<std::int32_t>(42));
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

    class ReflectiveSharedTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();

            ReflectiveTypeReaderBuilder<Creature>("Bestiary.Creature")
                .Field(&Creature::Legs)
                .Field(&Creature::Name)
                .RegisterShared();

            ContentTypeReaderManager::AddTypeCreator(
                "Microsoft.Xna.Framework.Content.ListReader`1[[Bestiary.Creature]]",
                [] {
                    return std::make_unique<
                        CNA::Internal::Xnb::ListReader<std::shared_ptr<Creature>>>(
                        "System.Collections.Generic.List`1[[Bestiary.Creature]]",
                        ReflectiveTypeReader<Creature>::CanonicalReaderName("Bestiary.Creature"));
                });

            ReflectiveTypeReaderBuilder<Herd>("Bestiary.Herd")
                .Field(&Herd::Members)
                .Field(&Herd::Poses)
                .Field(&Herd::Parents)
                .Register();
        }

        void TearDown() override
        {
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<Herd>::CanonicalReaderName("Bestiary.Herd"));
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<Creature>::CanonicalReaderName("Bestiary.Creature"));
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                "Microsoft.Xna.Framework.Content.ListReader`1[[Bestiary.Creature]]");
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<SharedOwner>::CanonicalReaderName("Bestiary.SharedOwner"));
            ContentTypeReaderManager::RemoveTypeCreatorEXT(
                ReflectiveTypeReader<SharedPayload>::CanonicalReaderName(
                    "Bestiary.SharedPayload"));
        }
    };

    TEST_F(ReflectiveSharedTypeReaderTest, ReadsAReflectiveClassDispatchedFromInsideAList)
    {
        ScratchContentRoot root;
        WriteBytes(root.path() / "herd.xnb", BuildHerdXnb());

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());

        const Herd loaded = content.Load<Herd>("herd");

        ASSERT_EQ(2u, loaded.Members.size());
        ASSERT_NE(nullptr, loaded.Members[0]);
        EXPECT_EQ(4, loaded.Members[0]->Legs);
        EXPECT_EQ("Bear", loaded.Members[0]->Name);
        EXPECT_EQ(2, loaded.Members[1]->Legs);
        EXPECT_EQ("Crane", loaded.Members[1]->Name)
            << "each element consumed its own reader index; a value-shaped registration would "
               "have read the index as data and desynchronised everything after it";
    }

    TEST_F(ReflectiveSharedTypeReaderTest, SharedResourceFieldAppliesItsDeferredFixup)
    {
        ReflectiveTypeReaderBuilder<SharedPayload>("Bestiary.SharedPayload")
            .Field(&SharedPayload::Value)
            .RegisterShared();
        ReflectiveTypeReaderBuilder<SharedOwner>("Bestiary.SharedOwner")
            .SharedResourceField(&SharedOwner::Payload)
            .RegisterShared();

        ScratchContentRoot root;
        WriteBytes(root.path() / "shared-owner.xnb", BuildSharedOwnerXnb());

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());
        const auto loaded = content.Load<std::shared_ptr<SharedOwner>>("shared-owner");

        ASSERT_NE(nullptr, loaded);
        ASSERT_NE(nullptr, loaded->Payload)
            << "the fixup runs only after the shared payload following the root has been read";
        EXPECT_EQ(42, loaded->Payload->Value);
    }

    TEST_F(ReflectiveSharedTypeReaderTest, SharedResourceFieldRejectsAValueRegistration)
    {
        EXPECT_THROW(
            ReflectiveTypeReaderBuilder<SharedOwner>("Bestiary.SharedOwner")
                .SharedResourceField(&SharedOwner::Payload)
                .Register(),
            std::logic_error);
    }

    // The two closed generics a custom model processor's Tag needs, neither of which had an
    // instantiation registered anywhere before -- and an .xnb's table must resolve IN FULL, so a
    // file naming either failed as a whole rather than in the part that used it.
    TEST_F(ReflectiveSharedTypeReaderTest, ReadsListOfMatrixAndListOfInt32)
    {
        ScratchContentRoot root;
        WriteBytes(root.path() / "herd.xnb", BuildHerdXnb());

        ContentManager content;
        content.setRootDirectoryProperty(root.path().string());

        const Herd loaded = content.Load<Herd>("herd");

        ASSERT_EQ(2u, loaded.Poses.size());
        EXPECT_FLOAT_EQ(1.0f, loaded.Poses[0].M11);
        EXPECT_FLOAT_EQ(16.0f, loaded.Poses[0].M44);
        EXPECT_FLOAT_EQ(100.0f, loaded.Poses[1].M11);
        EXPECT_FLOAT_EQ(115.0f, loaded.Poses[1].M44);

        EXPECT_EQ(std::vector<std::int32_t>({-1, 0, 1}), loaded.Parents);
    }

    // A Model.Tag is stored as a System::Object*, so the reader for a type attached to one has to
    // hand back a shared_ptr to THAT base, not to the concrete type. RegisterShared<TStored>()
    // names it.
    TEST_F(ReflectiveSharedTypeReaderTest, RegisterSharedCanHandBackAPointerToABase)
    {
        EXPECT_EQ("Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Bestiary.Creature]]",
                  (ReflectiveSharedTypeReader<Creature, System::Object>::CanonicalReaderName(
                      "Bestiary.Creature")))
            << "the .xnb names the serialized type, so the key does not depend on the C++ shape";

        ReflectiveTypeReaderBuilder<Creature>("Bestiary.Creature")
            .Field(&Creature::Legs)
            .Field(&Creature::Name)
            .RegisterShared<System::Object>();

        auto reader = ContentTypeReaderManager::CreateReader(
            ReflectiveTypeReader<Creature>::CanonicalReaderName("Bestiary.Creature"));
        ASSERT_NE(nullptr, reader);
        EXPECT_EQ("Bestiary.Creature", reader->getTargetTypeNameProperty());
    }

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
