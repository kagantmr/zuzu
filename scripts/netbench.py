#!/usr/bin/env python3
"""Host side of netbench: drive the Pi's netbench server and print results.

Usage: netbench.py <pi-ip> [--tests tcp-rx,tcp-tx,udp-echo,udp-rx,ping] [--repeat 3]

Wire protocol (all integers little-endian):
  tcp-rx   port 5201  host sends u64 N, then N bytes; Pi replies u64 bytes received.
  tcp-tx   port 5202  host sends u64 N; Pi sends N bytes and closes.
  udp-echo port 5203  Pi echoes every datagram unchanged.
  udp-rx   port 5204  datagrams start with u32 seq; a datagram "done" makes the Pi
                      reply u32 received, u32 highest_seq + 1, and reset its counters.
"""

import argparse
import socket
import statistics
import struct
import subprocess
import sys
import time

PORT_TCP_RX = 5201
PORT_TCP_TX = 5202
PORT_UDP_ECHO = 5203
PORT_UDP_RX = 5204
DGRAM_MAX = 1472


def recv_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"closed after {len(buf)} of {n} bytes")
        buf += chunk
    return bytes(buf)


def tcp_rx(ip, size):
    payload = bytes(range(256)) * 256
    with socket.create_connection((ip, PORT_TCP_RX), timeout=30) as s:
        start = time.perf_counter()
        s.sendall(struct.pack("<Q", size))
        left = size
        while left:
            n = min(left, len(payload))
            s.sendall(payload[:n])
            left -= n
        (got,) = struct.unpack("<Q", recv_exact(s, 8))
        elapsed = time.perf_counter() - start
    if got != size:
        raise RuntimeError(f"Pi received {got} of {size} bytes")
    return size * 8 / elapsed / 1e6


def tcp_tx(ip, size):
    with socket.create_connection((ip, PORT_TCP_TX), timeout=30) as s:
        start = time.perf_counter()
        s.sendall(struct.pack("<Q", size))
        got = 0
        while got < size:
            chunk = s.recv(65536)
            if not chunk:
                break
            got += len(chunk)
        elapsed = time.perf_counter() - start
    if got != size:
        raise RuntimeError(f"host received {got} of {size} bytes")
    return size * 8 / elapsed / 1e6


def udp_echo(ip, count, size):
    rtts, lost = [], 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(1.0)
        for seq in range(count):
            msg = struct.pack("<I", seq).ljust(size, b"\0")
            start = time.perf_counter()
            s.sendto(msg, (ip, PORT_UDP_ECHO))
            try:
                while True:
                    data, _ = s.recvfrom(DGRAM_MAX)
                    if data[:4] == msg[:4]:
                        break
                rtts.append((time.perf_counter() - start) * 1e3)
            except socket.timeout:
                lost += 1
    return rtts, lost


def udp_rx(ip, count, size, pps):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        gap = 1.0 / pps if pps else 0.0
        start = time.perf_counter()
        for seq in range(count):
            s.sendto(struct.pack("<I", seq).ljust(size, b"\0"), (ip, PORT_UDP_RX))
            if gap:
                target = start + (seq + 1) * gap
                while time.perf_counter() < target:
                    pass
        elapsed = time.perf_counter() - start
        s.settimeout(1.0)
        for _ in range(5):
            s.sendto(b"done", (ip, PORT_UDP_RX))
            try:
                data, _ = s.recvfrom(DGRAM_MAX)
                if len(data) >= 8:
                    received, highest = struct.unpack("<II", data[:8])
                    return received, highest, elapsed
            except socket.timeout:
                continue
    raise RuntimeError("no reply to 'done'")


def ping(ip, count):
    out = subprocess.run(["ping", "-c", str(count), "-i", "0.2", ip],
                         capture_output=True, text=True).stdout
    return out.strip().splitlines()[-2:]


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p / 100))]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ip")
    ap.add_argument("--tests", default="tcp-rx,tcp-tx,udp-echo,udp-rx,ping")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--tcp-bytes", type=int, default=16 << 20)
    ap.add_argument("--echo-count", type=int, default=1000)
    ap.add_argument("--echo-size", type=int, default=64)
    ap.add_argument("--rx-count", type=int, default=20000)
    ap.add_argument("--rx-size", type=int, default=DGRAM_MAX)
    ap.add_argument("--rx-pps", type=int, default=0, help="0 = as fast as possible")
    args = ap.parse_args()
    tests = args.tests.split(",")

    for name, fn in (("tcp-rx", tcp_rx), ("tcp-tx", tcp_tx)):
        if name in tests:
            runs = [fn(args.ip, args.tcp_bytes) for _ in range(args.repeat)]
            print(f"{name:9} median {statistics.median(runs):7.1f} Mb/s  "
                  f"runs {' '.join(f'{r:.1f}' for r in runs)}")

    if "udp-echo" in tests:
        rtts, lost = udp_echo(args.ip, args.echo_count, args.echo_size)
        if rtts:
            print(f"udp-echo  min {min(rtts):.3f} p50 {pct(rtts, 50):.3f} "
                  f"p99 {pct(rtts, 99):.3f} max {max(rtts):.3f} ms  lost {lost}/{args.echo_count}")
        else:
            print(f"udp-echo  no replies, lost {lost}/{args.echo_count}")

    if "udp-rx" in tests:
        received, highest, elapsed = udp_rx(args.ip, args.rx_count, args.rx_size, args.rx_pps)
        sent_mbps = args.rx_count * args.rx_size * 8 / elapsed / 1e6
        loss = 100.0 * (args.rx_count - received) / args.rx_count
        print(f"udp-rx    sent {sent_mbps:.1f} Mb/s  received {received}/{args.rx_count} "
              f"({loss:.2f}% loss), highest seq {highest}")

    if "ping" in tests:
        for line in ping(args.ip, 100):
            print(f"ping      {line}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as e:
        sys.exit(f"netbench: {e}")
