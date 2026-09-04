// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-65: the IGL renderer's surface-format transfers, against a real device.
//
// This test drives `IGraphicsRenderer` directly rather than `Texture2D`, and that is deliberate:
// the shared `Texture::ValidateFormat` still admits only `SurfaceFormat::Color` for every renderer
// except Skia, so no public XNA texture under IGL is anything else today. The renderer contract
// carries no such restriction -- `ImageData::surfaceFormat` and `CreateRenderTarget2DEXT`'s
// `surfaceFormat` argument are raw ordinals -- so the contract layer is where a wrong row pitch is
// a real out-of-bounds read rather than a hypothetical one.
//
// Check A -- a `RenderTarget2D` of each supported byte class (1, 2, 4, 8 and 16 bytes per texel)
//   survives an upload/readback round trip with its bytes intact. This is the discriminating
//   check: with the old `width * 4` row pitch a 16-byte texel's rows were sliced to a quarter of
//   their length and a 1-byte texel's upload read four times its own buffer. The row ORDER is now
//   the same on both backends (IGL-67) -- see the note inside CheckRoundTrip.
// Check B -- `CreateTexture` sizes a non-`Color` `ImageData` from that image's own format, and
//   refuses an image whose pixel vector is too short for it rather than reading past its end.
// Check C -- a format IGL cannot represent (`Rgba64`, whose 8-byte R16G16B16A16 texel has no IGL
//   counterpart) is refused by name rather than silently substituted with another layout.
// Check D -- `GetData` refuses a destination buffer sized for `Color` when the target is wider,
//   instead of overrunning it.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Igl/IglSurfaceFormats.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Internal::Renderers::Igl::FormatRegionByteCount;
using CNA::Internal::Renderers::Igl::FormatRowByteCount;
using CNA::Internal::Renderers::Igl::FormatUnitByteCount;
using CNA::Internal::Renderers::Igl::GetSurfaceFormatName;

namespace
{
    constexpr int kSize = 64;
    constexpr int kTargetWidth = 8;
    constexpr int kTargetHeight = 4;

    /// A deterministic, position-dependent byte pattern: a row-pitch error shifts it, and a
    /// texel-size error truncates it, so a byte comparison catches both.
    [[nodiscard]] std::vector<std::uint8_t> MakePattern(const int surfaceFormat, const int width,
                                                        const int height)
    {
        const int unitBytes = FormatUnitByteCount(surfaceFormat);
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(FormatRegionByteCount(surfaceFormat, width, height)));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                for (int byte = 0; byte < unitBytes; ++byte)
                {
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * width + x) * unitBytes + byte;
                    // Kept away from the exponent bits of the float formats: a payload that
                    // happened to encode NaN could legitimately read back as a different NaN.
                    bytes[offset] = static_cast<std::uint8_t>((x * 7 + y * 3 + byte * 11) & 0x3F);
                }
            }
        }
        return bytes;
    }


}

class IglSurfaceFormatTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// One upload/readback round trip through the renderer contract, in one surface format.
    void CheckRoundTrip(CNA::Internal::Renderers::IGraphicsRenderer& renderer,
                        const SurfaceFormat format)
    {
        const int ordinal = static_cast<int>(format);
        const std::string name = GetSurfaceFormatName(ordinal);
        const std::string label = name + " (" + std::to_string(FormatUnitByteCount(ordinal)) +
                                  " bytes/texel) survives an upload/readback round trip";

        std::unique_ptr<CNA::Internal::Renderers::IRenderTargetRenderer> target;
        try
        {
            target = renderer.CreateRenderTarget2DEXT(
                kTargetWidth, kTargetHeight, static_cast<int>(DepthFormat::None),
                /*preserveContents=*/true, /*mipMap=*/false, /*multiSampleCount=*/1, ordinal);
        }
        catch (const std::exception& e)
        {
            ExpectTrue(label.c_str(), false);
            std::printf("      the device refused a %s render target: %s\n", name.c_str(),
                        e.what());
            return;
        }
        if (!ExpectTrue((name + ": the render target was created").c_str(), target != nullptr))
            return;

        const std::vector<std::uint8_t> uploaded =
            MakePattern(ordinal, kTargetWidth, kTargetHeight);
        target->UpdatePixels(uploaded.data(), FormatRowByteCount(ordinal, kTargetWidth));

        std::vector<std::uint8_t> read(uploaded.size(), 0xCD);
        const bool readBack = target->GetData(0, 0, 0, kTargetWidth, kTargetHeight, read.data(),
                                              static_cast<int>(read.size()));
        if (!ExpectTrue((name + ": GetData read the whole region back").c_str(), readBack))
            return;

        // plans/plan_igl.md IGL-67, now closed: this expectation used to be flipped on Vulkan, because
        // igl::vulkan::Framebuffer::copyBytesColorAttachment reverses the rows of every rectangle
        // it copies while its OpenGL counterpart reverses none. The renderer now undoes exactly
        // that one flip (IglRenderTargetRenderer::UndoVulkanReadbackRowFlip), so both backends owe
        // the caller the rows it uploaded, in the order it uploaded them -- which is what XNA says
        // and what the sampler was already doing.
        const std::vector<std::uint8_t>& expected = uploaded;

        std::size_t firstMismatch = expected.size();
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            if (expected[i] != read[i])
            {
                firstMismatch = i;
                break;
            }
        }
        ExpectTrue(label.c_str(), firstMismatch == expected.size());
        if (firstMismatch != expected.size())
        {
            std::printf("      first differing byte at %zu of %zu: expected 0x%02X, read 0x%02X\n",
                        firstMismatch, expected.size(),
                        static_cast<unsigned>(expected[firstMismatch]),
                        static_cast<unsigned>(read[firstMismatch]));
        }
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();

        // ---- Check A: one round trip per byte class ------------------------------------------
        CheckRoundTrip(renderer, SurfaceFormat::ByteEXT);      // 1 byte
        CheckRoundTrip(renderer, SurfaceFormat::HalfSingle);   // 2 bytes
        CheckRoundTrip(renderer, SurfaceFormat::Color);        // 4 bytes
        CheckRoundTrip(renderer, SurfaceFormat::Vector2);      // 8 bytes
        CheckRoundTrip(renderer, SurfaceFormat::Vector4);      // 16 bytes

        // ---- Check B: CreateTexture sizes an image from its own format ------------------------
        {
            ImageData image;
            image.width = kTargetWidth;
            image.height = kTargetHeight;
            image.mipLevels = 1;
            image.surfaceFormat = static_cast<int>(SurfaceFormat::HalfVector4);
            image.pixels = MakePattern(image.surfaceFormat, image.width, image.height);
            // Under the old `width * 4` row pitch this upload read 8 bytes per texel out of a
            // buffer strided at 4, running past the end of the vector.
            bool created = false;
            try
            {
                const std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> texture =
                    renderer.CreateTexture(image);
                created = texture != nullptr;
            }
            catch (const std::exception& e)
            {
                std::printf("      CreateTexture threw for HalfVector4: %s\n", e.what());
            }
            ExpectTrue("CreateTexture accepts an 8-byte-per-texel image sized from its own format",
                       created);
        }
        {
            ImageData image;
            image.width = kTargetWidth;
            image.height = kTargetHeight;
            image.mipLevels = 1;
            image.surfaceFormat = static_cast<int>(SurfaceFormat::Vector4);
            // One texel short of a full level: an implementation deriving the size from
            // `width * 4` would read past the end of this vector instead of refusing.
            image.pixels = MakePattern(image.surfaceFormat, image.width, image.height);
            image.pixels.resize(image.pixels.size() - static_cast<std::size_t>(
                                                          FormatUnitByteCount(image.surfaceFormat)));
            bool refused = false;
            try
            {
                (void)renderer.CreateTexture(image);
            }
            catch (const std::exception&)
            {
                refused = true;
            }
            ExpectTrue("CreateTexture refuses an image too short for its own format", refused);
        }

        // ---- Check C: an unrepresentable format is refused by name ----------------------------
        {
            std::string message;
            bool refused = false;
            try
            {
                ImageData image;
                image.width = 4;
                image.height = 4;
                image.mipLevels = 1;
                image.surfaceFormat = static_cast<int>(SurfaceFormat::Rgba64);
                image.pixels = MakePattern(image.surfaceFormat, image.width, image.height);
                (void)renderer.CreateTexture(image);
            }
            catch (const std::exception& e)
            {
                refused = true;
                message = e.what();
            }
            ExpectTrue("Rgba64 is refused rather than substituted with another texel layout",
                       refused);
            ExpectTrue("the Rgba64 refusal names the format",
                       message.find("Rgba64") != std::string::npos);
        }

        // ---- Check D: a too-small readback buffer is refused ----------------------------------
        {
            std::unique_ptr<CNA::Internal::Renderers::IRenderTargetRenderer> wide;
            try
            {
                wide = renderer.CreateRenderTarget2DEXT(
                    kTargetWidth, kTargetHeight, static_cast<int>(DepthFormat::None), true, false,
                    1, static_cast<int>(SurfaceFormat::Vector4));
            }
            catch (const std::exception&)
            {
                wide.reset();
            }
            if (wide != nullptr)
            {
                // Exactly what a `w * h * 4` guard would have accepted, and then overrun by 12
                // bytes per texel.
                std::vector<std::uint8_t> tooSmall(
                    static_cast<std::size_t>(kTargetWidth * kTargetHeight * 4), 0);
                const bool accepted = wide->GetData(0, 0, 0, kTargetWidth, kTargetHeight,
                                                    tooSmall.data(),
                                                    static_cast<int>(tooSmall.size()));
                ExpectTrue("GetData refuses a Color-sized buffer for a 16-byte-per-texel target",
                           !accepted);
            }
            else
            {
                ExpectTrue("a Vector4 render target could be created for the readback-guard check",
                           false);
            }
        }

        // The frame still has to present something, so the harness's own readback path stays
        // exercised alongside the contract-level checks above.
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(128), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        ExpectPixel("the back buffer still presents normally after the format checks",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(128), static_cast<bytecs>(0),
                          static_cast<bytecs>(255)),
                    /*tolerance=*/4);
    }

public:
    IglSurfaceFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglSurfaceFormatTest>();
}
