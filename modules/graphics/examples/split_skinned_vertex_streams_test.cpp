// SPDX-License-Identifier: MS-PL
// GAP-14 shared renderer-neutral witness. XNA Refresh GraphicsDevice.SetVertexBuffers
// IL_00f5..0121 binds every stream with its own stride * VertexOffset;
// DrawIndexedPrimitives IL_00bd..00c7 forwards baseVertex separately.
// Skin arithmetic: weights sum to 1; I and Translate(+1,0,0) move a quad centred at
// x=-.75 to -.75, -.25 or +.25 for bone1 weights 0, .5 or 1. At width 64 these
// centres are pixels 8,24,40. No sample lies near an edge; tolerance 2 UNORM units.
// CNA_GAP14_PBR adds CNAEXT-only legs; the default source uses genuine SkinnedEffect.
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#ifdef CNA_GAP14_PBR
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#endif
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    using F = VertexElementFormat;
    using U = VertexElementUsage;
    VertexDeclaration GeometryDeclaration()
    {
        return VertexDeclaration(32, {{0,F::Vector3,U::Position,0}, {12,F::Vector3,U::Normal,0},
                                      {24,F::Vector2,U::TextureCoordinate,0}});
    }
    VertexDeclaration BonesDeclaration(bool floats, int stride)
    {
        if (floats) return VertexDeclaration(stride, {{0,F::Vector4,U::BlendIndices,0},
                                                      {16,F::Vector4,U::BlendWeight,0}});
        return VertexDeclaration(stride, {{0,F::Vector4,U::BlendWeight,0},
                                           {16,F::Byte4,U::BlendIndices,0}});
    }
    VertexDeclaration InstancesDeclaration()
    {
        return VertexDeclaration(64, {{0,F::Vector4,U::TextureCoordinate,2},
            {16,F::Vector4,U::TextureCoordinate,3}, {32,F::Vector4,U::TextureCoordinate,4},
            {48,F::Vector4,U::TextureCoordinate,5}});
    }
    std::vector<std::uint8_t> GeometryBytes()
    {
        constexpr std::array<std::array<float,2>,6> corners{{
            {-.875f,.15f}, {-.875f,-.15f}, {-.625f,-.15f},
            {-.875f,.15f}, {-.625f,-.15f}, {-.625f,.15f}}};
        std::vector<std::uint8_t> bytes(9 * 32, 0);
        for (int i = 0; i < 9; ++i)
        {
            const float x = i < 3 ? -4 : corners[i-3][0];
            const float y = i < 3 ? -4 : corners[i-3][1];
            const std::array<float,8> record{x,y,.5f, 0,0,-1, 0,0};
            std::memcpy(bytes.data() + i*32, record.data(), 32);
        }
        return bytes;
    }
    void UploadBones(GraphicsDevice& device, VertexBuffer& vb, bool floats, int stride, int mode)
    {
        std::vector<std::uint8_t> bytes(12 * stride, 0);
        for (int i = 0; i < 12; ++i)
        {
            // Prefixes select the wrong bone and must fail at the expected screen position.
            const bool prefix = i < 6;
            const std::array<float,4> weights = mode == 2 && !prefix
                ? std::array<float,4>{.5f,.5f,0,0} : std::array<float,4>{1,0,0,0};
            const std::array<std::uint8_t,4> indices = mode == 1 || prefix
                ? std::array<std::uint8_t,4>{1,0,0,0} : std::array<std::uint8_t,4>{0,1,0,0};
            auto* dst = bytes.data() + i*stride;
            if (floats)
            {
                const std::array<float,4> indexFloats{float(indices[0]),float(indices[1]),0,0};
                std::memcpy(dst, indexFloats.data(), 16);
                std::memcpy(dst+16, weights.data(), 16);
            }
            else
            {
                std::memcpy(dst, weights.data(), 16);
                std::memcpy(dst+16, indices.data(), 4);
            }
        }
        // Refresh VertexBuffer.CopyData IL_002c..0081 refuses a bound SetData resource.
        // Unbinding preserves queued draws; each caller rebinds before its next draw.
        device.SetVertexBuffer(nullptr);
        vb.SetDataRaw(bytes.data(), 12, stride);
    }
    template<class EffectType> void Configure(EffectType& fx, Texture2D& white)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&white);
        fx.setDiffuseColorProperty(Vector3(1,1,1));
        fx.setAmbientLightColorProperty(Vector3(1,1,1));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
    }
    template<class EffectType> void Palette(EffectType& fx)
    {
        fx.SetBoneTransforms({Matrix::getIdentityProperty(), Matrix::CreateTranslation(Vector3(1,0,0))});
        fx.setWeightsPerVertexProperty(2);
    }
}
class SplitSkinnedStreamsTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int failures_ = 0;
    bool done_ = false;
    void ExpectPixel(int x, int y, Color expected, const char* label)
    {
        Color actual;
        const Rectangle area(x,y,1,1);
        getGraphicsDeviceProperty().GetBackBufferData(&area, &actual, 0, 1);
        const bool ok = std::abs(int(actual.getRProperty())-int(expected.getRProperty())) <= 2 &&
                        std::abs(int(actual.getGProperty())-int(expected.getGProperty())) <= 2 &&
                        std::abs(int(actual.getBProperty())-int(expected.getBProperty())) <= 2 &&
                        std::abs(int(actual.getAProperty())-int(expected.getAProperty())) <= 2;
        std::printf("[%s] %s @(%d,%d) rgba=%u,%u,%u,%u\n", ok?"PASS":"FAIL", label, x,y,
            unsigned(actual.getRProperty()),unsigned(actual.getGProperty()),
            unsigned(actual.getBProperty()),unsigned(actual.getAProperty()));
        if (!ok) ++failures_;
    }
    void ExpectLocation(int centre, Color color, const char* label, int y = 32)
    {
        for (int x : {8,24,40}) ExpectPixel(x,y,x==centre?color:Color::Black,label);
    }
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& d = getGraphicsDeviceProperty();
        d.setRasterizerStateProperty(RasterizerState::CullNone);
        d.setBlendStateProperty(BlendState::Opaque);
        d.setDepthStencilStateProperty(DepthStencilState::None);
        Texture2D white(d,1,1); const Color whitePixel = Color::White; white.SetData(&whitePixel,1);
        VertexBuffer geometry(d,GeometryDeclaration(),9,BufferUsage::None);
        const auto geometryData = GeometryBytes();
        d.SetVertexBuffer(nullptr);
        geometry.SetDataRaw(geometryData.data(),9,32);
        IndexBuffer indices(d,IndexElementSize::SixteenBits,9,BufferUsage::None);
        const std::uint16_t indexData[]{0,0,0, 0,1,2,3,4,5};
        d.setIndicesProperty(nullptr);
        indices.SetData(indexData,9);
        d.setIndicesProperty(&indices);
        SkinnedEffect skin(d); Configure(skin,white); Palette(skin);
        // Ambient-only, white texture, diffuse green => exactly (0,255,0,255).
        skin.setDiffuseColorProperty(Vector3(0,1,0));
        for (bool vertexLit : {true,false})
        {
            skin.setPreferPerPixelLightingProperty(!vertexLit);
            // Indexed + nonindexed, unequal offsets, nonzero base/start, Byte4 vs reordered
            // Vector4 indices. Byte4 padded to 32 then Vector4 32 probes stale layout keys.
            for (int layout : {0,1,2})
            {
                const bool floats = layout == 2;
                const int stride = layout == 0 ? 20 : 32;
                VertexBuffer bones(d,BonesDeclaration(floats,stride),12,BufferUsage::None);
                for (bool indexed : {false,true})
                {
                    for (int mode : {0,1,2})
                    {
                        d.Clear(Color::Black);
                        UploadBones(d,bones,floats,stride,mode);
                        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5)});
                        skin.Apply();
                        if (indexed) d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
                        else d.DrawPrimitives(PrimitiveType::TriangleList,1,2);
                        ExpectLocation(mode==0?8:mode==1?40:24,Color::Lime,
                            "SecondStreamWeightsIndicesAndOffsets");
                    }
                }
            }
        }
        VertexBuffer bones(d,BonesDeclaration(false,20),12,BufferUsage::None);
        UploadBones(d,bones,false,20,1);
        // Both draws remain queued while the same second buffer is updated. Owned normalized
        // bytes preserve the first draw's bone index as well as the second draw's new index.
        d.Clear(Color::Black);
        UploadBones(d,bones,false,20,0);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5)});
        skin.Apply(); d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
        UploadBones(d,bones,false,20,1);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5)});
        skin.Apply(); d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
        ExpectPixel(8,32,Color::Lime,"QueuedSecondStreamSnapshotFirstDraw");
        ExpectPixel(40,32,Color::Lime,"QueuedSecondStreamSnapshotSecondDraw");
        ExpectPixel(24,32,Color::Black,"QueuedSecondStreamSnapshotGap");
        const VertexDeclaration unusedDecl(16,{{0,F::Vector4,U::TextureCoordinate,7}});
        VertexBuffer unused(d,unusedDecl,1,BufferUsage::None);
        const std::array<float,4> decoy{9,9,9,9};
        d.SetVertexBuffer(nullptr);
        unused.SetDataRaw(decoy.data(),1,16);
        d.Clear(Color::Black);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                            VertexBufferBinding(&unused)});
        skin.Apply(); d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
        ExpectLocation(40,Color::Lime,"UnconsumedShortStreamDoesNotLimitSkin");
        const VertexDeclaration unusedColorDecl(4,{{0,F::Color,U::Color,0}});
        VertexBuffer unusedColor(d,unusedColorDecl,1,BufferUsage::None);
        const std::uint32_t unusedBlue = Color::Blue.getPackedValueProperty();
        d.SetVertexBuffer(nullptr);
        unusedColor.SetDataRaw(&unusedBlue,1,4);
        d.Clear(Color::Black);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                            VertexBufferBinding(&unusedColor)});
        skin.Apply(); d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
        ExpectLocation(40,Color::Lime,"UnconsumedDisabledColorDoesNotLimitSkin");
        VertexBuffer instances(d,InstancesDeclaration(),3,BufferUsage::None);
        std::array<float,48> transforms{};
        for (int i=0;i<3;++i)
            Matrix::CreateTranslation(Vector3(0,i==0?4.0f:i==1?.5f:-.5f,0)).ToColumnMajor(
                transforms.data()+i*16);
        d.SetVertexBuffer(nullptr);
        instances.SetDataRaw(transforms.data(),3,64);
        d.Clear(Color::Black);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                            VertexBufferBinding(&instances,1,1)});
        skin.Apply(); d.DrawInstancedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2,2);
        ExpectLocation(40,Color::Lime,"SplitSkinInstancingAfterBone",16);
        ExpectLocation(40,Color::Lime,"SplitSkinInstancingAfterBone",48);
        ExpectPixel(40,32,Color::Black,"InstanceGapRemainsClear");

