// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-023: does a CNA processor, constructed and asked nothing
// else, answer what XNA's answered?
//
// The plan's §10 is an inventory of every processor property with the default the black-box oracle
// read off a freshly constructed instance of Microsoft's own class. That inventory is the audit;
// this is the other half of it, and the half that keeps being true: each property is constructed
// here and compared with the value recorded there. A default is the most quietly wrong thing a
// reimplementation can have -- nothing fails, the content is just different -- and three of them
// were wrong before this plan (`ColorKeyEnabled`, `SpriteTextureProcessor`'s `GenerateMipmaps`, and
// the font-description style parsing), each found by hand.
//
// Every row below carries an `XNA-DEFAULT:` comment naming the processor, the property and the
// value. `tools/xna-pipeline-oracle/processor_defaults_gate.py` reads those comments back out of
// this file and compares them with the inventory, so the table cannot drift from the measurement
// and cannot quietly cover fewer than all 47 properties.
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VideoProcessor.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
using Microsoft::Xna::Framework::Color;

namespace
{
    const Color kMagenta{255, 0, 255, 255};
}

TEST(XnaProcessorDefaults, EffectProcessorAnswersWhatXnasDid)
{
    const Processors::EffectProcessor processor;
    // XNA-DEFAULT: EffectProcessor DebugMode EffectProcessorDebugMode.Auto
    EXPECT_EQ(processor.getDebugModeProperty(), Processors::EffectProcessorDebugMode::Auto);
    // XNA-DEFAULT: EffectProcessor Defines None
    EXPECT_TRUE(processor.getDefinesProperty().empty());
}

TEST(XnaProcessorDefaults, FontTextureProcessorAnswersWhatXnasDid)
{
    const Processors::FontTextureProcessor processor;
    // XNA-DEFAULT: FontTextureProcessor FirstCharacter ' ' (32)
    EXPECT_EQ(processor.getFirstCharacterProperty(), u' ');
    // XNA-DEFAULT: FontTextureProcessor PremultiplyAlpha True
    EXPECT_TRUE(processor.getPremultiplyAlphaProperty());
    // XNA-DEFAULT: FontTextureProcessor TextureFormat TextureProcessorOutputFormat.Color
    EXPECT_EQ(processor.getTextureFormatProperty(), Processors::TextureProcessorOutputFormat::Color);
}

TEST(XnaProcessorDefaults, MaterialProcessorAnswersWhatXnasDid)
{
    const Processors::MaterialProcessor processor;
    // XNA-DEFAULT: MaterialProcessor ColorKeyColor Color:{R:255 G:0 B:255 A:255}
    EXPECT_EQ(processor.getColorKeyColorProperty(), kMagenta);
    // XNA-DEFAULT: MaterialProcessor ColorKeyEnabled True
    EXPECT_TRUE(processor.getColorKeyEnabledProperty());
    // XNA-DEFAULT: MaterialProcessor DefaultEffect MaterialProcessorDefaultEffect.BasicEffect
    EXPECT_EQ(processor.getDefaultEffectProperty(),
              Processors::MaterialProcessorDefaultEffect::BasicEffect);
    // XNA-DEFAULT: MaterialProcessor GenerateMipmaps True
    EXPECT_TRUE(processor.getGenerateMipmapsProperty());
    // XNA-DEFAULT: MaterialProcessor PremultiplyTextureAlpha True
    EXPECT_TRUE(processor.getPremultiplyTextureAlphaProperty());
    // XNA-DEFAULT: MaterialProcessor ResizeTexturesToPowerOfTwo False
    EXPECT_FALSE(processor.getResizeTexturesToPowerOfTwoProperty());
    // XNA-DEFAULT: MaterialProcessor TextureFormat TextureProcessorOutputFormat.DxtCompressed
    EXPECT_EQ(processor.getTextureFormatProperty(),
              Processors::TextureProcessorOutputFormat::DxtCompressed);
}

TEST(XnaProcessorDefaults, ModelProcessorAnswersWhatXnasDid)
{
    const Processors::ModelProcessor processor;
    // XNA-DEFAULT: ModelProcessor ColorKeyColor Color:{R:255 G:0 B:255 A:255}
    EXPECT_EQ(processor.getColorKeyColorProperty(), kMagenta);
    // XNA-DEFAULT: ModelProcessor ColorKeyEnabled True
    EXPECT_TRUE(processor.getColorKeyEnabledProperty());
    // XNA-DEFAULT: ModelProcessor DefaultEffect MaterialProcessorDefaultEffect.BasicEffect
    EXPECT_EQ(processor.getDefaultEffectProperty(),
              Processors::MaterialProcessorDefaultEffect::BasicEffect);
    // XNA-DEFAULT: ModelProcessor GenerateMipmaps True
    EXPECT_TRUE(processor.getGenerateMipmapsProperty());
    // XNA-DEFAULT: ModelProcessor GenerateTangentFrames False
    EXPECT_FALSE(processor.getGenerateTangentFramesProperty());
    // XNA-DEFAULT: ModelProcessor PremultiplyTextureAlpha True
    EXPECT_TRUE(processor.getPremultiplyTextureAlphaProperty());
    // XNA-DEFAULT: ModelProcessor PremultiplyVertexColors True
    EXPECT_TRUE(processor.getPremultiplyVertexColorsProperty());
    // XNA-DEFAULT: ModelProcessor ResizeTexturesToPowerOfTwo False
    EXPECT_FALSE(processor.getResizeTexturesToPowerOfTwoProperty());
    // XNA-DEFAULT: ModelProcessor RotationX 0
    EXPECT_FLOAT_EQ(processor.getRotationXProperty(), 0.0f);
    // XNA-DEFAULT: ModelProcessor RotationY 0
    EXPECT_FLOAT_EQ(processor.getRotationYProperty(), 0.0f);
    // XNA-DEFAULT: ModelProcessor RotationZ 0
    EXPECT_FLOAT_EQ(processor.getRotationZProperty(), 0.0f);
    // XNA-DEFAULT: ModelProcessor Scale 1
    EXPECT_FLOAT_EQ(processor.getScaleProperty(), 1.0f);
    // XNA-DEFAULT: ModelProcessor SwapWindingOrder False
    EXPECT_FALSE(processor.getSwapWindingOrderProperty());
    // XNA-DEFAULT: ModelProcessor TextureFormat TextureProcessorOutputFormat.DxtCompressed
    EXPECT_EQ(processor.getTextureFormatProperty(),
              Processors::TextureProcessorOutputFormat::DxtCompressed);
}

