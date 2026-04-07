#!/bin/zsh

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_IF_NEEDED=0

while (( $# > 0 )); do
  case "$1" in
    --build-if-needed)
      BUILD_IF_NEEDED=1
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      exit 1
      ;;
  esac
  shift
done

function append_candidates() {
  local base_dir="$1"

  if [[ -z "${base_dir}" || ! -d "${base_dir}" ]]; then
    return
  fi

  candidates+=(
    "${base_dir}/build/ninja-release/spacerabbitd"
    "${base_dir}/build/ninja-release/package-shared/bin/spacerabbitd"
    "${base_dir}/build/ninja-release/package-static/bin/spacerabbitd"
    "${base_dir}/build/ninja-debug/spacerabbitd"
    "${base_dir}/build/ninja-debug/package-shared/bin/spacerabbitd"
    "${base_dir}/build/ninja-debug/package-static/bin/spacerabbitd"
  )
}

function find_daemon() {
  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  return 1
}

function build_core_if_possible() {
  local core_dir="$1"

  if [[ -n "${SPACERABBIT_CORE_BUILD_COMMAND:-}" ]]; then
    (
      cd "${ROOT_DIR}"
      zsh -lc "${SPACERABBIT_CORE_BUILD_COMMAND}"
    )
    return
  fi

  if [[ -x "${core_dir}/scripts/ci-build-spacerabbitd.sh" ]]; then
    (
      cd "${core_dir}"
      ./scripts/ci-build-spacerabbitd.sh
    )
    return
  fi

  if [[ -f "${core_dir}/Makefile" ]]; then
    make -C "${core_dir}" release
    return
  fi

  echo "error: spacerabbitd not found and no build hook is configured." >&2
  echo "Set SPACERABBIT_CORE_BUILD_COMMAND, add spacerabbit-core/scripts/ci-build-spacerabbitd.sh, or provide a core Makefile release target." >&2
  exit 1
}

if [[ -n "${SPACERABBITD_PATH:-}" && -x "${SPACERABBITD_PATH}" ]]; then
  echo "${SPACERABBITD_PATH}"
  exit 0
fi

primary_core_dir="${SPACERABBIT_CORE_DIR:-}"
if [[ -z "${primary_core_dir}" && -d "${ROOT_DIR}/spacerabbit-core" ]]; then
  primary_core_dir="${ROOT_DIR}/spacerabbit-core"
fi

fallback_core_dirs=()
if [[ -n "${primary_core_dir}" ]]; then
  fallback_core_dirs+=("${ROOT_DIR}/../spacerabbit-core")
else
  fallback_core_dirs+=(
    "${ROOT_DIR}/spacerabbit-core"
    "${ROOT_DIR}/../spacerabbit-core"
  )
fi

if [[ -n "${primary_core_dir}" && -d "${primary_core_dir}" ]]; then
  candidates=()
  append_candidates "${primary_core_dir}"
  if daemon_path=$(find_daemon); then
    echo "${daemon_path}"
    exit 0
  fi

  if [[ "${BUILD_IF_NEEDED}" == "1" ]]; then
    build_core_if_possible "${primary_core_dir}"
    candidates=()
    append_candidates "${primary_core_dir}"
    if daemon_path=$(find_daemon); then
      echo "${daemon_path}"
      exit 0
    fi
  fi
fi

candidates=()
for core_dir in "${fallback_core_dirs[@]}"; do
  append_candidates "${core_dir}"
done

if daemon_path=$(find_daemon); then
  echo "${daemon_path}"
  exit 0
fi

if [[ "${BUILD_IF_NEEDED}" != "1" ]]; then
  echo "error: spacerabbitd not found in known locations." >&2
  exit 1
fi

for core_dir in "${fallback_core_dirs[@]}"; do
  if [[ -d "${core_dir}" ]]; then
    build_core_if_possible "${core_dir}"
    candidates=()
    append_candidates "${core_dir}"
    if daemon_path=$(find_daemon); then
      echo "${daemon_path}"
      exit 0
    fi
  fi
done

echo "error: spacerabbitd could not be built or located." >&2
exit 1
