// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/Guid.hpp"
#include "System/IO/FileStream.hpp"
#include "CNA/DdsCubeFixtureEXT.hpp"
#if defined(CNA_RENDERER_SOFTWARE)
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CNA_PRESSURE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define CNA_PRESSURE_ASAN 1
#endif

#if defined(CNA_PRESSURE_ASAN)
extern "C" int __sanitizer_install_malloc_and_free_hooks(
    void (*mallocHook)(const volatile void*, std::size_t),
    void (*freeHook)(const volatile void*));
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Content;
using Microsoft::Xna::Framework::Audio::SoundEffect;

namespace
{
    // Requested heap bytes, including malloc/calloc/realloc and C++ new, on the loading thread.
    // ASan's allocator hooks include dependency allocations (stb/cgltf) and SOFTWARE storage.
    // This fixed table allocates nothing inside a hook. No RSS, ASan redzones/quarantine, stacks,
    // file mappings, foreign-thread allocations or real GPU/driver memory are counted.
    class HeapLedger
    {
        struct Entry { const volatile void* pointer; std::size_t size; };
        static constexpr std::size_t Capacity = 131072;
        static inline std::array<Entry, Capacity> entries{};
        static inline thread_local HeapLedger* active = nullptr;
        static constexpr auto Tombstone = static_cast<std::uintptr_t>(1);
        std::size_t liveBytes = 0;
        std::size_t liveCount = 0;
        std::size_t peakBytes = 0;
        std::size_t allocations = 0;
        bool overflow = false;

        static std::size_t Hash(const volatile void* pointer)
        {
            return (reinterpret_cast<std::uintptr_t>(pointer) >> 4u) % Capacity;
        }
        static void Allocate(const volatile void* pointer, std::size_t size)
        {
            auto* ledger = active;
            if (!ledger || !pointer) return;
            auto slot = Hash(pointer);
            for (std::size_t n = 0; n < Capacity; ++n, slot = (slot + 1) % Capacity)
            {
                auto& entry = entries[slot];
                if (!entry.pointer || reinterpret_cast<std::uintptr_t>(entry.pointer) == Tombstone)
                {
                    entry = {pointer, size};
                    ledger->liveBytes += size;
                    ++ledger->liveCount;
                    ++ledger->allocations;
                    ledger->peakBytes = std::max(ledger->peakBytes, ledger->liveBytes);
                    return;
                }
            }
            ledger->overflow = true;
        }
        static void Free(const volatile void* pointer)
        {
            auto* ledger = active;
            if (!ledger || !pointer) return;
            auto slot = Hash(pointer);
            for (std::size_t n = 0; n < Capacity; ++n, slot = (slot + 1) % Capacity)
            {
                auto& entry = entries[slot];
                if (!entry.pointer) return;
                if (entry.pointer == pointer)
                {
                    ledger->liveBytes -= entry.size;
                    --ledger->liveCount;
                    entry = {reinterpret_cast<const volatile void*>(Tombstone), 0};
                    return;
                }
            }
        }
    public:
        struct Sample { std::size_t bytes; std::size_t count; };
        static bool Available()
        {
#if defined(CNA_PRESSURE_ASAN)
            static const bool installed = __sanitizer_install_malloc_and_free_hooks(Allocate, Free) != 0;
            return installed;
#else
            return false;
#endif
        }
        HeapLedger()
        {
            for (auto& entry : entries) entry = {};
            active = this;
        }
        ~HeapLedger()
        {
            active = nullptr;
            // Do not leave diagnostic pointers as global roots for a later LeakSanitizer run.
            for (auto& entry : entries) entry = {};
        }
        Sample Now() const { return {liveBytes, liveCount}; }
        void ResetPeak() { peakBytes = liveBytes; allocations = 0; }
        std::size_t Peak() const { return peakBytes; }
        std::size_t Allocations() const { return allocations; }
        bool Overflowed() const { return overflow; }
    };

