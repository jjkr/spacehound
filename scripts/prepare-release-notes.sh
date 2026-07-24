#!/bin/zsh

set -euo pipefail

if (( $# != 4 )); then
  echo "usage: $0 X.Y.Z X.Y.ZfcN source-notes.md output-directory" >&2
  exit 2
fi

marketing_version="${1#v}"
bundle_version="$2"
source_path="$3"
output_dir="$4"

"${0:A:h}/validate-release-version.sh" \
  "${marketing_version}" "${bundle_version}" >/dev/null

if [[ ! -f "${source_path}" ]]; then
  echo "error: release notes not found: ${source_path}" >&2
  exit 1
fi
if ! grep -q '[^[:space:]]' "${source_path}"; then
  echo "error: release notes are empty: ${source_path}" >&2
  exit 1
fi
if grep -q 'RELEASE_NOTES_PLACEHOLDER' "${source_path}"; then
  echo "error: remove the template placeholder from ${source_path}" >&2
  exit 1
fi

candidate_number="${bundle_version##*fc}"
artifact_version="${bundle_version/fc/-fc}"
notes_name="SpaceHound-${artifact_version}-arm64.md"

mkdir -p "${output_dir}/beta" "${output_dir}/production"

{
  printf '# SpaceHound %s (final candidate %s)\n\n' \
    "${marketing_version}" "${candidate_number}"
  cat "${source_path}"
  printf '\n'
} > "${output_dir}/beta/${notes_name}"

{
  printf '# SpaceHound %s\n\n' "${marketing_version}"
  cat "${source_path}"
  printf '\n'
} > "${output_dir}/production/${notes_name}"

echo "Prepared beta and production release notes from ${source_path}"
