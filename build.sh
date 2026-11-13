#!/bin/sh
# builds rifle with clang and lld, then links every tool name to it in bin/. no GNU toolchain, no libc: c.h is the libc
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-empty-body -Wno-deprecated-non-prototype -Wno-unused-parameter \
    -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-pie -ffunction-sections -fdata-sections -fuse-ld=lld -Wl,--gc-sections -Wl,-s -Wl,-z,norelro \
    -o rifle rifle.c
rm -rf bin && mkdir bin
./rifle --install bin