    class PressureAssets
    {
    public:
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("cna-safe006-" + System::Guid::NewGuid().ToString());
        explicit PressureAssets(GraphicsDevice& device)
        {
            std::filesystem::create_directory(root);
            const std::array<float, 24> vertices{
                -0.5f, -0.5f, 0, 0, 0, 1, 0, 1,
                 0.5f, -0.5f, 0, 0, 0, 1, 1, 1,
                 0.0f,  0.5f, 0, 0, 0, 1, 0.5f, 0};
            const std::array<std::uint16_t, 3> indices{0, 1, 2};
            Write("vertices.bin", vertices.data(), sizeof(vertices));
            Write("indices.bin", indices.data(), sizeof(indices));
            Texture2D texture(device, 64, 64);
            std::vector<Color> pixels(64 * 64, Color(19, 37, 73, 255));
            texture.SetData(pixels.data(), static_cast<int>(pixels.size()));
            texture.SaveAsPng(CNA::Internal::PathToUtf8(root / "tile.png"));
            for (const char* name : {"a.png", "b.png", "c.png"})
                std::filesystem::copy_file(root / "tile.png", root / name);
            const std::array<Color, 6> faces{Color::Red, Color::Green, Color::Blue,
                                           Color::Yellow, Color::Magenta, Color::Cyan};
            const auto cube = CNA::TestSupport::BuildSolidColorCubeDds(
                16, faces.data(), 3, CNA::TestSupport::DdsBlockFormat::Rgb24);
            Write("cube.dds", cube.data(), cube.size());
            const auto dds = CNA::TestSupport::BuildSolidColorCubeDds(
                16, faces.data(), 5, CNA::TestSupport::DdsBlockFormat::Dxt1, false);
            Write("tile.dds", dds.data(), dds.size());
            std::filesystem::copy_file("tests/assets/xna40/media/pcm8_mono_8000.wav", root / "tone.wav");
            Text("model.cnj", ModelText());
            Text("texture.cnj", R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"tile.png"})");
            Text("cube.cnj", R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"cube.dds"})");
            Text("sound.cnj", R"({"cnjVersion":1,"type":"SoundEffect","sourceFile":"tone.wav"})");
            for (const char* name : {"model", "texture", "cube", "sound"})
            {
                auto compiled = CNA::Content::Cnb::CompileCnjToCnb(
                    CNA::Internal::PathToUtf8(root / (std::string(name) + ".cnj")));
                Write(std::string(name) + ".cnb", compiled.bytes.data(), compiled.bytes.size());
                if (CNA::Content::Cnb::IsCnbCompressionSupported(CNA::Content::Cnb::CnbCompression::ReservedZstd))
                {
                    namespace Cnb = CNA::Content::Cnb;
                    const auto document = Cnb::CnbDocument::Parse(std::move(compiled.bytes), name);
                    Cnb::CnbWriter writer(document.AssetTypeId(), document.AssetSchemaVersion());
                    writer.SetMetadata(document.Metadata().assetTypeName, name);
                    writer.SetExternalReferences(document.ExternalReferences());
                    writer.SetCompression(Cnb::CnbCompression::ReservedZstd);
                    for (std::size_t i = 0; i < document.ChunkCount(); ++i)
                    {
                        const auto& chunk = document.ChunkAt(i);
                        if (chunk.type == Cnb::MakeChunkId('C', 'M', 'E', 'T') ||
                            chunk.type == Cnb::MakeChunkId('X', 'R', 'E', 'F')) continue;
                        const auto data = document.ChunkData(i);
                        writer.AddChunk(chunk.type, {data.begin(), data.end()}, chunk.flags, chunk.alignment);
                    }
                    const auto compressed = writer.Build();
                    Write(std::string(name) + "-zstd.cnb", compressed.data(), compressed.size());
                }
            }
        }
        ~PressureAssets()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void Write(const std::string& name, const void* bytes, std::size_t size)
        {
            std::ofstream stream(root / name, std::ios::binary);
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            stream.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        }
        void Text(const std::string& name, const std::string& value) { Write(name, value.data(), value.size()); }
        static std::string ModelText()
        {
            std::string meshes;
            for (const char* name : {"a.png", "b.png", "c.png"})
            {
                if (!meshes.empty()) meshes += ',';
                meshes += R"({"name":"Triangle","vertices":"vertices.bin","indices":"indices.bin","vertexStride":32,"effect":"BasicEffect","texture":")";
                meshes += name;
                meshes += "\"}";
            }
            return R"({"cnjVersion":1,"type":"Model","meshes":[)" + meshes + "]}";
        }
    };

    enum class Kind { Model, Texture, Cube, Sound };
    struct Asset { const char* label; Kind kind; const char* root; const char* name; };
    class ContentResourcePressureTest : public ::testing::TestWithParam<Asset> {};

    template<class T>
    void Cycle(ContentManager& manager, const std::string& name)
    {
        { auto value = manager.Load<T>(name); }
        manager.Unload();
    }

    struct Footprint
    {
        std::size_t semanticObjects = 0;
        std::size_t payloadBytes = 0;
        std::size_t cpuPixelCapacity = 0;
        std::size_t rendererRgbaCapacity = 0;
    };
    Footprint Inspect(Texture2D& texture);
    Footprint Inspect(Model& model)
    {
        Footprint result{1, 0};
        const auto& meshes = model.getMeshesProperty();
        for (int m = 0; m < meshes.getCountProperty(); ++m)
        {
            ++result.semanticObjects;
            const auto& parts = meshes[m]->getMeshPartsProperty();
            for (int p = 0; p < parts.getCountProperty(); ++p)
            {
                ++result.semanticObjects;
                auto* part = parts[p];
                if (auto* vertices = part->getVertexBufferProperty())
                    result.payloadBytes += static_cast<std::size_t>(vertices->getVertexCountProperty()) *
                        vertices->getVertexDeclarationProperty().getVertexStrideProperty();
                if (auto* indices = part->getIndexBufferProperty())
                    result.payloadBytes += static_cast<std::size_t>(indices->getIndexCountProperty()) *
                        (indices->getIndexElementSizeProperty() == IndexElementSize::SixteenBits ? 2 : 4);
                Texture2D* texture = nullptr;
                if (auto* effect = dynamic_cast<BasicEffect*>(part->getEffectProperty())) texture = effect->getTextureProperty();
                if (auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty())) texture = effect->getTextureProperty();
                if (texture)
                {
                    const auto pixels = Inspect(*texture);
                    result.cpuPixelCapacity += pixels.cpuPixelCapacity;
                    result.rendererRgbaCapacity += pixels.rendererRgbaCapacity;
                }
            }
        }
        return result;
    }
    Footprint Inspect(Texture2D& texture)
    {
        // Logical RGBA texels, irrespective of source compression; not an extra counted copy.
        Footprint result{1, static_cast<std::size_t>(texture.getWidthProperty()) * texture.getHeightProperty() * 4};
        if (const auto pixels = texture.GetCpuPixelsWeak().lock()) result.cpuPixelCapacity = pixels->capacity();
#if defined(CNA_RENDERER_SOFTWARE)
        const auto& renderer = dynamic_cast<const CNA::Internal::Renderers::Software::SoftwareTextureRenderer&>(texture.GetRenderer());
        for (int i = 0; i < renderer.ColorLevelCount(); ++i)
            result.rendererRgbaCapacity += renderer.ColorPixels(i).capacity();
#endif
        return result;
    }
    Footprint Inspect(TextureCube& texture)
    {
        Footprint result{1, static_cast<std::size_t>(texture.getSizeProperty()) * texture.getSizeProperty() * 4 * 6};
#if defined(CNA_RENDERER_SOFTWARE)
        const auto& renderer = dynamic_cast<const CNA::Internal::Renderers::Software::SoftwareTextureCubeRenderer&>(texture.GetRenderer());
        for (int i = 0; i < renderer.LevelCount(); ++i)
            for (int face = 0; face < 6; ++face) result.rendererRgbaCapacity += renderer.FacePixels(face, i).capacity();
#endif
        return result;
    }
    Footprint Inspect(SoundEffect&) { return {1, 0}; }

    class DdsTextureReader final : public LooseFileContentTypeReader<Texture2D>
    {
    public:
        std::vector<std::string> GetExtensions() const override { return {".dds"}; }
        Texture2D Read(const std::string& path, ContentManager& manager) override
        {
            System::IO::FileStream stream(path, System::IO::FileMode::Open, System::IO::FileAccess::Read);
            return Texture2D::DDSFromStreamEXT(manager.getGraphicsDeviceInternal(), stream);
        }
    };

    template<class T>
    void Measure(GraphicsDevice& device, const std::string& root, const std::string& name, const char* label,
                 std::size_t decodedSoundBytes = 0)
    {
        ContentManager manager(nullptr, root);
        manager.setGraphicsDevice(device);
        // The default loose image reader has no DDS contract; use the explicit DDS API.
        if (std::string(label) == "DdsTexture") manager.RegisterTypeReader<Texture2D>(std::make_unique<DdsTextureReader>());
        // Warm all process registries, lazy type names, manager capacities and device bookkeeping.
        Cycle<T>(manager, name);
        const auto resources = device.GetTrackedResourceCount();
        std::array<HeapLedger::Sample, 50> after{};
        HeapLedger::Sample baseline{}, retained{}, heldAfterUnload{}, released{};
        std::size_t peak = 0, allocationCount = 0, loadedResources = 0;
        bool overflow = false;
        Footprint footprint{};
        {
            HeapLedger ledger;
            Cycle<T>(manager, name);
            baseline = ledger.Now();
            ledger.ResetPeak();
            {
                auto value = manager.Load<T>(name);
                retained = ledger.Now();
                loadedResources = device.GetTrackedResourceCount();
                footprint = Inspect(value);
                if (decodedSoundBytes != 0) footprint.payloadBytes = decodedSoundBytes;
                manager.Unload();
                heldAfterUnload = ledger.Now();
            }
            released = ledger.Now();
            peak = ledger.Peak();
            allocationCount = ledger.Allocations();
            for (auto& sample : after)
            {
                Cycle<T>(manager, name);
                sample = ledger.Now();
            }
            overflow = ledger.Overflowed();
        }
        ASSERT_FALSE(overflow) << "allocation table exhausted; measurement invalid";
        EXPECT_GT(allocationCount, 0u);
        EXPECT_GT(retained.bytes, baseline.bytes);
        EXPECT_GE(peak, retained.bytes);
        EXPECT_LT(peak, 16u * 1024u * 1024u) << "tiny-fixture harness heap budget";
        EXPECT_GT(footprint.semanticObjects, 0u);
        EXPECT_EQ(released.bytes, baseline.bytes);
        EXPECT_EQ(released.count, baseline.count);
        for (std::size_t cycle = 0; cycle < after.size(); ++cycle)
        {
            EXPECT_EQ(after[cycle].bytes, baseline.bytes) << label << " cycle " << cycle;
            EXPECT_EQ(after[cycle].count, baseline.count) << label << " cycle " << cycle;
        }
        EXPECT_EQ(device.GetTrackedResourceCount(), resources);
        std::printf("SAFE006 %s cycles=50 peak=%zu retained=%zu unload-held=%zu released=%zu allocations=%zu semantic-roots-meshes-parts=%zu logical-payload=%zu cpu-pixel-capacity=%zu renderer-rgba-capacity=%zu live-resources=%zu\n",
                    label, peak, retained.bytes, heldAfterUnload.bytes, released.bytes, allocationCount,
                    footprint.semanticObjects, footprint.payloadBytes, footprint.cpuPixelCapacity,
                    footprint.rendererRgbaCapacity, loadedResources - resources);
    }
}