#ifdef CNA_GAP14_PBR
        // CNAEXT contract: no lights/emission, ambient white, metallic=0, white texture =>
        // output is COLOR0. A second stream alternates red/green; stream 0 never changes.
        const VertexDeclaration colorDecl(4,{{0,F::Color,U::Color,0}});
        VertexBuffer colors(d,colorDecl,11,BufferUsage::None);
        skin.VertexColorEnabled = true;
        skin.setDiffuseColorProperty(Vector3(1,1,1));
        std::array<std::uint32_t,11> skinColors{};
        skinColors.fill(Color::Red.getPackedValueProperty());
        d.SetVertexBuffer(nullptr);
        colors.SetDataRaw(skinColors.data(),11,4);
        d.Clear(Color::Black);
        d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                            VertexBufferBinding(&colors,4)});
        skin.Apply(); d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
        ExpectLocation(40,Color::Red,"SkinColoredCanonicalLayoutAfterUncolored");
        skin.VertexColorEnabled = false;
        PbrEffect rigid(d); Configure(rigid,white); rigid.setMetallicFactorProperty(0);
        rigid.VertexColorEnabledEXT = true;
        SkinnedPbrEffect pbrSkin(d); Configure(pbrSkin,white); Palette(pbrSkin);
        pbrSkin.setMetallicFactorProperty(0);
        pbrSkin.VertexColorEnabledEXT = true;
        for (bool skinned : {false,true})
        {
            // Warm the uncolored canonical pipeline, then require the colored sibling below.
            d.Clear(Color::Black);
            d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5)});
            if (skinned) pbrSkin.Apply(); else rigid.Apply();
            d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
            ExpectLocation(skinned?40:8,Color::White,"PbrUncoloredCanonicalLayout");
            for (Color color : {Color::Red,Color::Lime})
            {
                std::array<std::uint32_t,11> packed{};
                packed.fill(Color::Blue.getPackedValueProperty());
                for (int i=5;i<11;++i) packed[i]=color.getPackedValueProperty();
                d.SetVertexBuffer(nullptr);
                colors.SetDataRaw(packed.data(),11,4);
                d.Clear(Color::Black);
                d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                                    VertexBufferBinding(&colors,4)});
                if (skinned) pbrSkin.Apply(); else rigid.Apply();
                d.DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2);
                ExpectLocation(skinned?40:8,color,"PbrSecondStreamColorAndIndependentOffset");
                d.Clear(Color::Black);
                d.SetVertexBuffers({VertexBufferBinding(&geometry,2),VertexBufferBinding(&bones,5),
                                    VertexBufferBinding(&colors,4),VertexBufferBinding(&instances,1,1)});
                if (skinned) pbrSkin.Apply(); else rigid.Apply();
                d.DrawInstancedPrimitives(PrimitiveType::TriangleList,1,0,6,3,2,2);
                ExpectLocation(skinned?40:8,color,"PbrSplitInstancing",16);
                ExpectLocation(skinned?40:8,color,"PbrSplitInstancing",48);
                ExpectPixel(skinned?40:8,32,Color::Black,"PbrInstanceGapRemainsClear");
            }
        }
