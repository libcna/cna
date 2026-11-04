// SPDX-License-Identifier: MS-PL
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

namespace
{
    template<class Exception, class Action>
    void Refuses(Action&& action)
    {
        try { action(); ADD_FAILURE() << "Request was accepted"; }
        catch (const Exception& error) { EXPECT_EQ(typeid(error), typeid(Exception)); }
    }

    class GraphicsProfileBoundaryTest : public testing::TestWithParam<GraphicsProfile>
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
            // IsProfileSupported is a renderer/hardware query, not a promise that Reach is
            // promoted to HiDef. Test both requests on a renderer advertising both profiles.
            auto& adapter = GraphicsAdapter::getDefaultAdapterProperty();
            ASSERT_TRUE(adapter.IsProfileSupported(GraphicsProfile::Reach));
            ASSERT_TRUE(adapter.IsProfileSupported(GraphicsProfile::HiDef));
            device = std::make_unique<GraphicsDevice>(adapter, GetParam(),
                PresentationParameters::CreateWindowedEXT(8, 8));
            ASSERT_EQ(device->getGraphicsProfileProperty(), GetParam());
        }
        bool Reach() const { return GetParam() == GraphicsProfile::Reach; }
        std::unique_ptr<GraphicsDevice> device;
    };

    INSTANTIATE_TEST_SUITE_P(Xna, GraphicsProfileBoundaryTest,
        testing::Values(GraphicsProfile::Reach, GraphicsProfile::HiDef),
        [](const testing::TestParamInfo<GraphicsProfile>& info) {
            return info.param == GraphicsProfile::Reach ? "Reach" : "HiDef";
        });
}

TEST_P(GraphicsProfileBoundaryTest, TextureAndRenderTarget2DCheckEachAxisAtTheCeiling)
{
    // XNA ProfileCapabilities::.cctor IL_0082/02b9: 2048/4096. Texture2D validation
    // IL_0062-0090 checks BOTH axes; RT2D.CreateRenderTarget IL_0043 shares that gate.
    const int maximum = Reach() ? 2048 : 4096;
    for (const bool vertical : {false, true})
    {
        SCOPED_TRACE(vertical);
        const int width = vertical ? 2 : maximum;
        const int height = vertical ? maximum : 2;
        Texture2D texture(*device, width, height, false, SurfaceFormat::Color);
        EXPECT_EQ(texture.getWidthProperty(), width);
        EXPECT_EQ(texture.getHeightProperty(), height);
        EXPECT_EQ(texture.getLevelCountProperty(), 1);
        RenderTarget2D target(*device, width, height);
        EXPECT_EQ(target.getWidthProperty(), width);
        EXPECT_EQ(target.getHeightProperty(), height);
        device->SetRenderTarget(&target);
        ASSERT_EQ(device->GetRenderTargets().size(), 1u);
        EXPECT_EQ(device->GetRenderTargets()[0].getRenderTargetProperty(), &target);
        device->SetRenderTarget(nullptr);
        Refuses<System::NotSupportedException>([&] {
            Texture2D refused(*device, vertical ? 2 : maximum + 1,
                              vertical ? maximum + 1 : 2, false, SurfaceFormat::Color);
        });
        Refuses<System::NotSupportedException>([&] {
            RenderTarget2D refused(*device, vertical ? 2 : maximum + 1,
                                   vertical ? maximum + 1 : 2);
        });
    }
}

TEST_P(GraphicsProfileBoundaryTest, CubeAndRenderTargetCubeAcceptTheCeilingAndRefuseTheNextSize)
{
    // XNA .cctor IL_008d/02c4: 512/4096, TextureCube.ValidateCreationParameters
    // IL_004e-0074. DXT1 keeps the accepted 4096 cube's storage bounded.
    const int maximum = Reach() ? 512 : 4096;
    {
        TextureCube cube(*device, maximum, false, SurfaceFormat::Dxt1);
        EXPECT_EQ(cube.getSizeProperty(), maximum);
        EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Dxt1);
        EXPECT_EQ(cube.getLevelCountProperty(), 1);
    }
    {
        RenderTargetCube target(*device, maximum, false, SurfaceFormat::Color, DepthFormat::None);
        EXPECT_EQ(target.getSizeProperty(), maximum);
        EXPECT_EQ(target.getLevelCountProperty(), 1);
        device->SetRenderTarget(&target, CubeMapFace::NegativeZ);
        ASSERT_EQ(device->GetRenderTargets().size(), 1u);
        EXPECT_EQ(device->GetRenderTargets()[0].getRenderTargetProperty(), &target);
        EXPECT_EQ(device->GetRenderTargets()[0].getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
        device->SetRenderTargets({});
    }
    Refuses<System::NotSupportedException>([&] {
        TextureCube refused(*device, maximum + 1, false, SurfaceFormat::Color);
    });
    Refuses<System::NotSupportedException>([&] {
        RenderTargetCube refused(*device, maximum + 1, false, SurfaceFormat::Color, DepthFormat::None);
    });
}

