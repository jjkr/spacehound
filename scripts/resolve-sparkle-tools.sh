#!/bin/zsh
# SPDX-FileCopyrightText: 2026 Joe Kramer
# SPDX-License-Identifier: Apache-2.0

# Prints the directory containing the Sparkle CLI tools (generate_appcast,
# sign_update) that match the version pinned in project.yml.
#
# After package-release.sh has run, the tools are already present in the
# release derived-data directory. Otherwise the package graph is resolved
# without building anything, which downloads the pinned Sparkle artifact.

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DERIVED_DATA_PATH="${DERIVED_DATA_PATH:-${ROOT_DIR}/build/DerivedDataRelease}"
sparkle_bin="${DERIVED_DATA_PATH}/SourcePackages/artifacts/sparkle/Sparkle/bin"

if [[ ! -x "${sparkle_bin}/generate_appcast" || ! -x "${sparkle_bin}/sign_update" ]]; then
  if ! command -v xcodegen >/dev/null 2>&1; then
    echo "error: xcodegen is required to resolve the Sparkle package" >&2
    exit 1
  fi
  (
    cd "${ROOT_DIR}"
    xcodegen generate --spec project.yml >&2
    xcodebuild \
      -project SpaceHound.xcodeproj \
      -scheme SpaceHound \
      -derivedDataPath "${DERIVED_DATA_PATH}" \
      -resolvePackageDependencies >&2
  )
fi

for tool in generate_appcast sign_update; do
  if [[ ! -x "${sparkle_bin}/${tool}" ]]; then
    echo "error: Sparkle tool not found after package resolution: ${sparkle_bin}/${tool}" >&2
    exit 1
  fi
done

echo "${sparkle_bin}"
