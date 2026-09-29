// SPDX-License-Identifier: MS-PL
// The Guide's frame: input, the screen stack and its transitions, the shell (identity rail,
// header, hints), dialogs, the game's own message box and keyboard, and notifications.
#include "GuideScreen.hpp"
#include "../GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
using Clock=std::chrono::steady_clock;
namespace In=Microsoft::Xna::Framework::Input;
namespace GS=Microsoft::Xna::Framework::GamerServices;

double seconds()
{
    static const auto start=Clock::now();
    return std::chrono::duration<double>(Clock::now()-start).count();
}

Xna::Color faded(Xna::Color color,float alpha)
{
    return Xna::Color(static_cast<int>(color.getRProperty()*alpha),static_cast<int>(color.getGProperty()*alpha),
                      static_cast<int>(color.getBProperty()*alpha),static_cast<int>(color.getAProperty()*alpha));
}

// ---- Input -------------------------------------------------------------------------------
class InputReader {
public:
    // One read per frame. Directions repeat while held; everything else fires on the press. A
    // reader that was idle takes whatever is held as already held, so the press that opened a
    // screen (A in a game menu, Enter confirming a prompt) cannot also answer it.
    // Whatever is held when something new appears on top is already held for it.
    void prime(){primed_=true;}
    InputFrame poll(double now)
    {
        InputFrame frame;
        const auto keys=In::Keyboard::GetState();
        std::array<bool,10> keyDown{},padDown{};
        auto set=[](std::array<bool,10>& down,Command command,bool value){down[static_cast<std::size_t>(command)]=down[static_cast<std::size_t>(command)]||value;};
        set(keyDown,Command::Up,keys.IsKeyDown(In::Keys::Up));
        set(keyDown,Command::Down,keys.IsKeyDown(In::Keys::Down));
        set(keyDown,Command::Left,keys.IsKeyDown(In::Keys::Left));
        set(keyDown,Command::Right,keys.IsKeyDown(In::Keys::Right));
        set(keyDown,Command::Accept,keys.IsKeyDown(In::Keys::Enter)||keys.IsKeyDown(In::Keys::Space));
        set(keyDown,Command::Back,keys.IsKeyDown(In::Keys::Escape));
        set(keyDown,Command::X,keys.IsKeyDown(In::Keys::X));
        set(keyDown,Command::Y,keys.IsKeyDown(In::Keys::Y));
        set(keyDown,Command::Previous,keys.IsKeyDown(In::Keys::Q)||keys.IsKeyDown(In::Keys::PageUp));
        set(keyDown,Command::Next,keys.IsKeyDown(In::Keys::E)||keys.IsKeyDown(In::Keys::PageDown)||keys.IsKeyDown(In::Keys::Tab));
        for(int index=0;index<4;++index) {
            const auto pad=In::GamePad::GetState(static_cast<Xna::PlayerIndex>(index));
            if(!pad.getIsConnectedProperty())continue;
            const auto stick=pad.getThumbSticksProperty().getLeftProperty();
            set(padDown,Command::Up,pad.IsButtonDown(In::Buttons::DPadUp)||stick.Y>0.5f);
            set(padDown,Command::Down,pad.IsButtonDown(In::Buttons::DPadDown)||stick.Y<-0.5f);
            set(padDown,Command::Left,pad.IsButtonDown(In::Buttons::DPadLeft)||stick.X<-0.5f);
            set(padDown,Command::Right,pad.IsButtonDown(In::Buttons::DPadRight)||stick.X>0.5f);
            set(padDown,Command::Accept,pad.IsButtonDown(In::Buttons::A)||pad.IsButtonDown(In::Buttons::Start));
            set(padDown,Command::Back,pad.IsButtonDown(In::Buttons::B)||pad.IsButtonDown(In::Buttons::Back));
            set(padDown,Command::X,pad.IsButtonDown(In::Buttons::X));
            set(padDown,Command::Y,pad.IsButtonDown(In::Buttons::Y));
            set(padDown,Command::Previous,pad.IsButtonDown(In::Buttons::LeftShoulder));
            set(padDown,Command::Next,pad.IsButtonDown(In::Buttons::RightShoulder));
        }
        Xna::Vector2 look((keys.IsKeyDown(In::Keys::C)?1.0f:0.0f)-(keys.IsKeyDown(In::Keys::Z)?1.0f:0.0f),
            (keys.IsKeyDown(In::Keys::OemPlus)||keys.IsKeyDown(In::Keys::Add)?1.0f:0.0f)-
            (keys.IsKeyDown(In::Keys::OemMinus)||keys.IsKeyDown(In::Keys::Subtract)?1.0f:0.0f));
        for(int index=0;index<4;++index) {
            const auto pad=In::GamePad::GetState(static_cast<Xna::PlayerIndex>(index));
            if(!pad.getIsConnectedProperty())continue;
            const auto triggers=pad.getTriggersProperty();
            look.X+=pad.getThumbSticksProperty().getRightProperty().X;
            look.Y+=triggers.getRightProperty()-triggers.getLeftProperty();
        }
        frame.look=Xna::Vector2(std::clamp(look.X,-1.0f,1.0f),std::clamp(look.Y,-1.0f,1.0f));
        const auto mouse=In::Mouse::GetState();
        frame.mouse=Xna::Vector2(static_cast<float>(mouse.getXProperty()),static_cast<float>(mouse.getYProperty()));
        const bool left=mouse.getLeftButtonProperty()==In::ButtonState::Pressed;
        frame.mouseDown=left;
        const bool right=mouse.getRightButtonProperty()==In::ButtonState::Pressed;
        const int wheel=mouse.getScrollWheelValueProperty();
        const bool idle=now-lastPoll_>0.5||primed_;
        lastPoll_=now;
        primed_=false;
        if(idle) {
            keyHeld_=keyDown;padHeld_=padDown;leftHeld_=left;rightHeld_=right;wheel_=wheel;mouse_=frame.mouse;
            return frame;
        }
        for(std::size_t i=0;i<frame.pressed.size();++i) {
            const bool down=keyDown[i]||padDown[i];
            const bool wasDown=keyHeld_[i]||padHeld_[i];
            const bool direction=i<=static_cast<std::size_t>(Command::Right);
            if(down&&!wasDown){frame.pressed[i]=true;repeat_[i]=now+0.38;}
            else if(down&&direction&&now>=repeat_[i]){frame.pressed[i]=true;repeat_[i]=now+0.085;}
            if(keyDown[i]&&!keyHeld_[i])device_=Device::Keyboard;
            if(padDown[i]&&!padHeld_[i])device_=Device::Pad;
            frame.keyboard[i]=keyDown[i]&&!keyHeld_[i];
        }
        keyHeld_=keyDown;padHeld_=padDown;
        frame.mouseMoved=frame.mouse.X!=mouse_.X||frame.mouse.Y!=mouse_.Y;
        if(frame.mouseMoved)device_=Device::Mouse;
        mouse_=frame.mouse;
        frame.click=left&&!leftHeld_;
        if(right&&!rightHeld_)frame.pressed[static_cast<std::size_t>(Command::Back)]=true;
        leftHeld_=left;rightHeld_=right;
        frame.wheel=(wheel-wheel_)/120;
        wheel_=wheel;
        frame.device=device_;
        return frame;
    }

private:
    std::array<bool,10> keyHeld_{},padHeld_{};
    std::array<double,10> repeat_{};
    bool leftHeld_=false,rightHeld_=false;
    int wheel_=0;
    Xna::Vector2 mouse_;
    double lastPoll_=-10;
    bool primed_=false;
    Device device_=Device::Keyboard;
};

