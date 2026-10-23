#!/usr/bin/env python3
"""
read_telemetry.py - Real-time StatsD & Telemetry Listener for Proxy pipelines.

Listens on UDP (default: 0.0.0.0:8125) for live StatsD metrics transmitted by
proxy handlers and displays real-time rates, latencies, and gauge values in
human-readable or JSON formats.

Usage:
  ./scripts/read_telemetry.py
  ./scripts/read_telemetry.py --port 8125 --interval 1.0
  ./scripts/read_telemetry.py --json
"""

from __future__ import annotations

import argparse
import http.server
import json
import socket
import sys
import threading
import time
from collections import defaultdict
from pathlib import Path


class TelemetryCollector:
    def __init__(self):
        self.lock = threading.Lock()
        self.counters = defaultdict(int)
        self.gauges = {}
        self.timings = defaultdict(list)
        self.raw_lines = 0

    def parse_packet(self, data: str):
        with self.lock:
            for line in data.strip().splitlines():
                if not line or ":" not in line:
                    continue
                self.raw_lines += 1
                try:
                    name_part, rest = line.split(":", 1)
                    if "|" not in rest:
                        continue
                    val_str, mtype = rest.split("|", 1)
                    val = float(val_str)
                    metric_type = mtype.split("@")[0].strip()

                    if metric_type == "c":
                        # If cumulative absolute value is sent or delta:
                        # In Prometheus exporter, the total counter value is emitted periodically.
                        # We store the latest absolute value for monotonic counters:
                        self.counters[name_part] = int(val)
                    elif metric_type == "g":
                        self.gauges[name_part] = val
                    elif metric_type == "ms":
                        self.timings[name_part].append(val)
                except Exception:
                    pass

    def get_snapshot(self):
        with self.lock:
            counters = dict(self.counters)
            gauges = dict(self.gauges)
            timings = {k: list(v) for k, v in self.timings.items()}
            return counters, gauges, timings

    def get_snapshot_and_reset(self):
        with self.lock:
            counters = dict(self.counters)
            gauges = dict(self.gauges)
            timings = {k: list(v) for k, v in self.timings.items()}
            self.timings.clear()
            return counters, gauges, timings


def run_listener(collector: TelemetryCollector, host: str, port: int, stop_event: threading.Event):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.settimeout(0.5)

    while not stop_event.is_set():
        try:
            data, _ = sock.recvfrom(65535)
            if data:
                collector.parse_packet(data.decode("utf-8", errors="replace"))
        except socket.timeout:
            continue
        except Exception:
            break
    sock.close()


