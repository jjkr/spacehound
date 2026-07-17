#!/bin/zsh

set -euo pipefail

if (( $# < 1 || $# > 2 )); then
  echo "usage: $0 X.Y.Z [existing-appcast.xml]" >&2
  exit 2
fi

candidate="${1#v}"
appcast_path="${2:-}"

if [[ ! "${candidate}" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
  echo "error: version must use X.Y.Z format, got ${1}" >&2
  exit 1
fi

if [[ -z "${appcast_path}" || ! -s "${appcast_path}" ]]; then
  echo "${candidate}"
  exit 0
fi

if ! command -v xmllint >/dev/null 2>&1; then
  echo "error: xmllint is required to inspect the existing appcast" >&2
  exit 1
fi

current=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="version"])' \
  "${appcast_path}")

if [[ ! "${current}" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
  echo "error: existing appcast has an invalid or missing version: ${current}" >&2
  exit 1
fi

if ! awk -v candidate="${candidate}" -v current="${current}" '
  BEGIN {
    split(candidate, nextParts, ".")
    split(current, currentParts, ".")
    for (i = 1; i <= 3; i++) {
      if ((nextParts[i] + 0) > (currentParts[i] + 0)) exit 0
      if ((nextParts[i] + 0) < (currentParts[i] + 0)) exit 1
    }
    exit 1
  }
'; then
  echo "error: release ${candidate} must be greater than published version ${current}" >&2
  exit 1
fi

echo "${candidate}"
