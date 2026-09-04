#!/bin/sh
# Fetches the pinned three.js builds this spike runs against, into vendor/.
#
# Two artifacts are fetched deliberately:
#   * three@0.185.1  build/three.module.min.js + build/three.core.min.js -- the CURRENT shape:
#     ESM only, and a two-file relative graph.
#   * three@0.160.1  build/three.min.js -- the LAST release that shipped a UMD classic script,
#     kept so the "just use the UMD build like PIXIJS does" option can be inspected rather than
#     argued about. The file itself prints a deprecation warning saying it is removed with r160.
#
# Uses the npm registry, not jsDelivr: cdn.jsdelivr.net is blocked by the outbound proxy in this
# environment, which cmake/ThirdPartyPixiJS.cmake records hitting too.
set -e
cd "$(dirname "$0")"
mkdir -p vendor
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fetch() {
    version=$1
    shift
    (cd "$tmp" && npm pack "three@$version" --silent >/dev/null)
    for file in "$@"; do
        tar -xzf "$tmp"/three-"$version".tgz -C "$tmp" "package/build/$file"
    done
}

fetch 0.185.1 three.module.min.js three.core.min.js
cp "$tmp/package/build/three.module.min.js" "$tmp/package/build/three.core.min.js" vendor/

fetch 0.160.1 three.min.js
cp "$tmp/package/build/three.min.js" vendor/three-0.160.1.umd.min.js

echo "vendor/ populated:"
ls -l vendor/
