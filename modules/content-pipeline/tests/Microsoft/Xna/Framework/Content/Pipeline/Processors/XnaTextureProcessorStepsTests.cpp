// SPDX-License-Identifier: MS-PL
//
// TQ-034: XNA's TextureProcessor.Process step for step -- what it validates, the type it keys and
// premultiplies in, the order of premultiply and resize, the type a resize keeps, and the volume it
// will not compress. Expectations are the genuine processor's answers, run in process under Wine
// over the same hand-built content (TQ-034 lane oracle, cases texture/*), read against its IL
// (Microsoft.Xna.Framework.Content.Pipeline.dll, TextureProcessor.Process and
// TextureProcessorUtils).
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "XnaProcessorTestContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChain.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace
{
    namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity;
    using Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessor;
    using Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat;
    using CNA::Tests::XnaProcessors::RecordingContext;
    using CNA::Tests::XnaProcessors::RefusalOf;
    using CNA::Tests::XnaProcessors::Spell;

    std::shared_ptr<Graphics::Texture2DContent> Single(const std::shared_ptr<Graphics::BitmapContent>& bitmap)
    {
        auto texture = std::make_shared<Graphics::Texture2DContent>();
        texture->getMipmapsProperty().Add(bitmap);
        return texture;
    }

    std::shared_ptr<Graphics::PixelBitmapContent<Color>> Colours(int width, int height, const std::vector<Color>& pixels)
    {
        auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Color>>(width, height);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            bitmap->SetPixel(static_cast<int>(i) % width, static_cast<int>(i) / width, pixels[i]);
        }
        return bitmap;
    }

    std::shared_ptr<Graphics::PixelBitmapContent<Vector4>> Vectors(int width, int height, const std::vector<Vector4>& pixels)
    {
        auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Vector4>>(width, height);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            bitmap->SetPixel(static_cast<int>(i) % width, static_cast<int>(i) / width, pixels[i]);
        }
        return bitmap;
    }

    std::shared_ptr<Graphics::PixelBitmapContent<Color>> Opaque4x4(int seed)
    {
        auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Color>>(4, 4);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                bitmap->SetPixel(x, y, Color(x * 60 + seed, y * 60, 30, 255));
            }
        }
        return bitmap;
    }

    const std::shared_ptr<Graphics::BitmapContent>& Level(Graphics::TextureContent& texture, int face, int level)
    {
        const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<Graphics::MipmapChain>>&>(texture.getFacesProperty());
        return static_cast<const System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::BitmapContent>>&>(
            *faces[face])[level];
    }

    void ExpectColoursWithin(const Graphics::BitmapContent& bitmap, const std::vector<Color>& xna, int tolerance,
                             const std::string& label)
    {
        const auto* colours = dynamic_cast<const Graphics::PixelBitmapContent<Color>*>(&bitmap);
        ASSERT_NE(colours, nullptr) << label << ": not a Color bitmap";
        ASSERT_EQ(colours->getWidthProperty() * colours->getHeightProperty(), static_cast<int>(xna.size())) << label;
        for (std::size_t i = 0; i < xna.size(); ++i)
        {
            const Color got = colours->GetPixel(static_cast<int>(i) % colours->getWidthProperty(),
                                                static_cast<int>(i) / colours->getWidthProperty());
            EXPECT_NEAR(got.getRProperty(), xna[i].getRProperty(), tolerance) << label << " pixel " << i;
            EXPECT_NEAR(got.getGProperty(), xna[i].getGProperty(), tolerance) << label << " pixel " << i;
            EXPECT_NEAR(got.getBProperty(), xna[i].getBProperty(), tolerance) << label << " pixel " << i;
            EXPECT_NEAR(got.getAProperty(), xna[i].getAProperty(), tolerance) << label << " pixel " << i;
        }
    }

    void ExpectVectors(const Graphics::BitmapContent& bitmap, const std::vector<Vector4>& xna, const std::string& label)
    {
        const auto* vectors = dynamic_cast<const Graphics::PixelBitmapContent<Vector4>*>(&bitmap);
        ASSERT_NE(vectors, nullptr) << label << ": not a Vector4 bitmap";
        ASSERT_EQ(vectors->getWidthProperty() * vectors->getHeightProperty(), static_cast<int>(xna.size())) << label;
        for (std::size_t i = 0; i < xna.size(); ++i)
        {
            const Vector4 got = vectors->GetPixel(static_cast<int>(i) % vectors->getWidthProperty(),
                                                  static_cast<int>(i) / vectors->getWidthProperty());
            EXPECT_NEAR(got.X, xna[i].X, 1e-6f) << label << " pixel " << i;
            EXPECT_NEAR(got.Y, xna[i].Y, 1e-6f) << label << " pixel " << i;
            EXPECT_NEAR(got.Z, xna[i].Z, 1e-6f) << label << " pixel " << i;
            EXPECT_NEAR(got.W, xna[i].W, 1e-6f) << label << " pixel " << i;
        }
    }
}