#endif
        // A later ordinary draw after the instanced/split families must use only its own binding.
        d.Clear(Color::Black);
        BasicEffect ordinary(d);
        ordinary.setWorldProperty(Matrix::getIdentityProperty());
        ordinary.setViewProperty(Matrix::getIdentityProperty());
        ordinary.setProjectionProperty(Matrix::getIdentityProperty());
        ordinary.setDiffuseColorProperty(Vector3(0,1,0));
        ordinary.setLightingEnabledProperty(false);
        d.SetVertexBuffer(&geometry,3); ordinary.Apply();
        d.DrawPrimitives(PrimitiveType::TriangleList,0,2);
        ExpectLocation(8,Color::Lime,"LaterOrdinaryDrawUsesItsOwnBindings");
        ExpectPixel(8,16,Color::Black,"LaterOrdinaryDrawDoesNotRetainInstanceRate");
        d.SetVertexBuffer(nullptr); d.setIndicesProperty(nullptr);
        Exit();
    }
public:
    SplitSkinnedStreamsTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const { return failures_ == 0 && done_ ? 0 : 1; }
};
int main()
{
    try { SplitSkinnedStreamsTest game; game.Run(); return game.Result(); }
    catch (const std::exception& error) { std::fprintf(stderr,"[FAIL] %s\n",error.what()); return 1; }
}