// ---- State -------------------------------------------------------------------------------
struct GuideState {
    std::vector<std::shared_ptr<Screen>> stack;
    Xna::PlayerIndex player=Xna::PlayerIndex::One;
    double openedAt=0,topChangedAt=0;
    const Screen* top=nullptr;
    InputReader input;
    std::unique_ptr<Style> style;
    Portraits portraits;
    std::unique_ptr<Xna::Graphics::SpriteBatch> batch;
    Xna::Graphics::GraphicsDevice* device=nullptr;
    std::string shownTop;  // what was on top last frame (a new top primes the input)
    bool wasShowing=false;  // a screen or dialog was up last frame (opening plays a sound)
    std::string lastToast;  // the notification on screen last frame (a new one chimes)
    // Identity shown on the rail, per player.
    std::array<Identity,4> identities;
};
// Never destroyed: its GPU resources must not be released during static teardown, after the
// device is gone (the device-disposing hook releases them while the game runs).
GuideState& state(){static GuideState* guide=new GuideState;return *guide;}

void noteTop()
{
    auto& s=state();
    const Screen* current=s.stack.empty()?nullptr:s.stack.back().get();
    if(current!=s.top){s.top=current;s.topChangedAt=seconds();}
}
}

bool reducedMotion()
{
    static const bool reduced=[] {
        const auto* value=std::getenv("CNA_GAMER_SERVICES_REDUCED_MOTION");
        return value&&std::string(value)=="1";
    }();
    return reduced;
}

float ease(double elapsed,double duration)
{
    if(reducedMotion()||duration<=0)return 1.0f;
    const float t=static_cast<float>(std::clamp(elapsed/duration,0.0,1.0));
    return 1.0f-(1.0f-t)*(1.0f-t)*(1.0f-t);
}

// ---- Stack -------------------------------------------------------------------------------
void open(std::shared_ptr<Screen> screen,Xna::PlayerIndex player)
{
    auto& s=state();
    if(s.stack.empty())s.openedAt=seconds();
    s.player=player;
    screen->player=player;
    s.stack.clear();
    s.stack.push_back(std::move(screen));
    refreshIdentity(player);
    noteTop();
}

bool visible(){return !state().stack.empty();}

void closeAll()
{
    state().stack.clear();
    noteTop();
}

void push(std::shared_ptr<Screen> screen)
{
    auto& s=state();
    screen->player=s.player;
    s.stack.push_back(std::move(screen));
    noteTop();
}

void pop(const Screen* screen)
{
    auto& s=state();
    auto found=std::ranges::find_if(s.stack,[&](const auto& entry){return entry.get()==screen;});
    if(found!=s.stack.end())s.stack.erase(found,s.stack.end());
    noteTop();
}

void replace(std::shared_ptr<Screen> screen)
{
    auto& s=state();
    screen->player=s.player;
    s.stack.clear();
    s.stack.push_back(std::move(screen));
    noteTop();
}

Identity& identity(Xna::PlayerIndex player){return state().identities[static_cast<std::size_t>(player)&3];}

// ---- Widgets ------------------------------------------------------------------------------
int List::input(InputContext& ui,int count,int columns)
{
    if(count<=0){focus=0;return -1;}
    focus=std::clamp(focus,0,count-1);
    columns=std::max(columns,1);
    if(ui.input(Command::Up)&&focus-columns>=0)focus-=columns;
    if(ui.input(Command::Down)&&focus+columns<count)focus+=columns;
    if(columns>1&&ui.input(Command::Left)&&focus%columns>0)--focus;
    if(columns>1&&ui.input(Command::Right)&&focus%columns<columns-1&&focus+1<count)++focus;
    focus-=ui.input.wheel*columns;
    focus=std::clamp(focus,0,count-1);
    for(const auto& [index,box]:hits) {
        if(!box.contains(ui.input.mouse.X,ui.input.mouse.Y))continue;
        if(ui.input.mouseMoved)focus=index;
        if(ui.input.click){focus=index;return index;}
    }
    return ui.input(Command::Accept)?focus:-1;
}

void List::draw(Ui& ui,const Box& area,int count,float rowHeight,const std::function<void(int,const Box&,bool)>& row,int columns)
{
    hits.clear();
    columns=std::max(columns,1);
    const float height=ui.px(rowHeight);
    const int visibleRows=std::max(1,static_cast<int>(area.h/height));
    const int focusRow=focus/columns;
    // Keep the focus in view, one row of context where possible.
    float target=scroll;
    if(focusRow<target+0.5f)target=static_cast<float>(std::max(0,focusRow-1));
    if(focusRow>target+visibleRows-1.5f)target=static_cast<float>(focusRow-visibleRows+(focusRow+1<(count+columns-1)/columns?2:1));
    target=std::clamp(target,0.0f,static_cast<float>(std::max(0,(count+columns-1)/columns-visibleRows)));
    scroll=reducedMotion()?target:scroll+(target-scroll)*0.35f;
    if(std::fabs(target-scroll)<0.01f)scroll=target;
    const float cellWidth=(area.w-ui.px(12)*static_cast<float>(columns-1))/static_cast<float>(columns);
    for(int index=0;index<count;++index) {
        const int r=index/columns,c=index%columns;
        const float y=area.y+(static_cast<float>(r)-scroll)*height;
        if(y+height<area.y-1||y>area.bottom()-height*0.35f)continue;
        const Box box{area.x+static_cast<float>(c)*(cellWidth+ui.px(12)),y,cellWidth,height-ui.px(6)};
        row(index,box,index==focus);
        hits.emplace_back(index,box);
    }
}