// TextureProcessor.Process premultiplies (IL_0090-0094) before it resizes (IL_0099-00a2). The lane
// oracle shows it directly: a straight-alpha source premultiplied by the processor and an already
// premultiplied source resized alone come out the same, (192,96,48,245) (75,37,19,96) (0,0,0,0)
// (0,0,0,0) (texture/order_premultiply_then_resize, texture/order_resize_of_premultiplied_input).
// Resizing first lets the transparent white bleed in: 0.375*(200,100,50,255) + 0.625*(255,255,255,0)
// = (234,197,178,96), premultiplied (88,74,67,96) -- 13 units off in red. One unit is D3DX's dither.
TEST(XnaTextureProcessorSteps, PremultipliesBeforeItResizes)
{
    TextureProcessor processor;
    processor.setColorKeyEnabledProperty(false);
    processor.setResizeToPowerOfTwoProperty(true);
    RecordingContext context;
    auto texture = Single(Colours(3, 1, {Color(200, 100, 50, 255), Color(255, 255, 255, 0), Color(255, 255, 255, 0)}));
    (void)processor.Process(texture, context);
    ExpectColoursWithin(*Level(*texture, 0, 0),
                        {Color(192, 96, 48, 245), Color(75, 37, 19, 96), Color(0, 0, 0, 0), Color(0, 0, 0, 0)}, 1,
                        "premultiplied then resized");

    // The control: the same source with premultiplying off is resized straight
    // (texture/order_resize_only).
    TextureProcessor straight;
    straight.setColorKeyEnabledProperty(false);
    straight.setPremultiplyAlphaProperty(false);
    straight.setResizeToPowerOfTwoProperty(true);
    auto unpremultiplied =
        Single(Colours(3, 1, {Color(200, 100, 50, 255), Color(255, 255, 255, 0), Color(255, 255, 255, 0)}));
    (void)straight.Process(unpremultiplied, context);
    ExpectColoursWithin(*Level(*unpremultiplied, 0, 0),
                        {Color(203, 107, 59, 245), Color(234, 197, 178, 96), Color(255, 255, 255, 0),
                         Color(255, 255, 255, 0)},
                        1, "resized only");
}

// A float texture is keyed and premultiplied as floats: IsHighPrecisionFormat (IL_0000-00d2) sends a
// PixelBitmapContent<Vector4> to Vector4, not Color (Process IL_005a-007c); ColorKey compares the
// key's own Vector4 exactly (IL_0041-0058); PremultiplyAlpha scales X, Y, Z by W only when W < 1
// (IL_00ef-0146); NoChange gives the texture back in Vector4 (ChangeTextureToRequestedFormat
// IL_0015-0020). Lane oracle texture/vector4_nochange_key_premultiply,
// texture/vector4_alpha_above_one_nochange.
TEST(XnaTextureProcessorSteps, AFloatTextureIsKeyedAndPremultipliedInFloats)
{
    TextureProcessor processor;
    processor.setTextureFormatProperty(TextureProcessorOutputFormat::NoChange);
    RecordingContext context;
    auto texture = Single(Vectors(3, 1, {Vector4(0.3f, 2.5f, -0.25f, 0.5f), Vector4(0.1f, 0.2f, 0.3f, 1.0f),
                                         Vector4(1, 0, 1, 1)}));
    (void)processor.Process(texture, context);
    ExpectVectors(*Level(*texture, 0, 0),
                  {Vector4(0.3f * 0.5f, 2.5f * 0.5f, -0.25f * 0.5f, 0.5f), Vector4(0.1f, 0.2f, 0.3f, 1.0f),
                   Vector4(0, 0, 0, 0)},
                  "keyed and premultiplied as floats");

    // An alpha above one is not "below one": the pixel is left as it is, and so is the HDR colour a
    // byte could not hold.
    TextureProcessor unkeyed;
    unkeyed.setColorKeyEnabledProperty(false);
    unkeyed.setTextureFormatProperty(TextureProcessorOutputFormat::NoChange);
    auto bright = Single(Vectors(2, 1, {Vector4(0.5f, 0.5f, 0.5f, 2.0f), Vector4(0.5f, 0.25f, 0.75f, 1.0f)}));
    (void)unkeyed.Process(bright, context);
    ExpectVectors(*Level(*bright, 0, 0), {Vector4(0.5f, 0.5f, 0.5f, 2.0f), Vector4(0.5f, 0.25f, 0.75f, 1.0f)},
                  "alpha above one");
}

