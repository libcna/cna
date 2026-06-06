#include "InputDemo.hpp"

#include <cmath>

#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Input::Touch;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

static const Color BG         {15,  15,  20,  255};
static const Color KEY_OFF    {50,  50,  50,  255};
static const Color KEY_ON     {70,  220, 70,  255};
static const Color MOUSE_OFF  {30,  30,  80,  255};
static const Color MOUSE_ON   {80,  140, 255, 255};
static const Color TOUCH_CLR  {255, 200, 0,   255};
static const Color GP_OFF     {60,  30,  10,  255};
static const Color GP_ON      {255, 130, 0,   255};
static const Color DPAD_OFF   {50,  50,  50,  255};
static const Color DPAD_ON    {210, 210, 210, 255};
static const Color TRIG_BG    {40,  40,  40,  255};
static const Color TRIG_FILL  {200, 80,  0,   255};
static const Color STICK_BG   {30,  30,  30,  255};
static const Color STICK_DOT  {200, 200, 200, 255};
static const Color CONN_YES   {0,   200, 80,  255};
static const Color CONN_NO    {160, 30,  30,  255};
static const Color SECT_BG    {25,  25,  35,  255};
static const Color CURSOR_CLR {255, 80,  80,  255};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

InputDemo::InputDemo()
    : pixel_()
{
    System::TimeSpan target = System::TimeSpan::FromMilliseconds(1000.0 / 60.0);
    Game::setTargetElapsedTimeProperty(target);
    Game::getWindowProperty().setTitleProperty("CNA Input & Touch Demo");

    TouchPanel::setEnabledGesturesProperty(
        GestureType::Tap | GestureType::FreeDrag | GestureType::Flick);
}

InputDemo::~InputDemo()
{
    delete spriteBatch_;
}

// ---------------------------------------------------------------------------
// LoadContent
// ---------------------------------------------------------------------------

