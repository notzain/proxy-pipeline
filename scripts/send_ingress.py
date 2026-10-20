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

