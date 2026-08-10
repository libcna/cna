#!/bin/bash
# Apply one move-map group as pure git mv operations. Usage: apply_moves.sh <group>
set -euo pipefail
cd "$(dirname "$0")/../.."
group="$1"
count=0
while IFS=$'\t' read -r old new g; do
    [ "$g" = "$group" ] || continue
    mkdir -p "$(dirname "$new")"
    git mv "$old" "$new"
    count=$((count+1))
done < modularization/physical-modules/move-map.tsv
echo "moved $count files for group $group"
git status --short | awk '{print $1}' | sort | uniq -c