void rowBackground(Ui& ui,const Box& box,bool focused)
{
    if(focused) {
        const float pulse=reducedMotion()?1.0f:0.85f+0.15f*static_cast<float>(std::sin(ui.time*4.0));
        ui.style.rounded(ui.batch,box,ui.px(12),faded(Palette::focus(),pulse*1.4f));
        ui.style.rounded(ui.batch,Box{box.x,box.y+ui.px(8),ui.px(4),box.h-ui.px(16)},ui.px(2),Palette::accent());
    } else {
        ui.style.rounded(ui.batch,box,ui.px(12),Palette::surface());
    }
}

void portrait(Ui& ui,const std::vector<unsigned char>& avatar,const Box& box,Framing framing,bool live)
{
    ui.style.gradient(ui.batch,box,Xna::Color(52,64,84),Xna::Color(30,38,52));
    if(framing==Framing::Body) {
        // The avatar stands on a soft shadow.
        ui.style.shadow(ui.batch,Box{box.x+box.w*0.28f,box.bottom()-box.h*0.07f,box.w*0.44f,box.h*0.03f},ui.px(8),ui.px(10),Xna::Color(0,0,0,110));
    }
    auto* texture=ui.portraits.request(avatar,framing,static_cast<int>(std::lround(box.w)),static_cast<int>(std::lround(box.h)),live);
    if(texture) {
        ui.batch.Draw(*texture,Xna::Vector2(box.x,box.y),std::nullopt,Xna::Color::White,0.0f,Xna::Vector2::Zero,
            Xna::Vector2(box.w/static_cast<float>(texture->getWidthProperty()),box.h/static_cast<float>(texture->getHeightProperty())),
            Xna::Graphics::SpriteEffects::None,0.0f);
    } else {
        const float s=std::min(box.w,box.h)*0.62f;
        ui.style.icon(ui.batch,Icon::Person,Box{box.x+(box.w-s)/2,box.y+(box.h-s)/2,s,s},Xna::Color(120,134,156));
    }
}

void statusDot(Ui& ui,Xna::Vector2 center,Xna::Color color)
{
    ui.style.disc(ui.batch,center,ui.px(7),Palette::panel());
    ui.style.disc(ui.batch,center,ui.px(5),color);
}

void loading(Ui& ui,const Box& area,const std::string& text)
{
    const Xna::Vector2 c(area.x+area.w/2,area.y+area.h/2);
    for(int dot=0;dot<3;++dot) {
        const float phase=static_cast<float>(std::sin(ui.time*5.0-dot*0.7));
        ui.style.disc(ui.batch,Xna::Vector2(c.X+ui.px(18)*static_cast<float>(dot-1),c.Y-ui.px(16)),ui.px(5+2*std::max(0.0f,phase)),
            faded(Palette::accent(),0.55f+0.45f*std::max(0.0f,phase)));
    }
    ui.style.text(ui.batch,Font::Body,text,Xna::Vector2(c.X,c.Y+ui.px(2)),Palette::muted(),Align::Center);
}

void emptyState(Ui& ui,const Box& area,Icon icon,const std::string& title,const std::string& text)
{
    const float s=ui.px(64);
    const float top=area.y+area.h*0.28f;
    ui.style.icon(ui.batch,icon,Box{area.x+(area.w-s)/2,top,s,s},Palette::faint());
    ui.style.text(ui.batch,Font::Heading,title,Xna::Vector2(area.x+area.w/2,top+s+ui.px(14)),Palette::text(),Align::Center);
    float y=top+s+ui.px(54);
    for(const auto& line:ui.style.wrap(Font::Body,text,std::min(area.w,ui.px(560)))) {
        ui.style.text(ui.batch,Font::Body,line,Xna::Vector2(area.x+area.w/2,y),Palette::muted(),Align::Center);
        y+=ui.style.measure(Font::Body,"Ag").Y;
    }
}

void drawActions(Ui& ui,List& list,const Box& area,const std::vector<std::pair<Icon,std::string>>& items)
{
    list.draw(ui,area,static_cast<int>(items.size()),56,[&](int index,const Box& box,bool focused) {
        rowBackground(ui,box,focused);
        const float s=ui.px(26);
        ui.style.icon(ui.batch,items[static_cast<std::size_t>(index)].first,Box{box.x+ui.px(18),box.y+(box.h-s)/2,s,s},
            focused?Palette::accent():Palette::muted());
        const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
        ui.style.text(ui.batch,Font::BodyBold,items[static_cast<std::size_t>(index)].second,Xna::Vector2(box.x+ui.px(58),box.y+(box.h-h)/2),Palette::text());
    });
}

std::optional<std::pair<std::string,std::string>> signedInAccount(Xna::PlayerIndex player)
{
    for(auto* gamer:*GS::Gamer::getSignedInGamersProperty())
        if(gamer->getPlayerIndexProperty()==player)
            return std::pair{GamerAccess::userId(*gamer),gamer->getGamertagProperty()};
    return std::nullopt;
}

// ---- A simple system dialog --------------------------------------------------------------
namespace {
class InfoScreen final : public Screen {
public:
    InfoScreen(std::string title,std::string text,Icon icon):title_(std::move(title)),text_(std::move(text)),icon_(icon){}
    bool sounded_=false;
    std::string name() const override {return "info";}
    std::string title() const override {return title_;}
    void input(InputContext& ui) override {if(ui.input(Command::Accept)||ui.input(Command::Back)||ui.input.click)pop(this);}
    void draw(Ui& ui,const Box& area) override {
        if(!sounded_){sounded_=true;if(icon_==Icon::Error||icon_==Icon::Warning)play(Sound::Error);}
        const float s=ui.px(44);
        ui.style.icon(ui.batch,icon_,Box{area.x,area.y,s,s},icon_==Icon::Error||icon_==Icon::Warning?Palette::error():Palette::accent());
        float y=area.y;
        for(const auto& line:ui.style.wrap(Font::Body,text_,area.w-s-ui.px(18))) {
            ui.style.text(ui.batch,Font::Body,line,Xna::Vector2(area.x+s+ui.px(18),y),Palette::text());
            y+=ui.style.measure(Font::Body,"Ag").Y;
        }
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"OK"}};}
    std::vector<std::string> labels() const override {return {text_};}
private:
    std::string title_,text_;
    Icon icon_;
};
}

void inform(Xna::PlayerIndex player,std::string title,std::string text,Icon icon)
{
    auto screen=std::make_shared<InfoScreen>(std::move(title),std::move(text),icon);
    if(visible())push(std::move(screen));
    else open(std::move(screen),player);
}

