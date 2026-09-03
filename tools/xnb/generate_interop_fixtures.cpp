// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-30/XNAP-31: the CNA-generated XNB corpus, and the
// expected-value manifest for each fixture, that a genuine Microsoft XNA 4.0 runtime can be
// pointed at.
//
// This tool exists because CNA cannot run XNA here. What it can do is produce, deterministically,
// exactly the files an XNA-capable Windows installation would need to load -- together with a
// statement of what a correct runtime must observe in each. The harness under
// tests/interop/xna40/ consumes both.
//
// Everything written is a pure function of this source file: no clock, no random source, no host
// paths. Running the tool twice must produce identical bytes, and a test asserts exactly that
// against the committed corpus.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

namespace Xnb = CNA::Internal::Xnb;

namespace
{
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    /** @brief Minimal JSON emission, sufficient for a manifest of scalars, arrays and objects. */
    class Json
    {
    public:
        /** @brief Writes an object opening brace and begins tracking separators. */
        void BeginObject() { Separate(); text_ += "{"; first_.push_back(true); }

        /** @brief Closes the current object. */
        void EndObject() { text_ += "}"; first_.pop_back(); MarkWritten(); }

        /** @brief Writes an array opening bracket. */
        void BeginArray() { Separate(); text_ += "["; first_.push_back(true); }

        /** @brief Closes the current array. */
        void EndArray() { text_ += "]"; first_.pop_back(); MarkWritten(); }

        /** @brief Writes a member key inside the current object. @param name The key. */
        void Key(const std::string& name)
        {
            Separate();
            text_ += Quote(name);
            text_ += ":";
            if (!first_.empty()) { first_.back() = true; }
        }

        /** @brief Writes a string value. @param value The text. */
        void String(const std::string& value) { Separate(); text_ += Quote(value); MarkWritten(); }

        /** @brief Writes an integer value. @param value The number. */
        void Number(const std::int64_t value)
        {
            Separate();
            text_ += std::to_string(value);
            MarkWritten();
        }

        /**
         * @brief Writes a floating-point value with enough digits to round-trip.
         * @param value The number.
         */
        void Real(const double value)
        {
            Separate();
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.precision(9);
            stream << value;
            text_ += stream.str();
            MarkWritten();
        }

        /** @brief Writes a boolean value. @param value The flag. */
        void Bool(const bool value) { Separate(); text_ += value ? "true" : "false"; MarkWritten(); }

        /** @brief Returns the complete document with a trailing newline. */
        [[nodiscard]] std::string Take() const { return text_ + "\n"; }

    private:
        void Separate()
        {
            if (first_.empty()) { return; }
            if (!first_.back()) { text_ += ","; }
        }

        void MarkWritten()
        {
            if (!first_.empty()) { first_.back() = false; }
        }

        [[nodiscard]] static std::string Quote(const std::string& value)
        {
            std::string out = "\"";
            for (const char character : value)
            {
                switch (character)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    default: out += character; break;
                }
            }
            return out + "\"";
        }