TEST_P(GraphicsProfileBoundaryTest, VolumeChecksAllThreeAxesAndAllowsHiDefNpotMipmaps)
{
    // XNA .cctor IL_0098/02cf: Reach has no volumes; HiDef extent is 256.
    // Texture3D.CreateTexture validates width, height and depth independently.
    if (Reach())
    {
        Refuses<System::NotSupportedException>([&] {
            Texture3D refused(*device, 1, 1, 1, false, SurfaceFormat::Color);
        });
        return;
    }
    for (int axis = 0; axis != 3; ++axis)
    {
        SCOPED_TRACE(axis);
        std::array<int, 3> extents{1, 1, 1};
        extents[axis] = 256;
        Texture3D volume(*device, extents[0], extents[1], extents[2], true, SurfaceFormat::Color);
        EXPECT_EQ(volume.getWidthProperty(), extents[0]);
        EXPECT_EQ(volume.getHeightProperty(), extents[1]);
        EXPECT_EQ(volume.getDepthProperty(), extents[2]);
        EXPECT_EQ(volume.getLevelCountProperty(), 9); // log2(256) + 1.
        extents[axis] = 257;
        Refuses<System::NotSupportedException>([&] {
            Texture3D refused(*device, extents[0], extents[1], extents[2], false, SurfaceFormat::Color);
        });
    }
    Texture3D npot(*device, 3, 5, 7, true, SurfaceFormat::Color);
    EXPECT_EQ(npot.getLevelCountProperty(), 3); // 7 -> 3 -> 1.
}

TEST_P(GraphicsProfileBoundaryTest, NpotMipmapsAreValidatedAtTextureAndTargetConstruction)
{
    // XNA Texture2D.ValidateCreationParameters IL_00d0-0101; RT2D.CreateRenderTarget
    // IL_0038-0043 passes through the same mipMap value. FNA omits this RT restriction.
    for (const auto dimensions : {std::array{3, 4}, std::array{4, 5}})
    {
        Texture2D single(*device, dimensions[0], dimensions[1], false, SurfaceFormat::Color);
        EXPECT_EQ(single.getLevelCountProperty(), 1);
        RenderTarget2D target(*device, dimensions[0], dimensions[1]);
        EXPECT_EQ(target.getLevelCountProperty(), 1);
        if (Reach())
        {
            Refuses<System::NotSupportedException>([&] {
                Texture2D refused(*device, dimensions[0], dimensions[1], true, SurfaceFormat::Color);
            });
            Refuses<System::NotSupportedException>([&] {
                RenderTarget2D refused(*device, dimensions[0], dimensions[1], true,
                                       SurfaceFormat::Color, DepthFormat::None);
            });
        }
        else
        {
            Texture2D mipped(*device, dimensions[0], dimensions[1], true, SurfaceFormat::Color);
            RenderTarget2D mippedTarget(*device, dimensions[0], dimensions[1], true,
                                       SurfaceFormat::Color, DepthFormat::None);
            EXPECT_EQ(mipped.getLevelCountProperty(), 3); // max(3,4) or max(4,5) -> 2 -> 1.
            EXPECT_EQ(mippedTarget.getLevelCountProperty(), 3);
        }
    }
    // Positive Reach control: mipmaps themselves are legal when both axes are powers of two.
    Texture2D pot(*device, 4, 8, true, SurfaceFormat::Color);
    RenderTarget2D potTarget(*device, 4, 8, true, SurfaceFormat::Color, DepthFormat::None);
    EXPECT_EQ(pot.getLevelCountProperty(), 4);
    EXPECT_EQ(potTarget.getLevelCountProperty(), 4);
}

