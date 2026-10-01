// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "System/InvalidOperationException.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

#include <any>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace CNA::Internal::GamerServices
{
    namespace fs = std::filesystem;
    using StoreJson = nlohmann::json;
    using Microsoft::Xna::Framework::GamerServices::LeaderboardOutcome;
    using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;

    namespace
    {
        const StoreJson* Find(const StoreJson& object, const char* key)
        {
            const auto it = object.find(key);
            return it == object.end() ? nullptr : &*it;
        }

        // Accept legacy integral floating-point numbers without an out-of-range cast.
        // Already-rounded legacy data cannot recover its original integer value.
        std::optional<long long> Integer(const StoreJson* value)
        {
            if (!value) return std::nullopt;
            if (value->is_number_unsigned())
            {
                const auto number = value->get<unsigned long long>();
                if (number > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) return std::nullopt;
                return static_cast<long long>(number);
            }
            if (value->is_number_integer()) return value->get<long long>();
            if (value->is_number_float())
            {
                const double number = value->get<double>();
                if (std::isfinite(number) && std::trunc(number) == number &&
                    number >= -0x1p63 && number < 0x1p63) return static_cast<long long>(number);
            }
            return std::nullopt;
        }

        // Task 4.2: reuses this codebase's existing user-data-directory convention
        // (StorageDevice::GetStorageRootEXT(), platform user-data-directory backed) rather than inventing a
        // new one - a plain "GamerServices" subdirectory under it.
        fs::path StoreRoot()
        {
            return CNA::Internal::PathFromUtf8(
                Microsoft::Xna::Framework::Storage::StorageDevice::GetStorageRootEXT())
                / "GamerServices";
        }

        fs::path AchievementsDir() { return StoreRoot() / "achievements"; }
        fs::path LeaderboardsDir() { return StoreRoot() / "leaderboards"; }

        // Best-effort read - a missing or corrupt file starts empty rather than throwing
        // (plans/plan_net.md Task 4.7's explicit requirement), since "no local record yet" and "record
        // is unreadable" both mean the same thing to a caller: nothing usable was persisted.
        std::optional<StoreJson> TryReadJsonFile(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return std::nullopt;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            try
            {
                return StoreJson::parse(buffer.str());
            }
            catch (const StoreJson::exception&)
            {
                return std::nullopt;
            }
        }

        void WriteJsonFile(const fs::path& path, const StoreJson& value)
        {
            fs::create_directories(path.parent_path());
            fs::path tmp = path;
            tmp += ".tmp";
            bool opened = false;
            try
            {
                std::ofstream out;
                out.exceptions(std::ios::failbit | std::ios::badbit);
                out.open(tmp, std::ios::binary | std::ios::trunc);
                opened = true;
                out << value.dump();
                out.flush();
                out.close();
                // Never truncate the previous record if replacement fails. This is an atomic
                // replacement on supported filesystems, not a power-loss durability guarantee.
                fs::rename(tmp, path);
            }
            catch (...)
            {
                if (opened)
                {
                    std::error_code ignored;
                    fs::remove(tmp, ignored);
                }
                throw;
            }
        }
    }

    std::string GetGamerServicesStoreRootEXT()
    {
        std::error_code ec;
        fs::create_directories(StoreRoot(), ec);
        return CNA::Internal::PathToGenericUtf8(StoreRoot());
    }

    std::string SanitizeStoreFileNameComponent(const std::string& raw)
    {
        std::string result;
        result.reserve(raw.size());
        for (const char c : raw)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')
            {
                result += c;
            }
            else
            {
                result += '_';
            }
        }
        if (result.empty())
        {
            result = "_";
        }
        return result;
    }

    std::string MakeLeaderboardFileKeyEXT(const std::string& leaderboardKeyName, int gameMode)
    {
        return SanitizeStoreFileNameComponent(leaderboardKeyName) + "_" + std::to_string(gameMode);
    }

    std::vector<PersistedAchievement> LoadEarnedAchievementsEXT(const std::string& gamertag)
    {
        std::vector<PersistedAchievement> result;
        const fs::path path = AchievementsDir() / (SanitizeStoreFileNameComponent(gamertag) + ".json");
        const auto doc = TryReadJsonFile(path);
        if (!doc.has_value())
        {
            return result;
        }
        const StoreJson* achievements = Find(*doc, "achievements");
        if (achievements == nullptr || !achievements->is_array())
        {
            return result;
        }
        for (const StoreJson& entry : *achievements)
        {
            const StoreJson* key = Find(entry, "key");
            const StoreJson* earnedTicks = Find(entry, "earnedTicks");
            if (key == nullptr || !key->is_string() || earnedTicks == nullptr || !earnedTicks->is_number())
            {
                continue;
            }
            PersistedAchievement record;
            record.Key = key->get_ref<const std::string&>();
            const auto ticks = Integer(earnedTicks);
            if (!ticks || *ticks < 0 || *ticks > 3155378975999999999LL) continue;
            record.EarnedTicks = *ticks;
            result.push_back(std::move(record));
        }
        return result;
    }

    void SaveEarnedAchievementEXT(const std::string& gamertag, const std::string& key, long long earnedTicks)
    {
        std::vector<PersistedAchievement> current = LoadEarnedAchievementsEXT(gamertag);

        bool updated = false;
        for (PersistedAchievement& record : current)
        {
            if (record.Key == key)
            {
                record.EarnedTicks = earnedTicks;
                updated = true;
                break;
            }
        }
        if (!updated)
        {
            current.push_back(PersistedAchievement{key, earnedTicks});
        }

        StoreJson achievementsArray = StoreJson::array();
        for (const PersistedAchievement& record : current)
        {
            StoreJson entry = StoreJson::object();
            entry["key"] = record.Key;
            entry["earnedTicks"] = record.EarnedTicks;
            achievementsArray.push_back(std::move(entry));
        }
        StoreJson root = StoreJson::object();
        root["achievements"] = std::move(achievementsArray);

        WriteJsonFile(AchievementsDir() / (SanitizeStoreFileNameComponent(gamertag) + ".json"), root);
    }

    namespace
    {
        // Task 4.2: PropertyDictionary's std::any values are only ever one of these 8 concrete
        // types (see PropertyDictionary::SetValue's own overload set) - Stream* is deliberately
        // excluded (and simply skipped when persisting), matching Achievement::GetPicture()'s own
        // "genuine platform unavailability" precedent: a live I/O handle has no meaningful
        // persisted form.
        enum class ColumnType { Int32, Int64, Double, Single, StringValue, DateTimeTicks, TimeSpanTicks, Outcome };

        StoreJson ColumnsToJson(const PropertyDictionary& columns)
        {
            StoreJson array = StoreJson::array();
            for (const auto& [key, value] : columns)
            {
                StoreJson entry = StoreJson::object();
                entry["key"] = key;

                if (value.type() == typeid(int))
                {
                    entry["type"] = "int32";
                    entry["value"] = std::any_cast<int>(value);
                }
                else if (value.type() == typeid(long long))
                {
                    entry["type"] = "int64";
                    entry["value"] = std::any_cast<long long>(value);
                }
                else if (value.type() == typeid(double))
                {
                    entry["type"] = "double";
                    entry["value"] = std::any_cast<double>(value);
                }
                else if (value.type() == typeid(float))
                {
                    entry["type"] = "single";
                    entry["value"] = static_cast<double>(std::any_cast<float>(value));
                }
                else if (value.type() == typeid(std::string))
                {
                    entry["type"] = "string";
                    entry["value"] = std::any_cast<std::string>(value);
                }
                else if (value.type() == typeid(System::DateTime))
                {
                    entry["type"] = "dateTime";
                    entry["value"] = std::any_cast<System::DateTime>(value).getTicksProperty();
                }
                else if (value.type() == typeid(System::TimeSpan))
                {
                    entry["type"] = "timeSpan";
                    entry["value"] = std::any_cast<System::TimeSpan>(value).getTicksProperty();
                }
                else if (value.type() == typeid(LeaderboardOutcome))
                {
                    entry["type"] = "outcome";
                    entry["value"] = static_cast<int>(std::any_cast<LeaderboardOutcome>(value));
                }
                else
                {
                    // Stream* (or any future unsupported type) - skip, not an error.
                    continue;
                }
                array.push_back(std::move(entry));
            }
            return array;
        }

        void JsonToColumns(const StoreJson& array, PropertyDictionary& outColumns)
        {
            if (!array.is_array())
            {
                return;
            }
            for (const StoreJson& entry : array)
            {
                const StoreJson* key = Find(entry, "key");
                const StoreJson* type = Find(entry, "type");
                const StoreJson* value = Find(entry, "value");
                if (key == nullptr || !key->is_string() || type == nullptr || !type->is_string() || value == nullptr)
                {
                    continue;
                }
                const std::string& t = type->get_ref<const std::string&>();
                const auto integer = Integer(value);
                if ((t == "int32" || t == "int64" || t == "dateTime" || t == "timeSpan" || t == "outcome") && !integer) continue;
                if (t == "int32" && (*integer < -2147483648LL || *integer > 2147483647LL)) continue;
                if (t == "dateTime" && (*integer < 0 || *integer > 3155378975999999999LL)) continue;
                if (t == "outcome" && (*integer < 0 || *integer > 3)) continue;
                if ((t == "single" || t == "double") && !value->is_number()) continue;
                if (t == "string" && !value->is_string()) continue;
                if (t == "int32")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), static_cast<int>(*integer));
                }
                else if (t == "int64")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), *integer);
                }
                else if (t == "double")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), value->get<double>());
                }
                else if (t == "single")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), static_cast<float>(value->get<double>()));
                }
                else if (t == "string")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), value->get_ref<const std::string&>());
                }
                else if (t == "dateTime")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), System::DateTime(static_cast<SharpRuntime::longcs>(*integer)));
                }
                else if (t == "timeSpan")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), System::TimeSpan(static_cast<SharpRuntime::longcs>(*integer)));
                }
                else if (t == "outcome")
                {
                    outColumns.SetValue(key->get_ref<const std::string&>(), static_cast<LeaderboardOutcome>(static_cast<int>(*integer)));
                }
                // Unrecognized type tag (e.g. from a future version): skip rather than throw -
                // matches the "corrupt/missing store never crashes" requirement.
            }
        }
    }

    std::vector<PersistedLeaderboardEntry> LoadLeaderboardEntriesEXT(const std::string& leaderboardFileKey)
    {
        std::vector<PersistedLeaderboardEntry> result;
        const fs::path path = LeaderboardsDir() / (leaderboardFileKey + ".json");
        const auto doc = TryReadJsonFile(path);
        if (!doc.has_value())
        {
            return result;
        }
        const StoreJson* entries = Find(*doc, "entries");
        if (entries == nullptr || !entries->is_array())
        {
            return result;
        }
        for (const StoreJson& entry : *entries)
        {
            const StoreJson* gamertag = Find(entry, "gamertag");
            const StoreJson* rating = Find(entry, "rating");
            if (gamertag == nullptr || !gamertag->is_string() || rating == nullptr || !rating->is_number())
            {
                continue;
            }
            PersistedLeaderboardEntry record;
            record.Gamertag = gamertag->get_ref<const std::string&>();
            const auto number = Integer(rating);
            if (!number) continue;
            record.Rating = *number;
            result.push_back(std::move(record));
        }
        return result;
    }

    void SaveLeaderboardEntryEXT(
        const std::string& leaderboardFileKey,
        const PersistedLeaderboardEntry& entry,
        const PropertyDictionary* columns
    ) {
        const fs::path path = LeaderboardsDir() / (leaderboardFileKey + ".json");
        auto doc = TryReadJsonFile(path);
        StoreJson root = doc.value_or(StoreJson::object());
        if (!root.is_object())
        {
            root = StoreJson::object();
        }

        const auto* existingEntries = Find(root, "entries");
        StoreJson entriesArray = existingEntries && existingEntries->is_array()
            ? *existingEntries : StoreJson::array();

        bool updated = false;
        for (StoreJson& existing : entriesArray)
        {
            const StoreJson* gamertag = Find(existing, "gamertag");
            if (gamertag != nullptr && gamertag->is_string() && gamertag->get_ref<const std::string&>() == entry.Gamertag)
            {
                existing["rating"] = entry.Rating;
                existing["columns"] = columns != nullptr ? ColumnsToJson(*columns) : StoreJson::array();
                updated = true;
                break;
            }
        }
        if (!updated)
        {
            StoreJson newEntry = StoreJson::object();
            newEntry["gamertag"] = entry.Gamertag;
            newEntry["rating"] = entry.Rating;
            newEntry["columns"] = columns != nullptr ? ColumnsToJson(*columns) : StoreJson::array();
            entriesArray.push_back(std::move(newEntry));
        }

        root["entries"] = std::move(entriesArray);
        WriteJsonFile(path, root);
    }

    void LoadLeaderboardEntryColumnsEXT(
        const std::string& leaderboardFileKey,
        const std::string& gamertag,
        PropertyDictionary& outColumns
    ) {
        const fs::path path = LeaderboardsDir() / (leaderboardFileKey + ".json");
        const auto doc = TryReadJsonFile(path);
        if (!doc.has_value())
        {
            return;
        }
        const StoreJson* entries = Find(*doc, "entries");
        if (entries == nullptr || !entries->is_array())
        {
            return;
        }
        for (const StoreJson& entry : *entries)
        {
            const StoreJson* entryGamertag = Find(entry, "gamertag");
            if (entryGamertag == nullptr || !entryGamertag->is_string() || entryGamertag->get_ref<const std::string&>() != gamertag)
            {
                continue;
            }
            const StoreJson* columns = Find(entry, "columns");
            if (columns != nullptr)
            {
                JsonToColumns(*columns, outColumns);
            }
            return;
        }
    }

    void ResetStoreForTestingEXT()
    {
        std::error_code ec;
        fs::remove_all(StoreRoot(), ec);
    }

    namespace
    {
        std::optional<std::optional<std::vector<OfflineAchievementDefinition>>>& OfflineCatalog()
        {
            static std::optional<std::optional<std::vector<OfflineAchievementDefinition>>> catalog;
            return catalog;
        }

        [[noreturn]] void RefuseCatalog(const std::string& problem)
        {
            throw System::InvalidOperationException(
                std::string("The offline achievement catalog ") + OfflineAchievementCatalogPath + " is invalid: " + problem + ".");
        }

        // The same key rule as the service's catalog identifiers.
        bool IsCatalogKey(const std::string& key)
        {
            if (key.empty() || key.size() > 64) return false;
            return std::all_of(key.begin(), key.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-' || c == '.';
            });
        }

        std::string CatalogText(const JsonValue& entry, const char* name, std::size_t limit, bool required)
        {
            const JsonValue* value = entry.FindMember(name);
            if (value == nullptr)
            {
                if (required) RefuseCatalog(std::string("an entry has no \"") + name + "\"");
                return {};
            }
            if (!value->IsString() || value->stringValue.size() > limit)
                RefuseCatalog(std::string("\"") + name + "\" must be text of at most " + std::to_string(limit) + " bytes");
            return value->stringValue;
        }

        // A title-relative PNG path that cannot leave the title's directory.
        bool IsTitlePicturePath(const std::string& path)
        {
            if (path.empty() || path.size() > 256 || path.front() == '/' || path.front() == '\\' ||
                path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
                return false;
            const fs::path parsed(path);
            for (const auto& part : parsed)
                if (part == "..") return false;
            std::string extension = parsed.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".png";
        }

        std::vector<OfflineAchievementDefinition> ParseOfflineCatalog(const std::string& text)
        {
            JsonValue root;
            try { root = ParseJson(text); }
            catch (const JsonParseException&) { RefuseCatalog("it is not JSON"); }
            if (root.type != JsonType::Array) RefuseCatalog("it must be an array of achievements");
            if (root.arrayValue.size() > 1024) RefuseCatalog("it lists more than 1024 achievements");
            std::vector<OfflineAchievementDefinition> definitions;
            for (const JsonValue& entry : root.arrayValue)
            {
                if (!entry.IsObject()) RefuseCatalog("every achievement must be an object");
                OfflineAchievementDefinition definition;
                definition.Key = CatalogText(entry, "key", 64, true);
                if (!IsCatalogKey(definition.Key)) RefuseCatalog("\"" + definition.Key + "\" is not a valid key");
                for (const auto& earlier : definitions)
                    if (earlier.Key == definition.Key) RefuseCatalog("\"" + definition.Key + "\" is listed twice");
                definition.Name = CatalogText(entry, "name", 128, false);
                definition.Description = CatalogText(entry, "description", 1024, false);
                definition.HowToEarn = CatalogText(entry, "howToEarn", 1024, false);
                definition.Picture = CatalogText(entry, "picture", 256, false);
                if (!definition.Picture.empty() && !IsTitlePicturePath(definition.Picture))
                    RefuseCatalog("the picture of \"" + definition.Key + "\" must be a PNG path inside the title");
                const JsonValue* score = entry.FindMember("score");
                if (score == nullptr || !score->IsNumber() || score->numberValue < 0 || score->numberValue > 1000 ||
                    score->numberValue != static_cast<double>(static_cast<int>(score->numberValue)))
                    RefuseCatalog("the score of \"" + definition.Key + "\" must be a whole number from 0 to 1000");
                definition.Score = static_cast<int>(score->numberValue);
                if (const JsonValue* display = entry.FindMember("display"))
                {
                    if (display->type != JsonType::Boolean) RefuseCatalog("\"display\" must be true or false");
                    definition.DisplayBeforeEarned = display->boolValue;
                }
                definitions.push_back(std::move(definition));
            }
            return definitions;
        }
    }

    const std::optional<std::vector<OfflineAchievementDefinition>>& LoadOfflineAchievementCatalogEXT()
    {
        auto& catalog = OfflineCatalog();
        if (catalog) return *catalog;
        std::unique_ptr<System::IO::Stream> stream;
        try
        {
            stream = Microsoft::Xna::Framework::TitleContainer::OpenStream(OfflineAchievementCatalogPath);
        }
        catch (const std::runtime_error&)
        {
            catalog.emplace();
            return *catalog;
        }
        std::string text;
        std::vector<SharpRuntime::bytecs> buffer(4096);
        for (;;)
        {
            const auto read = stream->Read(buffer.data(), 0, static_cast<SharpRuntime::intcs>(buffer.size()));
            if (read <= 0) break;
            text.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(read));
            if (text.size() > 1024 * 1024) RefuseCatalog("it is larger than 1 MiB");
        }
        catalog.emplace(ParseOfflineCatalog(text));
        return *catalog;
    }

    void ResetOfflineAchievementCatalogForTestingEXT()
    {
        OfflineCatalog().reset();
    }
}
