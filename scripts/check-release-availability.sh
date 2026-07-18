#!/bin/zsh

set -euo pipefail

if (( $# != 2 )); then
  echo "usage: $0 OWNER/REPOSITORY X.Y.Z" >&2
  exit 2
fi

repository="$1"
marketing_version="${2#v}"

if [[ ! "${repository}" =~ '^[^/]+/[^/]+$' ]]; then
  echo "error: repository must use OWNER/REPOSITORY format, got ${repository}" >&2
  exit 1
fi
if [[ ! "${marketing_version}" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
  echo "error: marketing version must use X.Y.Z format, got $2" >&2
  exit 1
fi

owner="${repository%%/*}"
name="${repository#*/}"
tag="v${marketing_version}"
query='query($owner: String!, $name: String!, $qualifiedTag: String!, $tag: String!) {
  repository(owner: $owner, name: $name) {
    ref(qualifiedName: $qualifiedTag) { id }
    release(tagName: $tag) { id }
  }
}'

availability=$(gh api graphql \
  -f query="${query}" \
  -f owner="${owner}" \
  -f name="${name}" \
  -f qualifiedTag="refs/tags/${tag}" \
  -f tag="${tag}" \
  --jq '
    if .data.repository == null then "unknown"
    elif .data.repository.ref != null or .data.repository.release != null then "exists"
    else "available"
    end
  ')

case "${availability}" in
  available)
    echo "${tag} is available"
    ;;
  exists)
    echo "error: ${tag} has already been promoted; use a higher marketing version" >&2
    exit 1
    ;;
  *)
    echo "error: unable to determine whether ${tag} has already been promoted" >&2
    exit 1
    ;;
esac