TEST_P(GraphicsProfileBoundaryTest, NpotCubeAndDxtAlignmentAreIndependentRestrictions)
{
    // XNA TextureCube.ValidateCreationParameters IL_0079-00b5; Texture2D validation
    // IL_0106-013a: Reach requires POT DXT; HiDef only requires a multiple of four.
    for (const auto format : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        SCOPED_TRACE(static_cast<int>(format));
        Texture2D pot(*device, 8, 4, true, format);
        EXPECT_EQ(pot.getLevelCountProperty(), 4);
        if (Reach())
        {
            Refuses<System::NotSupportedException>([&] { Texture2D t(*device, 12, 4, false, format); });
            Refuses<System::NotSupportedException>([&] { TextureCube t(*device, 12, false, format); });
        }
        else
        {
            Texture2D npot(*device, 12, 4, true, format);
            TextureCube cube(*device, 12, true, format);
            EXPECT_EQ(npot.getWidthProperty(), 12);
            EXPECT_EQ(cube.getSizeProperty(), 12);
            Refuses<System::ArgumentException>([&] { Texture2D t(*device, 6, 4, false, format); });
            Refuses<System::ArgumentException>([&] { TextureCube t(*device, 6, false, format); });
        }
    }
    if (Reach())
    {
        Refuses<System::NotSupportedException>([&] { TextureCube t(*device, 3, false, SurfaceFormat::Color); });
        Refuses<System::NotSupportedException>([&] {
            RenderTargetCube t(*device, 3, false, SurfaceFormat::Color, DepthFormat::None);
        });
    }
    else
    {
        TextureCube t(*device, 3, true, SurfaceFormat::Color);
        RenderTargetCube rt(*device, 3, true, SurfaceFormat::Color, DepthFormat::None);
        EXPECT_EQ(t.getLevelCountProperty(), 2);
        EXPECT_EQ(rt.getLevelCountProperty(), 2);
    }
}

TEST_P(GraphicsProfileBoundaryTest, MrtCeilingRefusalKeepsEveryPreviousBinding)
{
    // XNA SetRenderTargets IL_0088-00ac checks profile count before visiting slots.
    const int maximum = Reach() ? 1 : 4; // ProfileCapabilities .cctor IL_00b9/02f4.
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i != maximum; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(*device, 2, 2));
        bindings.emplace_back(targets.back().get());
    }
    device->SetRenderTargets(bindings);
    ASSERT_EQ(device->GetRenderTargets().size(), static_cast<std::size_t>(maximum));
    for (int i = 0; i != maximum; ++i)
        EXPECT_EQ(device->GetRenderTargets()[i].getRenderTargetProperty(), targets[i].get());
    bindings.emplace_back(); // Count must win over this null slot.
    Refuses<System::NotSupportedException>([&] { device->SetRenderTargets(bindings); });
    ASSERT_EQ(device->GetRenderTargets().size(), static_cast<std::size_t>(maximum));
    for (int i = 0; i != maximum; ++i)
        EXPECT_EQ(device->GetRenderTargets()[i].getRenderTargetProperty(), targets[i].get());
    device->SetRenderTargets({});
}

TEST_P(GraphicsProfileBoundaryTest, SixteenVertexStreamsAreRetainedAndSeventeenRefusedBeforeMutation)
{
    // XNA .cctor IL_0059/0290 and SetVertexBuffers IL_0015-0039.
    const VertexDeclaration declaration(4, {VertexElement(0, VertexElementFormat::Single,
                                                         VertexElementUsage::Position, 0)});
    VertexBuffer buffer(*device, declaration, 17, BufferUsage::None);
    std::vector<VertexBufferBinding> bindings;
    for (int i = 0; i != 16; ++i) bindings.emplace_back(&buffer, i, 0);
    device->SetVertexBuffers(bindings);
    ASSERT_EQ(device->GetVertexBuffers().size(), 16u);
    for (int i = 0; i != 16; ++i)
    {
        EXPECT_EQ(device->GetVertexBuffers()[i].getVertexBufferProperty(), &buffer);
        EXPECT_EQ(device->GetVertexBuffers()[i].getVertexOffsetProperty(), i);
    }
    bindings.emplace_back(); // Count wins over the null seventeenth stream.
    Refuses<System::NotSupportedException>([&] { device->SetVertexBuffers(bindings); });
    ASSERT_EQ(device->GetVertexBuffers().size(), 16u);
    EXPECT_EQ(device->GetVertexBuffers()[15].getVertexOffsetProperty(), 15);
}

