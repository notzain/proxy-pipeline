#!/usr/bin/env python3
"""
chain_runner.py - Orchestrator and runner for Proxy handler chains.

Supports:
  1. Standard distributed execution across any transport backend ('file', 'socket', 'shm', 'uds', 'sim').
  2. Interactive step-by-step PCAP simulation:
     - Preloads all PCAP packets into an in-memory queue.
     - Frontend queries /api/simulation to display all loaded packets in a chronological list.
     - Play / Step Forward (/api/simulation/step):
         * If an envelope is currently held in an intermediate channel's output buffer,
           it forwards it from the output channel into the next handler's input channel.
         * Otherwise, it loads the next PCAP packet into the first handler's input channel.
         * The handler processes the message (agnostic of simulation).
         * If the handler produces an output message, it is held in the output buffer until
           the next step.
         * If no output message is produced by the handler (e.g. invalid XML dropped by validator),
           the coordinator detects that 0 bytes were output, marks the drop at that handler node,
           and finishes the packet's lifecycle.
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    import tomllib
except ImportError:
    tomllib = None  # type: ignore


# ---------------------------------------------------------------------------
# Zero-Dependency PCAP Extraction & Envelope Serialization
# ---------------------------------------------------------------------------

class PcapPacket:
    """Structured representation of an extracted network packet from PCAP."""

    def __init__(
        self,
        seq_id: int,
        timestamp_ns: int,
        src_ip: str,
        src_port: int,
        dst_ip: str,
        dst_port: int,
        raw_payload: bytes,
        payload_text: str,
    ):
        self.seq_id = seq_id
        self.timestamp_ns = timestamp_ns
        self.src_ip = src_ip
        self.src_port = src_port
        self.dst_ip = dst_ip
        self.dst_port = dst_port
        self.raw_payload = raw_payload
        self.payload_text = payload_text

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.seq_id,
            "timestampNs": self.timestamp_ns,
            "srcIp": self.src_ip,
            "srcPort": self.src_port,
            "dstIp": self.dst_ip,
            "dstPort": self.dst_port,
            "size": len(self.raw_payload),
            "payloadSummary": self.payload_text[:60] + "..." if len(self.payload_text) > 60 else self.payload_text,
            "payloadFull": self.payload_text,
        }

    def pack_envelope(self) -> bytes:
        """
        Packs payload and UDP metadata into standard Proxy MessageEnvelope wire format.
        EnvelopeHeader (18 bytes) + UdpMetadata (48 bytes) + raw payload.
        """
        src_ip_bytes = socket.inet_aton(self.src_ip)
        dst_ip_bytes = socket.inet_aton(self.dst_ip)
        src_port_net = struct.pack("!H", self.src_port)
        dst_port_net = struct.pack("!H", self.dst_port)

        meta = (
            src_ip_bytes
            + src_port_net
            + dst_port_net
            + dst_ip_bytes
            + (b"\x00" * 32)  # IPv6 bytes
            + b"\x04"  # addressFamily = 4 (IPv4)
            + (b"\x00" * 3)  # reserved[3]
        )
        assert len(meta) == 48

        header = struct.pack(
            "<HBBHIQ",
            0x5058,  # magic 'PX'
            1,  # version
            1,  # protocol = Udp
            len(meta),
            len(self.raw_payload),
            self.timestamp_ns,
        )
        assert len(header) == 18

        return header + meta + self.raw_payload


class PcapReader:
    """Zero-dependency parser for standard PCAP captures."""

    @staticmethod
    def read_packets(filepath: str | Path) -> list[PcapPacket]:
        packets: list[PcapPacket] = []
        p = Path(filepath)
        if not p.is_file():
            return packets

        with open(p, "rb") as f:
            global_hdr = f.read(24)
            if len(global_hdr) < 24:
                return packets

            magic = struct.unpack("<I", global_hdr[:4])[0]
            if magic in (0xA1B2C3D4, 0xA1B23C4D):
                endian = "<"
                is_nano = (magic == 0xA1B23C4D)
            elif magic in (0xD4C3B2A1, 0x4D3CB2A1):
                endian = ">"
                is_nano = (magic == 0x4D3CB2A1)
            else:
                return packets

            seq = 1
            while True:
                hdr_bytes = f.read(16)
                if len(hdr_bytes) < 16:
                    break
                ts_sec, ts_usec, incl_len, _ = struct.unpack(f"{endian}IIII", hdr_bytes)
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

                ts_ns = (ts_sec * 1_000_000_000) + (ts_usec if is_nano else ts_usec * 1_000)
                try:
                    text_preview = payload.decode("utf-8")
                except UnicodeDecodeError:
                    text_preview = payload.hex()

                packets.append(
                    PcapPacket(
                        seq_id=seq,
                        timestamp_ns=ts_ns,
                        src_ip=src_ip,
                        src_port=src_port,
                        dst_ip=dst_ip,
                        dst_port=dst_port,
                        raw_payload=payload,
                        payload_text=text_preview,
                    )
                )
                seq += 1

        return packets


# ---------------------------------------------------------------------------
# Interactive Simulation Step Coordinator
# ---------------------------------------------------------------------------

class InteractiveSimulationCoordinator:
    """
    Coordinates step-by-step execution across real C++ handler binaries:
      1. Holds preloaded PCAP packets.
      2. Intercepts held messages in /tmp/proxy_sim_hold_<channel>.sock.
      3. Forwards held messages into the downstream /tmp/proxy_sim_<channel>.sock on step.
      4. Tracks drops if a handler completes without producing an envelope.
    """

    def __init__(self, chain_config: dict[str, Any], pcap_packets: list[PcapPacket]):
        self.chain_config = chain_config
        self.handlers: list[dict[str, Any]] = chain_config.get("handlers", [])
        self.pcap_packets = pcap_packets
        self.current_packet_idx = 0
        self.events: list[dict[str, Any]] = []
        self.step_history: list[dict[str, Any]] = []
        self.event_counter = 1
        self.lock = threading.Lock()

        # In-flight active message state
        # None if waiting for next packet to start, or { "packet": PcapPacket, "curr_node_idx": int, "channel": str, "held_data": bytes }
        self.active_in_flight: Optional[dict[str, Any]] = None

        # Build topology mapping: channel -> [consumer_handlers]
        self.channel_to_consumers: dict[str, list[dict[str, Any]]] = {}
        for h in self.handlers:
            in_chs = h.get("input_channels") or ([h.get("input_channel")] if h.get("input_channel") else [])
            for ch in in_chs:
                if ch:
                    self.channel_to_consumers.setdefault(ch, []).append(h)

        # Create receiving sockets for held intermediate channels (/tmp/proxy_sim_hold_<ch>.sock)
        self.hold_sockets: dict[str, socket.socket] = {}
        self._init_hold_sockets()

    def _init_hold_sockets(self):
        for h in self.handlers:
            out_chs = h.get("output_channels") or ([h.get("output_channel")] if h.get("output_channel") else [])
            for ch in out_chs:
                if not ch or ch in self.hold_sockets:
                    continue
                hold_path = f"/tmp/proxy_sim_hold_{ch}.sock"
                try:
                    if os.path.exists(hold_path):
                        os.unlink(hold_path)
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
                    s.bind(hold_path)
                    s.setblocking(False)
                    self.hold_sockets[ch] = s
                except Exception as ex:
                    print(f"[Coordinator] Warning creating hold socket {hold_path}: {ex}")

    def close(self):
        for ch, s in self.hold_sockets.items():
            try:
                s.close()
                hold_path = f"/tmp/proxy_sim_hold_{ch}.sock"
                if os.path.exists(hold_path):
                    os.unlink(hold_path)
            except Exception:
                pass
        self.hold_sockets.clear()

    def select_packet(self, packet_id: int) -> dict[str, Any]:
        """Sets the active/next packet to process to the packet with the given ID."""
        with self.lock:
            found_idx = -1
            for idx, p in enumerate(self.pcap_packets):
                if p.seq_id == packet_id:
                    found_idx = idx
                    break

            if found_idx == -1:
                return {"status": "error", "message": f"Packet #{packet_id} not found"}

            # If there was an in-flight message from another packet, clear it so we start fresh with selected packet
            self.active_in_flight = None
            # Drain hold sockets to clear old buffered messages
            for sock in self.hold_sockets.values():
                while True:
                    try:
                        sock.recvfrom(65535)
                    except Exception:
                        break

            self.current_packet_idx = found_idx
            selected_pkt = self.pcap_packets[found_idx]
            return {"status": "ok", "currentPacketIdx": self.current_packet_idx, "packet": selected_pkt.to_dict()}

    def _snapshot_state(self) -> dict[str, Any]:
        """Snapshots the coordinator's current state so it can be restored on step back."""
        in_flight_copy = None
        if self.active_in_flight:
            in_flight_copy = {
                "packet": self.active_in_flight["packet"],
                "curr_node_name": self.active_in_flight["curr_node_name"],
                "curr_node_type": self.active_in_flight["curr_node_type"],
                "output_channel": self.active_in_flight["output_channel"],
                "held_data": bytes(self.active_in_flight["held_data"]),
            }
        return {
            "current_packet_idx": self.current_packet_idx,
            "active_in_flight": in_flight_copy,
            "events_len": len(self.events),
            "event_counter": self.event_counter,
        }

    def step_forward(self) -> dict[str, Any]:
        """
        Executes one step in the pipeline:
          Case A: No in-flight message -> load next packet into first handler's input channel.
          Case B: In-flight message held at an intermediate output channel -> release to next input channel.
        """
        with self.lock:
            # Save pre-step snapshot for step_back
            snapshot = self._snapshot_state()

            # Check if there is an in-flight message held at an output channel
            if self.active_in_flight is not None:
                res = self._advance_in_flight()
                if res.get("status") in ("ok", "dropped"):
                    self.step_history.append(snapshot)
                return res

            # Case A: Start next packet
            if self.current_packet_idx >= len(self.pcap_packets):
                return {"status": "finished", "message": "All PCAP packets have completed the chain."}

            res = self._start_next_packet()
            if res.get("status") in ("ok", "dropped"):
                self.step_history.append(snapshot)
            return res

    def _start_next_packet(self) -> dict[str, Any]:
        pkt = self.pcap_packets[self.current_packet_idx]
        self.current_packet_idx += 1

        first_handler = self.handlers[0] if self.handlers else None
        if not first_handler:
            return {"status": "error", "message": "No handlers defined"}

        in_ch = first_handler.get("input_channel", "default_in")
        first_in_sock = f"/tmp/proxy_sim_{in_ch}.sock"

        # Check if first handler is udp_ingress_handler with an external UDP port
        udp_port = first_handler.get("config", {}).get("ingress.port") if first_handler.get("type") == "udp_ingress_handler" else None

        # 1. Emit Ingress event
        ev_ingress = {
            "eventId": self.event_counter,
            "msgId": pkt.seq_id,
            "timestampNs": pkt.timestamp_ns,
            "type": "ingress",
            "source": f"{pkt.src_ip}:{pkt.src_port}",
            "target": first_handler["name"],
            "targetType": first_handler.get("type", "unknown"),
            "channel": "udp_network" if udp_port else in_ch,
            "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
            "payloadFull": pkt.payload_text,
            "status": "received",
            "latencyUs": 0.0,
            "detail": f"Packet #{pkt.seq_id} injected into {first_handler['name']}",
        }
        self.event_counter += 1
        self.events.append(ev_ingress)

        # 2. Inject packet into first handler
        if udp_port:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.sendto(pkt.raw_payload, ("127.0.0.1", int(udp_port)))
            s.close()
        else:
            # Deliver packed envelope to the input socket
            env = pkt.pack_envelope()
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            try:
                s.sendto(env, first_in_sock)
            except Exception as ex:
                print(f"[Coordinator] Error delivering to {first_in_sock}: {ex}")
            s.close()

        # 3. Wait up to 300ms for handler to process and produce output in its hold socket
        out_ch = first_handler.get("output_channel")
        held_data = self._poll_held_output(out_ch, timeout_sec=0.35)

        if held_data:
            # Message reached the output channel and is held!
            self.active_in_flight = {
                "packet": pkt,
                "curr_node_name": first_handler["name"],
                "curr_node_type": first_handler.get("type"),
                "output_channel": out_ch,
                "held_data": held_data,
            }
            ev_hop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "hop",
                "source": "ingress",
                "target": first_handler["name"],
                "targetType": first_handler.get("type"),
                "channel": in_ch if in_ch else out_ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "at_node",
                "latencyUs": 10.0,
                "detail": f"Injected into '{first_handler['name']}' -> held at output channel '{out_ch}'",
            }
            self.event_counter += 1
            self.events.append(ev_hop)
            return {"status": "ok", "event": ev_hop, "heldChannel": out_ch}
        else:
            # 0 bytes output: message was dropped by this handler!
            self.active_in_flight = None
            ev_drop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "drop",
                "source": "ingress",
                "target": first_handler["name"],
                "targetType": first_handler.get("type"),
                "channel": in_ch if in_ch else "ingress",
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "dropped",
                "latencyUs": 15.0,
                "detail": f"Packet #{pkt.seq_id} rejected and discarded at node '{first_handler['name']}'",
            }
            self.event_counter += 1
            self.events.append(ev_drop)
            return {"status": "dropped", "event": ev_drop}

    def _advance_in_flight(self) -> dict[str, Any]:
        """Releases the held message from the output channel into the next handler."""
        info = self.active_in_flight
        if not info:
            return {"status": "idle"}

        pkt = info["packet"]
        ch = info["output_channel"]
        held_data = info["held_data"]

        consumers = self.channel_to_consumers.get(ch, [])
        if not consumers:
            # End of chain or sink handler reached!
            self.active_in_flight = None
            ev_egress = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "egress",
                "source": info["curr_node_name"],
                "target": info["curr_node_name"],
                "targetType": info["curr_node_type"],
                "channel": ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "transmitted",
                "latencyUs": 25.0,
                "detail": f"Packet #{pkt.seq_id} successfully delivered at chain egress ({ch})",
            }
            self.event_counter += 1
            self.events.append(ev_egress)
            return {"status": "ok", "event": ev_egress, "finishedPacket": pkt.seq_id}

        next_handler = consumers[0]
        next_in_sock = f"/tmp/proxy_sim_{ch}.sock"

        # Deliver the held envelope into the next handler's input channel socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        try:
            s.sendto(held_data, next_in_sock)
        except Exception as ex:
            print(f"[Coordinator] Error delivering held envelope to {next_in_sock}: {ex}")
        s.close()

        # Wait up to 150ms for next handler to process
        next_out_ch = next_handler.get("output_channel")
        is_sink = not next_out_ch or "egress" in next_handler.get("type", "") or "print" in next_handler.get("type", "")

        if is_sink:
            time.sleep(0.08)  # Let sink print or egress
            self.active_in_flight = None
            ev_sink = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "egress",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "transmitted",
                "latencyUs": 20.0,
                "detail": f"Packet #{pkt.seq_id} reached sink/egress handler '{next_handler['name']}'",
            }
            self.event_counter += 1
            self.events.append(ev_sink)
            return {"status": "ok", "event": ev_sink, "finishedPacket": pkt.seq_id}

        # Intermediate handler
        next_held = self._poll_held_output(next_out_ch, timeout_sec=0.15)
        if next_held:
            self.active_in_flight = {
                "packet": pkt,
                "curr_node_name": next_handler["name"],
                "curr_node_type": next_handler.get("type"),
                "output_channel": next_out_ch,
                "held_data": next_held,
            }
            ev_hop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "hop",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "at_node",
                "latencyUs": 15.0,
                "detail": f"Packet #{pkt.seq_id} forwarded from '{info['curr_node_name']}' to '{next_handler['name']}' -> held at '{next_out_ch}'",
            }
            self.event_counter += 1
            self.events.append(ev_hop)
            return {"status": "ok", "event": ev_hop, "heldChannel": next_out_ch}
        else:
            # Dropped at this handler! (e.g. XmlVerificationHandler discarded invalid schema)
            self.active_in_flight = None
            ev_drop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "timestampNs": time.time_ns(),
                "type": "drop",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "dropped",
                "latencyUs": 20.0,
                "detail": f"Packet #{pkt.seq_id} dropped by '{next_handler['name']}' (0 bytes emitted to '{next_out_ch}')",
            }
            self.event_counter += 1
            self.events.append(ev_drop)
            return {"status": "dropped", "event": ev_drop}

    def _poll_held_output(self, channel_name: Optional[str], timeout_sec: float = 0.15) -> Optional[bytes]:
        if not channel_name or channel_name not in self.hold_sockets:
            return None
        sock = self.hold_sockets[channel_name]
        start = time.time()
        while time.time() - start < timeout_sec:
            try:
                data, _ = sock.recvfrom(65535)
                if data:
                    return data
            except (BlockingIOError, socket.timeout):
                pass
            except Exception:
                break
            time.sleep(0.005)
        return None

    def step_back(self) -> dict[str, Any]:
        """
        Reverts the pipeline state to the previous step snapshot.
        """
        with self.lock:
            if not self.step_history:
                return {"status": "error", "message": "No previous steps to revert."}

            prev_snap = self.step_history.pop()
            self.current_packet_idx = prev_snap["current_packet_idx"]
            self.active_in_flight = prev_snap["active_in_flight"]
            self.event_counter = prev_snap["event_counter"]
            
            # Truncate events list back to snapshot length
            del self.events[prev_snap["events_len"]:]

            # Drain any hold sockets to prevent desync
            for sock in self.hold_sockets.values():
                while True:
                    try:
                        sock.recvfrom(65535)
                    except Exception:
                        break

            # If the reverted state had a message held in a socket, the held_data is restored in active_in_flight
            active_event = self.events[-1] if self.events else None
            return {
                "status": "ok",
                "currentPacketIdx": self.current_packet_idx,
                "inFlight": bool(self.active_in_flight),
                "event": active_event,
            }

    def reset(self):
        with self.lock:
            self.current_packet_idx = 0
            self.events.clear()
            self.step_history.clear()
            self.event_counter = 1
            self.active_in_flight = None
            # Drain any lingering data in hold sockets
            for sock in self.hold_sockets.values():
                while True:
                    try:
                        sock.recvfrom(65535)
                    except Exception:
                        break


