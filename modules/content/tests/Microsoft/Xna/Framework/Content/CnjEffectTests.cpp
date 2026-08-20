// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-15/CNB-16: first-ever gtest coverage for EffectTypeReader, migrated from
// .shader.json to .cnj (CNB-14). The only prior exercise of this reader anywhere in the repo was
// modules/renderers/easygl/examples/easygl_bloom_extract_test.cpp (a standalone example program, not part of CnaTests),
// which CNB-15 also migrated to .cnj.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "CNA/GraphicsCapability.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_effect_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    FragColor = texture(texture1, TexCoord);
}
)";
}

class CnjEffectTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjEffectTest, LoadsRealCnjFixture)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "passthrough.vert.glsl", kVertSrc);
    WriteFile(root.path() / "passthrough.frag.glsl", kFragSrc);
    WriteFile(root.path() / "passthrough.cnj", R"({
        "cnjVersion": 1,
        "type": "Effect",
        "vertex": "passthrough.vert.glsl",
        "fragment": "passthrough.frag.glsl"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("passthrough");

    ASSERT_NE(loaded, nullptr);
    auto* shaderEffect = dynamic_cast<ShaderEffect*>(loaded.get());
    ASSERT_NE(shaderEffect, nullptr);
    // A renderer with no shader compiler (OpenGL ES 1.1's fixed-function pipeline has none at all)
    // cannot produce a valid custom-GLSL effect. Gate on the capability rather than the renderer
    // name, matching CnjTexture3DTests' own convention for the same situation.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
    {
        EXPECT_FALSE(shaderEffect->IsEffectValid());
        return;
    }
    EXPECT_TRUE(shaderEffect->IsEffectValid());
}

TEST_F(CnjEffectTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "wrong.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Effect>>("wrong"), ContentLoadException);
}