// ---- Drawing -------------------------------------------------------------------------------
namespace {
struct RailEntry {Category category;Icon icon;const char* label;};
constexpr RailEntry Rail[]{
    {Category::Home,Icon::Home,"Home"},{Category::Friends,Icon::People,"Friends"},{Category::Party,Icon::Party,"Party"},
    {Category::Messages,Icon::Message,"Messages"},{Category::Achievements,Icon::Trophy,"Achievements"},
    {Category::Leaderboards,Icon::Leaderboard,"Leaderboards"},{Category::Players,Icon::Clock,"Recent players"},{Category::Content,Icon::Store,"Game content"},
    {Category::Settings,Icon::Settings,"Settings"}};

std::shared_ptr<Screen> rootFor(Category category,Xna::PlayerIndex player)
{
    switch(category) {
    case Category::Home: return homeScreen(player);
    case Category::Friends: return friendsScreen(player);
    case Category::Party: return partyScreen(player);
    case Category::Messages: return messagesScreen(player);
    case Category::Achievements: return achievementsScreen(player);
    case Category::Leaderboards: return leaderboardsScreen(player);
    case Category::Players: return playersScreen(player);
    case Category::Content: return contentScreen(player);
    case Category::Settings: return settingsScreen(player);
    case Category::None: break;
    }
    return nullptr;
}

std::string commandLabel(Command command,Device device)
{
    if(device==Device::Pad)return {};
    switch(command) {
    case Command::Accept: return "Enter";
    case Command::Back: return "Esc";
    case Command::X: return "X";
    case Command::Y: return "Y";
    case Command::Previous: return "Q";
    case Command::Next: return "E";
    default: return "";
    }
}

}

float drawHints(Ui& ui,const std::vector<Hint>& hints,Xna::Vector2 position,bool rightAligned)
{
    // Measure first so the row can be right-aligned.
    const float gap=ui.px(22),size=ui.px(26);
    std::vector<float> widths;
    float total=0;
    for(const auto& [command,label]:hints) {
        const float glyph=ui.input.device==Device::Pad?size:std::max(size,ui.style.measure(Font::Caption,commandLabel(command,ui.input.device)).X+size*0.7f);
        const float w=glyph+ui.px(8)+ui.style.measure(Font::Body,label).X;
        widths.push_back(w);
        total+=w+gap;
    }
    float x=rightAligned?position.X-total+gap:position.X;
    for(std::size_t i=0;i<hints.size();++i) {
        const auto& [command,label]=hints[i];
        float glyph;
        if(ui.input.device==Device::Pad) {
            static const PadButton buttons[]{PadButton::A,PadButton::A,PadButton::A,PadButton::A,PadButton::A,PadButton::B,PadButton::X,
                PadButton::Y,PadButton::LB,PadButton::RB};
            ui.style.pad(ui.batch,buttons[static_cast<std::size_t>(command)],Xna::Vector2(x+size/2,position.Y),size);
            glyph=size;
        } else {
            glyph=ui.style.key(ui.batch,commandLabel(command,ui.input.device),Xna::Vector2(x,position.Y),size);
        }
        const float h=ui.style.measure(Font::Body,label).Y;
        ui.style.text(ui.batch,Font::Body,label,Xna::Vector2(x+glyph+ui.px(8),position.Y-h*0.55f),Palette::muted());
        x+=widths[i]+gap;
    }
    return total;
}

