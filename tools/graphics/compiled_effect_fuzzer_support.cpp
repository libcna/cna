// SPDX-License-Identifier: MS-PL
#include "compiled_effect_fuzzer_support.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>

namespace CNA::Tools::FxFuzz
{
    std::uint32_t Rng::Next()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    }

    std::uint32_t Rng::Below(std::uint32_t bound) { return bound == 0 ? 0 : Next() % bound; }

    void Mutate(Rng& random, std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty()) { return; }
        const auto count = 1u + random.Below(6);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            switch (random.Below(4))
            {
                case 0:
                    bytes[random.Below(static_cast<std::uint32_t>(bytes.size()))] ^=
                        static_cast<std::uint8_t>(1u << random.Below(8));
                    break;
                case 1:
                    bytes[random.Below(static_cast<std::uint32_t>(bytes.size()))] =
                        static_cast<std::uint8_t>(random.Below(256));
                    break;
                case 2:
                    if (bytes.size() >= 4)
                    {
                        const auto word = random.Below(static_cast<std::uint32_t>(bytes.size() / 4));
                        const auto value = random.Below(4) == 0 ? 0xFFFFFFFFu : random.Next();
                        for (std::size_t byte = 0; byte < 4; ++byte)
                        {
                            bytes[word * 4 + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
                        }
                    }
                    break;
                default:
                    bytes.resize(1 + random.Below(static_cast<std::uint32_t>(bytes.size())));
                    break;
            }
        }
    }

    std::uint64_t ParseUnsigned(std::string_view text, bool allowHex)
    {
        int base = 10;
        if (allowHex && text.starts_with("0x")) { text.remove_prefix(2); base = 16; }
        std::uint64_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        {
            throw std::invalid_argument("expected a complete unsigned integer");
        }
        return value;
    }

    std::vector<std::filesystem::path> EnumerateInputs(const std::vector<std::filesystem::path>& paths)
    {
        std::vector<std::filesystem::path> files;
        const auto take = [&](const std::filesystem::path& path) {
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("not a regular corpus file: " + path.string());
            }
            if (files.size() >= MaximumCorpusFiles) { throw std::runtime_error("too many corpus files"); }
            files.push_back(std::filesystem::absolute(path).lexically_normal());
        };
        for (const auto& path : paths)
        {
            if (std::filesystem::is_directory(path))
            {
                const auto before = files.size();
                for (const auto& entry : std::filesystem::directory_iterator(path))
                {
                    if (entry.is_regular_file()) { take(entry.path()); }
                }
                if (files.size() == before) { throw std::runtime_error("empty corpus directory: " + path.string()); }
            }
            else { take(path); }
        }
        std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
            return left.generic_u8string() < right.generic_u8string();
        });
        if (files.empty()) { throw std::runtime_error("the corpus is empty"); }
        return files;
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path, std::size_t remainingBudget)
    {
        if (!std::filesystem::is_regular_file(path)) { throw std::runtime_error("not a regular corpus file: " + path.string()); }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) { throw std::runtime_error("cannot open corpus file: " + path.string()); }
        const auto length = input.tellg();
        if (length <= 0 || static_cast<std::uint64_t>(length) > MaximumInputBytes
            || static_cast<std::uint64_t>(length) > remainingBudget)
        {
            throw std::runtime_error("empty, oversized or over-budget corpus file: " + path.string());
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))
            || input.peek() != std::char_traits<char>::eof() || input.bad())
        {
            throw std::runtime_error("incomplete or changing corpus file: " + path.string());
        }
        return bytes;
    }
}