# ---------------------------------------------------------------------------
# HTTP API Server for Simulation Dashboard
# ---------------------------------------------------------------------------

class SimDashboardHandler(http.server.SimpleHTTPRequestHandler):
    coordinator: InteractiveSimulationCoordinator
    pcap_path: str

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            dashboard_file = Path(__file__).resolve().parent / "simulation_dashboard.html"
            if dashboard_file.is_file():
                self.wfile.write(dashboard_file.read_bytes())
            else:
                self.wfile.write(b"<h1>Simulation Dashboard HTML not found</h1>")
        elif self.path == "/api/simulation":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            packets_meta = [p.to_dict() for p in self.coordinator.pcap_packets]
            data = {
                "chain": self.coordinator.chain_config,
                "pcap": self.pcap_path,
                "packetCount": len(self.coordinator.pcap_packets),
                "packets": packets_meta,
                "events": self.coordinator.events,
                "currentPacketIdx": self.coordinator.current_packet_idx,
                "inFlight": bool(self.coordinator.active_in_flight),
            }
            self.wfile.write(json.dumps(data).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/api/simulation/step":
            result = self.coordinator.step_forward()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode("utf-8"))
        elif self.path == "/api/simulation/step_back":
            result = self.coordinator.step_back()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode("utf-8"))
        elif self.path == "/api/simulation/reset":
            self.coordinator.reset()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        elif self.path == "/api/simulation/select":
            content_len = int(self.headers.get("Content-Length", 0))
            post_body = self.rfile.read(content_len) if content_len > 0 else b"{}"
            try:
                params = json.loads(post_body.decode("utf-8"))
            except Exception:
                params = {}
            pkt_id = params.get("id")
            if pkt_id is None:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b'{"status":"error","message":"Missing packet id"}')
                return
            result = self.coordinator.select_packet(int(pkt_id))
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass  # Suppress routine HTTP log spam


