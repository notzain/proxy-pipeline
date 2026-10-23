#!/usr/bin/env python3
"""
send_ingress.py - Utility to send test messages and data streams to Proxy Ingress handlers.

Supports:
  - UDP datagram transmission (default) or TCP streaming.
  - Sending built-in sample domain payloads (e.g. valid/invalid XML orders).
  - Sending custom messages, files, or streaming from STDIN.
  - Rate-limited continuous transmission (burst, count, interval).

Examples:
  # Send a single default valid order to 127.0.0.1:9123 (UDP)
  ./scripts/send_ingress.py

  # Send continuous stream of random orders every 500ms
  ./scripts/send_ingress.py --sample random_order --interval 0.5 --count 10

  # Send a custom message or XML string
  ./scripts/send_ingress.py --message '<order><customer>Bob</customer><item>Gadget</item><quantity>99</quantity></order>'

  # Pipe data from stdin
  echo "Hello Ingress!" | ./scripts/send_ingress.py --stdin

  # Send contents of a file
  ./scripts/send_ingress.py --file payload.xml
"""

from __future__ import annotations

import argparse
import random
import socket
import sys
import time
from pathlib import Path


SAMPLE_CUSTOMERS = ["Alice", "Bob", "Charlie", "Diana", "Evan", "Fiona"]
SAMPLE_ITEMS = ["Widget", "Gadget", "HyperDrive", "Sensor-X", "Relay-Core", "Quantum-Mod"]


def generate_sample(sample_type: str) -> str:
    if sample_type == "valid_order":
        return "<order><customer>Alice</customer><item>Widget</item><quantity>42</quantity></order>"
    elif sample_type == "invalid_order":
        # Missing required quantity tag and customer violates schema
        return "<order><customer></customer><item>Bad-Widget</item></order>"
    elif sample_type == "random_order":
        customer = random.choice(SAMPLE_CUSTOMERS)
        item = random.choice(SAMPLE_ITEMS)
        qty = random.randint(1, 1000)
        return f"<order><customer>{customer}</customer><item>{item}</item><quantity>{qty}</quantity></order>"
    elif sample_type == "plain":
        return "Ping from send_ingress.py"
    else:
        return f"<order><customer>TestUser</customer><item>DefaultItem</item><quantity>1</quantity></order>"


def parse_bandwidth(bandwidth_str: str) -> float:
    """
    Parses bandwidth string into bytes per second.
    Supports units: B, KB, MB, GB, bps, Kbps, Mbps, Gbps (with or without /s).
    """
    s = bandwidth_str.strip().lower().rstrip("/s").rstrip("ps")
    unit_multipliers = {
        "k": 1024,
        "kb": 1024,
        "m": 1024 * 1024,
        "mb": 1024 * 1024,
        "g": 1024 * 1024 * 1024,
        "gb": 1024 * 1024 * 1024,
        "b": 1,
    }
    # Check for bit rates (e.g. mbps)
    is_bits = "b" in bandwidth_str.lower() and not "bytes" in bandwidth_str.lower() and "bps" in bandwidth_str.lower()
    
    num_part = ""
    unit_part = ""
    for ch in s:
        if ch.isdigit() or ch == ".":
            num_part += ch
        else:
            unit_part += ch

    val = float(num_part)
    unit_part = unit_part.strip()
    multiplier = unit_multipliers.get(unit_part, 1024 * 1024 if not unit_part else 1)
    bytes_per_sec = val * multiplier
    if is_bits:
        bytes_per_sec /= 8.0
    return max(1.0, bytes_per_sec)


def send_udp(host: str, port: int, data: bytes) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        return sock.sendto(data, (host, port))


