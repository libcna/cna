// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/GamerServices/Configuration.hpp"
#include <filesystem>
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
/** @brief Where a signed-in player's refresh credential is kept, one process lease per
 * endpoint/title. Linux: the freedesktop Secret Service (the desktop keyring) when a session bus
 * and a keyring answer, else owner-only 0600 files; macOS and other POSIX: owner-only files;
 * Windows: files sealed to the user with DPAPI. A directory named by
 * CNA_GAMER_SERVICES_CREDENTIALS_DIR, or CNA_GAMER_SERVICES_KEYRING=0, keeps Linux on files;
 * CNA_GAMER_SERVICES_CREDENTIALS_DIR=0 keeps nothing. Nothing here is ever logged. */
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
    /** @brief How records are protected: "secret-service", "private-file", "dpapi", or "none"
     * (nothing is kept). @return Protection. */
    const char* protection() const;
private:
    std::string name(int slot) const;
    std::string key_;
#if defined(_WIN32)
    void* lease_=nullptr;
    std::filesystem::path root_;
#else
    int directory_=-1,lease_=-1;
    bool keyring_=false;
#endif
};
}
