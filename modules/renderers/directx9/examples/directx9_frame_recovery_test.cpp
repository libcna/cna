// SPDX-License-Identifier: MS-PL
// FULLSCREEN-001: a lost D3D9 device must skip Draw while ordinary Update ticks
// continue.
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;
using CNA::Internal::Renderers::DirectX9::D3D9NativeOperationEXT;

class FrameRecoveryTest final : public Game {
  std::unique_ptr<GraphicsDeviceManager> manager_;
  static constexpr int kRecoveryDeadline = 90;
  int updates_ = 0, draws_ = 0, result_ = 1, drawsBeforeReset_ = 0,
      stateFailures_ = 0, recoveredAt_ = 0, lostEvents_ = 0, resetEvents_ = 0;
  Microsoft::WRL::ComPtr<IDirect3DSurface9> heldBackBuffer_;
  void Update(GameTime &) override {
    auto &renderer = static_cast<DirectX9Renderer &>(
        getGraphicsDeviceProperty().GetRenderer());
    ++updates_;
    if (updates_ == 1)
      renderer.DebugSimulateContextLoss();
    if (updates_ == 4) {
      if (draws_ != 0 || renderer.CanBeginDrawEXT()) {
        std::printf("[FAIL] a draw went through, or the gate opened, while the device was lost\n");
        Exit();
        return;
      }
      renderer.DebugRestoreContext();
    }
    if (updates_ == 8) {
      if (draws_ == 0 || !renderer.CanBeginDrawEXT()) {
        std::printf("[FAIL] no draw resumed after the context was restored\n");
        Exit();
        return;
      }
      std::printf(
          "[PASS] Update continues, Draw skips loss and resumes after Reset\n");
      const HRESULT hr = renderer.GetDeviceEXT()->GetBackBuffer(
          0, 0, D3DBACKBUFFER_TYPE_MONO, heldBackBuffer_.GetAddressOf());
      if (FAILED(hr)) {
        Exit();
        return;
      }
      drawsBeforeReset_ = draws_;
      auto &device = getGraphicsDeviceProperty();
      device.DeviceLost += [this](System::Object *, const System::EventArgs &) {
        ++lostEvents_;
      };
      device.DeviceReset += [this](System::Object *, const System::EventArgs &) {
        ++resetEvents_;
      };
      manager_->ToggleFullScreen();
      // Check the same Update, before TryBeginDraw can discover the native loss.
      // A later-frame check misses illegal viewport/present calls after failed Reset.
      if (renderer.CanBeginDrawEXT() || !renderer.IsDeviceLostEXT() ||
          lostEvents_ != 1 || resetEvents_ != 0) {
        std::printf("[FAIL] immediately after failed Reset: gate=%d, lost=%d, "
                    "Lost events=%d, Reset events=%d\n",
                    renderer.CanBeginDrawEXT(), renderer.IsDeviceLostEXT(),
                    lostEvents_, resetEvents_);
        Exit();
        return;
      }
      const auto gets = renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::GetRenderTarget);
      const auto sets = renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::SetViewport);
      bool viewportRefused = false, clearRefused = false;
      try {
        renderer.SetViewport(0, 0, 1, 1, 0, 1);
      } catch (const Graphics::DeviceLostException &) {
        viewportRefused = true;
      }
      try {
        device.Clear(Color::White);
      } catch (const Graphics::DeviceLostException &) {
        clearRefused = true;
      }
      if (!viewportRefused || !clearRefused ||
          gets != renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::GetRenderTarget) ||
          sets != renderer.GetNativeCallCountEXT(D3D9NativeOperationEXT::SetViewport)) {
        std::printf("[FAIL] a lost device reached native viewport/clear work\n");
        Exit();
        return;
      }
      std::printf("[PASS] failed Reset closes the gate immediately, defers Reset events "
                  "and refuses viewport/clear before native calls\n");
    }
    if (updates_ == 12) {
      std::printf("[diagnostic] held buffer draws=%d, previous=%d, gate=%d, "
                  "native=0x%08lx\n",
                  draws_, drawsBeforeReset_, renderer.CanBeginDrawEXT(),
                  static_cast<unsigned long>(
                      renderer.GetDeviceEXT()->TestCooperativeLevel()));
      if (draws_ != drawsBeforeReset_ || lostEvents_ != 1 || resetEvents_ != 0) {
        std::printf("[FAIL] a draw went through while the held back buffer kept Reset failing\n");
        Exit();
        return;
      }
      heldBackBuffer_.Reset();
    }
    // How many frames Wine needs before the retried Reset succeeds varies with load, so the
    // recovery is waited for inside a window rather than asserted at one fixed update.
    if (updates_ > 12 && recoveredAt_ == 0) {
      if (draws_ > drawsBeforeReset_ && renderer.CanBeginDrawEXT()) {
        recoveredAt_ = updates_;
        std::printf("[diagnostic] released buffer draws=%d, previous=%d, "
                    "gate=%d, native=0x%08lx, recovered at update %d\n",
                    draws_, drawsBeforeReset_, renderer.CanBeginDrawEXT(),
                    static_cast<unsigned long>(
                        renderer.GetDeviceEXT()->TestCooperativeLevel()),
                    updates_);
        manager_->ToggleFullScreen();
      } else if (updates_ >= kRecoveryDeadline) {
        std::printf("[FAIL] the device did not recover within %d updates after the back "
                    "buffer was released (draws=%d, previous=%d, gate=%d, native=0x%08lx)\n",
                    kRecoveryDeadline, draws_, drawsBeforeReset_,
                    renderer.CanBeginDrawEXT(),
                    static_cast<unsigned long>(
                        renderer.GetDeviceEXT()->TestCooperativeLevel()));
        Exit();
        return;
      }
    }
    if (recoveredAt_ != 0 && updates_ == recoveredAt_ + 8) {
      result_ = (draws_ > drawsBeforeReset_ && renderer.CanBeginDrawEXT() &&
                 stateFailures_ == 0 && lostEvents_ == 1 && resetEvents_ == 2)
                    ? 0
                    : 1;
      std::printf("[%s] failed fullscreen Reset waits for a held backbuffer, "
                  "then recovers\n",
                  result_ == 0 ? "PASS" : "FAIL");
      Exit();
    }
  }
  void Draw(const GameTime &) override {
    ++draws_;
    auto &dev = getGraphicsDeviceProperty();
    dev.setBlendStateProperty(
        Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend);
    dev.setDepthStencilStateProperty(
        Microsoft::Xna::Framework::Graphics::DepthStencilState::None);
    auto &renderer = static_cast<DirectX9Renderer &>(dev.GetRenderer());
    DWORD blend = 0, depth = 1;
    renderer.GetDeviceEXT()->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZENABLE, &depth);
    if (blend != TRUE || depth != D3DZB_FALSE)
      ++stateFailures_;
    dev.Clear(Color::CornflowerBlue);
  }

public:
  FrameRecoveryTest()
      : manager_(std::make_unique<GraphicsDeviceManager>(this)) {}
  int Result() const { return result_; }
};
int main() {
  FrameRecoveryTest game;
  game.Run();
  return game.Result();
}
