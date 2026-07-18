#!/bin/zsh

set -euo pipefail

function require_env() {
  local name="$1"
  if [[ -z "${(P)name:-}" ]]; then
    echo "error: ${name} must be set" >&2
    exit 1
  fi
}

required_vars=(
  RELEASE_MARKETING_VERSION
  RELEASE_BUILD_VERSION
  UPDATE_ARCHIVE_PATH
  RELEASE_NOTES_PATH
  APPCAST_OUTPUT_PATH
  DOWNLOAD_URL_PREFIX
  RELEASE_NOTES_URL_PREFIX
  SPARKLE_ED_PRIVATE_KEY
  SPARKLE_PUBLIC_ED_KEY
  SPARKLE_GENERATE_APPCAST
  SPARKLE_SIGN_UPDATE
)

for name in "${required_vars[@]}"; do
  require_env "${name}"
done

marketing_version="${RELEASE_MARKETING_VERSION#v}"
bundle_version="${RELEASE_BUILD_VERSION}"
artifact_version="${bundle_version/fc/-fc}"
"${0:A:h}/validate-release-version.sh" \
  "${marketing_version}" "${bundle_version}" >/dev/null

if [[ ! -f "${UPDATE_ARCHIVE_PATH}" ]]; then
  echo "error: update archive not found: ${UPDATE_ARCHIVE_PATH}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_NOTES_PATH}" ]]; then
  echo "error: release notes not found: ${RELEASE_NOTES_PATH}" >&2
  exit 1
fi

for tool in "${SPARKLE_GENERATE_APPCAST}" "${SPARKLE_SIGN_UPDATE}"; do
  if [[ ! -x "${tool}" ]]; then
    echo "error: Sparkle tool is missing or not executable: ${tool}" >&2
    exit 1
  fi
done

print -rn -- "${SPARKLE_ED_PRIVATE_KEY}" | xcrun swift \
  "${0:A:h}/validate-sparkle-key-pair.swift" \
  "${SPARKLE_PUBLIC_ED_KEY}"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/spacerabbit-appcast.XXXXXX")
function cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

archive_name="SpaceRabbit-${artifact_version}-arm64.zip"
notes_name="SpaceRabbit-${artifact_version}-arm64.md"
cp "${UPDATE_ARCHIVE_PATH}" "${work_dir}/${archive_name}"
cp "${RELEASE_NOTES_PATH}" "${work_dir}/${notes_name}"

print -rn -- "${SPARKLE_ED_PRIVATE_KEY}" | "${SPARKLE_GENERATE_APPCAST}" \
  --ed-key-file - \
  --download-url-prefix "${DOWNLOAD_URL_PREFIX%/}/" \
  --release-notes-url-prefix "${RELEASE_NOTES_URL_PREFIX%/}/" \
  --link "https://github.com/animaslabs/spacerabbit" \
  --versions "${bundle_version}" \
  --maximum-versions 1 \
  --maximum-deltas 0 \
  -o "${work_dir}/appcast.xml" \
  "${work_dir}"

print -rn -- "${SPARKLE_ED_PRIVATE_KEY}" | "${SPARKLE_SIGN_UPDATE}" \
  --verify \
  --ed-key-file - \
  "${work_dir}/appcast.xml"

if ! xmllint --noout "${work_dir}/appcast.xml"; then
  echo "error: generated appcast is not valid XML" >&2
  exit 1
fi

appcast_version=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="version"])' \
  "${work_dir}/appcast.xml")
appcast_marketing_version=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="shortVersionString"])' \
  "${work_dir}/appcast.xml")
enclosure_url=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="enclosure"]/@url)' \
  "${work_dir}/appcast.xml")
minimum_system_version=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="minimumSystemVersion"])' \
  "${work_dir}/appcast.xml")
hardware_requirements=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="hardwareRequirements"])' \
  "${work_dir}/appcast.xml")
enclosure_signature=$(xmllint --xpath \
  'string(/*[local-name()="rss"]/*[local-name()="channel"]/*[local-name()="item"][1]/*[local-name()="enclosure"]/@*[local-name()="edSignature"])' \
  "${work_dir}/appcast.xml")

expected_url="${DOWNLOAD_URL_PREFIX%/}/${archive_name}"
if [[ "${appcast_version}" != "${bundle_version}" ||
      "${appcast_marketing_version}" != "${marketing_version}" ||
      "${enclosure_url}" != "${expected_url}" ]]; then
  echo "error: generated appcast has versions ${appcast_marketing_version}/${appcast_version} and URL ${enclosure_url}; expected ${marketing_version}/${bundle_version} and ${expected_url}" >&2
  exit 1
fi

if [[ "${minimum_system_version}" != "14.0" || "${hardware_requirements}" != "arm64" ]]; then
  echo "error: generated appcast must require macOS 14.0 and arm64" >&2
  exit 1
fi

if [[ -z "${enclosure_signature}" ]]; then
  echo "error: generated appcast enclosure is missing its Sparkle EdDSA signature" >&2
  exit 1
fi

print -rn -- "${SPARKLE_ED_PRIVATE_KEY}" | "${SPARKLE_SIGN_UPDATE}" \
  --verify \
  --ed-key-file - \
  "${work_dir}/${archive_name}" \
  "${enclosure_signature}"

if ! grep -q '<!-- sparkle-signatures:' "${work_dir}/appcast.xml"; then
  echo "error: generated appcast is missing its signed-feed block" >&2
  exit 1
fi

mkdir -p "${APPCAST_OUTPUT_PATH:h}"
cp "${work_dir}/appcast.xml" "${APPCAST_OUTPUT_PATH}"
cp "${work_dir}/${notes_name}" "${RELEASE_NOTES_PATH}"

echo "Created signed appcast at ${APPCAST_OUTPUT_PATH}"
