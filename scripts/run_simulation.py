#!/usr/bin/env python3
"""
Interactive Visual Simulation Runner for Proxy chains.
Parses PCAP capture files, simulates multi-source handler chains (supporting concurrent
external UDP network ingress and internal scheduled dummy_send message generators),
and serves an interactive real-time visual animated topological dashboard.
"""

import argparse
import http.server
import json
import os
import socket
import struct
import sys
from typing import Any, Dict, List, Optional, Tuple

try:
    import tomllib
except ModuleNotFoundError:
    try:
        import tomli as tomllib
    except ModuleNotFoundError:
        import toml as tomllib


class PcapPacket:
    """Structured representation of an extracted network packet."""

    def __init__(
        self,
        seq_id: int,
        timestamp_ns: int,
        src_ip: str,
        src_port: int,
        dst_ip: str,
        dst_port: int,
        size: int,
        payload_text: str,
    ):
        self.seq_id = seq_id
        self.timestamp_ns = timestamp_ns
        self.src_ip = src_ip
        self.src_port = src_port
        self.dst_ip = dst_ip
        self.dst_port = dst_port
        self.size = size
        self.payload_text = payload_text

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.seq_id,
            "timestamp_ns": self.timestamp_ns,
            "src_ip": self.src_ip,
            "src_port": self.src_port,
            "dst_ip": self.dst_ip,
            "dst_port": self.dst_port,
            "size": self.size,
            "payload_text": self.payload_text,
        }


class PcapParser:
    """Zero-dependency native parser for standard PCAP files."""

    def __init__(self, filepath: str):
        self.filepath = filepath
        self.packets: List[PcapPacket] = []
        self._parse()

    def _parse(self):
        if not os.path.exists(self.filepath):
            return

        with open(self.filepath, "rb") as f:
            global_hdr = f.read(24)
            if len(global_hdr) < 24:
                return

            magic = struct.unpack("<I", global_hdr[:4])[0]
            if magic in (0xA1B2C3D4, 0xA1B23C4D):
                endian = "<"
                is_nano = magic == 0xA1B23C4D
            elif magic in (0xD4C3B2A1, 0x4D3CB2A1):
                endian = ">"
                is_nano = magic == 0x4D3CB2A1
            else:
                return

            seq = 1
            while True:
                pkt_hdr_data = f.read(16)
                if len(pkt_hdr_data) < 16:
                    break
                ts_sec, ts_usec, incl_len, _ = struct.unpack(
                    f"{endian}IIII", pkt_hdr_data
                )
                raw_pkt = f.read(incl_len)
                if len(raw_pkt) < 14:
                    continue

                # Parse Ethernet header
                eth_type = struct.unpack("!H", raw_pkt[12:14])[0]
                offset = 14
                if eth_type == 0x8100:  # VLAN tag
                    eth_type = struct.unpack("!H", raw_pkt[16:18])[0]
                    offset = 18

                if eth_type != 0x0800 or len(raw_pkt) < offset + 20:  # IPv4
                    continue

                ip_hdr = raw_pkt[offset : offset + 20]
                ihl = (ip_hdr[0] & 0x0F) * 4
                proto = ip_hdr[9]
                src_ip = socket.inet_ntoa(ip_hdr[12:16])
                dst_ip = socket.inet_ntoa(ip_hdr[16:20])

                if proto != 17 or len(raw_pkt) < offset + ihl + 8:  # UDP
                    continue

                udp_hdr = raw_pkt[offset + ihl : offset + ihl + 8]
                src_port, dst_port, udp_len, _ = struct.unpack("!HHHH", udp_hdr)
                payload_offset = offset + ihl + 8
                payload_len = max(0, udp_len - 8)
                payload = raw_pkt[payload_offset : payload_offset + payload_len]

                ts_ns = (ts_sec * 1_000_000_000) + (
                    ts_usec if is_nano else ts_usec * 1_000
                )
                try:
                    text_preview = payload.decode("utf-8")
                except UnicodeDecodeError:
                    text_preview = payload.hex()

                self.packets.append(
                    PcapPacket(
                        seq_id=seq,
                        timestamp_ns=ts_ns,
                        src_ip=src_ip,
                        src_port=src_port,
                        dst_ip=dst_ip,
                        dst_port=dst_port,
                        size=len(payload),
                        payload_text=text_preview,
                    )
                )
                seq += 1


class PipelineTopology:
    """Builds channel mappings, target routing, and discovers generator handlers."""

    def __init__(self, handlers: List[Dict[str, Any]]):
        self.handlers = handlers
        self.channel_to_targets: Dict[str, List[str]] = {}
        self.ingress_handlers: List[Dict[str, Any]] = []
        self.generator_handlers: List[Dict[str, Any]] = []
        self._analyze()

    def _analyze(self):
        for h in self.handlers:
            h_type = h.get("type", "")
            if "ingress" in h_type:
                self.ingress_handlers.append(h)
            elif "dummy_send" in h_type:
                self.generator_handlers.append(h)

            in_channels = []
            if "input_channel" in h and h["input_channel"]:
                in_channels.append(h["input_channel"])
            if "input_channels" in h and isinstance(h["input_channels"], list):
                in_channels.extend(h["input_channels"])

            for in_ch in in_channels:
                self.channel_to_targets.setdefault(in_ch, []).append(h["name"])

    def get_handler_by_name(self, name: str) -> Optional[Dict[str, Any]]:
        return next((h for h in self.handlers if h["name"] == name), None)


