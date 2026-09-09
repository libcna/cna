// SPDX-License-Identifier: MS-PL
// Task 132: EasyGL integration test — ShaderEffect (GLSL) with SpriteBatch.
//
// Renders a white 1×1 texture through the GLSL ES variant of the generated
// portable tint package, producing a red-tinted sprite over a green background.
//
// Vertex shader matches the SpriteBatch attribute layout exactly:
//   location 0 = vec2 aPos      (pixel coords)
//   location 1 = vec2 aTexCoord
//   location 2 = vec4 aColor
//   uniform mat4 projection     (set by SpriteBatch on the compiled program)
//
// Fragment shader:
//   FragColor = texture(texture1, TexCoord) * Color * uColor
//   → white texel × red uniform → red output
//
// The sampler2D 'texture1' uniform defaults to texture unit 0, where SpriteBatch
// binds the sprite texture. The package source and Vulkan bytecode share the same
// observable tint contract even though their renderer integration differs.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "common/PortableTintShaderPackage.generated.hpp"
#include "common/PortableTintShaderPackage.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// The checked-in package is generated from the adjacent declared GLSL source files.
static constexpr const char* kVertSrc =
    CNA::Examples::PortableTint::kEasyGlVertexSource.data();
static constexpr const char* kFragSrc =
    CNA::Examples::PortableTint::kEasyGlFragmentSource.data();

static const char* kBrokenFragSrc = R"(#version 300 es
precision mediump float;

out vec4 FragColor;
void main()
{
    FragColor = definitely_not_a_declared_value;
}
)";

class EasyGLShaderEffectTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_;
    bool                         done_   = false;
    int                          result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto dialect = device.GetShaderDialectEXT();
        const bool dialectOk = dialect == CNA::Internal::Renderers::ShaderDialectEXT::GlslEs;
        const bool graphicsLanguageOk = device.SupportsShaderLanguageEXT(
            CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex)
            && device.SupportsShaderLanguageEXT(
                CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment);
        const bool computeLanguageOk = device.SupportsShaderLanguageEXT(
            CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Compute)
            == device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders);
        const bool refusalOk = !device.SupportsShaderLanguageEXT(
            CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment)
            && !device.SupportsShaderLanguageEXT(
                CNA::ShaderLanguageEXT::Unknown, CNA::ShaderStageEXT::Vertex)
            && !device.SupportsShaderLanguageEXT(
                static_cast<CNA::ShaderLanguageEXT>(999), CNA::ShaderStageEXT::Fragment)
            && !device.SupportsShaderLanguageEXT(
                CNA::ShaderLanguageEXT::GlslEs, static_cast<CNA::ShaderStageEXT>(999));
        if (!dialectOk || !graphicsLanguageOk || !computeLanguageOk || !refusalOk)
        {
            std::printf("[FAIL] EasyGLShaderEffect: shader language contract mismatch "
                        "(dialect=%d, graphics=%d, compute=%d, refusal=%d)\n",
                        static_cast<int>(dialect), graphicsLanguageOk, computeLanguageOk,
                        refusalOk);
            done_ = true;
            Exit();
            return;
        }
        std::printf("[PASS] EasyGLShaderEffect: GLSL language/stage contract\n");

        ShaderEffect broken(device, kVertSrc, kBrokenFragSrc);
        const auto diagnostics = broken.GetShaderDiagnosticsEXT();
        const bool brokenDiagnosticOk = !broken.IsEffectValid() && !diagnostics.empty()
            && diagnostics.front().getSeverity()
                == CNA::ShaderDiagnosticSeverityEXT::Error
            && diagnostics.front().getStage() == CNA::ShaderStageEXT::Fragment
            && diagnostics.front().getSourceLabel().empty()
            && diagnostics.front().getLine() == 7
            && diagnostics.front().getColumn() >= 0
            && !diagnostics.front().getMessage().empty();
        if (!brokenDiagnosticOk)
        {
            std::printf("[FAIL] EasyGLShaderEffect: broken GLSL diagnostic was not structured "
                        "(count=%zu, stage=%d, line=%d)\n",
                        diagnostics.size(),
                        diagnostics.empty() ? -1
                            : static_cast<int>(diagnostics.front().getStage()),
                        diagnostics.empty() ? -1 : diagnostics.front().getLine());
            done_ = true;
            Exit();
            return;
        }
        std::printf("[PASS] EasyGLShaderEffect: broken GLSL diagnostic stage=fragment line=7\n");

        sb_ = std::make_unique<SpriteBatch>(device);

        // 1×1 solid-white texture — the red-tint shader will colourise it.
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        bool packageSelectionOk = true;
        std::unique_ptr<ShaderEffect> ownedEffect;
#ifdef CNA_CNAEXT
        const auto package = CNA::Examples::PortableTint::CreatePackage();
        const auto selection = package.selectFor(device);
        ownedEffect = std::make_unique<ShaderEffect>(device, package);
        packageSelectionOk = selection.isUsable()
            && selection.getLanguage() == CNA::ShaderLanguageEXT::GlslEs
            && ownedEffect->GetSelectedShaderLanguageEXT() == CNA::ShaderLanguageEXT::GlslEs
            && selection.findStage(CNA::ShaderStageEXT::Vertex) != nullptr
            && selection.findStage(CNA::ShaderStageEXT::Fragment) != nullptr;
        std::printf("[%s] MOD-2217: portable package selected GLSL ES on EasyGL\n",
                    packageSelectionOk ? "PASS" : "FAIL");
#else
        // The legacy constructor keeps this renderer test usable in the intentional CNAEXT-off
        // configuration; MOD-2217's package-selection leg runs in the CNAEXT-on configuration.
        ownedEffect = std::make_unique<ShaderEffect>(device, kVertSrc, kFragSrc);
#endif
        ShaderEffect& fx = *ownedEffect;

        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] EasyGLShaderEffect: GLSL compile failed\n");
            Exit();
            return;
        }

        fx.Apply();
        fx.SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   nullptr, nullptr, nullptr, &fx);
        sb_->Draw(tex_,
                  Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();

        // Centre pixel should be red (white texture × red-tint shader).
        // Corner pixel should be green (uncleared background).
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        const Rectangle bgReg(1, 1, 1, 1);
        Color centPx(0, 0, 0, 0);
        Color bgPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);
        device.GetBackBufferData(&bgReg,   &bgPx,   0, 1);

        const bool centOk = (centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50);
        const bool bgOk   = (bgPx.getGProperty()   >= 200 && bgPx.getRProperty()   <= 50);

        if (centOk && bgOk && packageSelectionOk)
        {
            std::printf("[PASS] EasyGLShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n"
                        "       expected: centre=red (R>=200,G<=50), bg=green (G>=200,R<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLShaderEffectTest game;
    game.Run();
    return game.getResult();
}
