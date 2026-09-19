#!/bin/zsh

set -euo pipefail

root_dir=$(cd "${0:A:h}/../.." && pwd)
validator="${root_dir}/scripts/validate-release-version.sh"
notes_preparer="${root_dir}/scripts/prepare-release-notes.sh"
key_pair_validator="${root_dir}/scripts/validate-sparkle-key-pair.swift"
promoter="${root_dir}/scripts/promote-appcast.swift"
feed_fetcher="${root_dir}/scripts/fetch-live-appcast.sh"
package_script="${root_dir}/scripts/package-release.sh"
symbol_uploader="${root_dir}/scripts/upload-sentry-symbols.sh"
release_workflow="${root_dir}/.github/workflows/release.yml"
promote_workflow="${root_dir}/.github/workflows/promote.yml"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/spacehound-release-tests.XXXXXX")
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

# --- validate-release-version.sh -------------------------------------------

for version in 0.1.0 1.0.0 12.34.56; do
  [[ "$("${validator}" "${version}")" == "${version}" ]]
  [[ "$("${validator}" "v${version}")" == "${version}" ]]
done

expect_failure "${validator}" 1.2
expect_failure "${validator}" 1.2.3.4
expect_failure "${validator}" 01.2.3
expect_failure "${validator}" 1.2.3fc1
expect_failure "${validator}" 1.2.3-beta.1
expect_failure "${validator}" ""

# --- package-release.sh fails before doing any work -------------------------

if missing_token_output=$(env -u SENTRY_AUTH_TOKEN \
  DEVELOPMENT_TEAM=test-team SENTRY_DSN=test-dsn \
  "${package_script}" 2>&1); then
  echo "error: expected package script to reject a missing Sentry auth token" >&2
  exit 1
fi
grep -q 'SENTRY_AUTH_TOKEN must be set' <<< "${missing_token_output}"

if missing_dsn_output=$(env -u SENTRY_DSN \
  DEVELOPMENT_TEAM=test-team SENTRY_AUTH_TOKEN=test-token \
  "${package_script}" 2>&1); then
  echo "error: expected package script to reject a missing Sentry DSN" >&2
  exit 1
fi
grep -q 'SENTRY_DSN must be set' <<< "${missing_dsn_output}"

# --- upload-sentry-symbols.sh -----------------------------------------------

mkdir -p "${test_dir}/bin"
cat > "${test_dir}/bin/sentry-cli" <<'EOF'
#!/bin/zsh
print -rl -- "$@" > "${MOCK_SENTRY_ARGS_PATH}"
[[ "${MOCK_SENTRY_RESULT:-success}" == success ]]
EOF
chmod +x "${test_dir}/bin/sentry-cli"
mock_path="${test_dir}/bin:${PATH}"

mkdir -p "${test_dir}/dsyms/SpaceHound.app.dSYM"
mkdir -p "${test_dir}/empty-dsyms"
expect_failure env PATH="/usr/bin:/bin" \
  "${symbol_uploader}" "${test_dir}/dsyms"
expect_failure env PATH="/usr/bin:/bin" SENTRY_AUTH_TOKEN=test-token \
  "${symbol_uploader}" "${test_dir}/dsyms"
expect_failure env PATH="${mock_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/unused-args" \
  "${symbol_uploader}" "${test_dir}/missing-dsyms"
expect_failure env PATH="${mock_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/unused-args" \
  "${symbol_uploader}" "${test_dir}/empty-dsyms"
expect_failure env PATH="${mock_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_RESULT=fail MOCK_SENTRY_ARGS_PATH="${test_dir}/failed-args" \
  "${symbol_uploader}" "${test_dir}/dsyms"
env PATH="${mock_path}" SENTRY_AUTH_TOKEN=test-token \
  MOCK_SENTRY_ARGS_PATH="${test_dir}/sentry-args" \
  "${symbol_uploader}" "${test_dir}/dsyms" >/dev/null
diff -u - "${test_dir}/sentry-args" <<EOF
debug-files
upload
--org
jjkr
--project
spacehound
${test_dir}/dsyms
EOF

# --- prepare-release-notes.sh -----------------------------------------------

cat > "${test_dir}/notes.md" <<'EOF'
## Highlights

