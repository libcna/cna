// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CnaService {
using Json = nlohmann::json;
/** @brief Current service protocol major version. */
inline constexpr int ProtocolVersion = 1;
/** @brief Maximum encoded control request or response size in bytes. */
inline constexpr std::size_t MaxMessageBytes = 65536;
/** @brief Fixed nullable matchmaking property slots in protocol v1. */
inline constexpr std::size_t SessionPropertyCount = 8;
/** @brief Maximum session membership supported by protocol v1. */
inline constexpr int MaxSessionGamers = 31;
/** @brief A stable protocol failure without credential-bearing diagnostics. */
class Error : public std::runtime_error {
public:
    /** @brief Constructs an error with its stable code. @param code Protocol error code. */
    explicit Error(std::string code);
    /** @brief Returns the stable protocol code. @return Error code. */
    const std::string& code() const noexcept;
private:
    std::string code_;
};
/** @brief Parses bounded UTF-8 JSON, rejecting duplicate keys and excessive nesting.
 * @param bytes Encoded envelope. @return Valid JSON value. */
Json parse(std::string_view bytes);
/** @brief Validates a v1 request before dispatch. @param request Parsed envelope. */
void validateRequest(const Json& request);
/** @brief Validates an identifier. @param value Identifier bytes. @return Whether accepted. */
bool identifier(std::string_view value);
/** @brief Gets a bounded string field. @param value Source object. @param key Field name.
 * @param maximum Byte limit. @return Field value. */
std::string stringField(const Json& value, std::string_view key, std::size_t maximum);
/** @brief Validates the fixed nullable signed-32-bit property array.
 * @param properties Property values. */
void validateSessionProperties(const Json& properties);
/** @brief Constructs a response. @param id Request ID. @param error Stable error code.
 * @param result Result data. @return Response envelope. */
Json response(std::string_view id, std::string_view error, Json result = Json::object());
}
