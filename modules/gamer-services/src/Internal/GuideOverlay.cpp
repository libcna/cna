// SPDX-License-Identifier: MS-PL
#include "GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/DispatcherGraphics.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "System/InvalidOperationException.hpp"
#include "CNA/Internal/Runtime/IGameOverlay.hpp"
#include "CNA/Internal/Graphics/SystemFont.hpp"
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
namespace CNA::Internal::GamerServices {
namespace {
using namespace Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;
using Guide = Microsoft::Xna::Framework::GamerServices::Guide;
class Overlay final : public CNA::Internal::Runtime::IGameOverlay {
public:
    XnaGraphics::IGraphicsDeviceService* graphics=nullptr;
    [[nodiscard]] bool isModalVisible() const override { return guideIsVisible(); }
    void reset() { batch_.reset(); font_.reset(); white_.reset(); device_=nullptr; }
    void draw() override {
        if(!guideIsVisible()||!graphics)return;
        auto* device=graphics->getGraphicsDeviceProperty();if(!device)return;
        if(device!=device_) {
            batch_=std::make_unique<XnaGraphics::SpriteBatch>(*device);
            font_=CNA::Internal::Graphics::makeSystemFont(*device);
            white_=std::make_unique<XnaGraphics::Texture2D>(XnaGraphics::Texture2D::CreateFromPixels(*device,1,1,std::vector<SharpRuntime::bytecs>{255,255,255,255}));device_=device;
        }
        batch_->Begin();
        Guide::RenderPendingKeyboardInputEXT(*device,*batch_,*font_,*white_);
        Guide::RenderPendingMessageBoxEXT(*device,*batch_,*font_,*white_);
        const auto status=guideSignInStatus();
        if(!status.empty()&&!Guide::getHasPendingKeyboardInputEXTProperty()&&!Guide::getHasPendingMessageBoxEXTProperty()) {
            batch_->DrawString(*font_,status,Vector2{32,32},Color::White);
        }
        batch_->End();
    }
private:
    XnaGraphics::GraphicsDevice* device_=nullptr;
    std::unique_ptr<XnaGraphics::SpriteBatch> batch_;
    std::unique_ptr<XnaGraphics::SpriteFont> font_;
    std::unique_ptr<XnaGraphics::Texture2D> white_;
};
Overlay overlay;
}
namespace {
Microsoft::Xna::Framework::Graphics::GraphicsDevice* testingDevice=nullptr;
}
bool guideOverlayAttached() {return overlay.graphics!=nullptr;}
Microsoft::Xna::Framework::Graphics::GraphicsDevice& dispatcherGraphicsDevice() {
    if(testingDevice)return *testingDevice;
    if(!Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::getIsInitializedProperty())
        throw System::InvalidOperationException("GamerServicesDispatcher.Initialize must be called before using gamer services graphics.");
    auto* device=overlay.graphics?overlay.graphics->getGraphicsDeviceProperty():nullptr;
    if(!device)throw System::InvalidOperationException("No graphics device is available to gamer services.");
    return *device;
}
void setDispatcherGraphicsDeviceForTesting(Microsoft::Xna::Framework::Graphics::GraphicsDevice* device){testingDevice=device;}
void installGuideOverlay(System::IServiceProvider& provider) {
    auto* container=dynamic_cast<Microsoft::Xna::Framework::GameServiceContainer*>(&provider);
    overlay.graphics=static_cast<Microsoft::Xna::Framework::Graphics::IGraphicsDeviceService*>(provider.GetService(typeid(Microsoft::Xna::Framework::Graphics::IGraphicsDeviceService)));
    if(overlay.graphics) {
        overlay.graphics->getDeviceDisposingEvent().Add([](auto*,const auto&){overlay.reset();});
        overlay.graphics->getDeviceResettingEvent().Add([](auto*,const auto&){overlay.reset();});
    }
    if(container&&!container->GetService<CNA::Internal::Runtime::IGameOverlay>())container->AddService<CNA::Internal::Runtime::IGameOverlay>(&overlay);
}
}
