"""
Standalone trace viewer HTTP server for simulation_dashboard.html.
Allows visualizing and replaying any trace artifact independently without chain configs or C++ processes.
"""

import http.server
import json
from pathlib import Path
import sys
import time

from .trace import TraceManager


class StandaloneTraceHttpHandler(http.server.SimpleHTTPRequestHandler):
    trace_data: dict

    def do_GET(self):
        dashboard_dir = Path(__file__).resolve().parent.parent / "dashboard"

        if self.path in ("/", "/index.html"):
            index_file = dashboard_dir / "index.html"
            content = index_file.read_text(encoding="utf-8")
            
            # Inject auto-load script for the standalone trace into the reactive app
            auto_load_snippet = f"""
            <script>
              window.__INITIAL_TRACE_DOC__ = {json.dumps(self.trace_data)};
              window.__INITIAL_TRACE_NAME__ = "{self.trace_data.get('name', 'trace.json')}";
            </script>
            """
            content = content.replace("</body>", f"{auto_load_snippet}</body>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(content.encode("utf-8"))
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
        elif self.path == "/api/simulation":
            # Return baseline schema so dashboard initialization succeeds
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps({
                "chain": self.trace_data.get("topology") or {"backend": "sim", "handlers": []},
                "pcap": self.trace_data.get("name", "trace.json"),
                "packetCount": 0,
                "packets": [],
                "events": self.trace_data.get("events", []),
                "currentPacketIdx": 0,
                "inFlight": False,
                "inFlightSummary": None
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
    print(f" 🚀 Standalone Trace Viewer active for: {trace_path.name}")
    print(f" 👉 http://localhost:{port}")
    print(f"=======================================================\n")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping trace viewer server.")
        server.shutdown()

    return 0

