// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>
#include <optional>
namespace CNA::GamerServices {
/** @brief Out-of-band deployment settings; contains no account credentials. */
struct Configuration {
    /** @brief Full HTTPS control endpoint ending in /cna/v1. Empty selects unconfigured offline mode. */
    std::string endpoint;
    /** @brief Stable registered title identifier, isolated by the service. */
    std::string gameId;
    /** @brief Optional trust bundle path; normal certificate and hostname checks remain enabled. */
    std::string caBundle;
    /** @brief Explicitly permits unencrypted numeric loopback development connections only. */
    bool insecureLoopback = false;
};
/** @brief Overrides deployment configuration before dispatcher initialization.
 * @param configuration Complete override; nullopt removes it. */
void setConfigurationOverride(std::optional<Configuration> configuration);
/** @brief Resolves override, environment, title manifest and user settings in that order.
 * @return Validated deployment configuration. */
Configuration resolveConfiguration();
/** @brief Validates endpoint/title/security constraints without making a connection.
 * @param configuration Settings to validate. */
void validateConfiguration(const Configuration& configuration);
}
