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
        self._initial_pcap_packets = list(pcap_packets)
        self.pcap_packets = list(pcap_packets)
        self.current_packet_idx = 0
        self.events: list[dict[str, Any]] = []
        self.event_counter = 1
        self.lock = threading.Lock()
        self.pending_message_results: list[dict[str, Any]] = []

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
        self.pending_queue: list[dict[str, Any]] = []

        # Custom batch groupings: list of lists of packet IDs
        self.batch_groups: list[list[int]] = []
        self.last_triggered_generator: Optional[str] = None

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
                if "rules" in h_cfg and isinstance(h_cfg["rules"], list):
                    for r in h_cfg["rules"]:
                        if isinstance(r, dict) and r.get("target") and str(r["target"]).strip():
                            channels.add(str(r["target"]).strip())
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
                "traces": [
                    {
                        "traceId": 1,
                        "msgId": pkt.seq_id,
                        "channel": ingress_channel_label,
                        "inputChannel": ingress_channel_label,
                        "result": "Loaded",
                        "status": "loaded",
                        "source": f"{pkt.src_ip}:{pkt.src_port}",
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
                        "detail": f"Staged at ingress queue '{ingress_channel_label}'",
                    }
                ],
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
        If no packet is currently in-flight:
          - First checks if any branches are held in self.pending_queue, and promotes the next one.
          - Otherwise, if auto_load is True, loads the next PCAP packet.
        """
        with self.lock:
            if self.active_in_flight is None:
                if self.pending_queue:
                    nxt = self.pending_queue.pop(0)
                    self.active_in_flight = nxt
                    stage = self.active_in_flight.get("stage")
                    if stage == "loaded":
                        return self._step_loaded_into_first_handler()
                    else:
                        return self._advance_in_flight()

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
            expected_msg_timestamps=(
                None
                if first_desc.is_external_ingress
                else {packet.timestamp_ns for packet in current_batch_pkts}
            ),
        )

    def _advance_in_flight(self) -> dict[str, Any]:
        """Releases the held message from the output channel into the next downstream handler."""
        info = self.active_in_flight
        if not info:
            return {"status": "idle"}

        pkt: PcapPacket = info["packet"]
        batch_pkts: list[PcapPacket] = info.get("batch") or [pkt]
        pending_branches: list[dict[str, Any]] = list(info.get("pending_branches") or [])

        # If there are pending fan-out branches created simultaneously from the same upstream node,
        # advance all of them concurrently in this step!
        all_parallel_branches: list[dict[str, Any]] = [info] + pending_branches
        remaining_pending: list[dict[str, Any]] = []

        # Separate branches into those with downstream consumers and those that are chain egress
        branches_to_deliver: list[tuple[dict[str, Any], dict[str, Any], str]] = [] # (branch, consumer_handler, in_sock)
        egress_traces: list[dict[str, Any]] = []

        for b in all_parallel_branches:
            b_ch = b["output_channel"]
            consumers = self.channel_to_consumers.get(b_ch, [])
            if consumers:
                branches_to_deliver.append((b, consumers[0], f"/tmp/proxy_sim_{b_ch}.sock"))
            else:
                egress_traces.append({
                    "traceId": len(egress_traces) + 1,
                    "msgId": pkt.seq_id,
                    "channel": b_ch,
                    "outputChannel": b_ch,
                    "result": "Forwarded",
                    "status": "transmitted",
                    "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                    "payloadFull": pkt.payload_text,
                    "detail": f"Transmitted out of chain egress channel '{b_ch}'",
                })

        if not branches_to_deliver:
            # All parallel branches were external egress channels
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
                "outputChannel": ", ".join(b["output_channel"] for b in all_parallel_branches),
                "payloadSummary": pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text,
                "payloadFull": pkt.payload_text,
                "status": "transmitted",
                "latencyUs": 10.0,
                "detail": f"Packet #{pkt.seq_id} transmitted out of chain egress channel(s)",
                "traces": egress_traces,
            }
            self.event_counter += 1
            self.events.append(ev_egress)
            self._advance_or_clear_branches(remaining_pending)
            return {"status": "ok", "event": ev_egress, "finishedPacket": pkt.seq_id}

        send_time_ns = time.time_ns()

        # Deliver held envelope(s) to all downstream consumer sockets in parallel
        s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        for b, _, next_in_sock in branches_to_deliver:
            held_data = b["held_data"]
            try:
                if isinstance(held_data, list):
                    for envelope in held_data:
                        s.sendto(envelope, next_in_sock)
                elif held_data:
                    s.sendto(held_data, next_in_sock)
            except Exception as ex:
                print(f"[Coordinator] Error delivering held envelope to {next_in_sock}: {ex}")
        s.close()

        # Wait for MessageResult from all target consumer handlers
        expected_handler_names = {consumer["name"] for _, consumer, _ in branches_to_deliver}
        results = self._wait_for_message_results(
            expected_handler=expected_handler_names,
            expected_count=len(branches_to_deliver) * len(batch_pkts),
            timeout_sec=0.5,
            expected_msg_timestamps={packet.timestamp_ns for packet in batch_pkts},
        )
        elapsed_us = round(max(1.0, (time.time_ns() - send_time_ns) / 1000.0), 2)

        # Group results by consumer handler
        results_by_handler: dict[str, list[dict[str, Any]]] = {}
        for r in results:
            h_name = r.get("handler", "")
            results_by_handler.setdefault(h_name, []).append(r)

        # Evaluate each branch outcome and collect traces
        combined_traces: list[dict[str, Any]] = list(egress_traces)
        next_held_branches: list[dict[str, Any]] = []
        is_batch = len(batch_pkts) > 1
        seq_label = f"Batch [{', '.join(str(p.seq_id) for p in batch_pkts)}]" if is_batch else f"#{pkt.seq_id}"

        for b, consumer, _ in branches_to_deliver:
            c_name = consumer["name"]
            c_type = consumer.get("type", "unknown")
            in_ch = b["output_channel"]
            c_results = results_by_handler.get(c_name, [])

            raw_traces: list[dict[str, Any]] = []
            for r in c_results:
                if "traces" in r and isinstance(r["traces"], list):
                    raw_traces.extend(r["traces"])
                elif "result" in r:
                    raw_traces.append(r)

            forwarded_trs = [t for t in raw_traces if t.get("result") == "Forwarded"]
            consumed_trs = [t for t in raw_traces if t.get("result") == "Consumed"]
            dropped_trs = [t for t in raw_traces if t.get("result") == "Dropped"]
            recovered_held: dict[str, list[bytes]] = {}
            if not raw_traces:
                for out_channel in self._handler_output_channels(consumer):
                    held = self._poll_held_output(out_channel, timeout_sec=0.15)
                    if held:
                        recovered_held[out_channel] = held
                        forwarded_trs.append({"outputChannel": out_channel, "result": "Forwarded"})

            if forwarded_trs:
                out_channels: list[str] = []
                for t in forwarded_trs:
                    raw_oc = t.get("outputChannel") or ""
                    for part in raw_oc.split(","):
                        c = part.strip()
                        if c and c not in out_channels:
                            out_channels.append(c)

                if not out_channels and consumer.get("output_channel"):
                    out_channels = [c.strip() for c in str(consumer["output_channel"]).split(",") if c.strip()]
                if not out_channels and consumer.get("output_channels"):
                    ocs = consumer["output_channels"]
                    out_channels = list(ocs) if isinstance(ocs, list) else [c.strip() for c in str(ocs).split(",") if c.strip()]

                for oc in out_channels:
                    self._ensure_hold_socket(oc)
                    held = recovered_held.pop(oc, None) or self._poll_held_output(oc, timeout_sec=0.15)
                    if held:
                        next_held_branches.append({
                            "stage": "held",
                            "packet": pkt,
                            "batch": batch_pkts,
                            "curr_node_name": c_name,
                            "curr_node_type": c_type,
                            "input_channel": in_ch,
                            "output_channel": oc,
                            "held_data": held,
                        })
                    matched_tr = next((t for t in forwarded_trs if t.get("outputChannel") == oc), forwarded_trs[0])
                    combined_traces.append({
                        "traceId": len(combined_traces) + 1,
                        "msgId": pkt.seq_id,
                        "handler": c_name,
                        "handlerType": c_type,
                        "inputChannel": in_ch,
                        "outputChannel": oc,
                        "channel": oc,
                        "result": "Forwarded",
                        "status": "forwarded",
                        "payloadSummary": matched_tr.get("summary") or pkt.payload_text[:60],
                        "payloadFull": matched_tr.get("payload") or pkt.payload_text,
                        "detail": f"'{c_name}' forwarded to '{oc}'",
                    })
            elif consumed_trs:
                primary_tr = consumed_trs[0]
                summary = f"Batch of {len(batch_pkts)} messages" if is_batch else (primary_tr.get("summary") or pkt.payload_text[:60])
                full = "\n---\n".join(p.payload_text for p in batch_pkts) if is_batch else (primary_tr.get("payload") or pkt.payload_text)
                combined_traces.append({
                    "traceId": len(combined_traces) + 1,
                    "msgId": pkt.seq_id,
                    "handler": c_name,
                    "handlerType": c_type,
                    "inputChannel": in_ch,
                    "channel": in_ch,
                    "result": "Consumed",
                    "status": "consumed",
                    "payloadSummary": summary,
                    "payloadFull": full,
                    "detail": f"Consumed by '{c_name}'",
                })
            else:
                if not dropped_trs:
                    summary = f"Batch of {len(batch_pkts)} messages" if is_batch else pkt.payload_text[:60]
                    full = "\n---\n".join(p.payload_text for p in batch_pkts) if is_batch else pkt.payload_text
                    combined_traces.append({
                        "traceId": len(combined_traces) + 1,
                        "msgId": pkt.seq_id,
                        "handler": c_name,
                        "handlerType": c_type,
                        "inputChannel": in_ch,
                        "channel": in_ch,
                        "result": "Unknown",
                        "status": "unconfirmed",
                        "payloadSummary": summary,
                        "payloadFull": full,
                        "detail": f"No message result received from '{c_name}'",
                    })
                    continue
                primary_tr = dropped_traces[0]
                reason = primary_tr.get("reason") or "Message rejected by handler"
                error_code = int(primary_tr.get("errorCode") or 0)
                code_suffix = f" (code {error_code})" if error_code else ""
                summary = f"Batch of {len(batch_pkts)} messages" if is_batch else (primary_tr.get("summary") or pkt.payload_text[:60])
                full = "\n---\n".join(p.payload_text for p in batch_pkts) if is_batch else (primary_tr.get("payload") or pkt.payload_text)
                combined_traces.append({
                    "traceId": len(combined_traces) + 1,
                    "msgId": pkt.seq_id,
                    "handler": c_name,
                    "handlerType": c_type,
                    "inputChannel": in_ch,
                    "channel": in_ch,
                    "result": "Dropped",
                    "status": "dropped",
                    "reason": reason,
                    "errorCode": error_code,
                    "payloadSummary": summary,
                    "payloadFull": full,
                    "detail": f"Dropped by '{c_name}': {reason}{code_suffix}",
                })

        # Re-number traceIds 1..N
        for idx, tr in enumerate(combined_traces):
            tr["traceId"] = idx + 1

        all_handler_names = [consumer["name"] for _, consumer, _ in branches_to_deliver]
        primary_handler = ", ".join(all_handler_names) if len(all_handler_names) > 1 else (all_handler_names[0] if all_handler_names else info["curr_node_name"])
        primary_handler_type = branches_to_deliver[0][1].get("type", "unknown") if branches_to_deliver else info["curr_node_type"]

        has_forwarded = any(t.get("result") == "Forwarded" for t in combined_traces)
        has_dropped = any(t.get("result") == "Dropped" for t in combined_traces)
        all_consumed = all(t.get("result") == "Consumed" for t in combined_traces)
        has_unconfirmed = any(t.get("result") == "Unknown" for t in combined_traces)

        if has_forwarded:
            event_type = "hop"
            overall_result = "Forwarded"
            overall_status = "forwarded"
        elif all_consumed:
            event_type = "egress"
            overall_result = "Consumed"
            overall_status = "consumed"
        elif has_dropped:
            event_type = "drop"
            overall_result = "Dropped"
            overall_status = "dropped"
        elif has_unconfirmed:
            event_type = "unconfirmed"
            overall_result = "Unknown"
            overall_status = "unconfirmed"
        else:
            event_type = "egress"
            overall_result = "Consumed"
            overall_status = "consumed"

        ev_combined = {
            "eventId": self.event_counter,
            "msgId": pkt.seq_id,
            "batchIds": [p.seq_id for p in batch_pkts],
            "batchCount": len(batch_pkts),
            "timestampNs": results[0].get("timestampNs", time.time_ns()) if results else time.time_ns(),
            "type": event_type,
            "result": overall_result,
            "handler": primary_handler,
            "handlerType": primary_handler_type,
            "inputChannel": ", ".join(b["output_channel"] for b, _, _ in branches_to_deliver),
            "outputChannel": (
                ", ".join(b["output_channel"] for b in next_held_branches)
                if next_held_branches
                else None
            ),
            "payloadSummary": combined_traces[0].get("payloadSummary") if combined_traces else pkt.payload_text[:60],
            "payloadFull": combined_traces[0].get("payloadFull") if combined_traces else pkt.payload_text,
            "status": overall_status,
            "latencyUs": elapsed_us,
            "detail": f"{primary_handler} {overall_status.lower()} Packet {seq_label}",
            "traces": combined_traces,
        }
        self.event_counter += 1
        self.events.append(ev_combined)

        if next_held_branches:
            first_held = next_held_branches[0]
            first_held["pending_branches"] = next_held_branches[1:] + remaining_pending
            self.active_in_flight = first_held
            return {"status": "ok", "event": ev_combined, "heldChannel": first_held["output_channel"]}
        else:
            self._advance_or_clear_branches(remaining_pending)
            return {"status": "ok", "event": ev_combined, "finishedPacket": pkt.seq_id}

    def _process_handler_execution(
        self,
        handler_spec: dict[str, Any],
        source_name: str,
        input_channel: str,
        pkt: PcapPacket,
        batch_pkts: list[PcapPacket],
        send_time_ns: int,
        pending_branches: list[dict[str, Any]],
        expected_msg_timestamps: Optional[set[int]] = None,
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
            expected_msg_timestamps=expected_msg_timestamps,
        )
        elapsed_us = round(max(1.0, (time.time_ns() - send_time_ns) / 1000.0), 2)

        # Determine primary outcome from MessageResult
        # Each item in results can have a 'traces' list or flat result fields
        raw_traces: list[dict[str, Any]] = []
        for r in results:
            if "traces" in r and isinstance(r["traces"], list):
                raw_traces.extend(r["traces"])
            elif "result" in r:
                raw_traces.append(r)

        forwarded_traces = [t for t in raw_traces if t.get("result") == "Forwarded"]
        consumed_traces = [t for t in raw_traces if t.get("result") == "Consumed"]
        dropped_traces = [t for t in raw_traces if t.get("result") == "Dropped"]
        recovered_held: dict[str, list[bytes]] = {}
        if not raw_traces:
            for out_channel in self._handler_output_channels(handler_spec):
                held = self._poll_held_output(out_channel, timeout_sec=0.15)
                if held:
                    recovered_held[out_channel] = held
                    forwarded_traces.append({"outputChannel": out_channel, "result": "Forwarded"})

        if forwarded_traces:
            # Extract target output channels across all forwarded traces
            out_channels: list[str] = []
            for t in forwarded_traces:
                raw_oc = t.get("outputChannel") or ""
                for part in raw_oc.split(","):
                    c = part.strip()
                    if c and c not in out_channels:
                        out_channels.append(c)

            if not out_channels:
                out_channels = self._handler_output_channels(handler_spec)

            collected_branches: list[dict[str, Any]] = []
            for oc in out_channels:
                self._ensure_hold_socket(oc)
                held = recovered_held.pop(oc, None) or self._poll_held_output(oc, timeout_sec=0.15)
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
                held = recovered_held.pop(oc, None) or self._poll_held_output(oc, timeout_sec=0.15)
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

                # Construct discrete trace objects for each collected branch
                event_traces: list[dict[str, Any]] = []
                for idx, branch in enumerate(collected_branches):
                    b_oc = branch["output_channel"]
                    matched_tr = next((t for t in forwarded_traces if t.get("outputChannel") == b_oc), forwarded_traces[0])
                    tr_obj = {
                        "traceId": idx + 1,
                        "msgId": pkt.seq_id,
                        "channel": b_oc,
                        "outputChannel": b_oc,
                        "result": "Forwarded",
                        "status": "forwarded",
                        "payloadSummary": matched_tr.get("summary") or pkt.payload_text[:60],
                        "payloadFull": matched_tr.get("payload") or pkt.payload_text,
                        "detail": f"Forwarded to '{b_oc}'",
                    }
                    event_traces.append(tr_obj)

                ev_hop = {
                    "eventId": self.event_counter,
                    "msgId": pkt.seq_id,
                    "batchIds": [p.seq_id for p in batch_pkts],
                    "batchCount": out_batch_count,
                    "timestampNs": results[0].get("timestampNs", time.time_ns()) if results else time.time_ns(),
                    "type": "hop",
                    "result": "Forwarded",
                    "handler": handler_name,
                    "handlerType": handler_type,
                    "inputChannel": input_channel,
                    "outputChannel": out_ch,
                    "payloadSummary": (
                        f"Batch of {out_batch_count} messages"
                        if out_batch_count > 1
                        else (event_traces[0].get("payloadSummary") or pkt.payload_text[:60])
                    ),
                    "payloadFull": (
                        "\n---\n".join(p.payload_text for p in batch_pkts)
                        if is_batch
                        else (event_traces[0].get("payloadFull") or pkt.payload_text)
                    ),
                    "status": "forwarded",
                    "latencyUs": elapsed_us,
                    "detail": f"'{handler_name}' forwarded Packet {seq_label} to '{all_out_str}'{batch_note}",
                    "traces": event_traces,
                }
                self.event_counter += 1
                self.events.append(ev_hop)
                return {"status": "ok", "event": ev_hop, "heldChannel": out_ch}

        if consumed_traces:
            primary_tr = consumed_traces[0]
            consumed_summary = (
                f"Batch of {len(batch_pkts)} messages"
                if is_batch
                else (primary_tr.get("summary") or pkt.payload_text[:60])
            )
            consumed_full = (
                "\n---\n".join(p.payload_text for p in batch_pkts)
                if is_batch
                else (primary_tr.get("payload") or pkt.payload_text)
            )
            ev_consumed = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": results[0].get("timestampNs", time.time_ns()) if results else time.time_ns(),
                "type": "egress",
                "result": "Consumed",
                "handler": handler_name,
                "handlerType": handler_type,
                "inputChannel": input_channel,
                "payloadSummary": consumed_summary,
                "payloadFull": consumed_full,
                "status": "consumed",
                "latencyUs": elapsed_us,
                "detail": f"'{handler_name}' consumed Packet {seq_label}",
                "traces": [
                    {
                        "traceId": 1,
                        "msgId": pkt.seq_id,
                        "channel": input_channel,
                        "inputChannel": input_channel,
                        "result": "Consumed",
                        "status": "consumed",
                        "payloadSummary": consumed_summary,
                        "payloadFull": consumed_full,
                        "detail": f"Consumed by '{handler_name}'",
                    }
                ],
            }
            self.event_counter += 1
            self.events.append(ev_consumed)
            self._advance_or_clear_branches(pending_branches)
            return {"status": "ok", "event": ev_consumed, "finishedPacket": pkt.seq_id}

        if not dropped_traces:
            ev_unconfirmed = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": time.time_ns(),
                "type": "unconfirmed",
                "result": "Unknown",
                "handler": handler_name,
                "handlerType": handler_type,
                "inputChannel": input_channel,
                "payloadSummary": f"Batch of {len(batch_pkts)} messages" if is_batch else pkt.payload_text[:60],
                "payloadFull": "\n---\n".join(p.payload_text for p in batch_pkts) if is_batch else pkt.payload_text,
                "status": "unconfirmed",
                "latencyUs": elapsed_us,
                "detail": f"No message result received from '{handler_name}'",
                "traces": [{
                    "traceId": 1,
                    "msgId": pkt.seq_id,
                    "handler": handler_name,
                    "handlerType": handler_type,
                    "channel": input_channel,
                    "inputChannel": input_channel,
                    "result": "Unknown",
                    "status": "unconfirmed",
                    "detail": f"No message result received from '{handler_name}'",
                }],
            }
            self.event_counter += 1
            self.events.append(ev_unconfirmed)
            self._advance_or_clear_branches(pending_branches)
            return {"status": "ok", "event": ev_unconfirmed, "finishedPacket": pkt.seq_id}

        # Explicit MessageResult::Dropped
        primary_tr = dropped_traces[0] if dropped_traces else {}
        reason = primary_tr.get("reason") or "Message rejected by handler"
        error_code = int(primary_tr.get("errorCode") or 0)
        code_suffix = f" (code {error_code})" if error_code else ""
        drop_summary = (
            f"Batch of {len(batch_pkts)} messages"
            if is_batch
            else (primary_tr.get("summary") or pkt.payload_text[:60])
        )
        drop_full = (
            "\n---\n".join(p.payload_text for p in batch_pkts)
            if is_batch
            else (primary_tr.get("payload") or pkt.payload_text)
        )

        ev_drop = {
            "eventId": self.event_counter,
            "msgId": pkt.seq_id,
            "batchIds": [p.seq_id for p in batch_pkts],
            "batchCount": len(batch_pkts),
            "timestampNs": results[0].get("timestampNs", time.time_ns()) if results else time.time_ns(),
            "type": "drop",
            "result": "Dropped",
            "reason": reason,
            "errorCode": error_code,
            "handler": handler_name,
            "handlerType": handler_type,
            "inputChannel": input_channel,
            "payloadSummary": drop_summary,
            "payloadFull": drop_full,
            "status": "dropped",
            "latencyUs": elapsed_us,
            "detail": f"'{handler_name}' dropped Packet {seq_label}: {reason}{code_suffix}",
            "traces": [
                {
                    "traceId": 1,
                    "msgId": pkt.seq_id,
                    "channel": input_channel,
                    "inputChannel": input_channel,
                    "result": "Dropped",
                    "status": "dropped",
                    "reason": reason,
                    "errorCode": error_code,
                    "payloadSummary": drop_summary,
                    "payloadFull": drop_full,
                    "detail": f"Dropped by '{handler_name}': {reason}{code_suffix}",
                }
            ],
        }
        self.event_counter += 1
        self.events.append(ev_drop)
        self._advance_or_clear_branches(pending_branches)
        return {"status": "dropped", "event": ev_drop}

    def _advance_or_clear_branches(self, pending_branches: list[dict[str, Any]]):
        if pending_branches:
            self.pending_queue = list(pending_branches) + self.pending_queue
        self.active_in_flight = None

    def _wait_for_message_results(
        self,
        expected_handler: str | set[str] | list[str],
        expected_count: int = 1,
        timeout_sec: float = 0.5,
        expected_msg_timestamps: Optional[set[int]] = None,
    ) -> list[dict[str, Any]]:
        """
        Polls /tmp/proxy_sim_events.sock for MessageResult JSON payloads emitted by SimBackend::onMessageResult.
        Can accept a single handler name or a collection/set of expected handler names.
        """
        expected_handlers: set[str] = (
            set(expected_handler)
            if isinstance(expected_handler, (set, list))
            else {expected_handler}
        )
        matched: list[dict[str, Any]] = []
        known_handlers = {h.get("name") for h in self.handlers}

        def matches(event: dict[str, Any]) -> bool:
            return (
                event.get("type") == "message_result"
                and event.get("handler") in expected_handlers
                and (
                    expected_msg_timestamps is None
                    or event.get("msgTimestampNs") in expected_msg_timestamps
                )
            )

        remaining: list[dict[str, Any]] = []
        for event in self.pending_message_results:
            if len(matched) < expected_count and matches(event):
                matched.append(event)
            else:
                remaining.append(event)
        self.pending_message_results = remaining
        if not self.events_sock or len(matched) >= expected_count:
            return matched

        deadline = time.time() + timeout_sec

        while time.time() < deadline and len(matched) < expected_count:
            try:
                raw, _ = self.events_sock.recvfrom(65535)
                if raw:
                    ev = json.loads(raw.decode("utf-8", errors="replace"))
                    if matches(ev):
                        matched.append(ev)
                        # Extend deadline slightly to drain remaining batch items if any
                        deadline = min(deadline, time.time() + 0.05)
                        continue
                    if ev.get("type") == "message_result" and ev.get("handler") in known_handlers:
                        self.pending_message_results.append(ev)
            except (BlockingIOError, socket.timeout):
                pass
            except Exception:
                pass
            time.sleep(0.002)

        return matched

    @staticmethod
    def _handler_output_channels(handler: dict[str, Any]) -> list[str]:
        configured = handler.get("output_channels") or handler.get("output_channel") or []
        if isinstance(configured, str):
            return [channel.strip() for channel in configured.split(",") if channel.strip()]
        return [str(channel).strip() for channel in configured if str(channel).strip()]

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

    def trigger_generator(self, node_name: str, custom_payload: Optional[str] = None) -> dict[str, Any]:
        """
        Triggers a generator node on demand and holds the generated envelope at its output channel.
        If custom_payload is provided, it is used instead of the handler's statically configured dummy.message.
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
            if custom_payload is not None:
                msg_text = str(custom_payload)
            else:
                msg_text = h_config.get("dummy.message", f"Message from {node_name}")
            payload_bytes = msg_text.encode("utf-8")
            timestamp_ns = time.time_ns()

            header = struct.pack(
                "!HBBHIQ",
                0x5058,  # magic 'PX' (big-endian 0x5058)
                1,  # version
                0,  # protocol = Raw
                0,  # metadataLength = 0
                len(payload_bytes),
                timestamp_ns,
            )
            raw_envelope = header + payload_bytes

            new_seq_id = len(self.pcap_packets) + 1
            synthetic_pkt = PcapPacket(
                seq_id=new_seq_id,
                timestamp_ns=timestamp_ns,
                src_ip="127.0.0.1",
                src_port=0,
                dst_ip="127.0.0.1",
                dst_port=0,
                raw_payload=payload_bytes,
                payload_text=msg_text,
            )
            self.pcap_packets.append(synthetic_pkt)
            self.current_packet_idx = len(self.pcap_packets)

            # Check if we should accumulate into active_in_flight or the latest queued branch
            target_branch = None
            if self.last_triggered_generator == gen_handler["name"]:
                if self.pending_queue and self.pending_queue[-1].get("curr_node_name") == gen_handler["name"] and self.pending_queue[-1].get("output_channel") == out_ch:
                    target_branch = self.pending_queue[-1]
                elif self.active_in_flight and self.active_in_flight.get("curr_node_name") == gen_handler["name"] and self.active_in_flight.get("output_channel") == out_ch:
                    target_branch = self.active_in_flight

            self.last_triggered_generator = gen_handler["name"]

            is_queued = False
            if target_branch is not None:
                existing_held = target_branch["held_data"]
                held_batch = (existing_held if isinstance(existing_held, list) else [existing_held]) + [raw_envelope]
                existing_batch_pkts = target_branch.get("batch") or [target_branch["packet"]]
                all_batch_pkts = existing_batch_pkts + [synthetic_pkt]

                target_branch["held_data"] = held_batch
                target_branch["batch"] = all_batch_pkts
                target_branch["packet"] = all_batch_pkts[0]

                batch_ids = [p.seq_id for p in all_batch_pkts]
                self.batch_groups = [grp for grp in self.batch_groups if not any(pid in grp for pid in batch_ids)]
                self.batch_groups.append(batch_ids)

                batch_len = len(all_batch_pkts)
                batch_ids_str = ", ".join(str(p.seq_id) for p in all_batch_pkts)

                is_queued = (target_branch is not self.active_in_flight)
                action_word = "queued batch of" if is_queued else "held batch of"

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
                    "detail": f"Generator '{gen_handler['name']}' accumulated message #{synthetic_pkt.seq_id} -> {action_word} {batch_len} at '{out_ch}'",
                }
            elif self.active_in_flight is not None:
                # Another message is already in-flight or held elsewhere; queue this generated message into pending_queue
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
                self.pending_queue.append(new_branch)
                is_queued = True

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
                    "traces": [
                        {
                            "traceId": 1,
                            "msgId": synthetic_pkt.seq_id,
                            "channel": out_ch,
                            "outputChannel": out_ch,
                            "result": "Forwarded",
                            "status": "forwarded",
                            "payloadSummary": msg_text[:60] + "..." if len(msg_text) > 60 else msg_text,
                            "payloadFull": msg_text,
                            "detail": f"Triggered to '{out_ch}'",
                        }
                    ],
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
                    "traces": [
                        {
                            "traceId": 1,
                            "msgId": synthetic_pkt.seq_id,
                            "channel": out_ch,
                            "outputChannel": out_ch,
                            "result": "Forwarded",
                            "status": "forwarded",
                            "payloadSummary": msg_text[:60] + "..." if len(msg_text) > 60 else msg_text,
                            "payloadFull": msg_text,
                            "detail": f"Triggered to '{out_ch}'",
                        }
                    ],
                }

            self.event_counter += 1
            self.events.append(ev_gen)

            return {
                "status": "ok",
                "event": ev_gen,
                "packet": synthetic_pkt.to_dict(),
                "heldChannel": out_ch,
                "inFlight": not is_queued,
                "isQueued": is_queued,
            }

    def reset(self):
        with self.lock:
            self.pcap_packets = list(self._initial_pcap_packets)
            self.current_packet_idx = 0
            self.events.clear()
            self.event_counter = 1
            self.active_in_flight = None
            self.pending_queue.clear()
            self.pending_message_results.clear()
            self.last_triggered_generator = None
            self.batch_groups.clear()
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
