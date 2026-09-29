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
#include <deque>
#include <mutex>
#include <optional>
namespace CNA::Internal::GamerServices {
namespace {
using namespace Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;
using Guide = Microsoft::Xna::Framework::GamerServices::Guide;
using Clock = std::chrono::steady_clock;

// Guide notifications: one on screen at a time, each for GuideNotificationDuration from when it
// first shows; a bounded queue behind it.
struct Toast {std::string text;std::optional<Clock::time_point> shown;};
std::mutex toastLock;
std::deque<Toast> toasts;
std::function<Clock::time_point()> toastClock;
Clock::time_point toastNow(){return toastClock?toastClock():Clock::now();}
// Caller holds toastLock. Drops finished toasts and starts the next one's time.
void advanceToasts() {
    const auto now=toastNow();
    while(!toasts.empty()&&toasts.front().shown&&now-*toasts.front().shown>=GuideNotificationDuration)toasts.pop_front();
    if(!toasts.empty()&&!toasts.front().shown)toasts.front().shown=now;
}
class Overlay final : public CNA::Internal::Runtime::IGameOverlay {
public:
    XnaGraphics::IGraphicsDeviceService* graphics=nullptr;
    [[nodiscard]] bool isModalVisible() const override { return guideIsVisible(); }
    void reset() { batch_.reset(); font_.reset(); white_.reset(); device_=nullptr; }
    void draw() override {
        if(!graphics)return;
        std::optional<std::string> toast;
        {std::lock_guard guard(toastLock);advanceToasts();if(!toasts.empty())toast=toasts.front().text;}
        if(!guideIsVisible()&&!toast)return;
        auto* device=graphics->getGraphicsDeviceProperty();if(!device)return;
        if(device!=device_) {
            batch_=std::make_unique<XnaGraphics::SpriteBatch>(*device);
            font_=CNA::Internal::Graphics::makeSystemFont(*device);
            white_=std::make_unique<XnaGraphics::Texture2D>(XnaGraphics::Texture2D::CreateFromPixels(*device,1,1,std::vector<SharpRuntime::bytecs>{255,255,255,255}));device_=device;
        }
        batch_->Begin();
        if(guideIsVisible()) {
            Guide::RenderPendingKeyboardInputEXT(*device,*batch_,*font_,*white_);
            Guide::RenderPendingMessageBoxEXT(*device,*batch_,*font_,*white_);
            const auto status=guideSignInStatus();
            if(!status.empty()&&!Guide::getHasPendingKeyboardInputEXTProperty()&&!Guide::getHasPendingMessageBoxEXTProperty()) {
                batch_->DrawString(*font_,status,Vector2{32,32},Color::White);
            }
        }
        if(toast)drawToast(*device,*toast);
        batch_->End();
    }
    // A toast above everything, where the game asked (Guide.NotificationPosition).
    void drawToast(XnaGraphics::GraphicsDevice& device,const std::string& text) {
        constexpr float scale=2.0f;constexpr int padding=14,accent=6;
        const auto size=font_->MeasureString(text)*scale;
        const int width=static_cast<int>(size.X)+padding*2+accent,height=static_cast<int>(size.Y)+padding*2;
        const auto viewport=device.getViewportProperty();
        const auto origin=guideNotificationOrigin(Guide::getNotificationPositionProperty(),viewport.getWidthProperty(),viewport.getHeightProperty(),width,height);
        batch_->Draw(*white_,Rectangle(origin.X,origin.Y,width,height),Color(20,24,32,225));
        batch_->Draw(*white_,Rectangle(origin.X,origin.Y,accent,height),Color(110,190,90));
        batch_->DrawString(*font_,text,Vector2(static_cast<float>(origin.X+accent+padding),static_cast<float>(origin.Y+padding)),
            Color::White,0.0f,Vector2::Zero,scale,XnaGraphics::SpriteEffects::None,0.0f);
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
void postGuideNotification(std::string text) {
    if(text.empty())return;
    std::lock_guard guard(toastLock);
    // A burst (four gamers signing in) keeps its latest eight.
    if(toasts.size()>=8)toasts.erase(toasts.begin()+(toasts.front().shown?1:0));
    toasts.push_back({std::move(text),std::nullopt});
}
std::vector<std::string> guideNotifications() {
    std::lock_guard guard(toastLock);advanceToasts();
    std::vector<std::string> result;for(const auto& toast:toasts)result.push_back(toast.text);return result;
}
Microsoft::Xna::Framework::Point guideNotificationOrigin(Microsoft::Xna::Framework::GamerServices::NotificationPosition position,
    int screenWidth,int screenHeight,int width,int height) {
    using Position=Microsoft::Xna::Framework::GamerServices::NotificationPosition;
    const int marginX=screenWidth/20,marginY=screenHeight/20;
    const int column=static_cast<int>(position)%3,row=static_cast<int>(position)/3;
    const int x=column==0?marginX:column==1?(screenWidth-width)/2:screenWidth-marginX-width;
    const int y=row==0?marginY:row==1?(screenHeight-height)/2:screenHeight-marginY-height;
    static_assert(static_cast<int>(Position::BottomRight)==8);
    return {x,y};
}
void setGuideNotificationClockForTesting(std::function<std::chrono::steady_clock::time_point()> clock) {
    std::lock_guard guard(toastLock);toastClock=std::move(clock);
}
Microsoft::Xna::Framework::Graphics::GraphicsDevice& dispatcherGraphicsDevice() {
    if(testingDevice)return *testingDevice;
    if(!Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::getIsInitializedProperty())
        throw System::InvalidOperationException("GamerServicesDispatcher.Initialize must be called before using gamer services graphics.");
    auto* device=overlay.graphics?overlay.graphics->getGraphicsDeviceProperty():nullptr;
    if(!device)throw System::InvalidOperationException("No graphics device is available to gamer services.");
    return *device;
}
void setDispatcherGraphicsDeviceForTesting(Microsoft::Xna::Framework::Graphics::GraphicsDevice* device){testingDevice=device;}
namespace {CNA::Internal::Runtime::IModalFrames* testingFrames=nullptr;}
CNA::Internal::Runtime::IModalFrames* guideModalFrames(){return testingFrames?testingFrames:CNA::Internal::Runtime::activeModalFrames();}
void setGuideModalFramesForTesting(CNA::Internal::Runtime::IModalFrames* frames){testingFrames=frames;}
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