TEST_P(ContentResourcePressureTest, PeakAndFiftyUnloadCycles)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required for heap accounting";
#if !defined(CNA_RENDERER_SOFTWARE)
    GTEST_SKIP() << "SOFTWARE required to count renderer storage as heap memory";
#endif
    GraphicsDevice device;
    PressureAssets fixtures(device);
    const auto& asset = GetParam();
    const auto root = asset.root[0] ? std::string(asset.root) : CNA::Internal::PathToUtf8(fixtures.root);
    const std::string name = asset.name;
    if (name.find("-zstd") != std::string::npos &&
        !CNA::Content::Cnb::IsCnbCompressionSupported(CNA::Content::Cnb::CnbCompression::ReservedZstd))
        GTEST_SKIP() << "Zstd support required";
    auto path = CNA::Internal::PathFromUtf8(root) / name;
    if (!std::filesystem::exists(path)) path += asset.kind == Kind::Model && std::string(asset.label) == "GltfModel" ? ".gltf" : ".xnb";
    const auto inputBytes = std::filesystem::file_size(path);
    ASSERT_LT(inputBytes, 512u * 1024u) << "fixture input budget";
    std::size_t expandedBytes = 0, sidecarBytes = 0;
    if (path.extension() == ".cnb")
    {
        const auto document = CNA::Content::Cnb::CnbDocument::ParseFile(CNA::Internal::PathToUtf8(path));
        for (std::size_t i = 0; i < document.ChunkCount(); ++i)
            expandedBytes += document.ChunkAt(i).uncompressedSize;
    }
    if (path.extension() == ".xnb")
    {
        std::array<unsigned char, 14> header{};
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(header.data()), header.size());
        if (header[5] & 0xC0)
            for (int i = 0; i < 4; ++i) expandedBytes |= static_cast<std::size_t>(header[10 + i]) << (8 * i);
        else expandedBytes = inputBytes - 10;
    }
    if (!asset.root[0])
    {
        auto add = [&](const char* file) { sidecarBytes += std::filesystem::file_size(fixtures.root / file); };
        if (asset.kind == Kind::Model)
        {
            for (const char* texture : {"a.png", "b.png", "c.png"}) add(texture);
            if (path.extension() == ".cnj") { add("vertices.bin"); add("indices.bin"); }
        }
        else if (path.extension() == ".cnj")
            add(asset.kind == Kind::Texture ? "tile.png" : asset.kind == Kind::Cube ? "cube.dds" : "tone.wav");
    }
    ASSERT_LT(sidecarBytes, 512u * 1024u) << "fixture sidecar budget";
    ASSERT_LT(expandedBytes, 512u * 1024u) << "fixture expansion budget";
    std::printf("SAFE006 input %s primary=%ju unique-sidecars=%zu expanded-container=%zu\n",
                asset.label, static_cast<std::uintmax_t>(inputBytes), sidecarBytes, expandedBytes);
    std::size_t decodedSoundBytes = 0;
    if (asset.kind == Kind::Sound)
    {
        if (!asset.root[0])
        {
            std::ifstream wav(fixtures.root / "tone.wav", std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(wav)), std::istreambuf_iterator<char>());
            decodedSoundBytes = CNA::Internal::Audio::DecodeWavToPcm16(bytes, "SAFE006 sound source").samples.size();
        }
        else
        {
            // Independent committed manifest: both XNB encodings contain this same PCM payload.
            std::ifstream expected("tests/assets/xnb/cna/windows/uncompressed/soundeffect_pcm16_mono_22050.expected.json");
            const std::string text((std::istreambuf_iterator<char>(expected)), std::istreambuf_iterator<char>());
            const auto manifest = CNA::Internal::ParseJson(text);
            decodedSoundBytes = static_cast<std::size_t>(manifest.FindMember("root")->FindMember("sampleByteCount")->numberValue);
        }
        ASSERT_GT(decodedSoundBytes, 0u);
    }
    switch (asset.kind)
    {
        case Kind::Model: Measure<Model>(device, root, name, asset.label); break;
        case Kind::Texture: Measure<Texture2D>(device, root, name, asset.label); break;
        case Kind::Cube: Measure<TextureCube>(device, root, name, asset.label); break;
        case Kind::Sound: Measure<SoundEffect>(device, root, name, asset.label, decodedSoundBytes); break;
    }
}

