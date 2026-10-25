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
from collections import defaultdict, deque
import http.server
import json
import math
import socket
import sys
import threading
import time
from pathlib import Path
from typing import Any

from proxy_runner.topology import MAX_TOPOLOGY_BODY_BYTES, MAX_TOPOLOGY_EDGES, MAX_TOPOLOGY_HANDLERS


MAX_TIMING_SAMPLES_PER_METRIC = 2048


def validate_topology_document(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise ValueError("Topology document must be a JSON object")

    chain_id = document.get("chainId")
    backend = document.get("backend")
    nodes = document.get("nodes")
    edges = document.get("edges")
    unresolved = document.get("unresolvedOutputChannels", [])
    if not isinstance(chain_id, str) or not chain_id or len(chain_id) > 128:
        raise ValueError("Topology must include a chainId of at most 128 characters")
    if not isinstance(backend, str) or not backend or len(backend) > 64:
        raise ValueError("Topology must include a valid backend")
    if not isinstance(nodes, list) or not nodes or len(nodes) > MAX_TOPOLOGY_HANDLERS:
        raise ValueError(f"Topology nodes must contain 1 to {MAX_TOPOLOGY_HANDLERS} items")
    if not isinstance(edges, list) or len(edges) > MAX_TOPOLOGY_EDGES:
        raise ValueError(f"Topology edges must contain at most {MAX_TOPOLOGY_EDGES} items")
    if not isinstance(unresolved, list) or len(unresolved) > MAX_TOPOLOGY_EDGES:
        raise ValueError("Topology unresolvedOutputChannels must be a bounded list")

    sanitized_nodes = []
    node_names: set[str] = set()
    for node in nodes:
        if not isinstance(node, dict):
            raise ValueError("Each topology node must be an object")
        name = node.get("name")
        handler_type = node.get("type")
        metric_prefix = node.get("metricPrefix")
        input_channels = node.get("inputChannels", [])
        output_channels = node.get("outputChannels", [])
        if not all(
            isinstance(value, str) and value and len(value) <= 128
            for value in (name, handler_type, metric_prefix)
        ):
            raise ValueError("Topology node name, type, and metricPrefix must be non-empty strings")
        if name in node_names:
            raise ValueError(f"Duplicate topology node name: {name}")
        if not isinstance(input_channels, list) or not isinstance(output_channels, list):
            raise ValueError(f"Topology channels for node '{name}' must be lists")
        if any(
            not isinstance(channel, str) or not channel or len(channel) > 128
            for channel in input_channels + output_channels
        ):
            raise ValueError(f"Topology channels for node '{name}' must be non-empty strings")
        node_names.add(name)
        sanitized_nodes.append({
            "name": name,
            "type": handler_type,
            "metricPrefix": metric_prefix,
            "inputChannels": list(dict.fromkeys(input_channels)),
            "outputChannels": list(dict.fromkeys(output_channels)),
        })

    sanitized_edges = []
    for edge in edges:
        if not isinstance(edge, dict):
            raise ValueError("Each topology edge must be an object")
        source = edge.get("from")
        target = edge.get("to")
        channel = edge.get("channel")
        if not isinstance(channel, str) or not channel or len(channel) > 128:
            raise ValueError("Topology edge channel must be a non-empty string")
        if (
            not isinstance(source, str)
            or not isinstance(target, str)
            or len(source) > 128
            or len(target) > 128
        ):
            raise ValueError("Topology edge endpoints must be valid node names")
        if source not in node_names or target not in node_names:
            raise ValueError("Topology edge references an unknown node")
        sanitized_edges.append({"from": source, "to": target, "channel": channel})

    if any(not isinstance(channel, str) or not channel or len(channel) > 128 for channel in unresolved):
        raise ValueError("Topology unresolved output channels must be non-empty strings")

    return {
        "chainId": chain_id,
        "backend": backend,
        "nodes": sanitized_nodes,
        "edges": sanitized_edges,
        "unresolvedOutputChannels": list(dict.fromkeys(unresolved)),
    }


class TelemetryCollector:
    def __init__(self):
        self.lock = threading.Lock()
        self.counters = defaultdict(int)
        self.gauges = {}
        self.timings = defaultdict(lambda: deque(maxlen=MAX_TIMING_SAMPLES_PER_METRIC))
        self.metric_last_seen: dict[str, float] = {}
        self.last_received_at: float | None = None
        self.topology: dict[str, Any] | None = None
        self.peak_throughput = 0.0
        self.peak_source_rates: dict[str, float] = {}
        self.peak_previous_counters: dict[str, tuple[int, float]] = {}
        self.peak_topology_key: tuple[str, str, tuple[str, ...]] | None = None
        self.raw_lines = 0

    def parse_packet(self, data: str):
        received_at = time.time()
        with self.lock:
            received_metric = False
            received_counters: dict[str, int] = {}
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
                    if not math.isfinite(val):
                        continue
                    metric_type = mtype.split("@")[0].strip()

                    if metric_type == "c":
                        self.counters[name_part] = int(val)
                        received_counters[name_part] = int(val)
                    elif metric_type == "g":
                        self.gauges[name_part] = val
                    elif metric_type == "ms":
                        self.timings[name_part].append(val)
                    else:
                        continue
                    self.metric_last_seen[name_part] = received_at
                    received_metric = True
                except (ValueError, OverflowError):
                    pass
            if received_metric:
                self.last_received_at = received_at
            self._update_peak_throughput(received_counters, received_at)

    def _update_peak_throughput(self, received_counters: dict[str, int], received_at: float) -> None:
        if not self.topology:
            return

        source_nodes = self._source_nodes(self.topology)
        source_metrics = {
            f"{node['metricPrefix']}.messages_ingress_total"
            for node in source_nodes
        }
        for name, value in received_counters.items():
            if name not in source_metrics:
                continue

            previous = self.peak_previous_counters.get(name)
            if previous and received_at > previous[1] and value >= previous[0]:
                self.peak_source_rates[name] = (value - previous[0]) / (received_at - previous[1])
            else:
                self.peak_source_rates[name] = 0.0
            self.peak_previous_counters[name] = (value, received_at)

        if received_counters.keys() & source_metrics:
            current_rate = sum(self.peak_source_rates.get(name, 0.0) for name in source_metrics)
            self.peak_throughput = max(self.peak_throughput, current_rate)

    @staticmethod
    def _source_nodes(topology: dict[str, Any]) -> list[dict[str, Any]]:
        incoming = {edge["to"] for edge in topology["edges"]}
        return [node for node in topology["nodes"] if node["name"] not in incoming]

    def get_snapshot(self):
        with self.lock:
            counters = dict(self.counters)
            gauges = dict(self.gauges)
            timings = {k: list(v) for k, v in self.timings.items()}
            return counters, gauges, timings

    def get_api_snapshot(self):
        with self.lock:
            counters = dict(self.counters)
            gauges = dict(self.gauges)
            timings = {key: list(values) for key, values in self.timings.items()}
            return {
                "timestamp": time.time(),
                "lastReceivedAt": self.last_received_at,
                "topologyAvailable": self.topology is not None,
                "peakThroughput": self.peak_throughput,
                "metricLastSeen": dict(self.metric_last_seen),
                "counters": counters,
                "gauges": gauges,
                "timings": {
                    key: {
                        "count": len(values),
                        "avg": sum(values) / len(values) if values else 0.0,
                        "p99": sorted(values)[min(len(values) - 1, int(len(values) * 0.99))] if values else 0.0,
                    }
                    for key, values in timings.items()
                },
            }

    def set_topology(self, document: Any):
        sanitized = validate_topology_document(document)
        with self.lock:
            source_nodes = self._source_nodes(sanitized)
            topology_key = (
                sanitized["chainId"],
                sanitized["backend"],
                tuple(sorted(node["metricPrefix"] for node in source_nodes)),
            )
            if topology_key != self.peak_topology_key:
                self.peak_topology_key = topology_key
                self.peak_throughput = 0.0
                self.peak_source_rates.clear()
                self.peak_previous_counters.clear()
            self.topology = sanitized

    def get_topology(self):
        with self.lock:
            return dict(self.topology) if self.topology else None

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


def create_web_server(collector: TelemetryCollector, port: int = 8126, host: str = "127.0.0.1"):
    html_file = Path(__file__).parent / "telemetry_dashboard.html"

    class TelemetryHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/" or self.path.startswith("/index"):
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                if html_file.exists():
                    self.wfile.write(html_file.read_bytes())
                else:
                    self.wfile.write(b"<h1>Dashboard HTML missing</h1>")
            elif self.path == "/telemetry_dashboard.css":
                css_file = html_file.with_name("telemetry_dashboard.css")
                self.send_response(200)
                self.send_header("Content-Type", "text/css; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(css_file.read_bytes())
            elif self.path == "/api/metrics":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(json.dumps(collector.get_api_snapshot()).encode("utf-8"))
            elif self.path == "/api/topology":
                topology = collector.get_topology()
                if topology is None:
                    self.send_response(404)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(b'{"error":"No chain topology has been published"}')
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(json.dumps(topology).encode("utf-8"))
            else:
                self.send_response(404)
                self.end_headers()

        def do_POST(self):
            if self.path != "/api/topology":
                self.send_response(404)
                self.end_headers()
                return

            try:
                content_length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self.send_error(400, "Invalid Content-Length")
                return
            if content_length <= 0 or content_length > MAX_TOPOLOGY_BODY_BYTES:
                status = 413 if content_length > MAX_TOPOLOGY_BODY_BYTES else 400
                self.send_error(status, "Invalid topology body size")
                return

            try:
                document = json.loads(self.rfile.read(content_length))
                collector.set_topology(document)
            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                self.send_error(400, str(error))
                return

            self.send_response(204)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()

        def log_message(self, format, *args):
            pass  # Suppress HTTP access logging

    return http.server.ThreadingHTTPServer((host, port), TelemetryHandler)


def run_web_server(collector: TelemetryCollector, port: int = 8126, host: str = "127.0.0.1"):
    create_web_server(collector, port, host).serve_forever()


def main():
    parser = argparse.ArgumentParser(description="Proxy Pipeline Real-time Telemetry Listener")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8125, help="UDP port (default: 8125)")
    parser.add_argument("--web-port", type=int, default=8126, help="Web dashboard port (default: 8126)")
    parser.add_argument("--web-host", default="127.0.0.1", help="Dashboard bind address (default: 127.0.0.1)")
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
        target=run_web_server, args=(collector, args.web_port, args.web_host), daemon=True
    )
    web_thread.start()

    print(f"\033[1;36m[read_telemetry] Listening on UDP {args.host}:{args.port}\033[0m")
    print(f"\033[1;32m[read_telemetry] Live Web Dashboard available at http://{args.web_host}:{args.web_port}\033[0m")

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
