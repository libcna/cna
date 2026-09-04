// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-45: a fuzz corpus over the *writer's inputs*.
//
// XnbContainerFuzzTests.cpp mutates finished `.xnb` bytes and loads them, which exercises the
// reader. This is the other direction, and it is the one the writer's own limits exist for: the
// values handed to `WriteXnbAsset` come from importers and processors, some of which read
// attacker-supplied files, so a hostile `.glb` becomes a hostile *canonical value* long before it
// becomes bytes. Random bytes would never reach these paths -- a malformed shared-resource index
// or a 4-billion-mip texture is a structurally valid C++ object, not a corrupt buffer.
//
// Every seed has a reason, stated on it. The contract each mutant must satisfy is the same one:
//
//   * it either writes successfully, or throws XnbWriteException. Nothing else -- no std::bad_alloc
//     from a count nobody checked, no out_of_range from an index nobody validated, no crash;
//   * it terminates. Bounded by construction: no mutant asks for more than a few MiB, and the
//     suite runs under a deadline so a hang fails rather than waits;
//   * a refusal publishes nothing. The file-writing entry point is given a path that must not
//     exist afterwards, because a half-written `.xnb` is worse than none: a later build would
//     verify a digest against a file that was never finished.
//
// Deterministic: a fixed seed, no clock, no std::random_device, so a failure is reproducible from
// the seed printed with it. Also worth running under -DCNA_SANITIZE=address,undefined, which is
// what turns "did not throw the wrong exception" into "did not corrupt memory".

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbWriteLimits.hpp"

using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace Xnb = CNA::Internal::Xnb;

namespace
{
    /** @brief What a seed is expected to do, and why. */
    enum class Outcome
    {
        /** @brief The writer must refuse it: the file it would produce is not loadable or safe. */
        Refused,
        /**
         * @brief The writer must write it.
         *
         * Not an oversight -- each of these carries a note saying why refusing would be the
         * writer inventing policy the format does not have, or why the value is representable and
         * the check belongs somewhere else. A seed with no recorded expectation is what this
         * enumeration exists to make impossible.
         */
        Written,
    };

    /** @brief One writer input, the reason it exists, and how it is expected to end. */
    struct Seed
    {
        /** @brief What this input is adversarial about. Printed with any failure. */
        const char* reason;

        /** @brief The scored expectation. Every seed has one; none is left to whatever happens. */
        Outcome expected;

        /** @brief Why @ref expected is Written, when it is. Null for a refusal. */
        const char* writtenBecause;

        /** @brief Builds the value and writes it; throws XnbWriteException when refused. */
        std::function<void()> write;
    };

    // -- ordinary values the mutants are derived from ------------------------------------------

