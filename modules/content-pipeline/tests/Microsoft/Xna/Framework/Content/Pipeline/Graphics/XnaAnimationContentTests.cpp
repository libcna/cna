// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-097, 098: AnimationKeyframe, AnimationChannel, the two
// animation dictionaries and AnimationContent against what the genuine XNA 4.0 pipeline does with
// the same inputs (tests/reference/xna40/graphics/graphics-content-oracle.json, cases animation/*).
//
// The measurements settle the two things a signature cannot: a channel keeps its keyframes ordered
// by time, answering the index it inserted at and placing a keyframe after one that already holds
// its time; and membership is by reference, so a keyframe equal in time and transform to one in the
// channel is not contained. Both directions of the intermediate format are pinned too, including
// that a keyframe writes the two properties XNA marks [ContentSerializerIgnore].
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Graphics::AnimationChannel;
using Graphics::AnimationChannelDictionary;
using Graphics::AnimationContent;
using Graphics::AnimationContentDictionary;
using Graphics::AnimationKeyframe;
using Intermediate::IntermediateSerializer;

namespace
{
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/graphics/graphics-content-oracle.json";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    std::string Unescape(const std::string& text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    /** @brief Drops the parameter-name tail each host spells its own way. */
    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t parameter = text.find("Parameter name:");
        if (parameter != std::string::npos)
        {
            std::size_t cut = parameter;
            while (cut > 0 && (text[cut - 1] == '\n' || text[cut - 1] == '\r'))
            {
                --cut;
            }
            text = text.substr(0, cut);
        }
        const std::size_t core = text.find(" (Parameter '");
        if (core != std::string::npos)
        {
            const std::size_t end = text.find(')', core);
            text = text.substr(0, core) + (end == std::string::npos ? "" : text.substr(end + 1));
        }
        return text;
    }

    const std::map<std::string, std::string>& Oracle()
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(CorpusFile());
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        return cases;
    }

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return Normalize(body());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return Normalize("throws ArgumentOutOfRangeException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const InvalidContentException& error)
        {
            return Normalize("throws InvalidContentException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    std::shared_ptr<AnimationKeyframe> Keyframe(double seconds, const Matrix& transform = Matrix::getIdentityProperty())
    {
        return std::make_shared<AnimationKeyframe>(System::TimeSpan::FromSeconds(seconds), transform);
    }

    /** @brief The .NET TimeSpan.ToString() the oracle printed: [-][d.]hh:mm:ss[.fffffff]. */
    std::string TimeText(System::TimeSpan value)
    {
        std::ostringstream text;
        text.fill('0');
        const std::int64_t ticks = value.getTicksProperty();
        const std::int64_t days = ticks / System::TimeSpan::TicksPerDay;
        if (days != 0)
        {
            text << days << '.';
        }
        text.width(2);
        text << (ticks / System::TimeSpan::TicksPerHour) % 24 << ':';
        text.width(2);
        text << (ticks / System::TimeSpan::TicksPerMinute) % 60 << ':';
        text.width(2);
        text << (ticks / System::TimeSpan::TicksPerSecond) % 60;
        const std::int64_t fraction = ticks % System::TimeSpan::TicksPerSecond;
        if (fraction != 0)
        {
            text << '.';
            text.width(7);
            text << fraction;
        }
        return text.str();
    }

    std::string Number(double value)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << value;
        return text.str();
    }

    template<typename T>
    std::string Serialize(const std::shared_ptr<T>& value)
    {
        System::Xml::XmlWriterSettings settings;
        settings.Indent = true;
        settings.NewLineChars = "\r\n";
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        IntermediateSerializer::Serialize<std::shared_ptr<T>>(*writer, value, std::string());
        std::string xml = writer->ToString();
        const std::size_t cut = xml.find("?>");
        if (cut != std::string::npos)
        {
            xml = xml.substr(cut + 2);
        }
        while (!xml.empty() && (xml.front() == '\r' || xml.front() == '\n'))
        {
            xml.erase(xml.begin());
        }
        return xml;
    }

    template<typename T>
    std::shared_ptr<T> Deserialize(const std::string& xml)
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        return IntermediateSerializer::Deserialize<std::shared_ptr<T>>(*reader, std::string());
    }
}

