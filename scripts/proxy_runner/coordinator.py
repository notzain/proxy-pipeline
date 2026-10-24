"""
Simulation coordinator managing step-by-step pipeline execution, socket interception,
and drop detection across real C++ handlers.
"""

import os
from pathlib import Path
import socket
import struct
import threading
import time
from typing import Any, Optional

from .pcap import PcapPacket


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
        # None if waiting for next packet to start, or { "packet": PcapPacket, "curr_node_idx": int, "channel": str, "held_data": bytes/list[bytes] }
        self.active_in_flight: Optional[dict[str, Any]] = None

        # Custom batch groupings: list of lists of packet IDs (e.g. [[1, 2], [3], [4, 5, 6]])
        self.batch_groups: list[list[int]] = []

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
            raw_held = self.active_in_flight["held_data"]
            if isinstance(raw_held, list):
                held_copy = [bytes(b) for b in raw_held]
            elif isinstance(raw_held, (bytes, bytearray)):
                held_copy = bytes(raw_held)
            else:
                held_copy = raw_held

            batch_copy = None
            if self.active_in_flight.get("batch"):
                batch_copy = list(self.active_in_flight["batch"])

            in_flight_copy = {
                "packet": self.active_in_flight["packet"],
                "batch": batch_copy,
                "curr_node_name": self.active_in_flight["curr_node_name"],
                "curr_node_type": self.active_in_flight["curr_node_type"],
                "output_channel": self.active_in_flight["output_channel"],
                "held_data": held_copy,
            }
        return {
            "current_packet_idx": self.current_packet_idx,
            "active_in_flight": in_flight_copy,
            "batch_groups": [list(g) for g in self.batch_groups],
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
        
        # Check if the current packet is part of a user-defined batch group
        current_batch_pkts: list[PcapPacket] = [pkt]
        for group in self.batch_groups:
            if pkt.seq_id in group:
                id_to_pkt = {p.seq_id: p for p in self.pcap_packets}
                matched = [id_to_pkt[pid] for pid in group if pid in id_to_pkt]
                if matched:
                    current_batch_pkts = matched
                break

        # Advance current_packet_idx past the batch
        self.current_packet_idx += len(current_batch_pkts)

        first_handler = self.handlers[0] if self.handlers else None
        if not first_handler:
            return {"status": "error", "message": "No handlers defined"}

        # If the first handler is a generator (has no input channel), don't attempt to inject PCAP packets into it
        is_generator = not first_handler.get("input_channel") and not first_handler.get("input_channels")
        if is_generator and first_handler.get("type") != "udp_ingress_handler":
            return {
                "status": "error",
                "message": f"Handler '{first_handler['name']}' is a generator with no input channel. Click '⚡ Trigger' on the node to emit messages.",
            }

        in_ch = first_handler.get("input_channel")
        first_in_sock = f"/tmp/proxy_sim_{in_ch}.sock" if in_ch else None

        # Check if first handler is udp_ingress_handler with an external UDP port
        udp_port = first_handler.get("config", {}).get("ingress.port") if first_handler.get("type") == "udp_ingress_handler" else None

        is_batch = len(current_batch_pkts) > 1
        seq_label = f"Batch [{', '.join(str(p.seq_id) for p in current_batch_pkts)}]" if is_batch else f"#{pkt.seq_id}"

        # 1. Emit Ingress event
        ev_ingress = {
            "eventId": self.event_counter,
            "msgId": pkt.seq_id,
            "batchIds": [p.seq_id for p in current_batch_pkts],
            "batchCount": len(current_batch_pkts),
            "timestampNs": pkt.timestamp_ns,
            "type": "ingress",
            "source": f"{pkt.src_ip}:{pkt.src_port}",
            "target": first_handler["name"],
            "targetType": first_handler.get("type", "unknown"),
            "channel": "udp_network" if udp_port else in_ch,
            "payloadSummary": f"Batch of {len(current_batch_pkts)} packets" if is_batch else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text),
            "payloadFull": "\n---\n".join(p.payload_text for p in current_batch_pkts) if is_batch else pkt.payload_text,
            "status": "received",
            "latencyUs": 0.0,
            "detail": f"Packet {seq_label} injected into {first_handler['name']}",
        }
        self.event_counter += 1
        self.events.append(ev_ingress)

        # 2. Inject packet(s) into first handler
        if udp_port:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            for p in current_batch_pkts:
                s.sendto(p.raw_payload, ("127.0.0.1", int(udp_port)))
            s.close()
        else:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            try:
                for p in current_batch_pkts:
                    env = p.pack_envelope()
                    s.sendto(env, first_in_sock)
            except Exception as ex:
                print(f"[Coordinator] Error delivering to {first_in_sock}: {ex}")
            s.close()

        # 3. Wait up to 350ms for handler to process and produce output in its hold socket
        out_ch = first_handler.get("output_channel")
        held_data = self._poll_held_output(out_ch, timeout_sec=0.35)

        if held_data:
            # Message(s) reached the output channel and are held!
            self.active_in_flight = {
                "packet": pkt,
                "batch": current_batch_pkts,
                "curr_node_name": first_handler["name"],
                "curr_node_type": first_handler.get("type"),
                "output_channel": out_ch,
                "held_data": held_data,
            }
            batch_count = len(held_data) if isinstance(held_data, list) else 1
            batch_note = f" (Batch of {batch_count} messages)" if batch_count > 1 else ""
            ev_hop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in current_batch_pkts],
                "batchCount": len(current_batch_pkts),
                "timestampNs": time.time_ns(),
                "type": "hop",
                "source": "ingress",
                "target": first_handler["name"],
                "targetType": first_handler.get("type"),
                "channel": in_ch if in_ch else (f"udp:{udp_port}" if udp_port else out_ch),
                "payloadSummary": f"Batch of {batch_count} messages" if is_batch else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text),
                "payloadFull": "\n---\n".join(p.payload_text for p in current_batch_pkts) if is_batch else pkt.payload_text,
                "status": "at_node",
                "latencyUs": 10.0,
                "detail": f"Injected into '{first_handler['name']}' -> held at output channel '{out_ch}'{batch_note}",
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
                "batchIds": [p.seq_id for p in current_batch_pkts],
                "batchCount": len(current_batch_pkts),
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
                "detail": f"Packet {seq_label} rejected and discarded at node '{first_handler['name']}'",
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

        # Deliver all held envelope(s) in the batch into the next handler's input channel socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        try:
            if isinstance(held_data, list):
                for envelope in held_data:
                    s.sendto(envelope, next_in_sock)
            else:
                s.sendto(held_data, next_in_sock)
        except Exception as ex:
            print(f"[Coordinator] Error delivering held envelope to {next_in_sock}: {ex}")
        s.close()

        # Wait up to 250ms for next handler to process
        next_out_ch = next_handler.get("output_channel")
        is_sink = not next_out_ch or "egress" in next_handler.get("type", "") or "print" in next_handler.get("type", "")

        batch_pkts = info.get("batch") or [pkt]
        batch_ids_str = f"Batch [{', '.join(str(p.seq_id) for p in batch_pkts)}]" if len(batch_pkts) > 1 else f"#{pkt.seq_id}"

        if is_sink:
            time.sleep(0.08)  # Let sink print or egress
            self.active_in_flight = None
            ev_sink = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": time.time_ns(),
                "type": "egress",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": f"Batch of {len(batch_pkts)} messages" if len(batch_pkts) > 1 else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text),
                "payloadFull": "\n---\n".join(p.payload_text for p in batch_pkts) if len(batch_pkts) > 1 else pkt.payload_text,
                "status": "transmitted",
                "latencyUs": 20.0,
                "detail": f"{batch_ids_str} reached sink/egress handler '{next_handler['name']}'",
            }
            self.event_counter += 1
            self.events.append(ev_sink)
            return {"status": "ok", "event": ev_sink, "finishedPacket": pkt.seq_id}

        # Intermediate handler
        next_held = self._poll_held_output(next_out_ch, timeout_sec=0.25)
        if next_held:
            out_batch_count = len(next_held) if isinstance(next_held, list) else 1
            self.active_in_flight = {
                "packet": pkt,
                "batch": batch_pkts,
                "curr_node_name": next_handler["name"],
                "curr_node_type": next_handler.get("type"),
                "output_channel": next_out_ch,
                "held_data": next_held,
            }
            batch_note = f" (Batch of {out_batch_count} messages)" if out_batch_count > 1 else ""
            ev_hop = {
                "eventId": self.event_counter,
                "msgId": pkt.seq_id,
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": out_batch_count,
                "timestampNs": time.time_ns(),
                "type": "hop",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": f"Batch of {out_batch_count} messages" if out_batch_count > 1 else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text),
                "payloadFull": "\n---\n".join(p.payload_text for p in batch_pkts) if len(batch_pkts) > 1 else pkt.payload_text,
                "status": "at_node",
                "latencyUs": 15.0,
                "detail": f"{batch_ids_str} forwarded from '{info['curr_node_name']}' to '{next_handler['name']}' -> held at '{next_out_ch}'{batch_note}",
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
                "batchIds": [p.seq_id for p in batch_pkts],
                "batchCount": len(batch_pkts),
                "timestampNs": time.time_ns(),
                "type": "drop",
                "source": info["curr_node_name"],
                "target": next_handler["name"],
                "targetType": next_handler.get("type"),
                "channel": ch,
                "payloadSummary": f"Batch of {len(batch_pkts)} messages" if len(batch_pkts) > 1 else (pkt.payload_text[:60] + "..." if len(pkt.payload_text) > 60 else pkt.payload_text),
                "payloadFull": "\n---\n".join(p.payload_text for p in batch_pkts) if len(batch_pkts) > 1 else pkt.payload_text,
                "status": "dropped",
                "latencyUs": 20.0,
                "detail": f"{batch_ids_str} dropped by '{next_handler['name']}' (0 bytes emitted to '{next_out_ch}')",
            }
            self.event_counter += 1
            self.events.append(ev_drop)
            return {"status": "dropped", "event": ev_drop}

    def _poll_held_output(self, channel_name: Optional[str], timeout_sec: float = 0.15) -> Optional[list[bytes]]:
        """
        Polls the hold socket for one or more batched messages emitted by the upstream handler.
        Returns a list of datagram byte buffers if any were emitted.
        """
        if not channel_name or channel_name not in self.hold_sockets:
            return None
        sock = self.hold_sockets[channel_name]
        start = time.time()
        batch: list[bytes] = []

        # Wait for the first message
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
            time.sleep(0.005)

        if not batch:
            return None

        # Speculatively drain any additional batched datagrams queued in the socket
        drain_deadline = time.time() + 0.05
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

            active_event = self.events[-1] if self.events else None
            return {
                "status": "ok",
                "currentPacketIdx": self.current_packet_idx,
                "inFlight": bool(self.active_in_flight),
                "event": active_event,
            }

    def trigger_generator(self, node_name: str) -> dict[str, Any]:
        """
        Triggers a generator node on demand:
          1. Finds the generator handler specification.
          2. Creates its configured payload as a raw envelope.
          3. Holds the message in the generator's output channel hold socket.
          4. Emits a hop/trigger event and marks it as active in-flight held at that node.
        """
        with self.lock:
            snapshot = self._snapshot_state()

            gen_handler = None
            for h in self.handlers:
                if h.get("name") == node_name:
                    gen_handler = h
                    break

            if not gen_handler:
                return {"status": "error", "message": f"Handler node '{node_name}' not found."}

            out_ch = gen_handler.get("output_channel")
            if not out_ch:
                return {"status": "error", "message": f"Handler '{node_name}' has no output_channel."}

            # Drain hold socket only if we are not accumulating an in-flight batch at this channel
            if out_ch in self.hold_sockets and not (self.active_in_flight and self.active_in_flight.get("curr_node_name") == gen_handler["name"]):
                while True:
                    try:
                        self.hold_sockets[out_ch].recvfrom(65535)
                    except Exception:
                        break

            # Resolve message text
            h_config = gen_handler.get("config", {})
            msg_text = h_config.get("dummy.message", f"Message from {node_name}")
            payload_bytes = msg_text.encode("utf-8")

            # Pack raw MessageEnvelope
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

            is_accumulating = (
                self.active_in_flight is not None
                and self.active_in_flight.get("curr_node_name") == gen_handler["name"]
                and self.active_in_flight.get("output_channel") == out_ch
            )

            if is_accumulating:
                existing_held = self.active_in_flight["held_data"]
                if isinstance(existing_held, list):
                    held_batch = existing_held + [raw_envelope]
                else:
                    held_batch = [existing_held, raw_envelope]

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
                    "batchIds": [p.seq_id for p in all_batch_pkts],
                    "batchCount": batch_len,
                    "timestampNs": time.time_ns(),
                    "type": "hop",
                    "source": gen_handler["name"],
                    "target": gen_handler["name"],
                    "targetType": gen_handler.get("type"),
                    "channel": out_ch,
                    "payloadSummary": f"Batch of {batch_len} messages [Pkts {batch_ids_str}]",
                    "payloadFull": "\n---\n".join(p.payload_text for p in all_batch_pkts),
                    "status": "at_node",
                    "latencyUs": 5.0,
                    "detail": f"Generator '{gen_handler['name']}' accumulated message #{synthetic_pkt.seq_id} -> held batch of {batch_len} at '{out_ch}'",
                }
            else:
                self.active_in_flight = {
                    "packet": synthetic_pkt,
                    "batch": [synthetic_pkt],
                    "curr_node_name": gen_handler["name"],
                    "curr_node_type": gen_handler.get("type"),
                    "output_channel": out_ch,
                    "held_data": [raw_envelope],
                }

                ev_gen = {
                    "eventId": self.event_counter,
                    "msgId": synthetic_pkt.seq_id,
                    "batchIds": [synthetic_pkt.seq_id],
                    "batchCount": 1,
                    "timestampNs": time.time_ns(),
                    "type": "hop",
                    "source": gen_handler["name"],
                    "target": gen_handler["name"],
                    "targetType": gen_handler.get("type"),
                    "channel": out_ch,
                    "payloadSummary": msg_text[:60] + "..." if len(msg_text) > 60 else msg_text,
                    "payloadFull": msg_text,
                    "status": "at_node",
                    "latencyUs": 5.0,
                    "detail": f"Generator '{gen_handler['name']}' triggered -> held at output channel '{out_ch}'",
                }
            self.event_counter += 1
            self.events.append(ev_gen)
            if not is_accumulating:
                self.step_history.append(snapshot)

            batch_obj = None
            if is_accumulating:
                batch_obj = {
                    "id": f"batch_{all_batch_pkts[0].seq_id}_{len(all_batch_pkts)}",
                    "batchCount": len(all_batch_pkts),
                    "batchIds": [p.seq_id for p in all_batch_pkts],
                    "timestampNs": all_batch_pkts[0].timestamp_ns,
                    "srcIp": all_batch_pkts[0].src_ip,
                    "srcPort": all_batch_pkts[0].src_port,
                    "dstIp": all_batch_pkts[0].dst_ip,
                    "dstPort": all_batch_pkts[0].dst_port,
                    "size": sum(len(p.raw_payload) for p in all_batch_pkts),
                    "payloadSummary": f"Batch of {len(all_batch_pkts)} messages [Pkts {batch_ids_str}]",
                    "payloadFull": "\n---\n".join(f"[Message #{p.seq_id}]: {p.payload_text}" for p in all_batch_pkts),
                }

            return {
                "status": "ok",
                "event": ev_gen,
                "packet": synthetic_pkt.to_dict(),
                "batchPacket": batch_obj,
                "heldChannel": out_ch,
                "inFlight": True,
            }

    def reset(self):
        with self.lock:
            self.current_packet_idx = 0
            self.events.clear()
            self.step_history.clear()
            self.event_counter = 1
            self.active_in_flight = None
            for sock in self.hold_sockets.values():
                while True:
                    try:
                        sock.recvfrom(65535)
                    except Exception:
                        break

    def run_to_completion(self, max_steps: int = 10000) -> list[dict[str, Any]]:
        """
        Fast-forward headless loop: advances the pipeline step-by-step until all
        packets have reached egress or drop, or max_steps limit is reached.
        Returns the list of recorded simulation events.
        """
        steps = 0
        while steps < max_steps:
            res = self.step_forward()
            steps += 1
            if res.get("status") == "finished":
                break
        return list(self.events)