INSTANTIATE_TEST_SUITE_P(Safe006, ContentResourcePressureTest, ::testing::Values(
    Asset{"CnjModel", Kind::Model, "", "model.cnj"},
    Asset{"CnbModel", Kind::Model, "", "model.cnb"},
    Asset{"ZstdModel", Kind::Model, "", "model-zstd.cnb"},
    Asset{"XnbModel", Kind::Model, "tests/assets/xnb/cna/windows/uncompressed", "model_triangle_basiceffect"},
    Asset{"LzxModel", Kind::Model, "tests/assets/xnb/cna/windows/lzx", "model_triangle_basiceffect"},
    Asset{"GltfModel", Kind::Model, "tests/assets/gltf", "mat-basecolor-factor-times-texture"},
    Asset{"PngTexture", Kind::Texture, "", "tile.png"},
    Asset{"DdsTexture", Kind::Texture, "", "tile.dds"},
    Asset{"CnjTexture", Kind::Texture, "", "texture.cnj"},
    Asset{"CnbTexture", Kind::Texture, "", "texture.cnb"},
    Asset{"ZstdTexture", Kind::Texture, "", "texture-zstd.cnb"},
    Asset{"XnbTexture", Kind::Texture, "tests/assets/xnb/cna/windows/uncompressed", "texture2d_color_mips"},
    Asset{"LzxTexture", Kind::Texture, "tests/assets/xnb/cna/windows/lzx", "texture2d_color_mips"},
    Asset{"DdsCube", Kind::Cube, "", "cube.dds"},
    Asset{"CnjCube", Kind::Cube, "", "cube.cnj"},
    Asset{"CnbCube", Kind::Cube, "", "cube.cnb"},
    Asset{"ZstdCube", Kind::Cube, "", "cube-zstd.cnb"},
    Asset{"XnbCube", Kind::Cube, "tests/assets/xnb/cna/windows/uncompressed", "texturecube_six_faces"},
    Asset{"LzxCube", Kind::Cube, "tests/assets/xnb/cna/windows/lzx", "texturecube_six_faces"},
    Asset{"WavSound", Kind::Sound, "", "tone.wav"},
    Asset{"CnjSound", Kind::Sound, "", "sound.cnj"},
    Asset{"CnbSound", Kind::Sound, "", "sound.cnb"},
    Asset{"ZstdSound", Kind::Sound, "", "sound-zstd.cnb"},
    Asset{"XnbSound", Kind::Sound, "tests/assets/xnb/cna/windows/uncompressed", "soundeffect_pcm16_mono_22050"},
    Asset{"LzxSound", Kind::Sound, "tests/assets/xnb/cna/windows/lzx", "soundeffect_pcm16_mono_22050"}),
    [](const ::testing::TestParamInfo<Asset>& info) { return info.param.label; });

