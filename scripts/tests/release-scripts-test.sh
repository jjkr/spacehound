#!/bin/zsh

set -euo pipefail

root_dir=$(cd "${0:A:h}/../.." && pwd)
validator="${root_dir}/scripts/validate-release-version.sh"
availability_checker="${root_dir}/scripts/check-release-availability.sh"
notes_preparer="${root_dir}/scripts/prepare-release-notes.sh"
key_pair_validator="${root_dir}/scripts/validate-sparkle-key-pair.swift"
package_script="${root_dir}/scripts/package-release.sh"
symbol_uploader="${root_dir}/scripts/upload-sentry-symbols.sh"
candidate_workflow="${root_dir}/.github/workflows/release.yml"
promotion_workflow="${root_dir}/.github/workflows/promote-release.yml"
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

if missing_token_output=$(env DEVELOPMENT_TEAM=test-team SENTRY_DSN=test-dsn \
  "${package_script}" 2>&1); then
  echo "error: expected package script to reject a missing Sentry auth token" >&2
  exit 1
fi
grep -q 'SENTRY_AUTH_TOKEN must be set' <<< "${missing_token_output}"

if missing_dsn_output=$(env DEVELOPMENT_TEAM=test-team SENTRY_AUTH_TOKEN=test-token \
  "${package_script}" 2>&1); then
  echo "error: expected package script to reject a missing Sentry DSN" >&2
  exit 1
fi
grep -q 'SENTRY_DSN must be set' <<< "${missing_dsn_output}"

mkdir -p "${test_dir}/bin"
cat > "${test_dir}/bin/gh" <<'EOF'
#!/bin/zsh
case "${MOCK_GH_RESULT:-available}" in
  available|exists|unknown) print -r -- "${MOCK_GH_RESULT:-available}" ;;
  error) exit 1 ;;
esac
EOF
chmod +x "${test_dir}/bin/gh"

cat > "${test_dir}/bin/sentry-cli" <<'EOF'
#!/bin/zsh
print -rl -- "$@" > "${MOCK_SENTRY_ARGS_PATH}"
[[ "${MOCK_SENTRY_RESULT:-success}" == success ]]
EOF
chmod +x "${test_dir}/bin/sentry-cli"

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

mkdir -p "${test_dir}/dsyms/SpaceRabbit.app.dSYM"
mkdir -p "${test_dir}/empty-dsyms"
expect_failure env PATH="/usr/bin:/bin" \
  "${symbol_uploader}" "${test_dir}/dsyms"
expect_failure env PATH="/usr/bin:/bin" SENTRY_AUTH_TOKEN=test-token \
  "${symbol_uploader}" "${test_dir}/dsyms"
expect_failure env PATH="${checker_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/unused-args" \
  "${symbol_uploader}" "${test_dir}/missing-dsyms"
expect_failure env PATH="${checker_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/unused-args" \
  "${symbol_uploader}" "${test_dir}/empty-dsyms"
expect_failure env PATH="${checker_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_RESULT=fail MOCK_SENTRY_ARGS_PATH="${test_dir}/failed-args" \
  "${symbol_uploader}" "${test_dir}/dsyms"
env PATH="${checker_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/sentry-args" \
  "${symbol_uploader}" "${test_dir}/dsyms" >/dev/null
diff -u - "${test_dir}/sentry-args" <<EOF
debug-files
upload
--org
animaslabs
--project
spacerabbit
${test_dir}/dsyms
EOF

cat > "${test_dir}/notes.md" <<'EOF'
## Highlights

SpaceRabbit is faster than ever.
EOF
"${notes_preparer}" \
  1.2.3 1.2.3fc4 "${test_dir}/notes.md" "${test_dir}/dist" >/dev/null
beta_notes="${test_dir}/dist/beta/SpaceRabbit-1.2.3-fc4-arm64.md"
production_notes="${test_dir}/dist/production/SpaceRabbit-1.2.3-fc4-arm64.md"
grep -q '^# SpaceRabbit 1.2.3 (final candidate 4)$' "${beta_notes}"
grep -q '^# SpaceRabbit 1.2.3$' "${production_notes}"
grep -q '^SpaceRabbit is faster than ever\.$' "${beta_notes}"
grep -q '^SpaceRabbit is faster than ever\.$' "${production_notes}"
expect_failure "${notes_preparer}" \
  1.2.3 1.2.3fc4 "${test_dir}/missing.md" "${test_dir}/missing-dist"
print -r -- '<!-- RELEASE_NOTES_PLACEHOLDER -->' > "${test_dir}/placeholder.md"
expect_failure "${notes_preparer}" \
  1.2.3 1.2.3fc4 "${test_dir}/placeholder.md" "${test_dir}/placeholder-dist"

if grep -q '^  promote:' "${candidate_workflow}"; then
  echo "error: candidate workflow must not contain an automatic promotion job" >&2
  exit 1
fi
grep -q 'group: spacerabbit-release' "${candidate_workflow}"
grep -q 'Candidate run ID:.*GITHUB_RUN_ID' "${candidate_workflow}"
grep -q 'release-notes/v${RELEASE_VERSION}.md' "${candidate_workflow}"
grep -q 'SENTRY_DSN:.*vars.SENTRY_DSN' "${candidate_workflow}"
grep -q 'SENTRY_AUTH_TOKEN:.*secrets.SENTRY_AUTH_TOKEN' "${candidate_workflow}"
grep -q 'xcodegen cmake ninja sentry-cli' "${candidate_workflow}"
if grep -q 'generate-notes' "${candidate_workflow}"; then
  echo "error: candidate workflow must use authored release notes" >&2
  exit 1
fi
grep -q 'group: spacerabbit-release' "${promotion_workflow}"
grep -q 'GITHUB_ACTOR.*jjkr' "${promotion_workflow}"
grep -q 'GITHUB_REF.*refs/heads/main' "${promotion_workflow}"
grep -q 'workflow_path.*\.github/workflows/release\.yml' "${promotion_workflow}"
grep -q 'ref:.*steps\.candidate\.outputs\.sha' "${promotion_workflow}"
grep -q 'run-id:.*inputs\.candidate_run_id' "${promotion_workflow}"
grep -q -- '--target.*CANDIDATE_SHA' "${promotion_workflow}"

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
