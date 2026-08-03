// SPDX-License-Identifier: MS-PL
// SKIA-150: public sampling oracles for cube/volume sampling covering the three input shapes
// SKIA-149's own end-to-end test never exercised (it only used in-memory-generated SetData) --
// a real DXT1-compressed DDS-decoded TextureCube, a real hand-constructed-XNB-decoded Texture3D,
// and a RenderTargetCube drawn to through the public SpriteBatch API and then sampled as a
// cnaSampleCubeEXT source. Also regression-covers SKIA-150's own bind-time volume atlas resource
// budget check (docs/skia-cube-volume-sampling-contract.md's "Resource limits": a Texture3D whose
// plain storage fits 256 MiB can still produce a padded atlas that does not).

#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NotSupportedException.hpp"

#include <any>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    const std::string kMarker = "CNA_SKIA_SKSL_V1";

    const std::string kCubeEffectSksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        uniform float3 cubeDirection;
        half4 main(float2 sourcePixel) {
            return cnaSampleCubeEXT(cubeDirection) * cnaTint;
        }
    )";

    const std::string kVolumeEffectSksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        uniform float3 volumeUvw;
        half4 main(float2 sourcePixel) {
            return cnaSampleVolumeEXT(volumeUvw) * cnaTint;
        }
    )";

    [[nodiscard]] bool SamePixel(Color a, Color b) noexcept
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    void AppendU32LE(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }

    void AppendU16LE(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    // Packs an 8-bit RGB colour into RGB565 (DXT1's own colour endpoint format). 0/255 channel
    // values round-trip exactly through the standard bit-replication expansion decoders use
    // (e.g. r5=31 -> (31<<3)|(31>>2) = 255), so every face colour below is chosen from pure
    // 0/255 combinations to allow exact-equality assertions rather than a tolerance band.
    [[nodiscard]] std::uint16_t ToRgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::uint16_t r5 = static_cast<std::uint16_t>(r >> 3);
        const std::uint16_t g6 = static_cast<std::uint16_t>(g >> 2);
        const std::uint16_t b5 = static_cast<std::uint16_t>(b >> 3);
        return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
    }

    // A single solid-colour DXT1 4x4 block: colour0 == colour1, every 2-bit index == 0b00.
    void AppendSolidDxt1Block(std::vector<std::uint8_t>& out, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::uint16_t c = ToRgb565(r, g, b);
        AppendU16LE(out, c);
        AppendU16LE(out, c);
        AppendU32LE(out, 0);
    }

    constexpr std::uint32_t kDdsMagic        = 0x20534444;
    constexpr std::uint32_t kDdsHeaderSize   = 124;
    constexpr std::uint32_t kDdsdCaps        = 0x1;
    constexpr std::uint32_t kDdsdHeight      = 0x2;
    constexpr std::uint32_t kDdsdWidth       = 0x4;
    constexpr std::uint32_t kDdsdPixelFmt    = 0x1000;
    constexpr std::uint32_t kDdpfFourCC      = 0x4;
    constexpr std::uint32_t kFourCcDxt1      = 0x31545844;
    constexpr std::uint32_t kDdscapsTexture  = 0x1000;
    constexpr std::uint32_t kDdscapsComplex  = 0x8;
    constexpr std::uint32_t kDdscaps2Cubemap = 0x200;
    constexpr std::uint32_t kDdscaps2AllFaces = 0xFC00;

    struct FaceSample final { int face; float dx, dy, dz; int r, g, b; const char* label; };

    // Face order matches DDSFromStreamEXT's own face loop / ITextureCubeBackend::SetData/CubeMapFace
    // (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z), and SkiaCubeSampling.hpp's dominant-axis face-centre directions.
    constexpr std::array<FaceSample, 6> kFaces{{
        {0, 1.0f, 0.0f, 0.0f, 255, 0, 0, "+X decodes to Red"},
        {1, -1.0f, 0.0f, 0.0f, 0, 255, 255, "-X decodes to Cyan"},
        {2, 0.0f, 1.0f, 0.0f, 0, 255, 0, "+Y decodes to Green"},
        {3, 0.0f, -1.0f, 0.0f, 255, 0, 255, "-Y decodes to Magenta"},
        {4, 0.0f, 0.0f, 1.0f, 0, 0, 255, "+Z decodes to Blue"},
        {5, 0.0f, 0.0f, -1.0f, 255, 255, 0, "-Z decodes to Yellow"},
    }};

    // Builds a real, minimal, single-mip DXT1 DDS cubemap: 4x4 texels/face, one distinct solid
    // colour per face, matching kFaces above.
    [[nodiscard]] std::vector<std::uint8_t> BuildDxt1Cubemap()
    {
        std::vector<std::uint8_t> out;
        out.reserve(128 + 6 * 8);

        AppendU32LE(out, kDdsMagic);
        AppendU32LE(out, kDdsHeaderSize);
        AppendU32LE(out, kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFmt);
        AppendU32LE(out, 4);
        AppendU32LE(out, 4);
        AppendU32LE(out, 8);
        AppendU32LE(out, 0);
        AppendU32LE(out, 1);
        for (int i = 0; i < 11; ++i) AppendU32LE(out, 0);

        AppendU32LE(out, 32);
        AppendU32LE(out, kDdpfFourCC);
        AppendU32LE(out, kFourCcDxt1);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);

        AppendU32LE(out, kDdscapsTexture | kDdscapsComplex);
        AppendU32LE(out, kDdscaps2Cubemap | kDdscaps2AllFaces);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);
        AppendU32LE(out, 0);

        for (const FaceSample& f : kFaces)
            AppendSolidDxt1Block(out, f.r, f.g, f.b);

        return out;
    }
}

