// SPDX-License-Identifier: MS-PL
//
// TQ-018: fuzz harness for the runtime .xnb reader -- the container (header, LZX and LZ4
// payloads), the type-reader manifest, the shared-resource table and every built-in content type
// reader a game reaches through ContentManager::Load<T>(). An .xnb is the one untrusted binary
// format every XNA game opens, so each candidate goes the whole way a game's load goes and the
// result is then walked, so an object holding a dangling or out-of-range view is caught rather
// than merely returned.
//
// One candidate runs in stages, each with its own documented refusal contract; anything else that
// leaves a stage escapes on purpose and ends the process, which is how the campaign records it:
//
//   1. CNA::Internal::Xnb::DecodeXnbContainerBody(): only ContentLoadException (its contract in
//      XnbContainerBody.hpp). A decompressor that lets a System::ArgumentOutOfRangeException or a
//      std:: exception out fails here, before stage 3 could mistake it for an XNA refusal.
//   2. ParseXnbTypeReaderTable() on a raw BinaryReader over the decoded body: ContentLoadException,
//      System::FormatException (an over-long 7-bit integer, .NET BinaryReader.Read7BitEncodedInt)
//      and System::IO::IOException (a raw reader has no ContentReader to wrap I/O; XNA's
//      ContentReader.ReadAsset wraps exactly IOException, IL_0019-002c). The table also names the
//      root object's reader, which picks the T of stage 3.
//   3. ContentManager::Load<T>() over the candidate, served by OpenStream() so no file is
//      touched: ContentLoadException; System::FormatException (XNA's ReadAsset catches IOException
//      only, so a FormatException escapes XNA's Load too); the System::ArgumentException family
//      (XNA's own graphics constructors and SetData refuse inconsistent content that way, e.g.
//      IndexBufferReader.Read IL_002a-0033 -> IndexBuffer..ctor);
//      System::NotSupportedException only for the graphics-profile refusal; and
//      System::InvalidOperationException for rejected Effect code (Refresh Effect::.ctor
//      IL_0009 -> CreateEffectFromCode IL_0208-020d / IL_0128-012d, Helpers.GetExceptionFromResult
//      default IL_0252-025c; EffectReader.Read IL_0016 has no catch). Every other
//      exception -- std::bad_alloc, std::out_of_range, std::length_error, std::bad_any_cast,
//      System::IO::IOException -- is a finding.
//   4. The loaded object is walked (bones, meshes, parts, buffers, effects, glyphs, keys, sizes).
//
// The mutator is structure-aware: it parses the candidate with its own tolerant reader (never the
// code under test -- a mutator that crashed would be attributed to the previous seed) and edits
// one field per round: header bytes, the total and decompressed lengths, LZX frame headers, the
// first bits of an LZX block, 7-bit counts and type ids (including over-long and negative
// encodings), reader names and versions, likely count and size fields of the object body,
// length-prefixed strings (external-reference paths among them), truncation, splices -- and it
// can re-wrap a mutated body as a valid LZX (uncompressed blocks) or LZ4 (one literal run)
// container, so body mutations also reach the readers through both decompressors.
//
// Shapes, matching the .x/FBX and intermediate-XML harnesses beside it:
//
//   * standalone (default): `cna_xnb_reader_fuzzer replay <file|dir>...` runs every file once;
//     `cna_xnb_reader_fuzzer mutate <dir> <iterations> [seed]` mutates the corpus
//     deterministically; the long campaign's seeded protocol (`--seeded`, `--emit`,
//     `--replay-one`) is tools/fuzz/seeded_harness.hpp;
//   * libFuzzer/AFL++ (`-DCNA_XNB_READER_FUZZER_ENTRY_POINT=ON`, clang): exports
//     LLVMFuzzerTestOneInput and lets the driver own main().
//
// Corpus: the .xnb fixtures named in tools/fuzz/campaign_targets.json (target `xnb-reader`).
#include "../fuzz/seeded_harness.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/DateTime.hpp"
#include "System/FormatException.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/IOException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/OverflowException.hpp"

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentManager;
    using Bytes = CNA::Tools::Fuzz::Bytes;

    constexpr const char* CandidateAsset = "candidate";

    /** @brief What the stages reached; every counter is cumulative over the process. */
    struct Statistics
    {
        std::uint64_t operations = 0, candidates = 0;
        std::uint64_t containersDecoded = 0, containerRefusals = 0;
        std::uint64_t uncompressedPayloads = 0, lzxPayloads = 0, lz4Payloads = 0;
        std::uint64_t tablesParsed = 0, tableRefusals = 0;
        std::uint64_t loads = 0, accepted = 0, refusedContent = 0, refusedFormat = 0;
        std::uint64_t refusedArgument = 0, refusedProfile = 0, refusedEffect = 0, walked = 0;
    } stats;

    // ---------------------------------------------------------------------------------------
    // The device, the reader registry and the per-candidate content manager
    // ---------------------------------------------------------------------------------------

    struct DeviceUnavailable : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    GraphicsDevice& SharedDevice()
    {
        static auto ownedDevice = [] {
            try
            {
                return std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                    GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(16, 16));
            }
            catch (const std::bad_alloc&) { throw; }
            catch (const std::exception& error) { throw DeviceUnavailable(error.what()); }
        }();
        return *ownedDevice;
    }

    void RegisterReadersOnce()
    {
        static const bool registered = [] {
            Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            return true;
        }();
        (void)registered;
    }

    /**
     * @brief A manager whose only asset is the candidate, served from memory through OpenStream().
     *
     * Every other name is missing, the way a content root holding one file answers: an external
     * reference to anything but the candidate is an ordinary refusal, and a reference back to the
     * candidate is the self-reference a hostile file can make. The root names no real directory,
     * so the Song and Video readers' media paths never resolve to a file on this machine.
     */
    class CandidateContentManager final : public ContentManager
    {
    public:
        CandidateContentManager(const Bytes& bytes, GraphicsDevice& device)
            : ContentManager(), bytes_(bytes)
        {
            setRootDirectoryProperty("/nonexistent/cna-xnb-reader-fuzzer");
            RouteLoadsThroughOpenStreamEXT(true);
            setGraphicsDevice(device);
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            if (assetName != CandidateAsset)
            {
                throw ContentLoadException("cna_xnb_reader_fuzzer: no asset '" + assetName + "'.");
            }
            return std::make_unique<System::IO::MemoryStream>(
                bytes_.data(), static_cast<std::int32_t>(bytes_.size()), false);
        }

    private:
        const Bytes& bytes_;
    };

    // ---------------------------------------------------------------------------------------
    // Walking what a load returned
    // ---------------------------------------------------------------------------------------

    void Touch(const double value)
    {
        static volatile double sink = 0.0;
        sink = sink + value;
        ++stats.operations;
    }

    /// Sums in double: the fields come from the file, and an Int32 sum of them can overflow.
    double Sum(std::initializer_list<double> values)
    {
        double total = 0.0;
        for (const double value : values) { total += value; }
        return total;
    }

    void Walk(const Texture2D& texture)
    {
        Touch(Sum({static_cast<double>(texture.getWidthProperty()), static_cast<double>(texture.getHeightProperty()),
                   static_cast<double>(texture.getLevelCountProperty()),
                   static_cast<double>(static_cast<int>(texture.getFormatProperty()))}));
    }

    void Walk(const std::shared_ptr<Texture3D>& texture)
    {
        if (!texture) { return; }
        Touch(Sum({static_cast<double>(texture->getWidthProperty()),
                   static_cast<double>(texture->getHeightProperty()),
                   static_cast<double>(texture->getDepthProperty()),
                   static_cast<double>(texture->getLevelCountProperty())}));
    }

    void Walk(const TextureCube& texture)
    {
        Touch(Sum({static_cast<double>(texture.getSizeProperty()),
                   static_cast<double>(texture.getLevelCountProperty())}));
    }

    void Walk(const Model& model)
    {
        const ModelBoneCollection& bones = model.getBonesProperty();
        std::vector<Matrix> absolute(static_cast<std::size_t>(bones.getCountProperty()));
        model.CopyAbsoluteBoneTransformsTo(absolute);
        for (int index = 0; index < bones.getCountProperty(); ++index)
        {
            const ModelBone* bone = bones[index];
            Touch(bone->getIndexProperty() + static_cast<double>(bone->getNameProperty().size()) +
                  bone->getChildrenProperty().getCountProperty() + absolute[static_cast<std::size_t>(index)].M44);
            if (const ModelBone* parent = bone->getParentProperty()) { Touch(parent->getIndexProperty()); }
        }
        if (const ModelBone* root = model.getRootProperty()) { Touch(root->getIndexProperty()); }
        const ModelMeshCollection& meshes = model.getMeshesProperty();
        for (int meshIndex = 0; meshIndex < meshes.getCountProperty(); ++meshIndex)
        {
            const ModelMesh* mesh = meshes[meshIndex];
            Touch(static_cast<double>(mesh->getNameProperty().size()) + mesh->getBoundingSphereProperty().Radius);
            if (const ModelBone* parent = mesh->getParentBoneProperty()) { Touch(parent->getIndexProperty()); }
            const ModelMeshPartCollection& parts = mesh->getMeshPartsProperty();
            for (int partIndex = 0; partIndex < parts.getCountProperty(); ++partIndex)
            {
                const ModelMeshPart* part = parts[partIndex];
                Touch(Sum({static_cast<double>(part->getVertexOffsetProperty()),
                           static_cast<double>(part->getNumVerticesProperty()),
                           static_cast<double>(part->getStartIndexProperty()),
                           static_cast<double>(part->getPrimitiveCountProperty())}));
                if (const VertexBuffer* vertices = part->getVertexBufferProperty())
                {
                    Touch(vertices->getVertexCountProperty());
                }
                if (const IndexBuffer* indices = part->getIndexBufferProperty())
                {
                    Touch(indices->getIndexCountProperty());
                }
                if (const Effect* effect = part->getEffectProperty())
                {
                    Touch(effect->getTechniquesProperty().getCountProperty());
                }
            }
        }
    }

    void Walk(const SpriteFont& font)
    {
        // A UTF-8 rendering of the font's own first characters; a character outside the font
        // without a default is XNA's ArgumentException from MeasureString, an ordinary refusal.
        std::string text;
        std::size_t taken = 0;
        for (const char16_t character : font.getCharactersProperty())
        {
            if (taken++ == 16) { break; }
            if (character >= 0xD800u && character <= 0xDFFFu) { continue; }
            if (character < 0x80u) { text += static_cast<char>(character); }
            else if (character < 0x800u)
            {
                text += static_cast<char>(0xC0u | (character >> 6));
                text += static_cast<char>(0x80u | (character & 0x3Fu));
            }
            else
            {
                text += static_cast<char>(0xE0u | (character >> 12));
                text += static_cast<char>(0x80u | ((character >> 6) & 0x3Fu));
                text += static_cast<char>(0x80u | (character & 0x3Fu));
            }
        }
        Touch(font.getLineSpacingProperty() + font.getSpacingProperty() +
              static_cast<double>(font.getCharactersProperty().size()));
        try
        {
            const Vector2 size = font.MeasureString(text);
            Touch(size.X + size.Y);
        }
        catch (const System::ArgumentException&)
        {
            ++stats.refusedArgument;
        }
    }

    void Walk(const std::shared_ptr<Effect>& effect)
    {
        if (effect) { Touch(effect->getTechniquesProperty().getCountProperty()); }
    }

    void Walk(const Audio::SoundEffect& sound)
    {
        Touch(static_cast<double>(sound.getDurationProperty().getTicksProperty()));
    }

    void Walk(const Media::Song& song) { Touch(static_cast<double>(song.getDurationProperty().getTicksProperty())); }

    void Walk(const Media::Video& video)
    {
        Touch(Sum({static_cast<double>(video.getWidthProperty()), static_cast<double>(video.getHeightProperty()),
                   static_cast<double>(video.getDurationProperty().getTicksProperty())}));
    }

    void Walk(const Curve& curve)
    {
        Touch(curve.getKeysProperty().getCountProperty() + curve.Evaluate(0.5f));
    }

    void Walk(const std::int32_t value) { Touch(value); }

    void Walk(const Vector3& value) { Touch(value.X + value.Y + value.Z); }

    void Walk(const System::DateTime& value)
    {
        Touch(static_cast<double>(value.getTicksProperty()) + static_cast<int>(value.getKindProperty()));
    }

    void Walk(const std::vector<std::string>& values)
    {
        for (const std::string& value : values) { Touch(static_cast<double>(value.size())); }
    }

    void Walk(const std::vector<Rectangle>& values)
    {
        for (const Rectangle& value : values)
        {
            Touch(Sum({static_cast<double>(value.X), static_cast<double>(value.Y), static_cast<double>(value.Width),
                       static_cast<double>(value.Height)}));
        }
    }

    template<typename Value>
    void Walk(const std::unordered_map<std::string, Value>& values)
    {
        for (const auto& [key, value] : values) { Touch(static_cast<double>(key.size()) + sizeof(value)); }
    }

    void Walk(const std::map<std::string, std::any>& values)
    {
        for (const auto& [key, value] : values) { Touch(static_cast<double>(key.size()) + value.has_value()); }
    }

    template<typename T>
    void LoadAndWalk(const Bytes& bytes)
    {
        CandidateContentManager manager(bytes, SharedDevice());
        ++stats.loads;
        ++stats.operations;
        bool loaded = false;
        try
        {
            const T asset = manager.Load<T>(CandidateAsset);
            loaded = true;
            ++stats.accepted;
            Walk(asset);
            ++stats.walked;
        }
        catch (const ContentLoadException&)
        {
            if (loaded) { throw; }
            ++stats.refusedContent;
        }
        catch (const System::FormatException&)
        {
            if (loaded) { throw; }
            ++stats.refusedFormat;
        }
        catch (const System::ArgumentException&)
        {
            if (loaded) { throw; }
            ++stats.refusedArgument;
        }
        catch (const System::OverflowException&)
        {
            // XNA's `newarr` of a negative count: ArrayReader`1.Read IL_0001-0008 and
            // VertexDeclarationReader.Read IL_000f-0010 (TQ-033). Counted with the argument refusals.
            if (loaded) { throw; }
            ++stats.refusedArgument;
        }
        catch (const System::NotSupportedException& refusal)
        {
            // The graphics profile refusing a declaration or texture it cannot express, as XNA's
            // own profile validation does; any other NotSupportedException is a finding.
            if (loaded || refusal.getMessageProperty().find("active graphics profile") == std::string::npos)
            {
                throw;
            }
            ++stats.refusedProfile;
        }
        catch (const System::InvalidOperationException&)
        {
            // EffectReader.Read IL_0016 passes the Effect constructor's invalid-code
            // refusal through Load; Model can reach it through a shared effect too.
            // Other root types and post-load walking still treat this as a finding.
            if (loaded || (!std::is_same_v<T, std::shared_ptr<Effect>> &&
                           !std::is_same_v<T, Model>))
                throw;
            ++stats.refusedEffect;
        }
        manager.Unload();
    }

    // The Load<T> each root reader's target is loaded as; any other root -- another value, a mutated
    // or unknown name -- loads as Dictionary<string, object>, which still runs the root's reader in
    // full and then refuses the type, as XNA's InvokeReader does (IL_0044-006d).
    void LoadByRootReader(const std::string& reader, const Bytes& bytes)
    {
        const std::string prefix = "Microsoft.Xna.Framework.Content.";
        const std::string name = reader.rfind(prefix, 0) == 0 ? reader.substr(prefix.size()) : std::string();
        if (name == "Texture2DReader") { return LoadAndWalk<Texture2D>(bytes); }
        if (name == "Texture3DReader") { return LoadAndWalk<std::shared_ptr<Texture3D>>(bytes); }
        if (name == "TextureCubeReader") { return LoadAndWalk<TextureCube>(bytes); }
        if (name == "SpriteFontReader") { return LoadAndWalk<SpriteFont>(bytes); }
        if (name == "ModelReader")
        {
            // Without a 3D pipeline every model is refused before its graph is read; such a
            // renderer still runs the container, table and dispatch stages above.
            if (!SharedDevice().SupportsCapability(CNA::GraphicsCapability::ThreeD)) { return; }
            return LoadAndWalk<Model>(bytes);
        }
        if (name == "EffectReader" || name == "BasicEffectReader" || name == "SkinnedEffectReader" ||
            name == "EnvironmentMapEffectReader" || name == "DualTextureEffectReader" ||
            name == "AlphaTestEffectReader" || name == "EffectMaterialReader")
        {
            return LoadAndWalk<std::shared_ptr<Effect>>(bytes);
        }
        if (name == "SoundEffectReader") { return LoadAndWalk<Audio::SoundEffect>(bytes); }
        if (name == "SongReader") { return LoadAndWalk<Media::Song>(bytes); }
        if (name == "VideoReader") { return LoadAndWalk<Media::Video>(bytes); }
        if (name == "CurveReader") { return LoadAndWalk<Curve>(bytes); }
        if (name == "Int32Reader") { return LoadAndWalk<std::int32_t>(bytes); }
        if (name == "Vector3Reader") { return LoadAndWalk<Vector3>(bytes); }
        if (name == "DateTimeReader") { return LoadAndWalk<System::DateTime>(bytes); }
        if (name == "ListReader`1[[System.String]]") { return LoadAndWalk<std::vector<std::string>>(bytes); }
        if (name == "ListReader`1[[Microsoft.Xna.Framework.Rectangle]]")
        {
            return LoadAndWalk<std::vector<Rectangle>>(bytes);
        }
        if (name == "DictionaryReader`2[[System.String],[System.Int32]]")
        {
            return LoadAndWalk<std::unordered_map<std::string, std::int32_t>>(bytes);
        }
        if (name == "DictionaryReader`2[[System.String],[System.String]]")
        {
            return LoadAndWalk<std::unordered_map<std::string, std::string>>(bytes);
        }
        LoadAndWalk<std::map<std::string, std::any>>(bytes);
    }

    /** @brief One candidate through the four stages. */
    void RunOne(const std::uint8_t* data, const std::size_t size)
    {
        RegisterReadersOnce();
        ++stats.candidates;
        ++stats.operations;
        const Bytes bytes(data, data + size);

        // Stage 1: the container. Only ContentLoadException is a refusal here.
        std::optional<CNA::Internal::Xnb::XnbContainerBody> container;
        try
        {
            container.emplace(CNA::Internal::Xnb::DecodeXnbContainerBody(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), "candidate.xnb"));
            ++stats.containersDecoded;
        }
        catch (const ContentLoadException&)
        {
            ++stats.containerRefusals;
        }

        std::string rootReader;
        if (container)
        {
            switch (container->header.compression)
            {
                case CNA::Internal::Xnb::XnbCompression::Lzx: ++stats.lzxPayloads; break;
                case CNA::Internal::Xnb::XnbCompression::Lz4: ++stats.lz4Payloads; break;
                default: ++stats.uncompressedPayloads; break;
            }

            // Stage 2: the manifest on a raw reader, and the root object's reader name.
            const auto body = container->Bytes();
            try
            {
                System::IO::MemoryStream stream(body.data(), static_cast<std::int32_t>(body.size()));
                System::IO::BinaryReader reader(&stream, true);
                const auto table = CNA::Internal::Xnb::ParseXnbTypeReaderTable(reader, "candidate.xnb");
                ++stats.tablesParsed;
                (void)reader.Read7BitEncodedInt(); // shared-resource count
                const std::int32_t root = reader.Read7BitEncodedInt();
                if (root > 0 && static_cast<std::size_t>(root) <= table.size())
                {
                    rootReader = table[static_cast<std::size_t>(root - 1)].normalizedName;
                }
            }
            catch (const ContentLoadException&) { ++stats.tableRefusals; }
            catch (const System::FormatException&) { ++stats.tableRefusals; }
            catch (const System::IO::IOException&) { ++stats.tableRefusals; }
        }

        // Stage 3 and 4: the game's own route, whatever the stages above concluded -- a container
        // they refused must be refused the same way by Load<T>().
        LoadByRootReader(rootReader, bytes);
    }

    // ---------------------------------------------------------------------------------------
    // The structure-aware mutator (never calls the code under test)
    // ---------------------------------------------------------------------------------------

    struct Random64
    {
        std::uint64_t state;
        std::uint64_t Next() { return CNA::Tools::Fuzz::NextMixed(state); }
        std::uint64_t Below(const std::uint64_t bound) { return bound == 0 ? 0 : Next() % bound; }
        bool OneIn(const std::uint64_t n) { return Below(n) == 0; }
    };

    struct Varint { std::size_t offset = 0, length = 0; std::uint32_t value = 0; };
    struct NamedReader { std::size_t prefixOffset = 0, nameOffset = 0, nameLength = 0, versionOffset = 0; };
    struct LzxFrame { std::size_t offset = 0, headerLength = 0, blockLength = 0; };

    /** @brief The harness's own tolerant view of a candidate's fields. */
    struct Layout
    {
        bool xnb = false, compressed = false, lzx = false, tableComplete = false;
        std::size_t bodyEnd = 0, objectStart = 0;
        std::optional<Varint> readerCount, sharedCount, rootId;
        std::vector<NamedReader> readers;
        std::vector<LzxFrame> frames;
    };

    std::optional<Varint> ReadVarint(const Bytes& bytes, std::size_t& position, const std::size_t end)
    {
        Varint result{position, 0, 0};
        for (int shift = 0; shift < 35; shift += 7)
        {
            if (position >= end) { return std::nullopt; }
            const std::uint8_t byte = bytes[position++];
            result.value |= static_cast<std::uint32_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0)
            {
                result.length = position - result.offset;
                return result;
            }
        }
        return std::nullopt;
    }

    std::uint32_t ReadU32(const Bytes& bytes, const std::size_t at)
    {
        return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    }

    void WriteU32(Bytes& bytes, const std::size_t at, const std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes[at + index] = static_cast<std::uint8_t>(value >> (8 * index));
        }
    }

    Bytes EncodeVarint(std::uint32_t value)
    {
        Bytes out;
        while (value >= 0x80u)
        {
            out.push_back(static_cast<std::uint8_t>(value | 0x80u));
            value >>= 7;
        }
        out.push_back(static_cast<std::uint8_t>(value));
        return out;
    }

    Layout ParseLayout(const Bytes& bytes)
    {
        Layout layout;
        if (bytes.size() < 10 || bytes[0] != 'X' || bytes[1] != 'N' || bytes[2] != 'B') { return layout; }
        layout.xnb = true;
        const std::uint8_t flags = bytes[5];
        layout.compressed = (flags & 0xC0u) != 0;
        layout.lzx = (flags & 0xC0u) == 0x80u;
        if (layout.lzx)
        {
            std::size_t position = 14;
            while (position + 2 <= bytes.size() && layout.frames.size() < 4096)
            {
                LzxFrame frame{position, 2, (static_cast<std::size_t>(bytes[position]) << 8) | bytes[position + 1]};
                if (bytes[position] == 0xFFu)
                {
                    if (position + 5 > bytes.size()) { break; }
                    frame.headerLength = 5;
                    frame.blockLength = (static_cast<std::size_t>(bytes[position + 3]) << 8) | bytes[position + 4];
                }
                if (frame.blockLength == 0) { break; }
                layout.frames.push_back(frame);
                position += frame.headerLength + frame.blockLength;
            }
            return layout;
        }
        if (layout.compressed) { return layout; }

        const auto total = static_cast<std::int32_t>(ReadU32(bytes, 6));
        layout.bodyEnd = total >= 10 && static_cast<std::size_t>(total) <= bytes.size()
            ? static_cast<std::size_t>(total) : bytes.size();
        std::size_t position = 10;
        layout.readerCount = ReadVarint(bytes, position, layout.bodyEnd);
        if (!layout.readerCount) { return layout; }
        for (std::uint32_t index = 0; index < layout.readerCount->value && index < 256; ++index)
        {
            NamedReader named{position};
            const auto length = ReadVarint(bytes, position, layout.bodyEnd);
            if (!length || length->value > layout.bodyEnd - position) { return layout; }
            named.nameOffset = position;
            named.nameLength = length->value;
            position += length->value;
            if (layout.bodyEnd - position < 4) { return layout; }
            named.versionOffset = position;
            position += 4;
            layout.readers.push_back(named);
        }
        layout.sharedCount = ReadVarint(bytes, position, layout.bodyEnd);
        if (!layout.sharedCount) { return layout; }
        layout.rootId = ReadVarint(bytes, position, layout.bodyEnd);
        if (!layout.rootId) { return layout; }
        layout.objectStart = position;
        layout.tableComplete = layout.readers.size() == layout.readerCount->value;
        return layout;
    }

    void Splice(Bytes& bytes, const std::size_t offset, const std::size_t length, const Bytes& replacement)
    {
        const std::size_t end = std::min(bytes.size(), offset + length);
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(std::min(offset, bytes.size())),
                    bytes.begin() + static_cast<std::ptrdiff_t>(end));
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(std::min(offset, bytes.size())),
                     replacement.begin(), replacement.end());
    }

    /** @brief Keeps the declared total length equal to the file size, as a well-formed file has it. */
    void FixTotalLength(Bytes& bytes)
    {
        if (bytes.size() >= 10 && bytes.size() <= 0x7FFFFFFFu)
        {
            WriteU32(bytes, 6, static_cast<std::uint32_t>(bytes.size()));
        }
    }

    std::uint32_t InterestingInt(Random64& random, const std::uint32_t current, const std::size_t remaining)
    {
        const std::array<std::uint32_t, 20> values{
            0u, 1u, 2u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0xFFFFu, 0x10000u, 0x7FFFu, 0x8000u,
            10'000'000u, 10'000'001u, 0x00FFFFFFu, 255u, 256u, 4096u, 16384u, 32768u, 65535u, 0x0FFFFFFFu};
        switch (random.Below(6))
        {
            case 0: return current + 1u;
            case 1: return current - 1u;
            case 2: return current * 2u;
            case 3: return static_cast<std::uint32_t>(remaining + random.Below(3));
            case 4: return static_cast<std::uint32_t>(remaining / 4u);
            default: return values[random.Below(values.size())];
        }
    }

    // The 7-bit spellings a hostile file can give a count or a type id.
    Bytes InterestingVarint(Random64& random, const std::uint32_t current, const std::uint32_t readerCount)
    {
        switch (random.Below(10))
        {
            case 0: return EncodeVarint(0);
            case 1: return EncodeVarint(current + 1u);
            case 2: return EncodeVarint(current - 1u);
            case 3: return EncodeVarint(readerCount + 1u);
            case 4: return EncodeVarint(0xFFFFFFFFu);                      // five bytes, -1
            case 5: return EncodeVarint(0x80000000u);                      // five bytes, int.MinValue
            case 6: return Bytes{0x81, 0x80, 0x80, 0x80, 0x00};            // over-long spelling of 1
            case 7: return Bytes{0x80, 0x80, 0x80, 0x80, 0x80, 0x00};      // sixth byte: FormatException
            case 8: return Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x7F};            // fifth byte above 0x0F
            default: return EncodeVarint(static_cast<std::uint32_t>(random.Next()));
        }
    }

    std::string InterestingReaderName(Random64& random)
    {
        static const std::array<const char*, 40> names{
            "Microsoft.Xna.Framework.Content.Texture2DReader", "Microsoft.Xna.Framework.Content.Texture3DReader",
            "Microsoft.Xna.Framework.Content.TextureCubeReader", "Microsoft.Xna.Framework.Content.TextureReader",
            "Microsoft.Xna.Framework.Content.SpriteFontReader", "Microsoft.Xna.Framework.Content.ModelReader",
            "Microsoft.Xna.Framework.Content.VertexBufferReader", "Microsoft.Xna.Framework.Content.IndexBufferReader",
            "Microsoft.Xna.Framework.Content.VertexDeclarationReader", "Microsoft.Xna.Framework.Content.EffectReader",
            "Microsoft.Xna.Framework.Content.BasicEffectReader",
            "Microsoft.Xna.Framework.Content.SkinnedEffectReader",
            "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
            "Microsoft.Xna.Framework.Content.DualTextureEffectReader",
            "Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
            "Microsoft.Xna.Framework.Content.EffectMaterialReader",
            "Microsoft.Xna.Framework.Content.ExternalReferenceReader",
            "Microsoft.Xna.Framework.Content.SoundEffectReader", "Microsoft.Xna.Framework.Content.SongReader",
            "Microsoft.Xna.Framework.Content.VideoReader", "Microsoft.Xna.Framework.Content.CurveReader",
            "Microsoft.Xna.Framework.Content.StringReader", "Microsoft.Xna.Framework.Content.CharReader",
            "Microsoft.Xna.Framework.Content.Int32Reader", "Microsoft.Xna.Framework.Content.DecimalReader",
            "Microsoft.Xna.Framework.Content.DateTimeReader", "Microsoft.Xna.Framework.Content.MatrixReader",
            "Microsoft.Xna.Framework.Content.BoundingFrustumReader",
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.String]]",
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]",
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]",
            "Microsoft.Xna.Framework.Content.ArrayReader`1[[Microsoft.Xna.Framework.Vector3]]",
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]",
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Int32]]",
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String, mscorlib],[System.String, mscorlib]]",
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32, mscorlib, Version=4.0.0.0]], "
            "Microsoft.Xna.Framework, Version=4.0.0.0",
            "Microsoft.Xna.Framework.Content.NoSuchReader", "", "[["};
        if (random.OneIn(8))
        {
            // Generic nesting around the type-name parser's depth limit (XnbReadLimits: 256).
            const std::size_t depth = 250 + random.Below(12);
            std::string nested = "Microsoft.Xna.Framework.Content.ListReader`1";
            for (std::size_t level = 0; level < depth; ++level) { nested += "[[L`1"; }
            for (std::size_t level = 0; level < depth; ++level) { nested += "]]"; }
            return nested;
        }
        return names[random.Below(names.size())];
    }

    std::string InterestingString(Random64& random)
    {
        static const std::array<const char*, 14> strings{
            "candidate", "./candidate", "sub/../candidate", "../candidate", "../../etc/passwd", "/etc/passwd",
            "C:\\Windows\\win.ini", "\\\\host\\share\\x", "a//b", "x/../../y", "", "\xFF\xFE", "\xC3\x28",
            "song.wma"};
        if (random.OneIn(8)) { return std::string(250 + random.Below(10), 'a'); }
        return strings[random.Below(strings.size())];
    }

    /** @brief The object region's likely count/size fields: small positive little-endian Int32s. */
    std::vector<std::size_t> CountCandidates(const Bytes& bytes, const std::size_t begin, const std::size_t end)
    {
        std::vector<std::size_t> found;
        for (std::size_t at = begin; at + 4 <= end && found.size() < 512; ++at)
        {
            const std::uint32_t value = ReadU32(bytes, at);
            if (value != 0 && value <= 0x100000u) { found.push_back(at); }
        }
        return found;
    }

    /** @brief The object region's likely 7-bit-prefixed strings (printable, at least 2 bytes). */
    std::vector<std::size_t> StringCandidates(const Bytes& bytes, const std::size_t begin, const std::size_t end)
    {
        std::vector<std::size_t> found;
        for (std::size_t at = begin; at + 1 < end && found.size() < 256; ++at)
        {
            const std::size_t length = bytes[at];
            if (length < 2 || length >= 0x80u || at + 1 + length > end) { continue; }
            const bool printable = std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(at + 1),
                bytes.begin() + static_cast<std::ptrdiff_t>(at + 1 + length),
                [](const std::uint8_t c) { return c >= 0x20u && c < 0x7Fu; });
            if (printable) { found.push_back(at); }
        }
        return found;
    }

    /** @brief A body as one uncompressed LZX block in 32 KiB frames (the shape the decoder takes). */
    Bytes WrapLzxUncompressed(const Bytes& body)
    {
        // Bit order of LzxDecoder's BitBuffer: MSB first within 16-bit little-endian words.
        const std::uint32_t length = static_cast<std::uint32_t>(body.size());
        const std::uint64_t bits = (0ull << 27) | (3ull << 24) | ((length >> 8) & 0xFFFFu) << 8 | (length & 0xFFu);
        const std::uint32_t words = static_cast<std::uint32_t>(bits << 4); // 28 bits, MSB-aligned in 32
        Bytes head{static_cast<std::uint8_t>(words >> 16), static_cast<std::uint8_t>(words >> 24),
                   static_cast<std::uint8_t>(words), static_cast<std::uint8_t>(words >> 8)};
        for (int reg = 0; reg < 3; ++reg) { head.insert(head.end(), {1, 0, 0, 0}); }
        Bytes payload;
        for (std::size_t offset = 0; offset < body.size();)
        {
            const std::size_t chunk = std::min<std::size_t>(0x8000u, body.size() - offset);
            Bytes block = offset == 0 ? head : Bytes{};
            block.insert(block.end(), body.begin() + static_cast<std::ptrdiff_t>(offset),
                         body.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
            if (chunk == 0x8000u)
            {
                payload.push_back(static_cast<std::uint8_t>(block.size() >> 8));
                payload.push_back(static_cast<std::uint8_t>(block.size()));
            }
            else
            {
                payload.insert(payload.end(), {0xFF, static_cast<std::uint8_t>(chunk >> 8),
                    static_cast<std::uint8_t>(chunk), static_cast<std::uint8_t>(block.size() >> 8),
                    static_cast<std::uint8_t>(block.size())});
            }
            payload.insert(payload.end(), block.begin(), block.end());
            offset += chunk;
        }
        return payload;
    }

    /** @brief A body as one raw LZ4 block of a single literal run (MonoGame's XNB LZ4 shape). */
    Bytes WrapLz4Literals(const Bytes& body)
    {
        Bytes payload;
        if (body.size() < 15)
        {
            payload.push_back(static_cast<std::uint8_t>(body.size() << 4));
        }
        else
        {
            payload.push_back(0xF0);
            std::size_t rest = body.size() - 15;
            for (; rest >= 255; rest -= 255) { payload.push_back(255); }
            payload.push_back(static_cast<std::uint8_t>(rest));
        }
        payload.insert(payload.end(), body.begin(), body.end());
        return payload;
    }

    /** @brief Re-wraps an uncompressed candidate's body as an LZX or LZ4 container. */
    Bytes Rewrap(const Bytes& bytes, const Layout& layout, Random64& random)
    {
        if (!layout.xnb || layout.compressed || layout.bodyEnd <= 10 || layout.bodyEnd - 10 >= 0x00FFFFFFu)
        {
            return bytes;
        }
        const Bytes body(bytes.begin() + 10, bytes.begin() + static_cast<std::ptrdiff_t>(layout.bodyEnd));
        const bool lz4 = random.OneIn(2);
        const Bytes payload = lz4 ? WrapLz4Literals(body) : WrapLzxUncompressed(body);
        Bytes out(bytes.begin(), bytes.begin() + 10);
        out[5] = static_cast<std::uint8_t>((out[5] & 0x3Fu) | (lz4 ? 0x40u : 0x80u));
        const std::uint32_t decompressed = static_cast<std::uint32_t>(body.size());
        out.insert(out.end(), {static_cast<std::uint8_t>(decompressed), static_cast<std::uint8_t>(decompressed >> 8),
                               static_cast<std::uint8_t>(decompressed >> 16),
                               static_cast<std::uint8_t>(decompressed >> 24)});
        out.insert(out.end(), payload.begin(), payload.end());
        FixTotalLength(out);
        return out;
    }

    /** @brief One structure-aware edit; up to four are stacked per candidate. */
    Bytes Mutate(Bytes bytes, Random64& random)
    {
        const Layout layout = ParseLayout(bytes);
        if (!layout.xnb || bytes.size() < 10 || random.OneIn(32))
        {
            if (bytes.empty()) { return bytes; }
            bytes[random.Below(bytes.size())] ^= static_cast<std::uint8_t>(1u << random.Below(8));
            return bytes;
        }
        bool fixLength = !random.OneIn(8);
        switch (random.Below(14))
        {
            case 0: // header bytes
            {
                static const std::array<std::uint8_t, 8> platforms{'w', 'x', 'm', 'd', 'i', 'g', 'W', 'z'};
                static const std::array<std::uint8_t, 6> versions{5, 4, 6, 0, 3, 255};
                static const std::array<std::uint8_t, 7> flags{0x00, 0x01, 0x40, 0x80, 0xC0, 0x81, 0x7F};
                switch (random.Below(3))
                {
                    case 0: bytes[3] = platforms[random.Below(platforms.size())]; break;
                    case 1: bytes[4] = versions[random.Below(versions.size())]; break;
                    default: bytes[5] = flags[random.Below(flags.size())]; break;
                }
                return bytes;
            }
            case 1: // the declared total length
            {
                static const std::array<std::uint32_t, 8> totals{
                    0u, 9u, 10u, 13u, 14u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u};
                const std::uint32_t current = ReadU32(bytes, 6);
                const std::uint32_t value = random.OneIn(2) ? totals[random.Below(totals.size())]
                    : current + static_cast<std::uint32_t>(random.Below(5)) - 2u;
                WriteU32(bytes, 6, value);
                return bytes;
            }
            case 2: // the decompressed size of a compressed container
            {
                if (!layout.compressed || bytes.size() < 14) { break; }
                const std::uint32_t current = ReadU32(bytes, 10);
                WriteU32(bytes, 10, InterestingInt(random, current, 256u * 1024u * 1024u));
                return bytes;
            }
            case 3: // an LZX frame header: block and frame sizes
            {
                if (layout.frames.empty()) { break; }
                const LzxFrame& frame = layout.frames[random.Below(layout.frames.size())];
                static const std::array<std::uint16_t, 9> sizes{
                    0u, 1u, 2u, 0x7FFFu, 0x8000u, 0x8001u, 0xFEFFu, 0xFFFFu, 0x10u};
                const std::uint16_t value = random.OneIn(3)
                    ? static_cast<std::uint16_t>(frame.blockLength + random.Below(3) - 1u)
                    : sizes[random.Below(sizes.size())];
                if (frame.headerLength == 5 && random.OneIn(2))
                {
                    bytes[frame.offset + 1] = static_cast<std::uint8_t>(value >> 8);
                    bytes[frame.offset + 2] = static_cast<std::uint8_t>(value);
                }
                else
                {
                    const std::size_t at = frame.offset + (frame.headerLength == 5 ? 3 : 0);
                    bytes[at] = static_cast<std::uint8_t>(value >> 8);
                    bytes[at + 1] = static_cast<std::uint8_t>(value);
                }
                return bytes;
            }
            case 4: // the first bits of an LZX block: Intel flag, block type and length, pretree
            {
                if (layout.frames.empty()) { break; }
                const LzxFrame& frame = layout.frames[random.Below(layout.frames.size())];
                const std::size_t begin = frame.offset + frame.headerLength;
                const std::size_t span = std::min<std::size_t>(16, bytes.size() > begin ? bytes.size() - begin : 0);
                if (span == 0) { break; }
                bytes[begin + random.Below(span)] ^= static_cast<std::uint8_t>(1u << random.Below(8));
                return bytes;
            }
            case 5: // a 7-bit count or type id of the manifest
            {
                std::vector<Varint> fields;
                for (const auto& field : {layout.readerCount, layout.sharedCount, layout.rootId})
                {
                    if (field) { fields.push_back(*field); }
                }
                if (fields.empty()) { break; }
                const Varint field = fields[random.Below(fields.size())];
                const std::uint32_t readers = layout.readerCount ? layout.readerCount->value : 0u;
                Splice(bytes, field.offset, field.length, InterestingVarint(random, field.value, readers));
                if (fixLength) { FixTotalLength(bytes); }
                return bytes;
            }
            case 6: // a reader name
            {
                if (layout.readers.empty()) { break; }
                const NamedReader& named = layout.readers[random.Below(layout.readers.size())];
                const std::string name = InterestingReaderName(random);
                Bytes replacement = EncodeVarint(static_cast<std::uint32_t>(name.size()));
                replacement.insert(replacement.end(), name.begin(), name.end());
                Splice(bytes, named.prefixOffset,
                       named.nameOffset + named.nameLength - named.prefixOffset, replacement);
                if (fixLength) { FixTotalLength(bytes); }
                return bytes;
            }
            case 7: // a reader version
            {
                if (layout.readers.empty()) { break; }
                const NamedReader& named = layout.readers[random.Below(layout.readers.size())];
                static const std::array<std::uint32_t, 4> versions{0u, 1u, 0xFFFFFFFFu, 0x7FFFFFFFu};
                WriteU32(bytes, named.versionOffset, versions[random.Below(versions.size())]);
                return bytes;
            }
            case 8: // a likely count or size field of the object body
            {
                if (!layout.tableComplete || layout.objectStart >= layout.bodyEnd) { break; }
                const auto candidates = CountCandidates(bytes, layout.objectStart, layout.bodyEnd);
                if (candidates.empty()) { break; }
                const std::size_t at = candidates[random.Below(candidates.size())];
                WriteU32(bytes, at, InterestingInt(random, ReadU32(bytes, at), layout.bodyEnd - at));
                return bytes;
            }
            case 9: // a type id inside the object body
            {
                if (!layout.tableComplete || layout.objectStart >= layout.bodyEnd || !layout.readerCount) { break; }
                const std::uint32_t readers = layout.readerCount->value;
                std::vector<std::size_t> ids;
                for (std::size_t at = layout.objectStart; at < layout.bodyEnd && ids.size() < 512; ++at)
                {
                    if (bytes[at] >= 1u && bytes[at] <= readers) { ids.push_back(at); }
                }
                if (ids.empty()) { break; }
                const std::size_t at = ids[random.Below(ids.size())];
                const std::array<std::uint8_t, 4> values{0u, static_cast<std::uint8_t>(readers + 1u),
                    static_cast<std::uint8_t>(1u + random.Below(std::max<std::uint32_t>(readers, 1u))), 0x80u};
                bytes[at] = values[random.Below(values.size())];
                return bytes;
            }
            case 10: // a length-prefixed string of the object body (external references among them)
            {
                if (!layout.tableComplete || layout.objectStart >= layout.bodyEnd) { break; }
                const auto strings = StringCandidates(bytes, layout.objectStart, layout.bodyEnd);
                if (strings.empty()) { break; }
                const std::size_t at = strings[random.Below(strings.size())];
                if (random.OneIn(3))
                {
                    bytes[at] = static_cast<std::uint8_t>(random.OneIn(2) ? bytes[at] + 1u : 0x7Fu);
                    return bytes;
                }
                const std::string text = InterestingString(random);
                Bytes replacement = EncodeVarint(static_cast<std::uint32_t>(text.size()));
                replacement.insert(replacement.end(), text.begin(), text.end());
                Splice(bytes, at, 1u + bytes[at], replacement);
                if (fixLength) { FixTotalLength(bytes); }
                return bytes;
            }
            case 11: // truncate at a field boundary or anywhere
            {
                std::vector<std::size_t> boundaries{10, 14};
                for (const NamedReader& named : layout.readers) { boundaries.push_back(named.versionOffset); }
                if (layout.objectStart != 0) { boundaries.push_back(layout.objectStart); }
                std::size_t cut = random.OneIn(2) ? boundaries[random.Below(boundaries.size())]
                                                  : 1u + random.Below(bytes.size());
                cut = std::min(cut, bytes.size());
                bytes.resize(cut);
                if (fixLength && !random.OneIn(2)) { FixTotalLength(bytes); }
                return bytes;
            }
            case 12: // delete or duplicate a slice of the body
            {
                if (bytes.size() <= 11) { break; }
                const std::size_t begin = 10 + random.Below(bytes.size() - 10);
                const std::size_t length = std::min<std::size_t>(1 + random.Below(64), bytes.size() - begin);
                if (random.OneIn(2))
                {
                    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                                bytes.begin() + static_cast<std::ptrdiff_t>(begin + length));
                }
                else
                {
                    const Bytes slice(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(begin + length));
                    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(10 + random.Below(bytes.size() - 9)),
                                 slice.begin(), slice.end());
                }
                if (fixLength) { FixTotalLength(bytes); }
                return bytes;
            }
            default: // re-wrap the (possibly already mutated) body as LZX or LZ4
                return Rewrap(bytes, layout, random);
        }
        bytes[random.Below(bytes.size())] = static_cast<std::uint8_t>(random.Next());
        return bytes;
    }

    // ---------------------------------------------------------------------------------------
    // Standalone shapes
    // ---------------------------------------------------------------------------------------

    std::vector<std::uint8_t> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

    std::vector<std::filesystem::path> Gather(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        if (std::filesystem::is_directory(root, error))
        {
            for (const auto& entry : std::filesystem::directory_iterator(root, error))
            {
                if (error) { break; }
                if (entry.is_regular_file(error) && entry.path().extension() == ".xnb")
                {
                    files.push_back(entry.path());
                }
            }
        }
        else if (std::filesystem::is_regular_file(root, error))
        {
            files.push_back(root);
        }
        std::sort(files.begin(), files.end());
        return files;
    }
}

