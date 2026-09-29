// SPDX-License-Identifier: MS-PL
#pragma once
// What every Guide screen is built from: the per-frame context, the Screen base, a focusable
// list, and asynchronous service reads that land on the dispatcher thread.
#include "GuidePortraits.hpp"
#include "GuideStyle.hpp"
#include "GuideUi.hpp"
#include "GuideSystem.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <array>
#include <functional>
#include <optional>
#include <type_traits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief Which input device was used last (hints show its buttons). */
enum class Device : std::uint8_t { Keyboard, Pad, Mouse };

/** @brief The input of one frame. */
struct InputFrame {
    /** @brief Commands pressed this frame (with key repeat for directions). */
    std::array<bool,10> pressed{};
    /** @brief Commands whose press came from the keyboard (letters must not act while typing). */
    std::array<bool,10> keyboard{};
    /** @brief Mouse position. */
    Xna::Vector2 mouse;
    /** @brief The mouse moved this frame. */
    bool mouseMoved=false;
    /** @brief Left button pressed this frame. */
    bool click=false;
    /** @brief Wheel notches this frame (positive up). */
    int wheel=0;
    /** @brief Left button held. */
    bool mouseDown=false;
    /** @brief Turn (x, -1..1: right stick, Z/C) and zoom (y, -1..1, positive in: triggers, +/-) held this frame. */
    Xna::Vector2 look;
    /** @brief Last device used. */
    Device device=Device::Keyboard;
    /** @brief Whether a command was pressed. @param command Command. @return Pressed. */
    [[nodiscard]] bool operator()(Command command) const { return pressed[static_cast<std::size_t>(command)]; }
};

/** @brief The rail's categories; None for screens drawn as dialogs. */
enum class Category : std::uint8_t { Home, Friends, Party, Messages, Achievements, Leaderboards, Players, Content, Settings, None };

/** @brief A footer hint: a command and what it does. */
using Hint=std::pair<Command,std::string>;

/** @brief What input handling sees: no drawing resources, so tests drive screens headless. */
struct InputContext {
    /** @brief This frame's input. */
    InputFrame input;
    /** @brief Seconds since the Guide opened. */
    double time=0;
    /** @brief Seconds since the top screen appeared. */
    double screenTime=0;
    /** @brief The player the Guide belongs to. */
    Xna::PlayerIndex player=Xna::PlayerIndex::One;
};

/** @brief Per-frame drawing context. */
struct Ui : InputContext {
    /** @brief Creates the context. @param style Look. @param batch Begun batch. @param portraits Avatar views.
     * @param context Input. */
    Ui(Style& style,Xna::Graphics::SpriteBatch& batch,Portraits& portraits,InputContext context)
        : InputContext(std::move(context)),style(style),batch(batch),portraits(portraits) {}
    /** @brief Look. */
    Style& style;
    /** @brief Batch, begun. */
    Xna::Graphics::SpriteBatch& batch;
    /** @brief Avatar views. */
    Portraits& portraits;
    /** @brief A reference length at the UI scale. @param value Length. @return Pixels. */
    [[nodiscard]] float px(float value) const { return style.px(value); }
};

/** @brief A Guide screen. */
class Screen : public std::enable_shared_from_this<Screen> {
public:
    /** @brief Destroys the screen. */
    virtual ~Screen()=default;
    /** @brief Stable name for tests ("friends"). @return Name. */
    [[nodiscard]] virtual std::string name() const=0;
    /** @brief Heading. @return Title. */
    [[nodiscard]] virtual std::string title() const=0;
    /** @brief Line under the heading. @return Subtitle. */
    [[nodiscard]] virtual std::string subtitle() const { return {}; }
    /** @brief Rail category lit while it is up; None draws the screen as a dialog. @return Category. */
    [[nodiscard]] virtual Category category() const { return Category::None; }
    /** @brief Handles this frame's input (before draw). @param ui Input. */
    virtual void input(InputContext& ui) { (void)ui; }
    /** @brief Draws the content. @param ui Context. @param area Content area. */
    virtual void draw(Ui& ui,const Box& area)=0;
    /** @brief Footer hints. @return Hints. */
    [[nodiscard]] virtual std::vector<Hint> hints() const { return {{Command::Back,"Back"}}; }
    /** @brief Items as shown, for tests. @return Labels. */
    [[nodiscard]] virtual std::vector<std::string> labels() const { return {}; }
    /** @brief Focused item, for tests. @return Index or -1. */
    [[nodiscard]] virtual int focus() const { return -1; }
    /** @brief Activates an item as a click would. @param index Item. */
    virtual void activate(int index) { (void)index; }
    /** @brief Preferred dialog width (reference pixels). @return Width. */
    [[nodiscard]] virtual float dialogWidth() const { return 600; }
    /** @brief Dialog height (reference pixels), title and hints included. @return Height. */
    [[nodiscard]] virtual float dialogHeight() const { return 300; }
    /** @brief Whether the screen covers the whole display (the avatar editor) rather than sitting
     * in the shell or a dialog. @return Full screen. */
    [[nodiscard]] virtual bool fullScreen() const { return false; }
    /** @brief Draws into render targets before any sprite (a full-screen screen's 3D views).
     * @param device Device. @param seconds Guide time. */
    virtual void render(Xna::Graphics::GraphicsDevice& device,double seconds) { (void)device; (void)seconds; }
    /** @brief Whether the screen is still waiting for something it will show (tests and captures).
     * @return Busy. */
    [[nodiscard]] virtual bool busy() const { return false; }
    /** @brief Player the screen belongs to. */
    Xna::PlayerIndex player=Xna::PlayerIndex::One;
};

