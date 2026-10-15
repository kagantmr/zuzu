#!/bin/sh
# Strict documentation check over the given paths (files or directories):
#   1. every .c/.h has a @file block (Doxygen can't enforce this itself)
#   2. Doxygen reports zero warnings (docs/Doxyfile: undocumented anything fails)
#   3. clang -Wdocumentation is clean (@param names, commands, @return on void)
# Usage: scripts/doc_check.sh <kernel-flags-file> <user-flags-file> <path>...
set -u

kflags_file=$1
uflags_file=$2
shift 2

if [ $# -eq 0 ]; then
    echo "  DOC     nothing in docs/doc-scope yet"
    exit 0
fi

CLANG=${CLANG:-clang}
CLANG_DOC="--target=armv7a-none-eabi -fsyntax-only -Wno-everything -Wdocumentation \
-Wdocumentation-unknown-command -Werror"
files=$(find "$@" \( -name '*.c' -o -name '*.h' \) -not -path '*/dtb/*' | sort)
fail=0

missing=$(for f in $files; do grep -qE '[@\\]file' "$f" || echo "$f"; done)
if [ -n "$missing" ]; then
    echo "  DOC     no @file block:"
    echo "$missing" | sed 's/^/          /'
    fail=1
fi

mkdir -p docs/doxygen
if ! ZUZU_DOC_INPUT="docs/groups.dox $*" doxygen docs/Doxyfile >/dev/null 2>&1; then
    echo "  DOC     doxygen: $(grep -c . docs/doxygen/warnings.log) warning line(s):"
    sed 's/^/          /' docs/doxygen/warnings.log
    fail=1
fi

in_scope() {
    p=${1#./}
    for s in $scope; do
        case "$p" in "${s%/}"|"${s%/}"/*) return 0 ;; esac
    done
    return 1
}
scope="$*"

for f in $files; do
    case "$f" in
    *.c) flags_file=$kflags_file ;;
    include/*.h) flags_file=$uflags_file ;;
    *) continue ;;
    esac
    out=$(eval "$CLANG $CLANG_DOC $(cat "$flags_file") -x c $f" 2>&1)
    hits=$(echo "$out" | grep -E '^[^ :]+:[0-9]+:[0-9]+: (error|warning):' |
        while IFS=: read -r path line col rest; do
            in_scope "$path" && echo "$path:$line:$col:$rest"
        done | sort -u)
    if [ -n "$hits" ]; then
        echo "  DOC     clang -Wdocumentation ($f):"
        echo "$hits" | sed 's/^/          /'
        fail=1
    fi
done

[ $fail -eq 0 ] && echo "  DOC     clean ($(echo "$files" | wc -l | tr -d ' ') files)"
exit $fail
