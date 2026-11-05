// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <string>
#include <typeinfo>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/IO/Stream.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

namespace
{
    template<class Exception, class Action>
    void ExactMessage(Action&& action, const std::string& message)
    {
        try { action(); ADD_FAILURE() << "request was accepted"; }
        catch (const Exception& error)
        {
            EXPECT_EQ(typeid(error), typeid(Exception));
            EXPECT_EQ(error.getMessageProperty(), message);
        }
        catch (...) { ADD_FAILURE() << "unexpected exception type"; }
    }

    template<class Action>
    void DimensionError(Action&& action, const char* parameter)
    {
        // Refresh FrameworkResources.ResourcesMustBeGreaterThanZeroSize, used by
        // Texture2D.ValidateCreationParameters IL_0004-0027, Texture3D.CreateTexture
        // IL_0017-004f and TextureCube.ValidateCreationParameters IL_0004-0013.
        // Only the parameter suffix is delegated to sharp-runtime's exception formatting;
        // the message body, parameter, and absent ActualValue are independent literals.
        try { action(); ADD_FAILURE() << "nonpositive dimension was accepted"; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), parameter);
            EXPECT_TRUE(error.getActualValueProperty().empty());
            const System::ArgumentOutOfRangeException expected(
                parameter, "Resource size must be greater than zero.");
            EXPECT_EQ(error.getMessageProperty(), expected.getMessageProperty());
        }
        catch (...) { ADD_FAILURE() << "unexpected exception type"; }
    }

    class RefusalStream final : public System::IO::Stream
    {
    public:
        explicit RefusalStream(bool seekable) : seekable_(seekable) {}
        int Read(System::IO::bytecs[], int, int) override { ++reads; return 0; }
        void Close() override { ++closes; }
        int getLengthProperty() const override { ++lengths; return 0; }
        int getPositionProperty() const override { ++positions; return 0; }
        void setPositionProperty(int) override { ++seeks; }
        bool getCanSeekProperty() const override { return seekable_; }
        mutable int lengths = 0;
        mutable int positions = 0;
        int reads = 0;
        int seeks = 0;
        int closes = 0;
    private:
        bool seekable_;
    };

    void Untouched(const RefusalStream& stream)
    {
        EXPECT_EQ(stream.lengths, 0);
        EXPECT_EQ(stream.positions, 0);
        EXPECT_EQ(stream.reads, 0);
        EXPECT_EQ(stream.seeks, 0);
        EXPECT_EQ(stream.closes, 0);
    }
}

TEST(TextureConstructionContract, NonpositiveDimensionsKeepExactDiagnosticsAndPrecedence)
{
    GraphicsDevice device;
    // Every refusal occurs before allocation, on either profile, and even when
    // format/feature/size rules would also fail. Width precedes height precedes depth.
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        for (int invalid : {0, -1})
        {
            SCOPED_TRACE(invalid);
            DimensionError([&] { Texture2D t(device, invalid, invalid); }, "width");
            DimensionError([&] { Texture2D t(device, 1, invalid); }, "height");
            for (bool mip : {false, true})
            {
                DimensionError([&] { Texture2D t(device, invalid, invalid, mip, SurfaceFormat::Single); }, "width");
                DimensionError([&] { Texture2D t(device, 1, invalid, mip, SurfaceFormat::Single); }, "height");
                DimensionError([&] { Texture3D t(device, invalid, invalid, invalid, mip, SurfaceFormat::Dxt1); }, "width");
                DimensionError([&] { Texture3D t(device, 1, invalid, invalid, mip, SurfaceFormat::Dxt1); }, "height");
                DimensionError([&] { Texture3D t(device, 1, 1, invalid, mip, SurfaceFormat::Dxt1); }, "depth");
                DimensionError([&] { TextureCube t(device, invalid, mip, SurfaceFormat::NormalizedByte2); }, "size");
            }
        }
    }
}