/** @brief Pushes a screen above the top one. @param screen Screen. */
void push(std::shared_ptr<Screen> screen);
/** @brief Removes a screen (and any above it); closing the root closes the Guide. @param screen Screen. */
void pop(const Screen* screen);
/** @brief Replaces the whole stack with one screen (rail navigation). @param screen Screen. */
void replace(std::shared_ptr<Screen> screen);

/** @brief A vertical list with a focus, scrolling and mouse hit-testing. */
struct List {
    /** @brief Focused row. */
    int focus=0;
    /** @brief First visible row (smoothly followed). */
    float scroll=0;
    /** @brief Row boxes drawn last frame, for the mouse. */
    std::vector<std::pair<int,Box>> hits;
    /** @brief Moves the focus by input. @param ui Context. @param count Rows. @param columns Columns (grids).
     * @return The row activated (Accept or click), or -1. */
    int input(InputContext& ui,int count,int columns=1);
    /** @brief Draws visible rows. @param ui Context. @param area Area. @param count Rows. @param rowHeight Height
     * (reference pixels, including the gap). @param row Draws one row (index, box, focused).
     * @param columns Columns. */
    void draw(Ui& ui,const Box& area,int count,float rowHeight,const std::function<void(int,const Box&,bool)>& row,int columns=1);
};

/** @brief Runs a service read on the backend and hands the result to a screen at the next
 * dispatcher update, if the screen still exists; failures call failed. @param owner Screen.
 * @param work Service work. @param done Result. @param failed Failure. */
template<typename T,typename S>
void load(const std::shared_ptr<S>& owner,std::type_identity_t<std::function<T(IGamerServicesBackend&)>> work,
    std::type_identity_t<std::function<void(S&,T)>> done,std::type_identity_t<std::function<void(S&)>> failed={})
{
    auto service=backend();
    auto result=std::make_shared<std::optional<T>>();
    const std::weak_ptr<S> weak=owner;
    const std::weak_ptr<IGamerServicesBackend> origin=service;
    service->submit([result,origin,work=std::move(work)] {
        try {
            if(auto executor=origin.lock())*result=work(*executor);
        } catch(...) {}
    },[result,weak,done=std::move(done),failed=std::move(failed)] {
        auto screen=weak.lock();
        if(!screen)return;
        if(*result)done(*screen,std::move(**result));
        else if(failed)failed(*screen);
    });
}

/** @brief What a player may use of the service: service screens need a signed-in account, and
 * a local profile, a guest or nobody gets an honest explanation instead. */
enum class Access : std::uint8_t { Account, LocalProfile, Guest, Nobody, NoService };
/** @brief A player's access. @param player Player. @return Access. */
Access access(Xna::PlayerIndex player);
/** @brief Draws the explanation for anything but Account. @param ui Context. @param area Area.
 * @param value Access. @return Whether it drew one (the screen draws nothing else). */
bool explainAccess(Ui& ui,const Box& area,Access value);
/** @brief A player's service identity, or empty. @param player Player. @return User id. */
std::string userOf(Xna::PlayerIndex player);
/** @brief Whether motion is reduced (CNA_GAMER_SERVICES_REDUCED_MOTION=1): transitions finish at once. @return Reduced. */
bool reducedMotion();
/** @brief 0..1 over a duration, eased out; 1 at once with reduced motion. @param elapsed Seconds.
 * @param duration Seconds. @return Progress. */
float ease(double elapsed,double duration);
/** @brief Draws a row of button hints. @param ui Context. @param hints Hints. @param position Start
 * (or end when right-aligned), vertical centre. @param rightAligned Right-aligned. @return Width. */
float drawHints(Ui& ui,const std::vector<Hint>& hints,Xna::Vector2 position,bool rightAligned);
/** @brief Draws a focus highlight behind a row. @param ui Context. @param box Row. @param focused Focused. */
void rowBackground(Ui& ui,const Box& box,bool focused);
/** @brief Draws an avatar portrait tile (or a person icon while it loads). @param ui Context.
 * @param avatar Description bytes (empty: none). @param box Square. @param framing Framing. @param live Animate. */
void portrait(Ui& ui,const std::vector<unsigned char>& avatar,const Box& box,Framing framing=Framing::Head,bool live=false);
/** @brief Draws a presence dot. @param ui Context. @param center Centre. @param color Color. */
void statusDot(Ui& ui,Xna::Vector2 center,Xna::Color color);
/** @brief Draws a "loading" row of dots. @param ui Context. @param area Area to centre it in. @param text Label. */
void loading(Ui& ui,const Box& area,const std::string& text="Loading");
/** @brief Draws a centred message with an icon (empty lists, errors, offline). @param ui Context.
 * @param area Area. @param icon Icon. @param title Title. @param text Explanation. */
void emptyState(Ui& ui,const Box& area,Icon icon,const std::string& title,const std::string& text);
/** @brief Draws a vertical list of labelled action buttons (input through list.input).
 * @param ui Context. @param list Focus. @param area Area. @param actions Icons and labels. */
void drawActions(Ui& ui,List& list,const Box& area,const std::vector<std::pair<Icon,std::string>>& actions);
/** @brief Shows a short confirmation or error as a system dialog. @param player Player.
 * @param title Title. @param text Text. @param icon Icon. */
void inform(Xna::PlayerIndex player,std::string title,std::string text,Icon icon=Icon::Info);
/** @brief The signed-in gamer of a player, or null. @param player Player. @return Gamer's service id and tag. */
std::optional<std::pair<std::string,std::string>> signedInAccount(Xna::PlayerIndex player);
}