// The key is matched before any conversion to bytes, so a float pixel a byte would round to magenta
// is kept: (0.999, 0, 1, 1) is 254.7 -> 255, but it is not (1, 0, 1, 1). Lane oracle
// texture/vector4_near_magenta_default answers (255,0,255,255) (0,0,0,0) through the default
// processor. And premultiplying in floats rounds once: (0.2,0.4,0.6,0.25) -> (0.05,0.1,0.15,0.25)
// -> (13,26,38,64) (texture/vector4_color_premultiply), where bytes first answer
// (51,102,153,64) -> 51*64/255 = 12.
TEST(XnaTextureProcessorSteps, AFloatTextureIsKeyedBeforeItBecomesBytes)
{
    TextureProcessor processor;
    RecordingContext context;
    auto texture = Single(Vectors(2, 1, {Vector4(0.999f, 0, 1, 1), Vector4(1, 0, 1, 1)}));
    (void)processor.Process(texture, context);
    ExpectColoursWithin(*Level(*texture, 0, 0), {Color(255, 0, 255, 255), Color(0, 0, 0, 0)}, 0, "near magenta");

    TextureProcessor premultiplying;
    premultiplying.setColorKeyEnabledProperty(false);
    auto quarter = Single(Vectors(1, 1, {Vector4(0.2f, 0.4f, 0.6f, 0.25f)}));
    (void)premultiplying.Process(quarter, context);
    ExpectColoursWithin(*Level(*quarter, 0, 0), {Color(13, 26, 38, 64)}, 0, "premultiplied in floats");
}

// ResizeToPowerOfTwo keeps each level's own type (TextureProcessorUtils.ResizeToPowerOfTwo
// IL_004f-005c: TextureContent.ConvertBitmap(bitmap, bitmap.GetType(), ...)), so with nothing keyed
// or premultiplied a float texture is resized in floats. Lane oracle texture/vector4_resize_nochange;
// the values are the resize kernel's (XnaGraphicsBitmap.ResizeKernelMatchesXnaToTheFloat): 1/24 at
// each edge of a 3 -> 4 enlargement.
TEST(XnaTextureProcessorSteps, AResizeKeepsTheTexturesOwnType)
{
    TextureProcessor processor;
    processor.setColorKeyEnabledProperty(false);
    processor.setPremultiplyAlphaProperty(false);
    processor.setResizeToPowerOfTwoProperty(true);
    processor.setTextureFormatProperty(TextureProcessorOutputFormat::NoChange);
    RecordingContext context;
    auto texture = Single(Vectors(3, 1, {Vector4(0, 0, 0, 1), Vector4(1, 1, 1, 1), Vector4(0.5f, 0.25f, 0.125f, 1)}));
    (void)processor.Process(texture, context);
    ExpectVectors(*Level(*texture, 0, 0),
                  {Vector4(0.0416666642f, 0.0416666642f, 0.0416666642f, 1), Vector4(0.625f, 0.625f, 0.625f, 1),
                   Vector4(0.8125f, 0.71875f, 0.671875f, 1), Vector4(0.5208334f, 0.28125f, 0.161458343f, 1)},
                  "resized in floats");
}

// TextureProcessorUtils.BestGuessCompress converts to Color and then leaves a Texture3DContent
// alone (IL_0010-0016): a volume asked for DxtCompressed comes out Color, pixel for pixel. Lane
// oracle texture/volume_dxt_stays_color and texture/volume_dxt_fractional_alpha_stays_color; a cube
// is compressed (texture/cube_dxt_compresses: six Dxt1 faces).
TEST(XnaTextureProcessorSteps, AVolumeAskedForDxtStaysColor)
{
    TextureProcessor processor;
    processor.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    RecordingContext context;
    auto volume = std::make_shared<Graphics::Texture3DContent>();
    volume->getFacesProperty().Add(std::make_shared<Graphics::MipmapChain>(Opaque4x4(0)));
    volume->getFacesProperty().Add(std::make_shared<Graphics::MipmapChain>(Opaque4x4(5)));
    (void)processor.Process(volume, context);
    for (int slice = 0; slice < 2; ++slice)
    {
        std::vector<Color> expected;
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                expected.emplace_back(x * 60 + slice * 5, y * 60, 30, 255);
            }
        }
        ExpectColoursWithin(*Level(*volume, slice, 0), expected, 0, "slice " + std::to_string(slice));
    }

    TextureProcessor raw;
    raw.setColorKeyEnabledProperty(false);
    raw.setPremultiplyAlphaProperty(false);
    raw.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    auto translucent = std::make_shared<Graphics::Texture3DContent>();
    translucent->getFacesProperty().Add(std::make_shared<Graphics::MipmapChain>(
        Colours(4, 1, {Color(10, 20, 30, 128), Color(1, 2, 3, 255), Color(4, 5, 6, 255), Color(7, 8, 9, 255)})));
    (void)raw.Process(translucent, context);
    ExpectColoursWithin(*Level(*translucent, 0, 0),
                        {Color(10, 20, 30, 128), Color(1, 2, 3, 255), Color(4, 5, 6, 255), Color(7, 8, 9, 255)}, 0,
                        "a translucent volume");

    auto cube = std::make_shared<Graphics::TextureCubeContent>();
    for (int face = 0; face < 6; ++face)
    {
        std::as_const(cube->getFacesProperty())[face]->Add(Opaque4x4(face));
    }
    (void)processor.Process(cube, context);
    for (int face = 0; face < 6; ++face)
    {
        EXPECT_NE(std::dynamic_pointer_cast<Graphics::Dxt1BitmapContent>(Level(*cube, face, 0)), nullptr)
            << "cube face " << face;
    }
}

