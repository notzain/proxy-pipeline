"""
Simulation coordinator managing real-time packet loading, step-by-step pipeline execution,
socket interception, and deterministic MessageResult tracking across real C++ handlers.
"""

import json
import os
import socket
import struct
import threading
import time
from typing import Any, Optional

from .handlers import (
    HandlerDescriptor,
    HandlerProtocol,
    HandlerRole,
    load_handler_descriptor,
)
from .injectors import get_injector_for_handler
from .pcap import PcapPacket


class InteractiveSimulationCoordinator:
    """
    Coordinates Real-Time step-by-step execution across real C++ handler binaries:
      1. Holds preloaded PCAP packets and loads them into ingress on command.
      2. Listens on /tmp/proxy_sim_events.sock for exact MessageResult outcomes
         (Forwarded, Consumed, Dropped with reason & errorCode) emitted by SimBackend.
      3. Intercepts held messages in /tmp/proxy_sim_hold_<channel>.sock.
      4. Steps held messages into the downstream /tmp/proxy_sim_<channel>.sock on command.
    """

    EVENT_SOCK_PATH = "/tmp/proxy_sim_events.sock"

    def __init__(self, chain_config: dict[str, Any], pcap_packets: list[PcapPacket]):
        self.chain_config = chain_config
        self.handlers: list[dict[str, Any]] = chain_config.get("handlers", [])
        self.pcap_packets = pcap_packets
        self.current_packet_idx = 0
        self.events: list[dict[str, Any]] = []
        self.event_counter = 1
        self.lock = threading.Lock()

        # In-flight active message state:
        # None when idle between packets, or dict with:
        #   stage: "loaded" (staged at ingress) | "held" (held at output channel after Forwarded)
        #   packet: PcapPacket
        #   batch: list[PcapPacket]
        #   curr_node_name: str
        #   curr_node_type: str
        #   input_channel: Optional[str]
        #   output_channel: Optional[str]
        #   held_data: Optional[list[bytes]]
        #   pending_branches: list[dict[str, Any]] (for fan-out / SplitHandler)
        self.active_in_flight: Optional[dict[str, Any]] = None

        # Custom batch groupings: list of lists of packet IDs
        self.batch_groups: list[list[int]] = []

        # Build topology mapping: channel -> [consumer_handlers]
        self.channel_to_consumers: dict[str, list[dict[str, Any]]] = {}
        for h in self.handlers:
            in_chs = h.get("input_channels") or ([h.get("input_channel")] if h.get("input_channel") else [])
            if isinstance(in_chs, str):
                in_chs = [c.strip() for c in in_chs.split(",") if c.strip()]
            for ch in in_chs:
                if ch:
                    self.channel_to_consumers.setdefault(ch, []).append(h)

        # Create receiving socket for MessageResult events (/tmp/proxy_sim_events.sock)
        self.events_sock: Optional[socket.socket] = None
        self._init_events_socket()

        # Create receiving sockets for held intermediate channels (/tmp/proxy_sim_hold_<ch>.sock)
        self.hold_sockets: dict[str, socket.socket] = {}
        self._init_hold_sockets()

    def _init_events_socket(self):
        try:
            if os.path.exists(self.EVENT_SOCK_PATH):
                os.unlink(self.EVENT_SOCK_PATH)
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            s.bind(self.EVENT_SOCK_PATH)
            s.setblocking(False)
            self.events_sock = s
        except Exception as ex:
            print(f"[Coordinator] Warning creating events socket {self.EVENT_SOCK_PATH}: {ex}")

    def _discover_all_channels(self) -> set[str]:
        channels: set[str] = set()
        for h in self.handlers:
            for key in ("output_channel", "input_channel"):
                val = h.get(key)
                if isinstance(val, str) and val.strip():
                    for part in val.split(","):
                        if part.strip():
                            channels.add(part.strip())
            for key in ("output_channels", "input_channels"):
                vals = h.get(key)
                if isinstance(vals, list):
                    for v in vals:
                        if str(v).strip():
                            channels.add(str(v).strip())
                elif isinstance(vals, str):
                    for part in vals.split(","):
                        if part.strip():
                            channels.add(part.strip())
            h_cfg = h.get("config", {})
            if isinstance(h_cfg, dict):
                def_out = h_cfg.get("route.default_output")
                if isinstance(def_out, str) and def_out.strip():
                    channels.add(def_out.strip())
                for k, v in h_cfg.items():
                    if k.startswith("route.rule.") and k.endswith(".target") and isinstance(v, str) and v.strip():
                        channels.add(v.strip())
        return channels

    def _init_hold_sockets(self):
        for ch in self._discover_all_channels():
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

    def _ensure_hold_socket(self, ch: str) -> Optional[socket.socket]:
        if not ch:
            return None
        if ch in self.hold_sockets:
            return self.hold_sockets[ch]
        hold_path = f"/tmp/proxy_sim_hold_{ch}.sock"
        try:
            if os.path.exists(hold_path):
                os.unlink(hold_path)
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            s.bind(hold_path)
            s.setblocking(False)
            self.hold_sockets[ch] = s
            return s
        except Exception:
            return None

    def _drain_socket(self, sock: Optional[socket.socket]):
        if not sock:
            return
        while True:
            try:
                sock.recvfrom(65535)
            except Exception:
                break

    def _drain_all_sockets(self):
        self._drain_socket(self.events_sock)
        for sock in self.hold_sockets.values():
            self._drain_socket(sock)

    def wait_for_handlers_ready(self, timeout_sec: float = 5.0) -> bool:
        """
        Blocks until all configured handlers have emitted their 'handler_ready'
        startup signal to /tmp/proxy_sim_events.sock, ensuring all channel sockets
        and worker threads are bound and listening before any packet is injected.
        """
        if not self.events_sock or not self.handlers:
            return True

        expected = {
            h.get("name", f"{h.get('type', 'handler')}_{idx + 1}")
            for idx, h in enumerate(self.handlers)
        }
        ready: set[str] = set()
        deadline = time.time() + timeout_sec

        while time.time() < deadline and ready != expected:
            try:
                raw, _ = self.events_sock.recvfrom(65535)
                if raw:
                    ev = json.loads(raw.decode("utf-8", errors="replace"))
                    if ev.get("type") == "handler_ready" and ev.get("handler"):
                        ready.add(str(ev["handler"]))
                        continue
            except (BlockingIOError, socket.timeout):
                pass
            except Exception:
                pass
            time.sleep(0.005)

        # Allow a brief settle window for worker threads to enter recvBatch
        time.sleep(0.03)
        return ready == expected

    def close(self):
        if self.events_sock:
            try:
                self.events_sock.close()
                if os.path.exists(self.EVENT_SOCK_PATH):
                    os.unlink(self.EVENT_SOCK_PATH)
            except Exception:
                pass
            self.events_sock = None

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
        """Selects a specific packet ID from the PCAP queue to be loaded next."""
        with self.lock:
            found_idx = -1
            for idx, p in enumerate(self.pcap_packets):
                if p.seq_id == packet_id:
                    found_idx = idx
                    break

            if found_idx == -1:
                return {"status": "error", "message": f"Packet #{packet_id} not found"}

            self.active_in_flight = None
            self._drain_all_sockets()
            self.current_packet_idx = found_idx
            selected_pkt = self.pcap_packets[found_idx]
            return {
                "status": "ok",
                "currentPacketIdx": self.current_packet_idx,
                "packet": selected_pkt.to_dict(),
                "inFlight": False,
            }

    def load_next_packet(self, packet_id: Optional[int] = None) -> dict[str, Any]:
        """
        Loads the next PCAP packet (or specified packet_id) and stages it at chain ingress.
        Does not execute the first handler yet until step_forward() is called.
        """
        with self.lock:
            if packet_id is not None:
                found_idx = next((i for i, p in enumerate(self.pcap_packets) if p.seq_id == packet_id), -1)
                if found_idx == -1:
                    return {"status": "error", "message": f"Packet #{packet_id} not found"}
                self.current_packet_idx = found_idx
                self.active_in_flight = None
                self._drain_all_sockets()

            if self.active_in_flight is not None:
                return {
                    "status": "busy",
                    "message": "A packet is already in-flight. Step it forward through the chain before loading the next packet.",
                }

            if self.current_packet_idx >= len(self.pcap_packets):
                return {"status": "finished", "message": "All PCAP packets have been loaded."}

            pkt = self.pcap_packets[self.current_packet_idx]

            # Check if the current packet is part of a batch group
            current_batch_pkts: list[PcapPacket] = [pkt]
            for group in self.batch_groups:
                if pkt.seq_id in group:
                    id_to_pkt = {p.seq_id: p for p in self.pcap_packets}
                    matched = [id_to_pkt[pid] for pid in group if pid in id_to_pkt]
                    if matched:
                        current_batch_pkts = matched
                    break

            self.current_packet_idx += len(current_batch_pkts)

            first_handler = self.handlers[0] if self.handlers else None
            if not first_handler:
                return {"status": "error", "message": "No handlers defined in chain"}

            first_desc = load_handler_descriptor(first_handler.get("type", ""))
            if first_desc.is_generator:
                return {
                    "status": "error",
                    "message": f"Handler '{first_handler['name']}' is a generator with no input channel. Click '⚡ Trigger' on the node to emit messages.",
                }

            in_ch = first_handler.get("input_channel")
            if not in_ch and first_handler.get("input_channels"):
                chs = first_handler["input_channels"]
                in_ch = chs[0] if isinstance(chs, list) and chs else str(chs).split(",")[0].strip()

            is_ext_ingress = first_desc.is_external_ingress
            ingress_channel_label = (
                f"{first_desc.protocol.value}_network"
                if is_ext_ingress
                else (in_ch or "ingress")
            )

            is_batch = len(current_batch_pkts) > 1
            seq_label = (
                f"Batch [{', '.join(str(p.seq_id) for p in current_batch_pkts)}]"
                if is_batch
                else f"#{pkt.seq_id}"
            )

            self.active_in_flight = {
                "stage": "loaded",
                "packet": pkt,
                "batch": current_batch_pkts,
                "curr_node_name": first_handler["name"],
                "curr_node_type": first_handler.get("type", "unknown"),
                "input_channel": ingress_channel_label,
                "output_channel": None,
                "held_data": None,
                "pending_branches": [],
            }

            ev_ingress = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in current_batch_pkts],
                "batchCount": len(current_batch_pkts),
                "timestampNs": pkt.timestamp_ns,
                "type": "ingress",
                "result": "Loaded",
                "source": f"{pkt.src_ip}:{pkt.src_port}",
                "handler": first_handler["name"],
                "handlerType": first_handler.get("type", "unknown"),
                "inputChannel": ingress_channel_label,
                "payloadSummary": (
                    f"Batch of {len(current_batch_pkts)} packets"
                    if is_batch
                    else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text)
                ),
                "payloadFull": (
                    "\n---\n".join(p.payload_text for p in current_batch_pkts)
                    if is_batch
                    else pkt.payload_text
                ),
                "status": "loaded",
                "latencyUs": 0.0,
                "detail": f"Packet {seq_label} staged at ingress queue '{ingress_channel_label}'",
            }
            self.event_counter += 1
            self.events.append(ev_ingress)

            return {
                "status": "ok",
                "event": ev_ingress,
                "packet": pkt.to_dict(),
                "currentPacketIdx": self.current_packet_idx,
                "inFlight": True,
            }

    def step_forward(self, auto_load: bool = False) -> dict[str, Any]:
        """
        Steps the active in-flight packet forward into the next handler in the chain.
        If no packet is currently loaded and auto_load is True, loads the next packet.
        """
        with self.lock:
            if self.active_in_flight is None:
                if auto_load:
                    load_res = self._load_next_packet_unlocked()
                    return load_res
                return {
                    "status": "no_packet_loaded",
                    "message": "No packet is currently in-flight. Click 'Load Next Packet' first.",
                }

            stage = self.active_in_flight.get("stage")
            if stage == "loaded":
                return self._step_loaded_into_first_handler()
            else:
                return self._advance_in_flight()

    def _load_next_packet_unlocked(self) -> dict[str, Any]:
        """Internal helper to call load_next_packet when self.lock is already held."""
        # Release and re-acquire via a clean internal method
        if self.current_packet_idx >= len(self.pcap_packets):
            return {"status": "finished", "message": "All PCAP packets have completed the chain."}
        self.lock.release()
        try:
            return self.load_next_packet()
        finally:
            self.lock.acquire()

    def _step_loaded_into_first_handler(self) -> dict[str, Any]:
        info = self.active_in_flight
        if not info:
            return {"status": "idle"}

        pkt: PcapPacket = info["packet"]
        current_batch_pkts: list[PcapPacket] = info.get("batch") or [pkt]
        first_handler = self.handlers[0] if self.handlers else None
        if not first_handler:
            return {"status": "error", "message": "No handlers defined"}

        in_ch = first_handler.get("input_channel")
        if not in_ch and first_handler.get("input_channels"):
            chs = first_handler["input_channels"]
            in_ch = chs[0] if isinstance(chs, list) and chs else str(chs).split(",")[0].strip()

        first_desc = load_handler_descriptor(first_handler.get("type", ""))

        # Drain any stale notifications before injecting
        self._drain_socket(self.events_sock)
        send_time_ns = time.time_ns()

        # Inject packet(s) using pluggable injector strategy
        injector = get_injector_for_handler(
            handler_spec=first_handler,
            is_external_ingress=first_desc.is_external_ingress,
            protocol=first_desc.protocol.value,
        )
        injector.inject(first_handler, current_batch_pkts)

        return self._process_handler_execution(
            handler_spec=first_handler,
            source_name="ingress",
            input_channel=info.get("input_channel") or in_ch or "ingress",
            pkt=pkt,
            batch_pkts=current_batch_pkts,
            send_time_ns=send_time_ns,
            pending_branches=info.get("pending_branches", []),
        )

    def _advance_in_flight(self) -> dict[str, Any]:
        """Releases the held message from the output channel into the next downstream handler."""
        info = self.active_in_flight
        if not info:
            return {"status": "idle"}

        pkt: PcapPacket = info["packet"]
        batch_pkts: list[PcapPacket] = info.get("batch") or [pkt]
        ch: str = info["output_channel"]
        held_data = info["held_data"]
        pending_branches: list[dict[str, Any]] = list(info.get("pending_branches") or [])

        consumers = self.channel_to_consumers.get(ch, [])
        if not consumers:
            # Output channel is an external chain egress (no downstream handler consumes it)
            ev_egress = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": time.time_ns(),
                "type": "egress",
                "result": "Forwarded",
                "handler": info["curr_node_name"],
                "handlerType": info["curr_node_type"],
                "inputChannel": info.get("input_channel"),
                "outputChannel": ch,
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "transmitted",
                "latencyUs": 10.0,
                "detail": f"Packet #{pkt.seq_id} transmitted out of chain egress channel '{ch}'",
            }
            self.event_counter += 1
            self.events.append(ev_egress)
            self._advance_or_clear_branches(pending_branches)
            return {"status": "ok", "event": ev_egress, "finishedPacket": pkt.seq_id}

        next_handler = consumers[0]
        next_in_sock = f"/tmp/proxy_sim_{ch}.sock"

        self._drain_socket(self.events_sock)
        send_time_ns = time.time_ns()

        # Deliver held envelope(s) into the next handler's input channel socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        try:
            if isinstance(held_data, list):
                for envelope in held_data:
                    s.sendto(envelope, next_in_sock)
            elif held_data:
                s.sendto(held_data, next_in_sock)
        except Exception as ex:
            print(f"[Coordinator] Error delivering held envelope to {next_in_sock}: {ex}")
        s.close()

        return self._process_handler_execution(
            handler_spec=next_handler,
            source_name=info["curr_node_name"],
            input_channel=ch,
            pkt=pkt,
            batch_pkts=batch_pkts,
            send_time_ns=send_time_ns,
            pending_branches=pending_branches,
        )

    def _process_handler_execution(
        self,
        handler_spec: dict[str, Any],
        source_name: str,
        input_channel: str,
        pkt: PcapPacket,
        batch_pkts: list[PcapPacket],
        send_time_ns: int,
        pending_branches: list[dict[str, Any]],
    ) -> dict[str, Any]:
        handler_name = handler_spec["name"]
        handler_type = handler_spec.get("type", "unknown")
        is_batch = len(batch_pkts) > 1
        seq_label = (
            f"Batch [{', '.join(str(p.seq_id) for p in batch_pkts)}]"
            if is_batch
            else f"#{pkt.seq_id}"
        )

        # Wait for SimBackend::onMessageResult event(s) from this handler
        results = self._wait_for_message_results(
            expected_handler=handler_name,
            expected_count=len(batch_pkts),
            timeout_sec=0.5,
        )
        elapsed_us = round(max(1.0, (time.time_ns() - send_time_ns) / 1000.0), 2)

        # Determine primary outcome from MessageResult
        forwarded_results = [r for r in results if r.get("result") == "Forwarded"]
        consumed_results = [r for r in results if r.get("result") == "Consumed"]
        dropped_results = [r for r in results if r.get("result") == "Dropped"]

        if forwarded_results:
            primary_res = forwarded_results[0]
            raw_out_ch = primary_res.get("outputChannel") or handler_spec.get("output_channel") or ""
            out_channels = [c.strip() for c in raw_out_ch.split(",") if c.strip()]
            if not out_channels and handler_spec.get("output_channels"):
                ocs = handler_spec["output_channels"]
                out_channels = list(ocs) if isinstance(ocs, list) else [c.strip() for c in str(ocs).split(",") if c.strip()]

            collected_branches: list[dict[str, Any]] = []
            for oc in out_channels:
                self._ensure_hold_socket(oc)
                held = self._poll_held_output(oc, timeout_sec=0.15)
                if held:
                    collected_branches.append({
                        "stage": "held",
                        "packet": pkt,
                        "batch": batch_pkts,
                        "curr_node_name": handler_name,
                        "curr_node_type": handler_type,
                        "input_channel": input_channel,
                        "output_channel": oc,
                        "held_data": held,
                    })

            # Fallback if hold socket had data on default output channel
            if not collected_branches and out_channels:
                oc = out_channels[0]
                held = self._poll_held_output(oc, timeout_sec=0.15)
                if held:
                    collected_branches.append({
                        "stage": "held",
                        "packet": pkt,
                        "batch": batch_pkts,
                        "curr_node_name": handler_name,
                        "curr_node_type": handler_type,
                        "input_channel": input_channel,
                        "output_channel": oc,
                        "held_data": held,
                    })

            if collected_branches:
                first_branch = collected_branches[0]
                extra_branches = collected_branches[1:] + pending_branches
                first_branch["pending_branches"] = extra_branches
                self.active_in_flight = first_branch

                out_ch = first_branch["output_channel"]
                held_data = first_branch["held_data"]
                out_batch_count = len(held_data) if isinstance(held_data, list) else 1
                all_out_str = ", ".join(b["output_channel"] for b in collected_branches)
                batch_note = f" (Batch of {out_batch_count} messages)" if out_batch_count > 1 else ""

                ev_hop = {
                    "eventId": self.event_counter,
                    "msgId": pkt.seq_id,
                    "batchIds": [p.seq_id for p in batch_pkts],
                    "batchCount": out_batch_count,
                    "timestampNs": primary_res.get("timestampNs", time.time_ns()),
                    "type": "hop",
                    "result": "Forwarded",
                    "handler": handler_name,
                    "handlerType": handler_type,
                    "inputChannel": input_channel,
                    "outputChannel": out_ch,
                    "payloadSummary": (
                        f"Batch of {out_batch_count} messages"
                        if out_batch_count > 1
                        else (primary_res.get("summary") or pkt.payload_text[:60])
                    ),
                    "payloadFull": (
                        "\n---\n".join(p.payload_text for p in batch_pkts)
                        if is_batch
                        else (primary_res.get("payload") or pkt.payload_text)
                    ),
                    "status": "forwarded",
                    "latencyUs": elapsed_us,
                    "detail": f"'{handler_name}' forwarded Packet {seq_label} to '{all_out_str}'{batch_note}",
                }
                self.event_counter += 1
                self.events.append(ev_hop)
                return {"status": "ok", "event": ev_hop, "heldChannel": out_ch}

        if consumed_results:
            primary_res = consumed_results[0]
            ev_consumed = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": primary_res.get("timestampNs", time.time_ns()),
                "type": "egress",
                "result": "Consumed",
                "handler": handler_name,
                "handlerType": handler_type,
                "inputChannel": input_channel,
                "payloadSummary": (
                    f"Batch of {len(batch_pkts)} messages"
                    if is_batch
                    else (primary_res.get("summary") or pkt.payload_text[:60])
                ),
                "payloadFull": (
                    "\n---\n".join(p.payload_text for p in batch_pkts)
                    if is_batch
                    else (primary_res.get("payload") or pkt.payload_text)
                ),
                "status": "consumed",
                "latencyUs": elapsed_us,
                "detail": f"'{handler_name}' consumed Packet {seq_label}",
            }
            self.event_counter += 1
            self.events.append(ev_consumed)
            self._advance_or_clear_branches(pending_branches)
            return {"status": "ok", "event": ev_consumed, "finishedPacket": pkt.seq_id}

        # Dropped (either explicit MessageResult::Dropped or handler discarded without forwarding)
        primary_res = dropped_results[0] if dropped_results else {}
        reason = primary_res.get("reason") or "Message rejected by handler"
        error_code = int(primary_res.get("errorCode") or 0)
        code_suffix = f" (code {error_code})" if error_code else ""

        ev_drop = {
            "eventId": self.event_counter,
            "msgId": pkt.seq_id,
            "batchIds": [p.seq_id for p in batch_pkts],
            "batchCount": len(batch_pkts),
            "timestampNs": primary_res.get("timestampNs", time.time_ns()),
            "type": "drop",
            "result": "Dropped",
            "reason": reason,
            "errorCode": error_code,
            "handler": handler_name,
            "handlerType": handler_type,
            "inputChannel": input_channel,
            "payloadSummary": (
                f"Batch of {len(batch_pkts)} messages"
                if is_batch
                else (primary_res.get("summary") or pkt.payload_text[:60])
            ),
            "payloadFull": (
                "\n---\n".join(p.payload_text for p in batch_pkts)
                if is_batch
                else (primary_res.get("payload") or pkt.payload_text)
            ),
            "status": "dropped",
            "latencyUs": elapsed_us,
            "detail": f"'{handler_name}' dropped Packet {seq_label}: {reason}{code_suffix}",
        }
        self.event_counter += 1
        self.events.append(ev_drop)
        self._advance_or_clear_branches(pending_branches)
        return {"status": "dropped", "event": ev_drop}

    def _advance_or_clear_branches(self, pending_branches: list[dict[str, Any]]):
        if pending_branches:
            nxt = pending_branches[0]
            nxt["pending_branches"] = pending_branches[1:]
            self.active_in_flight = nxt
        else:
            self.active_in_flight = None

    def _wait_for_message_results(
        self,
        expected_handler: str,
        expected_count: int = 1,
        timeout_sec: float = 0.5,
    ) -> list[dict[str, Any]]:
        """
        Polls /tmp/proxy_sim_events.sock for MessageResult JSON payloads emitted by SimBackend::onMessageResult.
        """
        if not self.events_sock:
            return []

        matched: list[dict[str, Any]] = []
        deadline = time.time() + timeout_sec

        while time.time() < deadline and len(matched) < expected_count:
            try:
                raw, _ = self.events_sock.recvfrom(65535)
                if raw:
                    ev = json.loads(raw.decode("utf-8", errors="replace"))
                    if ev.get("type") == "message_result" and ev.get("handler") == expected_handler:
                        matched.append(ev)
                        # Extend deadline slightly to drain remaining batch items if any
                        deadline = min(deadline, time.time() + 0.05)
                        continue
            except (BlockingIOError, socket.timeout):
                pass
            except Exception:
                pass
            time.sleep(0.002)

        return matched

    def _poll_held_output(self, channel_name: Optional[str], timeout_sec: float = 0.15) -> Optional[list[bytes]]:
        """
        Polls the hold socket for one or more batched messages emitted by the upstream handler.
        """
        if not channel_name:
            return None
        sock = self._ensure_hold_socket(channel_name)
        if not sock:
            return None
        start = time.time()
        batch: list[bytes] = []

        while time.time() - start < timeout_sec:
            try:
                data, _ = sock.recvfrom(65535)
                if data:
                    batch.append(data)
                    break
            except (BlockingIOError, socket.timeout):
                pass
            except Exception:
                break
            time.sleep(0.002)

        if not batch:
            return None

        drain_deadline = time.time() + 0.02
        while time.time() < drain_deadline:
            try:
                data, _ = sock.recvfrom(65535)
                if data:
                    batch.append(data)
            except (BlockingIOError, socket.timeout):
                break
            except Exception:
                break

        return batch

    def trigger_generator(self, node_name: str) -> dict[str, Any]:
        """
        Triggers a generator node on demand and holds the generated envelope at its output channel.
        """
        with self.lock:
            gen_handler = next((h for h in self.handlers if h.get("name") == node_name), None)
            if not gen_handler:
                return {"status": "error", "message": f"Handler node '{node_name}' not found."}

            gen_desc = load_handler_descriptor(gen_handler.get("type", ""))
            if not gen_desc.is_generator:
                return {
                    "status": "error",
                    "message": f"Handler '{node_name}' is not an autonomous generator (role: {gen_desc.role.value}).",
                }

            out_ch = gen_handler.get("output_channel")
            if not out_ch:
                return {"status": "error", "message": f"Handler '{node_name}' has no output_channel."}

            # Only drain the hold socket if this channel is not currently holding any queued messages
            is_channel_in_flight = (
                self.active_in_flight and (
                    self.active_in_flight.get("output_channel") == out_ch
                    or any(b.get("output_channel") == out_ch for b in self.active_in_flight.get("pending_branches", []))
                )
            )
            if out_ch in self.hold_sockets and not is_channel_in_flight:
                self._drain_socket(self.hold_sockets[out_ch])

            h_config = gen_handler.get("config", {})
            msg_text = h_config.get("dummy.message", f"Message from {node_name}")
            payload_bytes = msg_text.encode("utf-8")

            header = struct.pack(
                "<HBBHIQ",
                0x5058,  # magic 'PX'
                1,  # version
                0,  # protocol = Raw
                0,  # metadataLength = 0
                len(payload_bytes),
                time.time_ns(),
            )
            raw_envelope = header + payload_bytes

            new_seq_id = len(self.pcap_packets) + 1
            synthetic_pkt = PcapPacket(
                seq_id=new_seq_id,
                timestamp_ns=time.time_ns(),
                src_ip="127.0.0.1",
                src_port=0,
                dst_ip="127.0.0.1",
                dst_port=0,
                raw_payload=payload_bytes,
                payload_text=msg_text,
            )
            self.pcap_packets.append(synthetic_pkt)
            self.current_packet_idx = len(self.pcap_packets)

            is_accumulating = (
                self.active_in_flight is not None
                and self.active_in_flight.get("curr_node_name") == gen_handler["name"]
                and self.active_in_flight.get("output_channel") == out_ch
            )

            if is_accumulating:
                existing_held = self.active_in_flight["held_data"]
                held_batch = (existing_held if isinstance(existing_held, list) else [existing_held]) + [raw_envelope]
                existing_batch_pkts = self.active_in_flight.get("batch") or [self.active_in_flight["packet"]]
                all_batch_pkts = existing_batch_pkts + [synthetic_pkt]

                self.active_in_flight["held_data"] = held_batch
                self.active_in_flight["batch"] = all_batch_pkts
                self.active_in_flight["packet"] = all_batch_pkts[0]

                batch_ids = [p.seq_id for p in all_batch_pkts]
                self.batch_groups = [grp for grp in self.batch_groups if not any(pid in grp for pid in batch_ids)]
                self.batch_groups.append(batch_ids)

                batch_len = len(all_batch_pkts)
                batch_ids_str = ", ".join(str(p.seq_id) for p in all_batch_pkts)

                ev_gen = {
                    "eventId": self.event_counter,
                    "msgId": synthetic_pkt.seq_id,
                    "batchIds": batch_ids,
                    "batchCount": batch_len,
                    "timestampNs": time.time_ns(),
                    "type": "hop",
                    "result": "Forwarded",
                    "handler": gen_handler["name"],
                    "handlerType": gen_handler.get("type"),
                    "outputChannel": out_ch,
                    "payloadSummary": f"Batch of {batch_len} messages [Pkts {batch_ids_str}]",
                    "payloadFull": "\n---\n".join(p.payload_text for p in all_batch_pkts),
                    "status": "forwarded",
                    "latencyUs": 5.0,
                    "detail": f"Generator '{gen_handler['name']}' accumulated message #{synthetic_pkt.seq_id} -> held batch of {batch_len} at '{out_ch}'",
                }
            elif self.active_in_flight is not None:
                # Another message is already in-flight or held elsewhere; queue this generated message as a pending branch
                new_branch = {
                    "stage": "held",
                    "packet": synthetic_pkt,
                    "batch": [synthetic_pkt],
                    "curr_node_name": gen_handler["name"],
                    "curr_node_type": gen_handler.get("type"),
                    "input_channel": None,
                    "output_channel": out_ch,
                    "held_data": [raw_envelope],
                    "pending_branches": [],
                }
                if "pending_branches" not in self.active_in_flight:
                    self.active_in_flight["pending_branches"] = []
                self.active_in_flight["pending_branches"].append(new_branch)

                ev_gen = {
                    "eventId": self.event_counter,
                    "msgId": synthetic_pkt.seq_id,
                    "batchIds": [synthetic_pkt.seq_id],
                    "batchCount": 1,
                    "timestampNs": time.time_ns(),
                    "type": "hop",
                    "result": "Forwarded",
                    "handler": gen_handler["name"],
                    "handlerType": gen_handler.get("type"),
                    "outputChannel": out_ch,
                    "payloadSummary": msg_text[:60] + "..." if len(msg_text) > 60 else msg_text,
                    "payloadFull": msg_text,
                    "status": "forwarded",
                    "latencyUs": 5.0,
                    "detail": f"Generator '{gen_handler['name']}' triggered -> queued at output channel '{out_ch}'",
                }
            else:
                self.active_in_flight = {
                    "stage": "held",
                    "packet": synthetic_pkt,
                    "batch": [synthetic_pkt],
                    "curr_node_name": gen_handler["name"],
                    "curr_node_type": gen_handler.get("type"),
                    "input_channel": None,
                    "output_channel": out_ch,
                    "held_data": [raw_envelope],
                    "pending_branches": [],
                }

                ev_gen = {
                    "eventId": self.event_counter,
                    "msgId": synthetic_pkt.seq_id,
                    "batchIds": [synthetic_pkt.seq_id],
                    "batchCount": 1,
                    "timestampNs": time.time_ns(),
                    "type": "hop",
                    "result": "Forwarded",
                    "handler": gen_handler["name"],
                    "handlerType": gen_handler.get("type"),
                    "outputChannel": out_ch,
                    "payloadSummary": msg_text[:60] + "..." if len(msg_text) > 60 else msg_text,
                    "payloadFull": msg_text,
                    "status": "forwarded",
                    "latencyUs": 5.0,
                    "detail": f"Generator '{gen_handler['name']}' triggered -> held at output channel '{out_ch}'",
                }

            self.event_counter += 1
            self.events.append(ev_gen)

            return {
                "status": "ok",
                "event": ev_gen,
                "packet": synthetic_pkt.to_dict(),
                "heldChannel": out_ch,
                "inFlight": True,
            }

    def reset(self):
        with self.lock:
            self.current_packet_idx = 0
            self.events.clear()
            self.event_counter = 1
            self.active_in_flight = None
            self._drain_all_sockets()

    def run_to_completion(self, max_steps: int = 10000) -> list[dict[str, Any]]:
        """
        Fast-forward headless loop: loads each packet and steps it through the pipeline
        until all packets have reached Consumed, Egress, or Dropped.
        """
        steps = 0
        while steps < max_steps:
            res = self.step_forward(auto_load=True)
            steps += 1
            if res.get("status") == "finished":
                break
        return list(self.events)
