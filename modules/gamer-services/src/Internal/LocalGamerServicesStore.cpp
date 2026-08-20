// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"

#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

#include <any>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace CNA::Internal::GamerServices
{
    namespace fs = std::filesystem;
    using Microsoft::Xna::Framework::GamerServices::LeaderboardOutcome;
    using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;

    namespace
    {
        // Task 4.2: reuses this codebase's existing user-data-directory convention
        // (StorageDevice::GetStorageRootEXT(), platform user-data-directory backed) rather than inventing a
        // new one - a plain "GamerServices" subdirectory under it.
        fs::path StoreRoot()
        {
            return fs::path(Microsoft::Xna::Framework::Storage::StorageDevice::GetStorageRootEXT()) / "GamerServices";
        }

        fs::path AchievementsDir() { return StoreRoot() / "achievements"; }
        fs::path LeaderboardsDir() { return StoreRoot() / "leaderboards"; }

        // Best-effort read - a missing or corrupt file starts empty rather than throwing
        // (plans/plan_net.md Task 4.7's explicit requirement), since "no local record yet" and "record
        // is unreadable" both mean the same thing to a caller: nothing usable was persisted.
        std::optional<CNA::Internal::JsonValue> TryReadJsonFile(const fs::path& path)
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
                return CNA::Internal::ParseJson(buffer.str());
            }
            catch (const CNA::Internal::JsonParseException&)
            {
                return std::nullopt;
            }
        }

        void WriteJsonFile(const fs::path& path, const CNA::Internal::JsonValue& value)
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            // Write to a temp file then rename, so a crash/power-loss mid-write can never leave
            // a half-written, unparseable store file behind - a fresh process would otherwise
            // permanently lose every previously-earned achievement/leaderboard entry to a single
            // torn write, not just the one being written at the time.
            const fs::path tmp = path.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                out << CNA::Internal::WriteJson(value);
            }
            fs::rename(tmp, path, ec);
            if (ec)
            {
                // Cross-filesystem temp dirs can make rename() fail; fall back to a direct write
                // rather than silently losing the update.
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << CNA::Internal::WriteJson(value);
            }
        }
    }

    std::string GetGamerServicesStoreRootEXT()
    {
        std::error_code ec;
        fs::create_directories(StoreRoot(), ec);
        return StoreRoot().string();
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
        const CNA::Internal::JsonValue* achievements = doc->FindMember("achievements");
        if (achievements == nullptr || achievements->type != CNA::Internal::JsonType::Array)
        {
            return result;
        }
        for (const CNA::Internal::JsonValue& entry : achievements->arrayValue)
        {
            const CNA::Internal::JsonValue* key = entry.FindMember("key");
            const CNA::Internal::JsonValue* earnedTicks = entry.FindMember("earnedTicks");
            if (key == nullptr || !key->IsString() || earnedTicks == nullptr || !earnedTicks->IsNumber())
            {
                continue;
            }
            PersistedAchievement record;
            record.Key = key->stringValue;
            record.EarnedTicks = static_cast<long long>(earnedTicks->numberValue);
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

        CNA::Internal::JsonValue achievementsArray = CNA::Internal::JsonValue::MakeArray();
        for (const PersistedAchievement& record : current)
        {
            CNA::Internal::JsonValue entry = CNA::Internal::JsonValue::MakeObject();
            entry.Set("key", CNA::Internal::JsonValue::MakeString(record.Key));
            entry.Set("earnedTicks", CNA::Internal::JsonValue::MakeNumber(static_cast<double>(record.EarnedTicks)));
            achievementsArray.arrayValue.push_back(std::move(entry));
        }
        CNA::Internal::JsonValue root = CNA::Internal::JsonValue::MakeObject();
        root.Set("achievements", std::move(achievementsArray));

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

        CNA::Internal::JsonValue ColumnsToJson(const PropertyDictionary& columns)
        {
            CNA::Internal::JsonValue array = CNA::Internal::JsonValue::MakeArray();
            for (const auto& [key, value] : columns)
            {
                CNA::Internal::JsonValue entry = CNA::Internal::JsonValue::MakeObject();
                entry.Set("key", CNA::Internal::JsonValue::MakeString(key));

                if (value.type() == typeid(int))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("int32"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(std::any_cast<int>(value)));
                }
                else if (value.type() == typeid(long long))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("int64"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(static_cast<double>(std::any_cast<long long>(value))));
                }
                else if (value.type() == typeid(double))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("double"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(std::any_cast<double>(value)));
                }
                else if (value.type() == typeid(float))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("single"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(static_cast<double>(std::any_cast<float>(value))));
                }
                else if (value.type() == typeid(std::string))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("string"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeString(std::any_cast<std::string>(value)));
                }
                else if (value.type() == typeid(System::DateTime))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("dateTime"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(
                        static_cast<double>(std::any_cast<System::DateTime>(value).getTicksProperty())));
                }
                else if (value.type() == typeid(System::TimeSpan))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("timeSpan"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(
                        static_cast<double>(std::any_cast<System::TimeSpan>(value).getTicksProperty())));
                }
                else if (value.type() == typeid(LeaderboardOutcome))
                {
                    entry.Set("type", CNA::Internal::JsonValue::MakeString("outcome"));
                    entry.Set("value", CNA::Internal::JsonValue::MakeNumber(
                        static_cast<double>(std::any_cast<LeaderboardOutcome>(value))));
                }
                else
                {
                    // Stream* (or any future unsupported type) - skip, not an error.
                    continue;
                }
                array.arrayValue.push_back(std::move(entry));
            }
            return array;
        }

        void JsonToColumns(const CNA::Internal::JsonValue& array, PropertyDictionary& outColumns)
        {
            if (array.type != CNA::Internal::JsonType::Array)
            {
                return;
            }
            for (const CNA::Internal::JsonValue& entry : array.arrayValue)
            {
                const CNA::Internal::JsonValue* key = entry.FindMember("key");
                const CNA::Internal::JsonValue* type = entry.FindMember("type");
                const CNA::Internal::JsonValue* value = entry.FindMember("value");
                if (key == nullptr || !key->IsString() || type == nullptr || !type->IsString() || value == nullptr)
                {
                    continue;
                }
                const std::string& t = type->stringValue;
                if (t == "int32")
                {
                    outColumns.SetValue(key->stringValue, static_cast<int>(value->numberValue));
                }
                else if (t == "int64")
                {
                    outColumns.SetValue(key->stringValue, static_cast<long long>(value->numberValue));
                }
                else if (t == "double")
                {
                    outColumns.SetValue(key->stringValue, value->numberValue);
                }
                else if (t == "single")
                {
                    outColumns.SetValue(key->stringValue, static_cast<float>(value->numberValue));
                }
                else if (t == "string")
                {
                    outColumns.SetValue(key->stringValue, value->stringValue);
                }
                else if (t == "dateTime")
                {
                    outColumns.SetValue(key->stringValue, System::DateTime(static_cast<SharpRuntime::longcs>(value->numberValue)));
                }
                else if (t == "timeSpan")
                {
                    outColumns.SetValue(key->stringValue, System::TimeSpan(static_cast<SharpRuntime::longcs>(value->numberValue)));
                }
                else if (t == "outcome")
                {
                    outColumns.SetValue(key->stringValue, static_cast<LeaderboardOutcome>(static_cast<int>(value->numberValue)));
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
        const CNA::Internal::JsonValue* entries = doc->FindMember("entries");
        if (entries == nullptr || entries->type != CNA::Internal::JsonType::Array)
        {
            return result;
        }
        for (const CNA::Internal::JsonValue& entry : entries->arrayValue)
        {
            const CNA::Internal::JsonValue* gamertag = entry.FindMember("gamertag");
            const CNA::Internal::JsonValue* rating = entry.FindMember("rating");
            if (gamertag == nullptr || !gamertag->IsString() || rating == nullptr || !rating->IsNumber())
            {
                continue;
            }
            PersistedLeaderboardEntry record;
            record.Gamertag = gamertag->stringValue;
            record.Rating = static_cast<long long>(rating->numberValue);
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
        CNA::Internal::JsonValue root = doc.value_or(CNA::Internal::JsonValue::MakeObject());
        if (root.type != CNA::Internal::JsonType::Object)
        {
            root = CNA::Internal::JsonValue::MakeObject();
        }

        CNA::Internal::JsonValue* existingEntries = nullptr;
        for (auto& [memberKey, memberValue] : root.objectValue)
        {
            if (memberKey == "entries") { existingEntries = &memberValue; break; }
        }
        CNA::Internal::JsonValue entriesArray = (existingEntries != nullptr && existingEntries->type == CNA::Internal::JsonType::Array)
            ? *existingEntries
            : CNA::Internal::JsonValue::MakeArray();

        bool updated = false;
        for (CNA::Internal::JsonValue& existing : entriesArray.arrayValue)
        {
            const CNA::Internal::JsonValue* gamertag = existing.FindMember("gamertag");
            if (gamertag != nullptr && gamertag->IsString() && gamertag->stringValue == entry.Gamertag)
            {
                existing.Set("rating", CNA::Internal::JsonValue::MakeNumber(static_cast<double>(entry.Rating)));
                existing.Set("columns", columns != nullptr ? ColumnsToJson(*columns) : CNA::Internal::JsonValue::MakeArray());
                updated = true;
                break;
            }
        }
        if (!updated)
        {
            CNA::Internal::JsonValue newEntry = CNA::Internal::JsonValue::MakeObject();
            newEntry.Set("gamertag", CNA::Internal::JsonValue::MakeString(entry.Gamertag));
            newEntry.Set("rating", CNA::Internal::JsonValue::MakeNumber(static_cast<double>(entry.Rating)));
            newEntry.Set("columns", columns != nullptr ? ColumnsToJson(*columns) : CNA::Internal::JsonValue::MakeArray());
            entriesArray.arrayValue.push_back(std::move(newEntry));
        }

        root.Set("entries", std::move(entriesArray));
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
        const CNA::Internal::JsonValue* entries = doc->FindMember("entries");
        if (entries == nullptr || entries->type != CNA::Internal::JsonType::Array)
        {
            return;
        }
        for (const CNA::Internal::JsonValue& entry : entries->arrayValue)
        {
            const CNA::Internal::JsonValue* entryGamertag = entry.FindMember("gamertag");
            if (entryGamertag == nullptr || !entryGamertag->IsString() || entryGamertag->stringValue != gamertag)
            {
                continue;
            }
            const CNA::Internal::JsonValue* columns = entry.FindMember("columns");
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
}
