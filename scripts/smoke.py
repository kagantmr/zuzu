#!/usr/bin/env python3
"""Boot zuzu under QEMU, run zztest, exit nonzero on failure.

Usage: smoke.py [--prompt-only] [--timeout S] -- <qemu command line>
"""

import argparse
import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

ANSI = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")
PROMPT = b"zzsh ~>"
OK = b"ALL PASS"
BAD = (b"FAILED", b"FATAL:", b"PANIC", b"KERNEL PANIC")


class Timeout(Exception):
    pass


class Console:
    def __init__(self, cmd):
        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True,
            preexec_fn=os.setsid)
        os.close(slave)
        self.log = b""
        self.pos = 0

    def _pump(self, deadline):
        left = deadline - time.monotonic()
        if left <= 0:
            raise Timeout()
        if not select.select([self.master], [], [], min(left, 0.5))[0]:
            if self.proc.poll() is not None:
                raise Timeout()
            return
        try:
            chunk = os.read(self.master, 4096)
        except OSError:
            raise Timeout()
        if not chunk:
            raise Timeout()
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        self.log += chunk

    # Strip ANSI over the whole accumulated buffer, never per-chunk: an escape
    # sequence split across two reads would otherwise survive unstripped.
    def _flat(self):
        return ANSI.sub(b"", self.log).replace(b"\r", b"")

    def wait_for(self, needle, timeout, bad=BAD):
        deadline = time.monotonic() + timeout
        while True:
            flat = self._flat()
            hit = flat.find(needle, self.pos)
            if hit >= 0:
                self.pos = hit + len(needle)
                return
            for b in bad:
                if flat.find(b, self.pos) >= 0:
                    raise RuntimeError(f"saw {b.decode()!r} before {needle.decode()!r}")
            self._pump(deadline)

    def send(self, line):
        os.write(self.master, line + b"\r")

    def close(self):
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait()
        os.close(self.master)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt-only", action="store_true",
                    help="boot to the shell prompt and stop (boards without zztest)")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == "--" else args.cmd
    if not cmd:
        ap.error("no QEMU command given")

    con = Console(cmd)
    try:
        con.wait_for(PROMPT, args.timeout)
        if args.prompt_only:
            print("\n[smoke] reached shell prompt", file=sys.stderr)
            return 0
        con.send(b"cd bin")
        con.wait_for(PROMPT, 15.0)
        con.send(b"zztest")
        con.wait_for(OK, args.timeout)
        print("\n[smoke] zztest: ALL PASS", file=sys.stderr)
        return 0
    except Timeout:
        print(f"\n[smoke] TIMEOUT after {args.timeout}s", file=sys.stderr)
        sys.stderr.buffer.write(b"--- last 2KB ---\n" + con.log[-2048:] + b"\n")
        return 1
    except RuntimeError as e:
        print(f"\n[smoke] FAIL: {e}", file=sys.stderr)
        sys.stderr.buffer.write(b"--- last 2KB ---\n" + con.log[-2048:] + b"\n")
        return 1
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