class SkiaCubeVolumeSamplingOracleTest final : public Game
{
public:
    SkiaCubeVolumeSamplingOracleTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(4);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        primary_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        primary_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        CheckCubeContentLoaded();
        CheckCubeTargetProduced();
        CheckVolumeContentLoaded();
        CheckVolumeAtlasBudgetRejection();

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    // -- CUBE: content-loaded (real DXT1 DDS decode) --------------------------------------------
    void CheckCubeContentLoaded()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> ddsBytes = BuildDxt1Cubemap();
        System::IO::MemoryStream ddsStream(
            ddsBytes.data(), static_cast<System::IO::intcs>(ddsBytes.size()));
        TextureCube cube = TextureCube::DDSFromStreamEXT(device, ddsStream);
        Check(cube.getSizeProperty() == 4, "content-loaded DDS cube decodes to the encoded 4x4 face size");

        ShaderEffect effect(device, kMarker, kCubeEffectSksl);
        Check(effect.IsEffectValid(), "cube-sampling effect compiles for the content-loaded check");
        effect.SetTexture(1, cube);

        bool allSampledMatch = true;
        bool allTransferMatch = true;
        for (const FaceSample& f : kFaces)
        {
            const Color sampled = SampleDirection(effect, f.dx, f.dy, f.dz);
            if (!SamePixel(sampled, Color(f.r, f.g, f.b, 255))) allSampledMatch = false;

            Color transferred = Color::Transparent;
            const Rectangle onePixel(0, 0, 1, 1);
            cube.GetData(static_cast<CubeMapFace>(f.face), 0, &onePixel, &transferred, 0, 1);
            if (!SamePixel(sampled, transferred)) allTransferMatch = false;
        }
        Check(allSampledMatch,
              "cnaSampleCubeEXT samples every face of a real DXT1-decoded content-loaded "
              "TextureCube to its exact encoded colour");
        Check(allTransferMatch,
              "sampled pixels agree exactly with GetData's own CPU transfer readback for the "
              "content-loaded cube -- the sampling path and the transfer path see the same bytes");
    }

    // -- CUBE: target-produced (drawn through the public SpriteBatch/SetRenderTarget API) ------
    void CheckCubeTargetProduced()
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTargetCube target(device, 4, false, SurfaceFormat::Color, DepthFormat::None);

        for (const FaceSample& f : kFaces)
            DrawSolidFaceEXT(target, static_cast<CubeMapFace>(f.face), Color(f.r, f.g, f.b, 255));

        ShaderEffect effect(device, kMarker, kCubeEffectSksl);
        Check(effect.IsEffectValid(), "cube-sampling effect compiles for the target-produced check");
        effect.SetTexture(1, target);

        bool allSampledMatch = true;
        bool allTransferMatch = true;
        for (const FaceSample& f : kFaces)
        {
            const Color sampled = SampleDirection(effect, f.dx, f.dy, f.dz);
            if (!SamePixel(sampled, Color(f.r, f.g, f.b, 255))) allSampledMatch = false;

            Color transferred = Color::Transparent;
            const Rectangle onePixel(0, 0, 1, 1);
            target.GetData(static_cast<CubeMapFace>(f.face), 0, &onePixel, &transferred, 0, 1);
            if (!SamePixel(sampled, transferred)) allTransferMatch = false;
        }
        Check(allSampledMatch,
              "cnaSampleCubeEXT samples every face of a RenderTargetCube drawn through the real "
              "public SpriteBatch/SetRenderTarget API to its exact drawn colour");
        Check(allTransferMatch,
              "sampled pixels agree exactly with GetData's own CPU transfer readback for the "
              "target-produced cube");

        // Live-reference semantics through a RenderTargetCube specifically (SKIA-149's own test
        // only proved this for a plain TextureCube): a face redraw after SetTexture but before
        // the next draw must be visible, matching SKIA-146's target-as-source contract.
        DrawSolidFaceEXT(target, CubeMapFace::PositiveX, Color(10, 20, 30, 255));
        const Color updated = SampleDirection(effect, 1.0f, 0.0f, 0.0f);
        Check(SamePixel(updated, Color(10, 20, 30, 255)),
              "cube sampling observes a RenderTargetCube face redraw issued after SetTexture but "
              "before the draw, never a stale snapshot");
    }

    // -- VOLUME: content-loaded (real hand-constructed-XNB Texture3DReader decode) --------------
    void CheckVolumeContentLoaded()
    {
        auto& device = getGraphicsDeviceProperty();
        CNA::Internal::Xnb::RegisterTexture3DXnbReader();

        constexpr std::int32_t width = 2, height = 2, depth = 2, levelCount = 1;
        const std::vector<Color> voxels{
            Color(80, 40, 20, 255), Color(80, 40, 20, 255),
            Color(80, 40, 20, 255), Color(80, 40, 20, 255),
            Color(220, 180, 140, 255), Color(220, 180, 140, 255),
            Color(220, 180, 140, 255), Color(220, 180, 140, 255),
        };

        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(static_cast<std::int32_t>(SurfaceFormat::Color));
        writer.Write(width);
        writer.Write(height);
        writer.Write(depth);
        writer.Write(levelCount);
        writer.Write(static_cast<std::int32_t>(voxels.size() * 4));
        for (const Color& c : voxels)
        {
            writer.Write(c.getRProperty());
            writer.Write(c.getGProperty());
            writer.Write(c.getBProperty());
            writer.Write(c.getAProperty());
        }
        writer.Flush();
        const auto buf = ms.ToArray();

        System::IO::MemoryStream body(buf.data(), static_cast<System::IO::intcs>(buf.size()));
        ContentReader reader(&getContentProperty(), &body, "cube-volume-oracle", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture3DReader");
        Check(typeReader != nullptr, "Texture3DReader is registered and constructible");
        if (!typeReader) return;

        std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
        auto loaded = std::any_cast<std::shared_ptr<Texture3D>>(resultAny);
        Check(loaded != nullptr, "content-loaded XNB volume decodes to a real Texture3D");
        if (!loaded) return;

        ShaderEffect effect(device, kMarker, kVolumeEffectSksl);
        Check(effect.IsEffectValid(), "volume-sampling effect compiles for the content-loaded check");
        effect.SetTexture(1, *loaded);

        const Color firstSlice = SampleVolume(effect, 0.5f, 0.5f, 0.0f);
        const Color lastSlice = SampleVolume(effect, 0.5f, 0.5f, 1.0f);
        Check(SamePixel(firstSlice, Color(80, 40, 20, 255))
                  && SamePixel(lastSlice, Color(220, 180, 140, 255)),
              "cnaSampleVolumeEXT samples both slices of a real content-loaded Texture3D to their "
              "exact encoded colours");

        Color transferredFirst = Color::Transparent;
        Color transferredLast = Color::Transparent;
        // GetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount).
        loaded->GetData(0, 0, 0, 1, 1, 0, 1, &transferredFirst, 0, 1);
        loaded->GetData(0, 0, 0, 1, 1, 1, 2, &transferredLast, 0, 1);
        Check(SamePixel(firstSlice, transferredFirst) && SamePixel(lastSlice, transferredLast),
              "sampled pixels agree exactly with GetData's own CPU transfer readback for the "
              "content-loaded volume");
    }

    // -- VOLUME: resource-limit rejection (SKIA-150's new BindTexture3D atlas budget check) -----
    void CheckVolumeAtlasBudgetRejection()
    {
        auto& device = getGraphicsDeviceProperty();
        // 64x64x16000 Color: plain storage = 64*64*16000*4 = 262,144,000 bytes (~250.0 MiB, fits
        // under the 256 MiB per-resource ceiling with headroom, so construction succeeds). Its
        // padded grid atlas (docs/skia-cube-volume-sampling-contract.md) packs a 66x66 tile
        // (64+2 border) over a 127x126 grid -> 8382x8316 -> ~278.8 MiB, comfortably exceeding the
        // same 256 MiB ceiling -- the exact "plain storage fits, atlas does not" case the contract
        // doc's Resource limits section requires SetTexture(1, Texture3D) to reject transactionally.
        Texture3D huge(device, 64, 64, 16000, false, SurfaceFormat::Color);

        ShaderEffect effect(device, kMarker, kVolumeEffectSksl);
        Check(effect.IsEffectValid(), "volume-sampling effect compiles for the budget-rejection check");

        bool threw = false;
        std::string message;
        try
        {
            effect.SetTexture(1, huge);
        }
        catch (const System::NotSupportedException& error)
        {
            threw = true;
            message = error.what();
        }
        catch (...)
        {
        }
        Check(threw && message.find("256 MiB") != std::string::npos,
              "SetTexture(1, Texture3D) rejects a volume whose plain storage fits budget but "
              "whose padded sampling atlas does not, naming the 256 MiB per-resource limit");
    }

    // -- Shared draw/sample helpers ---------------------------------------------------------------
    void DrawSolidFaceEXT(RenderTargetCube& target, CubeMapFace face, Color colour)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(&target, face);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        spriteBatch_->Draw(*primary_, Rectangle(0, 0, 4, 4), colour);
        spriteBatch_->End();
        device.SetRenderTargets({});
    }

    [[nodiscard]] Color SampleDirection(ShaderEffect& effect, float x, float y, float z)
    {
        effect.SetUniformVec3("cubeDirection", x, y, z);
        return DrawAndReadback(effect);
    }

    [[nodiscard]] Color SampleVolume(ShaderEffect& effect, float u, float v, float w)
    {
        effect.SetUniformVec3("volumeUvw", u, v, w);
        return DrawAndReadback(effect);
    }

    [[nodiscard]] Color DrawAndReadback(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            &effect, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*primary_, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        Color pixel = Color::Transparent;
        const Rectangle region(2, 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> primary_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaCubeVolumeSamplingOracleTest game;
    game.Run();
    return game.Result();
}
