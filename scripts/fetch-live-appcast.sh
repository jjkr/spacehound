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
headers_path=$(mktemp "${TMPDIR:-/tmp}/spacehound-feed-headers.XXXXXX")
trap 'rm -f "${headers_path}"' EXIT

http_status=$(curl --silent --show-error --location \
  --retry 3 --retry-delay 2 \
  --user-agent 'SpaceHound-release (+https://github.com/jjkr/spacehound)' \
  --header 'Cache-Control: no-cache' \
  --dump-header "${headers_path}" \
  --output "${output_path}" \
  --write-out '%{http_code}' \
  "${feed_url}") || {
  echo "error: could not reach ${feed_url}" >&2
  exit 1
}

# Cloudflare names the mitigation that fired (bot protection, WAF, challenge)
# in these headers and in the error page, which is what you need to fix it.
function explain_response() {
  grep -iE '^(server|cf-ray|cf-mitigated|cf-cache-status|content-type):' "${headers_path}" >&2 || true
  if [[ -s "${output_path}" ]]; then
    # Cloudflare block pages carry the error code (1010 browser integrity
    # check, 1020 WAF/access rule, ...) and a one-line reason.
    local summary
    summary=$(tr -d '\r' < "${output_path}" | grep -oE \
      '<title>[^<]*</title>|cf-error-code">[^<]*|<h1[^>]*>[^<]*|<h2[^>]*>[^<]*|Error code [0-9]+|Ray ID: [0-9a-f]+' \
      | sed -E 's/<[^>]*>//g; s/cf-error-code">/error code /' | head -6)
    if [[ -n "${summary}" ]]; then
      print -r -- "${summary}" >&2
    else
      echo "response body (first 400 bytes):" >&2
      head -c 400 "${output_path}" | tr -d '\r' >&2
      echo >&2
    fi
  fi
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
    echo "error: fetching ${feed_url} returned HTTP ${http_status}" >&2
    explain_response
    rm -f "${output_path}"
    exit 1
    ;;
esac