        std::string text_;
        std::vector<bool> first_;
    };

    /** @brief FNV-1a 64-bit, matching the conformance parser's own payload digest. */
    [[nodiscard]] std::string Digest(const std::vector<std::uint8_t>& data)
    {
        std::uint64_t value = 0xCBF29CE484222325ull;
        for (const std::uint8_t byte : data)
        {
            value = (value ^ byte) * 0x100000001B3ull;
        }
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string text(16u, '0');
        for (std::size_t index = 0u; index < 16u; ++index)
        {
            text[15u - index] = kDigits[(value >> (index * 4u)) & 0xFu];
        }
        return text;
    }

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    /** @brief One fixture: its bytes, the values a runtime must observe, and why it exists. */
    struct Fixture
    {
        std::string name;
        std::vector<std::uint8_t> bytes;
        std::string expectationJson;
        std::string purpose;
    };

    [[nodiscard]] std::vector<std::uint8_t> Gradient(const std::uint32_t width,
                                                     const std::uint32_t height)
    {
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width) * height * 4u, 0u);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
                pixels[offset + 0u] = static_cast<std::uint8_t>(x * 16u);
                pixels[offset + 1u] = static_cast<std::uint8_t>(y * 32u);
                pixels[offset + 2u] = 0x80u;
                pixels[offset + 3u] = 0xFFu;
            }
        }
        return pixels;
    }

    [[nodiscard]] Fixture MakeTexture2D(const Xnb::XnbFileOptions& options)
    {
        Xnb::XnbTextureData texture;
        texture.kind = Xnb::XnbTextureKind::Texture2D;
        texture.surfaceFormat = SurfaceFormat::Color;
        texture.width = 8u;
        texture.height = 4u;
        texture.mipCount = 4u;
        texture.levels = {Gradient(8u, 4u), Gradient(4u, 2u), Gradient(2u, 1u),
                          Gradient(1u, 1u)};

        Fixture fixture;
        fixture.name = "texture2d_color_mips";
        fixture.purpose =
            "Texture2D, SurfaceFormat.Color, 8x4 with a complete mip chain. A correct runtime "
            "must report Width 8, Height 4, LevelCount 4, Format Color, and level 0's first "
            "texel must be R=0 G=0 B=128 A=255.";
        fixture.bytes = Xnb::WriteXnbAsset(Xnb::XnbTexture2DContent{texture}, options,
                                           fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.Texture2DReader");
        json.Key("root");
        json.BeginObject();
        json.Key("kind"); json.String("Texture2D");
        json.Key("surfaceFormat"); json.String("Color");
        json.Key("width"); json.Number(8);
        json.Key("height"); json.Number(4);
        json.Key("mipCount"); json.Number(4);
        json.Key("levelByteSizes");
        json.BeginArray();
        json.Number(128); json.Number(32); json.Number(8); json.Number(4);
        json.EndArray();
        json.Key("levelDigests");
        json.BeginArray();
        for (const std::vector<std::uint8_t>& level : texture.levels)
        {
            json.String(Digest(level));
        }
        json.EndArray();
        json.EndObject();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }

    [[nodiscard]] Fixture MakeSoundEffect(const Xnb::XnbFileOptions& options)
    {
        Xnb::XnbSoundEffectData sound;
        sound.formatTag = 1u;
        sound.channels = 1u;
        sound.sampleRate = 22050u;
        sound.bitsPerSample = 16u;
        sound.blockAlign = 2u;
        sound.averageBytesPerSecond = 44100u;
        sound.samples.resize(2205u * 2u);
        for (std::size_t frame = 0u; frame * 2u + 1u < sound.samples.size(); ++frame)
        {
            const auto value = static_cast<std::int16_t>(
                ((frame % 50u) < 25u) ? 8000 : -8000);
            const auto bits = static_cast<std::uint16_t>(value);
            sound.samples[frame * 2u] = static_cast<std::uint8_t>(bits & 0xFFu);
            sound.samples[frame * 2u + 1u] = static_cast<std::uint8_t>(bits >> 8u);
        }
        sound.loopStart = 0;
        sound.loopLength = 0;
        sound.storedDurationMs = 100u;

        Fixture fixture;
        fixture.name = "soundeffect_pcm16_mono_22050";
        fixture.purpose =
            "SoundEffect, 16-bit PCM, mono, 22050 Hz, 2205 frames (100 ms). A correct runtime "
            "must report Duration 100 ms and must play a 441 Hz square wave.";
        fixture.bytes = Xnb::WriteXnbAsset(sound, options, fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.SoundEffectReader");
        json.Key("root");
        json.BeginObject();
        json.Key("formatTag"); json.Number(1);
        json.Key("channels"); json.Number(1);
        json.Key("sampleRate"); json.Number(22050);
        json.Key("bitsPerSample"); json.Number(16);
        json.Key("blockAlign"); json.Number(2);
        json.Key("averageBytesPerSecond"); json.Number(44100);
        json.Key("sampleByteCount"); json.Number(static_cast<std::int64_t>(sound.samples.size()));
        json.Key("sampleDigest"); json.String(Digest(sound.samples));
        json.Key("durationMs"); json.Number(100);
        json.EndObject();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }

    [[nodiscard]] Fixture MakeSpriteFont(const Xnb::XnbFileOptions& options)
    {
        Xnb::XnbSpriteFontData font;
        font.atlas.kind = Xnb::XnbTextureKind::Texture2D;
        font.atlas.surfaceFormat = SurfaceFormat::Color;
        font.atlas.width = 8u;
        font.atlas.height = 8u;
        font.atlas.mipCount = 1u;
        font.atlas.levels = {Gradient(8u, 8u)};
        font.glyphs = {Rectangle(0, 0, 4, 8), Rectangle(4, 0, 4, 8)};
        font.cropping = {Rectangle(0, 1, 4, 7), Rectangle(1, 1, 3, 7)};
        font.characters = {u'A', u'B'};
        font.lineSpacing = 10;
        font.spacing = 1.0f;
        font.kerning = {Vector3{0.0f, 4.0f, 0.0f}, Vector3{1.0f, 3.0f, 0.5f}};
        font.defaultCharacter = u'A';

        Fixture fixture;
        fixture.name = "spritefont_two_glyphs";
        fixture.purpose =
            "SpriteFont with exactly two glyphs, 'A' and 'B'. A correct runtime must report "
            "Characters {A, B}, LineSpacing 10, Spacing 1, DefaultCharacter 'A', and "
            "MeasureString(\"A\") must be (4, 10).";
        fixture.bytes = Xnb::WriteXnbAsset(font, options, fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.SpriteFontReader");
        json.Key("root");
        json.BeginObject();
        json.Key("characters");
        json.BeginArray(); json.String("A"); json.String("B"); json.EndArray();
        json.Key("lineSpacing"); json.Number(10);
        json.Key("spacing"); json.Real(1.0);
        json.Key("defaultCharacter"); json.String("A");
        json.Key("glyphs");
        json.BeginArray();
        for (const Rectangle& glyph : font.glyphs)
        {
            json.BeginArray();
            json.Number(glyph.X); json.Number(glyph.Y);
            json.Number(glyph.Width); json.Number(glyph.Height);
            json.EndArray();
        }
        json.EndArray();
        json.EndObject();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }

    [[nodiscard]] Fixture MakeCurve(const Xnb::XnbFileOptions& options)
    {
        using Microsoft::Xna::Framework::Curve;
        using Microsoft::Xna::Framework::CurveContinuity;
        using Microsoft::Xna::Framework::CurveKey;
        using Microsoft::Xna::Framework::CurveLoopType;

        Curve curve;
        curve.setPreLoopProperty(CurveLoopType::Constant);
        curve.setPostLoopProperty(CurveLoopType::Linear);
        curve.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 1.0f, CurveContinuity::Smooth));
        curve.getKeysProperty().Add(CurveKey(1.0f, 2.0f, 1.0f, 0.0f, CurveContinuity::Smooth));

        Fixture fixture;
        fixture.name = "curve_two_keys";
        fixture.purpose =
            "Curve with two keys. A correct runtime must report PreLoop Constant, PostLoop "
            "Linear, Keys.Count 2, Evaluate(0) == 0 and Evaluate(1) == 2.";
        fixture.bytes = Xnb::WriteXnbAsset(curve, options, fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.CurveReader");
        json.Key("root");
        json.BeginObject();
        // CurveLoopType ordinals: Constant 0, Cycle 1, CycleOffset 2, Oscillate 3, Linear 4.
        json.Key("preLoop"); json.Number(0);
        json.Key("postLoop"); json.Number(4);
        json.Key("keys");
        json.BeginArray();
        json.BeginObject();
        json.Key("position"); json.Real(0.0); json.Key("value"); json.Real(0.0);
        json.Key("tangentIn"); json.Real(0.0); json.Key("tangentOut"); json.Real(1.0);
        json.Key("continuity"); json.Number(0);
        json.EndObject();
        json.BeginObject();
        json.Key("position"); json.Real(1.0); json.Key("value"); json.Real(2.0);
        json.Key("tangentIn"); json.Real(1.0); json.Key("tangentOut"); json.Real(0.0);
        json.Key("continuity"); json.Number(0);
        json.EndObject();
        json.EndArray();
        json.EndObject();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }

    [[nodiscard]] Fixture MakeStringList(const Xnb::XnbFileOptions& options)
    {
        const std::vector<std::string> items{"alpha", "beta", "gamma"};

        Fixture fixture;
        fixture.name = "list_of_strings";
        fixture.purpose =
            "List<string> with three entries. A correct runtime must load it as "
            "List<string> {\"alpha\", \"beta\", \"gamma\"}. This is the type the committed "
            "genuine XNA 4.0 fixture also uses, so CNA's spelling of it is byte-proven.";
        fixture.bytes = Xnb::WriteXnbAsset(items, options, fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.ListReader`1[[System.String]]");
        json.Key("root");
        json.BeginArray();
        for (const std::string& item : items) { json.String(item); }
        json.EndArray();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }

    [[nodiscard]] Fixture MakeModel(const Xnb::XnbFileOptions& options)
    {
        Xnb::XnbModelData model;

        Xnb::XnbModelBoneData root;
        root.name = "RootNode";
        root.transform = Matrix::getIdentityProperty();
        root.parent = -1;
        root.children = {1};
        model.bones.push_back(root);

        Xnb::XnbModelBoneData child;
        child.name = "Triangle";
        child.transform = Matrix::getIdentityProperty();
        child.parent = 0;
        model.bones.push_back(child);
        model.rootBone = 0;

        Xnb::XnbVertexBufferData vertexBuffer;
        vertexBuffer.declaration.stride = 24;
        vertexBuffer.declaration.elements.emplace_back(
            0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
        vertexBuffer.declaration.elements.emplace_back(
            12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
        vertexBuffer.vertexCount = 3u;
        const float vertices[18] = {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        };
        vertexBuffer.bytes.resize(sizeof(vertices));
        std::memcpy(vertexBuffer.bytes.data(), vertices, sizeof(vertices));

        Xnb::XnbIndexBufferData indexBuffer;
        indexBuffer.indexElementSize = 2u;
        indexBuffer.bytes = {0u, 0u, 1u, 0u, 2u, 0u};

        Xnb::XnbBasicEffectData effect;
        effect.diffuseColor = Vector3{1.0f, 0.5f, 0.25f};
        effect.emissiveColor = Vector3{0.0f, 0.0f, 0.0f};
        effect.specularColor = Vector3{1.0f, 1.0f, 1.0f};
        effect.specularPower = 16.0f;
        effect.alpha = 1.0f;
        effect.vertexColorEnabled = false;

        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.VertexBufferReader", vertexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.IndexBufferReader", indexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.BasicEffectReader", effect});

        Xnb::XnbModelPartData part;
        part.vertexOffset = 0;
        part.vertexCount = 3;
        part.startIndex = 0;
        part.primitiveCount = 1;
        part.vertexBufferResource = 0;
        part.indexBufferResource = 1;
        part.effectResource = 2;

        Xnb::XnbModelMeshData mesh;
        mesh.name = "Triangle";
        mesh.parentBone = 1;
        mesh.boundingSphere =
            BoundingSphere(Vector3{0.5f, 0.5f, 0.0f}, 0.70710678f);
        mesh.parts.push_back(part);
        model.meshes.push_back(mesh);

        Fixture fixture;
        fixture.name = "model_triangle_basiceffect";
        fixture.purpose =
            "Model with 2 bones ('RootNode' -> 'Triangle'), one mesh, one mesh part, a "
            "Position+Normal vertex declaration of stride 24, a 16-bit index buffer of 3 indices "
            "and a BasicEffect. A correct runtime must report Bones.Count 2, Meshes.Count 1, "
            "Meshes[0].MeshParts.Count 1, PrimitiveCount 1, NumVertices 3, and the mesh part's "
            "Effect must be a BasicEffect with DiffuseColor (1, 0.5, 0.25).";
        fixture.bytes = Xnb::WriteXnbAsset(model, options, fixture.name);

        Json json;
        json.BeginObject();
        json.Key("rootReader");
        json.String("Microsoft.Xna.Framework.Content.ModelReader");
        json.Key("sharedResourceCount"); json.Number(3);
        json.Key("root");
        json.BeginObject();
        json.Key("rootBone"); json.Number(0);
        json.Key("bones");
        json.BeginArray();
        json.BeginObject();
        json.Key("name"); json.String("RootNode");
        json.Key("parent"); json.Number(-1);
        json.Key("children"); json.BeginArray(); json.Number(1); json.EndArray();
        json.EndObject();
        json.BeginObject();
        json.Key("name"); json.String("Triangle");
        json.Key("parent"); json.Number(0);
        json.Key("children"); json.BeginArray(); json.EndArray();
        json.EndObject();
        json.EndArray();
        json.Key("meshes");
        json.BeginArray();
        json.BeginObject();
        json.Key("name"); json.String("Triangle");
        json.Key("parentBone"); json.Number(1);
        json.Key("parts");
        json.BeginArray();
        json.BeginObject();
        json.Key("vertexOffset"); json.Number(0);
        json.Key("numVertices"); json.Number(3);
        json.Key("startIndex"); json.Number(0);
        json.Key("primitiveCount"); json.Number(1);
        json.Key("vertexBuffer"); json.Number(1);
        json.Key("indexBuffer"); json.Number(2);
        json.Key("effect"); json.Number(3);
        json.EndObject();
        json.EndArray();
        json.EndObject();
        json.EndArray();
        json.EndObject();
        json.EndObject();
        fixture.expectationJson = json.Take();
        return fixture;
    }
}

namespace
{
    /** @brief Builds every fixture at one container configuration. */
    [[nodiscard]] std::vector<Fixture> MakeAllFixtures(const Xnb::XnbFileOptions& options)
    {
        std::vector<Fixture> fixtures;
        fixtures.push_back(MakeTexture2D(options));
        fixtures.push_back(MakeSoundEffect(options));
        fixtures.push_back(MakeSpriteFont(options));
        fixtures.push_back(MakeCurve(options));
        fixtures.push_back(MakeStringList(options));
        fixtures.push_back(MakeModel(options));
        return fixtures;
    }

    /**
     * @brief Writes one corpus directory: every fixture, its expectation, and the index.
     *
     * @param root Directory to write into.
     * @param fixtures Fixtures to write.
     * @param compressionName Value recorded in the index's `compression` field.
     * @param note Provenance note recorded in the index.
     */
    void WriteCorpus(const std::filesystem::path& root, const std::vector<Fixture>& fixtures,
                     const std::string& compressionName, const std::string& note)
    {
        Json index;
        index.BeginObject();
        index.Key("producer");
        index.String("CNA native XNB writer (tools/xnb/generate_interop_fixtures.cpp)");
        index.Key("targetPlatform"); index.String("windows");
        index.Key("containerVersion"); index.Number(5);
        index.Key("graphicsProfile"); index.String("Reach");
        index.Key("compression"); index.String(compressionName);
        index.Key("readerNameStyle"); index.String("xna40");
        index.Key("license");
        index.String("MS-PL (same license as CNA itself); every byte here was generated by CNA, "
                     "and no third-party asset is embedded.");
        index.Key("note"); index.String(note);
        index.Key("fixtures");
        index.BeginArray();

        for (const Fixture& fixture : fixtures)
        {
            WriteFile(root / (fixture.name + ".xnb"), fixture.bytes);
            WriteFile(root / (fixture.name + ".expected.json"), fixture.expectationJson);
            index.BeginObject();
            index.Key("name"); index.String(fixture.name);
            index.Key("file"); index.String(fixture.name + ".xnb");
            index.Key("expectation"); index.String(fixture.name + ".expected.json");
            index.Key("byteCount"); index.Number(static_cast<std::int64_t>(fixture.bytes.size()));
            index.Key("digest"); index.String(Digest(fixture.bytes));
            index.Key("purpose"); index.String(fixture.purpose);
            index.EndObject();
        }

        index.EndArray();
        index.EndObject();
        WriteFile(root / "fixtures.json", index.Take());
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "usage: cna_xnb_interop_fixtures <output-directory> [<lzx-output-directory>]\n"
                  << "Writes the CNA-generated XNB interoperability corpus and its\n"
                  << "expected-value manifests (plans/plan_xnapipeline.md XNAP-30/XNAP-31).\n"
                  << "With a second directory, writes the same fixtures again LZX-compressed\n"
                  << "(XNAP-81) -- the same assets, the same expectations, the compression\n"
                  << "Microsoft XNA 4.0 itself produced.\n";
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    Xnb::XnbFileOptions options;
    options.platform = Xnb::XnbTargetPlatform::Windows;
    options.version = Xnb::XnbContainerVersion::Xna40;
    options.graphicsProfile = Xnb::XnbGraphicsProfile::Reach;
    options.compression = Xnb::XnbOutputCompression::None;
    options.readerNameStyle = Xnb::XnbReaderNameStyle::Xna40;

    std::vector<Fixture> fixtures;
    std::vector<Fixture> compressed;
    try
    {
        fixtures = MakeAllFixtures(options);
        if (argc == 3)
        {
            Xnb::XnbFileOptions lzx = options;
            lzx.compression = Xnb::XnbOutputCompression::Lzx;
            compressed = MakeAllFixtures(lzx);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "fixture generation failed: " << error.what() << "\n";
        return 1;
    }

    WriteCorpus(root, fixtures, "none",
                "These files were written by CNA, not by Microsoft XNA. They exist so an "
                "XNA-capable Windows installation can attempt ContentManager.Load<T>() on them "
                "and compare against each fixture's expectation manifest. Nothing here has been "
                "validated against a real XNA 4.0 runtime; see tests/interop/xna40/README.md.");
    std::cout << "wrote " << fixtures.size() << " fixture(s) to "
              << root.generic_string() << "\n";

    if (argc == 3)
    {
        const std::filesystem::path lzxRoot(argv[2]);
        WriteCorpus(lzxRoot, compressed, "lzx",
                    "The same assets as the uncompressed corpus, written with CNA's own LZX "
                    "encoder (plans/plan_xnapipeline.md XNAP-81) -- the compression Microsoft XNA "
                    "4.0 itself produced, and the only compressed form an XNA 4.0 runtime loads. "
                    "The expectation manifests are identical to the uncompressed corpus's, "
                    "because compression is a container concern and must not change a single "
                    "observed value. Nothing here has been validated against a real XNA 4.0 "
                    "runtime; see tests/interop/xna40/README.md.");
        std::cout << "wrote " << compressed.size() << " LZX fixture(s) to "
                  << lzxRoot.generic_string() << "\n";
    }
    return 0;
}
