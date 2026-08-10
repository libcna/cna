#include "SoundDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Audio;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;

static constexpr int   kScreenW = 800;
static constexpr int   kScreenH = 600;
static constexpr float kPi      = 3.14159265358979f;

SoundDemo::SoundDemo()
    : click_(""), tone_(""), beep_("")
{
    System::TimeSpan targetElapsedTime =
        System::TimeSpan(System::TimeSpan::FromMilliseconds(1000.0 / 60.0).getTicksProperty());
    Game::setTargetElapsedTimeProperty(targetElapsedTime);
    Game::getWindowProperty().setTitleProperty("CNA Audio Demo");
}

SoundDemo::~SoundDemo()
{
    delete spriteBatch_;
    spriteBatch_ = nullptr;
}

void SoundDemo::LoadContent()
{
    if (!spriteBatch_) {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
    }

    // 1x1 white texture for drawing solid rectangles
    pixel_ = Texture2D(getGraphicsDeviceProperty(), 1, 1);
    Color white(255, 255, 255, 255);
    pixel_.SetData(&white, 1);

    try {
        click_ = getContentProperty().Load<SoundEffect>("sounds/click.wav");
    } catch (const std::exception& ex) {
        std::cerr << "[SoundDemo] click.wav load failed: " << ex.what() << "\n";
    }
    try {
        tone_ = getContentProperty().Load<SoundEffect>("sounds/tone.wav");
    } catch (const std::exception& ex) {
        std::cerr << "[SoundDemo] tone.wav load failed: " << ex.what() << "\n";
    }
    try {
        beep_ = getContentProperty().Load<SoundEffect>("sounds/beep.wav");
    } catch (const std::exception& ex) {
        std::cerr << "[SoundDemo] beep.wav load failed: " << ex.what() << "\n";
    }

    // Looping SoundEffectInstance from tone_
    toneInstance_ = std::make_unique<SoundEffectInstance>(tone_.CreateInstance());
    toneInstance_->setIsLoopedProperty(true);
    toneInstance_->setVolumeProperty(volume_);
    toneInstance_->setPitchProperty(pitch_);
    toneInstance_->setPanProperty(pan_);

    // DynamicSoundEffectInstance — 44100 Hz stereo
    dynamicInstance_ = std::make_unique<DynamicSoundEffectInstance>(44100, AudioChannels::Stereo);
    dynamicInstance_->BufferNeeded +=
        [this](System::Object* sender, const System::EventArgs& e) {
            OnDynamicBufferNeeded(sender, e);
        };

    // 3D audio: listener at origin facing forward
    listener_.setPositionProperty(Vector3(0.0f, 0.0f, 0.0f));
    listener_.setForwardProperty(Vector3(0.0f, 0.0f, -1.0f));
    listener_.setUpProperty(Vector3(0.0f, 1.0f, 0.0f));

    emitter_.setPositionProperty(Vector3(emitterX_, 0.0f, -1.0f));
    emitter_.setForwardProperty(Vector3(0.0f, 0.0f, -1.0f));
    emitter_.setUpProperty(Vector3(0.0f, 1.0f, 0.0f));

    prevKeys_ = Keyboard::GetState();
    statusMessage_ = "Ready";
}

// --- helpers ---

bool SoundDemo::WasJustPressed(Keys key) const
{
    KeyboardState cur = Keyboard::GetState();
    return cur.IsKeyDown(key) && prevKeys_.IsKeyUp(key);
}

void SoundDemo::GenerateSineBuffer(std::vector<SharpRuntime::bytecs>& buffer,
                                    float frequency, float durationSeconds,
                                    int sampleRate, int channels)
{
    int totalFrames = static_cast<int>(sampleRate * durationSeconds);
    // 16-bit PCM: 2 bytes per sample per channel
    buffer.resize(static_cast<size_t>(totalFrames * channels * 2));

    for (int i = 0; i < totalFrames; ++i) {
        float t   = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = std::min(1.0f, t * 10.0f) * std::min(1.0f, (durationSeconds - t) * 10.0f);
        auto  val = static_cast<int16_t>(env * 0.4f * 32767.0f * std::sin(2.0f * kPi * frequency * t));

        for (int ch = 0; ch < channels; ++ch) {
            size_t idx = static_cast<size_t>((i * channels + ch) * 2);
            buffer[idx]     = static_cast<SharpRuntime::bytecs>(val & 0xFF);
            buffer[idx + 1] = static_cast<SharpRuntime::bytecs>((val >> 8) & 0xFF);
        }
    }
}