TEST(TextureConstructionContract, UndefinedFormatsUseProfileErrorsBeforeSizeOrShape)
{
    GraphicsDevice device;
    // Refresh Texture2D.ValidateCreationParameters IL_0028-0061 and
    // TextureCube.ValidateCreationParameters IL_0014-004d: undefined formats are
    // not in ValidTextureFormats/ValidCubeFormats; Enum.ToString renders the integer.
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        const std::string prefix = profile == GraphicsProfile::Reach
            ? "XNA Framework Reach profile " : "XNA Framework HiDef profile ";
        for (int ordinal : {-1, 27}) // 20..26 are CNAEXT formats, deliberately excluded.
        {
            SCOPED_TRACE(ordinal);
            const auto format = static_cast<SurfaceFormat>(ordinal);
            ExactMessage<System::NotSupportedException>([&] {
                Texture2D t(device, 4097, 3, true, format);
            }, prefix + "does not support Texture2D format " + std::to_string(ordinal) + ".");
            ExactMessage<System::NotSupportedException>([&] {
                TextureCube t(device, 4097, true, format);
            }, prefix + "does not support TextureCube format " + std::to_string(ordinal) + ".");
        }
    }
}

TEST(TextureConstructionContract, SmallDxtBasesAndNpotErrorsHaveExactIdentities)
{
    GraphicsDevice device;
    // Refresh Texture2D.ValidateCreationParameters IL_00d0-013b and
    // TextureCube.ValidateCreationParameters IL_0079-00b6; FrameworkResources.
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        for (const auto format : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
        {
            for (int size : {1, 2}) // POT does not waive the multiple-of-four rule.
            {
                for (bool mip : {false, true})
                {
                    for (bool vertical : {false, true})
                    {
                        ExactMessage<System::ArgumentException>([&] {
                            Texture2D t(device, vertical ? 4 : size, vertical ? size : 4, mip, format);
                        }, "DXT compressed texture sizes must be multiples of four.");
                    }
                    ExactMessage<System::ArgumentException>([&] {
                        TextureCube t(device, size, mip, format);
                    }, "DXT compressed texture sizes must be multiples of four.");
                }
            }
            if (profile == GraphicsProfile::Reach)
            {
                ExactMessage<System::NotSupportedException>([&] {
                    Texture2D t(device, 3, 4, true, format);
                }, "XNA Framework Reach profile requires mipmapped Texture2D sizes to be powers of two. To use a non power of two Texture2D, remove the mipmaps.");
                ExactMessage<System::NotSupportedException>([&] {
                    Texture2D t(device, 4, 3, false, format);
                }, "XNA Framework Reach profile requires DXT compressed Texture2D sizes to be powers of two. To use a non power of two Texture2D, disable DXT compression.");
                ExactMessage<System::NotSupportedException>([&] {
                    TextureCube t(device, 3, false, format);
                }, "XNA Framework Reach profile requires TextureCube sizes to be powers of two.");
            }
        }
    }
}

TEST(TextureConstructionContract, CubeShapeRefusalPrecedesRendererFormatAvailability)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    // Refresh TextureCube.ValidateCreationParameters IL_0079-009e. All three
    // formats are legal Reach cube formats; some renderers refuse packed cube
    // storage, but that cannot hide the earlier XNA shape error.
    for (const auto format : {SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551, SurfaceFormat::Bgra4444})
    {
        ExactMessage<System::NotSupportedException>([&] {
            TextureCube t(device, 3, false, format);
        }, "XNA Framework Reach profile requires TextureCube sizes to be powers of two.");
    }
}

TEST(TextureConstructionContract, ReachVolumeRefusalPrecedesRendererCapability)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    // Refresh Texture3D.CreateTexture IL_0057-0074; unlike TQ-024's Software
    // fixture this also runs on renderers with no volume implementation.
    ExactMessage<System::NotSupportedException>([&] {
        Texture3D t(device, 257, 1, 1, false, SurfaceFormat::Dxt1);
    }, "XNA Framework Reach profile does not support Texture3D.");
}

