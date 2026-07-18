#!/bin/zsh

set -euo pipefail

if (( $# < 2 || $# > 3 )); then
  echo "usage: $0 X.Y.Z X.Y.ZfcN [existing-appcast.xml]" >&2
  exit 2
fi

marketing_version="${1#v}"
bundle_version="$2"
appcast_path="${3:-}"

if [[ ! "${marketing_version}" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
  echo "error: marketing version must use X.Y.Z format, got ${1}" >&2
  exit 1
fi

if [[ ! "${bundle_version}" =~ '^[0-9]+\.[0-9]+\.[0-9]+fc[0-9]+$' ||
      "${bundle_version%fc*}" != "${marketing_version}" ]]; then
  echo "error: bundle version must use ${marketing_version}fcN format, got ${bundle_version}" >&2
  exit 1
fi

candidate_number="${bundle_version##*fc}"
if [[ "${candidate_number}" =~ '^0[0-9]+$' ]] ||
   (( candidate_number < 1 || candidate_number > 255 )); then
  echo "error: final-candidate number must be from 1 through 255 without leading zeroes" >&2
  exit 1
fi

if [[ -z "${appcast_path}" || ! -s "${appcast_path}" ]]; then
  echo "${bundle_version}"
  exit 0
fi

if ! command -v xmllint >/dev/null 2>&1; then
  echo "error: xmllint is required to inspect the existing appcast" >&2
  exit 1
fi

current=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="version"])' \
  "${appcast_path}")

if [[ ! "${current}" =~ '^[0-9]+\.[0-9]+\.[0-9]+(fc[0-9]+)?$' ]]; then
  echo "error: existing appcast has an invalid or missing version: ${current}" >&2
  exit 1
fi

current_marketing="${current%fc*}"
if [[ "${current}" == *fc* ]]; then
  current_candidate="${current##*fc}"
else
  # A previously published stable X.Y.Z sorts after every final candidate of X.Y.Z.
  current_candidate=256
fi

if ! awk \
  -v nextMarketing="${marketing_version}" \
  -v nextCandidate="${candidate_number}" \
  -v currentMarketing="${current_marketing}" \
  -v currentCandidate="${current_candidate}" '
  BEGIN {
    split(nextMarketing, nextParts, ".")
    split(currentMarketing, currentParts, ".")
    for (i = 1; i <= 3; i++) {
      if ((nextParts[i] + 0) > (currentParts[i] + 0)) exit 0
      if ((nextParts[i] + 0) < (currentParts[i] + 0)) exit 1
    }
    exit !((nextCandidate + 0) > (currentCandidate + 0))
  }
'; then
  echo "error: release ${bundle_version} must be greater than published version ${current}" >&2
  exit 1
fi

echo "${bundle_version}"