SpaceHound is faster than ever.
EOF
"${notes_preparer}" 1.2.3 "${test_dir}/notes.md" "${test_dir}/dist/notes.md" >/dev/null
diff -u "${test_dir}/notes.md" "${test_dir}/dist/notes.md"

expect_failure "${notes_preparer}" 1.2 "${test_dir}/notes.md" "${test_dir}/dist/bad.md"
expect_failure "${notes_preparer}" 1.2.3 "${test_dir}/missing.md" "${test_dir}/dist/bad.md"
print -r -- '<!-- RELEASE_NOTES_PLACEHOLDER -->' > "${test_dir}/placeholder.md"
expect_failure "${notes_preparer}" 1.2.3 "${test_dir}/placeholder.md" "${test_dir}/dist/bad.md"
print -r -- 'No headings here.' > "${test_dir}/no-headings.md"
expect_failure "${notes_preparer}" 1.2.3 "${test_dir}/no-headings.md" "${test_dir}/dist/bad.md"
printf '# SpaceHound 1.2.3\n\n## Highlights\n' > "${test_dir}/h1.md"
expect_failure "${notes_preparer}" 1.2.3 "${test_dir}/h1.md" "${test_dir}/dist/bad.md"
[[ ! -e "${test_dir}/dist/bad.md" ]]

# --- workflows ----------------------------------------------------------------

for workflow in "${release_workflow}" "${promote_workflow}"; do
  grep -q 'group: spacehound-release' "${workflow}"
  grep -q 'environment: release' "${workflow}"
  grep -q 'cloudflare/wrangler-action' "${workflow}"
  grep -q 'fetch-live-appcast.sh' "${workflow}"
done
grep -q -- '--prerelease' "${release_workflow}"
grep -q 'release-notes/v${RELEASE_VERSION}.md' "${release_workflow}"
grep -q 'SENTRY_AUTH_TOKEN:.*secrets.SENTRY_AUTH_TOKEN' "${release_workflow}"
grep -q 'xcodegen cmake ninja sentry-cli' "${release_workflow}"
grep -q 'types: \[released\]' "${promote_workflow}"
grep -q 'promote-appcast.swift' "${promote_workflow}"
if grep -q 'package-release.sh' "${promote_workflow}"; then
  echo "error: promotion must never rebuild the app" >&2
  exit 1
fi

# --- updates site -------------------------------------------------------------

grep -q 'X-Robots-Tag: noindex' "${root_dir}/updates/public/_headers"
grep -q '^Disallow: /$' "${root_dir}/updates/public/robots.txt"

# --- version ordering against a published feed --------------------------------

cat > "${test_dir}/appcast.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
  <channel>
    <item>
      <sparkle:channel>beta</sparkle:channel>
      <sparkle:version>1.3.0</sparkle:version>
    </item>
    <item>
      <sparkle:version>1.2.3</sparkle:version>
    </item>
  </channel>
</rss>
EOF

[[ "$("${validator}" 1.3.1 "${test_dir}/appcast.xml")" == "1.3.1" ]]
[[ "$("${validator}" 2.0.0 "${test_dir}/appcast.xml")" == "2.0.0" ]]
expect_failure "${validator}" 1.3.0 "${test_dir}/appcast.xml"
expect_failure "${validator}" 1.2.4 "${test_dir}/appcast.xml"
expect_failure "${validator}" 1.2.3 "${test_dir}/appcast.xml"
[[ "$("${validator}" 1.10.0 "${test_dir}/appcast.xml")" == "1.10.0" ]]

# --- promote-appcast.swift ----------------------------------------------------

cat > "${test_dir}/promote.xml" <<'EOF'
<?xml version="1.0" standalone="yes"?><!-- sparkle-sign-warning:
keep me
--><rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
    <channel>
        <title>SpaceHound</title>
        <item>
            <title>1.4.0</title>
            <sparkle:channel>beta</sparkle:channel>
            <sparkle:version>1.4.0</sparkle:version>
            <enclosure url="https://example.invalid/SpaceHound-1.4.0-arm64.zip" length="1" type="application/octet-stream" sparkle:edSignature="sig"/>
        </item>
        <item>
            <title>1.3.1</title>
            <sparkle:channel>beta</sparkle:channel>
            <sparkle:version>1.3.1</sparkle:version>
        </item>
        <item>
            <title>1.3.0</title>
            <sparkle:version>1.3.0</sparkle:version>
        </item>
    </channel>
