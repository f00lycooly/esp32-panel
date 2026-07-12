#!/usr/bin/env bash
# Lint (and optionally auto-fix) ESPHome configs in this repo.
#
# Usage:
#   bash lint.sh           # check only; exit non-zero on any failure
#   bash lint.sh --fix     # auto-fix simple issues, then re-run all checks
#
# Auto-fixes (when --fix is given):
#   * trim trailing whitespace
#   * ensure single newline at end of file
#   * collapse 3+ consecutive blank lines down to 2
#
# Always runs:
#   1. yamllint over the whole repo
#   2. `esphome config` against every example_code/*.yaml
#      (validated from the parent directory via a temp symlink, because the
#      examples !include paths like `esphome-modular-lvgl-buttons/...`)

set -u
shopt -s nullglob globstar

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$REPO_ROOT")"

FIX=0
[[ "${1:-}" == "--fix" ]] && FIX=1

# ---------------------------------------------------------------------------
# Auto-fix step
# ---------------------------------------------------------------------------
if [[ $FIX -eq 1 ]]; then
  echo "==> auto-fix (trailing whitespace, EOF newline, excess blank lines)"
  fixed_count=0
  while IFS= read -r -d '' f; do
    # Skip the .git directory.
    [[ "$f" == *"/.git/"* ]] && continue
    before="$(sha1sum "$f" | awk '{print $1}')"

    # 1. strip trailing whitespace
    # 2. collapse runs of 3+ blank lines to exactly 2
    # 3. ensure file ends with exactly one newline
    tmp="$(mktemp)"
    sed -E 's/[[:space:]]+$//' "$f" \
      | awk 'BEGIN{blank=0} /^$/{blank++; if(blank<=2)print; next} {blank=0; print}' \
      > "$tmp"
    python3 -c "
import sys
p=sys.argv[1]
with open(p,'rb') as fh: data=fh.read()
if data:
    data=data.rstrip(b'\n')+b'\n'
with open(p,'wb') as fh: fh.write(data)
" "$tmp"

    after="$(sha1sum "$tmp" | awk '{print $1}')"
    if [[ "$before" != "$after" ]]; then
      mv "$tmp" "$f"
      echo "  fixed $f"
      fixed_count=$((fixed_count + 1))
    else
      rm -f "$tmp"
    fi
  done < <(find "$REPO_ROOT" -type f \( -name '*.yaml' -o -name '*.yml' -o -name '*.sh' \) -print0)
  echo "  $fixed_count file(s) modified"
  echo
fi

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------
failures=0

echo "==> yamllint"
if command -v yamllint >/dev/null 2>&1; then
  if ! yamllint -c "$REPO_ROOT/.yamllint" "$REPO_ROOT"; then
    failures=$((failures + 1))
  fi
else
  echo "  yamllint not installed; skipping. Install with: pip install yamllint"
fi

echo
echo "==> esphome config (validates each example_code/*.yaml)"
if ! command -v esphome >/dev/null 2>&1; then
  echo "  esphome CLI not found. Install with: pip install esphome"
  exit 1
fi

declare -a links=()
cleanup() {
  for l in "${links[@]:-}"; do
    [[ -L "$l" ]] && rm -f "$l"
  done
}
trap cleanup EXIT

for cfg in "$REPO_ROOT"/example_code/*.yaml; do
  base="$(basename "$cfg")"
  link="$PARENT_DIR/$base"
  if [[ -e "$link" && ! -L "$link" ]]; then
    echo "  SKIP $base (real file already exists in $PARENT_DIR, not overwriting)"
    continue
  fi
  ln -sfn "$cfg" "$link"
  links+=("$link")

  printf '  %-70s ' "$base"
  if output="$(cd "$PARENT_DIR" && esphome config "$base" 2>&1)"; then
    echo "OK"
  else
    echo "FAIL"
    echo "$output" | tail -20 | sed 's/^/      /'
    failures=$((failures + 1))
  fi
done

echo
if [[ $failures -gt 0 ]]; then
  echo "FAILED: $failures check(s) failed."
  [[ $FIX -eq 0 ]] && echo "Hint: try 'bash lint.sh --fix' to auto-fix style issues."
  exit 1
fi
echo "All checks passed."
