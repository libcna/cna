#!/bin/sh
# WEBGPU-152: derive the plan's ✅/🟨/⬜ status summary from the row tables themselves, so the count at
# the top of plans/plan_webgpu.md is never hand-maintained. Prints the totals and the open (🟨/⬜) rows.
# Usage: tools/count_webgpu_plan_status.sh [path-to-plan]  (default: plans/plan_webgpu.md)
plan="${1:-plans/plan_webgpu.md}"
awk -F'|' '
    /^\| WEBGPU-[0-9]+ \|/ {
        id=$2; gsub(/^ *| *$/,"",id)
        s=$4;  gsub(/^ *| *$/,"",s)
        if (s ~ /^✅/)      { done++ }
        else if (s ~ /^🟨/) { part++; open[++n]=id }
        else if (s ~ /^⬜/) { todo++; open[++n]=id }
        else                { printf "UNKNOWN status for %s: %s\n", id, s }
    }
    END {
        total = done + part + todo
        printf "TOTAL=%d  DONE=%d  PARTIAL=%d  TODO=%d\n", total, done, part, todo
        printf "OPEN(%d):", n
        for (i=1;i<=n;i++) printf " %s", open[i]
        printf "\n"
    }
' "$plan"
