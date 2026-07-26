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
build "$arg"
