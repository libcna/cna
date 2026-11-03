// SPDX-License-Identifier: MS-PL
//
// MTH-SAFE-007: the seeded batch protocol every standalone fuzz harness shares with the long
// campaign driver (tools/fuzz/run_long_campaign.py) and its replay tool (tools/fuzz/reproduce.py).
//
// Candidate N depends only on the ordered corpus and the 64-bit seed N -- never on the candidates
// run before it -- so one recorded seed regenerates one exact input outside any campaign, and a
// saved input replays on its own:
//
//   <harness> --seeded <first-seed> <count> <corpus-file-or-dir>...
//   <harness> --emit <seed> <output-file> <corpus-file-or-dir>...
//   <harness> --replay-one <input-file>
//
// A directory contributes its regular files sorted by path; files keep command-line order.
// Standard output carries one `cna_fuzz_start {json}` line once the harness is ready (its required
// capability verified), one flushed `cna_fuzz_begin <seed>` line before every candidate and one
// `cna_fuzz_result {json}` line with the harness's own cumulative counters, "operations" among
// them; a `cna_fuzz_progress {json}` snapshot of the same counters follows a candidate at most once
// a second, so a process that dies still accounts for what it reached. `--seeded <first> 0` runs
// no candidate: the driver's control for a failure at exit. Exit status: 0 completed (also after SIGINT/SIGTERM, which end the batch after the current
// candidate), 2 usage or corpus error, 3 required capability unavailable. A failing candidate ends
// however the failure ends it -- a signal, a sanitizer exit, an escaped exception reaching
// std::terminate -- and the last begin line names its seed. Nothing here catches a candidate's
// exception: which exceptions are an ordinary refusal is each harness's own decision.
#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace CNA::Tools::Fuzz
{
    using Bytes = std::vector<std::uint8_t>;
    using Counters = std::vector<std::pair<std::string, std::uint64_t>>;

    inline constexpr std::size_t MaximumSeedFileBytes = 64u * 1024u * 1024u;
    inline constexpr std::size_t MaximumSeedCorpusBytes = 256u * 1024u * 1024u;
    inline constexpr std::size_t MaximumSeedFiles = 4096u;

    /** @brief Thrown by a harness's prepare step when its required capability is absent. */
    struct CapabilityUnavailable : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** @brief One harness: how it proves readiness, mutates, runs and counts. */
    struct SeededHarness
    {
        std::string name;
        /** Verifies the required capability and returns a description of it; may throw CapabilityUnavailable. */
        std::function<std::string()> prepare;
        /** One mutation round over a candidate, driven by a 64-bit value. */
        std::function<Bytes(Bytes, std::uint64_t)> mutate;
        /** Runs one candidate through the code under test. */
        std::function<void(const Bytes&)> run;
        /** The harness's cumulative counters; must contain "operations". */
        std::function<Counters()> counters;
        /** Most mutation rounds per candidate; 1 for a mutator that already applies several edits. */
        unsigned maximumRounds = 1;
    };

    /** @brief SplitMix64: a well-mixed stream from any seed, zero included. */
    inline std::uint64_t NextMixed(std::uint64_t& state)
    {
        std::uint64_t value = (state += 0x9E3779B97F4A7C15ULL);
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

    namespace Detail
    {
        struct UsageError : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        inline volatile std::sig_atomic_t stopRequested = 0;

        inline void RequestStop(int) { stopRequested = 1; }

        inline std::uint64_t ParseSeed(const std::string_view text, const char* what)
        {
            std::string_view digits = text;
            int base = 10;
            if (digits.starts_with("0x") || digits.starts_with("0X"))
            {
                digits.remove_prefix(2);
                base = 16;
            }
            std::uint64_t value = 0;
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
            if (digits.empty() || result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
            {
                throw UsageError(std::string("invalid unsigned 64-bit ") + what + ": " + std::string(text));
            }
            return value;
        }

        inline Bytes ReadBounded(const std::filesystem::path& path, std::size_t& remaining)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
            {
                throw UsageError("not a regular file: " + path.string());
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > MaximumSeedFileBytes || size > remaining)
            {
                throw UsageError("unreadable, oversized or over-budget input: " + path.string());
            }
            std::ifstream input(path, std::ios::binary);
            Bytes bytes(static_cast<std::size_t>(size));
            if (!input || !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))
                || input.peek() != std::ifstream::traits_type::eof())
            {
                throw UsageError("incomplete or changing input: " + path.string());
            }
            remaining -= bytes.size();
            return bytes;
        }

        inline std::vector<Bytes> ReadCorpus(char** first, char** last)
        {
            std::vector<std::filesystem::path> files;
            for (char** argument = first; argument != last; ++argument)
            {
                const std::filesystem::path path(*argument);
                std::error_code error;
                if (std::filesystem::is_directory(path, error))
                {
                    std::vector<std::filesystem::path> entries;
                    for (const auto& entry : std::filesystem::directory_iterator(path, error))
                    {
                        if (entry.is_regular_file(error)) { entries.push_back(entry.path()); }
                    }
                    if (error) { throw UsageError("cannot list corpus directory: " + path.string()); }
                    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                        return left.generic_string() < right.generic_string();
                    });
                    files.insert(files.end(), entries.begin(), entries.end());
                }
                else
                {
                    files.push_back(path);
                }
                if (files.size() > MaximumSeedFiles) { throw UsageError("too many corpus files"); }
            }
            if (files.empty()) { throw UsageError("the corpus is empty"); }
            std::vector<Bytes> corpus;
            std::size_t remaining = MaximumSeedCorpusBytes;
            for (const auto& file : files) { corpus.push_back(ReadBounded(file, remaining)); }
            return corpus;
        }

        inline std::string Quote(const std::string_view text)
        {
            std::string out = "\"";
            for (const char raw : text)
            {
                const auto c = static_cast<unsigned char>(raw);
                if (c == '"' || c == '\\') { out += '\\'; out += raw; }
                else if (c < 0x20)
                {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                }
                else { out += raw; }
            }
            return out + '"';
        }

        inline void Emit(const std::string& line)
        {
            if (std::fputs(line.c_str(), stdout) < 0 || std::fputc('\n', stdout) == EOF || std::fflush(stdout) != 0)
            {
                // The driver has gone; a candidate nobody can attribute is worth nothing.
                std::_Exit(4);
            }
        }

        inline std::string CounterJson(const SeededHarness& harness)
        {
            std::string out = "{";
            bool operations = false;
            for (const auto& [name, value] : harness.counters())
            {
                if (out.size() > 1) { out += ','; }
                out += Quote(name) + ':' + std::to_string(value);
                operations = operations || name == "operations";
            }
            if (!operations) { throw std::logic_error("harness counters lack \"operations\""); }
            return out + '}';
        }
    }

    /**
     * @brief The candidate for one seed: a seed-chosen corpus entry after 1..maximumRounds mutation rounds.
     *
     * @param harness The harness whose mutation rounds apply.
     * @param corpus The ordered corpus.
     * @param seed The candidate's seed.
     * @param corpusIndex Receives the chosen corpus entry.
     * @return The candidate bytes.
     */
    inline Bytes DeriveCandidate(const SeededHarness& harness, const std::vector<Bytes>& corpus,
                                 const std::uint64_t seed, std::size_t& corpusIndex)
    {
        std::uint64_t stream = seed;
        corpusIndex = static_cast<std::size_t>(NextMixed(stream) % corpus.size());
        const auto rounds = 1u + static_cast<unsigned>(NextMixed(stream) % std::max(1u, harness.maximumRounds));
        Bytes bytes = corpus[corpusIndex];
        for (unsigned round = 0; round < rounds; ++round) { bytes = harness.mutate(std::move(bytes), NextMixed(stream)); }
        return bytes;
    }

    /**
     * @brief Serves the three protocol modes.
     *
     * @param argc The process argument count.
     * @param argv The process arguments.
     * @param harness The harness description.
     * @return -1 when argv[1] names none of the modes, otherwise the process exit status.
     */
    inline int SeededMain(const int argc, char** argv, const SeededHarness& harness)
    {
        const std::string_view mode = argc > 1 ? argv[1] : "";
        if (mode != "--seeded" && mode != "--emit" && mode != "--replay-one") { return -1; }
        std::string capability;
        try
        {
            if (mode == "--emit")
            {
                if (argc < 5) { throw Detail::UsageError("--emit needs <seed> <output-file> <corpus>..."); }
                const auto seed = Detail::ParseSeed(argv[2], "seed");
                const auto corpus = Detail::ReadCorpus(argv + 4, argv + argc);
                std::size_t index = 0;
                const auto bytes = DeriveCandidate(harness, corpus, seed, index);
                std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                output.close();
                if (!output) { throw Detail::UsageError(std::string("cannot write ") + argv[3]); }
                Detail::Emit("cna_fuzz_emit {\"harness\":" + Detail::Quote(harness.name) + ",\"seed\":" +
                             std::to_string(seed) + ",\"corpus_index\":" + std::to_string(index) +
                             ",\"bytes\":" + std::to_string(bytes.size()) + '}');
                return 0;
            }
            std::uint64_t first = 0;
            std::uint64_t count = 1;
            std::vector<Bytes> corpus;
            if (mode == "--seeded")
            {
                if (argc < 5) { throw Detail::UsageError("--seeded needs <first-seed> <count> <corpus>..."); }
                first = Detail::ParseSeed(argv[2], "first seed");
                count = Detail::ParseSeed(argv[3], "count");
                // A count of zero is the driver's control batch: prepare, report, run nothing.
                if (count != 0 && first + (count - 1) < first) { throw Detail::UsageError("wrapping seed range"); }
                corpus = Detail::ReadCorpus(argv + 4, argv + argc);
            }
            else
            {
                if (argc != 3) { throw Detail::UsageError("--replay-one needs exactly one <input-file>"); }
                std::size_t remaining = MaximumSeedFileBytes;
                corpus.push_back(Detail::ReadBounded(argv[2], remaining));
            }
            std::signal(SIGINT, &Detail::RequestStop);
            std::signal(SIGTERM, &Detail::RequestStop);
            capability = harness.prepare();
            std::size_t corpusBytes = 0;
            for (const auto& entry : corpus) { corpusBytes += entry.size(); }
            Detail::Emit("cna_fuzz_start {\"harness\":" + Detail::Quote(harness.name) + ",\"mode\":" +
                         Detail::Quote(mode.substr(2)) + ",\"first_seed\":" + std::to_string(first) +
                         ",\"count\":" + std::to_string(count) + ",\"corpus_files\":" +
                         std::to_string(corpus.size()) + ",\"corpus_bytes\":" + std::to_string(corpusBytes) +
                         ",\"capability\":" + Detail::Quote(capability) + '}');
            std::uint64_t attempts = 0;
            auto nextProgress = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            for (; attempts < count && Detail::stopRequested == 0; ++attempts)
            {
                const auto seed = first + attempts;
                std::size_t index = 0;
                const auto bytes = mode == "--seeded" ? DeriveCandidate(harness, corpus, seed, index) : corpus.front();
                Detail::Emit("cna_fuzz_begin " + std::to_string(seed));
                harness.run(bytes);
                if (const auto now = std::chrono::steady_clock::now(); now >= nextProgress)
                {
                    nextProgress = now + std::chrono::seconds(1);
                    Detail::Emit("cna_fuzz_progress {\"attempts\":" + std::to_string(attempts + 1) +
                                 ",\"counters\":" + Detail::CounterJson(harness) + '}');
                }
            }
            Detail::Emit("cna_fuzz_result {\"harness\":" + Detail::Quote(harness.name) + ",\"first_seed\":" +
                         std::to_string(first) + ",\"attempts\":" + std::to_string(attempts) +
                         ",\"stopped\":" + (attempts < count ? "true" : "false") +
                         ",\"counters\":" + Detail::CounterJson(harness) + '}');
            return 0;
        }
        catch (const CapabilityUnavailable& error)
        {
            std::fprintf(stderr, "%s: not qualified: %s\n", harness.name.c_str(), error.what());
            return 3;
        }
        catch (const Detail::UsageError& error)
        {
            std::fprintf(stderr, "%s: %s\n", harness.name.c_str(), error.what());
            return 2;
        }
    }
}