namespace
{
    class RefusingTextureReader final : public LooseFileContentTypeReader<Texture2D>
    {
    public:
        int calls = 0;
        int refuseAt = 2;
        std::array<std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer>, 3> renderers{};
        std::vector<std::string> GetExtensions() const override { return {".png"}; }
        Texture2D Read(const std::string& path, ContentManager& manager) override
        {
            // Deliberate refusal after decoding/uploading the Nth texture, before cache insertion.
            System::IO::FileStream stream(path, System::IO::FileMode::Open, System::IO::FileAccess::Read);
            auto texture = Texture2D::FromStream(manager.getGraphicsDeviceInternal(), stream);
            renderers[static_cast<std::size_t>(calls % 3)] = texture.GetRendererWeak();
            if (++calls == refuseAt) throw std::bad_alloc();
            return texture;
        }
    };
}

TEST(ContentResourceFailureTest, Safe006SecondAndThirdTextureRefusalUnwindsAndCacheCanRetry)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required";
#if !defined(CNA_RENDERER_SOFTWARE)
    GTEST_SKIP() << "SOFTWARE required for this campaign";
#endif
    GraphicsDevice device;
    PressureAssets fixtures(device);
    for (const char* asset : {"model.cnj", "model.cnb"})
    for (int refuseAt : {2, 3})
    {
        ContentManager manager(nullptr, CNA::Internal::PathToUtf8(fixtures.root));
        manager.setGraphicsDevice(device);
        auto reader = std::make_unique<RefusingTextureReader>();
        auto* seam = reader.get();
        manager.RegisterTypeReader<Texture2D>(std::move(reader));
        const auto initialResources = device.GetTrackedResourceCount();
        std::array<HeapLedger::Sample, 51> samples{};
        bool sawAllRefusals = true, recovered = true, released = true, cacheHit = true;
        bool overflow = false;
        {
            HeapLedger ledger;
            for (auto& sample : samples)
            {
                seam->calls = 0;
                seam->refuseAt = refuseAt;
                bool refused = false;
                try { auto model = manager.Load<Model>(asset); }
                catch (const ContentLoadException&) { refused = true; }
                catch (const std::bad_alloc&) { refused = true; }
                sawAllRefusals &= refused && seam->calls == refuseAt;
                // Completed children are intentionally retained until Unload (XNA ownership).
                released &= seam->renderers[static_cast<std::size_t>(refuseAt - 1)].expired();
                seam->refuseAt = 0;
                {
                    auto model = manager.Load<Model>(asset);
                    recovered &= model.getMeshesProperty().getCountProperty() == 3;
                    const int calls = seam->calls;
                    auto cached = manager.Load<Model>(asset);
                    cacheHit &= seam->calls == calls;
                }
                manager.Unload();
                for (const auto& renderer : seam->renderers) released &= renderer.expired();
                sample = ledger.Now();
            }
            overflow = ledger.Overflowed();
        }
        EXPECT_FALSE(overflow);
        EXPECT_TRUE(sawAllRefusals) << asset << " refusal " << refuseAt;
        EXPECT_TRUE(recovered);
        EXPECT_TRUE(cacheHit);
        EXPECT_TRUE(released);
        EXPECT_EQ(device.GetTrackedResourceCount(), initialResources);
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            EXPECT_EQ(samples[i].bytes, samples[0].bytes) << asset << " cycle " << i;
            EXPECT_EQ(samples[i].count, samples[0].count) << asset << " cycle " << i;
        }
        std::printf("SAFE006 texture-refusal %s at=%d failure+recovery-cycles=51 released=%zu\n",
                    asset, refuseAt, samples.back().bytes);
    }
}

