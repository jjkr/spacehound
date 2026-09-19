#!/bin/zsh

set -euo pipefail

if (( $# != 3 )); then
  echo "usage: $0 X.Y.Z source-notes.md output-notes.md" >&2
  exit 2
fi

version="${1#v}"
source_path="$2"
output_path="$3"

"${0:A:h}/validate-release-version.sh" "${version}" >/dev/null

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
if ! grep -q '^## ' "${source_path}"; then
  echo "error: release notes need at least one second-level heading: ${source_path}" >&2
  exit 1
fi
if grep -q '^# ' "${source_path}"; then
  echo "error: release notes must not contain a top-level heading; Sparkle and GitHub add the title: ${source_path}" >&2
  exit 1
fi

# The same Markdown is embedded in the appcast item and used as the GitHub
# release body, so it is copied verbatim.
mkdir -p "${output_path:h}"
cp "${source_path}" "${output_path}"

echo "Prepared release notes for ${version} at ${output_path}"
