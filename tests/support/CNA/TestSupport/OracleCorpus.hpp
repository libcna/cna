// SPDX-License-Identifier: MS-PL

#ifndef CNA_TEST_SUPPORT_ORACLE_CORPUS_HPP
#define CNA_TEST_SUPPORT_ORACLE_CORPUS_HPP

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::TestSupport
{
    /**
     * @brief One field of an oracle record, and how its value is delimited.
     *
     * `escaped` mirrors the two spellings the corpus readers have always used: a field written as
     * `([^"]*)` holds no escape sequences and ends at the first quotation mark, while one written
     * as `((?:[^"\\]|\\.)*)` may carry `\"`, `\\`, `\n` and `\r` and ends at the first quotation
     * mark that is not itself escaped. The distinction is kept rather than collapsed because the
     * two answer differently for a value containing a backslash, and a reader that quietly widened
     * a field would be changing what the corpus means.
     */
    struct OracleField
    {
        std::string_view key;
        bool escaped;
    };

    /**
     * @brief Reads the string fields of a single-line JSON record such as the oracle corpora hold.
     *
     * The corpora are one flat object per line -- `{"case": "name", "result": "text"}` and its
     * three- and four-field relatives -- which every consumer used to pick apart with a
     * `std::regex`. That is why this exists: MSVC's `<regex>` matches by recursing once per
     * repetition, so `(?:[^"\\]|\\.)*` over a long `result` exhausts the 1 MB default thread stack
     * and throws `regex_error(error_stack)` instead of matching. On native Windows that turned the
     * corpus into an empty map and failed 174 tests at once, in fifteen files, all of them
     * reporting "unknown file" because the throw happened inside a function-local static's
     * initializer rather than inside any test body. libstdc++ matches the same pattern iteratively,
     * which is why Linux never saw it.
     *
     * A hand-written scan has no such limit, is exact for this grammar, and is faster.
     *
     * @param line   The line to read; anything before the record and after it is ignored, matching
     *               the `regex_search` this replaces.
     * @param fields The expected keys, in the order the record writes them.
     * @param values Receives one raw -- still escaped -- value per field, untouched on failure.
     *               Callers apply their own unescaping, which differs between corpora.
     * @return True when the line holds a record with exactly these keys in this order.
     */
    [[nodiscard]] inline bool ReadOracleFields(const std::string& line,
                                               std::initializer_list<OracleField> fields,
                                               std::vector<std::string>& values)
    {
        if (fields.size() == 0)
        {
            return false;
        }

        // Every candidate start is tried in turn, left to right, so the leftmost record wins --
        // the same answer regex_search gives.
        for (std::size_t start = line.find('{'); start != std::string::npos;
             start = line.find('{', start + 1))
        {
            std::size_t at = start + 1;
            std::vector<std::string> found;
            found.reserve(fields.size());

            bool ok = true;
            for (const OracleField& field : fields)
            {
                // `"key": "` -- the separator before every field but the first is the `, ` that
                // the previous iteration's closing quotation mark is followed by.
                if (!found.empty())
                {
                    if (line.compare(at, 2, ", ") != 0)
                    {
                        ok = false;
                        break;
                    }
                    at += 2;
                }
                const std::string opening = std::string("\"") + std::string(field.key) + "\": \"";
                if (line.compare(at, opening.size(), opening) != 0)
                {
                    ok = false;
                    break;
                }
                at += opening.size();

                const std::size_t valueStart = at;
                bool closed = false;
                while (at < line.size())
                {
                    const char c = line[at];
                    if (field.escaped && c == '\\')
                    {
                        if (at + 1 >= line.size())
                        {
                            break;  // a trailing backslash cannot be an escape sequence
                        }
                        at += 2;
                        continue;
                    }
                    if (c == '"')
                    {
                        closed = true;
                        break;
                    }
                    ++at;
                }
                if (!closed)
                {
                    ok = false;
                    break;
                }
                found.emplace_back(line, valueStart, at - valueStart);
                ++at;  // step over the closing quotation mark
            }

            if (ok && line.compare(at, 1, "}") == 0)
            {
                values = std::move(found);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Reads the two fields of the common `{"case": ..., "result": ...}` record.
     *
     * @param line   The line to read.
     * @param name   Receives the case name; untouched on failure.
     * @param result Receives the raw, still-escaped result text; untouched on failure.
     * @return True when the line holds such a record.
     */
    [[nodiscard]] inline bool ReadOracleCase(const std::string& line, std::string& name,
                                             std::string& result)
    {
        std::vector<std::string> values;
        if (!ReadOracleFields(line, {{"case", false}, {"result", true}}, values))
        {
            return false;
        }
        name = std::move(values[0]);
        result = std::move(values[1]);
        return true;
    }
}

#endif
