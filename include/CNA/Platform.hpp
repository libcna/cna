//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace CNA {

    enum class Platform {
        Desktop,
        Android,
        iOS,
        Web
    };
    Platform getCurrentPlatform();

} // CNA