namespace {
void drawRail(Ui& ui,const Box& rail,Category current,float alpha)
{
    auto& s=state();
    auto& who=identity(s.player);
    // Identity: portrait, gamertag, score and status.
    const Box face{rail.x+ui.px(22),rail.y+ui.px(24),ui.px(76),ui.px(76)};
    ui.style.rounded(ui.batch,Box{face.x-ui.px(3),face.y-ui.px(3),face.w+ui.px(6),face.h+ui.px(6)},ui.px(16),Palette::accent());
    portrait(ui,who.avatar,face);
    const float tx=face.right()+ui.px(14);
    if(who.gamertag.empty()) {
        ui.style.text(ui.batch,Font::BodyBold,"Not signed in",Xna::Vector2(tx,face.y+ui.px(8)),Palette::text());
        ui.style.text(ui.batch,Font::Caption,"Select Sign in",Xna::Vector2(tx,face.y+ui.px(36)),Palette::muted());
    } else {
        ui.style.text(ui.batch,Font::Heading,ui.style.fit(Font::Heading,who.gamertag,rail.right()-tx-ui.px(12)),
            Xna::Vector2(tx,face.y+ui.px(2)),Palette::text());
        const float iy=face.y+ui.px(40);
        if(who.online) {
            ui.style.icon(ui.batch,Icon::Trophy,Box{tx,iy,ui.px(18),ui.px(18)},Palette::gold());
            ui.style.text(ui.batch,Font::Caption,std::to_string(who.gamerScore),Xna::Vector2(tx+ui.px(24),iy),Palette::muted());
        }
        const auto color=!who.online?Palette::offline():who.status=="away"?Palette::away():who.status=="busy"?Palette::busy():Palette::online();
        statusDot(ui,Xna::Vector2(tx+ui.px(7),iy+ui.px(34)),color);
        const char* label=!who.online?(who.guest?"Guest":"Offline profile"):who.status=="away"?"Away":who.status=="busy"?"Busy":"Online";
        ui.style.text(ui.batch,Font::Caption,label,Xna::Vector2(tx+ui.px(20),iy+ui.px(24)),Palette::muted());
    }
    ui.style.fill(ui.batch,Box{rail.x+ui.px(20),rail.y+ui.px(124),rail.w-ui.px(40),ui.px(1)},Xna::Color(18,18,18,18));
    // Categories.
    float y=rail.y+ui.px(140);
    for(const auto& entry:Rail) {
        const Box row{rail.x+ui.px(12),y,rail.w-ui.px(24),ui.px(42)};
        const bool lit=entry.category==current;
        if(lit) {
            ui.style.rounded(ui.batch,row,ui.px(12),faded(Palette::focus(),1.3f*alpha));
            ui.style.rounded(ui.batch,Box{row.x,row.y+ui.px(10),ui.px(4),row.h-ui.px(20)},ui.px(2),Palette::accent());
        }
        const float s=ui.px(22);
        ui.style.icon(ui.batch,entry.icon,Box{row.x+ui.px(16),row.y+(row.h-s)/2,s,s},lit?Palette::accent():Palette::muted());
        const float h=ui.style.measure(Font::Body,"Ag").Y;
        ui.style.text(ui.batch,lit?Font::BodyBold:Font::Body,entry.label,Xna::Vector2(row.x+ui.px(50),row.y+(row.h-h)/2),
            lit?Palette::text():Palette::muted());
        if(entry.category==Category::Messages&&who.unread>0) {
            const auto count=std::to_string(who.unread);
            const float w=std::max(ui.px(24),ui.style.measure(Font::Caption,count).X+ui.px(12));
            ui.style.rounded(ui.batch,Box{row.right()-w-ui.px(10),row.y+ui.px(10),w,ui.px(22)},ui.px(11),Palette::accent());
            ui.style.text(ui.batch,Font::Caption,count,Xna::Vector2(row.right()-w/2-ui.px(10),row.y+ui.px(10)),Palette::text(),Align::Center);
        }
        y+=ui.px(46);
    }
}

void drawShell(Ui& ui,Screen& screen,float appear,float change)
{
    const auto& style=ui.style;
    const float W=static_cast<float>(style.width()),H=static_cast<float>(style.height());
    const float w=std::min(W-ui.px(40),ui.px(1080)),h=std::min(H-ui.px(40),ui.px(612));
    Box panel{(W-w)/2,(H-h)/2+(1.0f-appear)*ui.px(18),w,h};
    ui.style.shadow(ui.batch,panel,ui.px(22),ui.px(26),faded(Xna::Color(0,0,0,150),appear));
    ui.style.rounded(ui.batch,panel,ui.px(22),faded(Palette::panel(),appear));
    const Box rail{panel.x,panel.y,ui.px(280),panel.h};
    ui.style.rounded(ui.batch,rail,ui.px(22),faded(Palette::rail(),appear));
    ui.style.fill(ui.batch,Box{rail.right()-ui.px(22),rail.y,ui.px(22),rail.h},faded(Palette::rail(),appear));
    drawRail(ui,rail,screen.category(),appear);
    // Header, content and hints; the content slides in when the screen changes.
    const float slide=(1.0f-change)*ui.px(22);
    const float cx=rail.right()+ui.px(30);
    ui.style.text(ui.batch,Font::Title,screen.title(),Xna::Vector2(cx+slide,panel.y+ui.px(24)),faded(Palette::text(),change));
    const auto subtitle=screen.subtitle();
    if(!subtitle.empty())
        ui.style.text(ui.batch,Font::Body,subtitle,Xna::Vector2(cx+slide,panel.y+ui.px(72)),faded(Palette::muted(),change));
    const Box content{cx+slide,panel.y+ui.px(114),panel.right()-cx-ui.px(30),panel.h-ui.px(114)-ui.px(72)};
    screen.draw(ui,content);
    ui.style.fill(ui.batch,Box{cx,panel.bottom()-ui.px(62),panel.right()-cx-ui.px(30),ui.px(1)},Xna::Color(18,18,18,18));
    auto hints=screen.hints();
    drawHints(ui,hints,Xna::Vector2(panel.right()-ui.px(30),panel.bottom()-ui.px(31)),true);
    std::vector<Hint> nav{{Command::Previous,""},{Command::Next,"Switch"}};
    drawHints(ui,nav,Xna::Vector2(rail.x+ui.px(22),panel.bottom()-ui.px(31)),false);
}

void drawDialog(Ui& ui,Screen& screen,float appear)
{
    const float W=static_cast<float>(ui.style.width()),H=static_cast<float>(ui.style.height());
    const float w=std::min(W-ui.px(40),ui.px(screen.dialogWidth()));
    const float h=std::min(H-ui.px(40),ui.px(screen.dialogHeight()));
    Box card{(W-w)/2,(H-h)/2+(1.0f-appear)*ui.px(16),w,h};
    ui.style.shadow(ui.batch,card,ui.px(20),ui.px(24),faded(Xna::Color(0,0,0,160),appear));
    ui.style.rounded(ui.batch,card,ui.px(20),faded(Palette::panel(),appear));
    ui.style.rounded(ui.batch,Box{card.x,card.y,card.w,ui.px(6)},ui.px(3),faded(Palette::accent(),appear));
    ui.style.text(ui.batch,Font::Heading,screen.title(),Xna::Vector2(card.x+ui.px(28),card.y+ui.px(24)),faded(Palette::text(),appear));
    const auto subtitle=screen.subtitle();
    if(!subtitle.empty())ui.style.text(ui.batch,Font::Body,subtitle,Xna::Vector2(card.x+ui.px(28),card.y+ui.px(62)),faded(Palette::muted(),appear));
    const float top=ui.px(subtitle.empty()?74:108);
    const Box content{card.x+ui.px(28),card.y+top,card.w-ui.px(56),card.h-top-ui.px(66)};
    screen.draw(ui,content);
    drawHints(ui,screen.hints(),Xna::Vector2(card.right()-ui.px(26),card.bottom()-ui.px(32)),true);
}

Icon iconFor(Notification::Kind kind)
{
    switch(kind) {
    case Notification::Kind::SignIn: return Icon::Person;
    case Notification::Kind::SignOut: return Icon::SignOut;
    case Notification::Kind::FriendOnline: return Icon::People;
    case Notification::Kind::FriendRequest: return Icon::PersonAdd;
    case Notification::Kind::Invitation: return Icon::Invite;
    case Notification::Kind::Party: return Icon::Party;
    case Notification::Kind::Achievement: return Icon::Trophy;
    case Notification::Kind::Message: return Icon::Message;
    case Notification::Kind::Service: return Icon::Globe;
    case Notification::Kind::Info: break;
    }
    return Icon::Info;
}

void drawToast(Ui& ui,const GuideToast& toast)
{
    const auto& n=toast.notification;
    const bool twoLines=!n.text.empty();
    const float icon=ui.px(twoLines?52:40);
    const float textWidth=std::max(ui.style.measure(Font::BodyBold,n.title).X,twoLines?ui.style.measure(Font::Caption,n.text).X:0.0f);
    const float w=std::min(static_cast<float>(ui.style.width())*0.6f,icon+ui.px(46)+textWidth+ui.px(18));
    const float h=ui.px(twoLines?78:62);
    const auto origin=guideNotificationOrigin(GS::Guide::getNotificationPositionProperty(),ui.style.width(),ui.style.height(),
        static_cast<int>(w),static_cast<int>(h));
    // Slides in from the nearer side, fades out at the end.
    const float in=ease(toast.age,0.22);
    const double remaining=std::chrono::duration<double>(GuideNotificationDuration).count()-toast.age;
    const float out=reducedMotion()?1.0f:static_cast<float>(std::clamp(remaining/0.3,0.0,1.0));
    const float alpha=in*out;
    const int column=static_cast<int>(GS::Guide::getNotificationPositionProperty())%3;
    const float dx=(1.0f-in)*ui.px(column==0?-40:column==2?40:0);
    const float dy=(1.0f-in)*ui.px(column==1?(static_cast<int>(GS::Guide::getNotificationPositionProperty())<3?-30:30):0);
    const Box box{static_cast<float>(origin.X)+dx,static_cast<float>(origin.Y)+dy,w,h};
    ui.style.shadow(ui.batch,box,ui.px(18),ui.px(16),faded(Xna::Color(0,0,0,140),alpha));
    ui.style.rounded(ui.batch,box,ui.px(18),faded(Palette::panel(),alpha));
    const Box tile{box.x+ui.px(12),box.y+(h-icon)/2,icon,icon};
    if(!n.avatar.empty()) {
        portrait(ui,n.avatar,tile);
    } else {
        ui.style.rounded(ui.batch,tile,ui.px(12),faded(n.kind==Notification::Kind::Achievement?Palette::gold():Palette::accent(),alpha));
        ui.style.icon(ui.batch,iconFor(n.kind),tile.inset(icon*0.2f),faded(Palette::panel(),alpha));
    }
    const float tx=tile.right()+ui.px(14);
    if(twoLines) {
        ui.style.text(ui.batch,Font::BodyBold,n.title,Xna::Vector2(tx,box.y+ui.px(12)),faded(Palette::text(),alpha));
        ui.style.text(ui.batch,Font::Caption,n.text,Xna::Vector2(tx,box.y+ui.px(42)),faded(Palette::muted(),alpha));
    } else {
        const float th=ui.style.measure(Font::BodyBold,"Ag").Y;
        ui.style.text(ui.batch,Font::BodyBold,n.title,Xna::Vector2(tx,box.y+(h-th)/2),faded(Palette::text(),alpha));
    }
}

// The game's own message box (Guide.BeginShowMessageBox), as a system dialog.
void drawMessageBox(Ui& ui,const MessageBoxView& view)
{
    const float W=static_cast<float>(ui.style.width()),H=static_cast<float>(ui.style.height());
    const float w=std::min(W-ui.px(40),ui.px(620));
    const float lineHeight=ui.style.measure(Font::Body,"Ag").Y;
    const bool hasIcon=view.icon!=GS::MessageBoxIcon::None;
    const float textLeft=hasIcon?ui.px(72):0.0f;
    const auto lines=ui.style.wrap(Font::Body,view.text,w-ui.px(56)-textLeft);
    const float buttonsH=static_cast<float>(view.buttons.size())*ui.px(52);
    const float h=ui.px(84)+std::max(static_cast<float>(lines.size())*lineHeight,hasIcon?ui.px(48):0.0f)+ui.px(22)+buttonsH+ui.px(20);
    const Box card{(W-w)/2,(H-h)/2,w,h};
    ui.style.shadow(ui.batch,card,ui.px(20),ui.px(24),Xna::Color(0,0,0,160));
    ui.style.rounded(ui.batch,card,ui.px(20),Palette::panel());
    ui.style.rounded(ui.batch,Box{card.x,card.y,card.w,ui.px(6)},ui.px(3),Palette::accent());
    ui.style.text(ui.batch,Font::Heading,ui.style.fit(Font::Heading,view.title,w-ui.px(56)),Xna::Vector2(card.x+ui.px(28),card.y+ui.px(24)),Palette::text());
    float y=card.y+ui.px(78);
    if(hasIcon) {
        const Icon icon=view.icon==GS::MessageBoxIcon::Error?Icon::Error:view.icon==GS::MessageBoxIcon::Warning?Icon::Warning:
                        view.icon==GS::MessageBoxIcon::Alert?Icon::Info:Icon::Question;
        ui.style.icon(ui.batch,icon,Box{card.x+ui.px(28),y,ui.px(48),ui.px(48)},view.icon==GS::MessageBoxIcon::Error?Palette::error():
            view.icon==GS::MessageBoxIcon::Warning?Palette::away():Palette::accent());
    }
    for(const auto& line:lines) {
        ui.style.text(ui.batch,Font::Body,line,Xna::Vector2(card.x+ui.px(28)+textLeft,y),Palette::text());
        y+=lineHeight;
    }
    y=std::max(y,card.y+ui.px(78)+(hasIcon?ui.px(48):0.0f))+ui.px(22);
    // Buttons, one per row, the focused one lit; the mouse and every navigation device answer it.
    int focus=view.focus;
    std::optional<int> answer;
    for(std::size_t i=0;i<view.buttons.size();++i) {
        const Box row{card.x+ui.px(20),y,card.w-ui.px(40),ui.px(46)};
        if(row.contains(ui.input.mouse.X,ui.input.mouse.Y)) {
            if(ui.input.mouseMoved)focus=static_cast<int>(i);
            if(ui.input.click)answer=static_cast<int>(i);
        }
        rowBackground(ui,row,static_cast<int>(i)==focus);
        const float th=ui.style.measure(Font::BodyBold,"Ag").Y;
        ui.style.text(ui.batch,Font::BodyBold,view.buttons[i],Xna::Vector2(row.x+ui.px(22),row.y+(row.h-th)/2),Palette::text());
        y+=ui.px(52);
    }
    const int count=static_cast<int>(view.buttons.size());
    if(ui.input(Command::Up)||ui.input(Command::Left))focus=(focus+count-1)%count;
    if(ui.input(Command::Down)||ui.input(Command::Right)||(ui.input(Command::Next)&&ui.input.keyboard[static_cast<std::size_t>(Command::Next)]))
        focus=(focus+1)%count;
    if(focus!=view.focus)focusMessageBox(focus);
    if(answer){answerMessageBox(answer);return;}
    if(ui.input(Command::Accept)){answerMessageBox(focus);return;}
    if(ui.input(Command::Back))answerMessageBox(std::nullopt);
}

// The game's keyboard (Guide.BeginShowKeyboardInput): a text field, and for a controller an
// on-screen keyboard that types through the same path as real keys.
constexpr const char* KeyRows[]={"1234567890","qwertyuiop","asdfghjkl'","zxcvbnm,.-"};
struct KeyboardState {int row=0,column=0;bool shift=false;bool pad=false;};
KeyboardState keyboard;

void drawKeyboard(Ui& ui,const KeyboardView& view)
{
    if(ui.input.device==Device::Pad)keyboard.pad=true;
    if(ui.input.device==Device::Keyboard)keyboard.pad=false;
    const float W=static_cast<float>(ui.style.width()),H=static_cast<float>(ui.style.height());
    const float w=std::min(W-ui.px(40),ui.px(700));
    const float keysH=keyboard.pad?ui.px(5*50+10):0.0f;
    const float h=ui.px(200)+keysH;
    const Box card{(W-w)/2,(H-h)/2,w,h};
    ui.style.shadow(ui.batch,card,ui.px(20),ui.px(24),Xna::Color(0,0,0,160));
    ui.style.rounded(ui.batch,card,ui.px(20),Palette::panel());
    ui.style.rounded(ui.batch,Box{card.x,card.y,card.w,ui.px(6)},ui.px(3),Palette::accent());
    ui.style.text(ui.batch,Font::Heading,ui.style.fit(Font::Heading,view.title,w-ui.px(56)),Xna::Vector2(card.x+ui.px(28),card.y+ui.px(22)),Palette::text());
    ui.style.text(ui.batch,Font::Body,ui.style.fit(Font::Body,view.description,w-ui.px(56)),Xna::Vector2(card.x+ui.px(28),card.y+ui.px(62)),Palette::muted());
    const Box field{card.x+ui.px(24),card.y+ui.px(100),card.w-ui.px(48),ui.px(52)};
    ui.style.rounded(ui.batch,field,ui.px(12),Palette::accent());
    ui.style.rounded(ui.batch,field.inset(ui.px(2)),ui.px(10),Xna::Color(12,16,24));
    const auto shown=ui.style.fit(Font::Body,view.text,field.w-ui.px(40));
    const float th=ui.style.measure(Font::Body,"Ag").Y;
    ui.style.text(ui.batch,Font::Body,shown,Xna::Vector2(field.x+ui.px(16),field.y+(field.h-th)/2),Palette::text());
    if(std::fmod(ui.time,1.0)<0.6||reducedMotion()) {
        const float cx=field.x+ui.px(18)+ui.style.measure(Font::Body,shown).X;
        ui.style.fill(ui.batch,Box{cx,field.y+ui.px(12),ui.px(2),field.h-ui.px(24)},Palette::accent());
    }
    if(keyboard.pad) {
        // Four rows of keys and a row of actions: space, delete, shift, done.
        const float keyW=(card.w-ui.px(48)-ui.px(9*6))/10.0f,keyH=ui.px(44);
        float y=field.bottom()+ui.px(16);
        for(int r=0;r<5;++r) {
            const int columns=r<4?10:4;
            for(int c=0;c<columns;++c) {
                const float kw=r<4?keyW:(card.w-ui.px(48)-ui.px(3*6))/4.0f;
                const Box key{card.x+ui.px(24)+static_cast<float>(c)*(kw+ui.px(6)),y,kw,keyH};
                const bool focused=keyboard.row==r&&keyboard.column==c;
                ui.style.rounded(ui.batch,key,ui.px(8),focused?Palette::accent():Palette::surface());
                std::string label;
                if(r<4){label=std::string(1,KeyRows[r][c]);if(keyboard.shift)label[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));}
                else label=c==0?"Space":c==1?"Delete":c==2?(keyboard.shift?"abc":"Shift"):"Done";
                const auto size=ui.style.measure(Font::Body,label);
                ui.style.text(ui.batch,Font::Body,label,Xna::Vector2(key.x+key.w/2,key.y+(key.h-size.Y)/2),Palette::text(),Align::Center);
            }
            y+=keyH+ui.px(6);
        }
    }
    // Keys typed on a real keyboard arrive through text input (Enter commits); these handle the
    // controller and Escape.
    std::vector<Hint> hints;
    if(keyboard.pad)hints={{Command::Accept,"Type"},{Command::X,"Delete"},{Command::Y,"Done"},{Command::Back,"Cancel"}};
    else hints={{Command::Accept,"Done"},{Command::Back,"Cancel"}};
    drawHints(ui,hints,Xna::Vector2(card.right()-ui.px(26),card.bottom()-ui.px(28)),true);
    const bool fromPad=ui.input.device==Device::Pad;
    if(ui.input(Command::Back)){cancelKeyboard();keyboard={};return;}
    if(!fromPad)return;
    if(ui.input(Command::Up))keyboard.row=(keyboard.row+4)%5;
    if(ui.input(Command::Down))keyboard.row=(keyboard.row+1)%5;
    const int columns=keyboard.row<4?10:4;
    keyboard.column=std::min(keyboard.column,columns-1);
    if(ui.input(Command::Left))keyboard.column=(keyboard.column+columns-1)%columns;
    if(ui.input(Command::Right))keyboard.column=(keyboard.column+1)%columns;
    if(ui.input(Command::X))typeKeyboard(u'\b');
    if(ui.input(Command::Y)){typeKeyboard(u'\r');keyboard={};return;}
    if(ui.input(Command::Accept)) {
        if(keyboard.row<4) {
            char c=KeyRows[keyboard.row][keyboard.column];
            if(keyboard.shift)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            typeKeyboard(static_cast<char16_t>(c));
        } else if(keyboard.column==0)typeKeyboard(u' ');
        else if(keyboard.column==1)typeKeyboard(u'\b');
        else if(keyboard.column==2)keyboard.shift=!keyboard.shift;
        else {typeKeyboard(u'\r');keyboard={};}
    }
}

