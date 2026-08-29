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
//
// plans/plan_cnb.md CNBF-123 audited the publication step itself, which CNBF-122 performed with
// std::filesystem::rename under a comment asserting it "replaces the existing file in one step" on
// every platform CNA targets. That was an assertion rather than a measurement, and its two halves
// are not equally safe:
//
//   * POSIX. rename(2) is SPECIFIED to replace an existing destination atomically, and
//     [fs.op.rename] defines std::filesystem::rename as "as if by POSIX rename()". That is a
//     standardised guarantee, so the standard library call is used directly.
//   * Windows. The C runtime's rename() FAILS when the destination already exists -- measured,
//     see spikes/atomic-replace-spike -- so whether std::filesystem::rename replaces depends
//     entirely on which standard library is in the build. libstdc++ happens to call
//     MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) and therefore does;
//     that was confirmed by disassembling the import and by running this contract under Wine. CNA
//     also builds with MSVC, whose STL could not be exercised here, so the guarantee is NOT left
//     resting on a library detail: the Windows branch below calls MoveFileExW itself.
//
// It passes MOVEFILE_REPLACE_EXISTING and deliberately NOT MOVEFILE_COPY_ALLOWED. The temporary is
// always a sibling of the destination, so a cross-volume move cannot legitimately arise; permitting
// one would let publication silently degrade into a copy-then-delete, which is precisely the
// non-atomic window this file exists to remove. Refusing it is the safer failure.
//
// What is NOT done, and deliberately: delete-the-destination-then-rename. That "works" everywhere
// and destroys the invariant, because between the two calls there is no valid destination at all.
//
// The same audit found that the CNBF-122 version of this file did not work on Windows AT ALL, for
// a reason no Linux test could reach: it claimed the temporary's name with
// `std::ofstream(..., std::ios::noreplace)`, and libstdc++'s Windows filebuf fails that open even
// on a FREE name (measured -- see spikes/atomic-replace-spike; the CRT's own `fopen("wbx")`
// succeeds on the same path, so it is a library gap rather than an OS limitation). All 64 attempts
// therefore failed and every write threw "cannot create a temporary file beside ...". The
// temporary is now created and written through the platform's own exclusive-create primitive, so
// neither half of this file depends on a C++23 library feature one supported platform does not
// implement.
//
// Power-loss durability is out of scope: nothing here fsyncs the file or its directory. The
// contract is that no PROCESS failure can leave a partial file where a complete one was.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
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

        /**
         * @brief A newly created, exclusively owned file, written through the platform's own
         *        primitive (plans/plan_cnb.md `CNBF-123`).
         *
         * Exists because `std::ofstream` cannot do the one thing this needs on both platforms:
         * **create a file only if that name is free**. `std::ios::noreplace` is the standard way to
         * ask, and libstdc++'s Windows filebuf refuses the open even when the name IS free, so the
         * CNBF-122 helper could not create a temporary on Windows at all. `O_EXCL` and `CREATE_NEW`
         * are the platforms' own answers and both behave.
         *
         * Writing through the same handle rather than reopening by name also removes a
         * time-of-check window: nothing can substitute a different file for the one this claimed.
         */
        class ExclusiveNewFile
        {
        public:
#if defined(_WIN32)
            using Handle = HANDLE;
            static Handle Invalid() { return INVALID_HANDLE_VALUE; }
#else
            using Handle = int;
            static Handle Invalid() { return -1; }
#endif

            /**
             * @brief Creates @p path, failing if anything already has that name.
             *
             * @param path The file to create.
             */
            explicit ExclusiveNewFile(const std::filesystem::path& path)
            {
#if defined(_WIN32)
                // CREATE_NEW is the exclusive one: it fails with ERROR_FILE_EXISTS rather than
                // opening or truncating. Share mode 0 keeps the half-written temporary
                // unobservable, which is the same reason it is written under a different name.
                handle_ = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
#else
                // 0666 rather than 0600: the output of a content build is an ordinary artifact,
                // and the caller's umask is what should decide, exactly as std::ofstream would.
                do
                {
                    handle_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
                } while (handle_ < 0 && errno == EINTR);
#endif
            }

            /** @brief Closes the file if Close() was not called, discarding any error. */
            ~ExclusiveNewFile()
            {
                if (IsOpen()) { CloseHandleOnly(); }
            }

            /** @brief Not copyable: exactly one object owns the handle. */
            ExclusiveNewFile(const ExclusiveNewFile&) = delete;

            /** @brief Not copy-assignable, for the same reason. */
            ExclusiveNewFile& operator=(const ExclusiveNewFile&) = delete;

            /**
             * @brief Whether the exclusive create succeeded.
             *
             * @return True when this object owns an open file.
             */
            [[nodiscard]] bool IsOpen() const { return handle_ != Invalid(); }

            /**
             * @brief Writes every byte of @p bytes, or reports failure.
             *
             * Partial writes are resumed rather than assumed away -- a short `write` is legal on
             * POSIX and is exactly what a filling disk produces -- and `WriteFile` is fed in
             * `DWORD`-sized pieces so a payload above 4 GiB cannot silently truncate.
             *
             * @param bytes The complete contents.
             * @return True when all of it was accepted.
             */
            [[nodiscard]] bool WriteAll(const std::vector<std::uint8_t>& bytes)
            {
                return WriteAll(bytes.data(), bytes.size());
            }

            /**
             * @brief Writes a complete byte range, resuming short platform writes.
             *
             * @param bytes First byte to write, or null when @p size is zero.
             * @param size Number of bytes to write.
             * @return True when every byte was accepted.
             */
            [[nodiscard]] bool WriteAll(const std::uint8_t* bytes, std::size_t size)
            {
                std::size_t written = 0;
                while (written < size)
                {
                    const std::size_t remaining = size - written;
#if defined(_WIN32)
                    const DWORD piece = static_cast<DWORD>(
                        remaining > 0x2000'0000u ? 0x2000'0000u : remaining);
                    DWORD produced = 0;
                    if (::WriteFile(handle_, bytes + written, piece, &produced, nullptr) == 0)
                    {
                        return false;
                    }
                    if (produced == 0) { return false; }
                    written += produced;
#else
                    const ssize_t produced = ::write(handle_, bytes + written, remaining);
                    if (produced < 0)
                    {
                        if (errno == EINTR) { continue; }
                        return false;
                    }
                    if (produced == 0) { return false; }
                    written += static_cast<std::size_t>(produced);
#endif
                }
                return true;
            }

            /**
             * @brief Closes the file and reports whether the close itself succeeded.
             *
             * Checked rather than left to the destructor: a close is where a deferred write error
             * surfaces on more than one filesystem, and a truncated temporary published as though
             * it were whole is the failure this file exists to prevent.
             *
             * @return True when the file closed cleanly.
             */
            [[nodiscard]] bool Close()
            {
                if (!IsOpen()) { return true; }
                return CloseHandleOnly();
            }

        private:
            bool CloseHandleOnly()
            {
#if defined(_WIN32)
                const bool ok = ::CloseHandle(handle_) != 0;
#else
                // EINTR is NOT retried. POSIX leaves the descriptor's state unspecified after an
                // interrupted close, and on Linux it has already been released -- so looping would
                // be a double close, which in a threaded program can shut a descriptor another
                // thread has just been handed. Treated as closed, which is what Linux means by it.
                // Any other error is a real one and is reported: a close is where a deferred write
                // failure surfaces.
                const bool ok = ::close(handle_) == 0 || errno == EINTR;
#endif
                handle_ = Invalid();
                return ok;
            }

            Handle handle_ = Invalid();
        };

        /**
         * @brief Publishes @p temporary as @p destination in one step, replacing an existing
         *        @p destination (plans/plan_cnb.md `CNBF-123`).
         *
         * The whole platform surface of this header is this one function. Everything above it --
         * choosing a temporary name, writing, checking the close -- is portable; only the final
         * publication differs, so only the final publication is branched.
         *
         * **POSIX:** `std::filesystem::rename`, which [fs.op.rename] defines as "as if by POSIX
         * `rename()`", and POSIX specifies that as an atomic replace. A standardised guarantee, so
         * the standard library is trusted for it.
         *
         * **Windows:** `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` and **not**
         * `MOVEFILE_COPY_ALLOWED`. Called directly rather than through `std::filesystem::rename`
         * because on Windows that function's replace behaviour is a property of the standard
         * library rather than of the language: the C runtime's own `rename()` fails outright when
         * the destination exists, and libstdc++ only replaces because it happens to forward to this
         * same API. CNA also builds with MSVC. Calling it here makes the guarantee CNA's own on
         * every Windows toolchain instead of one it inherits from whichever library is linked.
         *
         * `std::filesystem::path::c_str()` is already `const wchar_t*` on Windows, so the native
         * wide path is passed through with no transcoding and no `MAX_PATH` truncation of a
         * caller's own making.
         *
         * @param temporary   The complete file to publish. Must be a sibling of @p destination.
         * @param destination The final path, which may or may not already exist.
         * @param ec          Cleared on success; set to the platform error on failure, in which
         *                    case @p destination is left as it was.
         */
        inline void ReplaceFileAtomically(const std::filesystem::path& temporary,
                                          const std::filesystem::path& destination,
                                          std::error_code& ec)
        {
#if defined(_WIN32)
            ec.clear();
            if (::MoveFileExW(temporary.c_str(), destination.c_str(),
                              MOVEFILE_REPLACE_EXISTING) == 0)
            {
                // GetLastError() is a Win32 code, which std::system_category() is defined to carry
                // on Windows -- the same pairing std::filesystem itself uses, so a caller printing
                // ec.message() gets the platform's own wording.
                ec.assign(static_cast<int>(::GetLastError()), std::system_category());
            }
#else
            std::filesystem::rename(temporary, destination, ec);
#endif
        }

        /**
         * @brief Produces one sibling temporary and atomically publishes it.
         *
         * @tparam Producer Callable accepting ExclusiveNewFile& and returning true on a complete
         * write.
         * @param destination Final file path.
         * @param producer Byte producer invoked exactly once after exclusive temporary creation.
         */
        template<typename Producer>
        inline void PublishFileAtomically(const std::filesystem::path& destination,
                                          Producer&& producer)
        {
            const std::filesystem::path directory =
                destination.has_parent_path() ? destination.parent_path()
                                              : std::filesystem::path(".");

            std::filesystem::path temporary;
            std::optional<ExclusiveNewFile> out;
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                const std::filesystem::path candidate =
                    directory /
                    (destination.filename().string() + ".cnatmp-" + ProcessTag() + "-" +
                     std::to_string(attempt));
                out.emplace(candidate);
                if (out->IsOpen())
                {
                    temporary = candidate;
                    break;
                }
                out.reset();
            }
            if (temporary.empty())
            {
                throw std::runtime_error("cannot create a temporary file beside '" +
                                         destination.string() + "'");
            }

            TemporaryFileGuard guard(temporary);
            if (!producer(*out))
            {
                throw std::runtime_error("failed while writing '" + destination.string() + "'");
            }
            if (!out->Close())
            {
                throw std::runtime_error("failed while closing the temporary for '" +
                                         destination.string() + "'");
            }

            std::error_code ec;
            ReplaceFileAtomically(temporary, destination, ec);
            if (ec)
            {
                throw std::runtime_error("cannot replace '" + destination.string() +
                                         "': " + ec.message());
            }
            guard.Release();
        }
    }

    /**
     * @brief Writes @p bytes to @p destination all-or-nothing (plans/plan_cnb.md `CNBF-122`).
     *
     * The bytes go to a temporary file **in the destination's own directory** -- a sibling, so the
     * publication that follows stays within one filesystem and is therefore a rename rather than a
     * copy-then-delete -- which is flushed and closed and checked before anything replaces the
     * destination. Only then is it published over @p destination by
     * Detail::ReplaceFileAtomically(), which replaces an existing destination in one step on both
     * of CNA's platform families and says there exactly how it does so
     * (plans/plan_cnb.md `CNBF-123`).
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
        Detail::PublishFileAtomically(destination, [&](Detail::ExclusiveNewFile& out)
        {
            return out.WriteAll(bytes);
        });
    }

    /**
     * @brief Copies @p source to @p destination with bounded memory and atomic replacement.
     *
     * The copy uses the same exclusively created sibling temporary and platform replacement path
     * as WriteFileAtomically(). The source is never opened for writing and the destination is not
     * changed unless the complete stream was read, written, and closed successfully.
     *
     * @param source Existing regular file to copy.
     * @param destination Final path to create or replace.
     * @throws std::runtime_error if the source cannot be read or atomic publication fails.
     */
    inline void CopyFileAtomically(const std::filesystem::path& source,
                                   const std::filesystem::path& destination)
    {
        std::ifstream input(source, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("cannot open deployment source '" + source.string() + "'");
        }
        Detail::PublishFileAtomically(destination, [&](Detail::ExclusiveNewFile& out)
        {
            std::array<std::uint8_t, 1024u * 1024u> buffer{};
            while (input)
            {
                input.read(reinterpret_cast<char*>(buffer.data()),
                           static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if (count > 0 &&
                    !out.WriteAll(buffer.data(), static_cast<std::size_t>(count)))
                {
                    return false;
                }
            }
            if (!input.eof())
            {
                throw std::runtime_error("failed while reading deployment source '" +
                                         source.string() + "'");
            }
            return true;
        });
    }
}