TEST(XnaProcessorDefaults, ModelTextureProcessorAnswersWhatXnasDid)
{
    const Processors::ModelTextureProcessor processor;
    // XNA-DEFAULT: ModelTextureProcessor ColorKeyColor Color:{R:255 G:0 B:255 A:255}
    EXPECT_EQ(processor.getColorKeyColorProperty(), kMagenta);
    // XNA-DEFAULT: ModelTextureProcessor ColorKeyEnabled True
    EXPECT_TRUE(processor.getColorKeyEnabledProperty());
    // XNA-DEFAULT: ModelTextureProcessor GenerateMipmaps True
    EXPECT_TRUE(processor.getGenerateMipmapsProperty());
    // XNA-DEFAULT: ModelTextureProcessor PremultiplyAlpha True
    EXPECT_TRUE(processor.getPremultiplyAlphaProperty());
    // XNA-DEFAULT: ModelTextureProcessor ResizeToPowerOfTwo False
    EXPECT_FALSE(processor.getResizeToPowerOfTwoProperty());
    // XNA-DEFAULT: ModelTextureProcessor TextureFormat TextureProcessorOutputFormat.DxtCompressed
    EXPECT_EQ(processor.getTextureFormatProperty(),
              Processors::TextureProcessorOutputFormat::DxtCompressed);
}

TEST(XnaProcessorDefaults, TheAudioAndVideoProcessorsAnswerWhatXnasDid)
{
    const Processors::SongProcessor song;
    // XNA-DEFAULT: SongProcessor Quality ConversionQuality.Best
    EXPECT_EQ(song.getQualityProperty(), Xna::Audio::ConversionQuality::Best);

    const Processors::SoundEffectProcessor sound;
    // XNA-DEFAULT: SoundEffectProcessor Quality ConversionQuality.Best
    EXPECT_EQ(sound.getQualityProperty(), Xna::Audio::ConversionQuality::Best);

    const Processors::VideoProcessor video;
    // XNA-DEFAULT: VideoProcessor VideoSoundtrackType VideoSoundtrackType.Music
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(),
              Microsoft::Xna::Framework::Media::VideoSoundtrackType::Music);
}

// The two texture processors that differ from `TextureProcessor` only in their defaults, which is
// the whole reason XNA has them and the reason getting one wrong is invisible.
TEST(XnaProcessorDefaults, TheThreeTextureProcessorsDifferOnlyWhereXnasDo)
{
    const Processors::TextureProcessor texture;
    // XNA-DEFAULT: TextureProcessor ColorKeyColor Color:{R:255 G:0 B:255 A:255}
    EXPECT_EQ(texture.getColorKeyColorProperty(), kMagenta);
    // XNA-DEFAULT: TextureProcessor ColorKeyEnabled True
    EXPECT_TRUE(texture.getColorKeyEnabledProperty());
    // XNA-DEFAULT: TextureProcessor GenerateMipmaps False
    EXPECT_FALSE(texture.getGenerateMipmapsProperty());
    // XNA-DEFAULT: TextureProcessor PremultiplyAlpha True
    EXPECT_TRUE(texture.getPremultiplyAlphaProperty());
    // XNA-DEFAULT: TextureProcessor ResizeToPowerOfTwo False
    EXPECT_FALSE(texture.getResizeToPowerOfTwoProperty());
    // XNA-DEFAULT: TextureProcessor TextureFormat TextureProcessorOutputFormat.Color
    EXPECT_EQ(texture.getTextureFormatProperty(), Processors::TextureProcessorOutputFormat::Color);

    const Processors::SpriteTextureProcessor sprite;
    // XNA-DEFAULT: SpriteTextureProcessor ColorKeyColor Color:{R:255 G:0 B:255 A:255}
    EXPECT_EQ(sprite.getColorKeyColorProperty(), kMagenta);
    // XNA-DEFAULT: SpriteTextureProcessor ColorKeyEnabled True
    EXPECT_TRUE(sprite.getColorKeyEnabledProperty());
    // XNA-DEFAULT: SpriteTextureProcessor GenerateMipmaps False
    EXPECT_FALSE(sprite.getGenerateMipmapsProperty());
    // XNA-DEFAULT: SpriteTextureProcessor PremultiplyAlpha True
    EXPECT_TRUE(sprite.getPremultiplyAlphaProperty());
    // XNA-DEFAULT: SpriteTextureProcessor ResizeToPowerOfTwo False
    EXPECT_FALSE(sprite.getResizeToPowerOfTwoProperty());
    // XNA-DEFAULT: SpriteTextureProcessor TextureFormat TextureProcessorOutputFormat.Color
    EXPECT_EQ(sprite.getTextureFormatProperty(), Processors::TextureProcessorOutputFormat::Color);
}
