#!/bin/zsh

set -euo pipefail

function require_env() {
  local name="$1"
  if [[ -z "${(P)name:-}" ]]; then
    echo "error: ${name} must be set" >&2
    exit 1
  fi
}

if [[ $# -ne 1 ]]; then
  echo "usage: $0 PATH_TO_DSYMS" >&2
  exit 1
fi

dsym_path="$1"
SENTRY_ORG="${SENTRY_ORG:-animaslabs}"
SENTRY_PROJECT="${SENTRY_PROJECT:-spacerabbit}"

require_env SENTRY_AUTH_TOKEN

if ! command -v sentry-cli >/dev/null 2>&1; then
  echo "error: sentry-cli is required to upload release debug symbols" >&2
  exit 1
fi

if [[ ! -d "${dsym_path}" ]]; then
  echo "error: dSYM directory not found: ${dsym_path}" >&2
  exit 1
fi

if [[ -z "$(find "${dsym_path}" -type d -name '*.dSYM' -print -quit)" ]]; then
  echo "error: no dSYM bundles found under: ${dsym_path}" >&2
  exit 1
fi

sentry-cli debug-files upload \
  --org "${SENTRY_ORG}" \
  --project "${SENTRY_PROJECT}" \
  "${dsym_path}"