</rss><!-- sparkle-signatures:
edSignature: old
length: 1
-->
EOF
cat > "${test_dir}/promote-expected.xml" <<'EOF'
<?xml version="1.0" standalone="yes"?><!-- sparkle-sign-warning:
keep me
--><rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">
    <channel>
        <title>SpaceHound</title>
        <item>
            <title>1.4.0</title>
            <sparkle:version>1.4.0</sparkle:version>
            <enclosure url="https://example.invalid/SpaceHound-1.4.0-arm64.zip" length="1" type="application/octet-stream" sparkle:edSignature="sig"/>
        </item>
        <item>
            <title>1.3.0</title>
            <sparkle:version>1.3.0</sparkle:version>
        </item>
    </channel>
</rss><!-- sparkle-signatures:
edSignature: old
length: 1
-->
EOF
# XMLDocument does not write a trailing newline.
perl -pi -e 'chomp if eof' "${test_dir}/promote-expected.xml"

cp "${test_dir}/promote.xml" "${test_dir}/promote-work.xml"
xcrun swift "${promoter}" "${test_dir}/promote-work.xml" 1.4.0 >/dev/null
diff -u "${test_dir}/promote-expected.xml" "${test_dir}/promote-work.xml"

# Promoting again, a production version, an unknown version, or a bad version
# must all fail and leave the file untouched.
cp "${test_dir}/promote-work.xml" "${test_dir}/promote-untouched.xml"
expect_failure xcrun swift "${promoter}" "${test_dir}/promote-work.xml" 1.4.0
expect_failure xcrun swift "${promoter}" "${test_dir}/promote-work.xml" 1.3.0
expect_failure xcrun swift "${promoter}" "${test_dir}/promote-work.xml" 9.9.9
expect_failure xcrun swift "${promoter}" "${test_dir}/promote-work.xml" 1.4
expect_failure xcrun swift "${promoter}" "${test_dir}/missing.xml" 1.4.0
diff -u "${test_dir}/promote-untouched.xml" "${test_dir}/promote-work.xml"

# --- fetch-live-appcast.sh ----------------------------------------------------

cat > "${test_dir}/bin/curl" <<'EOF'
#!/bin/zsh
output=""
while (( $# > 0 )); do
  case "$1" in
    --output) output="$2"; shift 2 ;;
    --write-out) shift 2 ;;
    --header|--retry|--retry-delay) shift 2 ;;
    *) shift ;;
  esac
done
case "${MOCK_CURL_STATUS:-200}" in
  200) cp "${MOCK_CURL_BODY}" "${output}"; print -n 200 ;;
  404) print -n '' > "${output}"; print -n 404 ;;
  500) print -n '' > "${output}"; print -n 500 ;;
  down) exit 7 ;;
esac
EOF
chmod +x "${test_dir}/bin/curl"

env PATH="${mock_path}" MOCK_CURL_STATUS=200 MOCK_CURL_BODY="${test_dir}/appcast.xml" \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml" >/dev/null
diff -u "${test_dir}/appcast.xml" "${test_dir}/fetched/appcast.xml"

env PATH="${mock_path}" MOCK_CURL_STATUS=200 MOCK_CURL_BODY="${test_dir}/notes.md" \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml" >/dev/null 2>&1 && {
  echo "error: expected an invalid published feed to be rejected" >&2
  exit 1
}

expect_failure env PATH="${mock_path}" MOCK_CURL_STATUS=404 \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml"
env PATH="${mock_path}" MOCK_CURL_STATUS=404 ALLOW_MISSING_FEED=1 \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml" >/dev/null 2>&1
[[ ! -e "${test_dir}/fetched/appcast.xml" ]]
expect_failure env PATH="${mock_path}" MOCK_CURL_STATUS=500 ALLOW_MISSING_FEED=1 \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml"
[[ ! -e "${test_dir}/fetched/appcast.xml" ]]
expect_failure env PATH="${mock_path}" MOCK_CURL_STATUS=down ALLOW_MISSING_FEED=1 \
  "${feed_fetcher}" https://example.invalid/appcast.xml "${test_dir}/fetched/appcast.xml"

# --- validate-sparkle-key-pair.swift ------------------------------------------

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
