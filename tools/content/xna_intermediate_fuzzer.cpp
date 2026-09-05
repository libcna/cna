// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-075: fuzz harness for the XNA intermediate-XML reader.
//
// One entry point deserializes untrusted bytes as every root shape the serializer supports --
// a described class with members of every kind, `object`, a packed list, a string -- and lets
// the process die on anything but the two outcomes a malformed document may have: a value, or an
// InvalidContentException. Two shapes:
//
//   * standalone replay (default): `cna_xna_intermediate_fuzzer replay <file|dir>...` runs every
//     file once; `cna_xna_intermediate_fuzzer mutate <dir> <iterations> [seed]` mutates the
//     corpus deterministically -- this is how the committed corpus is exercised and how a
//     campaign's crashing input is reproduced;
//   * libFuzzer/AFL++ (`-DCNA_XNA_INTERMEDIATE_FUZZER_ENTRY_POINT=ON`, clang): exports
//     LLVMFuzzerTestOneInput and lets the driver own main().
//
// Start a campaign from tests/reference/xna40/intermediate/*.xml and *.input.xml.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Object.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlReader.hpp"

namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::Pipeline::Carrier;
using Microsoft::Xna::Framework::Content::Pipeline::ContentObject;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;

namespace
{
    struct FuzzTexture
    {
        static constexpr std::string_view XnaTypeName = "Cna.Fuzz.Texture";
    };

    enum class FuzzMood { Happy, Sad };

    struct FuzzLeaf : System::Object
    {
        static constexpr std::string_view XnaTypeName = "Cna.Fuzz.Leaf";
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name(XnaTypeName);
            return name;
        }
        std::string Name;
        std::int32_t Value = 0;
        static void DescribeContent(Intermediate::ContentTypeDescriptor<FuzzLeaf>& d)
        {
            d.Field("Name", &FuzzLeaf::Name);
            d.Field("Value", &FuzzLeaf::Value);
        }
    };

    struct FuzzDerivedLeaf : FuzzLeaf
    {
        static constexpr std::string_view XnaTypeName = "Cna.Fuzz.DerivedLeaf";
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name(XnaTypeName);
            return name;
        }
        float Extra = 0;
        static void DescribeContent(Intermediate::ContentTypeDescriptor<FuzzDerivedLeaf>& d)
        {
            d.BaseType<FuzzLeaf>();
            d.Field("Extra", &FuzzDerivedLeaf::Extra);
        }
    };

    struct FuzzRoot : System::Object
    {
        static constexpr std::string_view XnaTypeName = "Cna.Fuzz.Root";
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name(XnaTypeName);
            return name;
        }
        bool Flag = false;
        std::int32_t Count = 0;
        float Weight = 0;
        double Precise = 0;
        std::string Text;
        std::optional<std::string> MaybeText;
        FuzzMood Mood = FuzzMood::Happy;
        Microsoft::Xna::Framework::Vector3 Position;
        Microsoft::Xna::Framework::Curve Curve;
        std::vector<std::int32_t> Packed;
        std::vector<std::string> Items;
        std::vector<std::shared_ptr<FuzzLeaf>> Leaves;
        std::map<std::string, std::int32_t> Map;
        std::optional<std::int32_t> Nullable;
        std::shared_ptr<FuzzLeaf> Leaf;
        std::shared_ptr<FuzzLeaf> Shared;
        std::shared_ptr<FuzzRoot> Child;
        ContentObject Boxed;
        std::shared_ptr<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<FuzzTexture>> Texture;
        std::vector<std::int32_t> Flattened;
        static void DescribeContent(Intermediate::ContentTypeDescriptor<FuzzRoot>& d)
        {
            d.Field("Flag", &FuzzRoot::Flag);
            d.Field("Count", &FuzzRoot::Count).Optional();
            d.Field("Weight", &FuzzRoot::Weight);
            d.Field("Precise", &FuzzRoot::Precise);
            d.Field("Text", &FuzzRoot::Text);
            d.Field("MaybeText", &FuzzRoot::MaybeText).Optional();
            d.Field("Mood", &FuzzRoot::Mood);
            d.Field("Position", &FuzzRoot::Position);
            d.Field("Curve", &FuzzRoot::Curve);
            d.Field("Packed", &FuzzRoot::Packed);
            d.Field("Items", &FuzzRoot::Items);
            d.Field("Leaves", &FuzzRoot::Leaves);
            d.Field("Map", &FuzzRoot::Map);
            d.Field("Nullable", &FuzzRoot::Nullable);
            d.Field("Leaf", &FuzzRoot::Leaf);
            d.Field("Shared", &FuzzRoot::Shared).SharedResource();
            d.Field("Child", &FuzzRoot::Child).Optional();
            d.Field("Boxed", &FuzzRoot::Boxed);
            d.Field("Texture", &FuzzRoot::Texture);
            d.Field("Flattened", &FuzzRoot::Flattened).FlattenContent();
        }
    };
}

CNA_XNA_CONTENT_ENUM(FuzzMood, "Cna.Fuzz.Mood", false, {FuzzMood::Happy, "Happy"}, {FuzzMood::Sad, "Sad"});

