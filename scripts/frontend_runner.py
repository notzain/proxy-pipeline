#!/usr/bin/env python3
"""
frontend_runner.py - Unified Control Plane & Web Studio for Proxy pipelines.

Hosts all front-end tools on a single unified HTTP server:
- Landing Page & Control Deck (/)
- Visual Chain Builder (/builder/)
- Simulation Visualizer & Trace Inspector (/dashboard/)
- Live Telemetry & Topology Dashboard (/telemetry/)

Features:
- Built-in UDP StatsD Telemetry Collector (:8125).
- Interactive Simulation Coordinator for live SimBackend pipelines.
- Process supervisor for chain_runner.py: start, stop, restart, and live log streaming
  directly from the UI without restarting the frontend server.
"""

from __future__ import annotations

import argparse
from collections import deque
import http.server
import json
import mimetypes
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Optional

# Ensure scripts root is in sys.path
scripts_dir = Path(__file__).resolve().parent
if str(scripts_dir) not in sys.path:
    sys.path.insert(0, str(scripts_dir))

from proxy_runner.config import find_bin_dir, load_chain_config
from proxy_runner.coordinator import InteractiveSimulationCoordinator
from proxy_runner.handlers import load_handler_descriptor
from proxy_runner.pcap import PcapPacket, PcapReader
from proxy_runner.schema_validator import discover_handler_schemas, validate_chain_topology
from proxy_runner.topology import MAX_TOPOLOGY_BODY_BYTES
from proxy_runner.trace import TraceManager
from read_telemetry import TelemetryCollector, run_listener, validate_topology_document