void InputDemo::LoadContent()
{
    spriteBatch_ = new Graphics::SpriteBatch(getGraphicsDeviceProperty());

    // Create a 1×1 white pixel used for all rectangle drawing.
    pixel_ = Graphics::Texture2D(getGraphicsDeviceProperty(), 1, 1);
    Color white{255, 255, 255, 255};
    pixel_.SetData(&white, 1);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void InputDemo::Update(GameTime& gameTime)
{
    (void)gameTime;
    if (Keyboard::GetState().IsKeyDown(Keys::Escape))
    {
        Exit();
    }
    TouchPanel::Update();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void InputDemo::Draw(const GameTime& gameTime)
{
    (void)gameTime;

    getGraphicsDeviceProperty().Clear(BG);

    const KbState kb = Keyboard::GetState();
    const MsState ms = Mouse::GetState();
    const GpState gp = GamePad::GetState(PlayerIndex::One);
    const TC      tc = TouchPanel::GetState();

    spriteBatch_->Begin();

    // Section backgrounds
    DrawRect(10,  10,  440, 380, SECT_BG);  // keyboard section
    DrawRect(10,  400, 440, 190, SECT_BG);  // mouse section
    DrawRect(460, 10,  330, 580, SECT_BG);  // gamepad section

    DrawKeyboard(20, 20, kb);
    DrawMouse(20, 410, ms);
    DrawGamePad(470, 20, gp);
    DrawTouchPoints(tc);

    spriteBatch_->End();
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void InputDemo::DrawRect(int x, int y, int w, int h, Color color)
{
    Rect dest{x, y, w, h};
    Rect src{0, 0, 1, 1};
    spriteBatch_->Draw(pixel_, dest, src, color);
}

void InputDemo::DrawKey(int x, int y, int w, int h, bool pressed)
{
    DrawRect(x, y, w, h, pressed ? KEY_ON : KEY_OFF);
}

void InputDemo::DrawButton(int x, int y, int w, int h, bool pressed, Color onColor)
{
    DrawRect(x, y, w, h, pressed ? onColor : GP_OFF);
}

void InputDemo::DrawBar(int x, int y, int bw, int bh, float value,
                        Color bgColor, Color fillColor)
{
    DrawRect(x, y, bw, bh, bgColor);
    int fill = static_cast<int>(value * static_cast<float>(bw));
    if (fill > 0)
    {
        DrawRect(x, y, fill, bh, fillColor);
    }
}

void InputDemo::DrawStick(int cx, int cy, int radius, float vx, float vy)
{
    DrawRect(cx - radius, cy - radius, radius * 2, radius * 2, STICK_BG);
    // Dot position: map [-1,1] to pixel offset
    const int dx = static_cast<int>(vx * static_cast<float>(radius - 4));
    const int dy = static_cast<int>(-vy * static_cast<float>(radius - 4));
    DrawRect(cx + dx - 4, cy + dy - 4, 8, 8, STICK_DOT);
}

// ---------------------------------------------------------------------------
// Keyboard section (ox, oy = top-left of section content)
// ---------------------------------------------------------------------------

void InputDemo::DrawKeyboard(int ox, int oy, const KbState& kb)
{
    // Each key cell: 32px wide, 28px tall, 3px gap -> step = 35, 31
    constexpr int KW  = 32;
    constexpr int KH  = 28;
    constexpr int SX  = 35; // step x
    constexpr int SY  = 31; // step y

    auto key = [&](int col, int row, int colSpan, Keys k)
    {
        DrawKey(ox + col * SX, oy + row * SY, KW + (colSpan - 1) * SX, KH,
                kb.IsKeyDown(k));
    };

    // Row 0 — special
    key(0, 0, 1, Keys::Escape);
    key(3, 0, 1, Keys::Tab);
    key(5, 0, 1, Keys::Back);
    key(7, 0, 1, Keys::Enter);
    key(9, 0, 1, Keys::LeftShift);
    key(11, 0, 1, Keys::LeftControl);

    // Row 1 — Q..P
    const Keys row1[] = {Keys::Q, Keys::W, Keys::E, Keys::R, Keys::T,
                         Keys::Y, Keys::U, Keys::I, Keys::O, Keys::P};
    for (int c = 0; c < 10; ++c) key(c, 1, 1, row1[c]);

    // Row 2 — A..L
    const Keys row2[] = {Keys::A, Keys::S, Keys::D, Keys::F, Keys::G,
                         Keys::H, Keys::J, Keys::K, Keys::L};
    for (int c = 0; c < 9; ++c) key(c, 2, 1, row2[c]);

    // Row 3 — Z..M
    const Keys row3[] = {Keys::Z, Keys::X, Keys::C, Keys::V,
                         Keys::B, Keys::N, Keys::M};
    for (int c = 0; c < 7; ++c) key(c, 3, 1, row3[c]);

    // Row 4 — Space bar
    key(0, 4, 5, Keys::Space);

    // Row 5 — arrow cluster
    key(1, 5, 1, Keys::Up);
    key(0, 6, 1, Keys::Left);
    key(1, 6, 1, Keys::Down);
    key(2, 6, 1, Keys::Right);
}

// ---------------------------------------------------------------------------
// Mouse section
// ---------------------------------------------------------------------------

void InputDemo::DrawMouse(int ox, int oy, const MsState& ms)
{
    // Buttons: L M R
    const bool ldown = ms.getLeftButtonProperty()   == BS::Pressed;
    const bool mdown = ms.getMiddleButtonProperty() == BS::Pressed;
    const bool rdown = ms.getRightButtonProperty()  == BS::Pressed;

    DrawRect(ox,       oy,      80, 50, ldown ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 90,  oy,      50, 50, mdown ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 150, oy,      80, 50, rdown ? MOUSE_ON : MOUSE_OFF);

    // Scroll bar — wheel value mapped into a relative indicator
    const float scrollNorm = 0.5f + static_cast<float>(ms.getScrollWheelValueProperty() % 200) / 200.0f;
    DrawBar(ox, oy + 60, 280, 20, std::fmod(scrollNorm, 1.0f), TRIG_BG, MOUSE_ON);

    // XButton1 and XButton2
    const bool x1 = ms.getXButton1Property() == BS::Pressed;
    const bool x2 = ms.getXButton2Property() == BS::Pressed;
    DrawRect(ox + 300, oy,      40, 50, x1 ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 350, oy,      40, 50, x2 ? MOUSE_ON : MOUSE_OFF);

    // Cursor indicator — clamp to section area (show relative within 440x580 window area)
    const int mx = std::max(0, std::min(ms.getXProperty(), 440));
    const int my = std::max(0, std::min(ms.getYProperty(), 580));
    DrawRect(mx - 5, my - 5, 10, 10, CURSOR_CLR);
}

// ---------------------------------------------------------------------------
// GamePad section
// ---------------------------------------------------------------------------

void InputDemo::DrawGamePad(int ox, int oy, const GpState& gp)
{
    const bool connected = gp.getIsConnectedProperty();
    DrawRect(ox, oy, 310, 25, connected ? CONN_YES : CONN_NO);

    if (!connected) return;

    const auto& btns  = gp.getButtonsProperty();
    const auto& dpad  = gp.getDPadProperty();
    const auto& stick = gp.getThumbSticksProperty();
    const auto& trig  = gp.getTriggersProperty();

    auto btn = [&](int x, int y, bool pressed, Color c)
    {
        DrawRect(ox + x, oy + y, 30, 30, pressed ? c : GP_OFF);
    };
    auto dpadBtn = [&](int x, int y, bool pressed)
    {
        DrawRect(ox + x, oy + y, 28, 28, pressed ? DPAD_ON : DPAD_OFF);
    };

    // --- DPad (top-left of gamepad area)
    const int dpx = 10, dpy = 40;
    dpadBtn(dpx + 30, dpy,       dpad.getUpProperty()    == BS::Pressed);
    dpadBtn(dpx,      dpy + 30,  dpad.getLeftProperty()  == BS::Pressed);
    dpadBtn(dpx + 30, dpy + 30,  dpad.getDownProperty()  == BS::Pressed);
    dpadBtn(dpx + 60, dpy + 30,  dpad.getRightProperty() == BS::Pressed);

    // --- ABXY (diamond, right side of DPad)
    const int abx = 160, aby = 40;
    btn(abx + 30, aby,       btns.getYProperty() == BS::Pressed, Color{220, 220, 0,   255});
    btn(abx,      aby + 30,  btns.getXProperty() == BS::Pressed, Color{80,  80,  220, 255});
    btn(abx + 60, aby + 30,  btns.getBProperty() == BS::Pressed, Color{220, 60,  60,  255});
    btn(abx + 30, aby + 60,  btns.getAProperty() == BS::Pressed, Color{60,  200, 60,  255});

    // --- Start / Back
    btn(100, 55, btns.getStartProperty() == BS::Pressed,  Color{180, 180, 180, 255});
    btn(135, 55, btns.getBackProperty()  == BS::Pressed,  Color{140, 140, 140, 255});

    // --- Shoulders
    const bool lb = btns.getLeftShoulderProperty()  == BS::Pressed;
    const bool rb = btns.getRightShoulderProperty() == BS::Pressed;
    DrawRect(ox + 10,  oy + 130, 60, 22, lb ? GP_ON : GP_OFF);
    DrawRect(ox + 240, oy + 130, 60, 22, rb ? GP_ON : GP_OFF);

    // --- Triggers as bars
    DrawBar(ox + 10,  oy + 158, 130, 20, trig.getLeftProperty(),  TRIG_BG, TRIG_FILL);
    DrawBar(ox + 170, oy + 158, 130, 20, trig.getRightProperty(), TRIG_BG, TRIG_FILL);

    // --- Thumbstick clicks
    const bool ls = btns.getLeftStickProperty()  == BS::Pressed;
    const bool rs = btns.getRightStickProperty() == BS::Pressed;
    DrawRect(ox + 10,  oy + 185, 20, 20, ls ? STICK_DOT : GP_OFF);
    DrawRect(ox + 280, oy + 185, 20, 20, rs ? STICK_DOT : GP_OFF);

    // --- Left thumbstick visualizer
    DrawStick(ox + 70,  oy + 270, 55,
              stick.getLeftProperty().X,
              stick.getLeftProperty().Y);

    // --- Right thumbstick visualizer
    DrawStick(ox + 235, oy + 270, 55,
              stick.getRightProperty().X,
              stick.getRightProperty().Y);
}

// ---------------------------------------------------------------------------
// Touch points overlay
// ---------------------------------------------------------------------------

void InputDemo::DrawTouchPoints(const TC& touches)
{
    for (const auto& loc : touches)
    {
        const auto state = loc.getStateProperty();
        if (state == TouchLocationState::Released) continue;

        const auto& pos = loc.getPositionProperty();
        const int tx = static_cast<int>(pos.X);
        const int ty = static_cast<int>(pos.Y);

        // Outer ring
        DrawRect(tx - 18, ty - 18, 36, 36, Color{255, 200, 0, 80});
        // Inner dot
        DrawRect(tx - 8,  ty - 8,  16, 16, TOUCH_CLR);
    }
}

GetTypeNameCPP(InputDemo, "InputDemo")
