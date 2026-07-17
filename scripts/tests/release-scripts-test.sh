#!/bin/zsh

set -euo pipefail

root_dir=$(cd "${0:A:h}/../.." && pwd)
validator="${root_dir}/scripts/validate-release-version.sh"
key_pair_validator="${root_dir}/scripts/validate-sparkle-key-pair.swift"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/spacerabbit-release-tests.XXXXXX")
function cleanup() {
  rm -rf "${test_dir}"
}
trap cleanup EXIT

function expect_failure() {
  if "$@" >/dev/null 2>&1; then
    echo "error: expected command to fail: $*" >&2
    exit 1
  fi
}

for version in 0.1.0 1.0.0 12.34.56; do
  actual=$("${validator}" "${version}")
  [[ "${actual}" == "${version}" ]]
done

for version in v1 1.2 1.2.3.4 1.2-beta abc; do
  expect_failure "${validator}" "${version}"
done

cat > "${test_dir}/appcast.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <item><sparkle:version>1.2.3</sparkle:version></item>
  </channel>
</rss>
EOF

[[ "$("${validator}" 1.2.4 "${test_dir}/appcast.xml")" == "1.2.4" ]]
[[ "$("${validator}" 1.3.0 "${test_dir}/appcast.xml")" == "1.3.0" ]]
[[ "$("${validator}" 2.0.0 "${test_dir}/appcast.xml")" == "2.0.0" ]]
expect_failure "${validator}" 1.2.3 "${test_dir}/appcast.xml"
expect_failure "${validator}" 1.2.2 "${test_dir}/appcast.xml"
expect_failure "${validator}" 0.99.99 "${test_dir}/appcast.xml"

# RFC 8032 test vector 1: a known Ed25519 seed and its public key.
private_key="nWGxne/9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A="
public_key="11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo="
mismatched_public_key="AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="

print -rn -- "${private_key}" | xcrun swift "${key_pair_validator}" "${public_key}"
if print -rn -- "${private_key}" | xcrun swift \
  "${key_pair_validator}" "${mismatched_public_key}" >/dev/null 2>&1; then
  echo "error: expected mismatched Sparkle key pair to fail validation" >&2
  exit 1
fi

echo "Release script tests passed"
