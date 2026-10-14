// SPDX-License-Identifier: MS-PL
// FULLSCREEN-001: a lost D3D9 device must skip Draw while ordinary Update ticks
// continue.
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;

class FrameRecoveryTest final : public Game {
  std::unique_ptr<GraphicsDeviceManager> manager_;
  static constexpr int kRecoveryDeadline = 90;
  int updates_ = 0, draws_ = 0, result_ = 1, drawsBeforeReset_ = 0,
      stateFailures_ = 0, recoveredAt_ = 0;
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
      manager_->ToggleFullScreen();
    }
    if (updates_ == 12) {
      std::printf("[diagnostic] held buffer draws=%d, previous=%d, gate=%d, "
                  "native=0x%08lx\n",
                  draws_, drawsBeforeReset_, renderer.CanBeginDrawEXT(),
                  static_cast<unsigned long>(
                      renderer.GetDeviceEXT()->TestCooperativeLevel()));
      if (draws_ != drawsBeforeReset_) {
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
                 stateFailures_ == 0)
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
