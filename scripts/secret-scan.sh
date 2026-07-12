#!/usr/bin/env bash
#
# secret-scan.sh — publish gate for the esp32-panel repo.
#
# Scans the working tree for private-network markers and site-identity terms
# that must never appear in this public repo. Exits non-zero with a per-hit
# report (file:line: matched line) if anything is found.
#
# Scope:
#   * In a git repo: tracked + staged files (`git ls-files`) — gitignored
#     site files (user-config.yaml, secrets.yaml, deploy.local.mk) are
#     naturally excluded, which is exactly right: they MAY contain site data,
#     they just must never be committed.
#   * Outside a git repo: `find` over the tree (minus .git/.esphome).
#
# Binary files (fonts, images, audio, models) are excluded from the text scan
# but still get a `strings`-based scan for IP/MAC patterns, so a leaked
# private IP baked into an asset is still caught.
#
# Allowlist: scripts/secret-scan-allow.txt — one extended regex per line
# (blank lines and #-comments ignored). A hit whose full "file:line:content"
# record matches an allow regex is dropped. Keep entries narrow (anchor on the
# file path too, e.g. `^esphome-modular-lvgl-buttons/LICENSE:.*Simon`).
#
# Usage:
#   scripts/secret-scan.sh          # scan, report, exit 1 on hits
#   make secret-scan                # same
#   (also invoked by the pre-commit hook installed via `make hooks`)
set -euo pipefail

# Default: the repo this script lives in. SCAN_ROOT overrides, letting QA aim
# the gate at any tree: SCAN_ROOT=/path/to/tree scripts/secret-scan.sh
cd "${SCAN_ROOT:-$(dirname "$0")/..}"
ALLOWLIST="scripts/secret-scan-allow.txt"

# ── Patterns ────────────────────────────────────────────────────────────────
# 1. Retired sync-public.sh scan: private-network + key-material markers.
NET_RE='tinkernet|bagofholding|vault\.int|BEGIN [A-Z ]*PRIVATE KEY|10\.0\.[0-9]+\.[0-9]+|192\.168\.[0-9]+\.[0-9]+|([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'
# 2. Site-identity terms (case-insensitive). Two classes:
#    * stems — device/entity-id prefixes that continue with word chars in the
#      wild (sigen_plant, tessy2, octopus_energy, ukz82tvc_power…): leading
#      word boundary only, or the trailing \b would miss every real leak.
#    * names — household member names: bounded on both sides so e.g.
#      "simonize"/"williamson" don't false-positive.
SITE_STEM_RE='\b(sigen|ukz82tvc|tessy|frontroomesp|front_?room|p4panel123|octopus)'
NAME_RE='\b(simon|rosie|william)\b'
SCAN_RE="($NET_RE|$SITE_STEM_RE|$NAME_RE)"
# Binary scan: only network-identity shapes (IPs, MACs) — text terms inside
# font/image blobs are noise.
BIN_RE='10\.0\.[0-9]+\.[0-9]+|192\.168\.[0-9]+\.[0-9]+|([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'

# ── File inventory ──────────────────────────────────────────────────────────
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  mapfile -t FILES < <(git ls-files)
else
  mapfile -t FILES < <(find . -type f \
    -not -path './.git/*' -not -path './.esphome/*' | sed 's|^\./||')
fi

# Files skipped entirely (never contain publishable text we author).
SKIP_RE='^(esphome-modular-lvgl-buttons/LICENSE|scripts/secret-scan-allow\.txt|scripts/secret-scan\.sh)$'
# Binary extensions: excluded from text scan, included in the strings scan.
BINEXT_RE='\.(ttf|otf|woff2?|png|jpe?g|gif|bmp|ico|wav|flac|mp3|ogg|tflite|bin|gz|zip)$'

TEXT_FILES=()
BIN_FILES=()
for f in "${FILES[@]}"; do
  [[ -f "$f" ]] || continue                      # skip deleted-but-tracked
  [[ "$f" =~ $SKIP_RE ]] && continue
  if [[ "$f" =~ $BINEXT_RE ]]; then
    BIN_FILES+=("$f")
  else
    TEXT_FILES+=("$f")
  fi
done

# ── Scan ────────────────────────────────────────────────────────────────────
HITS=""

if ((${#TEXT_FILES[@]})); then
  # -I: treat binary-looking tracked files as no-match; -H -n: file:line: prefix.
  MATCH="$(grep -IHniE "$SCAN_RE" -- "${TEXT_FILES[@]}" 2>/dev/null || true)"
  [[ -n "$MATCH" ]] && HITS+="$MATCH"$'\n'
fi

for f in "${BIN_FILES[@]}"; do
  if command -v strings >/dev/null 2>&1; then
    MATCH="$(strings -- "$f" 2>/dev/null | grep -nE "$BIN_RE" || true)"
  else
    # busybox/CI fallback: grep the raw bytes as text
    MATCH="$(grep -aoE "$BIN_RE" -- "$f" 2>/dev/null | sort -u || true)"
  fi
  [[ -n "$MATCH" ]] && HITS+="$(sed "s|^|$f:binary:|" <<<"$MATCH")"$'\n'
done

# ── Allowlist filter ────────────────────────────────────────────────────────
if [[ -n "$HITS" && -f "$ALLOWLIST" ]]; then
  ALLOW_RES="$(grep -vE '^[[:space:]]*(#|$)' "$ALLOWLIST" || true)"
  if [[ -n "$ALLOW_RES" ]]; then
    HITS="$(grep -vEf <(printf '%s\n' "$ALLOW_RES") <<<"$HITS" || true)"
  fi
fi
HITS="$(sed '/^[[:space:]]*$/d' <<<"$HITS" || true)"

# ── Report ──────────────────────────────────────────────────────────────────
if [[ -n "$HITS" ]]; then
  echo "x secret-scan FAILED — private/site-identifying content found:" >&2
  echo >&2
  echo "$HITS" >&2
  echo >&2
  echo "  Fix the content, or if a hit is provably benign add a NARROW regex" >&2
  echo "  (anchored on the file path) to $ALLOWLIST." >&2
  exit 1
fi

echo "ok secret-scan clean (${#TEXT_FILES[@]} text files, ${#BIN_FILES[@]} binaries via strings)"
