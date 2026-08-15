// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include <string>
#include <vector>

namespace CNA::Platform::Common {

    /**
     * @brief Path resolution and file loading on the C++ standard library alone.
     *
     * Shared by every implementation that has a real filesystem but no platform library to ask
     * about it. `GetFileSystem()` may never return null on any platform — a game's save/load path
     * has to work on a machine with no window, no display and no input — so this is the answer
     * for implementations that would otherwise each write the same `std::filesystem` calls.
     *
     * Deliberately not in the public include tree: it is an implementation detail shared between
     * implementations, not part of the contract.
     */
    class StandardFileSystem final : public IPlatformFileSystem
    {
    public:
        /**
         * @brief Creates the filesystem service.
         *
         * @param preferencesRootName The directory name placed under the system temporary
         *        directory to hold preference paths. Distinct per implementation so two platforms
         *        in one process cannot collide on the same directory.
         */
        explicit StandardFileSystem(std::string preferencesRootName);

        /** @brief Gets the directory the process was started from. @return An absolute path ending in a separator. */
        [[nodiscard]] std::string GetBasePath() const override;

        /**
         * @brief Gets a writable per-application directory, creating it if necessary.
         *
         * @param organization The organization name component.
         * @param application The application name component.
         * @return An absolute path ending in a separator.
         * @throws PlatformException If the directory cannot be created.
         */
        [[nodiscard]] std::string GetPreferencesPath(const std::string& organization,
                                                     const std::string& application) const override;

        /** @brief Gets a configured Music/Pictures path, or empty when unavailable. */
        [[nodiscard]] std::string GetUserFolder(UserFolder folder) const override;

        /**
         * @brief Reads a whole file into memory.
         *
         * @param path The file to read.
         * @param data Receives the file's bytes; left untouched when the file cannot be read.
         * @return True if the file was read.
         */
        [[nodiscard]] bool TryLoadFile(const std::string& path,
                                       std::vector<std::uint8_t>& data) const override;

        /**
         * @brief Creates a directory and any missing parents.
         *
         * @param path The directory to create.
         * @throws PlatformException If the directory cannot be created.
         */
        void CreateDirectory(const std::string& path) override;

    private:
        std::string preferencesRootName_;
    };

} // namespace CNA::Platform::Common