class ChannelMessageDispatcher:
    """Dispatches messages through downstream channels and executes handler validation rules."""

    def __init__(self, topology: PipelineTopology):
        self.topology = topology

    def dispatch_downstream(
        self,
        start_channel: str,
        msg_id: int,
        payload: str,
        start_time_ns: int,
        hop_latency_step_ns: int = 15_000,
    ) -> List[Dict[str, Any]]:
        events = []
        curr_ch = start_channel
        sim_time = start_time_ns + hop_latency_step_ns

        while curr_ch:
            targets = self.topology.channel_to_targets.get(curr_ch, [])
            if not targets:
                break

            for target_name in targets:
                target_handler = self.topology.get_handler_by_name(target_name)
                h_type = target_handler.get("type", "unknown") if target_handler else "unknown"
                hop_latency_us = round((sim_time - start_time_ns) / 1000.0, 1)

                is_dropped = False
                drop_reason = "OK"

                # Validation logic (e.g. XmlVerificationHandler schema checks)
                if "xml_verification" in h_type or "xml" in target_name:
                    if "<bad_element>" in payload or "invalid" in payload or not payload.strip().startswith("<"):
                        is_dropped = True
                        drop_reason = "Rejected: XSD schema validation failure (<bad_element> not allowed)"

                if is_dropped:
                    events.append({
                        "msgId": msg_id,
                        "timestampNs": sim_time,
                        "type": "drop",
                        "source": curr_ch,
                        "target": target_name,
                        "targetType": h_type,
                        "channel": curr_ch,
                        "payloadSummary": payload[:60] + "..." if len(payload) > 60 else payload,
                        "payloadFull": payload,
                        "status": "dropped",
                        "latencyUs": hop_latency_us,
                        "detail": f"Message discarded at '{target_name}': {drop_reason}",
                    })
                    curr_ch = None
                    break

                next_out = target_handler.get("output_channel") if target_handler else None
                is_sink = not next_out or "egress" in h_type or "print" in h_type

                events.append({
                    "msgId": msg_id,
                    "timestampNs": sim_time,
                    "type": "egress" if is_sink else "hop",
                    "source": curr_ch,
                    "target": target_name,
                    "targetType": h_type,
                    "channel": curr_ch,
                    "payloadSummary": payload[:60] + "..." if len(payload) > 60 else payload,
                    "payloadFull": payload,
                    "status": "transmitted" if is_sink else "forwarded",
                    "latencyUs": hop_latency_us,
                    "detail": f"Received on '{curr_ch}' by '{target_name}'"
                    + (f" -> forwarded to '{next_out}'" if next_out else " (final sink/egress)"),
                })

                sim_time += hop_latency_step_ns
                curr_ch = next_out

        return events


