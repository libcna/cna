// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "Protocol/CnaService/Protocol.hpp"
#include <functional>
#include <memory>

namespace CNA::Internal::GamerServices {
using DirectoryRequest=std::function<CnaService::Json(const std::string&,CnaService::Json,
    const std::string&,const std::vector<std::string>&)>;
/** @brief Creates typed control with validated responses. @param request Authenticated transport adapter.
 * @param capability Whether the connected service advertises a capability (empty: none optional).
 * @return Directory client with no public wire data. */
std::unique_ptr<IServiceSessionDirectory> makeSessionDirectoryClient(DirectoryRequest request,
    std::function<bool(const std::string&)> capability={});
}
