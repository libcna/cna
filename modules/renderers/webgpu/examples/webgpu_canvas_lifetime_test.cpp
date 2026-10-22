// SPDX-License-Identifier: MS-PL
// Museum canvas lifetime regression: synchronous CNA operations may yield to the browser.
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include <cstdio>
#include <memory>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

class CanvasLifetimeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<OcclusionQuery> query_;
    bool finished_ = false;
    int step_ = 0, checks_ = 0, failures_ = 0;
    void Check(bool ok, const char* label)
    {
        ++checks_; failures_ += !ok;
        std::printf("[%s] canvas step%d: %s\n", ok ? "PASS" : "FAIL", step_, label);
        std::fflush(stdout);
    }
    Color Centre(GraphicsDevice& device)
    {
        Color pixel;
        const auto& view = device.getViewportProperty();
        const Rectangle region(view.getWidthProperty()/2, view.getHeightProperty()/2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
    void Draw(const GameTime&) override
    {
        if (finished_)
        {
#if defined(__EMSCRIPTEN__)
            // The browser driver may hold the final frame for an actual canvas screenshot.
            if (EM_ASM_INT({ return !!window.CNA_CANVAS_HOLD && !window.CNA_CANVAS_CAPTURED; })) return;
#endif
            Exit();
            return;
        }
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
        const Color red(255,0,0,255), blue(0,0,255,255), green(0,255,0,255);
        if(step_ == 0)
        {
            device.Clear(red);
            Check(Centre(device) == red, "ordinary clear/readback control");
        }
        else if(step_ == 1)
        {
            RenderTarget2D target(device,8,8);
            device.Clear(red);
            device.SetRenderTarget(&target); // Encodes the backbuffer; not necessarily submitted.
            Check(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4),
                  "core float target probe completes while an encoded canvas pass is pending");
            device.Clear(blue);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(Centre(device) == red, "capability-probe yield preserves the pending backbuffer clear");
        }
        else if(step_ == 2)
        {
            RenderTarget2D target(device,8,8);
            device.Clear(red);
            device.SetRenderTarget(&target);
            device.Clear(blue);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixels[64]; target.GetData(pixels,64); // This map also yields to the browser.
            Check(pixels[0] == blue, "offscreen readback control");
            device.Clear(green);
            Check(Centre(device) == green, "rendering after an offscreen-map yield uses a live attachment");
        }
        else if(step_ == 3)
        {
            RenderTarget2D target(device,8,8);
            device.Clear(red);
            device.SetRenderTarget(&target);
            renderer.SetSwapInterval(0); // Reconfigures the surface with a previous canvas acquired.
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            device.Clear(green);
            Check(Centre(device) == green, "rendering after surface reconfiguration uses a live attachment");
        }
        else if(step_ == 4)
        {
            Texture2D white(device,1,1);
            const Color texel(255,255,255,255);
            white.SetData(&texel,1);
            SpriteBatch sprites(device);
            device.Clear(red);
            Check(Centre(device) == red, "readback before a partial draw");
            sprites.Begin();
            sprites.Draw(white,Rectangle(8,8,16,16),green);
            sprites.End();
            Check(Centre(device) == red, "partial draw after a map preserves untouched pixels");
            Color pixel;
            const Rectangle region(16,16,1,1);
            device.GetBackBufferData(&region,&pixel,0,1);
            Check(pixel == green, "partial draw writes the requested pixels");
        }
        else if(step_ == 5)
        {
            manager_->setPreferredBackBufferWidthProperty(80);
            manager_->setPreferredBackBufferHeightProperty(48);
            manager_->ApplyChanges();
            device.Clear(blue);
            Check(Centre(device) == blue, "resized backbuffer has matching color and depth attachments");
            Check(device.getViewportProperty().getWidthProperty() == 80 &&
                  device.getViewportProperty().getHeightProperty() == 48, "resize updates viewport dimensions");
        }
        else if(step_ == 6)
        {
            manager_->setPreferMultiSamplingProperty(true);
            manager_->ApplyChanges();
            const int samples = device.getPresentationParametersProperty().getMultiSampleCountProperty();
            std::printf("[INFO] canvas applied MSAA=%d\n",samples);
            Check(samples == 0 || samples == 4, "MSAA request selects a legal WebGPU sample count");
            device.Clear(green);
            Check(Centre(device) == green, "MSAA resolve and readback survive surface reconfiguration");
        }
        else if(step_ == 7)
        {
            query_ = std::make_unique<OcclusionQuery>(device);
            Texture2D white(device,1,1);
            const Color texel(255,255,255,255);
            white.SetData(&texel,1);
            SpriteBatch sprites(device);
            device.Clear(blue);
            query_->Begin();
            sprites.Begin();
            sprites.Draw(white,Rectangle(0,0,80,48),blue);
            sprites.End();
            query_->End();
            // Do not read the result here: Present must not yield to collect it after
            // acquiring the canvas for its final copy.
        }
        else if(step_ == 8)
        {
            Check(query_->getIsCompleteProperty() && query_->getPixelCountProperty() > 0,
                  "pending occlusion results survive Present without expiring its canvas");
            Check(Centre(device) == blue, "query frame remains visible and readable");
        }
        else
        {
            Check(renderer.GetUncapturedErrorCountEXT() == 0, "no uncaptured GPU validation errors");
            std::printf("CANVAS_LIFETIME_RESULT checks=%d failures=%d\n", checks_,failures_);
            finished_ = true;
        }
        ++step_;
    }
public:
    CanvasLifetimeTest()
    {
        manager_=std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const { return checks_ > 0 && failures_ == 0 ? 0 : 1; }
};
int main(){ CanvasLifetimeTest game; game.Run(); return game.Result(); }