def send_tcp(host: str, port: int, data: bytes) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((host, port))
        sock.sendall(data)
        return len(data)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send test data payloads and streams to Proxy Ingress handlers.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument(
        "--host",
        type=str,
        default="127.0.0.1",
        help="Target host or IP address (default: 127.0.0.1)",
    )
    parser.add_argument(
        "-p", "--port",
        type=int,
        default=9123,
        help="Target port (default: 9123)",
    )
    parser.add_argument(
        "--proto",
        choices=["udp", "tcp"],
        default="udp",
        help="Transport protocol (default: udp)",
    )
    parser.add_argument(
        "-m", "--message",
        type=str,
        help="Custom text or payload string to send",
    )
    parser.add_argument(
        "-f", "--file",
        type=Path,
        help="Path to a file whose contents will be sent",
    )
    parser.add_argument(
        "--sample",
        choices=["valid_order", "invalid_order", "random_order", "plain"],
        default="valid_order",
        help="Predefined sample payload to send (default: valid_order)",
    )
    parser.add_argument(
        "-b", "--burst",
        type=int,
        default=1,
        help="Number of datagrams to fire in an immediate zero-delay burst (default: 1)",
    )
    parser.add_argument(
        "-c", "--count",
        type=int,
        default=1,
        help="Number of bursts/transmissions to send (0 for infinite loop, default: 1)",
    )
    parser.add_argument(
        "-i", "--interval",
        type=float,
        default=1.0,
        help="Delay in seconds between successive transmissions (default: 1.0s)",
    )
    parser.add_argument(
        "--stdin",
        action="store_true",
        help="Read lines from standard input and send each line as a message",
    )
    parser.add_argument(
        "--bandwidth",
        type=str,
        help="Sustained bandwidth limit (e.g. '500KB', '5MB', '20MB/s', '100Mbps'). Enables continuous realistic stream pacing.",
    )
    parser.add_argument(
        "--rate",
        type=float,
        help="Target messages per second rate (e.g. --rate 10000 for 10k msg/s)",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.0,
        help="Maximum duration in seconds to run sustained load (0 for indefinite / count-limited)",
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=1,
        help="Number of concurrent sender worker threads (default: 1; use 2-4 to push > 25 MB/s with small packets)",
    )

    args = parser.parse_args()

    # Determine data source
    if args.stdin:
        print(f"[*] Reading from STDIN and sending via {args.proto.upper()} to {args.host}:{args.port}...")
        for line in sys.stdin:
            line_bytes = line.strip().encode("utf-8")
            if not line_bytes:
                continue
            if args.proto == "udp":
                sent = send_udp(args.host, args.port, line_bytes)
            else:
                sent = send_tcp(args.host, args.port, line_bytes)
            print(f"[+] Sent {sent} bytes to {args.host}:{args.port}: {line.strip()[:60]}")
            if args.interval > 0:
                time.sleep(args.interval)
        return 0

    if args.file:
        if not args.file.is_file():
            print(f"[-] Error: File not found: {args.file}", file=sys.stderr)
            return 1
        raw_payload = args.file.read_bytes()
    elif args.message is not None:
        raw_payload = args.message.encode("utf-8")
    else:
        raw_payload = None  # Will be dynamically generated if sample is random

    # Check if sustained load mode is requested via --bandwidth or --rate
    if args.bandwidth or args.rate:
        target_bps = parse_bandwidth(args.bandwidth) if args.bandwidth else 0.0
        target_mps = float(args.rate) if args.rate else 0.0
        num_threads = max(1, args.threads)

        print(f"\033[1;36m[*] Starting Sustained Realistic Load Generator ({num_threads} worker thread{'s' if num_threads > 1 else ''})\033[0m")
        print(f"    Target: {args.host}:{args.port} ({args.proto.upper()})")
        if target_bps > 0:
            print(f"    Target Bandwidth: {target_bps / (1024*1024):.2f} MB/s ({target_bps * 8 / 1e6:.1f} Mbps)")
        if target_mps > 0:
            print(f"    Target Rate: {target_mps:,.0f} msg/s")
        if args.duration > 0:
            print(f"    Max Duration: {args.duration:.1f}s")
        max_messages = args.count if (args.count != 1 or not (args.bandwidth or args.rate)) else 0
        if max_messages > 0:
            print(f"    Max Messages: {max_messages:,}")

        # Pre-generate sample pool for ultra-low overhead generation
        sample_pool = [
            (raw_payload if raw_payload is not None else generate_sample(args.sample).encode("utf-8"))
            for _ in range(256)
        ]
        sample_size = len(sample_pool[0])

        import threading

        start_time = time.time()
        stop_event = threading.Event()
        total_lock = threading.Lock()
        global_total_msgs = 0
        global_total_bytes = 0

        # Thread pacing targets
        per_thread_bps = target_bps / num_threads if target_bps > 0 else 0.0
        per_thread_mps = target_mps / num_threads if target_mps > 0 else 0.0
        batch_limit = max(16, min(256, int(per_thread_bps / (sample_size * 200)))) if per_thread_bps > 0 else 32

        def worker_fn(worker_id: int):
            nonlocal global_total_msgs, global_total_bytes
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if args.proto == "udp" else None
            if sock:
                try:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 * 1024 * 1024)
                except OSError:
                    pass

            thread_msgs = 0
            thread_bytes = 0
            pool_idx = worker_id * 16

            try:
                while not stop_event.is_set():
                    now = time.time()
                    elapsed = now - start_time
                    if args.duration > 0 and elapsed >= args.duration:
                        stop_event.set()
                        break

                    # Rate limiting check per thread
                    if per_thread_bps > 0 and elapsed > 0:
                        cur_rate = thread_bytes / elapsed
                        if cur_rate > per_thread_bps:
                            ahead_bytes = thread_bytes - (per_thread_bps * elapsed)
                            sleep_s = ahead_bytes / per_thread_bps
                            if sleep_s > 0.0001:
                                time.sleep(min(0.01, sleep_s))
                                continue

                    if per_thread_mps > 0 and elapsed > 0:
                        cur_rate = thread_msgs / elapsed
                        if cur_rate > per_thread_mps:
                            ahead_msgs = thread_msgs - (per_thread_mps * elapsed)
                            sleep_s = ahead_msgs / per_thread_mps
                            if sleep_s > 0.0001:
                                time.sleep(min(0.01, sleep_s))
                                continue

                    # Dispatch batch
                    for _ in range(batch_limit):
                        payload = sample_pool[pool_idx]
                        pool_idx = (pool_idx + 1) & 255

                        if args.proto == "udp":
                            sock.sendto(payload, (args.host, args.port))
                        else:
                            send_tcp(args.host, args.port, payload)

                        thread_msgs += 1
                        thread_bytes += len(payload)

                    # Update shared stats every 64 iterations for smooth live monitoring
                    if (thread_msgs & 63) == 0:
                        with total_lock:
                            global_total_msgs += 64
                            global_total_bytes += 64 * len(payload)

                    if max_messages > 0:
                        with total_lock:
                            if global_total_msgs >= max_messages:
                                stop_event.set()
                                break
            finally:
                if sock:
                    sock.close()
                rem_msgs = thread_msgs % 64
                if rem_msgs > 0:
                    with total_lock:
                        global_total_msgs += rem_msgs
                        global_total_bytes += rem_msgs * sample_size

        threads = [threading.Thread(target=worker_fn, args=(i,), daemon=True) for i in range(num_threads)]
        for t in threads:
            t.start()

        # Monitoring / Progress loop
        last_stat_time = start_time
        prev_mon_msgs = 0
        prev_mon_bytes = 0

        try:
            while not stop_event.is_set():
                time.sleep(0.5)
                now = time.time()
                elapsed = now - start_time
                if args.duration > 0 and elapsed >= args.duration:
                    stop_event.set()
                    break

                if now - last_stat_time >= 1.0:
                    with total_lock:
                        cur_msgs = global_total_msgs
                        cur_bytes = global_total_bytes
                    dt = now - last_stat_time
                    inst_rate = (cur_msgs - prev_mon_msgs) / dt
                    inst_bw = (cur_bytes - prev_mon_bytes) / (1024 * 1024 * dt)
                    print(
                        f"[{time.strftime('%H:%M:%S')}] Pacing: "
                        f"\033[1;32m{inst_rate:>8.0f} msg/s\033[0m | "
                        f"\033[1;34m{inst_bw:>6.2f} MB/s\033[0m | "
                        f"Total: {cur_msgs:>9,} msgs ({cur_bytes / (1024*1024):>6.1f} MB)"
                    )
                    prev_mon_msgs = cur_msgs
                    prev_mon_bytes = cur_bytes
                    last_stat_time = now

        except KeyboardInterrupt:
            print("\n[*] Sustained load stopped by user.")
            stop_event.set()
        finally:
            stop_event.set()
            for t in threads:
                t.join(timeout=1.0)
            total_elapsed = max(0.001, time.time() - start_time)
            print(f"\n[+] Summary: Sent {global_total_msgs:,} messages ({global_total_bytes / (1024*1024):.2f} MB) in {total_elapsed:.2f}s ({global_total_msgs/total_elapsed:,.0f} msg/s avg, {global_total_bytes/(1024*1024*total_elapsed):.2f} MB/s)")

        return 0

    target_sender = send_udp if args.proto == "udp" else send_tcp

    iteration = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if args.proto == "udp" else None

    try:
        while args.count == 0 or iteration < args.count:
            iteration += 1

            burst_size = max(1, args.burst)
            total_bytes = 0
            last_payload = b""

            if args.proto == "udp":
                assert sock is not None
                # Fire the entire burst back-to-back with zero I/O or sleep in between
                for _ in range(burst_size):
                    payload = raw_payload if raw_payload is not None else generate_sample(args.sample).encode("utf-8")
                    total_bytes += sock.sendto(payload, (args.host, args.port))
                    last_payload = payload
            else:
                for _ in range(burst_size):
                    payload = raw_payload if raw_payload is not None else generate_sample(args.sample).encode("utf-8")
                    total_bytes += send_tcp(args.host, args.port, payload)
                    last_payload = payload

            preview = last_payload[:70].decode("utf-8", errors="replace")
            if len(last_payload) > 70:
                preview += "..."

            timestamp = time.strftime("%H:%M:%S")
            if burst_size > 1:
                print(f"[{timestamp}] [#{iteration}] Sent burst of {burst_size} datagrams ({total_bytes} bytes total) to {args.host}:{args.port}")
            else:
                print(f"[{timestamp}] [#{iteration}] Sent {total_bytes} bytes ({args.proto.upper()}) to {args.host}:{args.port} -> {preview}")

            if (args.count == 0 or iteration < args.count) and args.interval > 0:
                time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n[*] Transmission stopped by user.")
    finally:
        if sock:
            sock.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())