#ifdef CNA_XNB_READER_FUZZER_ENTRY_POINT
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    RunOne(data, size);
    return 0;
}
#else
int main(int argc, char** argv)
{
    const CNA::Tools::Fuzz::SeededHarness seeded{
        "cna_xnb_reader_fuzzer",
        [] {
            try
            {
                RegisterReadersOnce();
                GraphicsDevice& device = SharedDevice();
                const bool threeD = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
                return "xnb runtime readers on " + std::string(device.GetGraphicsRendererName()) +
                       (threeD ? " with ThreeD" : " without ThreeD");
            }
            catch (const DeviceUnavailable& error) { throw CNA::Tools::Fuzz::CapabilityUnavailable(error.what()); }
        },
        [](CNA::Tools::Fuzz::Bytes bytes, const std::uint64_t value) {
            Random64 random{value};
            return Mutate(std::move(bytes), random);
        },
        [](const CNA::Tools::Fuzz::Bytes& bytes) { RunOne(bytes.data(), bytes.size()); },
        [] {
            return CNA::Tools::Fuzz::Counters{{"operations", stats.operations}, {"candidates", stats.candidates},
                {"containers_decoded", stats.containersDecoded}, {"container_refusals", stats.containerRefusals},
                {"uncompressed_payloads", stats.uncompressedPayloads}, {"lzx_payloads", stats.lzxPayloads},
                {"lz4_payloads", stats.lz4Payloads}, {"tables_parsed", stats.tablesParsed},
                {"table_refusals", stats.tableRefusals}, {"loads", stats.loads}, {"accepted", stats.accepted},
                {"refused_content", stats.refusedContent}, {"refused_format", stats.refusedFormat},
                {"refused_argument", stats.refusedArgument}, {"refused_profile", stats.refusedProfile},
                {"refused_effect", stats.refusedEffect},
                {"walked", stats.walked}};
        },
        // Each Mutate call is a single structural edit, so stack up to four.
        4u};
    if (const int handled = CNA::Tools::Fuzz::SeededMain(argc, argv, seeded); handled >= 0) { return handled; }

    if (argc >= 3 && std::strcmp(argv[1], "replay") == 0)
    {
        std::size_t count = 0;
        for (int i = 2; i < argc; ++i)
        {
            for (const auto& file : Gather(argv[i]))
            {
                const auto bytes = Read(file);
                RunOne(bytes.data(), bytes.size());
                ++count;
            }
        }
        std::printf("cna_xnb_reader_fuzzer: replayed %zu file(s), %llu accepted\n", count,
                    static_cast<unsigned long long>(stats.accepted));
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "mutate") == 0)
    {
        const auto corpus = Gather(argv[2]);
        if (corpus.empty())
        {
            std::fprintf(stderr, "cna_xnb_reader_fuzzer: no .xnb files under %s\n", argv[2]);
            return 2;
        }
        const long iterations = std::strtol(argv[3], nullptr, 10);
        Random64 random{argc >= 5 ? std::strtoull(argv[4], nullptr, 10) : 20261103ull};
        for (const auto& file : corpus)
        {
            const auto original = Read(file);
            for (long i = 0; i < iterations; ++i)
            {
                const auto mutated = Mutate(original, random);
                RunOne(mutated.data(), mutated.size());
            }
        }
        std::printf("cna_xnb_reader_fuzzer: %zu file(s) x %ld mutation(s)\n", corpus.size(), iterations);
        return 0;
    }
    std::fprintf(stderr,
                 "Usage: cna_xnb_reader_fuzzer replay <file|dir>...\n"
                 "       cna_xnb_reader_fuzzer mutate <dir> <iterations> [seed]\n"
                 "       cna_xnb_reader_fuzzer --seeded <first-seed> <count> <corpus>...\n"
                 "       cna_xnb_reader_fuzzer --emit <seed> <output-file> <corpus>...\n"
                 "       cna_xnb_reader_fuzzer --replay-one <input-file>\n");
    return 2;
}
#endif
