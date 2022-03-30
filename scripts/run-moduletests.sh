#!/bin/bash

cd "$(dirname "$(realpath "$0")")/.."

echo
echo "Module tests: start"
echo

set -e

get_arch () {
    case $(uname -m -s) in
        "Linux x86_64")
            echo linux64;;
        "Darwin x86_64")
            echo darwin;;
        *)
            return 1
    esac
}

arch=$(get_arch)
test_root_dir=stage/$arch/test
virtualenv_dir=$test_root_dir/pyenv

if [[ ! -d $virtualenv_dir ]]; then
    python3 -mvenv "$virtualenv_dir"
    "$virtualenv_dir/bin/pip" \
        install \
        -r test/requirements.txt
fi

export PYTHONDONTWRITEBYTECODE=1
ARCH=$arch "$virtualenv_dir"/bin/pytest \
    --basetemp=/tmp/pytest-sysvkit \
    --junitxml="$test_root_dir"/report.xml \
    -vv

echo
echo "Module tests: done"
echo