std::deque<Command> injected;
std::optional<int> injectedClick;

void apply(Screen& screen,InputContext& ui)
{
    auto& s=state();
    const auto before=s.stack.size();
    screen.input(ui);
    // Back leaves any screen that did not act on it itself; leaving the root closes the Guide.
    if(ui.input(Command::Back)&&s.stack.size()==before&&!s.stack.empty()&&s.stack.back().get()==&screen) {
        pop(&screen);
        return;
    }
    if(screen.category()!=Category::None&&(ui.input(Command::Previous)||ui.input(Command::Next))) {
        // Rail navigation: the next category's root replaces the stack.
        const auto current=std::ranges::find(Rail,screen.category(),&RailEntry::category);
        if(current!=std::end(Rail)) {
            const auto count=static_cast<int>(std::size(Rail));
            int index=static_cast<int>(current-std::begin(Rail))+(ui.input(Command::Next)?1:-1);
            index=(index+count)%count;
            if(auto next=rootFor(Rail[index].category,screen.player))replace(std::move(next));
        }
    }
}
}

namespace {
// Resources belong to one device and go when it is disposed or reset (a later device may even
// reuse its address).
Style& styleFor(Xna::Graphics::GraphicsDevice& device,int width,int height)
{
    auto& s=state();
    if(s.device!=&device) {
        s.portraits.clear();
        s.style.reset();
        s.batch.reset();
        s.device=&device;
        device.Disposing.Add([](System::Object*,const System::EventArgs&){releaseDeviceResources();});
        device.DeviceResetting.Add([](System::Object*,const System::EventArgs&){releaseDeviceResources();});
    }
    if(!s.style||!s.style->matches(device,width,height))s.style=std::make_unique<Style>(device,width,height);
    if(!s.batch)s.batch=std::make_unique<Xna::Graphics::SpriteBatch>(device);
    return *s.style;
}
}

