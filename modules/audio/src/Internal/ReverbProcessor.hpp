// SPDX-License-Identifier: MS-PL
#pragma once
#include "WetBus.hpp"
#include <memory>
namespace CNA::Internal::Audio
{
// Control thread constructs/updates/destroys; Process is allocation-free under the mixer barrier.
class ReverbProcessor
{
  public:
    /** @brief Allocates a mono FXReverb network on the control thread.
     * @param rate Sample frames per second.
     * @param parameters Validated XGS-domain preset values.
     */
    ReverbProcessor(int rate, const ReverbParameters &parameters);
    /** @brief Frees delay storage after the mixer callback barrier. */
    ~ReverbProcessor();
    /** @brief Publishes coefficients without clearing history; caller holds the mixer barrier.
     * @param parameters New XGS-domain preset values.
     */
    void Update(const ReverbParameters &parameters);
    /** @brief Processes mono samples without allocation, locking or ownership changes.
     * @param input Input samples.
     * @param output Output samples.
     * @param frames Number of mono frames.
     */
    void Process(float *input, float *output, std::size_t frames) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace CNA::Internal::Audio
