// SPDX-License-Identifier: MS-PL
// REMED-GFX-137: every EasyGL cube-sampling path must keep a rendered face upright.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;
constexpr int kFace = 16;

const char* kVertexShader = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
void main(){ gl_Position=vec4(aPosition.xy,0.0,1.0); }
)";

const char* kFragmentShader = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform samplerCube CubeSampler;
uniform vec3 direction;
uniform vec4 uRtCubeFlipV;
vec3 cnaCubeDir(vec3 d,float flip){
    if(flip<0.5)return d;
    vec3 a=abs(d);
    bool yf=a.y>a.x&&a.y>a.z;
    vec3 r=yf?vec3(d.x,d.y,-d.z):vec3(d.x,-d.y,d.z);
    float m=max(a.x,max(a.y,a.z));
    float lim=m*max(0.0,1.0-1.0/float(textureSize(CubeSampler,0).x));
    if(yf)r.xz=clamp(r.xz,vec2(-lim),vec2(lim));
    else if(a.x>=a.z)r.yz=clamp(r.yz,vec2(-lim),vec2(lim));
    else r.xy=clamp(r.xy,vec2(-lim),vec2(lim));
    return r;
}
void main(){ FragColor=texture(CubeSampler,cnaCubeDir(direction,uRtCubeFlipV.x)); }
)";

bool Near(const Color& c, int r, int g, int b)
{
    return std::abs(c.getRProperty() - r) < 60
        && std::abs(c.getGProperty() - g) < 60
        && std::abs(c.getBProperty() - b) < 60;
}
} // namespace

class RenderTargetCubeSamplingOrientationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    Color ReadPixel(int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    static void Quad(VertexPositionNormalTangentTexture (&quad)[6])
    {
        Vector3 up(0.0f, 0.25f, 1.0f);
        Vector3 down(0.0f, -0.25f, 1.0f);
        up.Normalize();
        down.Normalize();
        const Vector4 tangent(1, 0, 0, 1);
        const VertexPositionNormalTangentTexture tl(Vector3(-1.5f, 1.5f, 0), up, tangent, Vector2(0, 0));
        const VertexPositionNormalTangentTexture tr(Vector3(1.5f, 1.5f, 0), up, tangent, Vector2(1, 0));
        const VertexPositionNormalTangentTexture bl(Vector3(-1.5f, -1.5f, 0), down, tangent, Vector2(0, 1));
        const VertexPositionNormalTangentTexture br(Vector3(1.5f, -1.5f, 0), down, tangent, Vector2(1, 1));
        const VertexPositionNormalTangentTexture values[6] = {tl, bl, br, tl, br, tr};
        for (int i = 0; i < 6; ++i) quad[i] = values[i];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255});
        Texture2D black = Texture2D::CreateFromPixels(device, 1, 1, {0, 0, 0, 255});
        Texture2D scaleOnlyLut = Texture2D::CreateFromPixels(device, 1, 1, {255, 0, 0, 255});
        TextureCube blackCube(device, 1, false, SurfaceFormat::Color);
        const Color blackPixel = Color::Black;
        for (const CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
             CubeMapFace::PositiveY, CubeMapFace::NegativeY,
             CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
            blackCube.SetData(face, &blackPixel, 1);
        RenderTargetCube cube(device, kFace, false, SurfaceFormat::Color, DepthFormat::None);
        SpriteBatch batch(device);
        const CubeMapFace faces[6] = {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
        for (int i = 0; i < 6; ++i)
        {
            device.SetRenderTarget(&cube, faces[i]);
            device.Clear(Color::Black);
            batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                        nullptr, nullptr);
            if (i == 2) batch.Draw(white, Rectangle(0, 0, kFace, kFace), Color::Red);
            else if (i == 3) batch.Draw(white, Rectangle(0, 0, kFace, kFace), Color::Blue);
            else
            {
                batch.Draw(white, Rectangle(0, 0, kFace, kFace / 2), Color::Red);
                batch.Draw(white, Rectangle(0, kFace / 2, kFace, kFace / 2), Color::Blue);
            }
            batch.End();
        }
        device.SetRenderTarget(nullptr);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        VertexPositionNormalTangentTexture quad[6];
        Quad(quad);
        VertexBuffer vertices(device,
            VertexPositionNormalTangentTexture::getVertexDeclarationStatic(), 6,
            BufferUsage::None);
        vertices.SetData(quad, 6);
        device.SetVertexBuffer(&vertices);

        device.Clear(Color::Black);
        PbrEffect pbr(device);
        pbr.setWorldProperty(Matrix::getIdentityProperty());
        pbr.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 3), Vector3::Zero, Vector3::Up));
        pbr.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::ToRadians(60.0f), 1.0f, 0.1f, 20.0f));
        pbr.setTextureProperty(&white);
        pbr.setDiffuseColorProperty(Vector3::One);
        pbr.setMetallicFactorProperty(0.0f);
        pbr.setRoughnessFactorProperty(1.0f);
        pbr.setEncodeOutputToSrgbEXTProperty(false);
        ImageBasedLightEXT ibl;
        ibl.Irradiance = &cube;
        ibl.PrefilteredSpecular = &cube;
        ibl.BrdfLut = &black;
        pbr.setImageBasedLightEXT(ibl);
        pbr.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        Check(Near(ReadPixel(kSize / 2, kSize / 2 - 10), 255, 0, 0),
              "PBR IBL normal tilted up samples the rendered face's upper half");
        Check(Near(ReadPixel(kSize / 2, kSize / 2 + 10), 0, 0, 255),
              "PBR IBL normal tilted down samples the rendered face's lower half");

        // Isolate the second IBL cube: a metallic surface has no irradiance term and the (1,0)
        // LUT makes the prefiltered sample the complete result.
        device.Clear(Color::Black);
        ibl.Irradiance = &blackCube;
        ibl.PrefilteredSpecular = &cube;
        ibl.BrdfLut = &scaleOnlyLut;
        pbr.setMetallicFactorProperty(1.0f);
        pbr.setImageBasedLightEXT(ibl);
        pbr.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        Check(Near(ReadPixel(kSize / 2, kSize / 2 - 10), 255, 0, 0),
              "PBR IBL prefiltered cube keeps the rendered upper half upright");
        Check(Near(ReadPixel(kSize / 2, kSize / 2 + 10), 0, 0, 255),
              "PBR IBL prefiltered cube keeps the rendered lower half upright");

        ShaderEffect custom(device, kVertexShader, kFragmentShader);
        custom.SetUniformInt("CubeSampler", 0);
        custom.setWorldProperty(Matrix::getIdentityProperty());
        custom.setViewProperty(Matrix::getIdentityProperty());
        custom.setProjectionProperty(Matrix::getIdentityProperty());
        auto DrawCustom = [&](const Vector3& direction) {
            device.Clear(Color::Black);
            custom.Apply();
            custom.SetTexture(0, cube);
            custom.SetUniformVec3("direction", direction.X, direction.Y, direction.Z);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            return ReadPixel(kSize / 2, kSize / 2);
        };
        Check(Near(DrawCustom(Vector3(0, 0.25f, 1)), 255, 0, 0),
              "ShaderEffect cube flip contract keeps a rendered upper texel above");
        Check(Near(DrawCustom(Vector3(0, -0.25f, 1)), 0, 0, 255),
              "ShaderEffect cube flip contract keeps a rendered lower texel below");
        Check(Near(DrawCustom(Vector3(0, 1, 1)), 255, 0, 0),
              "ShaderEffect cube flip contract keeps GLES seamless filtering inside the chosen face");

        // Binding an ordinary uploaded cube after the rendered one must clear the unit's flag.
        TextureCube uploaded(device, kFace, false, SurfaceFormat::Color);
        std::vector<Color> uploadedPixels(static_cast<std::size_t>(kFace * kFace), Color::Blue);
        for (int y = 0; y < kFace / 2; ++y)
            for (int x = 0; x < kFace; ++x)
                uploadedPixels[static_cast<std::size_t>(y * kFace + x)] = Color::Red;
        for (const CubeMapFace face : faces)
            uploaded.SetData(face, uploadedPixels.data(), static_cast<int>(uploadedPixels.size()));
        auto DrawUploaded = [&](const Vector3& direction) {
            device.Clear(Color::Black);
            custom.Apply();
            custom.SetTexture(0, uploaded);
            custom.SetUniformVec3("direction", direction.X, direction.Y, direction.Z);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            return ReadPixel(kSize / 2, kSize / 2);
        };
        Check(Near(DrawUploaded(Vector3(0, 0.25f, 1)), 255, 0, 0),
              "ShaderEffect leaves an uploaded cube's upper half unflipped");
        Check(Near(DrawUploaded(Vector3(0, -0.25f, 1)), 0, 0, 255),
              "ShaderEffect leaves an uploaded cube's lower half unflipped");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    RenderTargetCubeSamplingOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetCubeSamplingOrientationTest game;
    game.Run();
    return game.Result();
}