class SimulationEngine:
    """Unified Multi-Source Simulation Engine: orchestrates PCAP and generator streams concurrently."""

    def __init__(self, chain_path: str, pcap_path: str):
        self.chain_path = chain_path
        self.pcap_path = pcap_path
        self.chain_config = self._load_chain()
        self.pcap_packets = PcapParser(pcap_path).packets
        self.topology = PipelineTopology(self.chain_config.get("handlers", []))
        self.dispatcher = ChannelMessageDispatcher(self.topology)
        self.events: List[Dict[str, Any]] = []
        self._simulate()

    def _load_chain(self) -> Dict[str, Any]:
        with open(self.chain_path, "rb") as f:
            return tomllib.load(f)

    def _simulate(self):
        all_events: List[Dict[str, Any]] = []
        next_msg_id = 1

        # Determine simulation time base from PCAP or default
        base_time_ns = (
            self.pcap_packets[0].timestamp_ns
            if self.pcap_packets
            else 1_000_000_000
        )

        # ----------------------------------------------------
        # Stream 1: Ingest PCAP Packets into Ingress Handlers
        # ----------------------------------------------------
        if self.pcap_packets and self.topology.ingress_handlers:
            primary_ingress = self.topology.ingress_handlers[0]
            out_ch = primary_ingress.get("output_channel", "orders_raw")

            for pkt in self.pcap_packets:
                msg_id = next_msg_id
                next_msg_id += 1

                # Ingress arrival event
                all_events.append({
                    "msgId": msg_id,
                    "timestampNs": pkt.timestamp_ns,
                    "type": "ingress",
                    "source": f"{pkt.src_ip}:{pkt.src_port}",
                    "target": primary_ingress["name"],
                    "targetType": primary_ingress.get("type", "udp_ingress_handler"),
                    "channel": "udp_network",
                    "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                    "payloadFull": pkt.payload_text,
                    "status": "received",
                    "latencyUs": 0.0,
                    "detail": f"External UDP Datagram {pkt.size}B received from {pkt.src_ip}:{pkt.src_port}",
                })

                # Follow downstream path
                downstream = self.dispatcher.dispatch_downstream(
                    start_channel=out_ch,
                    msg_id=msg_id,
                    payload=pkt.payload_text,
                    start_time_ns=pkt.timestamp_ns,
                )
                all_events.extend(downstream)

        # ----------------------------------------------------
        # Stream 2: Internal Generator Handlers (dummy_send)
        # ----------------------------------------------------
        if self.topology.generator_handlers:
            num_ticks = 10
            for cycle in range(1, num_ticks + 1):
                for gen in self.topology.generator_handlers:
                    msg_id = next_msg_id
                    next_msg_id += 1

                    out_ch = gen.get("output_channel")
                    cfg = gen.get("config", {})
                    msg_text = cfg.get("dummy.message", f"Message from {gen['name']}")
                    delay_ms = cfg.get("dummy.delay_ms", 500)
                    t_ns = base_time_ns + (cycle * delay_ms * 1_000_000)

                    # Generator production event
                    all_events.append({
                        "msgId": msg_id,
                        "timestampNs": t_ns,
                        "type": "ingress",
                        "source": gen["name"],
                        "target": gen["name"],
                        "targetType": gen.get("type", "dummy_send"),
                        "channel": out_ch or "internal",
                        "payloadSummary": msg_text,
                        "payloadFull": f"{msg_text} (Cycle: {cycle})",
                        "status": "generated",
                        "latencyUs": 0.0,
                        "detail": f"Generated by timer task in '{gen['name']}' -> dispatched to '{out_ch}'",
                    })

                    # Follow downstream path
                    if out_ch:
                        downstream = self.dispatcher.dispatch_downstream(
                            start_channel=out_ch,
                            msg_id=msg_id,
                            payload=msg_text,
                            start_time_ns=t_ns,
                        )
                        all_events.extend(downstream)

        # ----------------------------------------------------
        # Chronological Sort & Sequence Assignment
        # ----------------------------------------------------
        all_events.sort(key=lambda e: e["timestampNs"])
        for idx, ev in enumerate(all_events, 1):
            ev["eventId"] = idx

        self.events = all_events


class SimulationServer(http.server.ThreadingHTTPServer):
    def __init__(self, server_address, RequestHandlerClass, engine: SimulationEngine):
        self.engine = engine
        super().__init__(server_address, RequestHandlerClass)


class SimulationHttpHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            html_content = self._generate_html()
            self.wfile.write(html_content.encode("utf-8"))
        elif self.path == "/api/simulation":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            data = {
                "chain": self.server.engine.chain_config,
                "pcap": self.server.engine.pcap_path,
                "packetCount": len(self.server.engine.pcap_packets),
                "events": self.server.engine.events,
            }
            self.wfile.write(json.dumps(data).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def _generate_html(self) -> str:
        dashboard_path = os.path.join(
            os.path.dirname(__file__), "simulation_dashboard.html"
        )
        with open(dashboard_path, "r", encoding="utf-8") as f:
            return f.read()


def main():
    parser = argparse.ArgumentParser(
        description="Proxy PCAP & Multi-Source Visual Simulation Dashboard"
    )
    parser.add_argument(
        "--chain",
        default="chains/example_shm_full_proxy_chain.toml",
        help="Path to chain TOML config",
    )
    parser.add_argument(
        "--pcap", default="sample_orders.pcap", help="Path to input PCAP file"
    )
    parser.add_argument(
        "--port", type=int, default=8090, help="HTTP server port (default: 8090)"
    )
    args = parser.parse_args()

    if not os.path.exists(args.chain):
        print(f"Error: Chain configuration '{args.chain}' not found.", file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(args.pcap):
        print(
            f"Warning: PCAP file '{args.pcap}' not found. Generating sample PCAP...",
            file=sys.stderr,
        )
        os.system(f"./build/gcc16-release/bin/generate_sample_pcap {args.pcap} 30")

    print(f"Loading simulation model for chain: {args.chain}")
    print(f"Parsing PCAP packets from: {args.pcap}")
    engine = SimulationEngine(args.chain, args.pcap)
    print(
        f"Loaded {len(engine.pcap_packets)} PCAP packets -> {len(engine.events)} unified simulation events generated."
    )

    server = SimulationServer(("0.0.0.0", args.port), SimulationHttpHandler, engine)
    print(f"\n=======================================================")
    print(f" 🚀 Visual PCAP Simulation Dashboard live at:")
    print(f" 👉 http://localhost:{args.port}")
    print(f"=======================================================\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping simulation server.")
        server.shutdown()


if __name__ == "__main__":
    main()
