#!/bin/zsh

# Downloads the published Sparkle feed so a release can be merged into it.
#
# usage: fetch-live-appcast.sh FEED_URL OUTPUT_PATH
#
# A 404 is only acceptable for the very first release, and only when
# ALLOW_MISSING_FEED=1. Any other failure aborts so a transient outage can never
# cause the feed to be regenerated from scratch and drop published releases.

set -euo pipefail

if (( $# != 2 )); then
  echo "usage: $0 FEED_URL OUTPUT_PATH" >&2
  exit 2
fi

feed_url="$1"
output_path="$2"

mkdir -p "${output_path:h}"
rm -f "${output_path}"

http_status=$(curl --silent --show-error --location \
  --retry 3 --retry-delay 2 \
  --header 'Cache-Control: no-cache' \
  --output "${output_path}" \
  --write-out '%{http_code}' \
  "${feed_url}") || {
  echo "error: could not reach ${feed_url}" >&2
  exit 1
}

case "${http_status}" in
  200)
    if ! xmllint --noout "${output_path}"; then
      echo "error: published appcast at ${feed_url} is not valid XML" >&2
      exit 1
    fi
    echo "Fetched published appcast from ${feed_url}"
    ;;
  404)
    rm -f "${output_path}"
    if [[ "${ALLOW_MISSING_FEED:-0}" != "1" ]]; then
      echo "error: no appcast is published at ${feed_url}" >&2
      exit 1
    fi
    echo "warning: no appcast is published at ${feed_url}; a new feed will be created" >&2
    ;;
  *)
    rm -f "${output_path}"
    echo "error: fetching ${feed_url} returned HTTP ${http_status}" >&2
    exit 1
    ;;
esac
