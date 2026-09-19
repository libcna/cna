// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

namespace CNA::Inspector::Detail
{
    [[nodiscard]] std::string_view InspectorIndexHtml() noexcept;
    [[nodiscard]] std::string_view InspectorStyleCss() noexcept;
    [[nodiscard]] std::string_view InspectorAppJavaScript() noexcept;
}
