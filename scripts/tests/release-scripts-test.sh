#!/bin/zsh

set -euo pipefail

root_dir=$(cd "${0:A:h}/../.." && pwd)
validator="${root_dir}/scripts/validate-release-version.sh"
availability_checker="${root_dir}/scripts/check-release-availability.sh"
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

for pair in "0.1.0 0.1.0fc1" "1.0.0 1.0.0fc255" "12.34.56 12.34.56fc7"; do
  marketing="${pair%% *}"
  bundle="${pair##* }"
  [[ "$("${validator}" "${marketing}" "${bundle}")" == "${bundle}" ]]
done

expect_failure "${validator}" 1.2 1.2fc1
expect_failure "${validator}" 1.2.3 1.2.3
expect_failure "${validator}" 1.2.3 1.2.4fc1
expect_failure "${validator}" 1.2.3 1.2.3fc0
expect_failure "${validator}" 1.2.3 1.2.3fc256
expect_failure "${validator}" 1.2.3 1.2.3fc01

mkdir -p "${test_dir}/bin"
cat > "${test_dir}/bin/gh" <<'EOF'
#!/bin/zsh
case "${MOCK_GH_RESULT:-available}" in
  available|exists|unknown) print -r -- "${MOCK_GH_RESULT:-available}" ;;
  error) exit 1 ;;
esac
EOF
chmod +x "${test_dir}/bin/gh"

checker_path="${test_dir}/bin:${PATH}"
[[ "$(env PATH="${checker_path}" MOCK_GH_RESULT=available \
  "${availability_checker}" animaslabs/spacerabbit 1.2.3)" == "v1.2.3 is available" ]]
expect_failure env PATH="${checker_path}" MOCK_GH_RESULT=exists \
  "${availability_checker}" animaslabs/spacerabbit 1.2.3
expect_failure env PATH="${checker_path}" MOCK_GH_RESULT=unknown \
  "${availability_checker}" animaslabs/spacerabbit 1.2.3
expect_failure env PATH="${checker_path}" MOCK_GH_RESULT=error \
  "${availability_checker}" animaslabs/spacerabbit 1.2.3
expect_failure "${availability_checker}" invalid-repository 1.2.3
expect_failure "${availability_checker}" animaslabs/spacerabbit 1.2

cat > "${test_dir}/appcast.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <item><sparkle:version>1.2.3fc2</sparkle:version></item>
  </channel>
</rss>
EOF

[[ "$("${validator}" 1.2.3 1.2.3fc3 "${test_dir}/appcast.xml")" == "1.2.3fc3" ]]
[[ "$("${validator}" 1.2.4 1.2.4fc1 "${test_dir}/appcast.xml")" == "1.2.4fc1" ]]
[[ "$("${validator}" 2.0.0 2.0.0fc1 "${test_dir}/appcast.xml")" == "2.0.0fc1" ]]
expect_failure "${validator}" 1.2.3 1.2.3fc2 "${test_dir}/appcast.xml"
expect_failure "${validator}" 1.2.3 1.2.3fc1 "${test_dir}/appcast.xml"

sed -i '' 's/1.2.3fc2/1.2.3/' "${test_dir}/appcast.xml"
expect_failure "${validator}" 1.2.3 1.2.3fc255 "${test_dir}/appcast.xml"
[[ "$("${validator}" 1.2.4 1.2.4fc1 "${test_dir}/appcast.xml")" == "1.2.4fc1" ]]

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
