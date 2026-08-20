// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-19: end-to-end proof that block-compressed textures reach the GPU intact,
// and that the driver-capability gates around them refuse rather than mis-size.
//
// This test drives `IGraphicsRenderer` directly rather than `Texture2D`, and that is deliberate:
// the shared `Texture::ValidateFormat` still admits only `SurfaceFormat::Color` for every renderer
// except Skia, so no public XNA texture under FNA3D is compressed today. The renderer contract
// carries no such restriction -- `ImageData::surfaceFormat` is a raw SurfaceFormat ordinal that
// reaches `CreateTexture` unfiltered -- so the contract layer is where compressed transfers are
// genuinely reachable, and where sizing them as `width * height * 4` is a real out-of-bounds read
// rather than a hypothetical one.
//
// Check A -- the driver's own compressed-format support and sampler limits are reported, and the
//   whole DXT5 section is skipped truthfully if the driver has no S3TC.
// Check B -- the decoded texture samples as the four block colours in the right quadrants. This is
//   the primary oracle: it proves the blocks reached the GPU sized correctly AND landed in the
//   right block positions, which a byte comparison alone cannot (a transposed grid compares equal).
// Check C -- an NPOT 6x6 level, whose partial tail blocks are the case a naive count gets wrong,
//   decodes correctly too.
// Check D -- GetData answers honestly for the driver in hand: FNA3D's OpenGL and Direct3D 11
//   drivers refuse compressed readback outright, so the renderer must report that it read nothing
//   rather than hand back an untouched buffer; where the driver does read back (SDL_GPU), the
//   bytes must match exactly.
// Check E -- a compressed TextureCube is refused by name rather than mis-sized.
// Check F -- binding past the driver's real sampler-slot ceiling is refused by name.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Internal::Renderers::Fna3d::Fna3dRenderer;
using CNA::Internal::Renderers::Fna3d::FormatRegionByteCount;
using CNA::Internal::Renderers::Fna3d::FormatRowByteCount;

namespace
{
    // One opaque, single-colour DXT5 block: eight alpha bytes (a0 = a1 = 255, all indices 0) then
    // eight colour bytes (c0 = the colour in RGB565, c1 = 0, all indices 0 -> every texel picks
    // c0). c0 > c1 keeps the block in four-colour mode, so index 0 is exactly c0.
    void AppendSolidDxt5Block(std::vector<std::uint8_t>& out, std::uint16_t rgb565)
    {
        out.insert(out.end(), { 0xFF, 0xFF, 0, 0, 0, 0, 0, 0 });
        out.push_back(static_cast<std::uint8_t>(rgb565 & 0xFF));
        out.push_back(static_cast<std::uint8_t>(rgb565 >> 8));
        out.insert(out.end(), { 0x00, 0x00, 0, 0, 0, 0 });
    }

    constexpr std::uint16_t kRed565 = 0xF800;
    constexpr std::uint16_t kGreen565 = 0x07E0;
    constexpr std::uint16_t kBlue565 = 0x001F;
    constexpr std::uint16_t kWhite565 = 0xFFFF;

    /// 8x8 DXT5 = 2x2 blocks: red / green on the top row, blue / white on the bottom.
    std::vector<std::uint8_t> QuadrantDxt5()
    {
        std::vector<std::uint8_t> blocks;
        blocks.reserve(4 * 16);
        AppendSolidDxt5Block(blocks, kRed565);
        AppendSolidDxt5Block(blocks, kGreen565);
        AppendSolidDxt5Block(blocks, kBlue565);
        AppendSolidDxt5Block(blocks, kWhite565);
        return blocks;
    }

    ImageData CompressedImage(int width, int height, std::vector<std::uint8_t> blocks)
    {
        ImageData image;
        image.width = width;
        image.height = height;
        image.pixels = std::move(blocks);
        image.mipLevels = 1;
        image.surfaceFormat = static_cast<int>(SurfaceFormat::Dxt5);
        return image;
    }
}

