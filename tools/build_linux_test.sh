#!/bin/bash
# Builds the Linux test program (the bot without its window) used by the automatic tests.
set -e
cd "$(dirname "$0")/.."
OUT=${1:-build}
mkdir -p "$OUT"
for f in miniz miniz_tdef miniz_tinfl miniz_zip; do gcc -O2 -Ithird_party/miniz -c third_party/miniz/$f.c -o "$OUT/$f.o"; done
for f in common net webdriver scanner splitter drivers crypto remote core cli_main; do
  g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party -Ithird_party/miniz -c src/$f.cpp -o "$OUT/$f.o" &
done
wait
g++ -o "$OUT/ecbot_test" "$OUT"/*.o -lpthread