def run_web_server(collector: TelemetryCollector, port: int = 8126):
    html_file = Path(__file__).parent / "telemetry_dashboard.html"

    class TelemetryHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/" or self.path.startswith("/index"):
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                if html_file.exists():
                    self.wfile.write(html_file.read_bytes())
                else:
                    self.wfile.write(b"<h1>Dashboard HTML missing</h1>")
            elif self.path == "/api/metrics":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                counters, gauges, timings = collector.get_snapshot()
                obj = {
                    "timestamp": time.time(),
                    "counters": counters,
                    "gauges": gauges,
                    "timings": {
                        k: {
                            "count": len(v),
                            "avg": sum(v) / len(v) if v else 0.0,
                            "p99": sorted(v)[int(len(v) * 0.99)] if v else 0.0,
                        }
                        for k, v in timings.items()
                    },
                }
                self.wfile.write(json.dumps(obj).encode("utf-8"))
            else:
                self.send_response(404)
                self.end_headers()

        def log_message(self, format, *args):
            pass  # Suppress HTTP access logging

    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), TelemetryHandler)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description="Proxy Pipeline Real-time Telemetry Listener")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8125, help="UDP port (default: 8125)")
    parser.add_argument("--web-port", type=int, default=8126, help="Web dashboard port (default: 8126)")
    parser.add_argument("--interval", type=float, default=1.0, help="Display refresh interval in seconds")
    parser.add_argument("--json", action="store_true", help="Output metrics as continuous JSON objects")
    args = parser.parse_args()

    collector = TelemetryCollector()
    stop_event = threading.Event()

    listener_thread = threading.Thread(
        target=run_listener, args=(collector, args.host, args.port, stop_event), daemon=True
    )
    listener_thread.start()

    web_thread = threading.Thread(
        target=run_web_server, args=(collector, args.web_port), daemon=True
    )
    web_thread.start()

    print(f"\033[1;36m[read_telemetry] Listening on UDP {args.host}:{args.port}\033[0m")
    print(f"\033[1;32m[read_telemetry] Live Web Dashboard available at http://localhost:{args.web_port}\033[0m")

    prev_time = time.time()
    prev_counters = defaultdict(int)

    try:
        while True:
            time.sleep(args.interval)
            now = time.time()
            dt = max(0.001, now - prev_time)
            prev_time = now

            counters, gauges, timings = collector.get_snapshot_and_reset()

            if args.json:
                import json
                obj = {
                    "timestamp": now,
                    "counters": counters,
                    "gauges": gauges,
                    "timings": {
                        k: {
                            "count": len(v),
                            "avg": sum(v) / len(v) if v else 0.0,
                            "p99": sorted(v)[int(len(v) * 0.99)] if v else 0.0,
                        }
                        for k, v in timings.items()
                    },
                }
                print(json.dumps(obj))
                sys.stdout.flush()
                continue

            # Clear screen or print block
            print(f"\n--- Telemetry Report ({time.strftime('%Y-%m-%d %H:%M:%S')}) ---")
            
            # Print Counters & Rates
            if counters:
                print("\033[1;32m[Counters & Throughput]:\033[0m")
                for name, val in sorted(counters.items()):
                    delta = val - prev_counters.get(name, 0)
                    rate = delta / dt
                    print(f"  {name:<45} : {val:>10} total  (+{delta:>8} in {dt:.1f}s, \033[33m{rate:>10.1f} msg/s\033[0m)")
                prev_counters = dict(counters)
            else:
                print("  (No counter metrics received yet)")

            # Print Gauges
            if gauges:
                print("\033[1;34m[Gauges]:\033[0m")
                for name, val in sorted(gauges.items()):
                    print(f"  {name:<36} : {val}")

            # Print Timings / Latency
            # 1. From direct timers:
            printed_timings = False
            if timings:
                print("\033[1;35m[Latencies (last interval)]:\033[0m")
                printed_timings = True
                for name, vals in sorted(timings.items()):
                    if not vals:
                        continue
                    vals.sort()
                    avg = sum(vals) / len(vals)
                    p50 = vals[len(vals) // 2]
                    p99 = vals[min(len(vals) - 1, int(len(vals) * 0.99))]
                    print(f"  {name:<36} : count={len(vals)} avg={avg:.2f}ms p50={p50:.2f}ms p99={p99:.2f}ms")

            # 2. From histogram buckets & sum/count (per pipeline handler/stage):
            # Find all stages that emitted message_processing_duration_us metrics
            stage_prefixes = set()
            for k in list(gauges.keys()) + list(counters.keys()):
                if "message_processing_duration_us" in k:
                    # extract prefix before .message_processing_duration_us
                    stage = k.split(".message_processing_duration_us")[0]
                    stage_prefixes.add(stage)

            for stage in sorted(stage_prefixes):
                hist_sum = gauges.get(f"{stage}.message_processing_duration_us.sum", 0.0)
                hist_count = counters.get(f"{stage}.message_processing_duration_us.count", gauges.get(f"{stage}.message_processing_duration_us.count", 0))
                if hist_count <= 0:
                    continue

                if not printed_timings:
                    print("\033[1;35m[Latencies & Histogram Distribution]:\033[0m")
                    printed_timings = True

                avg_us = hist_sum / hist_count
                # Extract standard tiers:
                b5 = gauges.get(f"{stage}.message_processing_duration_us.bucket.5.000", 0)
                b25 = gauges.get(f"{stage}.message_processing_duration_us.bucket.25.000", 0)
                b100 = gauges.get(f"{stage}.message_processing_duration_us.bucket.100.000", 0)
                b1000 = gauges.get(f"{stage}.message_processing_duration_us.bucket.1000.000", 0)
                b5000 = gauges.get(f"{stage}.message_processing_duration_us.bucket.5000.000", 0)
                b25000 = gauges.get(f"{stage}.message_processing_duration_us.bucket.25000.000", 0)
                b_inf = gauges.get(f"{stage}.message_processing_duration_us.bucket.+Inf", hist_count)

                # Monotonic normalization
                if b25 < b5: b25 = b5
                if b100 < b25: b100 = b25
                if b1000 < b100: b1000 = b100
                if b5000 < b1000: b5000 = b1000
                if b25000 < b5000: b25000 = b5000
                if b_inf < b25000: b_inf = max(b25000, hist_count)

                # Disjoint intervals:
                c0_5 = b5
                c5_25 = max(0, b25 - b5)
                c25_100 = max(0, b100 - b25)
                c100_1000 = max(0, b1000 - b100)
                c1_5ms = max(0, b5000 - b1000)
                c5_25ms = max(0, b25000 - b5000)
                c_over25ms = max(0, b_inf - b25000)

                # Estimate p99:
                target_p99 = 0.99 * hist_count
                if b5 >= target_p99:
                    est_p99 = 5.0 * (target_p99 / max(1, b5))
                elif b25 >= target_p99:
                    est_p99 = 5.0 + 20.0 * ((target_p99 - b5) / max(1, b25 - b5))
                elif b100 >= target_p99:
                    est_p99 = 25.0 + 75.0 * ((target_p99 - b25) / max(1, b100 - b25))
                elif b1000 >= target_p99:
                    est_p99 = 100.0 + 900.0 * ((target_p99 - b100) / max(1, b1000 - b100))
                elif b5000 >= target_p99:
                    est_p99 = 1000.0 + 4000.0 * ((target_p99 - b1000) / max(1, b5000 - b1000))
                elif b25000 >= target_p99:
                    est_p99 = 5000.0 + 20000.0 * ((target_p99 - b5000) / max(1, b25000 - b5000))
                else:
                    est_p99 = 25000.0 + 10000.0

                stage_name = stage.replace("proxy.", "")
                print(f"  [{stage_name}] latency : count={int(hist_count)} avg={avg_us:.2f}µs est_p99={est_p99:.2f}µs")
                print(f"    0-5µs: {int(c0_5)} ({c0_5/hist_count*100:.1f}%) | 5-25µs: {int(c5_25)} ({c5_25/hist_count*100:.1f}%) | 25-100µs: {int(c25_100)} ({c25_100/hist_count*100:.1f}%) | 0.1-1ms: {int(c100_1000)} ({c100_1000/hist_count*100:.1f}%)")
                print(f"    1-5ms: {int(c1_5ms)} ({c1_5ms/hist_count*100:.1f}%) | 5-25ms: {int(c5_25ms)} ({c5_25ms/hist_count*100:.1f}%) | >25ms: {int(c_over25ms)} ({c_over25ms/hist_count*100:.1f}%)")

            sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[read_telemetry] Shutting down listener...")
        stop_event.set()
        listener_thread.join(timeout=1.0)


if __name__ == "__main__":
    main()