namespace {
void primeOnNewTop()
{
    auto& s=state();
    std::string top;
    if(auto box=pendingMessageBox())top="box:"+box->title+"\n"+box->text;
    else if(auto keys=pendingKeyboard())top="keys:"+keys->title+"\n"+keys->description;
    else if(!s.stack.empty())top="screen:"+std::to_string(reinterpret_cast<std::uintptr_t>(s.stack.back().get()));
    if(top!=s.shownTop){s.shownTop=top;s.input.prime();}
}
}

void draw(Xna::Graphics::GraphicsDevice& device)
{
    auto& s=state();
    releaseTouchSuppression();
    const auto toast=currentGuideToast();
    const bool dialogs=pendingMessageBox()||pendingKeyboard();
    if(s.stack.empty()&&!toast&&!dialogs){s.wasShowing=false;s.lastToast.clear();return;}
    const auto viewport=device.getViewportProperty();
    const int width=viewport.getWidthProperty(),height=viewport.getHeightProperty();
    (void)styleFor(device,width,height);
    const double now=seconds();
    primeOnNewTop();
    Ui ui(*s.style,*s.batch,s.portraits,InputContext{s.input.poll(now),now-s.openedAt,now-s.topChangedAt,s.player});
    // The system sounds of a frame a player drives: opening, moving, choosing, going back, a notification.
    const bool showing=!s.stack.empty()||dialogs;
    if(showing&&!s.wasShowing)play(Sound::Open);
    else if(showing) {
        if(ui.input(Command::Back))play(Sound::Back);
        else if(ui.input(Command::Accept)||ui.input.click)play(Sound::Accept);
        else if(ui.input(Command::Up)||ui.input(Command::Down)||ui.input(Command::Left)||ui.input(Command::Right)||
                ui.input(Command::Previous)||ui.input(Command::Next))play(Sound::Move);
    }
    s.wasShowing=showing;
    const std::string toastKey=toast?toast->notification.title+"\n"+toast->notification.text:std::string();
    if(toast&&toastKey!=s.lastToast&&toast->age<0.5)play(Sound::Notify);
    s.lastToast=toastKey;
    // Input goes to whatever is on top: the game's dialog, else the top screen.
    if(!dialogs&&!s.stack.empty()) {
        auto top=s.stack.back();
        apply(*top,ui);
        noteTop();
    }
    // Under the top: the nearest full-screen screen, or else the nearest shell screen.
    std::shared_ptr<Screen> base;
    for(auto it=s.stack.rbegin();it!=s.stack.rend();++it)
        if((*it)->fullScreen()||(*it)->category()!=Category::None){base=*it;break;}
    const bool full=base&&base->fullScreen();
    // 3D views are drawn before any sprite, into their own targets.
    if(full)base->render(device,now);
    s.portraits.render(device,now);
    s.batch->Begin(Xna::Graphics::SpriteSortMode::Deferred,&Xna::Graphics::BlendState::AlphaBlend,&Xna::Graphics::SamplerState::LinearClamp,
        nullptr,&Xna::Graphics::RasterizerState::CullNone);
    if(!s.stack.empty()||dialogs) {
        const float appear=ease(ui.time,0.18);
        const Box screenBox{0,0,static_cast<float>(width),static_cast<float>(height)};
        if(full) {
            // A full-screen system screen replaces the picture, fading in from black.
            base->draw(ui,screenBox);
            if(appear<1.0f)s.style->fill(*s.batch,screenBox,Xna::Color(0,0,0,static_cast<int>(255*(1.0f-appear))));
        } else {
            s.style->fill(*s.batch,screenBox,faded(Palette::dim(),s.stack.empty()?1.0f:appear));
            if(base)drawShell(ui,*base,appear,base==s.stack.back()?ease(ui.screenTime,0.16):1.0f);
        }
        // A dialog above them.
        if(!s.stack.empty()&&s.stack.back()!=base) {
            if(base)s.style->fill(*s.batch,screenBox,Xna::Color(0,0,0,full?130:90));
            drawDialog(ui,*s.stack.back(),base?ease(ui.screenTime,0.16):appear);
        }
        if(auto box=pendingMessageBox())drawMessageBox(ui,*box);
        else if(auto keys=pendingKeyboard())drawKeyboard(ui,*keys);
    }
    if(toast)drawToast(ui,*toast);
    s.batch->End();
}

