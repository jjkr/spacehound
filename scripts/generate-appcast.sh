#!/bin/zsh

# Adds one release to the Sparkle feed.
#
# APPCAST_PATH is the currently published feed (fetched from
# updates.spacehound.app) and is rewritten in place. When the file is absent
# a new feed is started, which should only happen for the very first release.
# Existing items are preserved by generate_appcast; the new item is tagged
# with RELEASE_CHANNEL (normally "beta") and later promoted by
# promote-appcast.swift.

set -euo pipefail

function require_env() {
  local name="$1"
  if [[ -z "${(P)name:-}" ]]; then
    echo "error: ${name} must be set" >&2
    exit 1
  fi
}

required_vars=(
  RELEASE_VERSION
  RELEASE_CHANNEL
  UPDATE_ARCHIVE_PATH
  RELEASE_NOTES_PATH
  APPCAST_PATH
  DOWNLOAD_URL_PREFIX
  SPARKLE_ED_PRIVATE_KEY
  SPARKLE_PUBLIC_ED_KEY
  SPARKLE_GENERATE_APPCAST
  SPARKLE_SIGN_UPDATE
)

for name in "${required_vars[@]}"; do
  require_env "${name}"
done

version="${RELEASE_VERSION#v}"
"${0:A:h}/validate-release-version.sh" "${version}" >/dev/null

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

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/spacehound-appcast.XXXXXX")
function cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

item_xpath="/*[local-name()=\"rss\"]/*[local-name()=\"channel\"]/*[local-name()=\"item\"]"
function feed_items() {
  xmllint --xpath "count(${item_xpath})" "$1"
}
function item_field() {
  # $1 feed, $2 version, $3 xpath relative to the item
  xmllint --xpath \
    "string(${item_xpath}[*[local-name()=\"version\"]=\"$2\"]/$3)" "$1"
}

archive_name="SpaceHound-${version}-arm64.zip"
notes_name="SpaceHound-${version}-arm64.md"
cp "${UPDATE_ARCHIVE_PATH}" "${work_dir}/${archive_name}"
cp "${RELEASE_NOTES_PATH}" "${work_dir}/${notes_name}"

previous_items=0
if [[ -s "${APPCAST_PATH}" ]]; then
  if ! xmllint --noout "${APPCAST_PATH}"; then
    echo "error: existing appcast is not valid XML: ${APPCAST_PATH}" >&2
    exit 1
  fi
  "${0:A:h}/validate-release-version.sh" "${version}" "${APPCAST_PATH}" >/dev/null
  cp "${APPCAST_PATH}" "${work_dir}/appcast.xml"
  previous_items=$(feed_items "${work_dir}/appcast.xml")
  echo "Merging ${version} into existing feed with ${previous_items} item(s)"
else
  echo "warning: no existing appcast at ${APPCAST_PATH}; starting a new feed" >&2
fi

print -rn -- "${SPARKLE_ED_PRIVATE_KEY}" | "${SPARKLE_GENERATE_APPCAST}" \
  --ed-key-file - \
  --download-url-prefix "${DOWNLOAD_URL_PREFIX%/}/" \
  --embed-release-notes \
  --link "https://github.com/jjkr/spacehound" \
  --versions "${version}" \
  --channel "${RELEASE_CHANNEL}" \
  --maximum-versions 3 \
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

feed="${work_dir}/appcast.xml"
item_count=$(xmllint --xpath \
  "count(${item_xpath}[*[local-name()=\"version\"]=\"${version}\"])" "${feed}")
if [[ "${item_count}" != "1" ]]; then
  echo "error: generated appcast has ${item_count} items for ${version}; expected exactly 1" >&2
  exit 1
fi

marketing_version=$(item_field "${feed}" "${version}" '*[local-name()="shortVersionString"]')
channel=$(item_field "${feed}" "${version}" '*[local-name()="channel"]')
enclosure_url=$(item_field "${feed}" "${version}" '*[local-name()="enclosure"]/@url')
enclosure_length=$(item_field "${feed}" "${version}" '*[local-name()="enclosure"]/@length')
enclosure_signature=$(item_field "${feed}" "${version}" '*[local-name()="enclosure"]/@*[local-name()="edSignature"]')
minimum_system_version=$(item_field "${feed}" "${version}" '*[local-name()="minimumSystemVersion"]')
hardware_requirements=$(item_field "${feed}" "${version}" '*[local-name()="hardwareRequirements"]')
description_format=$(item_field "${feed}" "${version}" '*[local-name()="description"]/@*[local-name()="format"]')
release_notes_link=$(item_field "${feed}" "${version}" '*[local-name()="releaseNotesLink"]')

expected_url="${DOWNLOAD_URL_PREFIX%/}/${archive_name}"
archive_size=$(stat -f %z "${work_dir}/${archive_name}")
if [[ "${marketing_version}" != "${version}" ||
      "${enclosure_url}" != "${expected_url}" ||
      "${enclosure_length}" != "${archive_size}" ]]; then
  echo "error: generated item has version ${marketing_version}, URL ${enclosure_url}, length ${enclosure_length}; expected ${version}, ${expected_url}, ${archive_size}" >&2
  exit 1
fi

if [[ "${channel}" != "${RELEASE_CHANNEL}" ]]; then
  echo "error: generated item is on channel '${channel}'; expected '${RELEASE_CHANNEL}'" >&2
  exit 1
fi

if [[ "${minimum_system_version}" != "14.0" || "${hardware_requirements}" != "arm64" ]]; then
  echo "error: generated appcast must require macOS 14.0 and arm64" >&2
  exit 1
fi

if [[ "${description_format}" != "markdown" || -n "${release_notes_link}" ]]; then
  echo "error: release notes must be embedded as markdown, not linked" >&2
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

if ! grep -q '<!-- sparkle-signatures:' "${feed}"; then
  echo "error: generated appcast is missing its signed-feed block" >&2
  exit 1
fi

# generate_appcast prunes stale beta items but must never drop the newest
# production item, or existing users would stop seeing updates.
new_items=$(feed_items "${feed}")
if (( previous_items > 0 && new_items < 2 )); then
  echo "error: generated appcast has ${new_items} item(s) after merging into a feed of ${previous_items}; previous releases were lost" >&2
  exit 1
fi

mkdir -p "${APPCAST_PATH:h}"
cp "${feed}" "${APPCAST_PATH}"

echo "Added ${version} (${RELEASE_CHANNEL}) to signed appcast at ${APPCAST_PATH} (${new_items} items)"