TEST(XnaAnimationContent, OracleIsPresent)
{
    ASSERT_GE(Oracle().size(), 350u) << CorpusFile();
}

TEST(XnaAnimationContent, KeyframeMembersMatchXna)
{
    const AnimationKeyframe keyframe(System::TimeSpan::FromSeconds(1.5), Matrix::getIdentityProperty());
    EXPECT_EQ("time=" + TimeText(keyframe.getTimeProperty()) + " transform=" +
                  Number(keyframe.getTransformProperty().M11) + "," + Number(keyframe.getTransformProperty().M44),
              Expected("animation/keyframe_members"));

    AnimationKeyframe moved(System::TimeSpan::Zero, Matrix::getIdentityProperty());
    moved.setTransformProperty(Matrix::CreateTranslation(1, 2, 3));
    EXPECT_EQ("transform=" + Number(moved.getTransformProperty().M41) + "," +
                  Number(moved.getTransformProperty().M42) + "," + Number(moved.getTransformProperty().M43),
              Expected("animation/keyframe_set_transform"));
}

TEST(XnaAnimationContent, KeyframesCompareByTimeOnly)
{
    const AnimationKeyframe early(System::TimeSpan::FromSeconds(1), Matrix::getIdentityProperty());
    const AnimationKeyframe late(System::TimeSpan::FromSeconds(2), Matrix::getIdentityProperty());
    const AnimationKeyframe same(System::TimeSpan::FromSeconds(1), Matrix::CreateScale(2));
    EXPECT_EQ("early_vs_late=" + std::to_string(early.CompareTo(late)) + " late_vs_early=" +
                  std::to_string(late.CompareTo(early)) + " same=" + std::to_string(early.CompareTo(same)) +
                  " equals=False",
              Expected("animation/keyframe_compare"));
}

TEST(XnaAnimationContent, ChannelKeepsKeyframesOrderedByTime)
{
    AnimationChannel channel;
    const auto third = channel.Add(Keyframe(3));
    const auto first = channel.Add(Keyframe(1));
    const auto second = channel.Add(Keyframe(2));
    std::string times;
    for (const std::shared_ptr<AnimationKeyframe>& frame : channel)
    {
        if (!times.empty())
        {
            times += ' ';
        }
        times += Number(frame->getTimeProperty().getTotalSecondsProperty());
    }
    EXPECT_EQ("indices=" + std::to_string(third) + "," + std::to_string(first) + "," + std::to_string(second) +
                  " count=" + std::to_string(channel.getCountProperty()) + " times=" + times,
              Expected("animation/channel_sorted"));

    AnimationChannel duplicates;
    duplicates.Add(Keyframe(1));
    const auto again = duplicates.Add(Keyframe(1, Matrix::CreateScale(2)));
    EXPECT_EQ("index=" + std::to_string(again) + " count=" + std::to_string(duplicates.getCountProperty()) +
                  " first_m11=" + Number(duplicates[0]->getTransformProperty().M11) + " second_m11=" +
                  Number(duplicates[1]->getTransformProperty().M11),
              Expected("animation/channel_duplicate_time"));
}

TEST(XnaAnimationContent, ChannelRefusalsMatchXna)
{
    EXPECT_EQ(Result([]
                     {
                         AnimationChannel channel;
                         channel.Add(nullptr);
                         return "count=" + std::to_string(channel.getCountProperty());
                     }),
              Expected("animation/channel_add_null"));

    EXPECT_EQ(Result([] { return TimeText(AnimationChannel()[0]->getTimeProperty()); }),
              Expected("animation/channel_indexer_out_of_range"));

    EXPECT_EQ(Result([]
                     {
                         AnimationChannel channel;
                         channel.RemoveAt(0);
                         return std::string("accepted");
                     }),
              Expected("animation/channel_remove_at_out_of_range"));
}

