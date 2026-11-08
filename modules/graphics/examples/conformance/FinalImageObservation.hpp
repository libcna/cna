// SPDX-License-Identifier: MS-PL
#pragma once

#include "PresentedWindowCapture.hpp"
#include "PresentedWindowCaptureWin32.hpp"

#include <memory>

namespace CNA::Examples::Conformance {
/**
 * @brief Observes a presented native client separately from logical backbuffer readback.
 *
 * The caller redirects before presenting its first test frame, then waits for a whole frame's
 * checks together. Capture retries only accommodate asynchronous presentation; every check is
 * subsequently evaluated against the same final snapshot, including on timeout. No renderer
 * readback, rescaling or color conversion based on the expected image is involved.
 */
class FinalImageObservation {
public:
  /**
   * @brief Attaches the native observer without owning the window.
   * @param handle The test's native window on its private display.
   * @param width Physical client width.
   * @param height Physical client height.
   */
  FinalImageObservation(const Platform::NativeWindowHandle &handle, int width,
                        int height) {
#if defined(CNA_GAP08_X11_CAPTURE)
    capture_ = std::make_unique<Testing::PresentedWindow>(
        handle, width, height, false, Testing::PresentedCaptureMethod::Auto);
#elif defined(_WIN32)
    capture_ = std::make_unique<Testing::PresentedWin32Window>(handle, width, height);
#else
    throw std::runtime_error("final-image observation requires native X11 or Win32 capture");
#endif
  }

  /**
   * @brief Waits for a presented frame, retaining the last actual snapshot on timeout.
   * @param platform The game's platform, used to pump events while waiting.
   * @param match Predicate for all checks on this frame.
   */
  template <class Match> void WaitFor(Platform::IPlatform &platform, Match match) {
#if defined(CNA_GAP08_X11_CAPTURE) || defined(_WIN32)
    const bool matched = capture_->WaitFor(platform, [&](const auto &rgb) {
      pixels_ = rgb;
      return match(pixels_);
    });
#if defined(CNA_GAP08_X11_CAPTURE)
    std::printf("[final-image] matched=%s %s\n", matched ? "yes" : "no",
                capture_->Diagnostics().c_str());
#else
    std::printf("[final-image] matched=%s native Win32 client\n", matched ? "yes" : "no");
#endif
#endif
  }

  /**
   * @brief Returns the last captured physical client pixels, in top-left row order.
   * @return Packed 0xRRGGBB pixels from the native observer.
   */
  const std::vector<std::uint32_t> &Pixels() const { return pixels_; }

private:
#if defined(CNA_GAP08_X11_CAPTURE)
  std::unique_ptr<Testing::PresentedWindow> capture_;
#elif defined(_WIN32)
  std::unique_ptr<Testing::PresentedWin32Window> capture_;
#endif
  std::vector<std::uint32_t> pixels_;
};
} // namespace CNA::Examples::Conformance