class Fna3dCompressedTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        auto* fna3d = dynamic_cast<Fna3dRenderer*>(&renderer);
        if (!Check(fna3d != nullptr, "the active renderer is Fna3dRenderer"))
        {
            return;
        }

        const int dxt5 = static_cast<int>(SurfaceFormat::Dxt5);

        // Check A -- the driver's own answer, and the format gate that follows from it.
        const bool supportsDxt5 = fna3d->SupportsTextureFormatEXT(dxt5);
        std::printf("[info] driver compressed support: DXT5 %s, texture slots %d (vertex %d)\n",
                    supportsDxt5 ? "yes" : "no", fna3d->GetMaxTextureSlotsEXT(),
                    fna3d->GetMaxVertexTextureSlotsEXT());
        Check(fna3d->SupportsTextureFormatEXT(static_cast<int>(SurfaceFormat::Color)),
              "an uncompressed format is always supported");
        Check(fna3d->GetMaxTextureSlotsEXT() > 0,
              "the driver reports a real fragment texture-slot count");

        if (!supportsDxt5)
        {
            // Truthful skip: the rest of this section measures DXT5 specifically, and asserting it
            // on a driver with no S3TC would be measuring nothing.
            std::printf("[SKIP] this driver reports no S3TC support; DXT5 checks not run\n");
        }
        else
        {
            const std::vector<std::uint8_t> blocks = QuadrantDxt5();
            Check(static_cast<int>(blocks.size()) == FormatRegionByteCount(dxt5, 8, 8),
                  "the hand-built DXT5 level is exactly the byte count the renderer computes");
            Check(FormatRowByteCount(dxt5, 8) == 2 * 16,
                  "a DXT5 row pitch is one block row, not one texel row");

            std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> compressed =
                renderer.CreateTexture(CompressedImage(8, 8, blocks));
            if (Check(compressed != nullptr, "the renderer creates a DXT5 texture"))
            {
                // Check B -- the blocks decode where they belong. A mis-sized upload cannot get
                // here: it would read past the end of `blocks` and store the wrong bytes.
                auto batch = renderer.CreateSpriteBatch();
                if (Check(batch != nullptr, "the renderer creates a sprite batch"))
                {
                    device.Clear(Color(0, 0, 0, 255));
                    batch->SetSamplerFilter(1); // Point: sample the block colour, not a blend of it
                    batch->Begin();
                    batch->Draw(*compressed, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 8, 8),
                                Color::White);
                    batch->End();
                    ExpectPixel("DXT5 block (0,0) decodes to red", Rectangle(16, 16, 1, 1),
                                Color(255, 0, 0, 255), 8);
                    ExpectPixel("DXT5 block (1,0) decodes to green", Rectangle(48, 16, 1, 1),
                                Color(0, 255, 0, 255), 8);
                    ExpectPixel("DXT5 block (0,1) decodes to blue", Rectangle(16, 48, 1, 1),
                                Color(0, 0, 255, 255), 8);
                    ExpectPixel("DXT5 block (1,1) decodes to white", Rectangle(48, 48, 1, 1),
                                Color(255, 255, 255, 255), 8);
                }
            }

            // Check C -- the NPOT padded-tail case. 6x6 still occupies 2x2 whole blocks; a count
            // that dropped the partial tail would short the upload by a whole block row, and the
            // bottom-right quadrant would sample as something other than the white block.
            Check(FormatRegionByteCount(dxt5, 6, 6) == 4 * 16,
                  "a 6x6 DXT5 level still occupies four whole blocks");
            std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> npot =
                renderer.CreateTexture(CompressedImage(6, 6, blocks));
            if (Check(npot != nullptr, "the renderer creates an NPOT DXT5 texture"))
            {
                auto batch = renderer.CreateSpriteBatch();
                if (batch != nullptr)
                {
                    device.Clear(Color(0, 0, 0, 255));
                    batch->SetSamplerFilter(1);
                    batch->Begin();
                    batch->Draw(*npot, Rectangle(0, 0, 60, 60), Rectangle(0, 0, 6, 6),
                                Color::White);
                    batch->End();
                    // 6 texels across 60 pixels = 10 pixels per texel; texels 0-3 are the first
                    // block column, 4-5 the padded tail block.
                    ExpectPixel("NPOT block (0,0) decodes to red", Rectangle(15, 15, 1, 1),
                                Color(255, 0, 0, 255), 8);
                    ExpectPixel("NPOT tail block (1,0) decodes to green", Rectangle(45, 15, 1, 1),
                                Color(0, 255, 0, 255), 8);
                    ExpectPixel("NPOT tail block (0,1) decodes to blue", Rectangle(15, 45, 1, 1),
                                Color(0, 0, 255, 255), 8);
                    ExpectPixel("NPOT tail block (1,1) decodes to white", Rectangle(45, 45, 1, 1),
                                Color(255, 255, 255, 255), 8);
                }

                // Check D -- readback answers for the driver actually running. FNA3D's OpenGL and
                // D3D11 drivers refuse compressed GetTextureData2D and write nothing; the renderer
                // must say so instead of reporting an untouched buffer as a successful read.
                std::vector<std::uint8_t> readback(blocks.size(), 0xCD);
                const bool readOk = npot->GetData(0, 0, 0, 6, 6, readback.data(),
                                                  static_cast<int>(readback.size()));
                Check(readOk == fna3d->SupportsCompressedReadbackEXT(),
                      "GetData reports exactly what the driver's compressed readback can do");
                if (readOk)
                {
                    Check(readback == blocks,
                          "a DXT5 level round-trips byte for byte where the driver reads it back");
                }
                else
                {
                    Check(readback == std::vector<std::uint8_t>(blocks.size(), 0xCD),
                          "a refused compressed readback leaves the destination untouched");
                    std::printf("[info] this driver has no compressed readback; the byte "
                                "round-trip is not measurable here\n");
                }
            }
        }

        // Check E -- cube maps are RGBA8 in CNA's renderer contract; a compressed request must be
        // refused by name rather than mis-sized.
        {
            bool refused = false;
            std::string message;
            try
            {
                (void) renderer.CreateTextureCube(8, false, dxt5);
            }
            catch (const std::runtime_error& error)
            {
                refused = true;
                message = error.what();
            }
            Check(refused, "a compressed TextureCube is refused, not mis-sized");
            Check(message.find("block-compressed") != std::string::npos,
                  "the cube refusal names the reason");

            // A render target is written by the GPU; no driver renders into compressed blocks.
            refused = false;
            try
            {
                (void) renderer.CreateRenderTarget2DEXT(8, 8, 0, false, false, 0, dxt5);
            }
            catch (const std::runtime_error&)
            {
                refused = true;
            }
            Check(refused, "a compressed RenderTarget2D is refused too");
        }

        // Check F -- a bind past the driver's real sampler ceiling samples nothing, so it is
        // refused rather than silently accepted.
        {
            const int beyond = fna3d->GetMaxTextureSlotsEXT();
            bool refused = false;
            std::string message;
            try
            {
                fna3d->BindEffectTextureEXT(beyond, nullptr);
            }
            catch (const std::runtime_error& error)
            {
                refused = true;
                message = error.what();
            }
            Check(refused, "binding past the driver's texture-slot ceiling is refused");
            Check(message.find("sampler slot") != std::string::npos,
                  "the slot refusal names the slot and the ceiling");
            // A slot the device does have must still work, so the refusal is a real boundary
            // rather than a blanket rejection.
            bool acceptedInRange = true;
            try
            {
                fna3d->BindEffectTextureEXT(0, nullptr);
            }
            catch (const std::runtime_error&)
            {
                acceptedInRange = false;
            }
            Check(acceptedInRange, "slot 0 is still accepted");
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dCompressedTest>();
}
