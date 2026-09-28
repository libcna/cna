// SPDX-License-Identifier: MS-PL
#include "CnaService/Protocol.hpp"
#include <set>
#include <vector>

namespace CnaService {
Error::Error(std::string code) : std::runtime_error(code), code_(std::move(code)) {}
const std::string& Error::code() const noexcept { return code_; }
bool identifier(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}
Json parse(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > MaxMessageBytes) throw Error("LIMIT_EXCEEDED");
    std::vector<std::set<std::string>> keys;
    try {
        return Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 16) throw Error("LIMIT_EXCEEDED");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                throw Error("MALFORMED_MESSAGE");
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            if (event == Json::parse_event_t::array_end && value.size() > 256) throw Error("LIMIT_EXCEEDED");
            return true;
        });
    } catch (const Error&) { throw; }
      catch (const Json::exception&) { throw Error("MALFORMED_MESSAGE"); }
}
std::string stringField(const Json& value, std::string_view key, std::size_t maximum) {
    auto it = value.find(std::string(key));
    if (it == value.end() || !it->is_string()) throw Error("INVALID_ARGUMENT");
    const auto& text = it->get_ref<const std::string&>();
    if (text.size() > maximum || text.find('\0') != std::string::npos) throw Error("INVALID_ARGUMENT");
    return text;
}
void validateRequest(const Json& request) {
    if (!request.is_object() || request.size() > 6) throw Error("MALFORMED_MESSAGE");
    static const std::set<std::string> fields{"v","id","game","op","token","args"};
    for (auto it=request.begin(); it!=request.end(); ++it)
        if (!fields.contains(it.key())) throw Error("MALFORMED_MESSAGE");
    if (!request.contains("v") || !request["v"].is_number_integer()) throw Error("MALFORMED_MESSAGE");
    if (request["v"] != ProtocolVersion) throw Error("UNSUPPORTED_VERSION");
    for (auto key : {"id", "game", "op"})
        if (!identifier(stringField(request,key,64))) throw Error("INVALID_ARGUMENT");
    if (!request.contains("args") || !request["args"].is_object()) throw Error("INVALID_ARGUMENT");
    if (request.contains("token")) (void)stringField(request,"token",128);
}
Json response(std::string_view id, std::string_view error, Json result) {
    return Json{{"v",ProtocolVersion},{"id",id},{"error",error},{"result",std::move(result)}};
}
}
