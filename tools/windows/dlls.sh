#!/usr/bin/env bash
# dlls.sh <dir> [search dir ...]: the DLLs <dir>/shantae.exe needs beside it,
# one path per line: the ones it imports, the ones opened at run time (ANGLE's
# libEGL.dll, which SDL loads for the GL context, and librashader.dll when the
# build has one), and theirs in turn. Each comes from the first search dir that
# has it, or else from <dir> itself. Windows' own DLLs (System32, API sets) are
# left out; a DLL found nowhere is an error.
set -euo pipefail
dir=$1
shift
dirs=("$@")
READOBJ="${SHANTAE_MSYS2:-/c/msys64}/mingw64/bin/llvm-readobj.exe"
SYSTEM=/c/Windows/System32

declare -A seen
missing=0

resolve() {  # resolve <dll> <needed by>
    local name=$1 key=${1,,} d
    [ -z "${seen[$key]:-}" ] || return 0
    seen[$key]=1
    case "$key" in api-ms-win-*|ext-ms-win-*) return 0 ;; esac
    for d in "${dirs[@]}" "$dir"; do
        if [ -f "$d/$name" ]; then
            echo "$d/$name"
            queue+=("$d/$name")
            return 0
        fi
    done
    [ -f "$SYSTEM/$name" ] && return 0
    echo "$2 needs $name, which is neither in $dir${dirs[*]:+ or ${dirs[*]}} nor part of Windows" >&2
    missing=1
}

queue=("$dir/shantae.exe")
resolve libEGL.dll shantae.exe
[ -f "$dir/librashader.dll" ] && resolve librashader.dll shantae.exe
while [ ${#queue[@]} -gt 0 ]; do
    file=${queue[0]}
    queue=("${queue[@]:1}")
    while read -r name; do
        resolve "$name" "${file##*/}"
    done < <("$READOBJ" --coff-imports "$file" | sed -n 's/^ *Name: //p' | tr -d '\r')
done
exit $missing