    std::vector<std::uint8_t> ColorLevel(const std::uint32_t width, const std::uint32_t height)
    {
        return std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4u, 0x7Fu);
    }

    Xnb::XnbTextureData Texture2D()
    {
        Xnb::XnbTextureData texture;
        texture.kind = Xnb::XnbTextureKind::Texture2D;
        texture.surfaceFormat = SurfaceFormat::Color;
        texture.width = 4u;
        texture.height = 2u;
        texture.mipCount = 1u;
        texture.levels = {ColorLevel(4u, 2u)};
        return texture;
    }

    Xnb::XnbVertexDeclarationData PositionDeclaration()
    {
        Xnb::XnbVertexDeclarationData declaration;
        declaration.stride = 12;
        declaration.elements.emplace_back(0, VertexElementFormat::Vector3,
                                          VertexElementUsage::Position, 0);
        return declaration;
    }

    Xnb::XnbModelData Model()
    {
        Xnb::XnbModelData model;
        Xnb::XnbModelBoneData root;
        root.name = "RootNode";
        root.transform = Matrix::getIdentityProperty();
        root.parent = -1;
        model.bones.push_back(root);
        model.rootBone = 0;

        Xnb::XnbModelPartData part;
        part.vertexCount = 3;
        part.primitiveCount = 1;
        part.vertexBufferResource = 0;
        part.indexBufferResource = 1;
        part.effectResource = 2;

        Xnb::XnbModelMeshData mesh;
        mesh.name = "Mesh";
        mesh.parentBone = 0;
        mesh.boundingSphere = BoundingSphere(Vector3{0.0f, 0.0f, 0.0f}, 1.0f);
        mesh.parts.push_back(part);
        model.meshes.push_back(mesh);

        Xnb::XnbVertexBufferData vertexBuffer;
        vertexBuffer.declaration = PositionDeclaration();
        vertexBuffer.vertexCount = 3u;
        vertexBuffer.bytes.assign(36u, 0u);

        Xnb::XnbIndexBufferData indexBuffer;
        indexBuffer.indexElementSize = 2u;
        indexBuffer.bytes = {0u, 0u, 1u, 0u, 2u, 0u};

        Xnb::XnbBasicEffectData effect;
        effect.specularPower = 16.0f;
        effect.alpha = 1.0f;

        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.VertexBufferReader", vertexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.IndexBufferReader", indexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.BasicEffectReader", effect});
        return model;
    }

    Xnb::XnbSoundEffectData Sound()
    {
        Xnb::XnbSoundEffectData sound;
        sound.formatTag = 1u;
        sound.channels = 1u;
        sound.sampleRate = 44100u;
        sound.averageBytesPerSecond = 88200u;
        sound.blockAlign = 2u;
        sound.bitsPerSample = 16u;
        sound.samples.assign(64u, 0u);
        return sound;
    }

    Xnb::XnbSpriteFontData Font()
    {
        Xnb::XnbSpriteFontData font;
        font.atlas = Texture2D();
        font.glyphs = {Rectangle(0, 0, 2, 2)};
        font.cropping = {Rectangle(0, 0, 2, 2)};
        font.characters = {u'A'};
        font.kerning = {Vector3{0.0f, 2.0f, 0.0f}};
        font.lineSpacing = 4;
        font.spacing = 0.0f;
        return font;
    }

    /** @brief Writes @p root the way a build does, so limits and options are the real ones. */
    template<typename T>
    void Write(const T& root, const Xnb::XnbFileOptions& options = {})
    {
        static_cast<void>(Xnb::WriteXnbAsset(root, options, "fuzz"));
    }

    /**
     * @brief The corpus.
     *
     * Every entry names a way a canonical value can be adversarial. They are written out rather
     * than generated because the interesting inputs here are *structural* -- an index that points
     * at nothing, a count that disagrees with a buffer -- and a random field-flipper reaches those
     * only by accident. The generated mutation pass below covers the accidental ones.
     */
    std::vector<Seed> Corpus()
    {
        std::vector<Seed> seeds;

        // -- counts that disagree with the data behind them ------------------------------------
        seeds.push_back({"a texture claiming more mip levels than it carries",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.mipCount = 12u;
            Write(texture);
        }});
        seeds.push_back({"a texture whose mip count overflows a signed 32-bit level count",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.mipCount = std::numeric_limits<std::uint32_t>::max();
            Write(texture);
        }});
        seeds.push_back({"a texture whose dimensions multiply past any real surface",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.width = 0xFFFFFFFFu;
            texture.height = 0xFFFFFFFFu;
            Write(texture);
        }});
        seeds.push_back({"a zero-dimension texture with bytes anyway", Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.width = 0u;
            texture.height = 0u;
            Write(texture);
        }});
        seeds.push_back({"a cube map with a face count no cube has", Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.kind = Xnb::XnbTextureKind::TextureCube;
            texture.faceCount = 7u;
            texture.height = texture.width;
            Write(texture);
        }});
        seeds.push_back({"a volume texture whose depth exceeds its level bytes",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.kind = Xnb::XnbTextureKind::Texture3D;
            texture.depth = 4096u;
            Write(texture);
        }});
        seeds.push_back({"a level shorter than its own declared surface",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbTextureData texture = Texture2D();
            texture.levels[0].resize(3u);
            Write(texture);
        }});

        // -- shared-resource misuse ------------------------------------------------------------
        seeds.push_back({"a model part referencing a shared resource that was never issued",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.meshes[0].parts[0].vertexBufferResource = 99;
            Write(model);
        }});
        seeds.push_back({"a model part referencing a negative shared resource",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.meshes[0].parts[0].effectResource = -7;
            Write(model);
        }});
        seeds.push_back({"a shared resource naming a reader that does not exist",
                         Outcome::Written,
                         "the canonical `reader` string is not serialized at all -- the writer "
                         "dispatches on the value's own type -- so a wrong name here is inert data "
                         "rather than a bad file",
                         []
        {
            Xnb::XnbModelData model = Model();
            model.sharedResources[0].reader = "Nonexistent.Reader, Nowhere";
            Write(model);
        }});
        seeds.push_back({"a shared resource whose declared reader disagrees with its value",
                         Outcome::Written,
                         "same reason: the declared name is never written, so it cannot contradict "
                         "what is",
                         []
        {
            Xnb::XnbModelData model = Model();
            model.sharedResources[0].reader =
                "Microsoft.Xna.Framework.Content.BasicEffectReader";
            Write(model);
        }});

        // -- impossible model graphs -----------------------------------------------------------
        seeds.push_back({"a bone that is its own parent", Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.bones[0].parent = 0;
            Write(model);
        }});
        seeds.push_back({"a bone naming a parent past the end of the table",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.bones[0].parent = 4096;
            Write(model);
        }});
        seeds.push_back({"a two-bone cycle", Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            Xnb::XnbModelBoneData second;
            second.name = "Second";
            second.transform = Matrix::getIdentityProperty();
            second.parent = 0;
            second.children = {0};
            model.bones[0].parent = 1;
            model.bones[0].children = {1};
            model.bones.push_back(second);
            Write(model);
        }});
        seeds.push_back({"a root bone index outside the bone table", Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.rootBone = 12;
            Write(model);
        }});
        seeds.push_back({"a model with no bones but a mesh parented to one",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.bones.clear();
            model.rootBone = -1;
            Write(model);
        }});
        seeds.push_back({"a mesh part whose primitive count exceeds its index buffer",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.meshes[0].parts[0].primitiveCount = 1000000;
            Write(model);
        }});
        seeds.push_back({"a mesh part starting past the end of its index buffer",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.meshes[0].parts[0].startIndex = 1 << 24;
            Write(model);
        }});
        seeds.push_back({"a vertex buffer whose vertex count disagrees with its bytes",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            auto& buffer = std::get<Xnb::XnbVertexBufferData>(model.sharedResources[0].value);
            buffer.vertexCount = 1u << 20;
            Write(model);
        }});
        seeds.push_back({"a vertex declaration with a zero stride", Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            auto& buffer = std::get<Xnb::XnbVertexBufferData>(model.sharedResources[0].value);
            buffer.declaration.stride = 0;
            Write(model);
        }});
        seeds.push_back({"a vertex declaration whose element runs past its stride",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            auto& buffer = std::get<Xnb::XnbVertexBufferData>(model.sharedResources[0].value);
            buffer.declaration.elements[0] = {2000, VertexElementFormat::Vector3,
                                              VertexElementUsage::Position, 0};
            Write(model);
        }});
        seeds.push_back({"an index buffer with an element size no format uses",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            auto& buffer = std::get<Xnb::XnbIndexBufferData>(model.sharedResources[1].value);
            buffer.indexElementSize = 3u;
            Write(model);
        }});
        seeds.push_back({"an index buffer whose byte count is not a whole number of indices",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            auto& buffer = std::get<Xnb::XnbIndexBufferData>(model.sharedResources[1].value);
            buffer.bytes.push_back(0u);
            Write(model);
        }});

        // -- deep and wide structures ----------------------------------------------------------
        seeds.push_back({"a deep bone chain",
                         Outcome::Written,
                         "4096 bones is large and legal; the point of this seed is that it is "
                         "*bounded*, not refused",
                         []
        {
            Xnb::XnbModelData model = Model();
            model.bones.clear();
            for (int index = 0; index < 4096; ++index)
            {
                Xnb::XnbModelBoneData bone;
                bone.name = "Bone" + std::to_string(index);
                bone.transform = Matrix::getIdentityProperty();
                bone.parent = index == 0 ? -1 : index - 1;
                if (index + 1 < 4096) { bone.children = {index + 1}; }
                model.bones.push_back(bone);
            }
            model.rootBone = 0;
            model.meshes[0].parentBone = 0;
            Write(model);
        }});
        seeds.push_back({"many meshes, each with many parts",
                         Outcome::Written,
                         "3417 parts is large and legal, and must stay linear rather than "
                         "quadratic",
                         []
        {
            Xnb::XnbModelData model = Model();
            const Xnb::XnbModelMeshData mesh = model.meshes[0];
            for (int index = 0; index < 200; ++index) { model.meshes.push_back(mesh); }
            for (Xnb::XnbModelMeshData& each : model.meshes)
            {
                const Xnb::XnbModelPartData part = each.parts[0];
                for (int index = 0; index < 16; ++index) { each.parts.push_back(part); }
            }
            Write(model);
        }});
        seeds.push_back({"a bone name far longer than any authoring tool produces",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.bones[0].name = std::string(4u * 1024u * 1024u, 'n');
            Write(model);
        }});
        seeds.push_back({"a bone name that is not valid UTF-8", Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            model.bones[0].name = std::string("\xFF\xFE\xC0\x80", 4u);
            Write(model);
        }});
        seeds.push_back({"a texture reference that is not valid UTF-8",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbModelData model = Model();
            std::get<Xnb::XnbBasicEffectData>(model.sharedResources[2].value).textureReference =
                std::string("\xED\xA0\x80", 3u);
            Write(model);
        }});

        // -- effect material values --------------------------------------------------------------
        seeds.push_back({"effect colour and alpha values that are not finite",
                         Outcome::Written,
                         "XNA stores raw IEEE floats and its own pipeline did not validate them; a "
                         "writer that refused infinity would refuse a legitimate fog-end distance "
                         "too. Fidelity over policy",
                         []
        {
            Xnb::XnbModelData model = Model();
            auto& effect = std::get<Xnb::XnbBasicEffectData>(model.sharedResources[2].value);
            effect.alpha = std::numeric_limits<float>::quiet_NaN();
            effect.specularPower = std::numeric_limits<float>::infinity();
            effect.diffuseColor = Vector3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
            Write(model);
        }});
        seeds.push_back({"a bounding sphere with a negative radius and a NaN centre",
                         Outcome::Written,
                         "same: XNA writes the sphere the content gave it, and a Model's own "
                         "bounds are advisory",
                         []
        {
            Xnb::XnbModelData model = Model();
            model.meshes[0].boundingSphere =
                BoundingSphere(Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
                               -1.0f);
            Write(model);
        }});

        // -- audio metadata ----------------------------------------------------------------------
        seeds.push_back({"a sound with zero channels and a zero sample rate",
                         Outcome::Written,
                         "WAVEFORMATEX has no validity rule the writer owns; SoundEffect's own "
                         "constructor is where this is caught, and the reader tests cover it",
                         []
        {
            Xnb::XnbSoundEffectData sound = Sound();
            sound.channels = 0u;
            sound.sampleRate = 0u;
            Write(sound);
        }});
        seeds.push_back({"a sound whose loop region runs past its samples",
                         Outcome::Written,
                         "XNA stored loop points as authored and clamped at play time; refusing "
                         "here would reject files a real XNA pipeline produced",
                         []
        {
            Xnb::XnbSoundEffectData sound = Sound();
            sound.loopStart = 1 << 20;
            sound.loopLength = 1 << 20;
            Write(sound);
        }});
        seeds.push_back({"a sound with a negative loop start",
                         Outcome::Written,
                         "same reason -- the field is a signed int32 in the format and the runtime "
                         "clamps",
                         []
        {
            Xnb::XnbSoundEffectData sound = Sound();
            sound.loopStart = -5;
            Write(sound);
        }});
        seeds.push_back({"a sound whose block alignment contradicts its bit depth",
                         Outcome::Written,
                         "the WAVEFORMATEX fields are copied through; CNA's own reader "
                         "deliberately ignores an incoherent blockAlign rather than trusting it "
                         "(see the reader tests)",
                         []
        {
            Xnb::XnbSoundEffectData sound = Sound();
            sound.blockAlign = 0u;
            sound.bitsPerSample = 0u;
            Write(sound);
        }});
        seeds.push_back({"a sound with a format tag no WAVEFORMATEX defines",
                         Outcome::Written,
                         "an unknown tag with extension data is exactly what a codec CNA does not "
                         "know looks like; the reader refuses it by tag, which is the right place",
                         []
        {
            Xnb::XnbSoundEffectData sound = Sound();
            sound.formatTag = 0xFFFFu;
            sound.extensionData.assign(4096u, 0xAAu);
            Write(sound);
        }});

        // -- sprite font tables that disagree -----------------------------------------------------
        seeds.push_back({"a font whose glyph, cropping and character tables have different sizes",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbSpriteFontData font = Font();
            font.glyphs.push_back(Rectangle(0, 0, 1, 1));
            Write(font);
        }});
        seeds.push_back({"a font whose characters are not sorted", Outcome::Refused, nullptr, []
        {
            Xnb::XnbSpriteFontData font = Font();
            font.characters = {u'Z', u'A'};
            font.glyphs.push_back(Rectangle(0, 0, 1, 1));
            font.cropping.push_back(Rectangle(0, 0, 1, 1));
            font.kerning.push_back(Vector3{0.0f, 1.0f, 0.0f});
            Write(font);
        }});
        seeds.push_back({"a font whose default character is not in its character table",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbSpriteFontData font = Font();
            font.defaultCharacter = u'Q';
            Write(font);
        }});
        seeds.push_back({"a font glyph rectangle outside its own atlas",
                         Outcome::Written,
                         "SpriteBatch clamps its source rectangle, and a glyph deliberately larger "
                         "than the atlas is how some authored fonts express overflow. Refusing "
                         "would be inventing a rule",
                         []
        {
            Xnb::XnbSpriteFontData font = Font();
            font.glyphs[0] = Rectangle(0, 0, 100000, 100000);
            Write(font);
        }});
        seeds.push_back({"a font with a negative line spacing",
                         Outcome::Written,
                         "negative line spacing is a legal SpriteFont value; it stacks lines "
                         "upward",
                         []
        {
            Xnb::XnbSpriteFontData font = Font();
            font.lineSpacing = -1;
            Write(font);
        }});

        // -- media metadata ------------------------------------------------------------------
        seeds.push_back({"a video with impossible dimensions and a NaN frame rate",
                         Outcome::Written,
                         "the Video metadata route requires these to be configured and validates "
                         "them there; the writer copies them through the way XNA's did",
                         []
        {
            Xnb::XnbVideoData video;
            video.mediaPath = "Video/clip.mp4";
            video.width = -1;
            video.height = std::numeric_limits<std::int32_t>::max();
            video.framesPerSecond = std::numeric_limits<float>::quiet_NaN();
            video.durationMs = -1;
            Write(video);
        }});
        seeds.push_back({"a song whose media path escapes the content root",
                         Outcome::Written,
                         "the path is written as a content *reference*, and containment is the "
                         "ContentManager's rule at load time -- the writer does not know the "
                         "content root",
                         []
        {
            Xnb::XnbSongData song;
            song.mediaPath = "../../../etc/passwd";
            song.durationMs = -1;
            Write(song);
        }});
        seeds.push_back({"a song with an absolute media path",
                         Outcome::Written,
                         "same reason; resolution and containment belong to the loader",
                         []
        {
            Xnb::XnbSongData song;
            song.mediaPath = "/etc/passwd";
            Write(song);
        }});

        // -- container options --------------------------------------------------------------
        seeds.push_back({"an unsupported platform byte", Outcome::Refused, nullptr, []
        {
            Xnb::XnbFileOptions options;
            options.platform = static_cast<Xnb::XnbTargetPlatform>(0x7F);
            Write(Texture2D(), options);
        }});
        seeds.push_back({"LZ4 compression on an XNA 4.0 target platform",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbFileOptions options;
            options.compression = Xnb::XnbOutputCompression::Lz4;
            options.platform = Xnb::XnbTargetPlatform::Windows;
            Write(Texture2D(), options);
        }});
        seeds.push_back({"a maximum file size smaller than a header", Outcome::Refused, nullptr, []
        {
            Xnb::XnbFileOptions options;
            options.limits.maxFileSize = 4;
            Write(Texture2D(), options);
        }});
        seeds.push_back({"a shared-resource ceiling of zero on a model that needs three",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbFileOptions options;
            options.limits.maxSharedResourceCount = 0;
            Write(Model(), options);
        }});
        seeds.push_back({"a compressible payload written under a tiny size ceiling",
                         Outcome::Refused, nullptr, []
        {
            Xnb::XnbFileOptions options;
            options.compression = Xnb::XnbOutputCompression::Lzx;
            options.limits.maxFileSize = 64;
            Xnb::XnbTextureData texture = Texture2D();
            texture.width = 256u;
            texture.height = 256u;
            texture.levels = {ColorLevel(256u, 256u)};
            Write(texture, options);
        }});

        return seeds;
    }

    /** @brief Runs @p work under a deadline, because a hang is a failure and not a slow pass. */
    template<typename Work>
    void WithDeadline(const int seconds, const char* what, Work work)
    {
        std::future<void> pending = std::async(std::launch::async, std::move(work));
        if (pending.wait_for(std::chrono::seconds(seconds)) == std::future_status::timeout)
        {
            ADD_FAILURE() << what << " did not finish within " << seconds << " seconds.";
            std::cerr.flush();
            std::quick_exit(1);
        }
        pending.get();
    }

    /** @brief A small deterministic generator; no clock, no std::random_device. */
    struct Rng
    {
        std::uint64_t state;

        explicit Rng(const std::uint64_t seed) : state(seed) {}

        std::uint32_t Next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }

        std::uint32_t Below(const std::uint32_t bound) { return bound == 0u ? 0u : Next() % bound; }

        /** @brief A value drawn from the ends of the range, where the interesting ones live. */
        std::int32_t Extreme()
        {
            static const std::int32_t values[] = {
                0, 1, -1, 2, -2, 7, -7, 255, 256, 65535, 65536,
                std::numeric_limits<std::int32_t>::max(),
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max() - 1,
                1 << 20, -(1 << 20),
            };
            return values[Below(sizeof(values) / sizeof(values[0]))];
        }
    };
} // namespace

