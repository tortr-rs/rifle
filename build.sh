#!/bin/sh
set -e
nasm -f elf64 -O9 rifle.asm -o rifle.o
ld.lld -s -N --no-rosegment -o rifle rifle.o
rm -f rifle.o