void SoundDemo::OnDynamicBufferNeeded(System::Object* /*sender*/, const System::EventArgs& /*e*/)
{
    if (!dynamicPlaying_) {
        return;
    }
    std::vector<SharpRuntime::bytecs> buf;
    GenerateSineBuffer(buf, dynamicFreq_, 0.25f, 44100, 2);
    dynamicInstance_->SubmitBuffer(buf);
}

// --- Update ---

void SoundDemo::Update(GameTime& gameTime)
{
    (void)gameTime;

    KeyboardState cur = Keyboard::GetState();

    if (cur.IsKeyDown(Keys::Escape)) {
        Exit();
    }

    // --- [1] Fire-and-forget SoundEffect play ---
    if (WasJustPressed(Keys::D1)) {
        click_.Play();
        statusMessage_ = "[1] Click played (fire-and-forget)";
    }

    // --- [2] Play/Pause looping tone instance ---
    if (WasJustPressed(Keys::D2)) {
        auto state = toneInstance_->getStateProperty();
        if (state == SoundState::Playing) {
            toneInstance_->Pause();
            statusMessage_ = "[2] Looping tone PAUSED";
        } else if (state == SoundState::Paused) {
            toneInstance_->Resume();
            statusMessage_ = "[2] Looping tone RESUMED";
        } else {
            toneInstance_->Play();
            statusMessage_ = "[2] Looping tone PLAYING";
        }
    }

    // --- [3] Stop looping tone ---
    if (WasJustPressed(Keys::D3)) {
        toneInstance_->Stop();
        statusMessage_ = "[3] Looping tone STOPPED";
    }

    // --- [4] Play beep with current volume/pitch/pan ---
    if (WasJustPressed(Keys::D4)) {
        beep_.Play(volume_, pitch_, pan_);
        statusMessage_ = "[4] Beep: vol=" + std::to_string(volume_).substr(0, 4)
                       + " pit=" + std::to_string(pitch_).substr(0, 5)
                       + " pan=" + std::to_string(pan_).substr(0, 5);
    }

    // --- [5] Apply3D to beep instance ---
    if (WasJustPressed(Keys::D5)) {
        emitter_.setPositionProperty(Vector3(emitterX_, 0.0f, -1.0f));
        SoundEffectInstance inst = beep_.CreateInstance();
        inst.Apply3D(listener_, emitter_);
        inst.setVolumeProperty(volume_);
        inst.Play();
        statusMessage_ = "[5] 3D beep: emitterX=" + std::to_string(emitterX_).substr(0, 5);
    }

    // --- [6] DynamicSoundEffectInstance toggle ---
    if (WasJustPressed(Keys::D6)) {
        if (!dynamicPlaying_) {
            dynamicPlaying_ = true;
            std::vector<SharpRuntime::bytecs> buf;
            GenerateSineBuffer(buf, dynamicFreq_, 0.25f, 44100, 2);
            dynamicInstance_->SubmitBuffer(buf);
            dynamicInstance_->Play();
            statusMessage_ = "[6] DynamicSEI started (" + std::to_string(static_cast<int>(dynamicFreq_)) + " Hz)";
        } else {
            dynamicPlaying_ = false;
            dynamicInstance_->Stop();
            statusMessage_ = "[6] DynamicSEI stopped";
        }
    }

    // --- Volume: Up/Down arrows ---
    if (cur.IsKeyDown(Keys::Up)) {
        volume_ = std::min(1.0f, volume_ + 0.01f);
        toneInstance_->setVolumeProperty(volume_);
    }
    if (cur.IsKeyDown(Keys::Down)) {
        volume_ = std::max(0.0f, volume_ - 0.01f);
        toneInstance_->setVolumeProperty(volume_);
    }

    // --- Pan: Left/Right arrows ---
    if (cur.IsKeyDown(Keys::Left)) {
        pan_ = std::max(-1.0f, pan_ - 0.01f);
        toneInstance_->setPanProperty(pan_);
    }
    if (cur.IsKeyDown(Keys::Right)) {
        pan_ = std::min(1.0f, pan_ + 0.01f);
        toneInstance_->setPanProperty(pan_);
    }

    // --- Pitch: W/S keys ---
    if (WasJustPressed(Keys::W)) {
        pitch_ = std::min(1.0f, pitch_ + 0.1f);
        toneInstance_->setPitchProperty(pitch_);
        statusMessage_ = "Pitch: " + std::to_string(pitch_).substr(0, 4);
    }
    if (WasJustPressed(Keys::S)) {
        pitch_ = std::max(-1.0f, pitch_ - 0.1f);
        toneInstance_->setPitchProperty(pitch_);
        statusMessage_ = "Pitch: " + std::to_string(pitch_).substr(0, 4);
    }

    // --- 3D emitter: A/D keys ---
    if (cur.IsKeyDown(Keys::A)) {
        emitterX_ = std::max(-10.0f, emitterX_ - 0.05f);
    }
    if (cur.IsKeyDown(Keys::D)) {
        emitterX_ = std::min(10.0f, emitterX_ + 0.05f);
    }

    // --- Dynamic freq: Z/X keys ---
    if (WasJustPressed(Keys::Z)) {
        dynamicFreq_ = std::max(110.0f, dynamicFreq_ - 55.0f);
        statusMessage_ = "Dynamic freq: " + std::to_string(static_cast<int>(dynamicFreq_)) + " Hz";
    }
    if (WasJustPressed(Keys::X)) {
        dynamicFreq_ = std::min(880.0f, dynamicFreq_ + 55.0f);
        statusMessage_ = "Dynamic freq: " + std::to_string(static_cast<int>(dynamicFreq_)) + " Hz";
    }

    // Update DynamicSEI pump
    if (dynamicPlaying_) {
        dynamicInstance_->Update();
    }

    prevKeys_ = cur;
}

