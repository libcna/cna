// SPDX-License-Identifier: MS-PL
// ShaderEffect's two refusals: a shader package with no variant the renderer accepts is refused
// with a structured ShaderCompilationExceptionEXT, and a uniform array past the renderer's
// capacity is refused by name rather than silently truncated.
//
// Moved from the retired VULKAN suite (MOD-2217, VULKAN-251, VULKAN-252/265). The portable tint
// package (common/PortableTintShaderPackage.hpp) carries GLSL ES, desktop GLSL, SPIR-V and WGSL
// variants, so the program lets selectFor() pick this renderer's variant and builds its refusal
// case from a language this renderer rejects for both stages.
//
//   A  the package selects a usable variant, the effect compiles in that language, and a Clone
//      keeps it
//   B  two uColor values drawn through SpriteBatch give two colours -- the drawn colour is a
//      function of the uniform, which also proves the effect the later legs talk to is live
//   C  a package holding only a rejected language: selectFor() is unusable and names the
//      language, both stage labels and "renderer rejects <language>/<stage>" for each stage;
//      constructing the effect throws ShaderCompilationExceptionEXT carrying exactly one
//      labelled diagnostic per stage, each repeating the selection diagnostic
//   D  the four array setters accept a count within capacity
//   E  a 1000-element mat4 array is refused with NotSupportedException naming the count
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display, CNA_CNAEXT off, or no package variant
// this renderer accepts).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/ShaderDiagnosticEXT.hpp"
#include "common/PortableTintShaderPackage.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;

namespace
{
    const char* LanguageName(CNA::ShaderLanguageEXT language)
    {
        switch (language)
        {
            case CNA::ShaderLanguageEXT::GlslDesktop: return "GlslDesktop";
            case CNA::ShaderLanguageEXT::GlslEs: return "GlslEs";
            case CNA::ShaderLanguageEXT::GlslVulkan: return "GlslVulkan";
            case CNA::ShaderLanguageEXT::Hlsl: return "Hlsl";
            case CNA::ShaderLanguageEXT::Msl: return "Msl";
            case CNA::ShaderLanguageEXT::Wgsl: return "Wgsl";
            case CNA::ShaderLanguageEXT::SpirV: return "SpirV";
            case CNA::ShaderLanguageEXT::Dxil: return "Dxil";
            default: return "Unknown";
        }
    }

    bool Contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

class ShaderEffectRefusalTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool skipped_ = false;

