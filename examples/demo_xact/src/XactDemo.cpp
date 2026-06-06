#include "XactDemo.hpp"
#include "XactFileGen.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Audio;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;

static constexpr int kW = 800;
static constexpr int kH = 600;

// Cue names and frequencies (C4 major triad + octave)
static constexpr float kFreqs[4] = { 261.6f, 329.6f, 392.0f, 523.3f };
static constexpr const char* kCueNames[4] = { "Tone261", "Tone330", "Tone392", "Tone523" };
// Categories: 0=Music (cues 0-1), 1=SFX (cues 2-3)
static constexpr uint8_t kCueCat[4] = { 0, 0, 1, 1 };

// ── Constructor ───────────────────────────────────────────────────────────────

XactDemo::XactDemo()
{
    setTargetElapsedTimeProperty(
        System::TimeSpan(System::TimeSpan::FromMilliseconds(1000.0 / 60.0).getTicksProperty()));
    getWindowProperty().setTitleProperty("CNA XACT Demo");
}

XactDemo::~XactDemo()
{
    soundBank_.reset();
    waveBank_.reset();
    audioEngine_.reset();
    delete spriteBatch_;
}

// ── XACT file generation ──────────────────────────────────────────────────────

void XactDemo::GenerateXactFiles(const std::string& audioDir)
{
    using namespace XactFileGen;

    std::filesystem::create_directories(audioDir);

    // ── XWB: 4 sine waves (0.8 s each) ──
    std::vector<std::vector<uint8_t>> waves;
    for (int i = 0; i < 4; ++i)
        waves.push_back(MakeSineWave(kFreqs[i], 0.8f));

    SaveFile(audioDir + "/Waves.xwb", MakeXwb("Waves", waves));

    // ── XGS: two categories (Music, SFX) + SpeedOfSound variable ──
    SaveFile(audioDir + "/Demo.xgs", MakeXgs());

    // ── XSB: one cue per tone ──
    std::vector<CueDef> cueDefs;
    for (uint16_t i = 0; i < 4; ++i)
        cueDefs.push_back({ kCueNames[i], i, kCueCat[i] });

    SaveFile(audioDir + "/Sounds.xsb",
             MakeXsb("Sounds", "Waves", cueDefs));
}

// ── LoadContent ───────────────────────────────────────────────────────────────

void XactDemo::LoadContent()
{
    spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());

    pixel_ = Texture2D(getGraphicsDeviceProperty(), 1, 1);
    Color white(255, 255, 255, 255);
    pixel_.SetData(&white, 1);

    // Find Content/Audio relative to executable
    std::string audioDir = "Content/Audio";
    GenerateXactFiles(audioDir);

    try
    {
        audioEngine_ = std::make_unique<AudioEngine>(audioDir + "/Demo.xgs");
        waveBank_    = std::make_unique<WaveBank>(audioEngine_.get(), audioDir + "/Waves.xwb");
        soundBank_   = std::make_unique<SoundBank>(audioEngine_.get(), audioDir + "/Sounds.xsb");

        musicCategory_.emplace(audioEngine_->GetCategory("Music"));
        sfxCategory_.emplace(audioEngine_->GetCategory("SFX"));

        speedOfSound_ = audioEngine_->GetGlobalVariable("SpeedOfSound");
        status_ = "XACT loaded — press 1-4 to play cues";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[XactDemo] Load error: " << ex.what() << "\n";
        status_ = "Load failed — check console";
    }

    prevKeys_ = Keyboard::GetState();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool XactDemo::WasJustPressed(Keys key) const
{
    return Keyboard::GetState().IsKeyDown(key) && prevKeys_.IsKeyUp(key);
}

void XactDemo::DrawRect(int x, int y, int w, int h, Color color)
{
    spriteBatch_->Draw(pixel_, Rectangle(x, y, w, h), Rectangle(0, 0, 1, 1), color);
}

void XactDemo::DrawBar(int x, int y, int totalW, int h, float frac,
                        Color fill, Color bg)
{
    DrawRect(x, y, totalW, h, bg);
    int filled = static_cast<int>(std::clamp(frac, 0.0f, 1.0f) * static_cast<float>(totalW));
    if (filled > 0) DrawRect(x, y, filled, h, fill);
}

