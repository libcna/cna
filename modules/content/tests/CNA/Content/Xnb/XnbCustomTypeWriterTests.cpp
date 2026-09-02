// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md: the public extension point, exercised the way a game actually would.
//
// A game defines its own type, writes it with its own XnbTypeWriter, and reads it back with its
// own ContentTypeReader -- with no CNA-side special-casing, no change to a central switch, and no
// RTTI in the lookup path. If adding a custom type needed anything more than the two classes and
// two registrations below, this test would not compile.

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Content::Xnb;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    /**
     * @brief A game's own compiled type, with no CNA or XNA ancestry at all.
     *
     * Modelled as a C# **struct**: a value type, written inline wherever the format says
     * `Object? T`. The writer's `IsValueType()` and the reader's own shape must agree about this,
     * because it decides whether each element of a collection carries a type identifier. CNA's
     * reader infers the shape from C++ (a `std::shared_ptr` element is a reference type, anything
     * else is a value type), so a C# class is represented on both sides as a `shared_ptr`.
     */
    struct LevelData
    {
        std::string title;
        std::int32_t roomCount = 0;
        float gravity = 0.0f;
        bool hasBoss = false;

        bool operator==(const LevelData&) const = default;
    };
}

namespace CNA::Content::Xnb
{
    /** @brief The serialized .NET type name a game's own `.xnb` content declares. */
    template <>
    struct XnbTypeKey<LevelData>
    {
        static std::string Name() { return "MyGame.Content.LevelData"; }
    };
}

namespace
{
    /** @brief The game's writer: four fields, in the order its reader expects them. */
    class LevelDataWriter final : public XnbTypeWriterT<LevelData>
    {
    public:
        [[nodiscard]] std::string TargetTypeName() const override
        {
            return XnbTypeKey<LevelData>::Name();
        }

        [[nodiscard]] std::string RuntimeReaderName() const override
        {
            return "MyGame.Content.LevelDataReader";
        }

        // LevelData models a C# struct, so it is written inline as a value type. Declaring it a
        // reference type here would emit a per-element type identifier that the matching
        // value-shaped reader does not consume, desynchronising everything after it.
        [[nodiscard]] bool IsValueType() const override { return true; }

        void Write(XnbWriter& output, const LevelData& value) const override
        {
            output.WriteString(value.title);
            output.WriteInt32(value.roomCount);
            output.WriteSingle(value.gravity);
            output.WriteBoolean(value.hasBoss);
        }
    };

    /** @brief The game's reader, the exact inverse, registered the way the read side documents. */
    class LevelDataReader final : public ContentTypeReader<LevelData>
    {
    public:
        LevelDataReader() : ContentTypeReader<LevelData>("MyGame.Content.LevelData") {}

    protected:
        LevelData Read(ContentReader& input, std::optional<LevelData> existingInstance) override
        {
            LevelData data = existingInstance.value_or(LevelData{});
            data.title = input.ReadString();
            data.roomCount = input.ReadInt32();
            data.gravity = input.ReadSingle();
            data.hasBoss = input.ReadBoolean();
            return data;
        }
    };

    class XnbCustomTypeWriterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            ContentTypeReaderManager::AddTypeCreator(
                "MyGame.Content.LevelDataReader",
                [] { return std::make_unique<LevelDataReader>(); });

            RegisterBuiltInXnbTypeWriters(registry_);
            registry_.Register(std::make_shared<const LevelDataWriter>());
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename T>
        [[nodiscard]] T RoundTripAs(const std::string& typeName, const std::any& value)
        {
            const std::vector<std::uint8_t> file =
                WriteXnbFile(registry_, options_, typeName, value);
            System::IO::MemoryStream body(file.data() + 10,
                                          static_cast<std::int32_t>(file.size() - 10u));
            ContentReader reader(nullptr, &body, "custom", options_.version, 'w');
            return reader.ReadAsset<T>();
        }

        XnbTypeWriterRegistry registry_;
        XnbFileOptions options_{};
    };
}

TEST_F(XnbCustomTypeWriterTest, AGameSOwnTypeRoundTripsThroughItsOwnWriterAndReader)
{
    const LevelData level{"The Sunken Halls", 42, -9.81f, true};
    const LevelData result = RoundTripAs<LevelData>(XnbTypeKey<LevelData>::Name(), std::any(level));
    EXPECT_EQ(result, level);
}

TEST_F(XnbCustomTypeWriterTest, TheCustomReaderNameIsWhatLandsInTheTypeReaderTable)
{
    const LevelData level{"Vault", 1, 0.0f, false};
    const std::vector<std::uint8_t> file =
        WriteXnbFile(registry_, options_, XnbTypeKey<LevelData>::Name(), std::any(level));

    System::IO::MemoryStream body(file.data() + 10,
                                  static_cast<std::int32_t>(file.size() - 10u));
    ContentReader reader(nullptr, &body, "custom", options_.version, 'w');
    EXPECT_EQ(reader.Read7BitEncodedInt(), 1) << "one type-reader entry";
    // A game's own type is written exactly as declared: only the game's assembly could resolve
    // it, and only the game knows that assembly's identity.
    EXPECT_EQ(reader.ReadString(), "MyGame.Content.LevelDataReader");
    EXPECT_EQ(reader.ReadInt32(), 0) << "the reader version";
}

TEST_F(XnbCustomTypeWriterTest, ACustomTypeComposesWithTheBuiltInGenericCollections)
{
    // The point of a typed registry rather than a central switch: List<MyGame.LevelData> needs
    // no CNA-side change at all, only the same two registrations the element type already made.
    RegisterXnbListWriter(registry_, XnbTypeKey<LevelData>::Name());
    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.ListReader`1[[MyGame.Content.LevelData]]",
        []
        {
            return std::make_unique<CNA::Internal::Xnb::ListReader<LevelData>>(
                "System.Collections.Generic.List`1[[MyGame.Content.LevelData]]",
                "MyGame.Content.LevelDataReader");
        });

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<LevelData>::Name();
    list.elements = {std::any(LevelData{"One", 1, 1.0f, false}),
                     std::any(LevelData{"Two", 2, 2.0f, true})};

    const auto result = RoundTripAs<std::vector<LevelData>>(
        XnbListTypeName(XnbTypeKey<LevelData>::Name()), std::any(list));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].title, "One");
    EXPECT_EQ(result[1].roomCount, 2);
    EXPECT_TRUE(result[1].hasBoss);
}

TEST_F(XnbCustomTypeWriterTest, RegisteringTwoWritersForOneTypeIsRefused)
{
    EXPECT_THROW(registry_.Register(std::make_shared<const LevelDataWriter>()),
                 XnbWriteException);
}
