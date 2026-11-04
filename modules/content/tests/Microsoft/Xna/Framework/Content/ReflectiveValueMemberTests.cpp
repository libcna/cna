// SPDX-License-Identifier: MS-PL
//
// TQ-033: a reflectively-read type whose members are XNA value types outside the ones the reader
// listed before (Point, Vector2..4, Matrix, Quaternion, Color, TimeSpan and the 32/64-bit
// integers): Rectangle, Int16, UInt16, SByte, UInt64, Char, BoundingBox, BoundingSphere, Ray,
// Plane and Nullable<Int32>, then a sentinel.
//
// Oracle: Microsoft.Xna.Framework.dll 4.0 ReflectiveReaderMemberHelper.Read IL_006e/IL_013e reads
// each member with ContentReader.ReadObject<object>(typeReader, existing), and
// ReadObjectInternal(typeReader, existing) IL_000e-0019 invokes a value-type reader directly,
// without a reader-dispatch index. The member layouts are the readers' own: RectangleReader (four
// Int32), BoundingBoxReader (Min, Max), BoundingSphereReader (Center, Radius), RayReader
// (Position, Direction), PlaneReader (Normal, D), NullableReader`1.Read IL_0000-0023 (Boolean, then
// the raw value when true).

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReader;
using Microsoft::Xna::Framework::Content::ReflectiveTypeReaderBuilder;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    constexpr const char* kTypeName = "Lane.ValueMembers";
    // A built-in registration since TQ-033 (NullableReader`1 for every built-in value type).
    constexpr const char* kNullableInt32Reader =
        "Microsoft.Xna.Framework.Content.NullableReader`1[[System.Int32]]";

    struct ValueMembers
    {
        Rectangle Box;
        std::int16_t Small = 0;
        std::uint16_t Unsigned = 0;
        std::int8_t Tiny = 0;
        std::uint64_t Big = 0;
        char16_t Letter = 0;
        BoundingBox Bounds;
        BoundingSphere Sphere;
        Ray Pick;
        Plane Floor;
        std::optional<std::int32_t> Maybe;
        std::optional<std::int32_t> Nothing = 5;
        std::uint32_t Sentinel = 0;
    };

    void WriteVector(System::IO::BinaryWriter& writer, const float x, const float y, const float z)
    {
        writer.Write(x);
        writer.Write(y);
        writer.Write(z);
    }

    Bytes ValueMembersXnb()
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        const std::vector<std::string> readers = {
            "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Lane.ValueMembers, Lane]]",
            "Microsoft.Xna.Framework.Content.RectangleReader",
            "Microsoft.Xna.Framework.Content.Int16Reader",
            "Microsoft.Xna.Framework.Content.UInt16Reader",
            "Microsoft.Xna.Framework.Content.SByteReader",
            "Microsoft.Xna.Framework.Content.UInt64Reader",
            "Microsoft.Xna.Framework.Content.CharReader",
            "Microsoft.Xna.Framework.Content.BoundingBoxReader",
            "Microsoft.Xna.Framework.Content.BoundingSphereReader",
            "Microsoft.Xna.Framework.Content.RayReader",
            "Microsoft.Xna.Framework.Content.PlaneReader",
            kNullableInt32Reader,
            "Microsoft.Xna.Framework.Content.Int32Reader",
            "Microsoft.Xna.Framework.Content.UInt32Reader",
        };
        writer.Write7BitEncodedInt(static_cast<std::int32_t>(readers.size()));
        for (const std::string& name : readers)
        {
            writer.Write(name);
            writer.Write(static_cast<std::int32_t>(0));
        }
        writer.Write7BitEncodedInt(0);  // no shared resources
        writer.Write7BitEncodedInt(1);  // root: the reflective reader
        for (const std::int32_t value : {3, 5, 7, 9}) { writer.Write(value); }  // Box
        writer.Write(static_cast<std::int16_t>(-1234));                          // Small
        writer.Write(static_cast<std::uint16_t>(0xBEEF));                        // Unsigned
        writer.Write(static_cast<std::int8_t>(-7));                              // Tiny
        writer.Write(static_cast<std::uint64_t>(0x0102030405060708ULL));         // Big
        writer.Write(static_cast<std::uint8_t>('Z'));                            // Letter (UTF-8)
        WriteVector(writer, -1, -2, -3);                                         // Bounds.Min
        WriteVector(writer, 4, 5, 6);                                            // Bounds.Max
        WriteVector(writer, 1, 2, 3);                                            // Sphere.Center
        writer.Write(4.0f);                                                      // Sphere.Radius
        WriteVector(writer, 0, 1, 0);                                            // Pick.Position
        WriteVector(writer, 0, 0, -1);                                           // Pick.Direction
        WriteVector(writer, 0, 1, 0);                                            // Floor.Normal
        writer.Write(-2.5f);                                                     // Floor.D
        writer.Write(static_cast<std::uint8_t>(1));                              // Maybe: has value
        writer.Write(static_cast<std::int32_t>(42));
        writer.Write(static_cast<std::uint8_t>(0));                              // Nothing: none
        writer.Write(static_cast<std::uint32_t>(0xD4C3B2A1u));                   // Sentinel
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
        InMemoryContentManager() : ContentManager(CNA::TestSupport::EmptyServices(), "Content") { RouteLoadsThroughOpenStreamEXT(true); }

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

    class ReflectiveValueMemberTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            ReflectiveTypeReaderBuilder<ValueMembers>(kTypeName)
                .Field(&ValueMembers::Box)
                .Field(&ValueMembers::Small)
                .Field(&ValueMembers::Unsigned)
                .Field(&ValueMembers::Tiny)
                .Field(&ValueMembers::Big)
                .Field(&ValueMembers::Letter)
                .Field(&ValueMembers::Bounds)
                .Field(&ValueMembers::Sphere)
                .Field(&ValueMembers::Pick)
                .Field(&ValueMembers::Floor)
                .Field(&ValueMembers::Maybe)
                .Field(&ValueMembers::Nothing)
                .Field(&ValueMembers::Sentinel)
                .Register();
        }

        void TearDown() override
        {
            ContentTypeReaderManager::RemoveTypeCreatorEXT(ReflectiveTypeReader<ValueMembers>::CanonicalReaderName(kTypeName));
        }

        InMemoryContentManager manager;
    };
}