TEST_P(GraphicsProfileBoundaryTest, DeclarationLimitsAreCheckedWhenBuffersAreCreated)
{
    // XNA .cctor MaxStreamStride=255 (IL_0061/0298), MaxVertexStreams=16;
    // VertexElementValidator.Validate(profile) IL_0007-0042. Strides must also be
    // multiples of four, so the largest constructible stride is floor(255/4)*4=252.
    const VertexElement first(0, VertexElementFormat::Single, VertexElementUsage::Position, 0);
    const VertexDeclaration maximumStride(252, {first});
    VertexBuffer buffer(*device, maximumStride, 1, BufferUsage::None);
    EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(), 252);
    const VertexDeclaration tooWide(256, {first});
    Refuses<System::NotSupportedException>([&] { VertexBuffer b(*device, tooWide, 1, BufferUsage::None); });
    Refuses<System::NotSupportedException>([&] { DynamicVertexBuffer b(*device, tooWide, 1, BufferUsage::None); });
    std::vector<VertexElement> elements;
    for (int i = 0; i != 16; ++i)
        elements.emplace_back(i * 4, VertexElementFormat::Single, VertexElementUsage::Position, i);
    const VertexDeclaration sixteen(64, elements);
    DynamicVertexBuffer accepted(*device, sixteen, 1, BufferUsage::None);
    EXPECT_EQ(accepted.getVertexDeclarationProperty().GetVertexElements().size(), 16u);
    elements.emplace_back(64, VertexElementFormat::Single, VertexElementUsage::Color, 0);
    const VertexDeclaration seventeen(68, elements);
    Refuses<System::NotSupportedException>([&] { VertexBuffer b(*device, seventeen, 1, BufferUsage::None); });
}

TEST_P(GraphicsProfileBoundaryTest, SamplerAndTextureSlotLimitsMatchEachStage)
{
    // XNA .cctor MaxSamplers=16, MaxVertexSamplers=0/4 (IL_00aa-00b3/02e5-02ee).
    Texture2D texture(*device, 1, 1, false, Reach() ? SurfaceFormat::Color : SurfaceFormat::Single);
    auto& textures = device->getTexturesProperty();
    auto& samplers = device->getSamplerStatesProperty();
    textures(15, &texture);
    samplers[15] = SamplerState::PointClamp;
    EXPECT_EQ(textures[15], &texture);
    EXPECT_EQ(samplers[15].getFilterProperty(), TextureFilter::Point);
    Refuses<System::ArgumentOutOfRangeException>([&] { textures(16, &texture); });
    Refuses<System::ArgumentOutOfRangeException>([&] { samplers[16] = SamplerState::PointClamp; });
    EXPECT_EQ(textures[15], &texture);
    const int count = Reach() ? 0 : 4;
    auto& vertexTextures = device->getVertexTexturesProperty();
    auto& vertexSamplers = device->getVertexSamplerStatesProperty();
    EXPECT_EQ(vertexSamplers.ActiveSamplerCount(), count);
    if (!Reach())
    {
        vertexTextures(3, &texture);
        vertexSamplers[3] = SamplerState::PointClamp;
        EXPECT_EQ(vertexTextures[3], &texture);
        EXPECT_EQ(vertexSamplers[3].getFilterProperty(), TextureFilter::Point);
    }
    Refuses<System::ArgumentOutOfRangeException>([&] { vertexTextures(count, nullptr); });
    Refuses<System::ArgumentOutOfRangeException>([&] { vertexSamplers[count] = SamplerState::PointClamp; });
}