TEST(XnaAnimationContent, ChannelMembershipIsByReference)
{
    AnimationChannel channel;
    const std::shared_ptr<AnimationKeyframe> frame = Keyframe(1);
    channel.Add(frame);
    const std::shared_ptr<AnimationKeyframe> equal = Keyframe(1);
    EXPECT_EQ(std::string("contains_same=") + (channel.Contains(frame) ? "True" : "False") + " contains_equal=" +
                  (channel.Contains(equal) ? "True" : "False") + " indexof_same=" +
                  std::to_string(channel.IndexOf(frame)) + " indexof_equal=" + std::to_string(channel.IndexOf(equal)) +
                  " indexof_missing=" + std::to_string(channel.IndexOf(Keyframe(9))),
              Expected("animation/channel_contains_and_indexof"));
}

TEST(XnaAnimationContent, ChannelRemovalMatchesXna)
{
    AnimationChannel channel;
    const std::shared_ptr<AnimationKeyframe> frame = Keyframe(1);
    channel.Add(frame);
    channel.Add(Keyframe(2));
    const bool removed = channel.Remove(frame);
    const bool missing = channel.Remove(Keyframe(9));
    channel.RemoveAt(0);
    EXPECT_EQ(std::string("removed=") + (removed ? "True" : "False") + " missing=" + (missing ? "True" : "False") +
                  " count=" + std::to_string(channel.getCountProperty()),
              Expected("animation/channel_remove"));

    AnimationChannel cleared;
    cleared.Add(Keyframe(1));
    cleared.Clear();
    EXPECT_EQ("count=" + std::to_string(cleared.getCountProperty()), Expected("animation/channel_clear"));
}

TEST(XnaAnimationContent, AnimationMembersMatchXna)
{
    const AnimationContent empty;
    EXPECT_EQ("duration=" + TimeText(empty.getDurationProperty()) + " channels=" +
                  std::to_string(empty.getChannelsProperty().getCountProperty()) + " name=\"" +
                  empty.getNameProperty() + "\"",
              Expected("animation/content_defaults"));

    AnimationContent animation;
    animation.setDurationProperty(System::TimeSpan::FromSeconds(2.5));
    auto channel = std::make_shared<AnimationChannel>();
    channel->Add(Keyframe(0));
    animation.getChannelsProperty().Add("Bone1", channel);
    EXPECT_EQ("duration=" + TimeText(animation.getDurationProperty()) + " channels=" +
                  std::to_string(animation.getChannelsProperty().getCountProperty()) + " keys=" +
                  std::to_string(animation.getChannelsProperty()["Bone1"]->getCountProperty()),
              Expected("animation/content_members"));
}

TEST(XnaAnimationContent, DictionariesRefuseANullValue)
{
    EXPECT_EQ(Result([]
                     {
                         AnimationChannelDictionary channels;
                         channels.Add("A", nullptr);
                         return "count=" + std::to_string(channels.getCountProperty());
                     }),
              Expected("animation/channel_dictionary_null"));

    EXPECT_EQ(Result([]
                     {
                         AnimationContentDictionary animations;
                         animations.Add("A", nullptr);
                         return "count=" + std::to_string(animations.getCountProperty());
                     }),
              Expected("animation/content_dictionary_null"));
}

TEST(XnaAnimationContent, SerializesAsXnaSerializes)
{
    auto animation = std::make_shared<AnimationContent>();
    animation->setNameProperty("Walk");
    animation->setDurationProperty(System::TimeSpan::FromSeconds(2));
    auto channel = std::make_shared<AnimationChannel>();
    channel->Add(Keyframe(0));
    channel->Add(Keyframe(1, Matrix::CreateTranslation(1, 0, 0)));
    animation->getChannelsProperty().Add("Root", channel);
    EXPECT_EQ(Serialize(animation), Expected("animation/serialize_content"));

    EXPECT_EQ(Serialize(std::make_shared<AnimationContent>()), Expected("animation/serialize_empty_content"));

    auto dictionary = std::make_shared<AnimationContentDictionary>();
    auto walk = std::make_shared<AnimationContent>();
    walk->setDurationProperty(System::TimeSpan::FromSeconds(1));
    dictionary->Add("Walk", walk);
    EXPECT_EQ(Serialize(dictionary), Expected("animation/serialize_dictionary"));
}

