#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

arg="${1:-gcc}"

cleanup() {
    printf "%s\n" "Cleaning stale socket"
    rm -f "${XDG_RUNTIME_DIR}/wayland-0"
}

trap cleanup EXIT

echo "Running program"
./build/"build-$arg"/mayday
