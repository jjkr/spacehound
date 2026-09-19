#!/bin/zsh

set -euo pipefail

if (( $# < 1 || $# > 2 )); then
  echo "usage: $0 X.Y.Z [existing-appcast.xml]" >&2
  exit 2
fi

version="${1#v}"
appcast_path="${2:-}"

if [[ ! "${version}" =~ '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' ]]; then
  echo "error: version must use X.Y.Z format without leading zeroes, got ${1}" >&2
  exit 1
fi

if [[ -z "${appcast_path}" || ! -s "${appcast_path}" ]]; then
  echo "${version}"
  exit 0
fi

if ! command -v xmllint >/dev/null 2>&1; then
  echo "error: xmllint is required to inspect the existing appcast" >&2
  exit 1
fi

# The feed may hold several items (production history plus a pending beta).
# A new release must sort after every one of them, whatever its channel.
published_versions=$(xmllint --xpath \
  '/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"]/*[local-name()="version"]/text()' \
  "${appcast_path}" 2>/dev/null | tr -s '[:space:]' '\n' || true)

for current in ${(f)published_versions}; do
  [[ -z "${current}" ]] && continue
  if [[ ! "${current}" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
    echo "error: existing appcast has an invalid version: ${current}" >&2
    exit 1
  fi

  if ! awk -v candidate="${version}" -v published="${current}" '
    BEGIN {
      split(candidate, nextParts, ".")
      split(published, currentParts, ".")
      for (i = 1; i <= 3; i++) {
        if ((nextParts[i] + 0) > (currentParts[i] + 0)) exit 0
        if ((nextParts[i] + 0) < (currentParts[i] + 0)) exit 1
      }
      exit 1
    }
  '; then
    echo "error: release ${version} must be greater than published version ${current}" >&2
    exit 1
  fi
done

echo "${version}"