class ChainProcessManager:
    """
    Manages execution lifecycle of chain_runner.py as a supervised child process.
    Allows starting and stopping chains from the UI while keeping the frontend alive.
    """

    def __init__(self, root_dir: Path):
        self.root_dir = root_dir
        self.lock = threading.Lock()
        self.process: Optional[subprocess.Popen] = None
        self.state: str = "idle"  # "idle" | "running" | "stopped" | "error"
        self.pid: Optional[int] = None
        self.config_path: Optional[str] = None
        self.backend: Optional[str] = None
        self.pcap_path: Optional[str] = None
        self.started_at: Optional[float] = None
        self.exit_code: Optional[int] = None
        self.logs: deque[str] = deque(maxlen=2000)
        self.coordinator: Optional[InteractiveSimulationCoordinator] = None
        self.current_chain_def: dict[str, Any] = {}

    def get_status(self) -> dict[str, Any]:
        with self.lock:
            if self.process is not None and self.state == "running":
                poll_code = self.process.poll()
                if poll_code is not None:
                    self.state = "stopped" if poll_code == 0 else "error"
                    self.exit_code = poll_code
                    self.process = None

            uptime = (time.time() - self.started_at) if (self.state == "running" and self.started_at) else 0.0

            return {
                "state": self.state,
                "pid": self.pid if self.state == "running" else None,
                "configPath": self.config_path,
                "backend": self.backend,
                "pcap": self.pcap_path,
                "startedAt": self.started_at,
                "uptimeSec": uptime,
                "exitCode": self.exit_code,
                "logs": list(self.logs),
            }

    def start_chain(
        self,
        config_path_str: str,
        backend: Optional[str] = None,
        pcap_str: Optional[str] = None,
        bin_dir: Optional[str] = None,
        http_port: int = 8080,
    ) -> dict[str, Any]:
        with self.lock:
            if self.process is not None and self.process.poll() is None:
                self._stop_process_locked(timeout=2.0)

            cfg_path = Path(config_path_str)
            if not cfg_path.is_absolute():
                cfg_path = (self.root_dir / cfg_path).resolve()

            if not cfg_path.is_file():
                raise FileNotFoundError(f"Chain configuration file not found: {cfg_path}")

            chain_def = load_chain_config(cfg_path)
            self.current_chain_def = chain_def
            resolved_backend = backend or chain_def.get("backend", "sim")

            pcap_path: Optional[Path] = None
            pcap_packets: list[PcapPacket] = []
            if pcap_str:
                p_cand = Path(pcap_str)
                if not p_cand.is_absolute():
                    p_cand = (self.root_dir / p_cand).resolve()
                if p_cand.is_file():
                    pcap_path = p_cand
                    pcap_packets = PcapReader.read_packets(str(pcap_path))

            # If simulation backend, set up InteractiveSimulationCoordinator
            if self.coordinator:
                try:
                    self.coordinator.close()
                except Exception:
                    pass
                self.coordinator = None

            if resolved_backend == "sim":
                self.coordinator = InteractiveSimulationCoordinator(chain_def, pcap_packets)

            runner_script = self.root_dir / "scripts" / "chain_runner.py"
            cmd = [
                sys.executable,
                str(runner_script),
                str(cfg_path),
                "--backend",
                resolved_backend,
                "--telemetry-url",
                f"http://127.0.0.1:{http_port}/api/topology",
            ]
            if pcap_path:
                cmd.extend(["--pcap", str(pcap_path)])
            if bin_dir:
                cmd.extend(["--bin-dir", str(bin_dir)])

            self.logs.clear()
            self.logs.append(f"[FrontendRunner] Starting chain: {cfg_path.name} (backend={resolved_backend})")
            self.logs.append(f"[FrontendRunner] Executing: {' '.join(cmd)}")

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                cwd=str(self.root_dir),
                start_new_session=True,
            )

            self.process = proc
            self.pid = proc.pid
            self.state = "running"
            self.config_path = str(cfg_path.relative_to(self.root_dir) if cfg_path.is_relative_to(self.root_dir) else cfg_path)
            self.backend = resolved_backend
            self.pcap_path = str(pcap_path.name) if pcap_path else None
            self.started_at = time.time()
            self.exit_code = None

            # Spawn reader thread for output
            reader_thread = threading.Thread(
                target=self._tail_output,
                args=(proc,),
                daemon=True,
            )
            reader_thread.start()

            return {
                "status": "ok",
                "pid": proc.pid,
                "backend": resolved_backend,
                "configPath": self.config_path,
            }

    def _tail_output(self, proc: subprocess.Popen):
        try:
            if proc.stdout:
                for line in iter(proc.stdout.readline, ""):
                    clean_line = line.rstrip()
                    if clean_line:
                        with self.lock:
                            self.logs.append(clean_line)
                proc.stdout.close()
        except Exception:
            pass
        finally:
            proc.wait()
            with self.lock:
                if self.process == proc:
                    self.exit_code = proc.returncode
                    self.state = "stopped" if proc.returncode == 0 else "error"
                    self.logs.append(f"[FrontendRunner] Chain process exited with code {proc.returncode}")

    def stop_chain(self, timeout: float = 3.0) -> dict[str, Any]:
        with self.lock:
            return self._stop_process_locked(timeout=timeout)

    def _stop_process_locked(self, timeout: float = 3.0) -> dict[str, Any]:
        if self.process is None or self.process.poll() is not None:
            self.state = "stopped"
            if self.coordinator:
                try:
                    self.coordinator.close()
                except Exception:
                    pass
                self.coordinator = None
            return {"status": "ok", "message": "No process was running"}

        self.logs.append(f"[FrontendRunner] Terminating chain runner PID {self.process.pid} (SIGINT)...")
        try:
            os.killpg(self.process.pid, signal.SIGINT)
        except Exception:
            try:
                self.process.send_signal(signal.SIGINT)
            except Exception:
                pass

        try:
            self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.logs.append(f"[FrontendRunner] Process did not terminate within {timeout}s, force killing (SIGKILL)...")
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except Exception:
                try:
                    self.process.kill()
                except Exception:
                    pass
            self.process.wait()

        self.exit_code = self.process.returncode
        self.state = "stopped"
        self.process = None

        if self.coordinator:
            try:
                self.coordinator.close()
            except Exception:
                pass
            self.coordinator = None

        self.logs.append("[FrontendRunner] Chain stopped cleanly.")
        return {"status": "ok", "exitCode": self.exit_code}

    def list_chains(self) -> list[dict[str, Any]]:
        chains_dir = self.root_dir / "chains"
        result = []
        if chains_dir.is_dir():
            for f in sorted(chains_dir.iterdir()):
                if f.is_file() and f.suffix in (".toml", ".json") and not f.name.endswith(".trace.json"):
                    try:
                        cfg = load_chain_config(f)
                        result.append({
                            "name": f.name,
                            "path": str(f.relative_to(self.root_dir)),
                            "backend": cfg.get("backend", "file"),
                            "handlersCount": len(cfg.get("handlers", [])),
                        })
                    except Exception:
                        result.append({
                            "name": f.name,
                            "path": str(f.relative_to(self.root_dir)),
                            "backend": "unknown",
                            "handlersCount": 0,
                        })
        return result

    def list_pcaps(self) -> list[str]:
        result = []
        candidates = [self.root_dir] + list(self.root_dir.glob("pcaps")) + list(self.root_dir.glob("data"))
        for d in candidates:
            if d.is_dir():
                for f in sorted(d.glob("*.pcap")):
                    if f.is_file():
                        rel = str(f.relative_to(self.root_dir))
                        if rel not in result:
                            result.append(rel)
        return result


