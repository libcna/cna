// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <string_view>
#include <vector>

namespace CNA::Tools::FxFuzz
{
    inline constexpr std::size_t MaximumInputBytes = 64u * 1024u * 1024u;
    inline constexpr std::size_t MaximumCorpusBytes = 256u * 1024u * 1024u;
    inline constexpr std::size_t MaximumCorpusFiles = 4096u;

    /** @brief Accepts ordinary API rejection; allocation exhaustion always escapes to the driver. */
    template<class Action>
    bool Attempt(Action&& action)
    {
        try { action(); return true; }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception&) { return false; }
    }

    struct Rng
    {
        std::uint64_t state;
        std::uint32_t Next();
        std::uint32_t Below(std::uint32_t bound);
    };

    void Mutate(Rng& random, std::vector<std::uint8_t>& bytes);
    std::uint64_t ParseUnsigned(std::string_view text, bool allowHex = false);
    std::vector<std::filesystem::path> EnumerateInputs(const std::vector<std::filesystem::path>& paths);
    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path,
                                     std::size_t remainingBudget = MaximumInputBytes);
}
