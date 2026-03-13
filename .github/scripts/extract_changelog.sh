#!/usr/bin/env bash
set -euo pipefail

version="${1:-}"
if [[ -z "$version" ]]; then
  echo "Usage: $0 <version>" >&2
  exit 1
fi

if [[ ! -f CHANGELOG.md ]]; then
  echo "CHANGELOG.md not found" >&2
  exit 1
fi

awk -v ver="$version" '
  BEGIN { in_block=0 }
  $0 ~ "^## " ver "$" { in_block=1; next }
  /^## / && in_block { exit }
  in_block { print }
' CHANGELOG.md | sed '/^[[:space:]]*$/N;/^\n$/D'
