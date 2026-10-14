#!/usr/bin/env bash
# Print the Tanara version from CMakeLists.txt (project(VERSION) + TANARA_VERSION_SUFFIX).
# With an argument (a release tag such as v0.5.11), fail unless the tag equals "v<version>".
# Usage: tools/ci/package-version.sh [tag]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CML="$ROOT/CMakeLists.txt"

num="$(sed -n 's/^project(tanara VERSION \([0-9][0-9.]*\).*/\1/p' "$CML" | head -n1)"
suffix="$(sed -n 's/^set(TANARA_VERSION_SUFFIX "\([^"]*\)").*/\1/p' "$CML" | head -n1)"
if [ -z "$num" ]; then
    echo "package-version: cannot read project(VERSION) from $CML" >&2
    exit 2
fi
version="${num}${suffix}"

if [ $# -ge 1 ]; then
    tag="${1#refs/tags/}"
    if [ "$tag" != "v$version" ]; then
        echo "package-version: tag '$tag' does not match the CMake version 'v$version' (project(VERSION) + TANARA_VERSION_SUFFIX in CMakeLists.txt)" >&2
        exit 1
    fi
fi
echo "$version"
