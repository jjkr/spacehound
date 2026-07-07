#!/bin/zsh

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
PROJECT_PATH="${ROOT_DIR}/SpaceRabbit.xcodeproj"
PROJECT_SPEC_PATH="${ROOT_DIR}/project.yml"
SCHEME="SpaceRabbit"
APP_NAME="SpaceRabbit"
BUILD_ROOT="${BUILD_ROOT:-${ROOT_DIR}/build/release}"
DERIVED_DATA_PATH="${DERIVED_DATA_PATH:-${ROOT_DIR}/build/DerivedDataRelease}"
ARCHIVE_PATH="${BUILD_ROOT}/${APP_NAME}.xcarchive"
EXPORT_PATH="${BUILD_ROOT}/export"
DMG_STAGING_PATH="${BUILD_ROOT}/dmg-root"
DIST_PATH="${DIST_PATH:-${ROOT_DIR}/build/dist}"
EXPORT_OPTIONS_PLIST="${BUILD_ROOT}/ExportOptions.plist"

function require_env() {
  local name="$1"
  if [[ -z "${(P)name:-}" ]]; then
    echo "error: ${name} must be set" >&2
    exit 1
  fi
}

function setup_notary_args() {
  NOTARY_ARGS=()

  if [[ -n "${APPLE_NOTARY_KEYCHAIN_PROFILE:-}" ]]; then
    NOTARY_ARGS=(-p "${APPLE_NOTARY_KEYCHAIN_PROFILE}")
    return
  fi

  if [[ -n "${APPLE_API_KEY_PATH:-}" && -n "${APPLE_API_KEY_ID:-}" ]]; then
    NOTARY_ARGS=(-k "${APPLE_API_KEY_PATH}" -d "${APPLE_API_KEY_ID}")
    if [[ -n "${APPLE_API_ISSUER_ID:-}" ]]; then
      NOTARY_ARGS+=(-i "${APPLE_API_ISSUER_ID}")
    fi
    return
  fi

  if [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
    NOTARY_ARGS=(
      --apple-id "${APPLE_ID}"
      --password "${APPLE_APP_SPECIFIC_PASSWORD}"
      --team-id "${APPLE_TEAM_ID}"
    )
  fi
}

require_env DEVELOPMENT_TEAM

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "error: xcodegen is required for release packaging" >&2
  exit 1
fi

CODE_SIGN_IDENTITY="${CODE_SIGN_IDENTITY:-Developer ID Application}"
RELEASE_VERSION="${RELEASE_VERSION:-}"

rm -rf "${BUILD_ROOT}" "${DIST_PATH}" "${DERIVED_DATA_PATH}"
mkdir -p "${BUILD_ROOT}" "${DIST_PATH}"

(
  cd "${ROOT_DIR}"
  xcodegen generate --spec "${PROJECT_SPEC_PATH}"
)

cat > "${EXPORT_OPTIONS_PLIST}" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>destination</key>
  <string>export</string>
  <key>method</key>
  <string>developer-id</string>
  <key>signingCertificate</key>
  <string>${CODE_SIGN_IDENTITY}</string>
  <key>signingStyle</key>
  <string>manual</string>
  <key>teamID</key>
  <string>${DEVELOPMENT_TEAM}</string>
</dict>
</plist>
EOF

xcodebuild \
  -project "${PROJECT_PATH}" \
  -scheme "${SCHEME}" \
  -configuration Release \
  -derivedDataPath "${DERIVED_DATA_PATH}" \
  -archivePath "${ARCHIVE_PATH}" \
  -destination "generic/platform=macOS" \
  ARCHS=arm64 \
  ONLY_ACTIVE_ARCH=NO \
  DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
  CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY="${CODE_SIGN_IDENTITY}" \
  archive

xcodebuild \
  -exportArchive \
  -archivePath "${ARCHIVE_PATH}" \
  -exportPath "${EXPORT_PATH}" \
  -exportOptionsPlist "${EXPORT_OPTIONS_PLIST}"

APP_PATH="${EXPORT_PATH}/${APP_NAME}.app"
if [[ ! -d "${APP_PATH}" ]]; then
  echo "error: exported app not found at ${APP_PATH}" >&2
  exit 1
fi

if [[ -z "${RELEASE_VERSION}" ]]; then
  RELEASE_VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "${APP_PATH}/Contents/Info.plist")
fi
RELEASE_VERSION="${RELEASE_VERSION#refs/tags/}"

ZIP_PATH="${DIST_PATH}/${APP_NAME}-${RELEASE_VERSION}-arm64.zip"
DMG_PATH="${DIST_PATH}/${APP_NAME}-${RELEASE_VERSION}-arm64.dmg"
CHECKSUMS_PATH="${DIST_PATH}/${APP_NAME}-${RELEASE_VERSION}-SHA256SUMS.txt"

codesign --verify --strict --verbose=2 "${APP_PATH}"

ditto -c -k --sequesterRsrc --keepParent "${APP_PATH}" "${ZIP_PATH}"

setup_notary_args
if (( ${#NOTARY_ARGS[@]} > 0 )); then
  xcrun notarytool submit "${ZIP_PATH}" --wait "${NOTARY_ARGS[@]}"
  xcrun stapler staple "${APP_PATH}"
  spctl --assess --type execute --verbose=4 "${APP_PATH}"
else
  echo "warning: notarization credentials were not provided; ZIP and DMG will not be notarized" >&2
fi

rm -rf "${DMG_STAGING_PATH}"
mkdir -p "${DMG_STAGING_PATH}"
cp -R "${APP_PATH}" "${DMG_STAGING_PATH}/"
ln -s /Applications "${DMG_STAGING_PATH}/Applications"

hdiutil create \
  -volname "${APP_NAME}" \
  -srcfolder "${DMG_STAGING_PATH}" \
  -ov \
  -format UDZO \
  "${DMG_PATH}"

codesign --force --sign "${CODE_SIGN_IDENTITY}" --timestamp "${DMG_PATH}"

if (( ${#NOTARY_ARGS[@]} > 0 )); then
  xcrun notarytool submit "${DMG_PATH}" --wait "${NOTARY_ARGS[@]}"
  xcrun stapler staple "${DMG_PATH}"
fi

shasum -a 256 "${ZIP_PATH}" "${DMG_PATH}" > "${CHECKSUMS_PATH}"

echo "Created release artifacts:"
echo "  ${ZIP_PATH}"
echo "  ${DMG_PATH}"
echo "  ${CHECKSUMS_PATH}"
