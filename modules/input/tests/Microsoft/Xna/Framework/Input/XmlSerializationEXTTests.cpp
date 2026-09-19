// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>
#include <utility>

#include "Microsoft/Xna/Framework/Input/XmlSerializationEXT.hpp"
#include "System/Xml/Serialization/XmlSerializer.hpp"

namespace
{
    using Microsoft::Xna::Framework::Input::Keys;
    using System::Xml::Serialization::XmlSerializer;

    struct KeyBinding
    {
        Keys Key = Keys::None;
        SHARP_XML_SERIALIZABLE(KeyBinding, "KeyBinding", SHARP_XML_M(KeyBinding, Key))
    };
}

TEST(XnaInputXmlSerializationTest, EveryKeysEnumeratorHasOneDistinctXmlName)
{
    const auto entries = SharpXmlEnumEntries(static_cast<const Keys*>(nullptr));
    ASSERT_EQ(entries.size(), 160u);
    std::unordered_set<std::string> names;
    std::unordered_set<int> values;
    for (const auto& entry : entries)
    {
        EXPECT_TRUE(names.insert(entry.name).second);
        EXPECT_TRUE(values.insert(static_cast<int>(entry.value)).second);
    }
}

TEST(XnaInputXmlSerializationTest, SampleAndNonSampleKeysUseTheirXnaNames)
{
    const XmlSerializer<KeyBinding> serializer;
    for (const auto& [key, name] : {
             std::pair{Keys::LeftControl, "LeftControl"},
             std::pair{Keys::PageDown, "PageDown"},
             std::pair{Keys::OemClear, "OemClear"}})
    {
        const std::string xml = serializer.Serialize(KeyBinding{key});
        EXPECT_NE(xml.find("<Key>" + std::string(name) + "</Key>"), std::string::npos);
        EXPECT_EQ(serializer.Deserialize(xml).Key, key);
    }
}
