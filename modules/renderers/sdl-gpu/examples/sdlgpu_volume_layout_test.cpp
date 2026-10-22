// SPDX-License-Identifier: MS-PL
// Isolated museum tex-volshader reproducer; one layout per process.
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/InvalidOperationException.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VolumeLayoutProbe final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::string mode_;
    int result_ = 1;

    void Draw(const GameTime &) override
    {
        auto &device = getGraphicsDeviceProperty();
        const bool native2d = mode_ == "native2d";
        const bool reject =
            mode_ == "invalid_volume" || mode_ == "mismatched_volume" || mode_ == "sampler_array";
        const bool missing = mode_ == "missing_volume";
        const bool noUniforms = mode_ == "zero_uniforms";
        const bool noSamplers = mode_ == "zero_samplers";
        const int uniformSet = mode_ == "portable_uniforms" ? 3 : 1;
        const bool nativeTexture =
            native2d || mode_ == "native_volume" || missing || noUniforms || mode_ == "sampler_array";
        const int textureSet = mode_ == "invalid_volume" ? 6 : nativeTexture ? 2 : 1;
        const int textureBinding =
            nativeTexture || mode_ == "invalid_volume" || mode_ == "mismatched_volume" ? 0 : 8;
        std::string vertex = R"GLSL(#version 450
layout(location=0) in vec2 inPos;
layout(location=1) in vec2 inUV;
layout(location=2) in vec4 inColor;
layout(location=0) out vec2 uv;
layout(set=)GLSL" + std::to_string(uniformSet) +
                             R"GLSL(,binding=0) uniform PC {
    vec4 vpSize_pad; mat4 matrix; vec4 color; vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc=(inPos/pc.vpSize_pad.xy)*2.0-1.0;
    gl_Position=vec4(ndc.x,-ndc.y,0.0,1.0);
    uv=inUV;
}
)GLSL";
        if (noUniforms)
        {
            const auto begin = vertex.find("layout(set=");
            vertex.erase(begin, vertex.find("void main()", begin) - begin);
            vertex.replace(vertex.find("pc.vpSize_pad.xy"), std::string("pc.vpSize_pad.xy").size(),
                           "vec2(64.0)");
        }
        std::string fragment = R"GLSL(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(set=)GLSL" + std::to_string(textureSet) +
                               ",binding=" + std::to_string(textureBinding) + ") uniform " +
                               (native2d ? "sampler2D" : "sampler3D") + R"GLSL( image;
void main() { outColor=texture(image,)GLSL" +
                               (native2d ? "uv" : "vec3(uv,0.75)") + "); }\n";
        if (mode_ == "sampler_array")
        {
            fragment.replace(fragment.find(" image;"), 7, " image[2];");
            fragment.replace(fragment.find("texture(image,"), 14, "texture(image[0],");
        }
        if (noSamplers)
            fragment = "#version 450\nlayout(location=0) out vec4 outColor;\n"
                       "void main(){outColor=vec4(32.0,64.0,128.0,255.0)/255.0;}\n";
        ShaderEffect effect(device, vertex, fragment);
        std::printf("mode=%s valid=%d diagnostic=%s\n", mode_.c_str(), effect.IsEffectValid(),
                    effect.GetCompileErrorEXT().c_str());
        std::fflush(stdout);
        if (reject)
        {
            result_ = !effect.IsEffectValid() && !effect.GetCompileErrorEXT().empty() ? 0 : 1;
            Exit();
            return;
        }
        if (!effect.IsEffectValid())
        {
            Exit();
            return;
        }
        RenderTarget2D target(device, 64, 64, false, SurfaceFormat::Color, DepthFormat::None);
        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        const std::array<Color, 8> voxels{Color::Black,       Color::Black,       Color::Black,
                                          Color::Black,       Color(32, 64, 128), Color(128, 32, 64),
                                          Color(64, 128, 32), Color(192, 64, 32)};
        Texture3D volume(device, 2, 2, 2, false, SurfaceFormat::Color);
        volume.SetData(voxels.data(), 8);
        if (!missing)
            effect.SetTexture(0, volume);
        SpriteBatch batch(device);
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &SamplerState::PointClamp, nullptr,
                    nullptr, &effect);
        bool missingRefused = false;
        try
        {
            batch.Draw(white, Rectangle(0, 0, 64, 64), Color::White);
        }
        catch (const System::InvalidOperationException &)
        {
            if (!missing)
                throw;
            missingRefused = true;
        }
        batch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D *>(nullptr));
        if (missing)
        {
            result_ = missingRefused ? 0 : 1;
            std::printf("VOLUME_LAYOUT missing texture refused=%d\n", missingRefused);
            Exit();
            return;
        }
        std::vector<Color> pixels(64 * 64);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        int wrong = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const Color &expected = native2d     ? whitePixel
                                        : noSamplers ? voxels[4]
                                                     : voxels[4 + (y / 32) * 2 + x / 32];
                if (pixels[y * 64 + x] != expected)
                    ++wrong;
            }
        std::printf("VOLUME_LAYOUT mode=%s pixels=4096 wrong=%d\n", mode_.c_str(), wrong);
        result_ = wrong == 0 ? 0 : 1;
        Exit();
    }

  public:
    explicit VolumeLayoutProbe(std::string mode) : mode_(std::move(mode))
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const
    {
        return result_;
    }
};

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const std::string mode = argv[1];
    if (mode != "native2d" && mode != "native_volume" && mode != "portable_volume" &&
        mode != "portable_uniforms" && mode != "invalid_volume" && mode != "missing_volume" &&
        mode != "mismatched_volume" && mode != "sampler_array" && mode != "zero_uniforms" &&
        mode != "zero_samplers")
        return 2;
    try
    {
        VolumeLayoutProbe game(mode);
        game.Run();
        return game.Result();
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 2;
    }
}
