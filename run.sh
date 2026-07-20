#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

build() {
    if [ -d "build/build-$1" ]; then
        echo -e "\033[0;32mBuilding $1\033[0m"
        ninja -C "build/build-$1"
    fi
}

build clang
build gcc

cleanup() {
    printf "\n%s" "Cleaning stale socket"
    rm -f "${XDG_RUNTIME_DIR}/wayland-0"
}

trap cleanup EXIT

echo "Running program"
./build/build-gcc/mayday
