// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/GamerServices/Configuration.hpp"
#include <optional>
#include <string>

namespace CNA::Internal::GamerServices {
/** @brief User-owned refresh authority; passwords and access tokens are never stored here. */
struct StoredCredential {
    /** @brief Opaque rotating refresh credential. */
    std::string refreshToken;
    /** @brief Client-estimated Unix-second expiry from the server's remaining lifetime. */
    long long expires=0;
};
/** @brief Private atomic POSIX credential files, with one process lease per endpoint/title. */
class CredentialStore {
public:
    /** @brief Opens secure user storage or selects ephemeral behavior if unavailable.
     * @param config Endpoint/title binding; no credentials. */
    explicit CredentialStore(const CNA::GamerServices::Configuration& config);
    /** @brief Releases the storage lease. */
    ~CredentialStore();
    /** @brief Disallows copying a live storage lease. @param other Source lease. */
    CredentialStore(const CredentialStore& other)=delete;
    /** @brief Disallows lease assignment. @param other Source lease. @return This lease. */
    CredentialStore& operator=(const CredentialStore& other)=delete;
    /** @brief Reads a validated private record. @param slot Local slot. @return Record or absence. */
    std::optional<StoredCredential> load(int slot) const;
    /** @brief Atomically persists refresh authority. @param slot Local slot. @param value Record.
     * @return Whether secure persistence succeeded. */
    bool save(int slot,const StoredCredential& value) const;
    /** @brief Removes this title/slot's authority. @param slot Local slot. */
    void remove(int slot) const;
private:
    std::string name(int slot) const;
    std::string key_;
    int directory_=-1,lease_=-1;
};
}
