#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

build() {
    if [ -d "build/build-$1" ]; then
        echo -e "\033[0;32mBuilding $1\033[0m"
        ninja -C "build/build-$1"
    fi
}

arg="${1:-gcc}"
case "$arg" in
    debug)
        build debug
        ;;
    clang)
        build clang
        ;;
    *)
        build gcc
        ;;
esac

cleanup() {
    printf "\n%s" "Cleaning stale socket"
    rm -f "${XDG_RUNTIME_DIR}/wayland-0"
}

trap cleanup EXIT

echo "Running program"
./build/"build-$arg"/mayday