TEST(XnaAnimationContent, DeserializesAsXnaDeserializes)
{
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n"
        "  <Asset Type=\"Graphics:AnimationContent\">\r\n"
        "    <Name>Walk</Name>\r\n"
        "    <Duration>PT2S</Duration>\r\n"
        "    <Channels>\r\n"
        "      <Channel Key=\"Root\">\r\n"
        "        <Keyframe>\r\n"
        "          <Time>PT1S</Time>\r\n"
        "          <Transform>1 0 0 0 0 1 0 0 0 0 1 0 1 0 0 1</Transform>\r\n"
        "        </Keyframe>\r\n"
        "        <Keyframe>\r\n"
        "          <Time>PT0S</Time>\r\n"
        "          <Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform>\r\n"
        "        </Keyframe>\r\n"
        "      </Channel>\r\n"
        "    </Channels>\r\n"
        "  </Asset>\r\n"
        "</XnaContent>\r\n";
    const std::shared_ptr<AnimationContent> animation = Deserialize<AnimationContent>(xml);
    ASSERT_NE(animation, nullptr);
    std::string times;
    for (const std::shared_ptr<AnimationKeyframe>& frame : *animation->getChannelsProperty()["Root"])
    {
        if (!times.empty())
        {
            times += ' ';
        }
        times += Number(frame->getTimeProperty().getTotalSecondsProperty());
    }
    EXPECT_EQ("name=\"" + animation->getNameProperty() + "\" duration=" + TimeText(animation->getDurationProperty()) +
                  " channels=" + std::to_string(animation->getChannelsProperty().getCountProperty()) + " times=" +
                  times + " m41=" +
                  Number((*animation->getChannelsProperty()["Root"])[1]->getTransformProperty().M41),
              Expected("animation/deserialize_content"));
}

TEST(XnaAnimationContent, RefusesTheDocumentsXnaRefuses)
{
    EXPECT_EQ(Result([]
                     {
                         const auto animation = Deserialize<AnimationContent>(
                             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                             "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n"
                             "  <Asset Type=\"Graphics:AnimationContent\">\r\n"
                             "    <Channels />\r\n"
                             "  </Asset>\r\n"
                             "</XnaContent>\r\n");
                         return TimeText(animation->getDurationProperty());
                     }),
              Expected("animation/deserialize_no_duration"));

    EXPECT_EQ(Result([]
                     {
                         const auto animation = Deserialize<AnimationContent>(
                             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                             "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\r\n"
                             "  <Asset Type=\"Graphics:AnimationContent\">\r\n"
                             "    <Duration>PT2S</Duration>\r\n"
                             "    <Channels>\r\n"
                             "      <Channel Key=\"Root\">\r\n"
                             "        <Keyframe><Transform>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</Transform></Keyframe>\r\n"
                             "      </Channel>\r\n"
                             "    </Channels>\r\n"
                             "  </Asset>\r\n"
                             "</XnaContent>\r\n");
                         return std::to_string(animation->getChannelsProperty()["Root"]->getCountProperty());
                     }),
              Expected("animation/deserialize_keyframe_no_time"));
}

TEST(XnaAnimationContent, ToStringIsTheFullTypeName)
{
    EXPECT_EQ(AnimationContent().ToString() + "|" + AnimationChannel().ToString() + "|" +
                  AnimationChannelDictionary().ToString() + "|" +
                  AnimationKeyframe(System::TimeSpan::Zero, Matrix::getIdentityProperty()).ToString(),
              Expected("animation/tostring"));
}