TEST_P(GraphicsProfileBoundaryTest, IndexWidthsAreEnforcedForStaticAndDynamicBuffers)
{
    // XNA IndexBuffer.CreateIndexBuffer's profile check precedes allocation; .cctor
    // IndexElementSize32 IL_0052/0289. Signed and unsigned element types have the same width.
    IndexBuffer narrow(*device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    EXPECT_EQ(narrow.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    if (Reach())
    {
        Refuses<System::NotSupportedException>([&] {
            IndexBuffer b(*device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
        });
        Refuses<System::NotSupportedException>([&] {
            DynamicIndexBuffer b(*device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
        });
    }
    else
    {
        IndexBuffer wide(*device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
        DynamicIndexBuffer dynamic(*device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
        const std::uint32_t marker = 0x12345678;
        for (IndexBuffer* buffer : {&wide, static_cast<IndexBuffer*>(&dynamic)})
        {
            EXPECT_EQ(buffer->getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
            buffer->SetData(&marker, 1);
            std::uint32_t readback = 0;
            buffer->GetData(&readback, 1);
            EXPECT_EQ(readback, marker); // Literal 32-bit marker catches silent 16-bit allocation.
        }
    }
}

TEST_P(GraphicsProfileBoundaryTest, QueryConstructionIsProfileRestrictedOnAQueryCapableRenderer)
{
    // XNA OcclusionQuery::.ctor IL_0019-0040; .cctor IL_0024/025b.
    if (Reach())
        Refuses<System::NotSupportedException>([&] { OcclusionQuery query(*device); });
    else
    {
        OcclusionQuery query(*device);
        EXPECT_EQ(query.getGraphicsDeviceProperty(), device.get());
        EXPECT_FALSE(query.getIsCompleteProperty());
    }
}

namespace
{
    template<class Action>
    void ProfileError(Action&& action, const std::string& expected)
    {
        try { action(); ADD_FAILURE() << "Request was accepted"; }
        catch (const System::NotSupportedException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::NotSupportedException));
            EXPECT_EQ(error.getMessageProperty(), expected);
        }
    }
}

TEST_P(GraphicsProfileBoundaryTest, ResourceProfileErrorsUseXnaMessagesAndValidationOrder)
{
    // Oracle: FrameworkResources ProfileTooBig/ProfileFormatNotSupported/ProfileAspectRatio
    // and ProfileCapabilities.ThrowNotSupportedException IL_0000-002e. These are plain
    // NotSupportedException, with no ArgumentException parameter. XNA Texture2D validates
    // format BEFORE maximum size (IL_0034-0090); Texture3D does likewise (IL_0079-00d8).
    const std::string prefix = std::string("XNA Framework ") + (Reach() ? "Reach" : "HiDef") + " profile ";
    const int maximum2D = Reach() ? 2048 : 4096;
    const int maximumCube = Reach() ? 512 : 4096;
    ProfileError([&] { Texture2D t(*device, maximum2D + 1, 2, false, SurfaceFormat::Color); },
        prefix + "supports a maximum Texture2D size of " + std::to_string(maximum2D) + ".");
    ProfileError([&] { TextureCube t(*device, maximumCube + 1, false, SurfaceFormat::Color); },
        prefix + "supports a maximum TextureCube size of " + std::to_string(maximumCube) + ".");
    ProfileError([&] { TextureCube t(*device, maximumCube + 1, false, SurfaceFormat::NormalizedByte2); },
        prefix + "does not support TextureCube format NormalizedByte2.");
    ProfileError([&] { RenderTarget2D t(*device, maximum2D + 1, 2); },
        prefix + "supports a maximum Texture2D size of " + std::to_string(maximum2D) + ".");
    ProfileError([&] { RenderTargetCube t(*device, maximumCube + 1, false, SurfaceFormat::Color, DepthFormat::None); },
        prefix + "supports a maximum TextureCube size of " + std::to_string(maximumCube) + ".");
    for (const bool vertical : {false, true})
    {
        Texture2D accepted(*device, vertical ? 1 : 2048, vertical ? 2048 : 1);
        EXPECT_EQ(accepted.getWidthProperty(), vertical ? 1 : 2048);
    }
    if (Reach())
    {
        ProfileError([&] { Texture2D t(*device, 2049, 2, false, SurfaceFormat::Single); },
            prefix + "does not support Texture2D format Single.");
        ProfileError([&] { Texture3D t(*device, 257, 1, 1, false, SurfaceFormat::Dxt1); },
            prefix + "does not support Texture3D.");
        ProfileError([&] { Texture2D t(*device, 3, 5, true, SurfaceFormat::Color); },
            prefix + "requires mipmapped Texture2D sizes to be powers of two. To use a non power of two Texture2D, remove the mipmaps.");
        ProfileError([&] { RenderTarget2D t(*device, 3, 5, true, SurfaceFormat::Single, DepthFormat::None); },
            prefix + "requires mipmapped Texture2D sizes to be powers of two. To use a non power of two Texture2D, remove the mipmaps.");
    }
    else
    {
        ProfileError([&] { Texture3D t(*device, 257, 1, 1, false, SurfaceFormat::Dxt1); },
            prefix + "does not support Texture3D format Dxt1.");
        ProfileError([&] { Texture3D t(*device, 257, 1, 1, false, SurfaceFormat::Color); },
            prefix + "supports a maximum Texture3D size of 256.");
        // N=2048:1 is legal; N+1 is within HiDef's dimension limit, but fails the ratio.
        for (const bool vertical : {false, true})
        {
            Texture2D accepted(*device, vertical ? 1 : 2048, vertical ? 2048 : 1);
            EXPECT_EQ(accepted.getWidthProperty(), vertical ? 1 : 2048);
            ProfileError([&] { Texture2D t(*device, vertical ? 1 : 2049, vertical ? 2049 : 1); },
                prefix + "supports a maximum Texture2D aspect ratio of 2048.");
        }
    }
}

TEST_P(GraphicsProfileBoundaryTest, BufferByteCeilingsAreCheckedForBothProfilesAndDynamicVariants)
{
    // XNA .cctor IL_006c-007c/02a3-02b3: 67108863 bytes. For a four-byte vertex/index
    // the final valid count is floor(67108863/4)=16777215; for ushort it is 33554431.
    const std::string prefix = std::string("XNA Framework ") + (Reach() ? "Reach" : "HiDef") + " profile ";
    VertexDeclaration declaration(4, {VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
    {
        VertexBuffer buffer(*device, declaration, 16'777'215, BufferUsage::None);
        EXPECT_EQ(buffer.getVertexCountProperty(), 16'777'215);
        EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(), 4);
    }
    {
        DynamicVertexBuffer buffer(*device, declaration, 16'777'215, BufferUsage::None);
        EXPECT_EQ(buffer.getVertexCountProperty(), 16'777'215);
    }
    for (const bool dynamic : {false, true})
    {
        SCOPED_TRACE(dynamic);
        ProfileError([&] {
            if (dynamic) { DynamicVertexBuffer b(*device, declaration, 16'777'216, BufferUsage::None); }
            else { VertexBuffer b(*device, declaration, 16'777'216, BufferUsage::None); }
        }, prefix + "supports a maximum VertexBuffer size of 67108863.");
        ProfileError([&] {
            if (dynamic) { DynamicIndexBuffer b(*device, IndexElementSize::SixteenBits, 33'554'432, BufferUsage::None); }
            else { IndexBuffer b(*device, IndexElementSize::SixteenBits, 33'554'432, BufferUsage::None); }
        }, prefix + "supports a maximum IndexBuffer size of 67108863.");
    }
    {
        DynamicIndexBuffer buffer(*device, IndexElementSize::SixteenBits, 33'554'431, BufferUsage::None);
        EXPECT_EQ(buffer.getIndexCountProperty(), 33'554'431);
        EXPECT_EQ(buffer.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    }
}

TEST(GraphicsProfileEnumValidationTest, InvalidProfilesAreRefusedByAdapterAndDeviceWithParameterName)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    // XNA ProfileChecker.IsProfileSupported IL_0000-0001 and GraphicsDevice::.ctor
    // IL_004d call ProfileCapabilities.GetInstance; IL_001e-0028 throws AORE(graphicsProfile).
    auto& adapter = GraphicsAdapter::getDefaultAdapterProperty();
    for (const int ordinal : {-1, 2})
    {
        const auto profile = static_cast<GraphicsProfile>(ordinal);
        for (const bool construct : {false, true})
        {
            try
            {
                if (construct) { GraphicsDevice d(adapter, profile, PresentationParameters::CreateWindowedEXT(8, 8)); }
                else { (void)adapter.IsProfileSupported(profile); }
                ADD_FAILURE() << "Invalid profile " << ordinal << " accepted";
            }
            catch (const System::ArgumentOutOfRangeException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
                EXPECT_EQ(error.getParamNameProperty(), "graphicsProfile");
            }
        }
    }
}

TEST_P(GraphicsProfileBoundaryTest, DeclarationErrorsDistinguishLimitsFormatsAndUsageIndices)
{
    const std::string prefix = std::string("XNA Framework ") + (Reach() ? "Reach" : "HiDef") + " profile ";
    // Oracle: VertexElementValidator.Validate(profile) IL_0007-0042/004e-008d/00b5-00f9.
    VertexDeclaration wide(256, {VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
    ProfileError([&] { VertexBuffer b(*device, wide, 1, BufferUsage::None); },
        prefix + "supports a maximum vertex stride of 255.");
    std::vector<VertexElement> elements;
    for (int i = 0; i != 16; ++i)
        elements.emplace_back(i * 4, VertexElementFormat::Single, VertexElementUsage::Position, i);
    elements.emplace_back(64, VertexElementFormat::Single, VertexElementUsage::Color, 0);
    VertexDeclaration excessive(68, elements);
    ProfileError([&] { VertexBuffer b(*device, excessive, 1, BufferUsage::None); },
        prefix + "supports a maximum of 16 simultaneous vertex elements.");
    if (Reach())
    {
        VertexDeclaration half(4, {VertexElement(0, VertexElementFormat::HalfVector2, VertexElementUsage::Position, 0)});
        ProfileError([&] { VertexBuffer b(*device, half, 1, BufferUsage::None); },
            prefix + "does not support VertexElement format HalfVector2.");
    }
    for (int index : {-1, 16})
    {
        VertexDeclaration badUsage(4, {VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, index)});
        try { VertexBuffer b(*device, badUsage, 1, BufferUsage::None); ADD_FAILURE() << "Usage accepted"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_TRUE(error.getParamNameProperty().empty());
            EXPECT_EQ(error.getMessageProperty(), "Invalid VertexDeclaration. Usage Position" + std::to_string(index) + " is out of range.");
        }
    }
}

TEST_P(GraphicsProfileBoundaryTest, CountAndFeatureRefusalsUseTheActiveProfileInTheirDiagnostic)
{
    const std::string prefix = std::string("XNA Framework ") + (Reach() ? "Reach" : "HiDef") + " profile ";
    // Oracle: XNA SetVertexBuffers IL_0015-0039, SetRenderTargets IL_0088-00ac,
    // OcclusionQuery::.ctor IL_0019-0040, GetBackBufferData IL_0015-0032.
    ProfileError([&] { device->SetVertexBuffers(std::vector<VertexBufferBinding>(17)); },
        prefix + "supports a maximum of 16 simultaneous vertex buffers.");
    ProfileError([&] { device->SetRenderTargets(std::vector<RenderTargetBinding>(Reach() ? 2 : 5)); },
        prefix + "supports a maximum of " + std::string(Reach() ? "1" : "4") + " simultaneous rendertargets.");
    if (Reach())
    {
        ProfileError([&] { OcclusionQuery query(*device); }, prefix + "does not support OcclusionQuery.");
        ProfileError([&] { device->GetBackBufferData(static_cast<Microsoft::Xna::Framework::Color*>(nullptr), 0); },
            prefix + "does not support GetBackBufferData.");
        ProfileError([&] { IndexBuffer b(*device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None); },
            prefix + "does not support 32 bit indices. Use IndexElementSize.SixteenBits or a type that has a size of two bytes.");
    }
}

TEST_P(GraphicsProfileBoundaryTest, BothProfilesRetainEveryXnaDepthFormatOnBothTargetKinds)
{
    // XNA .cctor ValidDepthFormats IL_0194-01b7/06af-06d2 contains Depth16,
    // Depth24 and Depth24Stencil8 in both profiles. QueryDepthFormat separately accepts None.
    for (const auto format : {DepthFormat::None, DepthFormat::Depth16,
                              DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
    {
        SCOPED_TRACE(static_cast<int>(format));
        RenderTarget2D target(*device, 2, 2, false, SurfaceFormat::Color, format);
        RenderTargetCube cube(*device, 2, false, SurfaceFormat::Color, format);
        EXPECT_EQ(target.getDepthStencilFormatProperty(), format);
        EXPECT_EQ(cube.getDepthStencilFormatProperty(), format);
    }
}
