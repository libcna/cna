// SPDX-License-Identifier: MS-PL
//
// XNAPP-290: the two modelling readers under hostile input.
//
// A `.x` file and an FBX document both arrive from outside the build, and both readers are new.
// A malformed one has exactly two permitted outcomes -- a graph, or an InvalidContentException --
// never a crash, a hang, an unbounded allocation or another exception type. The mutation pass is
// deterministic (fixed seed) so a failure reproduces, and it runs over the committed corpus so
// every construct the readers understand is what gets mutated.
//
// The ceilings themselves are checked separately: a document that declares a size no host could
// serve must be refused on the declaration rather than by trying to allocate it.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/DirectXFileReader.hpp"
#include "CNA/Content/Pipeline/FbxFileReader.hpp"

namespace Canon = CNA::Content::Pipeline;

namespace
{
    std::filesystem::path CorpusDirectory()
    {
        const std::filesystem::path relative = "tests/assets/xna40/model";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    std::vector<std::uint8_t> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

    std::vector<std::filesystem::path> Corpus(const std::string& extension)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(CorpusDirectory(), error))
        {
            if (!error && entry.is_regular_file() && entry.path().extension() == extension)
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    /**
     * @brief One mutated copy: a byte flipped, a run truncated, or a run zeroed.
     *
     * Three shapes rather than one, because they break different things: a flip corrupts a value
     * in place, a truncation ends the document mid-structure, and a zero run turns a length or a
     * count into something a reader might trust.
     */
    std::vector<std::uint8_t> Mutate(const std::vector<std::uint8_t>& source, std::mt19937& random)
    {
        if (source.empty()) { return source; }
        std::vector<std::uint8_t> out = source;
        switch (random() % 3u)
        {
            case 0:
            {
                const std::size_t at = random() % out.size();
                out[at] = static_cast<std::uint8_t>(random() % 256u);
                break;
            }
            case 1:
                out.resize(1u + (random() % out.size()));
                break;
            default:
            {
                const std::size_t at = random() % out.size();
                const std::size_t length = std::min<std::size_t>(out.size() - at, 1u + (random() % 32u));
                std::fill(out.begin() + static_cast<std::ptrdiff_t>(at),
                          out.begin() + static_cast<std::ptrdiff_t>(at + length), 0u);
                break;
            }
        }
        return out;
    }
}

TEST(XnaModelReaderHardening, NoMutatedXFileCrashesOrEscapesItsOwnExceptionType)
{
    const std::vector<std::filesystem::path> corpus = Corpus(".x");
    ASSERT_FALSE(corpus.empty()) << "the .x corpus is not on disk";
    std::mt19937 random(20260906u);
    std::size_t parsed = 0;
    std::size_t refused = 0;
    for (const std::filesystem::path& file : corpus)
    {
        const std::vector<std::uint8_t> original = Read(file);
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            const std::vector<std::uint8_t> mutated = Mutate(original, random);
            try
            {
                const Canon::DirectXFile document = Canon::ReadDirectXFile(mutated);
                ++parsed;
                // Whatever came back must be walkable without trusting anything in it.
                for (const Canon::DirectXFileObject& object : document.objects)
                {
                    EXPECT_LE(object.numbers.size(), 64u * 1024u * 1024u);
                }
            }
            catch (const Canon::DirectXFileException&)
            {
                ++refused;
            }
            // Any other exception type escaping is the failure this test exists to catch, and
            // gtest reports it as one.
        }
    }
    // Both outcomes must actually occur, or the mutation is not reaching the reader.
    EXPECT_GT(refused, 0u);
    EXPECT_GT(parsed, 0u);
}

TEST(XnaModelReaderHardening, NoMutatedFbxDocumentCrashesOrEscapesItsOwnExceptionType)
{
    const std::vector<std::filesystem::path> corpus = Corpus(".fbx");
    ASSERT_FALSE(corpus.empty()) << "the FBX corpus is not on disk";
    std::mt19937 random(20260906u);
    std::size_t parsed = 0;
    std::size_t refused = 0;
    for (const std::filesystem::path& file : corpus)
    {
        const std::vector<std::uint8_t> original = Read(file);
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            const std::vector<std::uint8_t> mutated = Mutate(original, random);
            try
            {
                const Canon::FbxFile document = Canon::ReadFbxFile(mutated);
                ++parsed;
                for (const Canon::FbxNode& node : document.nodes)
                {
                    EXPECT_LE(node.children.size(), 4000000u);
                }
            }
            catch (const Canon::FbxFileException&)
            {
                ++refused;
            }
        }
    }
    EXPECT_GT(refused, 0u);
    EXPECT_GT(parsed, 0u);
}