void drawGameDialogs(Xna::Graphics::GraphicsDevice& device,Xna::Graphics::SpriteBatch& batch)
{
    auto& s=state();
    const auto viewport=device.getViewportProperty();
    (void)styleFor(device,viewport.getWidthProperty(),viewport.getHeightProperty());
    const double now=seconds();
    primeOnNewTop();
    Ui ui(*s.style,batch,s.portraits,InputContext{s.input.poll(now),now,now,s.player});
    if(auto box=pendingMessageBox())drawMessageBox(ui,*box);
    else if(auto keys=pendingKeyboard())drawKeyboard(ui,*keys);
}

void releaseDeviceResources()
{
    auto& s=state();
    s.portraits.clear();
    s.style.reset();
    s.batch.reset();
    s.device=nullptr;
}

std::string currentScreenForTesting(){return state().stack.empty()?std::string():state().stack.back()->name();}
std::vector<std::string> labelsForTesting(){return state().stack.empty()?std::vector<std::string>{}:state().stack.back()->labels();}
int focusForTesting(){return state().stack.empty()?-1:state().stack.back()->focus();}

bool busyForTesting()
{
    auto& s=state();
    return std::ranges::any_of(s.stack,[](const auto& screen){return screen->busy();})||s.portraits.waiting()>0;
}

namespace {
void frameWith(std::optional<Command> command)
{
    auto& s=state();
    if(s.stack.empty())return;
    // Screens handle input without drawing; a test needs no device.
    InputContext context;
    if(command)context.input.pressed[static_cast<std::size_t>(*command)]=true;
    context.input.mouse=Xna::Vector2(-1,-1);
    context.player=s.player;
    auto top=s.stack.back();
    apply(*top,context);
    noteTop();
}
}

void frameForTesting(){frameWith(std::nullopt);}
void sendForTesting(Command command){frameWith(command);}

void clickForTesting(int index)
{
    auto& s=state();
    if(!s.stack.empty())s.stack.back()->activate(index);
    noteTop();
}
}
