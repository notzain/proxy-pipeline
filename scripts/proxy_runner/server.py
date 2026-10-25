"""
HTTP dashboard and REST API server for interactive simulation visualizer.
"""

import http.server
import json
from pathlib import Path
import threading

from .coordinator import InteractiveSimulationCoordinator
from .handlers import load_handler_descriptor
from .trace import TraceManager


class SimDashboardHandler(http.server.SimpleHTTPRequestHandler):
    coordinator: InteractiveSimulationCoordinator
    pcap_path: str

    def _send_json(self, status_code: int, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict:
        content_len = int(self.headers.get("Content-Length", 0))
        if content_len <= 0:
            return {}
        raw = self.rfile.read(content_len)
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:
            return {}

    def do_GET(self):
        dashboard_dir = Path(__file__).resolve().parent.parent / "dashboard"

        if self.path in ("/", "/index.html"):
            index_file = dashboard_dir / "index.html"
            if index_file.is_file():
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
                self.end_headers()
                self.wfile.write(index_file.read_bytes())
                return
        elif self.path.startswith(("/css/", "/js/", "/vendor/")):
            clean_path = self.path.split("?")[0].lstrip("/")
            static_file = dashboard_dir / clean_path
            if static_file.is_file():
                content_type = "text/plain"
                if clean_path.endswith(".css"):
                    content_type = "text/css"
                elif clean_path.endswith(".js"):
                    content_type = "application/javascript"
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
                self.end_headers()
                self.wfile.write(static_file.read_bytes())
                return

        if self.path == "/api/simulation":
            packets_meta = [p.to_dict() for p in self.coordinator.pcap_packets]
            in_flight_summary = None
            in_flight_stage = None
            if self.coordinator.active_in_flight:
                info = self.coordinator.active_in_flight
                in_flight_stage = info.get("stage", "held")
                batch_pkts = info.get("batch") or ([info["packet"]] if info.get("packet") else [])
                pending_count = len(info.get("pending_branches") or [])
                in_flight_summary = {
                    "stage": in_flight_stage,
                    "node": info.get("curr_node_name"),
                    "inputChannel": info.get("input_channel"),
                    "channel": info.get("output_channel"),
                    "batchCount": len(batch_pkts),
                    "batchIds": [p.seq_id for p in batch_pkts],
                    "msgId": self.coordinator.active_in_flight["packet"].seq_id if self.coordinator.active_in_flight.get("packet") else None,
                    "pendingBranchesCount": pending_count,
                }

            # Enrich handlers with role & protocol metadata
            handlers_meta = []
            for h in self.coordinator.handlers:
                h_type = h.get("type", "")
                desc = load_handler_descriptor(h_type)
                h_copy = dict(h)
                h_copy["role"] = desc.role.value
                h_copy["protocol"] = desc.protocol.value
                handlers_meta.append(h_copy)

            chain_meta = dict(self.coordinator.chain_config)
            chain_meta["handlers"] = handlers_meta

            # Compute queue display items (grouping accumulated batch packets into cohesive units)
            display_queue = []
            rendered_batch_keys = set()
            all_groups = [list(g) for g in self.coordinator.batch_groups]
            if in_flight_summary and in_flight_summary.get("batchCount", 0) > 1:
                if_batch_ids = in_flight_summary.get("batchIds", [])
                if not any(set(g) == set(if_batch_ids) for g in all_groups):
                    all_groups.append(if_batch_ids)

            # Pending queue IDs
            pending_batch_ids: list[list[int]] = []
            for b in self.coordinator.pending_queue:
                bpkts = b.get("batch") or ([b["packet"]] if b.get("packet") else [])
                pending_batch_ids.append([p.seq_id for p in bpkts])

            for p_idx, pkt in enumerate(packets_meta):
                pid = pkt["id"]
                found_group = next((g for g in all_groups if pid in g and len(g) > 1), None)
                if found_group:
                    g_key = tuple(sorted(found_group))
                    if g_key not in rendered_batch_keys:
                        rendered_batch_keys.add(g_key)
                        group_pkts = [p for p in packets_meta if p["id"] in found_group]
                        total_bytes = sum(p.get("size", 0) for p in group_pkts)
                        is_active = (
                            in_flight_summary is not None
                            and any(bid in in_flight_summary.get("batchIds", []) for bid in found_group)
                        )
                        is_queued = not is_active and any(
                            any(bid in pb for bid in found_group)
                            for pb in pending_batch_ids
                        )
                        is_completed = not is_active and not is_queued and any(
                            ev.get("type") in ("egress", "drop")
                            and (
                                (ev.get("batchIds") and any(bid in found_group for bid in ev.get("batchIds", [])))
                                or ev.get("msgId") in found_group
                            )
                            for ev in self.coordinator.events
                        )
                        display_queue.append({
                            "key": f"batch_{'_'.join(str(x) for x in g_key)}",
                            "isBatch": True,
                            "id": found_group[0],
                            "batchIds": found_group,
                            "batchCount": len(found_group),
                            "packets": group_pkts,
                            "size": total_bytes,
                            "isActive": is_active,
                            "isQueued": is_queued,
                            "isCompleted": is_completed,
                            "heldNode": in_flight_summary.get("node") if is_active else None,
                            "heldChannel": in_flight_summary.get("channel") if is_active else None,
                        })
                else:
                    is_active = in_flight_summary is not None and (
                        in_flight_summary.get("batchIds") == [pid]
                        or in_flight_summary.get("msgId") == pid
                    )
                    is_queued = not is_active and any(pid in pb for pb in pending_batch_ids)
                    is_completed = not is_active and not is_queued and any(
                        ev.get("type") in ("egress", "drop")
                        and (
                            (ev.get("batchIds") and pid in ev.get("batchIds", []))
                            or ev.get("msgId") == pid
                        )
                        for ev in self.coordinator.events
                    )
                    display_queue.append({
                        "key": f"pkt_{pid}",
                        "isBatch": False,
                        "id": pid,
                        "packet": pkt,
                        "pIdx": p_idx,
                        "isActive": is_active,
                        "isQueued": is_queued,
                        "isCompleted": is_completed,
                    })

            self._send_json(200, {
                "mode": "realtime",
                "chain": chain_meta,
                "pcap": self.pcap_path,
                "packetCount": len(self.coordinator.pcap_packets),
                "packets": packets_meta,
                "displayQueue": display_queue,
                "batchGroups": self.coordinator.batch_groups,
                "events": self.coordinator.events,
                "currentPacketIdx": self.coordinator.current_packet_idx,
                "inFlight": bool(self.coordinator.active_in_flight),
                "hasPendingQueue": len(self.coordinator.pending_queue) > 0,
                "pendingQueueCount": len(self.coordinator.pending_queue),
                "inFlightStage": in_flight_stage,
                "inFlightSummary": in_flight_summary,
            })
        elif self.path == "/api/simulation/export_trace":
            packets_meta = [p.to_dict() for p in self.coordinator.pcap_packets]
            doc = TraceManager.build_trace_document(
                self.coordinator.events,
                chain_config=self.coordinator.chain_config,
                packets=packets_meta,
            )
            body = json.dumps(doc, indent=2, sort_keys=True).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Disposition", 'attachment; filename="export.trace.json"')
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/api/simulation/load_packet":
            params = self._read_json_body()
            pkt_id = params.get("id")
            result = self.coordinator.load_next_packet(int(pkt_id) if pkt_id is not None else None)
            self._send_json(200, result)
        elif self.path == "/api/simulation/step":
            params = self._read_json_body()
            auto_load = bool(params.get("autoLoad", False))
            result = self.coordinator.step_forward(auto_load=auto_load)
            self._send_json(200, result)
        elif self.path == "/api/simulation/reset":
            self.coordinator.reset()
            self._send_json(200, {"status": "ok"})
        elif self.path == "/api/simulation/select":
            params = self._read_json_body()
            pkt_id = params.get("id")
            if pkt_id is None:
                self._send_json(400, {"status": "error", "message": "Missing packet id"})
                return
            result = self.coordinator.select_packet(int(pkt_id))
            self._send_json(200, result)
        elif self.path == "/api/simulation/trigger":
            params = self._read_json_body()
            node_name = params.get("node")
            if not node_name:
                self._send_json(400, {"status": "error", "message": "Missing node name"})
                return
            result = self.coordinator.trigger_generator(str(node_name))
            self._send_json(200, result)
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
