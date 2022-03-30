#!/bin/bash

set -ue

self="$(realpath "$0")"
top="$(dirname "$(dirname "${self}")")"

tmpdir="$(mktemp -d)"
pushd "${tmpdir}"

ok=true
for test in "${top}"/stage/linux64/build/**/*_test ; do
        if ! "${test}" ; then
                ok=false
        fi
done

popd
rm -rf "${tmpdir}" 2>/dev/null || true

${ok}