    // C: the structured refusal, built from the first language whose vertex AND fragment stage the
    // renderer rejects. Returns false when the renderer accepts every language in the package.
    bool CheckStructuredRefusal(GraphicsDevice& device, const ShaderPackageEXT& package)
    {
        const ShaderCodeEXT* vertex = nullptr;
        const ShaderCodeEXT* fragment = nullptr;
        for (const ShaderCodeEXT& candidate : package.getVariants())
        {
            if (candidate.getStage() != CNA::ShaderStageEXT::Vertex) continue;
            const CNA::ShaderLanguageEXT language = candidate.getLanguage();
            if (device.SupportsShaderLanguageEXT(language, CNA::ShaderStageEXT::Vertex) ||
                device.SupportsShaderLanguageEXT(language, CNA::ShaderStageEXT::Fragment))
                continue;
            for (const ShaderCodeEXT& other : package.getVariants())
                if (other.getLanguage() == language &&
                    other.getStage() == CNA::ShaderStageEXT::Fragment)
                    fragment = &other;
            if (fragment != nullptr) { vertex = &candidate; break; }
        }
        if (vertex == nullptr) return false;

        const std::string language = LanguageName(vertex->getLanguage());
        const ShaderPackageEXT rejectedOnly(
            {*vertex, *fragment}, {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
        const auto refusal = rejectedOnly.selectFor(device);
        const std::string& diagnostic = refusal.getDiagnostic();
        const std::string listed = language + " {Vertex='" + vertex->getSourceLabel() +
                                   "', Fragment='" + fragment->getSourceLabel() + "'}";
        Check(!refusal.isUsable() && Contains(diagnostic, listed) &&
                  Contains(diagnostic, "renderer rejects " + language + "/Vertex") &&
                  Contains(diagnostic, "renderer rejects " + language + "/Fragment"),
              ("C1 selectFor() refuses a " + language + "-only package and lists both stages: " +
               diagnostic).c_str());

        bool structured = false;
        std::string seen = "NOT REFUSED";
        try
        {
            ShaderEffect unsupported(device, rejectedOnly);
        }
        catch (const CNA::ShaderCompilationExceptionEXT& error)
        {
            const auto& diagnostics = error.getDiagnostics();
            seen = std::to_string(diagnostics.size()) + " diagnostic(s); what(): " + error.what();
            structured = diagnostics.size() == 2 &&
                diagnostics[0].getSourceLabel() == vertex->getSourceLabel() &&
                diagnostics[0].getStage() == CNA::ShaderStageEXT::Vertex &&
                diagnostics[0].getMessage() == diagnostic &&
                diagnostics[1].getSourceLabel() == fragment->getSourceLabel() &&
                diagnostics[1].getStage() == CNA::ShaderStageEXT::Fragment &&
                diagnostics[1].getMessage() == diagnostic;
        }
        catch (const std::exception& error)
        {
            seen = std::string("the wrong exception type: ") + error.what();
        }
        Check(structured,
              ("C2 ShaderEffect(device, " + language + "-only package) throws "
               "ShaderCompilationExceptionEXT with one labelled diagnostic per stage: " + seen)
                  .c_str());
        return true;
    }

public:
    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const ShaderPackageEXT package = CNA::Examples::PortableTint::CreatePackage();
        const auto selection = package.selectFor(device);
        if (!selection.isUsable())
        {
            std::printf("[SKIP] the portable tint package has no variant this renderer accepts: %s\n",
                        selection.getDiagnostic().c_str());
            skipped_ = true;
            return;
        }

        // ---- A ----------------------------------------------------------------------------------
        ShaderEffect fx(device, package);
        std::unique_ptr<Effect> cloned(fx.Clone());
        auto* clonedShader = dynamic_cast<ShaderEffect*>(cloned.get());
        const std::string chosen = LanguageName(selection.getLanguage());
        Check(fx.IsEffectValid() && fx.GetSelectedShaderLanguageEXT() == selection.getLanguage() &&
                  clonedShader != nullptr && clonedShader->IsEffectValid() &&
                  clonedShader->GetSelectedShaderLanguageEXT() == selection.getLanguage(),
              ("A the package selects " + chosen + ", the effect compiles in it, and a Clone keeps "
               "it" + (fx.IsEffectValid() ? std::string() : ": " + fx.GetCompileErrorEXT())).c_str());
        if (!fx.IsEffectValid()) return;

        // ---- B ----------------------------------------------------------------------------------
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1,
                                                      std::vector<std::uint8_t>{255, 255, 255, 255});
        const auto& viewport = device.getViewportProperty();
        const int w = viewport.getWidthProperty();
        const int h = viewport.getHeightProperty();
        device.Clear(Color(0, 255, 0, 255));
        device.setDepthStencilStateProperty(DepthStencilState::None);
        const auto drawTinted = [&](float r, float g, float b, const Rectangle& destination) {
            fx.SetUniformVec4("uColor", r, g, b, 1.0f);
            fx.Apply();
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                nullptr, &fx);
            spriteBatch_->Draw(white, destination, Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();
        };
        drawTinted(1.0f, 0.0f, 0.0f, Rectangle(w / 8, h / 4, w / 4, h / 2));
        drawTinted(0.0f, 0.0f, 1.0f, Rectangle(w * 5 / 8, h / 4, w / 4, h / 2));
        Color left(0, 0, 0, 0), right(0, 0, 0, 0), background(0, 0, 0, 0);
        const Rectangle leftProbe(w / 4, h / 2, 1, 1);
        const Rectangle rightProbe(w * 3 / 4, h / 2, 1, 1);
        const Rectangle backgroundProbe(1, 1, 1, 1);
        device.GetBackBufferData(&leftProbe, &left, 0, 1);
        device.GetBackBufferData(&rightProbe, &right, 0, 1);
        device.GetBackBufferData(&backgroundProbe, &background, 0, 1);
        const bool twoColours = left.getRProperty() >= 200 && left.getBProperty() <= 60 &&
                                right.getBProperty() >= 200 && right.getRProperty() <= 60 &&
                                background.getGProperty() >= 200 && background.getRProperty() <= 50;
        std::printf("[%s] B two uColor values give two colours: left=(%d,%d,%d) want red, "
                    "right=(%d,%d,%d) want blue, background=(%d,%d,%d) want green\n",
                    twoColours ? "PASS" : "FAIL", left.getRProperty(), left.getGProperty(),
                    left.getBProperty(), right.getRProperty(), right.getGProperty(),
                    right.getBProperty(), background.getRProperty(), background.getGProperty(),
                    background.getBProperty());
        if (!twoColours) MarkFailedEXT();

        // ---- C ----------------------------------------------------------------------------------
        if (!CheckStructuredRefusal(device, package))
            std::printf("[NOTE] C not run: this renderer accepts every language in the package, so "
                        "no refusal can be built from it\n");

        // ---- D ----------------------------------------------------------------------------------
        const float payload[16] = {};
        const auto expectAccepted = [&](const char* setter, auto&& call) {
            std::string what;
            bool accepted = true;
            try { call(); }
            catch (const std::exception& e) { accepted = false; what = e.what(); }
            Check(accepted, (std::string("D ") + setter + " within capacity is accepted" +
                             (accepted ? std::string() : ": refused with " + what)).c_str());
        };
        expectAccepted("SetUniformFloatArray", [&] { fx.SetUniformFloatArray("uWeights", payload, 4); });
        expectAccepted("SetUniformVec2Array", [&] { fx.SetUniformVec2Array("uOffsets", payload, 2); });
        expectAccepted("SetUniformVec3Array", [&] { fx.SetUniformVec3Array("uLightDirs", payload, 2); });
        expectAccepted("SetUniformMat4Array", [&] { fx.SetUniformMat4Array("uBones", payload, 1); });

        // ---- E ----------------------------------------------------------------------------------
        // Writing 72 of 1000 matrices and dropping the rest would pass every leg above and corrupt
        // whatever the shader reads past the array.
        std::vector<float> oversized(16u * 1000u, 0.0f);
        bool refused = false;
        std::string what;
        try { fx.SetUniformMat4Array("uBones", oversized.data(), 1000); }
        catch (const System::NotSupportedException& e) { refused = true; what = e.what(); }
        catch (const std::exception& e) { what = std::string("the wrong exception type: ") + e.what(); }
        Check(refused && Contains(what, "1000"),
              ("E a 1000-element mat4 array is refused by name, not truncated: " +
               (what.empty() ? std::string("NOT REFUSED") : what)).c_str());
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    ShaderEffectRefusalTest game;
    // GetBackBufferData is HiDef-only (XNA's rule).
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    game.Run();
    return game.ExitCode();
}

#else // CNA_CNAEXT

int main()
{
    std::printf("[SKIP] ShaderEffect shader packages need CNA_CNAEXT=ON\n");
    return CNA::Examples::kSkipExitCode;
}

#endif // CNA_CNAEXT
