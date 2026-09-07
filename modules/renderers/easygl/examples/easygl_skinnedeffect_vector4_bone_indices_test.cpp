// SPDX-License-Identifier: MS-PL
// plans/plan_fx.md FX-127: a BLENDINDICES element may be declared Vector4, not only Byte4.
//
// XNA's VertexElementFormat describes the bytes in the buffer. A content processor is free to
// write either form, and XNA renders both. CustomModelAnimation's own SkinnedModelProcessor does
// exactly that -- `ConvertChannelContent<Vector4>("BlendIndices0")` -- and real XNA draws the
// result; EasyGL originally refused it outright:
//
//   EasyGL: this VertexDeclaration cannot be bound to the stock 'skinned3d_vertexlit' program --
//   element 1 declares BlendIndices0@12 Vector4 but shader input 'aBoneIndices' expects Byte4.
//
// EasyGL uses one floating-point attribute and casts to int() when indexing uBones[]. D3D11/12
// select uint4 or float4 stock vertex-shader signatures because attaching R8G8B8A8_UINT to a
// float4 signature bit-reinterprets the values instead of numerically converting them.
//
// Six checks, because "it draws something" is not the same as "it read the index":
//   (a) a Vector4-declared BLENDINDICES draws at all -- before the fix this threw;
//   (b) index 1 really selects bone 1: with bone 0 pushed off screen and bone 1 identity, the
//       quad stays at the centre. An attribute stuck at its default would select bone 0 and the
//       centre would be background;
//   (c) the same scene with index 0 IS background, so (b) is not passing by luck;
//   (d) a Byte4-declared BLENDINDICES still draws -- the format that always worked must not regress.
//   (e-g) Vector4 indices also select bone 1 through per-pixel lighting and both COLOR0 variants.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// pos(12) + normal(12) + uv(8) + weights Vector4(16) + indices Vector4(16) = 64 bytes,
    /// the layout SkinnedModelProcessor's Vector4 conversion produces.
    struct FloatIndexVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        float i0, i1, i2, i3;
    };
    static_assert(sizeof(FloatIndexVertex) == 64, "the layout under test is the 64-byte one");

    /// The same vertex with the ordinary Byte4 indices: 12 + 12 + 8 + 16 + 4 = 52 bytes.
    struct ByteIndexVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(ByteIndexVertex) == 52, "the Byte4 control layout is the 52-byte one");

    struct FloatIndexColorVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        float i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(FloatIndexColorVertex) == 68,
                  "the Vector4-index layout with packed color is 68 bytes");
}