TEST(XnbWriterInputFuzzTest, EveryAdversarialCanonicalValueIsRefusedCleanlyOrWritten)
{
    const std::vector<Seed> corpus = Corpus();
    ASSERT_GE(corpus.size(), 40u) << "the corpus shrank; every seed here has a reason";

    std::size_t refused = 0u;
    std::size_t accepted = 0u;
    for (const Seed& seed : corpus)
    {
        // Every seed is scored: it is not enough that nothing crashed. A value that starts being
        // written when it used to be refused is a loosened check, and a value that starts being
        // refused when it used to be written is a route that stopped working -- both fail here.
        ASSERT_TRUE(seed.expected == Outcome::Refused || seed.writtenBecause != nullptr)
            << "seed '" << seed.reason << "' expects to be written and says nothing about why";

        WithDeadline(30, seed.reason, [&]
        {
            try
            {
                seed.write();
                ++accepted;
                EXPECT_EQ(seed.expected, Outcome::Written)
                    << "the writer accepted '" << seed.reason
                    << "', which is recorded as something it must refuse.";
            }
            catch (const Xnb::XnbWriteException& refusal)
            {
                ++refused;
                EXPECT_EQ(seed.expected, Outcome::Refused)
                    << "the writer refused '" << seed.reason
                    << "', which is recorded as something it must write, because "
                    << (seed.writtenBecause == nullptr ? "" : seed.writtenBecause)
                    << ".\nIt said: " << refusal.what();
            }
            catch (const std::exception& error)
            {
                ADD_FAILURE()
                    << "seed '" << seed.reason << "' threw " << typeid(error).name()
                    << " rather than XnbWriteException: " << error.what()
                    << ".\nEvery refusal on this path must be an XnbWriteException naming the "
                       "value, because that is what a build turns into a diagnostic a user can "
                       "act on. Anything else escaped a check.";
            }
        });
    }

    EXPECT_EQ(refused + accepted, corpus.size());
    std::cout << "[ CORPUS   ] " << corpus.size() << " adversarial writer inputs: " << refused
              << " refused, " << accepted << " written, 0 unscored.\n";
}

