// SPDX-License-Identifier: MS-PL
// living-room-simulator's R-3: a glTF doubleSided material never reversed the normal on its back
// face, so the back of a leaf or a curtain was shaded as if it faced away from the light that was
// plainly falling on it. glTF (and its Sample Viewer) shade a double-sided back face with the whole
// tangent basis reversed.
//
// Camera and light both look down -Z at a quad in the XY plane. A face whose normal points at the
// camera is lit; the same quad authored to face away shows the camera its back, which must stay
// dark while single-sided and be lit exactly like the front once double-sided. A mirroring World
// reverses the on-screen winding, so gl_FrontFacing then names the other face; a double-sided
// front face must stay lit there too. Rigid and skinned programs, into a Vector4 target.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48);

    struct SkinnedPbrVertex
    {
        PbrVertex base;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    /**
     * @brief A full-viewport quad in the XY plane. Facing the camera it is counter-clockwise on
     * screen with normal +Z; facing away it is wound the other way with normal -Z.
     */
    std::vector<PbrVertex> Quad(bool facesCamera)
    {
        const float nz = facesCamera ? 1.0f : -1.0f;
        const auto at = [&](float x, float y, float u, float v) {
            return PbrVertex{x, y, 0.0f, 0.0f, 0.0f, nz, 1.0f, 0.0f, 0.0f, 1.0f, u, v};
        };
        const PbrVertex tl = at(-1.0f, 1.0f, 0.0f, 0.0f);
        const PbrVertex bl = at(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = at(1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = at(1.0f, 1.0f, 1.0f, 0.0f);
        if (facesCamera) { return {tl, bl, br, tl, br, tr}; }
        return {tl, br, bl, tl, tr, br};
    }

    template <typename Effect>
    void Configure(Effect& effect, const Matrix& world, bool doubleSided)
    {
        effect.setWorldProperty(world);
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 2.0f), Vector3::Zero,
                                                    Vector3::Up));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.5f, 4.0f));
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(0.6f);
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.setDoubleSidedEXTProperty(doubleSided);
        effect.setAmbientLightColorProperty(Vector3(0.1f, 0.1f, 0.1f));
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight0.setSpecularColorProperty(Vector3::One);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }
}

class PbrDoubleSidedTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

        struct Case
        {
            const char* name;
            bool facesCamera;
            bool doubleSided;
            Matrix world;
            bool lit;
        };
        const Matrix identity = Matrix::getIdentityProperty();
        const Case cases[] = {
            {"control: single-sided front face", true, false, identity, true},
            {"single-sided back face stays dark", false, false, identity, false},
            {"double-sided back face", false, true, identity, true},
            {"double-sided front face", true, true, identity, true},
            {"double-sided front face under a mirroring World", true, true,
             Matrix::CreateScale(-1.0f, 1.0f, 1.0f), true},
        };

        for (int program = 0; program < 2; ++program)
        {
            const char* name = program == 0 ? "PbrEffect" : "SkinnedPbrEffect";
            float control = 0.0f;
            for (const Case& testCase : cases)
            {
                const float value = Draw(device, target, program == 1, testCase.facesCamera,
                                         testCase.doubleSided, testCase.world);
                if (control == 0.0f) { control = value; }
                // Lit cases must match the control; the dark case must lose most of its light
                // (ambient 0.1 against a direct term several times that).
                const bool pass = std::isfinite(value) &&
                                  (testCase.lit ? std::fabs(value - control) <= 0.01f
                                                : value < control * 0.5f);
                std::printf("[%s] %s, %s: %.4f (control %.4f)\n", pass ? "PASS" : "FAIL", name,
                            testCase.name, value, control);
                if (!pass) { MarkFailedEXT(); }
            }
        }
        device.SetVertexBuffer(nullptr);
    }

private:
    /** @brief Draws one case and returns the red channel, the same at every covered pixel. */
    static float Draw(GraphicsDevice& device, RenderTarget2D& target, bool skinned,
                      bool facesCamera, bool doubleSided, const Matrix& world)
    {
        const std::vector<PbrVertex> rigid = Quad(facesCamera);
        std::vector<SkinnedPbrVertex> weighted;
        for (const PbrVertex& vertex : rigid)
            weighted.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        VertexBuffer buffer(device, static_cast<int>(rigid.size()));
        if (skinned)
            buffer.SetDataRaw(weighted.data(), static_cast<int>(weighted.size()),
                              static_cast<int>(sizeof(SkinnedPbrVertex)));
        else
            buffer.SetDataRaw(rigid.data(), static_cast<int>(rigid.size()),
                              static_cast<int>(sizeof(PbrVertex)));

        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 0));
        device.SetVertexBuffer(&buffer);
        if (skinned)
        {
            SkinnedPbrEffect effect(device);
            Configure(effect, world, doubleSided);
            effect.SetBoneTransforms({Matrix::getIdentityProperty()});
            effect.setWeightsPerVertexProperty(1);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        else
        {
            PbrEffect effect(device);
            Configure(effect, world, doubleSided);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        device.SetRenderTarget(nullptr);

        std::vector<Vector4> pixels(static_cast<std::size_t>(kSize * kSize));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(kSize / 2 * kSize + kSize / 2)].X;
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrDoubleSidedTest>();
}
