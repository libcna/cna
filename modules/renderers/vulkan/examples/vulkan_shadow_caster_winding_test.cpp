// SPDX-License-Identifier: MS-PL
// plans/plan_street.md STREET-0007: a shadow caster drawn with back-face culling keeps the faces
// that face the light.
//
// The Vulkan pipelines take clockwise-as-displayed as the front face, and every stock 3D vertex
// program flips clip-space Y into Vulkan's convention to keep that true. The engine layer's
// directional caster programs did not flip, so their winding was mirrored: with CullCounterClockwise
// -- what a game (cna-street) draws casters with -- the roof of every building and the ground were
// culled and the faces turned away from the light were stored instead. Shadows still appeared, from
// the wrong surfaces, and the frame disagreed with EasyGL's.
//
// A closed box, wound the XNA way (front faces clockwise as seen from outside), lit from straight
// above and drawn into cascade 0 with CullCounterClockwise:
//
//   A  the box is stored by its top face, whichever way up the map is -- the winding itself
//   B  the validation layer stayed silent
//   C  and it is where the receiver reads it (shadow_sampling.glsl reads the map top-down)
//   D  a tower whose top is between the light and the cascade's near plane still casts from its
//      top: the casters store depth the GL way (z*0.5+0.5 of a [-w, w] clip range) and the cascade
//      fit relies on GL keeping z in [-w, 0); Vulkan clipped it, and every tall building between
//      the sun and the street stopped casting
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = skipped (no cascades on this device).

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowQuality;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    Vector3 Cross(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
    }

    float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

    // Front faces clockwise as seen from outside: in XNA's right-handed space a triangle that
    // reads clockwise to a viewer has its (b-a)x(c-a) normal pointing away from that viewer.
    void AddFace(std::vector<VertexPositionNormalTexture>& out, const Vector3& n, Vector3 a,
                 Vector3 b, Vector3 c, Vector3 d)
    {
        const auto add = [&](Vector3 p, Vector3 q, Vector3 r) {
            if (Dot(Cross(q - p, r - p), n) > 0.0f) std::swap(q, r);
            out.push_back({p, n, Vector2(0, 0)});
            out.push_back({q, n, Vector2(0, 0)});
            out.push_back({r, n, Vector2(0, 0)});
        };
        add(a, b, c);
        add(a, c, d);
    }

    std::vector<VertexPositionNormalTexture> Box(float h, float top)
    {
        std::vector<VertexPositionNormalTexture> v;
        const float y0 = 0.0f, y1 = top;
        AddFace(v, Vector3(0, 1, 0), {-h, y1, -h}, {h, y1, -h}, {h, y1, h}, {-h, y1, h});
        AddFace(v, Vector3(0, -1, 0), {-h, y0, -h}, {h, y0, -h}, {h, y0, h}, {-h, y0, h});
        AddFace(v, Vector3(1, 0, 0), {h, y0, -h}, {h, y1, -h}, {h, y1, h}, {h, y0, h});
        AddFace(v, Vector3(-1, 0, 0), {-h, y0, -h}, {-h, y1, -h}, {-h, y1, h}, {-h, y0, h});
        AddFace(v, Vector3(0, 0, 1), {-h, y0, h}, {h, y0, h}, {h, y1, h}, {-h, y1, h});
        AddFace(v, Vector3(0, 0, -1), {-h, y0, -h}, {h, y0, -h}, {h, y1, -h}, {-h, y1, -h});
        return v;
    }

    Vector3 Project(const Matrix& m, const Vector3& p)
    {
        const float x = p.X * m.M11 + p.Y * m.M21 + p.Z * m.M31 + m.M41;
        const float y = p.X * m.M12 + p.Y * m.M22 + p.Z * m.M32 + m.M42;
        const float z = p.X * m.M13 + p.Y * m.M23 + p.Z * m.M33 + m.M43;
        const float w = p.X * m.M14 + p.Y * m.M24 + p.Z * m.M34 + m.M44;
        return Vector3(x / w, y / w, z / w);
    }
}

class VulkanShadowCasterWindingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    int result_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        CascadedShadowMap cascades(dev, ShadowQuality::Low, 2);
        ShaderEffect* caster = cascades.getCasterEffect();
        Texture2D* atlas = cascades.getShadowTexture();
        if (!cascades.isSupported() || caster == nullptr || atlas == nullptr
            || atlas->getFormatProperty() != SurfaceFormat::Single) {
            std::printf("[SKIP] no float cascade atlas on this device\n");
            result_ = 77;
            Exit();
            return;
        }

        const auto boxVertices = Box(2.0f, 4.0f);
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                        static_cast<int>(boxVertices.size()), BufferUsage::None);
        vb.SetData(boxVertices.data(), static_cast<int>(boxVertices.size()));

        DirectionalLightEXT sun;
        sun.Direction = Vector3(0.0f, -1.0f, 0.0f);
        const Matrix view = Matrix::CreateLookAt(Vector3(0, 6, 8), Vector3::Zero, Vector3::Up);
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 40.0f);
        cascades.update(sun, view, projection);

        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.setBlendStateProperty(BlendState::Opaque);
        cascades.begin(0);
        const Matrix identity = Matrix::getIdentityProperty();
        caster->SetUniformMat4("uWorld", &identity.M11);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, static_cast<int>(boxVertices.size()) / 3);
        cascades.end();

        const int width = atlas->getWidthProperty();
        const int height = atlas->getHeightProperty();
        std::vector<float> texels(static_cast<std::size_t>(width) * height);
        atlas->GetData(texels.data(), static_cast<int>(texels.size()));

        // What shadow_sampling.glsl reads for a world point: the cascade matrix's atlas
        // coordinate, with V read top-down.
        const Matrix toAtlas = cascades.getCascadeMatrix(0);
        const Vector3 topCentre = Project(toAtlas, Vector3(0, 4, 0));
        const Vector3 bottomCentre = Project(toAtlas, Vector3(0, 0, 0));
        const int x = std::clamp(static_cast<int>(topCentre.X * width), 0, width - 1);
        const int y = std::clamp(static_cast<int>((1.0f - topCentre.Y) * height), 0, height - 1);
        const int mirroredY = std::clamp(static_cast<int>(topCentre.Y * height), 0, height - 1);
        const float stored = texels[static_cast<std::size_t>(y) * width + x];
        const float mirrored = texels[static_cast<std::size_t>(mirroredY) * width + x];
        const float box = stored < 1.0f ? stored : mirrored;
        check(std::fabs(box - topCentre.Z) < 0.01f,
              "A the box is stored by the face that faces the light",
              "stored " + std::to_string(box) + ", top face " + std::to_string(topCentre.Z)
                  + ", bottom face " + std::to_string(bottomCentre.Z));
        check(std::fabs(stored - topCentre.Z) < 0.01f,
              "C the receiver's texel for the box is the one that holds it",
              "receiver reads " + std::to_string(stored) + ", mirrored texel "
                  + std::to_string(mirrored));

        {
            // The same cascade refitted, and a tower instead of the box: tall enough that its top
            // is nearer the light than the cascade's near plane, not so tall that GL clips it.
            cascades.update(sun, view, projection);
            const Matrix fitted = cascades.getCascadeMatrix(0);
            float towerTop = 4.0f;
            for (float t = 4.0f; t < 400.0f; t += 1.0f) {
                const float z = Project(fitted, Vector3(0, t, 0)).Z;   // z*0.5+0.5 of clip z
                if (z < 0.25f) { towerTop = t; break; }
            }
            const auto tower = Box(0.5f, towerTop);
            VertexBuffer towerVb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                 static_cast<int>(tower.size()), BufferUsage::None);
            towerVb.SetData(tower.data(), static_cast<int>(tower.size()));
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            cascades.begin(0);
            caster->SetUniformMat4("uWorld", &identity.M11);
            dev.SetVertexBuffer(&towerVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, static_cast<int>(tower.size()) / 3);
            cascades.end();
            atlas->GetData(texels.data(), static_cast<int>(texels.size()));
            const Vector3 top = Project(fitted, Vector3(0, towerTop, 0));
            const int tx = std::clamp(static_cast<int>(top.X * width), 0, width - 1);
            const int ty = std::clamp(static_cast<int>((1.0f - top.Y) * height), 0, height - 1);
            const float towerStored = texels[static_cast<std::size_t>(ty) * width + tx];
            check(top.Z < 0.5f && std::fabs(towerStored - top.Z) < 0.01f,
                  "D a caster nearer the light than the cascade's near plane still casts",
                  "tower top " + std::to_string(towerTop) + " m at stored depth "
                      + std::to_string(top.Z) + ", map holds " + std::to_string(towerStored));
        }

        if (auto* vulkan = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer())) {
            const auto& messages = vulkan->GetValidationMessagesEXT();
            check(messages.empty(), "B no validation messages",
                  messages.empty() ? "0 captured"
                                   : std::to_string(messages.size()) + " captured, first: "
                                         + messages.front());
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        result_ = fail_ > 0 ? 1 : 0;
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanShadowCasterWindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanShadowCasterWindingTest g;
    g.Run();
    return g.getResult();
}