class SkinnedEffectVector4BoneIndicesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        ok ? ++pass_ : ++fail_;
    }

    /// The texture is red and the background green, so "the quad covers this pixel" is simply
    /// "red dominates".
    static bool isQuad(const Color& c)
    {
        return c.getRProperty() > c.getGProperty() + 40;
    }

    Color readCentre(GraphicsDevice& device)
    {
        const auto& vp = device.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        device.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

    /// Bone 0 pushes the quad far off screen; bone 1 leaves it where it is. Which bone the draw
    /// used is therefore readable from one pixel.
    void ApplyEffect(GraphicsDevice& device, SkinnedEffect& fx,
                     bool preferPerPixel = false, bool vertexColor = false)
    {
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        const std::vector<Matrix> bones = {
            Matrix::CreateTranslation(10.0f, 0.0f, 0.0f),
            Matrix::getIdentityProperty(),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.setPreferPerPixelLightingProperty(preferPerPixel);
        fx.VertexColorEnabled = vertexColor;
        fx.EnableDefaultLighting();
        fx.Apply();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    void PrepareFrame(GraphicsDevice& device)
    {
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        const std::vector<std::uint8_t> px = { 255, 0, 0, 255 };
        tex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        const VertexDeclaration floatIndexDeclaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0),
        };
        const VertexDeclaration byteIndexDeclaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        };
        const VertexDeclaration floatIndexColorDeclaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0),
            VertexElement(64, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        };

        const auto floatQuad = [](float boneIndex) {
            const float b = boneIndex;
            return std::vector<FloatIndexVertex>{
                { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  b,0,0,0 },
                { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  b,0,0,0 },
                {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  b,0,0,0 },
                { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  b,0,0,0 },
                {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  b,0,0,0 },
                {  1,  1, 0,  0,0,1,  1,0,  1,0,0,0,  b,0,0,0 },
            };
        };
        const auto floatColorQuad = [](float boneIndex) {
            const float b = boneIndex;
            return std::vector<FloatIndexColorVertex>{
                { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
                { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
                {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
                { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
                {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
                {  1,  1, 0,  0,0,1,  1,0,  1,0,0,0,  b,0,0,0,  255,255,255,255 },
            };
        };

        // (a)+(b): a Vector4 BLENDINDICES of 1 selects bone 1, which is the identity.
        PrepareFrame(device);
        SkinnedEffect boneOneEffect(device);
        ApplyEffect(device, boneOneEffect);
        const std::vector<FloatIndexVertex> boneOne = floatQuad(1.0f);
        VertexBuffer boneOneBuffer(device, floatIndexDeclaration, 6, BufferUsage::WriteOnly);
        boneOneBuffer.SetDataRaw(boneOne.data(), 6, static_cast<int>(sizeof(FloatIndexVertex)));
        device.SetVertexBuffer(&boneOneBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color boneOnePixel = readCentre(device);

        // (c): the same declaration with index 0 selects the bone that moves it off screen.
        PrepareFrame(device);
        SkinnedEffect boneZeroEffect(device);
        ApplyEffect(device, boneZeroEffect);
        const std::vector<FloatIndexVertex> boneZero = floatQuad(0.0f);
        VertexBuffer boneZeroBuffer(device, floatIndexDeclaration, 6, BufferUsage::WriteOnly);
        boneZeroBuffer.SetDataRaw(boneZero.data(), 6, static_cast<int>(sizeof(FloatIndexVertex)));
        device.SetVertexBuffer(&boneZeroBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color boneZeroPixel = readCentre(device);

        // (d): the Byte4 spelling still works.
        PrepareFrame(device);
        SkinnedEffect byteEffect(device);
        ApplyEffect(device, byteEffect);
        const ByteIndexVertex byteQuad[6] = {
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  1,0,0,0 },
            { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  1,0,0,0 },
            {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  1,0,0,0 },
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  1,0,0,0 },
            {  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  1,0,0,0 },
            {  1,  1, 0,  0,0,1,  1,0,  1,0,0,0,  1,0,0,0 },
        };
        VertexBuffer byteBuffer(device, byteIndexDeclaration, 6, BufferUsage::WriteOnly);
        byteBuffer.SetDataRaw(byteQuad, 6, static_cast<int>(sizeof(ByteIndexVertex)));
        device.SetVertexBuffer(&byteBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color bytePixel = readCentre(device);

        // The other three typed signatures must execute too: per-pixel lighting and both COLOR0
        // siblings. The same off-screen bone discriminator proves each still reads index 1.
        PrepareFrame(device);
        SkinnedEffect floatPerPixelEffect(device);
        ApplyEffect(device, floatPerPixelEffect, true, false);
        VertexBuffer floatPerPixelBuffer(
            device, floatIndexDeclaration, 6, BufferUsage::WriteOnly);
        floatPerPixelBuffer.SetDataRaw(
            boneOne.data(), 6, static_cast<int>(sizeof(FloatIndexVertex)));
        device.SetVertexBuffer(&floatPerPixelBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color floatPerPixel = readCentre(device);

        const std::vector<FloatIndexColorVertex> coloredBoneOne = floatColorQuad(1.0f);
        PrepareFrame(device);
        SkinnedEffect coloredVertexLitEffect(device);
        ApplyEffect(device, coloredVertexLitEffect, false, true);
        VertexBuffer coloredVertexLitBuffer(
            device, floatIndexColorDeclaration, 6, BufferUsage::WriteOnly);
        coloredVertexLitBuffer.SetDataRaw(
            coloredBoneOne.data(), 6, static_cast<int>(sizeof(FloatIndexColorVertex)));
        device.SetVertexBuffer(&coloredVertexLitBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color coloredVertexLit = readCentre(device);

        PrepareFrame(device);
        SkinnedEffect coloredPerPixelEffect(device);
        ApplyEffect(device, coloredPerPixelEffect, true, true);
        VertexBuffer coloredPerPixelBuffer(
            device, floatIndexColorDeclaration, 6, BufferUsage::WriteOnly);
        coloredPerPixelBuffer.SetDataRaw(
            coloredBoneOne.data(), 6, static_cast<int>(sizeof(FloatIndexColorVertex)));
        device.SetVertexBuffer(&coloredPerPixelBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color coloredPerPixel = readCentre(device);

        check(isQuad(boneOnePixel),
              "(a)+(b) a Vector4 BLENDINDICES draws, and index 1 selects bone 1", boneOnePixel);
        check(!isQuad(boneZeroPixel),
              "(c) index 0 selects bone 0, which moves the quad off screen", boneZeroPixel);
        check(isQuad(bytePixel),
              "(d) the Byte4 spelling still draws", bytePixel);
        check(isQuad(floatPerPixel),
              "(e) Vector4 indices draw through the per-pixel-lit signature", floatPerPixel);
        check(isQuad(coloredVertexLit),
              "(f) Vector4 indices draw through the vertex-lit COLOR0 signature", coloredVertexLit);
        check(isQuad(coloredPerPixel),
              "(g) Vector4 indices draw through the per-pixel-lit COLOR0 signature", coloredPerPixel);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    /** @brief Creates the device manager the test draws through. */
    SkinnedEffectVector4BoneIndicesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    /** @brief Whether every check passed. */
    [[nodiscard]] bool Passed() const { return fail_ == 0 && pass_ > 0; }
};

int main()
{
    SkinnedEffectVector4BoneIndicesTest test;
    test.Run();
    return test.Passed() ? 0 : 1;
}
