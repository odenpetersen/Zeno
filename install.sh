#!/usr/bin/env sh
# Build Zeno and install it for the current user:
#   ~/.local/bin/zeno  and  ~/.local/share/zeno/zeno.hpp
# Usage: ./install.sh [prefix]     (default prefix: ~/.local)
set -eu
cd "$(dirname "$0")"
PREFIX="${1:-$HOME/.local}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "zeno needs $1: $2"; exit 1; }; }
need make "sudo apt install make"
need g++ "sudo apt install g++   (version 11 or newer)"
major=$(g++ -dumpfullversion | cut -d. -f1)
[ "$major" -ge 11 ] || { echo "zeno needs g++ 11 or newer (found $(g++ -dumpfullversion))"; exit 1; }
for t in gdb objdump vim git; do command -v "$t" >/dev/null 2>&1 || echo "note: $t not found; some features are off (zeno doctor explains)"; done

make -j"$(nproc 2>/dev/null || echo 4)"
mkdir -p "$PREFIX/bin" "$PREFIX/share/zeno"
cp zeno "$PREFIX/bin/zeno"
cp include/zeno.hpp "$PREFIX/share/zeno/zeno.hpp"
echo "installed $PREFIX/bin/zeno"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "add $PREFIX/bin to your PATH, e.g.:  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.bashrc" ;; esac

"$PREFIX/bin/zeno" doctor || true
echo
echo "Start:  zeno new hello && zeno web hello"
