// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include <functional>
#include <memory>
namespace CNA::Internal::GamerServices {
/** @brief Creates an explicit deterministic in-process control fixture.
 * @param authorize Authenticated online-privileged local identity check.
 * @param gamertag Public name lookup by account ID. @param userId Recipient lookup by public name.
 * @param clock Optional injected Unix-second clock. @return Isolated fixture directory. */
std::unique_ptr<IServiceSessionDirectory> makeFakeSessionDirectory(
    std::function<void(const std::string&)> authorize,
    std::function<std::string(const std::string&)> gamertag,
    std::function<std::string(const std::string&)> userId,
    std::function<long long()> clock={});
}