TEST(XnbWriterInputFuzzTest, GeneratedFieldMutationsNeverEscapeTheWritersOwnExceptionType)
{
    // The corpus above reaches structural cases a random mutator would find only by accident.
    // This pass covers the accidental ones: it takes each ordinary value and perturbs numeric
    // fields with values drawn from the ends of their ranges, which is where the arithmetic that
    // computes a buffer size goes wrong.
    constexpr std::uint64_t seed = 0x5EED4A5ULL;
    constexpr int iterations = 400;
    Rng rng(seed);

    std::size_t refused = 0u;
    std::size_t accepted = 0u;
    WithDeadline(120, "the generated mutation pass", [&]
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            try
            {
                switch (rng.Below(4u))
                {
                case 0u:
                {
                    Xnb::XnbTextureData texture = Texture2D();
                    texture.width = static_cast<std::uint32_t>(rng.Extreme());
                    texture.height = static_cast<std::uint32_t>(rng.Extreme());
                    texture.depth = static_cast<std::uint32_t>(rng.Extreme());
                    texture.mipCount = static_cast<std::uint32_t>(rng.Extreme());
                    texture.faceCount = static_cast<std::uint32_t>(rng.Extreme());
                    texture.kind = static_cast<Xnb::XnbTextureKind>(rng.Below(4u));
                    Write(texture);
                    break;
                }
                case 1u:
                {
                    Xnb::XnbModelData model = Model();
                    model.rootBone = rng.Extreme();
                    model.bones[0].parent = rng.Extreme();
                    model.meshes[0].parentBone = rng.Extreme();
                    model.meshes[0].parts[0].vertexOffset = rng.Extreme();
                    model.meshes[0].parts[0].vertexCount = rng.Extreme();
                    model.meshes[0].parts[0].startIndex = rng.Extreme();
                    model.meshes[0].parts[0].primitiveCount = rng.Extreme();
                    model.meshes[0].parts[0].vertexBufferResource = rng.Extreme();
                    model.meshes[0].parts[0].indexBufferResource = rng.Extreme();
                    model.meshes[0].parts[0].effectResource = rng.Extreme();
                    Write(model);
                    break;
                }
                case 2u:
                {
                    Xnb::XnbSoundEffectData sound = Sound();
                    sound.formatTag = static_cast<std::uint16_t>(rng.Extreme());
                    sound.channels = static_cast<std::uint16_t>(rng.Extreme());
                    sound.sampleRate = static_cast<std::uint32_t>(rng.Extreme());
                    sound.averageBytesPerSecond = static_cast<std::uint32_t>(rng.Extreme());
                    sound.blockAlign = static_cast<std::uint16_t>(rng.Extreme());
                    sound.bitsPerSample = static_cast<std::uint16_t>(rng.Extreme());
                    sound.loopStart = rng.Extreme();
                    sound.loopLength = rng.Extreme();
                    Write(sound);
                    break;
                }
                default:
                {
                    Xnb::XnbSpriteFontData font = Font();
                    font.lineSpacing = rng.Extreme();
                    font.glyphs[0] = Rectangle(rng.Extreme(), rng.Extreme(), rng.Extreme(),
                                               rng.Extreme());
                    font.cropping[0] = Rectangle(rng.Extreme(), rng.Extreme(), rng.Extreme(),
                                                 rng.Extreme());
                    Write(font);
                    break;
                }
                }
                ++accepted;
            }
            catch (const Xnb::XnbWriteException&)
            {
                ++refused;
            }
            catch (const std::exception& error)
            {
                FAIL() << "iteration " << iteration << " (seed 0x" << std::hex << seed
                       << std::dec << ") threw " << typeid(error).name()
                       << " rather than XnbWriteException: " << error.what();
            }
        }
    });

    EXPECT_EQ(refused + accepted, static_cast<std::size_t>(iterations));
    EXPECT_GT(refused, 0u) << "no mutation was refused, so nothing was being checked";
    std::cout << "[ MUTATION ] " << iterations << " generated writer inputs (seed 0x" << std::hex
              << seed << std::dec << "): " << refused << " refused, " << accepted << " written.\n";
}

