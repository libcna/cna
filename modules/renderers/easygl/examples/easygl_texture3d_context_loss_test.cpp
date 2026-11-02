// SPDX-License-Identifier: MS-PL
// CNA-FIX-D27: sample retained volumes after real native GL context destruction/recreation.
// CPU GetData alone cannot detect this defect because it reads the restoration shadow.

#include "common/PixelTestGame.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Texture3DContextLossTest final : public CNA::Examples::PixelTestGame
{
    Color Sample(ShaderEffect& effect, Texture3D& texture, int level,
                 int x, int y, int z, bool alphaOnly = false)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(11, 17, 23, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        effect.SetTexture(0, texture);
        effect.SetUniformInt("volume", 0);
        effect.SetUniformInt("alphaOnly", alphaOnly ? 1 : 0);
        effect.SetUniformFloat("level", static_cast<float>(level));
        effect.SetUniformVec3("coord",
            (x + 0.5f) / std::max(1, texture.getWidthProperty() >> level),
            (y + 0.5f) / std::max(1, texture.getHeightProperty() >> level),
            (z + 0.5f) / std::max(1, texture.getDepthProperty() >> level));
        const VertexPositionTexture quad[] = {
            {Vector3(-1, -1, 0), Vector2::Zero}, {Vector3(-1, 1, 0), Vector2::Zero},
            {Vector3(1, 1, 0), Vector2::Zero}, {Vector3(-1, -1, 0), Vector2::Zero},
            {Vector3(1, 1, 0), Vector2::Zero}, {Vector3(1, -1, 0), Vector2::Zero}};
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2,
                               viewport.getHeightProperty() / 2, 1, 1);
        Color result;
        device.GetBackBufferData(&centre, &result, 0, 1);
        return result;
    }

    void ExpectSample(const std::string& label, ShaderEffect& effect, Texture3D& texture,
                      int level, int x, int y, int z, const Color& expected, bool alphaOnly = false)
    {
        const Color actual = Sample(effect, texture, level, x, y, z, alphaOnly);
        const bool matches = std::abs(actual.getRProperty() - expected.getRProperty()) <= 1 &&
                             std::abs(actual.getGProperty() - expected.getGProperty()) <= 1 &&
                             std::abs(actual.getBProperty() - expected.getBProperty()) <= 1;
        if (!matches)
            std::printf("  actual=(%d,%d,%d), expected=(%d,%d,%d)\n",
                actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        ExpectTrue(label.c_str(), matches);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const bool desktop = device.GetGraphicsRendererType() == CNA::GraphicsRendererType::OpenGL33;
        const std::string prefix = desktop ? "#version 330 core\n" :
            "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
        const std::string vertexSource = prefix + "layout(location=0) in vec3 aPosition;\n"
                     "void main(){gl_Position=vec4(aPosition,1.0);}\n";
        const std::string fragmentSource = prefix +
                     "uniform sampler3D volume; uniform vec3 coord; uniform float level;\n"
                     "uniform bool alphaOnly; out vec4 FragColor;\n"
                     "void main(){vec4 value=textureLod(volume,coord,level);\n"
                     "FragColor=alphaOnly?vec4(value.aaa,1.0):value;}\n";

        Texture3D color(device, 4, 2, 4, true, SurfaceFormat::Color);
        std::array<std::vector<Color>, 3> levels;
        for (int level = 0; level < 3; ++level)
        {
            const int width = std::max(1, 4 >> level);
            const int height = std::max(1, 2 >> level);
            const int depth = std::max(1, 4 >> level);
            auto& data = levels[level];
            for (int z = 0; z < depth; ++z)
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        data.emplace_back(31 + x * 37, 43 + y * 53 + level * 29, 59 + z * 41, 255);
            color.SetData(level, 0, 0, width, height, 0, depth, data.data(), 0,
                          static_cast<int>(data.size()));
        }
        const Color patch[] = {Color::Red, Color::Green, Color::Blue, Color::Yellow};
        color.SetData(0, 1, 1, 3, 2, 1, 3, patch, 0, 4);
        for (int z = 0; z < 2; ++z)
            for (int x = 0; x < 2; ++x)
                levels[0][((z + 1) * 2 + 1) * 4 + x + 1] = patch[z * 2 + x];

        Texture3D alpha(device, 3, 1, 2, true, SurfaceFormat::Alpha8);
        const bytecs alphaData[] = {23, 47, 71, 113, 157, 199};
        const bytecs alphaMip[] = {83};
        alpha.SetData(alphaData, 6);
        alpha.SetData(1, 0, 0, 1, 1, 0, 1, alphaMip, 0, 1);
        Texture3D single(device, 2, 1, 2, false, SurfaceFormat::Single);
        const float singleData[] = {0.125f, 0.25f, 0.5f, 0.75f};
        single.SetData(singleData, 4);
        Texture3D packed(device, 1, 1, 2, true, SurfaceFormat::Bgr565);
        const SharpRuntime::UInt16 packedData[] = {0xf800, 0x001f};
        const SharpRuntime::UInt16 packedMip[] = {0x07e0};
        packed.SetData(packedData, 2);
        packed.SetData(1, 0, 0, 1, 1, 0, 1, packedMip, 0, 1);

        // Destroyed volumes must leave the recovery registry; a later loss must not revisit them.
        { Texture3D disposed(device, 1, 1, 1, false, SurfaceFormat::Color); }
        // A distinct decoy prevents residual upload bindings from satisfying sampling.
        Texture3D decoy(device, 1, 1, 1, false, SurfaceFormat::Color);
        const Color black = Color::Black;
        decoy.SetData(&black, 1);

        for (int epoch = 0; epoch < 3; ++epoch)
        {
            if (epoch != 0) device.GetRenderer().DebugSimulateContextLoss();
            const std::string label = "epoch " + std::to_string(epoch);
            // Custom-program recovery is a separate limitation (CNA-FIX-L47). Compile a fresh
            // sampling program for each context while retaining the original texture objects.
            ShaderEffect effect(device, vertexSource, fragmentSource);
            if (!ExpectTrue((label + " volume sampling shader compiles").c_str(),
                            effect.IsEffectValid())) return;
            // An upload after recovery must not be needed to resurrect the retained volumes.
            ExpectSample(label + " decoy", effect, decoy, 0, 0, 0, 0, black);
            for (int level = 0; level < 3; ++level)
            {
                const int width = std::max(1, 4 >> level);
                const int height = std::max(1, 2 >> level);
                const int depth = std::max(1, 4 >> level);
                for (int z = 0; z < depth; ++z)
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                            ExpectSample(label + " color L" + std::to_string(level) + " (" +
                                std::to_string(x) + "," + std::to_string(y) + "," +
                                std::to_string(z) + ")", effect, color, level, x, y, z,
                                levels[level][(z * height + y) * width + x]);
                std::vector<Color> readback(levels[level].size());
                color.GetData(level, 0, 0, width, height, 0, depth, readback.data(), 0,
                              static_cast<int>(readback.size()));
                ExpectTrue((label + " exact CPU shadow L" + std::to_string(level)).c_str(),
                           readback == levels[level]);
            }
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 3; ++x)
                {
                    const int value = alphaData[z * 3 + x];
                    ExpectSample(label + " Alpha8", effect, alpha, 0, x, 0, z,
                                 Color(value, value, value), true);
                }
            ExpectSample(label + " Alpha8 mip", effect, alpha, 1, 0, 0, 0,
                         Color(83, 83, 83), true);
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x)
                    ExpectSample(label + " Single", effect, single, 0, x, 0, z,
                                 Color(static_cast<int>(singleData[z * 2 + x] * 255 + 0.5f), 255, 255));
            ExpectSample(label + " Bgr565 red", effect, packed, 0, 0, 0, 0, Color::Red);
            ExpectSample(label + " Bgr565 blue", effect, packed, 0, 0, 0, 1, Color::Blue);
            ExpectSample(label + " Bgr565 mip", effect, packed, 1, 0, 0, 0, Color(0, 255, 0));

            // A box updated on the restored GL object must also become the next recovery shadow.
            const Color next(190 - epoch * 30, 91 + epoch * 20, 37, 255);
            try
            {
                color.SetData(1, 1, 0, 2, 1, 1, 2, &next, 0, 1);
                levels[1][3] = next;
                ExpectSample(label + " post-recovery update", effect, color, 1, 1, 0, 1, next);
            }
            catch (const std::exception& error)
            {
                std::printf("  update failed: %s\n", error.what());
                ExpectTrue((label + " post-recovery update succeeds").c_str(), false);
            }
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Texture3DContextLossTest>();
}