TEST(TextureConstructionContract, FlatAndCubeMipCountsStraddlePowersOfTwo)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Headless, Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Complete D3D9 chain: Texture2D.CreateTexture IL_0024-002d,
    // TextureCube.CreateTexture IL_0021-0029, Texture3D.CreateTexture IL_0168-0171
    // pass levels=0 for mipMap=true. FNA Texture.cs:198-215 agrees.
    // Literal hand derivation: 1; 2->1; 3->1; 7->3->1; 8->4->2->1;
    // 9->4->2->1; 15->7->3->1; 16->8->4->2->1; 17->8->4->2->1.
    constexpr std::array sizes{1, 2, 3, 7, 8, 9, 15, 16, 17};
    constexpr std::array levels{1, 2, 2, 3, 4, 4, 4, 5, 5};
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        for (bool mip : {false, true})
        {
            SCOPED_TRACE(sizes[i]);
            const int expected = mip ? levels[i] : 1;
            for (bool vertical : {false, true})
            {
                Texture2D texture(device, vertical ? 1 : sizes[i], vertical ? sizes[i] : 1,
                                  mip, SurfaceFormat::Color);
                EXPECT_EQ(texture.getLevelCountProperty(), expected);
            }
            TextureCube cube(device, sizes[i], mip, SurfaceFormat::Color);
            EXPECT_EQ(cube.getLevelCountProperty(), expected);

        }
    }
}

TEST(TextureConstructionContract, VolumeMipCountsStraddlePowersOfTwoOnEveryAxis)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Refresh Texture3D.CreateTexture IL_0168-0171 passes native levels=0.
    // Hand chains are the same literal cases as the flat/cube test, with each
    // of width, height and depth taking a turn as the maximum dimension.
    constexpr std::array sizes{1, 2, 3, 7, 8, 9, 15, 16, 17};
    constexpr std::array levels{1, 2, 2, 3, 4, 4, 4, 5, 5};
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        for (bool mip : {false, true})
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                SCOPED_TRACE(axis);
                SCOPED_TRACE(sizes[i]);
                std::array extents{1, 1, 1};
                extents[axis] = sizes[i];
                Texture3D volume(device, extents[0], extents[1], extents[2], mip, SurfaceFormat::Color);
                EXPECT_EQ(volume.getLevelCountProperty(), mip ? levels[i] : 1);
            }
        }
    }
}

namespace
{
    template<class ShapeCheck>
    void MetadataAndDisposal(Texture& texture, GraphicsDevice& device, ShapeCheck shape)
    {
        // Refresh getters directly read fields (Texture.get_Format/get_LevelCount
        // IL_0000-0006; GraphicsResource.get_GraphicsDevice IL_0000-0006).
        // GraphicsResource Name/Tag accessors IL_0000-0027 have no disposed guard.
        // '~GraphicsResource' IL_0000-0026 publishes IsDisposed before one event.
        EXPECT_EQ(texture.getGraphicsDeviceProperty(), &device);
        EXPECT_TRUE(texture.getNameProperty().empty()); // C++ maps default null Name to empty.
        EXPECT_EQ(texture.getTagProperty(), nullptr);
        EXPECT_FALSE(texture.getIsDisposedProperty());
        const auto format = texture.getFormatProperty();
        const int levels = texture.getLevelCountProperty();
        texture.setNameProperty("texture metadata");
        texture.setTagProperty(&device);
        int events = 0;
        texture.Disposing += [&](System::Object* sender, const System::EventArgs&)
        {
            ++events;
            EXPECT_EQ(sender, &texture);
            EXPECT_TRUE(texture.getIsDisposedProperty());
            EXPECT_EQ(texture.getGraphicsDeviceProperty(), &device);
            EXPECT_EQ(texture.getNameProperty(), "texture metadata");
            EXPECT_EQ(texture.getTagProperty(), &device);
            EXPECT_EQ(texture.getFormatProperty(), format);
            EXPECT_EQ(texture.getLevelCountProperty(), levels);
            shape();
        };
        texture.Dispose();
        texture.Dispose();
        EXPECT_EQ(events, 1);
        EXPECT_TRUE(texture.getIsDisposedProperty());
        EXPECT_EQ(texture.getGraphicsDeviceProperty(), &device);
        EXPECT_EQ(texture.getFormatProperty(), format);
        EXPECT_EQ(texture.getLevelCountProperty(), levels);
        shape();
        texture.setNameProperty("after disposal");
        texture.setTagProperty(nullptr);
        EXPECT_EQ(texture.getNameProperty(), "after disposal");
        EXPECT_EQ(texture.getTagProperty(), nullptr);
    }
}

