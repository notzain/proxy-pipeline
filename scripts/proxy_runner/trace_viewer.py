"""
Standalone Trace Simulation HTTP server.
Allows visualizing and stepping forwards/backwards through an export.trace.json artifact
without starting up the simulation backend or any C++ handler processes.
"""

import http.server
import json
from pathlib import Path
import sys

from .trace import TraceManager


class StandaloneTraceHttpHandler(http.server.SimpleHTTPRequestHandler):
    trace_data: dict

    def do_GET(self):
        dashboard_dir = Path(__file__).resolve().parent.parent / "dashboard"

        if self.path in ("/", "/index.html"):
            index_file = dashboard_dir / "index.html"
            content = index_file.read_text(encoding="utf-8")

            safe_json = json.dumps(self.trace_data).replace("</", "<\\/")
            auto_load_snippet = f"""
            <script>
              window.__SIM_MODE__ = "trace";
              window.__INITIAL_TRACE_DOC__ = {safe_json};
              window.__INITIAL_TRACE_NAME__ = {json.dumps(self.trace_data.get('name', 'export.trace.json'))};
            </script>
            """
            content = content.replace("</body>", f"{auto_load_snippet}</body>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.end_headers()
            self.wfile.write(content.encode("utf-8"))
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
        elif self.path == "/api/simulation":
            packets = self.trace_data.get("packets", [])
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps({
                "mode": "trace",
                "chain": self.trace_data.get("topology") or {"backend": "sim", "handlers": []},
                "pcap": self.trace_data.get("name", "export.trace.json"),
                "packetCount": len(packets),
                "packets": packets,
                "events": self.trace_data.get("events", []),
                "currentPacketIdx": 0,
                "inFlight": False,
                "inFlightStage": None,
                "inFlightSummary": None,
            }).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass


def run_trace_viewer(trace_path: Path, port: int = 8090) -> int:
    if not trace_path.is_file():
        print(f"Error: Trace artifact not found: {trace_path}", file=sys.stderr)
        return 1

    doc = TraceManager.load_trace_document(trace_path)
    doc["name"] = trace_path.name
    StandaloneTraceHttpHandler.trace_data = doc

    server = http.server.HTTPServer(("0.0.0.0", port), StandaloneTraceHttpHandler)
    print(f"\n=======================================================")
    print(f" 📜 Trace Simulation Mode active (no backend started)")
    print(f" 📄 Trace file: {trace_path.name} ({len(doc.get('events', []))} events)")
    print(f" 👉 http://localhost:{port}")
    print(f"=======================================================\n")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping trace viewer server.")
        server.shutdown()

    return 0
