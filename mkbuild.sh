#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"
rm -rf build
rm -rf compile_commands.json

header() {
    echo -e "\033[0;32mGenerating $1\033[0m"
}

header "Clang"
cmake -B build/build-clang -G Ninja -Wno-author \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE="-O0 -DNDEBUG" \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -Wno-reserved-module-identifier -Wno-import-implementation-partition-unit-in-interface-unit" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"

GCC_FLAGS="-std=gnu++26 -freflection -DMAYQUILL_ICE"
LINKER_FLAGS="-Wl,-rpath,/opt/gcc-git/lib64 -fuse-ld=mold"

header "GCC"
cmake -B build/build-gcc -G Ninja -Wno-author \
    -DCMAKE_C_COMPILER=/opt/gcc-git/bin/gcc \
    -DCMAKE_CXX_COMPILER=/opt/gcc-git/bin/g++ \
    -DCMAKE_CXX_FLAGS="$GCC_FLAGS -O0 -DNDEBUG" \
    -DCMAKE_EXE_LINKER_FLAGS="$LINKER_FLAGS"

header "GCC [Debug]"
cmake -B build/build-debug -G Ninja -Wno-author \
-DCMAKE_C_COMPILER=/opt/gcc-git/bin/gcc \
    -DCMAKE_CXX_COMPILER=/opt/gcc-git/bin/g++ \
    -DCMAKE_CXX_FLAGS="$GCC_FLAGS -O0 -DDEBUG -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="$LINKER_FLAGS -fsanitize=address,undefined"

./gen.sh

ln -s build/build-clang/compile_commands.json compile_commands.json