TEST(TextureConstructionContract, FlatAndCubePropertiesSurviveDispose)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Headless, Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Distinct extents prevent swapped Width/Height/Depth passing. Refresh Texture2D
    // get_Bounds IL_0000-0016 returns Rectangle(0,0,width,height), even after Dispose.
    Texture2D flat(device, 3, 5, true, SurfaceFormat::Color);
    EXPECT_EQ(flat.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(flat.getLevelCountProperty(), 3); // 5->2->1.
    MetadataAndDisposal(flat, device, [&] {
        EXPECT_EQ(flat.getWidthProperty(), 3);
        EXPECT_EQ(flat.getHeightProperty(), 5);
        const auto bounds = flat.getBoundsProperty();
        EXPECT_EQ(bounds.X, 0); EXPECT_EQ(bounds.Y, 0);
        EXPECT_EQ(bounds.Width, 3); EXPECT_EQ(bounds.Height, 5);
    });
    TextureCube cube(device, 9, true, SurfaceFormat::Bgr565);
    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Bgr565);
    EXPECT_EQ(cube.getLevelCountProperty(), 4);
    MetadataAndDisposal(cube, device, [&] { EXPECT_EQ(cube.getSizeProperty(), 9); });
}

TEST(TextureConstructionContract, VolumePropertiesSurviveDispose)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Refresh Texture3D getters IL_0000-0006 return the cached dimensions;
    // GraphicsResource and Texture property/event oracle is in MetadataAndDisposal.
    Texture3D volume(device, 3, 5, 9, true, SurfaceFormat::Color);
    EXPECT_EQ(volume.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(volume.getLevelCountProperty(), 4); // 9->4->2->1.
    MetadataAndDisposal(volume, device, [&] {
        EXPECT_EQ(volume.getWidthProperty(), 3);
        EXPECT_EQ(volume.getHeightProperty(), 5);
        EXPECT_EQ(volume.getDepthProperty(), 9);
    });
}

TEST(TextureConstructionContract, FromStreamArgumentRefusalsDoNotInspectOrCloseTheStream)
{
    GraphicsDevice device;
    // Refresh stream constructor IL_002c-0043: seekability before dimensions;
    // IL_0044-004f: dimensions before ImageStream.FromStream at IL_005f.
    // No decoder expectations or image fixture are needed for an argument refusal.
    RefusalStream plain(false);
    const System::ArgumentException seekable("The stream is required to be seekable.", "stream");
    ExactMessage<System::ArgumentException>([&] { (void)Texture2D::FromStream(device, plain); },
                                             seekable.getMessageProperty());
    Untouched(plain);
    for (bool zoom : {false, true})
    {
        RefusalStream blocked(false);
        ExactMessage<System::ArgumentException>([&] {
            (void)Texture2D::FromStream(device, blocked, 0, 0, zoom);
        }, seekable.getMessageProperty());
        Untouched(blocked);
        for (const auto dimensions : {std::array{0, 0}, std::array{-1, 1}, std::array{1, 0}})
        {
            RefusalStream stream(true);
            DimensionError([&] {
                (void)Texture2D::FromStream(device, stream, dimensions[0], dimensions[1], zoom);
            }, dimensions[0] <= 0 ? "width" : "height");
            Untouched(stream);
        }
    }
}
