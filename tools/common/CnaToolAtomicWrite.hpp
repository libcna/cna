// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_cnb.md CNBF-122: one durable way for a content tool to produce its output file.
//
// Header-only, and shared rather than copied, for the same reason CnaToolNumericArgs.hpp is: the
// rule below is one rule, and two implementations of it would drift.
//
// The rule. A compiler that opens its destination with `trunc` has already destroyed the previous
// build's output by the time it discovers it cannot finish -- a full disk, an encoder that throws
// half-way, a process killed between two writes. What the caller is then left with is a file that
// exists, is shorter than it should be, and is newer than its inputs, so an incremental build
// leaves it alone and the failure surfaces much later as a corrupt asset. Writing a sibling
// temporary and renaming it over the destination makes the visible result all-or-nothing: either
// the complete new file or the untouched old one, never a prefix of the new one.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace CNA::Tools
{
    namespace Detail
    {
        /// Removes the temporary file however the enclosing scope exits, unless Release() said the
        /// rename already consumed it. This is what keeps a failed run from leaving debris beside
        /// the destination -- and it must run on the exception path, which is exactly the path a
        /// hand-written `remove()` after the write is not on.
        class TemporaryFileGuard
        {
        public:
            /**
             * @brief Takes responsibility for removing @p path unless Release() is called.
             *
             * @param path The temporary file to remove on destruction.
             */
            explicit TemporaryFileGuard(std::filesystem::path path) : path_(std::move(path)) {}

            /** @brief Removes the temporary file unless Release() disarmed the guard. */
            ~TemporaryFileGuard()
            {
                if (!armed_) { return; }
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            /** @brief Not copyable: exactly one object owns the removal. */
            TemporaryFileGuard(const TemporaryFileGuard&) = delete;

            /** @brief Not copy-assignable, for the same reason. */
            TemporaryFileGuard& operator=(const TemporaryFileGuard&) = delete;

            /** @brief Disarms the guard, after a successful rename has consumed the file. */
            void Release() { armed_ = false; }

        private:
            std::filesystem::path path_;
            bool armed_ = true;
        };

        /**
         * @brief The current process id, as the one varying part of a temporary file's name.
         *
         * No clock and no random source: these tools are deterministic by contract, and a name that
         * changed between two runs of the same command would be visible to anything watching the
         * output directory. Two *concurrent* runs of the same command still differ, which is all
         * this has to provide.
         *
         * @return The process id as decimal text.
         */
        [[nodiscard]] inline std::string ProcessTag()
        {
#ifdef _WIN32
            return std::to_string(static_cast<long long>(_getpid()));
#else
            return std::to_string(static_cast<long long>(::getpid()));
#endif
        }
    }

    /**
     * @brief Writes @p bytes to @p destination all-or-nothing (plans/plan_cnb.md `CNBF-122`).
     *
     * The bytes go to a temporary file **in the destination's own directory** -- a sibling, so the
     * rename that follows stays within one filesystem and is therefore atomic rather than a
     * copy-then-delete -- which is flushed and closed and checked before anything replaces the
     * destination. Only then is it renamed over @p destination, which on every platform CNA
     * targets replaces the existing file in one step.
     *
     * On **any** failure the temporary file is removed and @p destination is left exactly as it
     * was: an existing output from a previous build survives a failed one, and no partial file is
     * left behind for a build system to mistake for a finished product.
     *
     * @param destination The final path to create or replace.
     * @param bytes       The complete file contents.
     * @throws std::runtime_error if a temporary cannot be created, the write or close fails, or
     *         the rename fails. The message names the path and the reason.
     */
    inline void WriteFileAtomically(const std::filesystem::path& destination,
                                    const std::vector<std::uint8_t>& bytes)
    {
        const std::filesystem::path directory =
            destination.has_parent_path() ? destination.parent_path() : std::filesystem::path(".");

        // A short bounded search rather than one fixed name, so two tools writing different
        // destinations in one directory -- or the same tool retried after a crash left debris --
        // do not collide. `noreplace` makes the "is it free?" test and the claim one operation, so
        // there is no window between them.
        std::filesystem::path temporary;
        std::ofstream out;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const std::filesystem::path candidate =
                directory / (destination.filename().string() + ".cnatmp-" + Detail::ProcessTag() +
                             "-" + std::to_string(attempt));
            out.open(candidate, std::ios::binary | std::ios::out | std::ios::noreplace);
            if (out)
            {
                temporary = candidate;
                break;
            }
            out.clear();
        }
        if (temporary.empty())
        {
            throw std::runtime_error("cannot create a temporary file beside '" +
                                     destination.string() + "'");
        }

        Detail::TemporaryFileGuard guard(temporary);

        if (!bytes.empty())
        {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        // Closed EXPLICITLY and then tested. A destructor-closed stream reports nothing, so a
        // buffered write that failed only on the final flush -- the one a full disk produces --
        // would go unnoticed and a truncated file would be renamed into place as if it were whole.
        out.close();
        if (!out)
        {
            throw std::runtime_error("failed while writing '" + destination.string() + "'");
        }

        std::error_code ec;
        std::filesystem::rename(temporary, destination, ec);
        if (ec)
        {
            throw std::runtime_error("cannot replace '" + destination.string() +
                                     "': " + ec.message());
        }
        guard.Release();
    }
}
