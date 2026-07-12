#!/usr/bin/env bash
# lvgl-object-audit.sh — static LVGL object-count audit per page module.
#
# LVGL widgets are created at compile time, so a static count of widget-creating
# YAML keys + widget includes is a good proxy for the runtime object tree per page.
# This is the repeatable "audit" half; the exact runtime count needs the
# device (see the lv_obj_get_child_count snippet in docs/lvgl-object-budgets.md).
#
# Usage:  scripts/lvgl-object-audit.sh            # from repo root
#
# Counts, per modules/*.yaml:
#   - direct widget declarations (obj/label/button/image/bar/arc/slider/canvas/…)
#   - widgets/*.yaml includes (each expands to >=1 widget at merge)
set -uo pipefail
cd "$(dirname "$0")/.."

MODDIR="modules"
WIDGET_KEYS='obj|label|button|image|bar|arc|slider|canvas|line|checkbox|dropdown|roller|switch|textarea|spinbox|led|meter|tabview|chart'

printf '%-20s %8s %9s\n' "module" "widgets" "includes"
printf '%-20s %8s %9s\n' "------" "-------" "--------"
total=0
for f in "$MODDIR"/*.yaml; do
  [ -e "$f" ] || continue
  name=$(basename "$f" .yaml)
  w=$( { grep -oE "(^|[^a-z_])($WIDGET_KEYS):" "$f" 2>/dev/null || true; } \
        | grep -oE "($WIDGET_KEYS):" | wc -l | tr -d ' ')
  inc=$( { grep -cE '!include \{?file: widgets/' "$f" 2>/dev/null || true; } | tr -d ' ')
  inc=${inc:-0}
  [ "$w" -eq 0 ] && [ "$inc" -eq 0 ] && continue
  printf '%-20s %8s %9s\n' "$name" "$w" "$inc"
  total=$((total + w))
done
printf '%-20s %8s\n' "TOTAL (direct)" "$total"
echo
echo "Note: includes expand to >=1 widget each; this is a proxy, not the exact"
echo "runtime tree. Capture exact per-page counts on-device per the budgets doc."
