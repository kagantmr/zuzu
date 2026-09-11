#!/usr/bin/env sh
# Capture or compare a per-board build baseline: the raw kernel image and its
# symbol table. zuzu.img carries no DWARF, so it survives debug-info churn and
# stays a tight invariant on what actually boots across a build-system change.
#
#   scripts/baseline.sh save     [board...]
#   scripts/baseline.sh check    [board...]
set -eu

MAKE=${MAKE:-gmake}
DIR=${BASELINE_DIR:-.baseline}
CROSS=${CROSS:-arm-none-eabi-}


export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1700000000}

cmd=${1:-check}
shift 2>/dev/null || true
boards=${*:-}
[ -n "$boards" ] || boards=$($MAKE -s print-BOARDS)

fail=0

for b in $boards; do
    $MAKE -j"$(getconf _NPROCESSORS_ONLN)" BOARD="$b" kernel img >/dev/null
    # Ask make where it put things rather than assuming the layout.
    img=$($MAKE -s BOARD="$b" print-IMG)
    elf=$($MAKE -s BOARD="$b" print-TARGET)
    case "$cmd" in
    save)
        mkdir -p "$DIR"
        cp "$img" "$DIR/$b.img"
        "${CROSS}nm" --defined-only -n "$elf" | awk '{print $1, $3}' > "$DIR/$b.nm"
        echo "  SAVE    $b  ($(wc -c < "$DIR/$b.img" | tr -d ' ') bytes)"
        ;;
    check)
        if [ ! -f "$DIR/$b.img" ]; then
            echo "  MISS    $b  no baseline; run: scripts/baseline.sh save"
            fail=1
            continue
        fi
        if cmp -s "$img" "$DIR/$b.img"; then
            echo "  OK      $b  img identical"
        else
            echo "  DIFF    $b  img DIFFERS from baseline"
            fail=1
        fi
        "${CROSS}nm" --defined-only -n "$elf" | awk '{print $1, $3}' > "$DIR/$b.nm.new"
        if ! diff -q "$DIR/$b.nm" "$DIR/$b.nm.new" >/dev/null; then
            echo "  DIFF    $b  symbols differ:"
            diff "$DIR/$b.nm" "$DIR/$b.nm.new" | head -20
            fail=1
        fi
        rm -f "$DIR/$b.nm.new"
        ;;
    *)
        echo "usage: $0 {save|check} [board...]" >&2
        exit 2
        ;;
    esac
done

exit $fail
