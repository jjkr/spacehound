#!/bin/zsh

set -euo pipefail

root_dir=$(cd "${0:A:h}/../.." && pwd)
validator="${root_dir}/scripts/validate-release-version.sh"
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

echo "Release script tests passed"