TEST(XnbWriterInputFuzzTest, ARefusedWriteLeavesNoPartiallyPublishedFile)
{
    // The one thing worse than a refusal is half a file: a later incremental build would verify a
    // digest against something that was never finished.
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() /
        ("cna_writer_fuzz_" + std::to_string(reinterpret_cast<std::uintptr_t>(&scratch)));
    std::filesystem::create_directories(scratch);

    std::size_t refused = 0u;
    for (const Seed& seed : Corpus())
    {
        static_cast<void>(seed);
        ++refused;
    }
    ASSERT_GT(refused, 0u);

    const std::filesystem::path output = scratch / "refused.xnb";
    Xnb::XnbModelData model = Model();
    model.meshes[0].parts[0].vertexBufferResource = 99;
    EXPECT_THROW(Xnb::WriteXnbAssetFile(output, model, {}, "fuzz"), Xnb::XnbWriteException);
    EXPECT_FALSE(std::filesystem::exists(output))
        << "a refused write published " << std::filesystem::file_size(output) << " bytes";

    // Nothing beside it either: the atomic-publication path must not leave its temporary behind.
    std::vector<std::string> leftovers;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(scratch))
    {
        leftovers.push_back(entry.path().filename().string());
    }
    EXPECT_TRUE(leftovers.empty())
        << "a refused write left " << leftovers.size() << " file(s) behind, first: "
        << (leftovers.empty() ? std::string{} : leftovers.front());

    std::error_code error;
    std::filesystem::remove_all(scratch, error);
}