// --- Drawing helpers ---

void SoundDemo::DrawRect(int x, int y, int w, int h, Color color)
{
    Rectangle dest(x, y, w, h);
    Rectangle src(0, 0, 1, 1);
    spriteBatch_->Draw(pixel_, dest, src, color);
}

void SoundDemo::DrawBar(int x, int y, int totalW, int h, float fraction,
                         Color fillColor, Color bgColor)
{
    DrawRect(x, y, totalW, h, bgColor);
    int filled = static_cast<int>(fraction * static_cast<float>(totalW));
    if (filled > 0) {
        DrawRect(x, y, filled, h, fillColor);
    }
}

// --- Draw ---

void SoundDemo::Draw(const GameTime& /*gameTime*/)
{
    getGraphicsDeviceProperty().Clear(0.08f, 0.08f, 0.12f, 1.0f);

    spriteBatch_->Begin();

    // ── Background panel ───────────────────────────────────────────────
    DrawRect(10, 10, kScreenW - 20, kScreenH - 20, Color(20, 20, 35, 220));

    // ── Title bar ──────────────────────────────────────────────────────
    DrawRect(10, 10, kScreenW - 20, 36, Color(50, 60, 120, 255));
    DrawRect(12, 12, kScreenW - 24, 32, Color(70, 90, 180, 255));

    // ── Section: SoundEffect (left column) ────────────────────────────
    int col1 = 30, col2 = 420;
    int row = 60;

    // Section label strip
    DrawRect(col1, row, 370, 20, Color(40, 100, 40, 255));
    row += 24;

    // [1] Click indicator
    bool c1active = false;
    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, c1active ? Color(0, 220, 80, 255) : Color(80, 80, 80, 255));
    row += 22;

    // [2] Looping tone state
    auto toneState = toneInstance_ ? toneInstance_->getStateProperty() : SoundState::Stopped;
    Color stateColor = (toneState == SoundState::Playing)  ? Color(0, 220, 80, 255)
                     : (toneState == SoundState::Paused)   ? Color(220, 180, 0, 255)
                                                           : Color(180, 50, 50, 255);
    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, stateColor);
    row += 22;

    // [3] Stop row
    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, Color(80, 80, 80, 255));
    row += 22;

    // [4] Beep with v/p/pan
    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, Color(80, 80, 80, 255));
    row += 30;

    // ── Section: Parameters ────────────────────────────────────────────
    DrawRect(col1, row, 370, 20, Color(100, 60, 10, 255));
    row += 24;

    // Volume bar
    DrawRect(col1, row, 60, 14, Color(30, 30, 30, 255));
    DrawBar(col1 + 64, row, 280, 14, volume_, Color(80, 200, 80, 255), Color(40, 60, 40, 255));
    row += 18;

    // Pitch bar (centred: -1..+1 → left half / right half)
    DrawRect(col1, row, 60, 14, Color(30, 30, 30, 255));
    {
        float pitchFrac = (pitch_ + 1.0f) * 0.5f;
        DrawBar(col1 + 64, row, 280, 14, pitchFrac, Color(80, 150, 220, 255), Color(40, 50, 80, 255));
        // Centre line
        DrawRect(col1 + 64 + 140, row, 2, 14, Color(200, 200, 200, 180));
    }
    row += 18;

    // Pan bar (centred: -1..+1)
    DrawRect(col1, row, 60, 14, Color(30, 30, 30, 255));
    {
        float panFrac = (pan_ + 1.0f) * 0.5f;
        DrawBar(col1 + 64, row, 280, 14, panFrac, Color(220, 150, 80, 255), Color(80, 50, 40, 255));
        DrawRect(col1 + 64 + 140, row, 2, 14, Color(200, 200, 200, 180));
    }
    row += 30;

    // ── Section: 3D Audio ──────────────────────────────────────────────
    DrawRect(col1, row, 370, 20, Color(80, 20, 80, 255));
    row += 24;

    // Emitter position visualiser
    {
        int barX = col1 + 64, barW = 280, barH = 20;
        DrawRect(barX, row, barW, barH, Color(40, 20, 50, 255));
        // Emitter dot at emitterX_ [-10..10]
        float emFrac = (emitterX_ + 10.0f) / 20.0f;
        int dotX = barX + static_cast<int>(emFrac * static_cast<float>(barW - 10));
        DrawRect(dotX, row + 4, 10, 12, Color(220, 80, 220, 255));
        // Listener at centre
        DrawRect(barX + barW / 2 - 3, row + 6, 6, 8, Color(80, 200, 220, 255));
    }
    row += 26;

    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, Color(80, 80, 80, 255));
    row += 30;

    // ── Section: DynamicSoundEffectInstance ───────────────────────────
    DrawRect(col1, row, 370, 20, Color(20, 60, 100, 255));
    row += 24;

    // Playing indicator
    Color dynColor = dynamicPlaying_ ? Color(0, 200, 255, 255) : Color(60, 80, 100, 255);
    DrawRect(col1, row, 340, 18, Color(30, 30, 30, 200));
    DrawRect(col1, row, 18, 18, dynColor);
    row += 22;

    // Frequency indicator
    float freqFrac = (dynamicFreq_ - 110.0f) / (880.0f - 110.0f);
    DrawBar(col1 + 64, row, 280, 14, freqFrac, Color(0, 180, 255, 255), Color(20, 40, 60, 255));
    row += 22;

    // ── Key legend (right column) ──────────────────────────────────────
    int legendY = 60;
    DrawRect(col2, legendY, 360, 20, Color(20, 50, 80, 255));
    legendY += 24;

    struct LegendEntry { Color col; };
    auto addLegendRow = [&](Color col) {
        DrawRect(col2, legendY, 350, 18, Color(20, 20, 30, 180));
        DrawRect(col2, legendY, 8, 18, col);
        legendY += 22;
    };

    addLegendRow(Color(0, 220, 80, 255));   // [1] click
    addLegendRow(Color(0, 220, 80, 255));   // [2] play/pause
    addLegendRow(Color(180, 50, 50, 255));  // [3] stop
    addLegendRow(Color(200, 180, 0, 255));  // [4] beep v/p/pan
    addLegendRow(Color(220, 80, 220, 255)); // [5] 3D beep
    legendY += 8;
    addLegendRow(Color(80, 200, 80, 255));  // Up vol+
    addLegendRow(Color(80, 200, 80, 255));  // Down vol-
    addLegendRow(Color(80, 150, 220, 255)); // W pitch+
    addLegendRow(Color(80, 150, 220, 255)); // S pitch-
    addLegendRow(Color(220, 150, 80, 255)); // Left pan-
    addLegendRow(Color(220, 150, 80, 255)); // Right pan+
    legendY += 8;
    addLegendRow(Color(220, 80, 220, 255)); // A emitter left
    addLegendRow(Color(220, 80, 220, 255)); // D emitter right
    addLegendRow(Color(0, 200, 255, 255));  // [6] dynamic toggle
    addLegendRow(Color(0, 200, 255, 255));  // Z freq-
    addLegendRow(Color(0, 200, 255, 255));  // X freq+
    legendY += 8;
    addLegendRow(Color(180, 50, 50, 255));  // Esc exit

    // ── Status bar ─────────────────────────────────────────────────────
    DrawRect(10, kScreenH - 34, kScreenW - 20, 24, Color(30, 30, 50, 220));

    spriteBatch_->End();

    // Also show status in window title for extra feedback
    getWindowProperty().setTitleProperty("CNA Audio Demo | " + statusMessage_
        + " | Vol:" + std::to_string(volume_).substr(0,4)
        + " Pit:" + std::to_string(pitch_).substr(0,5)
        + " Pan:" + std::to_string(pan_).substr(0,5));
}

GetTypeNameCPP(SoundDemo, "SoundDemo")
