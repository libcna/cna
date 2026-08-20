// Task 479 (plans/plan_graphics.md Phase 53): minimal hand-rolled JSON writer for the CNA-side
// reference-value dump tool, mirroring tools/fna-reference/JsonWriter.cs exactly (same Add()
// overload shape, same nested-object support) so the two languages produce structurally
// comparable JSON without either side depending on a vendored JSON library.
//
// Not part of the Microsoft::Xna public API surface -- internal dev tooling only, so this is
// deliberately outside both the Microsoft::Xna and CNA namespaces and carries no Doxygen/CNAEXT
// requirements (CLAUDE.md's comment rules apply to the XNA API surface and CNA library code,
// not one-off internal tools).
#pragma once

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cna_reference
{
    class JsonWriter
    {
    public:
        JsonWriter& Add(const std::string& name, double value)
        {
            // std::ostringstream's default precision (6 significant digits) silently truncates
            // large packed-value integers (e.g. 4278190335 -> "4.2782e+09") and sub-millimeter
            // float differences alike; max_digits10 is the minimum precision that round-trips
            // any double back to its exact original bit pattern.
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
            entries_.emplace_back(name, oss.str());
            return *this;
        }

        JsonWriter& Add(const std::string& name, int value)
        {
            entries_.emplace_back(name, std::to_string(value));
            return *this;
        }

        JsonWriter& Add(const std::string& name, bool value)
        {
            entries_.emplace_back(name, value ? "true" : "false");
            return *this;
        }

        JsonWriter& Add(const std::string& name, const std::string& value)
        {
            entries_.emplace_back(name, Quote(value));
            return *this;
        }

        JsonWriter& Add(const std::string& name, const JsonWriter& nested)
        {
            entries_.emplace_back(name, nested.ToString());
            return *this;
        }

        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream oss;
            oss << "{";
            for (std::size_t i = 0; i < entries_.size(); ++i)
            {
                if (i > 0) oss << ",";
                oss << Quote(entries_[i].first) << ":" << entries_[i].second;
            }
            oss << "}";
            return oss.str();
        }

    private:
        static std::string Quote(const std::string& s)
        {
            std::string out = "\"";
            for (char c : s)
            {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += "\"";
            return out;
        }

        std::vector<std::pair<std::string, std::string>> entries_;
    };
}
