// SPDX-License-Identifier: MS-PL
//
// SAMPLE-066: proves the XNA math value types serialize the way .NET's reflection makes them
// serialize, against Microsoft's own shipped documents rather than against a transcription of
// them. sharp-runtime's own fixture tests cover the same corpus with hand-written stand-in
// structs; these run the real Microsoft::Xna::Framework types through the same files, which is
// exactly the step that handoff recorded as missing ("no sample is ported against it").

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/XmlSerializationEXT.hpp"
#include "System/IO/File.hpp"
#include "System/Xml/Serialization/XmlSerializer.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using System::Xml::Serialization::XmlSerializer;

namespace
{
    // The shapes ShipGame's own EntityList.cs / LightList.cs declare, using CNA's real math types.
    struct Entity
    {
        std::string name;
        Matrix transform;
        SHARP_XML_SERIALIZABLE(Entity, "Entity", SHARP_XML_M(Entity, name),
                                SHARP_XML_M(Entity, transform))
    };

    struct EntityList
    {
        std::vector<Entity> entities;
        SHARP_XML_SERIALIZABLE(EntityList, "EntityList", SHARP_XML_M(EntityList, entities))
    };

    struct Light
    {
        Vector3 position;
        float radius = 0.0f;
        Vector3 color;
        SHARP_XML_SERIALIZABLE(Light, "Light", SHARP_XML_M(Light, position),
                                SHARP_XML_M(Light, radius), SHARP_XML_M(Light, color))
    };

    struct LightList
    {
        Vector3 ambient;
        std::vector<Light> lights;
        SHARP_XML_SERIALIZABLE(LightList, "LightList", SHARP_XML_M(LightList, ambient),
                                SHARP_XML_M(LightList, lights))
    };

    /// The XNA Game Studio sample tree, whose shipped XML is the corpus. Absent on a machine that
    /// does not carry it, in which case the fixture-backed cases skip rather than fail.
    [[nodiscard]] std::filesystem::path SamplesRoot()
    {
        if (const char* fromEnvironment = std::getenv("XNA_SAMPLES_ROOT"))
        {
            return std::filesystem::path(fromEnvironment);
        }
        return std::filesystem::path("/rv/tmp/XNAGameStudio/Samples");
    }
}

TEST(XnaMathXmlSerializationTest, AMatrixRoundTripsThroughItsSixteenElementNames)
{
    Matrix value = Matrix::getIdentityProperty();
    value.M41 = -1072.0f;
    value.M42 = 256.0f;
    value.M43 = 800.0f;

    const std::string xml = XmlSerializer<Matrix>{}.Serialize(value);
    EXPECT_NE(xml.find("<M11>1</M11>"), std::string::npos) << xml;
    EXPECT_NE(xml.find("<M41>-1072</M41>"), std::string::npos) << xml;
    EXPECT_EQ(xml.find("<M55>"), std::string::npos) << "only the sixteen real members may appear";

    const Matrix back = XmlSerializer<Matrix>{}.Deserialize(xml);
    EXPECT_EQ(back, value);
}

TEST(XnaMathXmlSerializationTest, TheVectorTypesUseTheirOwnComponentNames)
{
    const std::string two = XmlSerializer<Vector2>{}.Serialize(Vector2(1.0f, 2.0f));
    EXPECT_NE(two.find("<X>1</X>"), std::string::npos) << two;
    EXPECT_NE(two.find("<Y>2</Y>"), std::string::npos) << two;
    EXPECT_EQ(XmlSerializer<Vector2>{}.Deserialize(two), Vector2(1.0f, 2.0f));

    const std::string three = XmlSerializer<Vector3>{}.Serialize(Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_NE(three.find("<Z>0.4</Z>"), std::string::npos) << three;
    EXPECT_EQ(XmlSerializer<Vector3>{}.Deserialize(three), Vector3(0.2f, 0.3f, 0.4f));

    const std::string four = XmlSerializer<Vector4>{}.Serialize(Vector4(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_NE(four.find("<W>4</W>"), std::string::npos) << four;
    EXPECT_EQ(XmlSerializer<Vector4>{}.Deserialize(four), Vector4(1.0f, 2.0f, 3.0f, 4.0f));
}

// The document ShipGame loads for every level spawn point, unmodified.
TEST(XnaMathXmlSerializationTest, ShipGameLevel1SpawnsLoadsIntoRealMatrices)
{
    const std::filesystem::path path =
        SamplesRoot() / "ShipGame_4_0/ShipGame/Content/levels/level1/level1_spawns.xml";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "the XNA Game Studio sample tree is not present: " << path;
    }

    const EntityList list =
        XmlSerializer<EntityList>{}.Deserialize(System::IO::File::ReadAllText(path.string()));

    ASSERT_FALSE(list.entities.empty());
    EXPECT_EQ(list.entities.front().name, "spawn0");

    // Every entity carries a real transform: an all-zero matrix would mean the members never
    // reached the object, which a name-count check alone would not catch.
    for (const Entity& entity : list.entities)
    {
        EXPECT_FALSE(entity.name.empty());
        EXPECT_FLOAT_EQ(entity.transform.M44, 1.0f) << entity.name;
        EXPECT_NE(entity.transform, Matrix()) << entity.name;
    }

    const Matrix& first = list.entities.front().transform;
    EXPECT_FLOAT_EQ(first.M13, 1.0f);
    EXPECT_FLOAT_EQ(first.M31, -1.0f);
    EXPECT_FLOAT_EQ(first.M41, -1072.0f);
    EXPECT_FLOAT_EQ(first.M42, 256.0f);
    EXPECT_FLOAT_EQ(first.M43, 800.0f);
}

// The document ShipGame loads for every level's lighting, unmodified.
TEST(XnaMathXmlSerializationTest, ShipGameLevel1LightsLoadsIntoRealVectors)
{
    const std::filesystem::path path =
        SamplesRoot() / "ShipGame_4_0/ShipGame/Content/levels/level1/level1_lights.xml";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "the XNA Game Studio sample tree is not present: " << path;
    }

    const LightList list =
        XmlSerializer<LightList>{}.Deserialize(System::IO::File::ReadAllText(path.string()));

    EXPECT_EQ(list.ambient, Vector3(0.2f, 0.2f, 0.2f));
    ASSERT_FALSE(list.lights.empty());

    const Light& first = list.lights.front();
    EXPECT_EQ(first.position, Vector3(0.0f, 500.0f, 0.0f));
    EXPECT_FLOAT_EQ(first.radius, 1000.0f);
    EXPECT_EQ(first.color, Vector3(0.7f, 0.7f, 0.7f));

    for (const Light& light : list.lights)
    {
        EXPECT_GT(light.radius, 0.0f) << "a light with no radius means radius never arrived";
    }
}