class FrontendStudioHandler(http.server.SimpleHTTPRequestHandler):
    root_dir: Path = Path(__file__).resolve().parent.parent
    manager: ChainProcessManager
    collector: TelemetryCollector
    schemas: dict[str, Any] = {}
    http_port: int = 8080

    def _send_json(self, status_code: int, payload: Any):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict[str, Any]:
        content_len = int(self.headers.get("Content-Length", 0))
        if content_len <= 0:
            return {}
        raw = self.rfile.read(content_len)
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:
            return {}

    def _serve_file(self, file_path: Path, fallback_content_type: str = "text/plain"):
        if not file_path.is_file():
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"404 Not Found")
            return

        content_type, _ = mimetypes.guess_type(str(file_path))
        if not content_type:
            if file_path.suffix == ".css":
                content_type = "text/css"
            elif file_path.suffix == ".js":
                content_type = "application/javascript"
            elif file_path.suffix == ".html":
                content_type = "text/html; charset=utf-8"
            else:
                content_type = fallback_content_type

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(file_path.read_bytes())

    def do_GET(self):
        scripts_path = self.root_dir / "scripts"
        clean_path = self.path.split("?")[0]

        # -------------------------------------------------------------
        # 1. LANDING PAGE & PORTAL ASSETS
        # -------------------------------------------------------------
        if clean_path in ("/", "/index.html"):
            portal_index = scripts_path / "portal" / "index.html"
            return self._serve_file(portal_index, "text/html; charset=utf-8")

        if clean_path.startswith("/portal/"):
            subpath = clean_path[len("/portal/"):].lstrip("/")
            return self._serve_file(scripts_path / "portal" / subpath)

        # -------------------------------------------------------------
        # 2. VISUAL CHAIN BUILDER
        # -------------------------------------------------------------
        if clean_path in ("/builder", "/builder/", "/builder/index.html"):
            builder_index = scripts_path / "builder" / "index.html"
            return self._serve_file(builder_index, "text/html; charset=utf-8")

        if clean_path.startswith("/builder/"):
            subpath = clean_path[len("/builder/"):].lstrip("/")
            return self._serve_file(scripts_path / "builder" / subpath)

        # -------------------------------------------------------------
        # 3. INTERACTIVE SIMULATION VISUALIZER & TRACE DASHBOARD
        # -------------------------------------------------------------
        if clean_path in ("/dashboard", "/dashboard/", "/dashboard/index.html", "/simulation", "/simulation/"):
            dash_index = scripts_path / "dashboard" / "index.html"
            return self._serve_file(dash_index, "text/html; charset=utf-8")

        if clean_path.startswith("/dashboard/"):
            subpath = clean_path[len("/dashboard/"):].lstrip("/")
            return self._serve_file(scripts_path / "dashboard" / subpath)

        # -------------------------------------------------------------
        # 4. LIVE TELEMETRY DASHBOARD
        # -------------------------------------------------------------
        if clean_path in ("/telemetry", "/telemetry/", "/telemetry/index.html", "/telemetry_dashboard.html"):
            telem_index = scripts_path / "telemetry_dashboard.html"
            return self._serve_file(telem_index, "text/html; charset=utf-8")

        if clean_path in ("/telemetry/telemetry_dashboard.css", "/telemetry_dashboard.css"):
            telem_css = scripts_path / "telemetry_dashboard.css"
            return self._serve_file(telem_css, "text/css")

        # -------------------------------------------------------------
        # 5. REST APIS: SUPERVISOR & RUNNER
        # -------------------------------------------------------------
        if clean_path == "/api/runner/status":
            return self._send_json(200, self.manager.get_status())

        if clean_path == "/api/runner/chains":
            return self._send_json(200, self.manager.list_chains())

        if clean_path == "/api/runner/pcaps":
            return self._send_json(200, self.manager.list_pcaps())

        # -------------------------------------------------------------
        # 6. REST APIS: SCHEMAS & BUILDER
        # -------------------------------------------------------------
        if clean_path == "/api/schemas":
            self.__class__.schemas = discover_handler_schemas()
            return self._send_json(200, self.schemas)

        if clean_path == "/api/chain":
            return self._send_json(200, self.manager.current_chain_def)

        # -------------------------------------------------------------
        # 7. REST APIS: TELEMETRY & TOPOLOGY
        # -------------------------------------------------------------
        if clean_path in ("/api/metrics", "/api/telemetry"):
            return self._send_json(200, self.collector.get_api_snapshot())

        if clean_path == "/api/topology":
            topology = self.collector.get_topology()
            if topology is None:
                return self._send_json(404, {"error": "No chain topology has been published yet"})
            return self._send_json(200, topology)

        # -------------------------------------------------------------
        # 8. REST APIS: INTERACTIVE SIMULATION
        # -------------------------------------------------------------
        if clean_path == "/api/simulation":
            coord = self.manager.coordinator
            if not coord:
                return self._send_json(200, {
                    "mode": "idle",
                    "chain": {"backend": "sim", "handlers": []},
                    "pcap": "None",
                    "packetCount": 0,
                    "packets": [],
                    "displayQueue": [],
                    "batchGroups": [],
                    "events": [],
                    "currentPacketIdx": 0,
                    "inFlight": False,
                    "hasPendingQueue": False,
                    "pendingQueueCount": 0,
                    "inFlightStage": None,
                    "inFlightSummary": None,
                })

            packets_meta = [p.to_dict() for p in coord.pcap_packets]
            in_flight_summary = None
            in_flight_stage = None
            if coord.active_in_flight:
                info = coord.active_in_flight
                in_flight_stage = info.get("stage", "held")
                batch_pkts = info.get("batch") or ([info["packet"]] if info.get("packet") else [])
                pending_branches_list = info.get("pending_branches") or []
                all_held_branches = [info] + list(pending_branches_list)
                held_channels = [b.get("output_channel") for b in all_held_branches if b.get("output_channel")]
                held_nodes = [b.get("curr_node_name") for b in all_held_branches if b.get("curr_node_name")]
                in_flight_summary = {
                    "stage": in_flight_stage,
                    "node": ", ".join(dict.fromkeys(held_nodes)),
                    "inputChannel": info.get("input_channel"),
                    "channel": ", ".join(dict.fromkeys(held_channels)),
                    "channels": held_channels,
                    "nodes": held_nodes,
                    "batchCount": len(batch_pkts),
                    "batchIds": [p.seq_id for p in batch_pkts],
                    "msgId": coord.active_in_flight["packet"].seq_id if coord.active_in_flight.get("packet") else None,
                    "pendingBranchesCount": len(pending_branches_list),
                }

            handlers_meta = []
            for h in coord.handlers:
                h_type = h.get("type", "")
                desc = load_handler_descriptor(h_type)
                h_copy = dict(h)
                h_copy["role"] = desc.role.value
                h_copy["protocol"] = desc.protocol.value
                handlers_meta.append(h_copy)

            chain_meta = dict(coord.chain_config)
            chain_meta["handlers"] = handlers_meta

            display_queue = []
            rendered_batch_keys = set()
            all_groups = [list(g) for g in coord.batch_groups]
            if in_flight_summary and in_flight_summary.get("batchCount", 0) > 1:
                if_batch_ids = in_flight_summary.get("batchIds", [])
                if not any(set(g) == set(if_batch_ids) for g in all_groups):
                    all_groups.append(if_batch_ids)

            pending_batch_ids: list[list[int]] = []
            for b in coord.pending_queue:
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
                            any(bid in pb for bid in found_group) for pb in pending_batch_ids
                        )
                        is_completed = not is_active and not is_queued and any(
                            ev.get("type") in ("egress", "drop")
                            and (
                                (ev.get("batchIds") and any(bid in found_group for bid in ev.get("batchIds", [])))
                                or ev.get("msgId") in found_group
                            )
                            for ev in coord.events
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
                        for ev in coord.events
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

            return self._send_json(200, {
                "mode": "realtime",
                "chain": chain_meta,
                "pcap": self.manager.pcap_path or "None",
                "packetCount": len(coord.pcap_packets),
                "packets": packets_meta,
                "displayQueue": display_queue,
                "batchGroups": coord.batch_groups,
                "events": coord.events,
                "currentPacketIdx": coord.current_packet_idx,
                "inFlight": bool(coord.active_in_flight),
                "hasPendingQueue": len(coord.pending_queue) > 0,
                "pendingQueueCount": len(coord.pending_queue),
                "inFlightStage": in_flight_stage,
                "inFlightSummary": in_flight_summary,
            })

        if clean_path == "/api/simulation/export_trace":
            coord = self.manager.coordinator
            if not coord:
                return self._send_json(400, {"status": "error", "message": "No active simulation"})
            packets_meta = [p.to_dict() for p in coord.pcap_packets]
            doc = TraceManager.build_trace_document(
                coord.events,
                chain_config=coord.chain_config,
                packets=packets_meta,
            )
            body = json.dumps(doc, indent=2, sort_keys=True).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Disposition", 'attachment; filename="export.trace.json"')
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
            return

        # Fallback 404
        self.send_response(404)
        self.end_headers()
        self.wfile.write(b"404 Not Found")

    def do_POST(self):
        clean_path = self.path.split("?")[0]

        # -------------------------------------------------------------
        # 1. RUNNER ACTIONS (START / STOP)
        # -------------------------------------------------------------
        if clean_path == "/api/runner/start":
            body = self._read_json_body()
            cfg_path = body.get("configPath")
            if not cfg_path:
                return self._send_json(400, {"status": "error", "message": "Missing configPath"})

            try:
                res = self.manager.start_chain(
                    config_path_str=cfg_path,
                    backend=body.get("backend"),
                    pcap_str=body.get("pcap"),
                    bin_dir=body.get("binDir"),
                    http_port=self.http_port,
                )
                return self._send_json(200, res)
            except Exception as e:
                return self._send_json(500, {"status": "error", "message": str(e)})

        if clean_path == "/api/runner/stop":
            res = self.manager.stop_chain()
            return self._send_json(200, res)

        # -------------------------------------------------------------
        # 2. TOPOLOGY REGISTRATION (from chain_runner.py)
        # -------------------------------------------------------------
        if clean_path == "/api/topology":
            try:
                content_len = int(self.headers.get("Content-Length", 0))
            except ValueError:
                return self._send_json(400, {"error": "Invalid Content-Length"})

            if content_len <= 0 or content_len > MAX_TOPOLOGY_BODY_BYTES:
                return self._send_json(413, {"error": "Invalid topology body size"})

            try:
                doc = json.loads(self.rfile.read(content_len).decode("utf-8"))
                validated = validate_topology_document(doc)
                self.collector.set_topology(validated)
                self.send_response(204)
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                return
            except Exception as e:
                return self._send_json(400, {"error": str(e)})

        # -------------------------------------------------------------
        # 3. BUILDER TOPOLOGY VALIDATION & SAVE & RUN
        # -------------------------------------------------------------
        if clean_path == "/api/chain/validate":
            payload = self._read_json_body()
            res = validate_chain_topology(payload, self.schemas)
            return self._send_json(200, res)

        if clean_path == "/api/chain/save":
            payload = self._read_json_body()
            target_path = self.root_dir / "chains" / payload.get("filename", "custom_chain.toml")
            target_path.parent.mkdir(parents=True, exist_ok=True)

            toml_lines = [
                "# Auto-generated by Proxy Visual Chain Builder",
            ]
            if payload.get("name"):
                toml_lines.append(f'# Pipeline Name: {payload.get("name")}')
            toml_lines.extend([
                f'backend = "{payload.get("backend", "file")}"',
                "",
                "[config]",
            ])
            for k, v in payload.get("config", {}).items():
                if isinstance(v, bool):
                    toml_lines.append(f'"{k}" = {str(v).lower()}')
                elif isinstance(v, (int, float)):
                    toml_lines.append(f'"{k}" = {v}')
                else:
                    toml_lines.append(f'"{k}" = "{v}"')

            for h in payload.get("handlers", []):
                toml_lines.append("")
                toml_lines.append("[[handlers]]")
                toml_lines.append(f'name = "{h.get("name", "handler")}"')
                toml_lines.append(f'type = "{h.get("type", "")}"')
                if "input_channel" in h:
                    toml_lines.append(f'input_channel = "{h["input_channel"]}"')
                if "input_channels" in h:
                    in_ch = h["input_channels"]
                    if isinstance(in_ch, list):
                        in_ch_str = ",".join(str(c) for c in in_ch)
                    else:
                        in_ch_str = str(in_ch)
                    toml_lines.append(f'input_channels = "{in_ch_str}"')
                if "output_channel" in h:
                    toml_lines.append(f'output_channel = "{h["output_channel"]}"')
                if "output_channels" in h:
                    out_ch = h["output_channels"]
                    if isinstance(out_ch, list):
                        out_ch_str = ",".join(str(c) for c in out_ch)
                    else:
                        out_ch_str = str(out_ch)
                    toml_lines.append(f'output_channels = "{out_ch_str}"')

                h_cfg = h.get("config", {})
                if h_cfg:
                    toml_lines.append("[handlers.config]")
                    for ck, cv in h_cfg.items():
                        if isinstance(cv, bool):
                            toml_lines.append(f'"{ck}" = {str(cv).lower()}')
                        elif isinstance(cv, (int, float)):
                            toml_lines.append(f'"{ck}" = {cv}')
                        else:
                            toml_lines.append(f'"{ck}" = "{cv}"')

            toml_lines.append("")
            target_path.write_text("\n".join(toml_lines), encoding="utf-8")
            return self._send_json(200, {"status": "ok", "path": str(target_path)})

        if clean_path == "/api/chain/run":
            payload = self._read_json_body()
            # Save chain to temp file and launch chain_runner
            tmp_chain = Path(tempfile.mktemp(prefix="builder_chain_", suffix=".toml"))
            payload_sim = dict(payload)
            payload_sim["backend"] = "sim"

            toml_lines = [
                'backend = "sim"',
                "",
                "[config]",
            ]
            for k, v in payload_sim.get("config", {}).items():
                toml_lines.append(f'"{k}" = "{v}"')

            for h in payload_sim.get("handlers", []):
                toml_lines.append("")
                toml_lines.append("[[handlers]]")
                toml_lines.append(f'name = "{h.get("name")}"')
                toml_lines.append(f'type = "{h.get("type")}"')
                if "input_channel" in h:
                    toml_lines.append(f'input_channel = "{h["input_channel"]}"')
                if "input_channels" in h:
                    in_ch = h["input_channels"]
                    if isinstance(in_ch, list):
                        in_ch_str = ",".join(str(c) for c in in_ch)
                    else:
                        in_ch_str = str(in_ch)
                    toml_lines.append(f'input_channels = "{in_ch_str}"')
                if "output_channel" in h:
                    toml_lines.append(f'output_channel = "{h["output_channel"]}"')
                if "output_channels" in h:
                    out_ch = h["output_channels"]
                    if isinstance(out_ch, list):
                        out_ch_str = ",".join(str(c) for c in out_ch)
                    else:
                        out_ch_str = str(out_ch)
                    toml_lines.append(f'output_channels = "{out_ch_str}"')
                h_cfg = h.get("config", {})
                if h_cfg:
                    toml_lines.append("[handlers.config]")
                    for ck, cv in h_cfg.items():
                        toml_lines.append(f'"{ck}" = "{cv}"')
            toml_lines.append("")
            tmp_chain.write_text("\n".join(toml_lines), encoding="utf-8")

            try:
                self.manager.start_chain(
                    config_path_str=str(tmp_chain),
                    backend="sim",
                    pcap_str=None,
                    http_port=self.http_port,
                )
                return self._send_json(200, {"status": "ok", "url": "/dashboard/"})
            except Exception as e:
                return self._send_json(500, {"status": "error", "message": str(e)})

        # -------------------------------------------------------------
        # 4. SIMULATION ACTIONS
        # -------------------------------------------------------------
        coord = self.manager.coordinator
        if not coord and clean_path.startswith("/api/simulation/"):
            return self._send_json(400, {"status": "error", "message": "No active simulation coordinator"})

        if clean_path == "/api/simulation/load_packet":
            params = self._read_json_body()
            pkt_id = params.get("id")
            result = coord.load_next_packet(int(pkt_id) if pkt_id is not None else None)
            return self._send_json(200, result)

        if clean_path == "/api/simulation/step":
            params = self._read_json_body()
            auto_load = bool(params.get("autoLoad", False))
            result = coord.step_forward(auto_load=auto_load)
            return self._send_json(200, result)

        if clean_path == "/api/simulation/reset":
            coord.reset()
            return self._send_json(200, {"status": "ok"})

        if clean_path == "/api/simulation/select":
            params = self._read_json_body()
            pkt_id = params.get("id")
            if pkt_id is None:
                return self._send_json(400, {"status": "error", "message": "Missing packet id"})
            result = coord.select_packet(int(pkt_id))
            return self._send_json(200, result)

        if clean_path == "/api/simulation/trigger":
            params = self._read_json_body()
            node_name = params.get("node")
            if not node_name:
                return self._send_json(400, {"status": "error", "message": "Missing node name"})
            result = coord.trigger_generator(str(node_name))
            return self._send_json(200, result)

        self.send_response(404)
        self.end_headers()

    def log_message(self, format, *args):
        pass  # Suppress HTTP spam


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Proxy Web Studio & Unified Control Plane"
    )
    parser.add_argument("--port", type=int, default=8080, help="HTTP server port (default: 8080)")
    parser.add_argument("--host", default="0.0.0.0", help="HTTP server bind address (default: 0.0.0.0)")
    parser.add_argument("--telemetry-port", type=int, default=8125, help="StatsD UDP listener port (default: 8125)")
    parser.add_argument("--telemetry-host", default="0.0.0.0", help="StatsD UDP bind address (default: 0.0.0.0)")
    parser.add_argument("--open", choices=["portal", "builder", "dashboard", "telemetry"], help="Open specific view in browser on startup")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent

    # 1. Initialize Telemetry Collector & Background UDP Listener
    collector = TelemetryCollector()
    telemetry_stop_event = threading.Event()
    telemetry_thread = threading.Thread(
        target=run_listener,
        args=(collector, args.telemetry_host, args.telemetry_port, telemetry_stop_event),
        daemon=True,
    )
    telemetry_thread.start()

    # 2. Initialize Process Manager
    manager = ChainProcessManager(root_dir)

    # 3. Preload Handler Schemas
    schemas = discover_handler_schemas()

    # 4. Configure Unified HTTP Handler
    FrontendStudioHandler.root_dir = root_dir
    FrontendStudioHandler.manager = manager
    FrontendStudioHandler.collector = collector
    FrontendStudioHandler.schemas = schemas
    FrontendStudioHandler.http_port = args.port

    httpd = http.server.ThreadingHTTPServer((args.host, args.port), FrontendStudioHandler)

    base_url = f"http://localhost:{args.port}"
    print("==================================================================")
    print(" 🚀 PROXY UNIFIED WEB STUDIO & CONTROL PLANE")
    print("==================================================================")
    print(f" 🏠 Landing Page Portal:       {base_url}/")
    print(f" 🎨 Visual Chain Builder:     {base_url}/builder/")
    print(f" 🔍 Simulation Visualizer:     {base_url}/dashboard/")
    print(f" 📊 Telemetry & Topology:      {base_url}/telemetry/")
    print(f" 📡 StatsD UDP Listener:       {args.telemetry_host}:{args.telemetry_port}")
    print("==================================================================")
    print(" Press Ctrl+C to shut down cleanly.")
    print("==================================================================")

    if args.open:
        target = f"{base_url}/" if args.open == "portal" else f"{base_url}/{args.open}/"
        try:
            import webbrowser
            webbrowser.open(target)
        except Exception:
            pass

    def handle_exit(sig, frame):
        print("\n[FrontendRunner] Shutting down...")
        manager.stop_chain(timeout=1.5)
        telemetry_stop_event.set()
        threading.Thread(target=httpd.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, handle_exit)
    signal.signal(signal.SIGTERM, handle_exit)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        handle_exit(signal.SIGINT, None)
    finally:
        httpd.server_close()

    return 0


if __name__ == "__main__":
    sys.exit(main())

