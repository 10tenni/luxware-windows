#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
x86_64-w64-mingw32-windres -I. app.rc -O coff -o build/app-resource.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -fmax-errors=5 -DNOMINMAX -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -municode -mwindows -static -Isrc -Ivendor src/main.cpp build/app-resource.o -o Luxware.exe -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -ldwmapi -ld3d11 -ldxgi -lwinmm