// A declared size no host could serve is refused on the declaration, not by trying to allocate it.
// XNASWEEP-195: a binary `.x` that declares its templates
// inline. The binary form spells `template` as a token rather than as the word, and the reader
// mapped that token to a generic keyword name -- so the parser's own `SkipTemplate` never fired,
// the declaration was read as an *object*, and the first `array` inside it (token 52, which was
// missing from the keyword list outright) ended the parse with `D3DXFERR_PARSEERROR`.
// SAMPLE-141's `target.x` and `xwing.x` are exactly this shape and XNA read both.
TEST(XnaModelReaderHardening, ABinaryXFileMayDeclareItsTemplatesInline)
{
    const auto word = [](std::vector<std::uint8_t>& out, const std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    const auto dword = [](std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
        for (int at = 0; at < 4; ++at)
        {
            out.push_back(static_cast<std::uint8_t>((value >> (8 * at)) & 0xFFu));
        }
    };
    const auto name = [&](std::vector<std::uint8_t>& out, const std::string& text)
    {
        word(out, 1u);
        dword(out, static_cast<std::uint32_t>(text.size()));
        out.insert(out.end(), text.begin(), text.end());
    };

    std::vector<std::uint8_t> bytes;
    const std::string header = "xof 0303bin 0032";
    bytes.insert(bytes.end(), header.begin(), header.end());

    // template Vector { <guid> FLOAT x; FLOAT y; FLOAT z; }
    word(bytes, 31u);                                  // TOKEN_TEMPLATE
    name(bytes, "Vector");
    word(bytes, 10u);                                  // {
    word(bytes, 5u);                                   // TOKEN_GUID
    bytes.insert(bytes.end(), 16u, 0u);
    for (const char* member : {"x", "y", "z"})
    {
        word(bytes, 42u);                              // TOKEN_FLOAT
        name(bytes, member);
        word(bytes, 20u);                              // ;
    }
    // array FLOAT more[3];  -- the token that ended the parse
    word(bytes, 52u);                                  // TOKEN_ARRAY
    word(bytes, 42u);                                  // TOKEN_FLOAT
    name(bytes, "more");
    word(bytes, 14u);                                  // [
    word(bytes, 3u);                                   // TOKEN_INTEGER
    dword(bytes, 3u);
    word(bytes, 15u);                                  // ]
    word(bytes, 20u);                                  // ;
    word(bytes, 11u);                                  // }

    // Vector Here { 1.0; 2.0; 3.0; }
    name(bytes, "Vector");
    name(bytes, "Here");
    word(bytes, 10u);                                  // {
    word(bytes, 7u);                                   // TOKEN_FLOAT_LIST
    dword(bytes, 3u);
    for (const float value : {1.0f, 2.0f, 3.0f})
    {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        dword(bytes, raw);
    }
    word(bytes, 11u);                                  // }

    Canon::DirectXFile document;
    ASSERT_NO_THROW(document = Canon::ReadDirectXFile(bytes));
    ASSERT_EQ(document.objects.size(), 1u) << "the template must be skipped, not read as an object";
    EXPECT_TRUE(document.objects[0].TypeIs("Vector"));
    EXPECT_EQ(document.objects[0].name, "Here");
    ASSERT_EQ(document.objects[0].numbers.size(), 3u);
    EXPECT_FLOAT_EQ(static_cast<float>(document.objects[0].numbers[0]), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(document.objects[0].numbers[2]), 3.0f);
}

namespace
{
    /**
     * @brief A binary FBX 7400 document: "Objects" holding one "Model" with one 'I' property.
     *
     * Offsets (hand-derived): the header is 27 bytes; a 7400 record header is 13 bytes (end,
     * property count, property-list length as uint32, then a name-length byte). "Objects" starts
     * at 27 and its child "Model" at 27 + 13 + 7 = 47; Model's name and one 5-byte 'I' property end
     * at 47 + 13 + 5 + 5 = 70. A 13-byte null record closes Objects' child list at 83, and another
     * closes the document at 96.
     */
    std::vector<std::uint8_t> NestedFbx(const std::uint32_t modelEnd, const std::uint32_t objectsEnd)
    {
        std::vector<std::uint8_t> fbx;
        const std::string magic = "Kaydara FBX Binary  ";
        fbx.insert(fbx.end(), magic.begin(), magic.end());
        fbx.insert(fbx.end(), {0, 0x1A, 0});
        const auto put32 = [&fbx](const std::uint32_t value)
        {
            for (int at = 0; at < 4; ++at) { fbx.push_back(static_cast<std::uint8_t>(value >> (8 * at))); }
        };
        put32(7400u);
        const auto record = [&](const std::uint32_t end, const std::uint32_t properties,
                                const std::uint32_t listLength, const std::string& name)
        {
            put32(end);
            put32(properties);
            put32(listLength);
            fbx.push_back(static_cast<std::uint8_t>(name.size()));
            fbx.insert(fbx.end(), name.begin(), name.end());
        };
        record(objectsEnd, 0u, 0u, "Objects");
        record(modelEnd, 1u, 5u, "Model");
        fbx.push_back('I');
        put32(42u);
        fbx.resize(fbx.size() + 13u, 0u);   // closes Objects' children
        fbx.resize(fbx.size() + 13u, 0u);   // closes the document
        return fbx;
    }
}

// MTH-SAFE-007 regression, found by the long fuzz campaign (xna-model, seed 20261104045844): one
// byte of fbx_binary_modern.fbx changed a record's end offset to the record's own start. The
// reader then rewound to it after every read, and the parent re-read the same record until the
// four-million-node ceiling refused the document -- 106 s and 2.4 GB under ASan for 23 KB. A
// record's end may lie neither before its own properties nor before the end of its children.
TEST(XnaModelReaderHardening, ARecordEndIsNeverBehindWhatTheRecordAlreadyRead)
{
    // A low node ceiling keeps a regression fast: re-reading would hit it after 1000 records.
    Canon::FbxFileLimits limits;
    limits.maximumNodes = 1000u;
    const auto expectRefusal = [&limits](const std::vector<std::uint8_t>& fbx, const char* reason,
                                         const char* why)
    {
        try
        {
            (void)Canon::ReadFbxFile(fbx, limits);
            ADD_FAILURE() << "accepted: " << why;
        }
        catch (const Canon::FbxFileException& error)
        {
            EXPECT_EQ(error.Error(), Canon::FbxFileError::ParseError) << why;
            EXPECT_NE(std::string(error.what()).find(reason), std::string::npos) << why << ": " << error.what();
        }
    };

    // The well-formed document, so the two refusals below are about the end offsets alone.
    Canon::FbxFile document;
    ASSERT_NO_THROW(document = Canon::ReadFbxFile(NestedFbx(70u, 83u), limits));
    ASSERT_EQ(document.nodes.size(), 1u);
    ASSERT_EQ(document.nodes[0].children.size(), 1u);
    EXPECT_EQ(document.nodes[0].children[0].name, "Model");
    EXPECT_DOUBLE_EQ(document.nodes[0].children[0].Number(0, 0.0), 42.0);

    expectRefusal(NestedFbx(47u, 83u), "ends before its own properties",
                  "Model ends at its own start, before its property");
    expectRefusal(NestedFbx(69u, 83u), "ends before its own properties",
                  "Model ends one byte inside its property");
    expectRefusal(NestedFbx(70u, 60u), "runs past the end of its parent",
                  "Model runs past the end of its parent Objects");
}

TEST(XnaModelReaderHardening, ADeclaredSizeIsCheckedBeforeItIsBelieved)
{
    const Canon::DirectXFileLimits tight{4096u, 4u, 64u, 16u, 32u};
    const std::string deep = "xof 0303txt 0032\n" + [] {
        std::string text;
        for (int i = 0; i < 40; ++i) { text += "Frame F" + std::to_string(i) + " {\n"; }
        for (int i = 0; i < 40; ++i) { text += "}\n"; }
        return text;
    }();
    const auto bytes = [&deep] {
        return std::vector<std::uint8_t>(deep.begin(), deep.end());
    }();
    try
    {
        (void)Canon::ReadDirectXFile(bytes, tight);
        ADD_FAILURE() << "a document nested past the ceiling was accepted";
    }
    catch (const Canon::DirectXFileException& error)
    {
        EXPECT_EQ(error.Error(), Canon::DirectXFileError::ParseError);
        EXPECT_NE(std::string(error.what()).find("nest deeper"), std::string::npos);
    }

    // A binary FBX array header claiming four billion doubles: refused on the count, and the
    // refusal must arrive without the reader having tried to make room for it.
    std::vector<std::uint8_t> fbx;
    const std::string magic = "Kaydara FBX Binary  ";
    fbx.insert(fbx.end(), magic.begin(), magic.end());
    fbx.insert(fbx.end(), {0, 0x1A, 0});
    const std::uint32_t version = 7400u;
    const auto* raw = reinterpret_cast<const std::uint8_t*>(&version);
    fbx.insert(fbx.end(), raw, raw + 4);
    // One record: end offset past the header, one property, a name, then a 'd' array of 2^32-1.
    const auto put32 = [&fbx](const std::uint32_t value)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        fbx.insert(fbx.end(), bytes, bytes + 4);
    };
    put32(200u);            // end offset
    put32(1u);              // property count
    put32(20u);             // property list length
    fbx.push_back(8u);
    const std::string name = "Vertices";
    fbx.insert(fbx.end(), name.begin(), name.end());
    fbx.push_back('d');
    put32(0xFFFFFFFFu);     // count
    put32(0u);              // encoding: uncompressed
    put32(0u);              // compressed length
    fbx.resize(200u, 0u);
    fbx.resize(225u, 0u);   // the null record that ends the list
    try
    {
        (void)Canon::ReadFbxFile(fbx);
        ADD_FAILURE() << "an array claiming four billion doubles was accepted";
    }
    catch (const Canon::FbxFileException& error)
    {
        EXPECT_EQ(error.Error(), Canon::FbxFileError::ParseError);
        EXPECT_NE(std::string(error.what()).find("longer than this reader accepts"),
                  std::string::npos);
    }
}
