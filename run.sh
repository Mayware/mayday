#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

arg="${1:-gcc}"

./build.sh "$arg"
./execute.sh "$arg"

cleanup() {
    printf "\n%s" "Cleaning stale socket"
    rm -f "${XDG_RUNTIME_DIR}/wayland-0"
}

trap cleanup EXIT

echo "Running program"
./build/"build-$arg"/mayday
