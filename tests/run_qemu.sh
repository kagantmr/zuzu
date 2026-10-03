#!/usr/bin/env bash
# Usage: tests/run_qemu.sh <board> <boot-manifest> [timeout-seconds] [log-file]
#   VERDICT=<name>      test whose "<name>: PASSED|FAILED" line ends the run
#                       (default: schedtest)
#   ALLOW_USER_FAULTS=1 tolerate "Oops! Segmentation fault" and "Oops! '...' killed:" lines,
#                       for tests that fault clients on purpose
#   SD_MANIFEST=<file>  build a fresh SD image from this manifest and boot with it
#
# Builds <board> with the given boot manifest, boots it under QEMU and waits
# for a "<VERDICT>: PASSED|FAILED" line. Exit status:
#   0  PASSED and no kernel fault in the log
#   1  FAILED, or "Oops!" / "Kernel fault" / "panic" in the log
#   2  timeout, bad arguments, or the build/boot did not start
set -u

if [ $# -lt 2 ]; then
    echo "usage: $0 <board> <boot-manifest> [timeout-seconds] [log-file]" >&2
    exit 2
fi

BOARD=$1
MANIFEST=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
TIMEOUT=${3:-120}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/zuzu-run.XXXXXX")
LOG=${4:-$OUT/qemu.log}
VERDICT=${VERDICT:-schedtest}
MAKE=$(command -v gmake || command -v make)

cd "$ROOT" || exit 2

SD_IMG=$ROOT/build/arm-$BOARD/sd.img
if [ -n "${SD_MANIFEST:-}" ]; then
    SD_MANIFEST=$(cd "$(dirname "$SD_MANIFEST")" && pwd)/$(basename "$SD_MANIFEST")
    if ! "$MAKE" BOARD="$BOARD" BOOT_MANIFEST="$MANIFEST" SD_MANIFEST="$SD_MANIFEST" O="$OUT" \
            sdimg >"$OUT/sdimg.log" 2>&1; then
        echo "sdimg failed; see $OUT/sdimg.log" >&2
        exit 2
    fi
    SD_IMG=$OUT/sd.img
fi
DRIVE=
[ -f "$SD_IMG" ] && DRIVE="-snapshot -drive file=$SD_IMG,if=sd,format=raw"

if ! "$MAKE" BOARD="$BOARD" BOOT_MANIFEST="$MANIFEST" O="$OUT" \
        "$OUT/zuzu.img" "$OUT/initrd.cpio" >"$OUT/build.log" 2>&1; then
    echo "build failed; see $OUT/build.log" >&2
    exit 2
fi

: >"$LOG"
"$MAKE" BOARD="$BOARD" BOOT_MANIFEST="$MANIFEST" O="$OUT" QEMU_DRIVE="$DRIVE" \
    run-direct >"$LOG" 2>&1 </dev/null &
MAKE_PID=$!

FaultLines()
{
    local lines
    lines=$(grep -a -E 'Oops!|Kernel fault|panic' "$LOG")
    if [ -n "${ALLOW_USER_FAULTS:-}" ]; then
        lines=$(printf '%s\n' "$lines" | grep -a -v -E "Oops! Segmentation fault|Oops! '[^']*' \(PID [0-9]+, TID [0-9]+\) killed:")
    fi
    printf '%s' "$lines" | grep -a -v '^$'
}

stop_qemu()
{
    pkill -f "$OUT/zuzu.img" 2>/dev/null
    kill "$MAKE_PID" 2>/dev/null
    wait "$MAKE_PID" 2>/dev/null
}
trap stop_qemu EXIT

verdict=
deadline=$((SECONDS + TIMEOUT))
while [ $SECONDS -lt $deadline ]; do
    if grep -a -q -E "$VERDICT: (PASSED|FAILED)" "$LOG"; then
        verdict=$(grep -a -o -E "$VERDICT: (PASSED|FAILED)" "$LOG" | tail -1)
        break
    fi
    if [ -n "$(FaultLines)" ]; then
        break
    fi
    kill -0 "$MAKE_PID" 2>/dev/null || break
    sleep 1
done

status=0
if [ -n "$(FaultLines)" ]; then
    echo "FAIL: kernel fault in log ($LOG)" >&2
    FaultLines | head -3 >&2
    status=1
fi
case "$verdict" in
*PASSED) ;;
*FAILED) echo "FAIL: $verdict ($LOG)" >&2; status=1 ;;
*)
    if [ $status -eq 0 ]; then
        echo "FAIL: timed out after ${TIMEOUT}s with no verdict ($LOG)" >&2
        status=2
    fi
    ;;
esac
[ $status -eq 0 ] && echo "PASS: $verdict ($LOG)"
exit $status
