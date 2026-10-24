"""
HTTP dashboard and REST API server for interactive simulation visualizer.
"""

import http.server
import json
from pathlib import Path
import threading

from .coordinator import InteractiveSimulationCoordinator
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
                in_flight_summary = {
                    "stage": in_flight_stage,
                    "node": info.get("curr_node_name"),
                    "inputChannel": info.get("input_channel"),
                    "channel": info.get("output_channel"),
                    "batchCount": len(batch_pkts),
                    "batchIds": [p.seq_id for p in batch_pkts],
                }

            self._send_json(200, {
                "mode": "realtime",
                "chain": self.coordinator.chain_config,
                "pcap": self.pcap_path,
                "packetCount": len(self.coordinator.pcap_packets),
                "packets": packets_meta,
                "batchGroups": self.coordinator.batch_groups,
                "events": self.coordinator.events,
                "currentPacketIdx": self.coordinator.current_packet_idx,
                "inFlight": bool(self.coordinator.active_in_flight),
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
