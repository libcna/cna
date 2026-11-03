// SPDX-License-Identifier: MS-PL
// MTH-PLAT-006: browser-only checks of the common campaign changes and retained resources.
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(__EMSCRIPTEN__) && defined(CNA_GL_PROFILE_WEBGL2)
#include <emscripten.h>

EM_JS(void, CNA_CampaignWatchWebGLRecovery, (), {
    const canvas = Module['canvas'];
    const gl = canvas.getContext('webgl2');
    for (const name of ['webglcontextlost', 'webglcontextrestored']) {
        canvas.addEventListener(name, (event) => {
            console.log('[WEBGL] ' + name + ' isContextLost=' + gl.isContextLost() +
                        ' defaultPrevented=' + event.defaultPrevented);
        }, {once: true});
    }
});

EM_JS(void, CNA_CampaignReportWebGLRecovery, (), {
    const gl = Module['canvas'].getContext('webgl2');
    console.log('[WEBGL] recovery timeout: browser isContextLost=' + gl.isContextLost());
});
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BrowserCampaignTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> content_;
    std::unique_ptr<Texture2D> texel_;
    std::unique_ptr<SpriteBatch> batch_;
    std::unique_ptr<RenderTarget2D> target_;
    std::vector<Color> contentPixels_;
    int phase_ = 0;
    int ticks_ = 0;
    int passed_ = 0;
    int total_ = 0;
    const char* activeCheck_ = "campaign initialization";

    void Check(bool ok, const char* label)
    {
        ++total_;
        passed_ += ok;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    }

    void Begin(const DepthStencilState& depth = DepthStencilState::None,
               const Matrix& transform = Matrix::getIdentityProperty())
    {
        batch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &SamplerState::PointClamp,
                      &depth, &RasterizerState::CullNone, nullptr, transform);
    }

    void Bind()
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color::Black);
    }

    std::vector<Color> Read()
    {
        getGraphicsDeviceProperty().SetRenderTarget(nullptr);
        std::vector<Color> pixels(16 * 16);
        target_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    std::vector<Color> DrawContent()
    {
        Bind();
        Begin();
        batch_->Draw(*content_, Rectangle(0, 0, 16, 16), Color::White);
        batch_->End();
        return Read();
    }

    void SpriteCases()
    {
        activeCheck_ = "Deferred resolves full SetData replacement at End";
        Bind();
        Color color = Color::Red;
        texel_->SetData(&color, 1);
        Begin();
        batch_->Draw(*texel_, Rectangle(0, 0, 16, 16), Color::White);
        color = Color::Lime;
        texel_->SetData(&color, 1);
        batch_->End();
        const auto deferred = Read();
        Check(std::all_of(deferred.begin(), deferred.end(), [](const Color& c) { return c == Color::Lime; }),
              activeCheck_);

        activeCheck_ = "fractional sprite matches translated integer sprite and draws one texel";
        Bind();
        Begin();
        batch_->Draw(*texel_, Vector2(5.75f, 6.75f), Color::White);
        batch_->End();
        const auto direct = Read();
        Bind();
        Begin(DepthStencilState::None, Matrix::CreateTranslation(0.75f, 0.75f, 0.0f));
        batch_->Draw(*texel_, Vector2(5.0f, 6.0f), Color::White);
        batch_->End();
        Check(direct == Read() && std::count(direct.begin(), direct.end(), Color::Lime) == 1,
              activeCheck_);

        activeCheck_ = "Default/None/Default sprites capture independent depth pipeline state";
        // SpriteBatch leaves its last texture bound after End; XNA forbids uploading while bound.
        getGraphicsDeviceProperty().getTexturesProperty()(0, nullptr);
        color = Color::White;
        texel_->SetData(&color, 1);
        Bind();
        const auto draw = [this](int x, Color tint, float z, const DepthStencilState& depth) {
            Begin(depth);
            batch_->Draw(*texel_, Rectangle(x, 0, 4, 4), Rectangle(0, 0, 1, 1), tint,
                         0.0f, Vector2::Zero, SpriteEffects::None, z);
            batch_->End();
        };
        for (int x : {0, 4, 8}) draw(x, Color::Red, 0.25f, DepthStencilState::Default);
        draw(0, Color::Lime, 0.75f, DepthStencilState::Default);
        draw(4, Color::Lime, 0.75f, DepthStencilState::None);
        draw(8, Color::Lime, 0.75f, DepthStencilState::Default);
        getGraphicsDeviceProperty().setDepthStencilStateProperty(DepthStencilState::None);
        const auto depth = Read();
        Check(depth[17] == Color::Red && depth[21] == Color::Lime && depth[25] == Color::Red,
              activeCheck_);
    }

    void Finish()
    {
        // Fixed denominator prevents an early exit from qualifying a partial run.
        std::printf("=== %d/11 PASS ===\n", passed_);
        std::fflush(stdout);
        Exit();
    }

    void Fail(const std::exception& error)
    {
        ++total_;
        std::printf("[FAIL] %s: %s\n", activeCheck_, error.what());
        phase_ = 5;
        Finish();
    }

protected:
    void LoadContent() override try
    {
        activeCheck_ = "selected renderer matches the requested build (no fallback)";
        auto& device = getGraphicsDeviceProperty();
        const auto rendererName = device.GetGraphicsRendererName();
        std::printf("[RENDERER] %.*s expected=%s\n", static_cast<int>(rendererName.size()),
                    rendererName.data(), CNA_BROWSER_EXPECTED_RENDERER);
        Check(device.GetGraphicsRendererName() == CNA_BROWSER_EXPECTED_RENDERER,
              activeCheck_);
        activeCheck_ = "selected browser adapter exposes a positive display mode";
        const auto& adapter = GraphicsAdapter::getDefaultAdapterProperty();
        const auto mode = adapter.getCurrentDisplayModeProperty();
        std::printf("[ADAPTER] %s %dx%d\n", adapter.getDescriptionProperty().c_str(),
                    mode.getWidthProperty(), mode.getHeightProperty());
        Check(mode.getWidthProperty() > 0 && mode.getHeightProperty() > 0,
              activeCheck_);
        activeCheck_ = "C++ exceptions cross throw/catch in the browser artifact";
        bool caught = false;
        try { throw std::runtime_error("browser exception ABI"); }
        catch (const std::runtime_error&) { caught = true; }
        Check(caught, activeCheck_);
        activeCheck_ = "HTTP preload package decodes through ContentManager";
        getContentProperty().setRootDirectoryProperty("Content");
        content_ = std::make_unique<Texture2D>(getContentProperty().Load<Texture2D>("player.png"));
        Check(content_->getWidthProperty() > 0 && content_->getHeightProperty() > 0,
              activeCheck_);
        activeCheck_ = "sprite test resource creation";
        texel_ = std::make_unique<Texture2D>(device, 1, 1);
        batch_ = std::make_unique<SpriteBatch>(device);
        target_ = std::make_unique<RenderTarget2D>(device, 16, 16, false,
                                                SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
    }
    catch (const std::exception& error) { Fail(error); }

    void Update(GameTime&) override try
    {
        auto& renderer = getGraphicsDeviceProperty().GetRenderer();
        if (++ticks_ > 600) {
            std::printf("[RECOVERY] phase=%d canBeginDraw=%d\n", phase_, renderer.CanBeginDrawEXT());
#if defined(__EMSCRIPTEN__) && defined(CNA_GL_PROFILE_WEBGL2)
            CNA_CampaignReportWebGLRecovery();
#endif
            Check(false, phase_ == 3 ? "browser context loss did not close the draw gate"
                                    : "browser context restoration did not reopen the draw gate");
            Finish();
        } else if (phase_ == 2) {
            activeCheck_ = "context loss closes the draw gate";
#if defined(__EMSCRIPTEN__) && defined(CNA_GL_PROFILE_WEBGL2)
            CNA_CampaignWatchWebGLRecovery();
#endif
            renderer.DebugSimulateContextLoss();
            phase_ = 3;
        } else if (phase_ == 3 && !renderer.CanBeginDrawEXT()) {
            Check(true, activeCheck_);
            activeCheck_ = "retained content texture, SpriteBatch and render target restore identical pixels";
            renderer.DebugRestoreContext();
            phase_ = 4;
        }
    }
    catch (const std::exception& error) { Fail(error); }

    void Draw(const GameTime&) override try
    {
        if (phase_ == 0) {
            SpriteCases();
            activeCheck_ = "packaged texture paints the render target";
            contentPixels_ = DrawContent();
            Check(std::any_of(contentPixels_.begin(), contentPixels_.end(),
                             [](const Color& c) { return c != Color::Black; }),
                  activeCheck_);
            activeCheck_ = "logical backbuffer resizes to 80x48";
            manager_->setPreferredBackBufferWidthProperty(80);
            manager_->setPreferredBackBufferHeightProperty(48);
            manager_->ApplyChanges();
            phase_ = 1;
        } else if (phase_ == 1) {
            const auto& parameters = getGraphicsDeviceProperty().getPresentationParametersProperty();
            Check(parameters.getBackBufferWidthProperty() == 80 && parameters.getBackBufferHeightProperty() == 48,
                  activeCheck_);
            phase_ = 2;
        } else if (phase_ == 4) {
            Check(DrawContent() == contentPixels_,
                  activeCheck_);
            phase_ = 5;
            Finish();
        }
    }
    catch (const std::exception& error) { Fail(error); }

public:
    BrowserCampaignTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const { return passed_ == 11 && total_ == 11 ? 0 : 1; }
};

int main()
{
    CNA::GraphicsRendererSelection::SetPreferred(CNA_BROWSER_EXPECTED_RENDERER);
    BrowserCampaignTest game;
    game.Run();
    return game.Result();
}
