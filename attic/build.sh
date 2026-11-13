#!/bin/sh
set -e
cd "$(dirname "$0")"; nasm -f elf64 -O9 rifle.asm -o rifle.o
ld.lld -s -N --no-rosegment -o rifle-asm rifle.o
rm -f rifle.o