namespace
{
    template<typename T>
    void TryRoot(const std::string& xml)
    {
        try
        {
            std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
            Carrier<T> value = Intermediate::IntermediateSerializer::Deserialize<T>(*reader, std::string());
            // A document the reader accepted must also serialize again without failing.
            System::Xml::XmlWriterSettings settings;
            settings.Indent = true;
            std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
            Intermediate::IntermediateSerializer::Serialize<T>(*writer, value, std::string());
            (void)writer->ToString();
        }
        catch (const InvalidContentException&)
        {
            // The one refusal a malformed document may produce.
        }
        catch (const System::Xml::XmlException&)
        {
            // Not well-formed XML: the parser's refusal, before the serializer sees the document.
        }
    }

    void Register()
    {
        static const bool once = []
        {
            Intermediate::IntermediateSerializer::TypeSerializerFor<FuzzRoot>();
            Intermediate::IntermediateSerializer::TypeSerializerFor<FuzzLeaf>();
            Intermediate::IntermediateSerializer::TypeSerializerFor<FuzzDerivedLeaf>();
            return true;
        }();
        (void)once;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    Register();
    std::string xml(reinterpret_cast<const char*>(data), size);
    // tinyxml2 takes a C string: a NUL ends the document, which is a legitimate truncation.
    TryRoot<FuzzRoot>(xml);
    TryRoot<ContentObject>(xml);
    TryRoot<std::vector<std::int32_t>>(xml);
    TryRoot<std::string>(xml);
    TryRoot<Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary>(xml);
    return 0;
}

#ifndef CNA_XNA_INTERMEDIATE_FUZZER_ENTRY_POINT
namespace
{
    struct Rng
    {
        std::uint64_t state;
        std::uint64_t Next()
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        }
    };

    std::vector<std::vector<std::uint8_t>> ReadCorpus(const std::filesystem::path& path, int& unreadable)
    {
        std::vector<std::vector<std::uint8_t>> corpus;
        const auto readOne = [&](const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                ++unreadable;
                return;
            }
            corpus.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        if (std::filesystem::is_directory(path))
        {
            for (const auto& entry : std::filesystem::directory_iterator(path))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".xml")
                {
                    readOne(entry.path());
                }
            }
        }
        else
        {
            readOne(path);
        }
        return corpus;
    }

    std::vector<std::uint8_t> Mutate(const std::vector<std::uint8_t>& seed, Rng& random)
    {
        std::vector<std::uint8_t> out = seed;
        const int edits = 1 + static_cast<int>(random.Next() % 4);
        for (int e = 0; e < edits && !out.empty(); ++e)
        {
            const std::size_t at = random.Next() % out.size();
            switch (random.Next() % 6)
            {
            case 0: out[at] = static_cast<std::uint8_t>(random.Next()); break;
            case 1: out.erase(out.begin() + static_cast<std::ptrdiff_t>(at)); break;
            case 2: out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(random.Next())); break;
            case 3: out.resize(at); break;
            case 4:
            {
                const std::size_t length = std::min<std::size_t>(out.size() - at, random.Next() % 40);
                std::vector<std::uint8_t> chunk(out.begin() + static_cast<std::ptrdiff_t>(at),
                                                out.begin() + static_cast<std::ptrdiff_t>(at + length));
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(random.Next() % (out.size() + 1)), chunk.begin(), chunk.end());
                break;
            }
            default:
            {
                static const char* const tokens[] = {"<Item>", "</Item>", "Null=\"true\"", "Type=\"int\"", "#Resource1",
                                                     "<Resources>", "</Resources>", "1e39", "NaN", "0x10", "<", "&", "]]>",
                                                     "<!DOCTYPE x>", "FlattenContent", "<Child>", "</Child>"};
                const char* token = tokens[random.Next() % (sizeof(tokens) / sizeof(tokens[0]))];
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), token, token + std::strlen(token));
                break;
            }
            }
        }
        return out;
    }
}

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "replay" && argc > 2)
    {
        int unreadable = 0;
        std::size_t count = 0;
        for (int i = 2; i < argc; ++i)
        {
            for (const auto& bytes : ReadCorpus(std::filesystem::path(argv[i]), unreadable))
            {
                LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
                ++count;
            }
        }
        std::printf("replayed %zu inputs (%d unreadable)\n", count, unreadable);
        return unreadable == 0 ? 0 : 1;
    }
    if (mode == "mutate" && argc > 3)
    {
        int unreadable = 0;
        const auto corpus = ReadCorpus(std::filesystem::path(argv[2]), unreadable);
        if (corpus.empty())
        {
            std::fprintf(stderr, "no corpus files under %s\n", argv[2]);
            return 2;
        }
        const long iterations = std::strtol(argv[3], nullptr, 10);
        Rng random{argc > 4 ? std::strtoull(argv[4], nullptr, 0) : 0x584E41505035ULL};
        for (long i = 0; i < iterations; ++i)
        {
            const std::vector<std::uint8_t> candidate = Mutate(corpus[random.Next() % corpus.size()], random);
            LLVMFuzzerTestOneInput(candidate.data(), candidate.size());
        }
        std::printf("mutated %ld inputs from %zu seeds\n", iterations, corpus.size());
        return 0;
    }
    std::fprintf(stderr,
                 "usage: %s replay <file|dir>...\n"
                 "       %s mutate <dir> <iterations> [seed]\n"
                 "Build with -DCNA_XNA_INTERMEDIATE_FUZZER_ENTRY_POINT=ON under clang for a coverage-guided\n"
                 "libFuzzer campaign; seed it from tests/reference/xna40/intermediate/.\n",
                 argv[0], argv[0]);
    return 2;
}
#endif
