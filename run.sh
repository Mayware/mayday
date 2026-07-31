#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

arg="${1:-gcc}"

./build.sh "$arg"
./execute.sh "$arg"
