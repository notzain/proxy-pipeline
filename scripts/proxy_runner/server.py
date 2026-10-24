"""
HTTP dashboard and REST API server for interactive simulation visualizer.
"""

import http.server
import json
from pathlib import Path
import threading

from .coordinator import InteractiveSimulationCoordinator


class SimDashboardHandler(http.server.SimpleHTTPRequestHandler):
    coordinator: InteractiveSimulationCoordinator
    pcap_path: str

    def do_GET(self):
        dashboard_dir = Path(__file__).resolve().parent.parent / "dashboard"
        
        # Route static dashboard files (html, css, js, vendor)
        if self.path in ("/", "/index.html"):
            index_file = dashboard_dir / "index.html"
            if index_file.is_file():
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(index_file.read_bytes())
                return
        elif self.path.startswith(("/css/", "/js/", "/vendor/")):
            static_file = dashboard_dir / self.path.lstrip("/")
            if static_file.is_file():
                content_type = "text/plain"
                if self.path.endswith(".css"):
                    content_type = "text/css"
                elif self.path.endswith(".js"):
                    content_type = "application/javascript"
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.end_headers()
                self.wfile.write(static_file.read_bytes())
                return

        if self.path == "/api/simulation":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            packets_meta = [p.to_dict() for p in self.coordinator.pcap_packets]
            in_flight_summary = None
            if self.coordinator.active_in_flight:
                info = self.coordinator.active_in_flight
                batch_pkts = info.get("batch") or ([info["packet"]] if info.get("packet") else [])
                in_flight_summary = {
                    "node": info.get("curr_node_name"),
                    "channel": info.get("output_channel"),
                    "batchCount": len(batch_pkts),
                    "batchIds": [p.seq_id for p in batch_pkts],
                }

            data = {
                "chain": self.coordinator.chain_config,
                "pcap": self.pcap_path,
                "packetCount": len(self.coordinator.pcap_packets),
                "packets": packets_meta,
                "batchGroups": self.coordinator.batch_groups,
                "events": self.coordinator.events,
                "currentPacketIdx": self.coordinator.current_packet_idx,
                "inFlight": bool(self.coordinator.active_in_flight),
                "inFlightSummary": in_flight_summary,
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
        elif self.path == "/api/simulation/trigger":
            content_len = int(self.headers.get("Content-Length", 0))
            post_body = self.rfile.read(content_len) if content_len > 0 else b"{}"
            try:
                params = json.loads(post_body.decode("utf-8"))
            except Exception:
                params = {}
            node_name = params.get("node")
            if not node_name:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b'{"status":"error","message":"Missing node name"}')
                return
            result = self.coordinator.trigger_generator(str(node_name))
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