// ── Update ────────────────────────────────────────────────────────────────────

void XactDemo::Update(GameTime& /*gameTime*/)
{
    KeyboardState cur = Keyboard::GetState();

    if (cur.IsKeyDown(Keys::Escape))
        Exit();

    // ── Play cues: keys 1-4 ──
    const Keys cueKeys[4] = { Keys::D1, Keys::D2, Keys::D3, Keys::D4 };
    for (int i = 0; i < 4; ++i)
    {
        if (WasJustPressed(cueKeys[i]) && soundBank_)
        {
            soundBank_->PlayCue(kCueNames[i]);
            cuePlayed_[i]    = true;
            cuePlayFrames_[i] = 30; // flash for 30 frames
            status_ = std::string("Played cue: ") + kCueNames[i];
        }
        if (cuePlayFrames_[i] > 0)
            --cuePlayFrames_[i];
        else
            cuePlayed_[i] = false;
    }

    // ── Music category controls ──
    if (WasJustPressed(Keys::M) && musicCategory_)
    {
        musicPaused_ = !musicPaused_;
        if (musicPaused_) musicCategory_->Pause();
        else              musicCategory_->Resume();
        status_ = musicPaused_ ? "Music PAUSED" : "Music RESUMED";
    }

    if (WasJustPressed(Keys::S) && sfxCategory_)
    {
        sfxPaused_ = !sfxPaused_;
        if (sfxPaused_) sfxCategory_->Pause();
        else            sfxCategory_->Resume();
        status_ = sfxPaused_ ? "SFX PAUSED" : "SFX RESUMED";
    }

    // ── Music volume: Left/Right ──
    if (cur.IsKeyDown(Keys::Left) && musicCategory_)
    {
        musicVolume_ = std::max(0.0f, musicVolume_ - 0.01f);
        musicCategory_->SetVolume(musicVolume_);
    }
    if (cur.IsKeyDown(Keys::Right) && musicCategory_)
    {
        musicVolume_ = std::min(1.0f, musicVolume_ + 0.01f);
        musicCategory_->SetVolume(musicVolume_);
    }

    // ── SFX volume: Down/Up ──
    if (cur.IsKeyDown(Keys::Down) && sfxCategory_)
    {
        sfxVolume_ = std::max(0.0f, sfxVolume_ - 0.01f);
        sfxCategory_->SetVolume(sfxVolume_);
    }
    if (cur.IsKeyDown(Keys::Up) && sfxCategory_)
    {
        sfxVolume_ = std::min(1.0f, sfxVolume_ + 0.01f);
        sfxCategory_->SetVolume(sfxVolume_);
    }

    // ── Global variable SpeedOfSound: Z/X ──
    if (cur.IsKeyDown(Keys::Z) && audioEngine_)
    {
        speedOfSound_ = std::max(0.0f, speedOfSound_ - 1.0f);
        audioEngine_->SetGlobalVariable("SpeedOfSound", speedOfSound_);
    }
    if (cur.IsKeyDown(Keys::X) && audioEngine_)
    {
        speedOfSound_ = std::min(1000.0f, speedOfSound_ + 1.0f);
        audioEngine_->SetGlobalVariable("SpeedOfSound", speedOfSound_);
    }

    // ── Stop all cues in Music category: Q ──
    if (WasJustPressed(Keys::Q) && musicCategory_)
    {
        musicCategory_->Stop(AudioStopOptions::Immediate);
        status_ = "Music category STOPPED";
    }

    // ── XACT engine update ──
    if (audioEngine_) audioEngine_->Update();

    prevKeys_ = cur;
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void XactDemo::Draw(const GameTime& /*gameTime*/)
{
    getGraphicsDeviceProperty().Clear(0.06f, 0.06f, 0.10f, 1.0f);
    spriteBatch_->Begin();

    // Background
    DrawRect(10, 10, kW - 20, kH - 20, Color(18, 18, 28, 230));

    // Title bar
    DrawRect(10, 10, kW - 20, 34, Color(40, 50, 110, 255));
    DrawRect(12, 12, kW - 24, 30, Color(60, 80, 170, 255));

    int y = 58;

    // ── Section: Cues ────────────────────────────────────────────────────────
    DrawRect(30, y, kW - 60, 20, Color(30, 90, 30, 255));
    y += 24;

    // 4 cue buttons in a row
    const char* catLabel[4] = {"Mus","Mus","SFX","SFX"};
    Color catColOn[2]  = { Color(80,220,80,255), Color(80,180,255,255) };
    Color catColOff[2] = { Color(30,80,30,255),  Color(30,60,100,255)  };
    for (int i = 0; i < 4; ++i)
    {
        int bx = 30 + i * 185;
        bool active = cuePlayed_[i];
        uint8_t cat = kCueCat[i];
        DrawRect(bx, y, 175, 48, active ? catColOn[cat] : catColOff[cat]);
        DrawRect(bx + 2, y + 2, 171, 44, active ? Color(120,255,120,255) : Color(40,50,40,255));
        // frequency indicator bar
        float frac = (kFreqs[i] - 200.0f) / (600.0f - 200.0f);
        DrawBar(bx + 4, y + 30, 163, 10, frac,
                active ? Color(255,255,80,255) : Color(100,140,100,255),
                Color(20,30,20,255));
    }
    y += 56;

    // Key legend for cues
    y += 6;
    // row of category labels
    for (int i = 0; i < 4; ++i)
    {
        int bx = 30 + i * 185;
        DrawRect(bx, y, 40, 14, Color(30,30,30,200));
        (void)catLabel; // drawn as colored rect only
    }
    y += 20;

    // ── Section: Categories ───────────────────────────────────────────────────
    DrawRect(30, y, kW - 60, 20, Color(90, 50, 10, 255));
    y += 24;

    // Music row
    Color musStateCol = musicPaused_ ? Color(220,180,0,255) : Color(0,200,80,255);
    DrawRect(30, y, 14, 14, musStateCol);
    DrawBar(50, y, 300, 14, musicVolume_, Color(80,200,80,255), Color(30,60,30,255));
    y += 20;

    // SFX row
    Color sfxStateCol = sfxPaused_ ? Color(220,180,0,255) : Color(80,160,255,255);
    DrawRect(30, y, 14, 14, sfxStateCol);
    DrawBar(50, y, 300, 14, sfxVolume_, Color(80,160,255,255), Color(20,40,80,255));
    y += 28;

    // ── Section: Global variable SpeedOfSound ────────────────────────────────
    DrawRect(30, y, kW - 60, 20, Color(80, 20, 80, 255));
    y += 24;

    DrawBar(30, y, 450, 14,
            speedOfSound_ / 1000.0f,
            Color(200,100,220,255),
            Color(50,20,60,255));
    y += 20;

    // mini bar ticks (100/200/.../1000)
    for (int tick = 1; tick <= 10; ++tick)
    {
        int tx = 30 + tick * 45;
        DrawRect(tx, y, 2, 6, Color(100,60,110,255));
    }
    y += 16;

    // ── Status bar ────────────────────────────────────────────────────────────
    y = kH - 50;
    DrawRect(30, y, kW - 60, 26, Color(20,20,40,200));

    // ── Key legend ────────────────────────────────────────────────────────────
    y = kH - 20;
    // small colored legend indicators (colored rect = keyboard key)
    const int keys[7][2] = {{30,y},{90,y},{155,y},{225,y},{295,y},{370,y},{450,y}};
    const Color keyCol[7] = {
        Color(80,200,80,255),  // 1-4
        Color(200,200,0,255),  // M
        Color(100,180,255,255),// S
        Color(150,80,150,255), // L/R arrows
        Color(80,160,255,255), // Up/Dn arrows
        Color(200,100,220,255),// Z/X
        Color(200,80,80,255),  // Q
    };
    for (int i = 0; i < 7; ++i)
        DrawRect(keys[i][0], keys[i][1], 55, 10, keyCol[i]);

    spriteBatch_->End();
}

GetTypeNameCPP(XactDemo, "XactDemo")
