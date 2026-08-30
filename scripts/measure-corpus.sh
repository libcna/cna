#!/usr/bin/env bash
# Measure the CNA study corpus. Baseline generator for plan_study20.md Appendix A.
#
# Every number in plan_study20.md comes from this script. Re-run it in each
# reserve week: if the production line count grew without a matching removal,
# plan_study20.md section 5 was violated and that is the week's finding.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SR="${SHARP_RUNTIME_ROOT:-$REPO/../sharp-runtime}"
cd "$REPO"

prod() { find modules -type f \( -name '*.cpp' -o -name '*.hpp' \) \
           -not -path '*/tests/*' -not -path '*/examples/*' "$@"; }
lines() { xargs cat 2>/dev/null | wc -l; }

echo "=== CNA study corpus — $(date +%F) — $(git rev-parse --short HEAD) ($(git branch --show-current)) ==="
echo

printf '%-38s %9s %7s\n' 'COMPONENT' 'LINES' 'FILES'
printf '%-38s %9s %7s\n' 'CNA production' "$(prod | lines)" "$(prod | wc -l)"
printf '%-38s %9s %7s\n' '  renderers' \
  "$(prod -path 'modules/renderers/*' | lines)" "$(prod -path 'modules/renderers/*' | wc -l)"
printf '%-38s %9s %7s\n' '  c-api' \
  "$(prod -path 'modules/c-api/*' | lines)" "$(prod -path 'modules/c-api/*' | wc -l)"
printf '%-38s %9s %7s\n' '  all other modules' \
  "$(prod -not -path 'modules/renderers/*' -not -path 'modules/c-api/*' | lines)" \
  "$(prod -not -path 'modules/renderers/*' -not -path 'modules/c-api/*' | wc -l)"

if [ -d "$SR" ]; then
  srf() { find "$SR" \( -name '*.cpp' -o -name '*.hpp' \) \
            -not -path '*/build*' -not -path '*/tests/*' \
            -not -path '*/test/*' -not -path '*/bench/*' -not -path '*/vendor/*'; }
  printf '%-38s %9s %7s\n' 'sharp-runtime production' "$(srf | lines)" "$(srf | wc -l)"
  printf '%-38s %9s %7s\n' '  System:: types used by CNA' \
    "$(grep -rhoE '\bSystem::[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)?' \
        modules --include='*.cpp' --include='*.hpp' 2>/dev/null | sort -u | wc -l)" '-'
else
  echo "sharp-runtime not found at $SR (set SHARP_RUNTIME_ROOT)"
fi

t() { find modules "$1" -type f -path "*$1*" \
        \( -name '*.cpp' -o -name '*.hpp' \) 2>/dev/null; }
printf '%-38s %9s %7s\n' 'CNA tests'    "$(t tests | lines)" "$(t tests | wc -l)"
printf '%-38s %9s %7s\n' 'CNA examples' "$(t examples | lines)" "$(t examples | wc -l)"
printf '%-38s %9s %7s\n' 'docs/*.md'    "$(ls docs/*.md | lines)"       "$(ls docs/*.md | wc -l)"
printf '%-38s %9s %7s\n' 'plan*.md'     "$(ls plans/plan*.md plan*.md 2>/dev/null | lines)" '-'
echo
printf '%-38s %9s\n' 'renderer families' "$(( $(ls -d modules/renderers/*/ | wc -l) - 1 ))"
printf '%-38s %9s\n' 'known_bugs.md open entries' "$(grep -cE '^## ' known_bugs.md || echo 0)"
printf '%-38s %9s\n' 'TODO/FIXME/XXX/HACK in production' \
  "$(prod | xargs grep -cE 'TODO|FIXME|XXX|HACK' 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
printf '%-38s %9s\n' 'develop -> HEAD commits' "$(git rev-list --count develop..HEAD 2>/dev/null || echo '-')"
echo
echo "=== per-module (production) ==="
for m in $(ls -d modules/*/ | sed 's#modules/##;s#/##' | grep -v '^renderers$'); do
  printf '%-20s %9s\n' "$m" "$(prod -path "modules/$m/*" | lines)"
done | sort -k2 -rn
echo
echo "=== per-renderer (production) ==="
for r in $(ls -d modules/renderers/*/ | sed 's#.*/renderers/##;s#/##'); do
  printf '%-20s %9s\n' "$r" "$(prod -path "modules/renderers/$r/*" | lines)"
done | sort -k2 -rn