def start_dashboard_server(
    port: int,
    coordinator: InteractiveSimulationCoordinator,
    pcap_path: str,
) -> http.server.HTTPServer:
    handler = SimDashboardHandler
    handler.coordinator = coordinator
    handler.pcap_path = pcap_path

    server = http.server.HTTPServer(("0.0.0.0", port), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


# ---------------------------------------------------------------------------
# Configuration & Binary Management
# ---------------------------------------------------------------------------

def load_chain_config(config_path: Path) -> dict[str, Any]:
    if not config_path.is_file():
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    content = config_path.read_text(encoding="utf-8")
    if config_path.suffix.lower() == ".json":
        return json.loads(content)
    elif config_path.suffix.lower() == ".toml":
        if tomllib is None:
            raise RuntimeError("Python 3.11+ is required for built-in TOML support.")
        return tomllib.loads(content)
    else:
        if tomllib:
            try:
                return tomllib.loads(content)
            except Exception:
                pass
        return json.loads(content)


def find_bin_dir(root_dir: Path, requested_bin_dir: str | None = None) -> Path:
    if requested_bin_dir:
        candidate = Path(requested_bin_dir)
        if candidate.is_dir():
            return candidate

    search_paths = [
        root_dir / "build" / "bin",
        root_dir / "build" / "gcc16-release" / "bin",
        root_dir / "build" / "gcc16-debug" / "bin",
        root_dir / "build" / "release" / "bin",
        root_dir / "build" / "debug" / "bin",
        root_dir / "bin",
    ]
    for p in search_paths:
        if p.is_dir():
            return p

    raise FileNotFoundError(
        f"Could not find build bin directory. Looked in: {[str(p) for p in search_paths]}. "
        "Please build the project first or provide --bin-dir."
    )


def flatten_dict(d: dict[str, Any], prefix: str = "") -> list[tuple[str, Any]]:
    items: list[tuple[str, Any]] = []
    for k, v in d.items():
        full_key = f"{prefix}.{k}" if prefix else str(k)
        if isinstance(v, dict):
            items.extend(flatten_dict(v, full_key))
        else:
            items.append((full_key, v))
    return items


def generate_handler_config(
    global_config: dict[str, Any],
    backend_name: str,
    handler_spec: dict[str, Any],
) -> str:
    lines: list[str] = [
        f"# Automatically generated configuration for handler: {handler_spec.get('name', 'handler')}",
        f"# Backend: {backend_name}",
        "",
        "# Global / backend settings",
    ]

    for key, val in flatten_dict(global_config):
        if backend_name == "sim" and ("channel." in key or "shm." in key or "file." in key):
            continue  # Let SimBackend use default /tmp/proxy_sim sockets in simulation mode
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    lines.append("")
    lines.append("# Handler-specific settings")

    handler_name = handler_spec.get("name")
    if handler_name:
        global_prefix = (
            global_config.get("telemetry", {}).get("prefix", "proxy")
            if isinstance(global_config.get("telemetry"), dict)
            else global_config.get("telemetry.prefix", "proxy")
        )
        lines.append(f"telemetry.prefix={global_prefix}.{handler_name}")
        lines.append(f"sim.handler_name={handler_name}")

    if "input_channel" in handler_spec:
        in_ch = handler_spec["input_channel"]
        lines.append(f"channel.input={in_ch}")
        lines.append(f"egress.input_channel={in_ch}")
        lines.append(f"dummy.channel={in_ch}")
        lines.append(f"print.channel={in_ch}")
        lines.append(f"split.input_channel={in_ch}")
        lines.append(f"xml.input_channel={in_ch}")
    if "input_channels" in handler_spec:
        in_chs = handler_spec["input_channels"]
        if isinstance(in_chs, list):
            lines.append(f"merge.input_channels={','.join(in_chs)}")
        else:
            lines.append(f"merge.input_channels={in_chs}")
    if "output_channel" in handler_spec:
        out_ch = handler_spec["output_channel"]
        lines.append(f"channel.output={out_ch}")
        lines.append(f"ingress.output_channel={out_ch}")
        lines.append(f"dummy.channel={out_ch}")
        lines.append(f"merge.output_channel={out_ch}")
        lines.append(f"xml.output_channel={out_ch}")
    if "output_channels" in handler_spec:
        out_chs = handler_spec["output_channels"]
        if isinstance(out_chs, list):
            lines.append(f"split.output_channels={','.join(out_chs)}")
        else:
            lines.append(f"split.output_channels={out_chs}")

    params = handler_spec.get("config", {})
    for key, val in flatten_dict(params):
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Runner Execution Engine
# ---------------------------------------------------------------------------

def run_chain(
    config_path: Path,
    bin_dir_override: str | None = None,
    work_dir_override: str | None = None,
    backend_override: str | None = None,
    pcap_path: str | None = None,
    dashboard_port: int | None = 8090,
) -> int:
    chain_def = load_chain_config(config_path)

    backend = backend_override or chain_def.get("backend")
    if not backend:
        raise ValueError("Configuration must specify 'backend' (e.g. 'file', 'socket', or 'sim')")

    handlers: list[dict[str, Any]] = chain_def.get("handlers", [])
    if not handlers:
        raise ValueError("Configuration must contain at least one handler in 'handlers'")

    root_dir = Path(__file__).resolve().parent.parent
    bin_dir = find_bin_dir(root_dir, bin_dir_override)
    print(f"[ChainRunner] Using binaries directory: {bin_dir}")
    print(f"[ChainRunner] Target backend: {backend}")

    if work_dir_override:
        work_dir = Path(work_dir_override)
        work_dir.mkdir(parents=True, exist_ok=True)
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="proxy_chain_", dir="/tmp"))

    print(f"[ChainRunner] Working directory for runtime configs: {work_dir}")

    global_config = chain_def.get("config", {})

    pcap_packets: list[PcapPacket] = []
    if pcap_path:
        pcap_packets = PcapReader.read_packets(pcap_path)
        print(f"[ChainRunner] Preloaded {len(pcap_packets)} packet(s) from PCAP: {pcap_path} (Waiting for Step/Play)")

    coordinator: Optional[InteractiveSimulationCoordinator] = None
    http_server_instance: Optional[http.server.HTTPServer] = None

    if backend == "sim" or dashboard_port:
        coordinator = InteractiveSimulationCoordinator(chain_def, pcap_packets)
        if dashboard_port:
            http_server_instance = start_dashboard_server(
                dashboard_port,
                coordinator,
                str(pcap_path) if pcap_path else "None",
            )
            print(f"=======================================================")
            print(f" 🚀 Interactive Simulation Dashboard live at:")
            print(f" 👉 http://localhost:{dashboard_port}")
            print(f"=======================================================")

    processes: list[tuple[str, subprocess.Popen, Path]] = []
    shutting_down = False

    def handle_signal(sig, frame):
        nonlocal shutting_down
        if shutting_down:
            return
        shutting_down = True
        print(f"\n[ChainRunner] Caught signal {sig}, terminating chain...")
        for name, proc, _ in reversed(processes):
            if proc.poll() is None:
                print(f"[ChainRunner] Stopping {name} (PID {proc.pid})...")
                proc.send_signal(signal.SIGINT)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        # Launch each handler process
        for idx, handler_spec in enumerate(handlers):
            handler_type = handler_spec.get("type")
            if not handler_type:
                raise ValueError(f"Handler #{idx + 1} is missing 'type'")

            handler_name = handler_spec.get("name", f"{handler_type}_{idx + 1}")
            binary_name = f"{handler_type}_{backend}"
            binary_path = bin_dir / binary_name

            if not binary_path.is_file():
                raise FileNotFoundError(
                    f"Executable '{binary_name}' not found in {bin_dir}. "
                    f"Ensure handler '{handler_type}' is built for backend '{backend}'."
                )

            conf_file = work_dir / f"{handler_name}.conf"
            conf_content = generate_handler_config(global_config, backend, handler_spec)
            conf_file.write_text(conf_content, encoding="utf-8")

            print(f"[ChainRunner] Launching {handler_name} [{binary_name}] with config: {conf_file}")
            proc = subprocess.Popen(
                [str(binary_path), str(conf_file)],
                cwd=str(root_dir),
            )
            processes.append((handler_name, proc, conf_file))
            time.sleep(0.12)

        print(f"\n[ChainRunner] All {len(processes)} handler(s) started and ready.")
        print("[ChainRunner] Packets are paused and ready in memory. Click Step Forward or Play in the dashboard.")

        while not shutting_down:
            for name, proc, _ in processes:
                exit_code = proc.poll()
                if exit_code is not None:
                    print(f"[ChainRunner] Handler {name} exited with code {exit_code}")
                    shutting_down = True
                    break
            time.sleep(0.5)

    except Exception as e:
        print(f"[ChainRunner] Error: {e}", file=sys.stderr)
        shutting_down = True
    finally:
        if coordinator:
            coordinator.close()
        if http_server_instance:
            try:
                http_server_instance.shutdown()
            except Exception:
                pass

        for name, proc, _ in reversed(processes):
            if proc.poll() is None:
                proc.send_signal(signal.SIGINT)
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    print(f"[ChainRunner] Force killing {name} (PID {proc.pid})...")
                    proc.kill()
                    proc.wait()

        print("[ChainRunner] All handlers stopped cleanly.")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure and run chains of Proxy handlers across any backend."
    )
    parser.add_argument(
        "config",
        type=Path,
        help="Path to chain configuration file (.toml or .json)",
    )
    parser.add_argument(
        "--backend",
        type=str,
        default=None,
        help="Override backend specified in chain configuration (e.g. 'sim', 'file', 'shm', 'socket')",
    )
    parser.add_argument(
        "--pcap",
        type=str,
        default=None,
        help="Path to PCAP capture file to feed into chain ingress",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8090,
        help="HTTP port for interactive Visual Simulation Dashboard (default: 8090)",
    )
    parser.add_argument(
        "--bin-dir",
        type=str,
        default=None,
        help="Custom path to directory containing handler binaries",
    )
    parser.add_argument(
        "--work-dir",
        type=str,
        default=None,
        help="Directory to store runtime generated configuration files",
    )

    args = parser.parse_args()
    return run_chain(
        args.config,
        bin_dir_override=args.bin_dir,
        work_dir_override=args.work_dir,
        backend_override=args.backend,
        pcap_path=args.pcap,
        dashboard_port=args.port,
    )


if __name__ == "__main__":
    sys.exit(main())