TEST(ContentResourcePressureAccountingTest, Safe006HooksCountCAndCppAllocationsAndFrees)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required";
    HeapLedger::Sample first{}, both{}, final{};
    bool overflow = false;
    {
        HeapLedger ledger;
        void* c = std::malloc(113);
        first = ledger.Now();
        auto* cpp = new unsigned char[257];
        both = ledger.Now();
        delete[] cpp;
        std::free(c);
        final = ledger.Now();
        overflow = ledger.Overflowed();
    }
    EXPECT_FALSE(overflow);
    EXPECT_EQ(first.bytes, 113u);
    EXPECT_EQ(first.count, 1u);
    EXPECT_EQ(both.bytes, 370u);
    EXPECT_EQ(both.count, 2u);
    EXPECT_EQ(final.bytes, 0u);
    EXPECT_EQ(final.count, 0u);
}

TEST(ContentResourcePressureAccountingTest, Safe006AggregateModelTextureCubeAndSoundStayResidentTogether)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required";
#if !defined(CNA_RENDERER_SOFTWARE)
    GTEST_SKIP() << "SOFTWARE required to count renderer storage";
#endif
    GraphicsDevice device;
    PressureAssets fixtures(device);
    ContentManager manager(nullptr, CNA::Internal::PathToUtf8(fixtures.root));
    manager.setGraphicsDevice(device);
    auto batch = [&]()
    {
        auto model = manager.Load<Model>("model.cnb");
        auto texture = manager.Load<Texture2D>("texture.cnb");
        auto cube = manager.Load<TextureCube>("cube.cnb");
        auto sound = manager.Load<SoundEffect>("sound.cnb");
    };
    batch();
    manager.Unload();
    const auto resources = device.GetTrackedResourceCount();
    HeapLedger::Sample baseline{}, modelOnly{}, withTexture{}, withCube{}, all{}, final{};
    std::array<HeapLedger::Sample, 50> cycles{};
    std::size_t peak = 0;
    bool overflow = false;
    {
        HeapLedger ledger;
        batch();
        manager.Unload();
        baseline = ledger.Now();
        ledger.ResetPeak();
        {
            auto model = manager.Load<Model>("model.cnb");
            modelOnly = ledger.Now();
            auto texture = manager.Load<Texture2D>("texture.cnb");
            withTexture = ledger.Now();
            auto cube = manager.Load<TextureCube>("cube.cnb");
            withCube = ledger.Now();
            auto sound = manager.Load<SoundEffect>("sound.cnb");
            all = ledger.Now();
            manager.Unload();
        }
        final = ledger.Now();
        peak = ledger.Peak();
        for (auto& cycle : cycles)
        {
            batch();
            manager.Unload();
            cycle = ledger.Now();
        }
        overflow = ledger.Overflowed();
    }
    EXPECT_FALSE(overflow);
    EXPECT_GT(modelOnly.bytes, baseline.bytes);
    EXPECT_GT(withTexture.bytes, modelOnly.bytes);
    EXPECT_GT(withCube.bytes, withTexture.bytes);
    EXPECT_GT(all.bytes, withCube.bytes);
    EXPECT_GE(peak, all.bytes);
    EXPECT_LT(peak, 16u * 1024u * 1024u) << "tiny-fixture aggregate heap budget";
    EXPECT_EQ(final.bytes, baseline.bytes);
    EXPECT_EQ(final.count, baseline.count);
    for (const auto& cycle : cycles)
    {
        EXPECT_EQ(cycle.bytes, baseline.bytes);
        EXPECT_EQ(cycle.count, baseline.count);
    }
    EXPECT_EQ(device.GetTrackedResourceCount(), resources);
    std::printf("SAFE006 aggregate cycles=50 model=%zu +texture=%zu +cube=%zu +sound=%zu peak=%zu released=%zu\n",
                modelOnly.bytes, withTexture.bytes, withCube.bytes, all.bytes, peak, final.bytes);
}