// Keying or premultiplying starts with TextureContent.Validate(null) (Process IL_0032-003d), whose
// refusals name the texture; with both off nothing validates the texture, and a chain Validate would
// refuse is carried through. Lane oracle texture/process_empty_2d_default,
// process_empty_2d_no_key_no_premultiply, process_mixed_chain_default, process_bad_chain_default,
// process_bad_chain_no_key_no_premultiply.
TEST(XnaTextureProcessorSteps, KeyingOrPremultiplyingValidatesTheTextureFirst)
{
    RecordingContext context;
    {
        auto empty = std::make_shared<Graphics::Texture2DContent>();
        empty->setIdentityProperty(ContentIdentity("probe.png", "TextureImporter", "frag"));
        const auto refusal = RefusalOf([&] { TextureProcessor processor; (void)processor.Process(empty, context); });
        EXPECT_EQ(refusal.message, "Invalid texture. Face 0 does not contain any mipmaps.");
        EXPECT_EQ(Spell(refusal.identity), "{probe.png|TextureImporter|frag}");
    }
    {
        auto empty = std::make_shared<Graphics::Texture2DContent>();
        TextureProcessor processor;
        processor.setColorKeyEnabledProperty(false);
        processor.setPremultiplyAlphaProperty(false);
        EXPECT_EQ(processor.Process(empty, context), empty);
        EXPECT_EQ(empty->getMipmapsProperty().getCountProperty(), 0);
    }
    {
        auto mixed = std::make_shared<Graphics::Texture2DContent>();
        mixed->setIdentityProperty(ContentIdentity("probe.dds", "TextureImporter", "mips"));
        mixed->getMipmapsProperty().Add(Opaque4x4(0));
        mixed->getMipmapsProperty().Add(std::make_shared<Graphics::PixelBitmapContent<Vector4>>(2, 2));
        mixed->getMipmapsProperty().Add(std::make_shared<Graphics::PixelBitmapContent<Color>>(1, 1));
        const auto refusal = RefusalOf([&] { TextureProcessor processor; (void)processor.Process(mixed, context); });
        EXPECT_EQ(refusal.message,
                  "Invalid texture. Bitmap type mismatch: face 0 mip 1 is PixelBitmapContent<Vector4>, 2x2, but face 0 "
                  "mip 0 is PixelBitmapContent<Color>, 4x4.");
        EXPECT_EQ(Spell(refusal.identity), "{probe.dds|TextureImporter|mips}");
    }
    {
        const auto badChain = []
        {
            auto texture = std::make_shared<Graphics::Texture2DContent>();
            texture->setIdentityProperty(ContentIdentity("probe.dds", "TextureImporter", "mips"));
            texture->getMipmapsProperty().Add(Opaque4x4(0));
            texture->getMipmapsProperty().Add(std::make_shared<Graphics::PixelBitmapContent<Color>>(3, 3));
            return texture;
        };
        auto refused = badChain();
        const auto refusal = RefusalOf([&] { TextureProcessor processor; (void)processor.Process(refused, context); });
        EXPECT_EQ(refusal.message, "Invalid texture. Face 0 mip 1 is sized 3x3, but should be 2x2.");
        EXPECT_EQ(Spell(refusal.identity), "{probe.dds|TextureImporter|mips}");

        auto carried = badChain();
        TextureProcessor processor;
        processor.setColorKeyEnabledProperty(false);
        processor.setPremultiplyAlphaProperty(false);
        (void)processor.Process(carried, context);
        ASSERT_EQ(carried->getMipmapsProperty().getCountProperty(), 2);
        EXPECT_EQ(Level(*carried, 0, 1)->getWidthProperty(), 3);
    }
}
