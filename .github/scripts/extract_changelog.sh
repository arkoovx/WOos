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

notes="$(
  awk -v ver="$version" '
    BEGIN { in_block=0; found=0 }
    $0 ~ "^## " ver "$" { in_block=1; found=1; next }
    /^## / && in_block { exit }
    in_block { print }
    END {
      if (!found) {
        print "CHANGELOG section ## " ver " not found" > "/dev/stderr"
        exit 2
      }
    }
  ' CHANGELOG.md | sed '/^[[:space:]]*$/N;/^\n$/D'
)"

if [[ -z "${notes//[[:space:]]/}" ]]; then
  echo "CHANGELOG section ## $version is empty" >&2
  exit 3
fi

printf '%s\n' "$notes"