TEST(ContentResourceFailureTest, Safe006GltfLateImageDecodeFailureReleasesPartialConstructionAndRetries)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required";
#if !defined(CNA_RENDERER_SOFTWARE)
    GTEST_SKIP() << "SOFTWARE required for this campaign";
#endif
    GraphicsDevice device;
    PressureAssets fixtures(device);
    std::ifstream source("tests/assets/gltf/mat-basecolor-factor-times-texture.gltf");
    const std::string original((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    auto gltf = CNA::Internal::ParseJson(original);
    auto images = *gltf.FindMember("images");
    auto secondImage = images.arrayValue[0];
    secondImage.Set("uri", CNA::Internal::JsonValue::MakeString("late.png"));
    images.arrayValue.push_back(std::move(secondImage));
    gltf.Set("images", std::move(images));
    auto textures = *gltf.FindMember("textures");
    auto secondTexture = textures.arrayValue[0];
    secondTexture.Set("source", CNA::Internal::JsonValue::MakeNumber(1));
    textures.arrayValue.push_back(std::move(secondTexture));
    gltf.Set("textures", std::move(textures));
    auto materials = *gltf.FindMember("materials");
    auto secondMaterial = materials.arrayValue[0];
    auto pbr = *secondMaterial.FindMember("pbrMetallicRoughness");
    auto baseColor = *pbr.FindMember("baseColorTexture");
    baseColor.Set("index", CNA::Internal::JsonValue::MakeNumber(1));
    pbr.Set("baseColorTexture", std::move(baseColor));
    secondMaterial.Set("pbrMetallicRoughness", std::move(pbr));
    materials.arrayValue.push_back(std::move(secondMaterial));
    gltf.Set("materials", std::move(materials));
    auto meshes = *gltf.FindMember("meshes");
    auto primitives = *meshes.arrayValue[0].FindMember("primitives");
    auto secondPrimitive = primitives.arrayValue[0];
    secondPrimitive.Set("material", CNA::Internal::JsonValue::MakeNumber(1));
    primitives.arrayValue.push_back(std::move(secondPrimitive));
    meshes.arrayValue[0].Set("primitives", std::move(primitives));
    gltf.Set("meshes", std::move(meshes));
    fixtures.Text("late.gltf", CNA::Internal::WriteJson(gltf));
    std::ifstream png(fixtures.root / "tile.png", std::ios::binary);
    const std::string validImage((std::istreambuf_iterator<char>(png)), std::istreambuf_iterator<char>());
    ContentManager manager(nullptr, CNA::Internal::PathToUtf8(fixtures.root));
    manager.setGraphicsDevice(device);
    const auto resources = device.GetTrackedResourceCount();
    std::array<HeapLedger::Sample, 51> samples{};
    bool refused = true, constructedFirstPrimitive = true, recovered = true, overflow = false;
    {
        HeapLedger ledger;
        for (auto& sample : samples)
        {
            fixtures.Text("late.png", "invalid image");
            bool threw = false;
            try { auto model = manager.Load<Model>("late.gltf"); }
            catch (const ContentLoadException&) { threw = true; }
            refused &= threw;
            constructedFirstPrimitive &= device.GetTrackedResourceCount() > resources;
            fixtures.Text("late.png", validImage);
            {
                auto model = manager.Load<Model>("late.gltf");
                recovered &= model.getMeshesProperty().getCountProperty() == 1 &&
                    model.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty() == 2;
            }
            manager.Unload();
            sample = ledger.Now();
        }
        overflow = ledger.Overflowed();
    }
    EXPECT_FALSE(overflow);
    EXPECT_TRUE(refused);
    EXPECT_TRUE(constructedFirstPrimitive);
    EXPECT_TRUE(recovered);
    EXPECT_EQ(device.GetTrackedResourceCount(), resources);
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        EXPECT_EQ(samples[i].bytes, samples[0].bytes) << "cycle " << i;
        EXPECT_EQ(samples[i].count, samples[0].count) << "cycle " << i;
    }
    std::printf("SAFE006 gltf-late-image failure+recovery-cycles=51 released=%zu\n", samples.back().bytes);
}

