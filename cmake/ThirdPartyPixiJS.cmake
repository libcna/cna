# PixiJS integration for the CNA PIXIJS graphics renderer (plans/plan_pixijs.md).
#
# The preferred input is a local, pinned PixiJS v7 UMD build (plans/plan_pixijs.md Design decision 3):
#   cmake -DCNA_GRAPHICS_RENDERER=PIXIJS -DCNA_PIXIJS_ROOT=/path/to/pixi.min.js ...
#
# When CNA_PIXIJS_ROOT is empty and CNA_PIXIJS_AUTO_DOWNLOAD is ON, CMake downloads the pinned
# official pixi.js@7 UMD release, verified against a pinned SHA256 (Design decision 4) -- never a
# runtime <script src="https://..."> tag, so the built game never depends on a third-party CDN
# being reachable at play time.

include_guard(GLOBAL)

set(CNA_PIXIJS_VERSION "7.4.2" CACHE STRING "Pinned PixiJS release used by CNA_RENDERER_PIXIJS")
set(CNA_PIXIJS_ROOT "" CACHE FILEPATH "Path to a local, pinned pixi.min.js (PixiJS v7 UMD build)")
option(CNA_PIXIJS_AUTO_DOWNLOAD "Download the pinned PixiJS UMD build when CNA_PIXIJS_ROOT is empty" ON)

# SHA256 of the official jsDelivr-distributed pixi.min.js for CNA_PIXIJS_VERSION. Update this
# alongside CNA_PIXIJS_VERSION -- file(DOWNLOAD ... EXPECTED_HASH) refuses to accept a file whose
# hash does not match, so a stale pin fails the configure loudly rather than silently linking an
# unverified/unexpected build.
#
# plans/plan_pixijs.md PIXIJS-1: computed from the real, official `pixi.js@7.4.2` npm package's
# `dist/pixi.min.js` (`npm pack pixi.js@7.4.2`, then `sha256sum package/dist/pixi.min.js`), and
# re-confirmed by an independent fresh `npm pack` on 2026-08-17 that reproduced it byte for byte.
#
# It has still not been checked against the jsDelivr URL cna_configure_pixijs() downloads from:
# both sessions that computed it had cdn.jsdelivr.net blocked by an outbound proxy (403 on CONNECT)
# while registry.npmjs.org was reachable. jsDelivr's npm mirror serves package contents unmodified,
# so this should match -- but if a real jsDelivr download ever produces a different hash, trust the
# mismatch (do not widen this pin) and investigate before proceeding.
set(CNA_PIXIJS_SHA256 "9ddba9cd78bc8610a1d445ec939393888be83925c78e40d66d9a17e98450228d" CACHE STRING "Expected SHA256 of the downloaded pixi.min.js")

# Sets CNA_PIXIJS_JS_FILE (cache-visible to the calling renderer's own CMakeLists.txt) to a real,
# on-disk pixi.min.js -- either the caller-supplied CNA_PIXIJS_ROOT, or a freshly verified download.
function(cna_configure_pixijs)
    if(CNA_PIXIJS_ROOT)
        if(NOT EXISTS "${CNA_PIXIJS_ROOT}")
            message(FATAL_ERROR "CNA PixiJS: CNA_PIXIJS_ROOT ('${CNA_PIXIJS_ROOT}') does not exist.")
        endif()
        set(CNA_PIXIJS_JS_FILE "${CNA_PIXIJS_ROOT}" CACHE INTERNAL "Resolved pixi.min.js path")
        return()
    endif()

    if(NOT CNA_PIXIJS_AUTO_DOWNLOAD)
        message(FATAL_ERROR
            "CNA PixiJS: CNA_PIXIJS_ROOT is empty and CNA_PIXIJS_AUTO_DOWNLOAD=OFF. Obtain a "
            "pinned PixiJS v${CNA_PIXIJS_VERSION} UMD build (pixi.min.js) and set CNA_PIXIJS_ROOT "
            "to it.")
    endif()

    # CNA_PIXIJS_SHA256 is pinned above; this only fires if a caller deliberately blanks it, which
    # would turn the download into an unverified one.
    if(NOT CNA_PIXIJS_SHA256)
        message(FATAL_ERROR
            "CNA PixiJS: CNA_PIXIJS_AUTO_DOWNLOAD is ON but CNA_PIXIJS_SHA256 is empty, so the "
            "download could not be verified. Restore the pinned hash for PixiJS "
            "v${CNA_PIXIJS_VERSION}, supply the one matching a different CNA_PIXIJS_VERSION, or set "
            "CNA_PIXIJS_ROOT to a local pixi.min.js instead.")
    endif()

    set(_download_dir "${CMAKE_BINARY_DIR}/_deps/pixijs-${CNA_PIXIJS_VERSION}")
    set(_js_file "${_download_dir}/pixi.min.js")
    if(NOT EXISTS "${_js_file}")
        set(_url "https://cdn.jsdelivr.net/npm/pixi.js@${CNA_PIXIJS_VERSION}/dist/pixi.min.js")
        message(STATUS "CNA PixiJS: downloading ${_url}")
        file(MAKE_DIRECTORY "${_download_dir}")
        file(DOWNLOAD "${_url}" "${_js_file}"
            SHOW_PROGRESS
            STATUS _download_status
            EXPECTED_HASH SHA256=${CNA_PIXIJS_SHA256}
            TLS_VERIFY ON)
        list(GET _download_status 0 _download_code)
        list(GET _download_status 1 _download_message)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${_js_file}")
            message(FATAL_ERROR
                "CNA PixiJS: failed to download pixi.min.js (${_download_message}). Download it "
                "manually and set CNA_PIXIJS_ROOT to its path.")
        endif()
    endif()

    set(CNA_PIXIJS_JS_FILE "${_js_file}" CACHE INTERNAL "Resolved pixi.min.js path")
endfunction()