TEST_F(ReflectiveValueMemberTest, ValueTypeMembersAreReadRawWithoutADispatchIndex)
{
    manager.files["members"] = ValueMembersXnb();
    const ValueMembers loaded = manager.Load<ValueMembers>("members");

    EXPECT_EQ(loaded.Box, Rectangle(3, 5, 7, 9));
    EXPECT_EQ(loaded.Small, -1234);
    EXPECT_EQ(loaded.Unsigned, 0xBEEF);
    EXPECT_EQ(loaded.Tiny, -7);
    EXPECT_EQ(loaded.Big, 0x0102030405060708ULL);
    EXPECT_EQ(loaded.Letter, u'Z');
    EXPECT_EQ(loaded.Bounds, BoundingBox(Vector3(-1, -2, -3), Vector3(4, 5, 6)));
    EXPECT_EQ(loaded.Sphere, BoundingSphere(Vector3(1, 2, 3), 4.0f));
    EXPECT_EQ(loaded.Pick, Ray(Vector3(0, 1, 0), Vector3(0, 0, -1)));
    EXPECT_EQ(loaded.Floor, Plane(Vector3(0, 1, 0), -2.5f));
    ASSERT_TRUE(loaded.Maybe.has_value());
    EXPECT_EQ(*loaded.Maybe, 42);
    EXPECT_FALSE(loaded.Nothing.has_value()) << "a false HasValue reads as null, replacing the default";
    EXPECT_EQ(loaded.Sentinel, 0xD4C3B2A1u) << "every member before it consumed exactly its bytes";
}