namespace
{
    class RecorderFailureManager : public ContentManager
    {
    public:
        using ContentManager::ContentManager;
        using ContentManager::ReadAsset;
    };

    template<class T>
    void RefuseXnbRecording(GraphicsDevice& device, const char* root, const char* name, int refuseAt)
    {
        RecorderFailureManager manager(nullptr, root);
        manager.setGraphicsDevice(device);
        Cycle<T>(manager, name);
        const auto resources = device.GetTrackedResourceCount();
        std::vector<std::shared_ptr<System::IDisposable>> owners;
        owners.reserve(16);
        std::array<HeapLedger::Sample, 51> samples{};
        bool allRefused = true, recovered = true, overflow = false;
        {
            HeapLedger ledger;
            for (auto& sample : samples)
            {
                int recorded = 0;
                bool refused = false;
                try
                {
                    auto value = manager.ReadAsset<T>(name, [&](auto owner)
                    {
                        owners.push_back(std::move(owner));
                        if (++recorded == refuseAt) throw std::bad_alloc();
                    });
                }
                catch (const std::bad_alloc&) { refused = true; }
                catch (const ContentLoadException&) { refused = true; }
                allRefused &= refused && recorded == refuseAt;
                // A custom ReadAsset recorder owns the records it accepted, even when it throws.
                for (auto& owner : owners) owner->Dispose();
                owners.clear();
                {
                    auto value = manager.Load<T>(name);
                    recovered &= Inspect(value).semanticObjects > 0;
                }
                manager.Unload();
                sample = ledger.Now();
            }
            overflow = ledger.Overflowed();
        }
        EXPECT_FALSE(overflow);
        EXPECT_TRUE(allRefused) << root << '/' << name << " record " << refuseAt;
        EXPECT_TRUE(recovered);
        EXPECT_EQ(device.GetTrackedResourceCount(), resources);
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            EXPECT_EQ(samples[i].bytes, samples[0].bytes) << name << " cycle " << i;
            EXPECT_EQ(samples[i].count, samples[0].count) << name << " cycle " << i;
        }
        std::printf("SAFE006 XNB recorder-refusal %s/%s at=%d failure+recovery-cycles=51 released=%zu\n",
                    root, name, refuseAt, samples.back().bytes);
    }
}

TEST(ContentResourceFailureTest, Safe006XnbRecorderRefusalAfterResourceConstructionAllowsRecovery)
{
    if (!HeapLedger::Available()) GTEST_SKIP() << "ASan allocator hooks required";
#if !defined(CNA_RENDERER_SOFTWARE)
    GTEST_SKIP() << "SOFTWARE required for this campaign";
#endif
    GraphicsDevice device;
    for (const char* root : {"tests/assets/xnb/cna/windows/uncompressed", "tests/assets/xnb/cna/windows/lzx"})
    {
        RefuseXnbRecording<Model>(device, root, "model_triangle_basiceffect", 2);
        RefuseXnbRecording<Model>(device, root, "model_triangle_basiceffect", 3);
        RefuseXnbRecording<Texture2D>(device, root, "texture2d_color_mips", 1);
        RefuseXnbRecording<TextureCube>(device, root, "texturecube_six_faces", 1);
        RefuseXnbRecording<SoundEffect>(device, root, "soundeffect_pcm16_mono_22050", 1);
    }
}
