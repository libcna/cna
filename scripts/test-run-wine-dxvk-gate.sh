#!/usr/bin/env bash
# GLTF-386: hermetic regression for run-wine-dxvk.sh's authenticity gate. No Wine, display or GPU
# is needed: a PATH-local stub emits the exact marker forms seen from official and packaged DXVK.
set -euo pipefail

scriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
wrapper="${scriptDir}/run-wine-dxvk.sh"
testRoot="$(mktemp -d "${TMPDIR:-/tmp}/cna-dxvk-gate-test.XXXXXX")"
trap 'rm -rf "${testRoot}"' EXIT

mkdir -p "${testRoot}/bin" "${testRoot}/prefix"
touch "${testRoot}/prefix/system.reg"

cat > "${testRoot}/bin/wine" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "${CNA_DXVK_TEST_OUTPUT:-}"
exit "${CNA_DXVK_TEST_EXIT:-0}"
EOF
chmod +x "${testRoot}/bin/wine"

runWrapper()
{
    env PATH="${testRoot}/bin:${PATH}" \
        TMPDIR="${testRoot}" \
        CNA_D3D11_WINEPREFIX="${testRoot}/prefix" \
        CNA_DXVK_TEST_OUTPUT="$1" \
        CNA_DXVK_TEST_EXIT="${2:-0}" \
        bash "${wrapper}" ignored.exe >/dev/null 2>&1
}

runWrapper "info: DXVK: v2.6"
runWrapper "info: DXVK: 2.6.0"

set +e
runWrapper "info: WineD3D renderer" 0
missingMarkerStatus=$?
runWrapper "info: DXVK: v2.6" 7
applicationStatus=$?
set -e

if [[ ${missingMarkerStatus} -ne 3 ]]; then
    echo "expected missing DXVK marker to exit 3, got ${missingMarkerStatus}" >&2
    exit 1
fi
if [[ ${applicationStatus} -ne 7 ]]; then
    echo "expected the wrapped application exit 7 to be preserved, got ${applicationStatus}" >&2
    exit 1
fi

echo "run-wine-dxvk gate: marker variants and exit propagation PASS"
