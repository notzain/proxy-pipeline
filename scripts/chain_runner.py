#!/usr/bin/env python3
"""
chain_runner.py - Orchestrator and runner for Proxy handler chains.

Supports:
- Running multi-handler chains across any backend (file, socket, uds, shm, sim).
- Interactive Visual Simulation mode with real-time web dashboard.
- Headless automated execution for CI / integration testing.
- Exporting deterministic execution trace artifacts.
- Semantic diffing / asserting execution traces against expected reference artifacts.
"""

import argparse
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any, Optional

from proxy_runner import (
    InteractiveSimulationCoordinator,
    load_handler_descriptor,
    PcapPacket,
    PcapReader,
    TraceDiffError,
    TraceManager,
    find_bin_dir,
    generate_handler_config,
    load_chain_config,
    project_chain_topology,
    publish_topology,
    start_dashboard_server,
)


def _signal_process_group(proc: subprocess.Popen, sig: int) -> None:
    if proc.poll() is not None:
        return

    try:
        os.killpg(proc.pid, sig)
    except ProcessLookupError:
        pass
    except PermissionError:
        if proc.poll() is None:
            try:
                proc.send_signal(sig)
            except ProcessLookupError:
                pass


def run_chain(
    config_path: Path,
    bin_dir_override: str | None = None,
    work_dir_override: str | None = None,
    backend_override: str | None = None,
    pcap_path: str | None = None,
    dashboard_port: int | None = 8090,
    headless: bool = False,
    export_trace_path: str | None = None,
    assert_trace_path: str | None = None,
    telemetry_url: str | None = None,
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
        owns_work_dir = False
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="proxy_chain_", dir="/tmp"))
        owns_work_dir = True

    print(f"[ChainRunner] Working directory for runtime configs: {work_dir}")

    global_config = chain_def.get("config", {})
    telemetry_config = global_config.get("telemetry", {}) if isinstance(global_config.get("telemetry"), dict) else {}
    collector_url = (
        telemetry_url
        or global_config.get("telemetry.collector_url")
        or telemetry_config.get("collector_url")
    )

    # Detect if this chain has any external ingress input channels.
    has_ingress_channel = False
    for h in handlers:
        desc = load_handler_descriptor(h.get("type", ""), [bin_dir] if bin_dir else None)
        if desc.is_external_ingress:
            has_ingress_channel = True
            break
        in_chs = h.get("input_channels") or ([h.get("input_channel")] if h.get("input_channel") else [])
        for ch in in_chs:
            is_internal = any(ch == other.get("output_channel") for other in handlers if other != h)
            if not is_internal:
                has_ingress_channel = True
                break
        if has_ingress_channel:
            break

    pcap_packets: list[PcapPacket] = []
    if pcap_path:
        if not has_ingress_channel:
            print(f"[ChainRunner] ⚠️ NOTICE: Chain '{config_path.name}' is driven solely by generators and has no external ingress channel.")
            print(f"[ChainRunner] ⚠️ PCAP '{pcap_path}' is ignored. Messages must be triggered via generator nodes.")
        else:
            pcap_packets = PcapReader.read_packets(pcap_path)
            print(f"[ChainRunner] Preloaded {len(pcap_packets)} packet(s) from PCAP: {pcap_path}")

    coordinator: Optional[InteractiveSimulationCoordinator] = None
    http_server_instance = None

    if backend == "sim" or headless:
        coordinator = InteractiveSimulationCoordinator(chain_def, pcap_packets)
        if backend == "sim" and dashboard_port and not headless:
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
    topology: dict[str, Any] | None = None
    next_topology_publish_at = 0.0
    topology_publish_failed = False
    topology_published = False

    def publish_topology_if_due(force: bool = False) -> None:
        nonlocal topology, next_topology_publish_at, topology_publish_failed, topology_published
        if not collector_url:
            return

        now = time.monotonic()
        if not force and now < next_topology_publish_at:
            return
        next_topology_publish_at = now + 5.0

        try:
            if topology is None:
                topology = project_chain_topology(chain_def, config_path.stem)
            publish_topology(str(collector_url), topology)
        except (TypeError, ValueError, RuntimeError) as ex:
            if not topology_publish_failed:
                print(f"[ChainRunner] WARNING: Could not publish chain topology: {ex}", file=sys.stderr)
            topology_publish_failed = True
            return

        if not topology_published or topology_publish_failed:
            print("[ChainRunner] Published chain topology to telemetry collector.")
        topology_published = True
        topology_publish_failed = False

    def handle_signal(sig, frame):
        nonlocal shutting_down
        if shutting_down:
            return
        shutting_down = True
        print(f"\n[ChainRunner] Caught signal {sig}, terminating chain...")
        for name, proc, _ in reversed(processes):
            if proc.poll() is None:
                print(f"[ChainRunner] Stopping {name} (PID {proc.pid})...")
                _signal_process_group(proc, signal.SIGINT)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGHUP, handle_signal)

    exit_code = 0
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
                start_new_session=True,
            )
            processes.append((handler_name, proc, conf_file))

        if coordinator and backend == "sim":
            coordinator.wait_for_handlers_ready(timeout_sec=5.0)
        else:
            time.sleep(0.2)

        print(f"\n[ChainRunner] All {len(processes)} handler(s) started and ready.")
        publish_topology_if_due(force=True)

        if headless:
            if not coordinator:
                raise RuntimeError("Headless mode requires simulation coordinator ('sim' backend)")

            print(f"[ChainRunner] Running in headless mode: advancing {len(pcap_packets)} packet(s)...")
            start_sim = time.time()
            recorded_events = coordinator.run_to_completion()
            elapsed_sec = time.time() - start_sim
            print(f"[ChainRunner] Simulation finished in {elapsed_sec:.2f}s with {len(recorded_events)} events.")

            if export_trace_path:
                print(f"[ChainRunner] Exporting execution trace to: {export_trace_path}")
                packets_meta = [p.to_dict() for p in coordinator.pcap_packets]
                TraceManager.save_trace(
                    recorded_events,
                    export_trace_path,
                    chain_config=chain_def,
                    packets=packets_meta,
                )
                print(f"[ChainRunner] Successfully saved trace artifact ({len(recorded_events)} events with embedded topology and packets).")

            if assert_trace_path:
                print(f"[ChainRunner] Asserting execution trace against expected reference: {assert_trace_path}")
                try:
                    TraceManager.assert_trace_match(recorded_events, assert_trace_path)
                    print("[ChainRunner] ✅ SUCCESS: Execution trace matches expected reference perfectly!")
                except TraceDiffError as ex:
                    print(f"\n[ChainRunner] ❌ ASSERTION FAILURE:\n{ex}", file=sys.stderr)
                    exit_code = 1
        else:
            if backend == "sim":
                print("[ChainRunner] Packets are paused and ready in memory. Click Step Forward or Play in the dashboard.")
            else:
                print("[ChainRunner] Live chain running. Waiting for input on configured channels or generator output.")
            while not shutting_down:
                publish_topology_if_due()
                for name, proc, _ in processes:
                    code = proc.poll()
                    if code is not None:
                        print(f"[ChainRunner] Handler {name} exited with code {code}")
                        shutting_down = True
                        break
                time.sleep(0.5)

    except Exception as e:
        print(f"[ChainRunner] Error: {e}", file=sys.stderr)
        exit_code = 1
    finally:
        if coordinator:
            coordinator.close()
        if http_server_instance:
            try:
                http_server_instance.shutdown()
            except Exception:
                pass

        for name, proc, _ in reversed(processes):
            _signal_process_group(proc, signal.SIGINT)
            if proc.poll() is None:
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    print(f"[ChainRunner] Force killing {name} (PID {proc.pid})...")
                    _signal_process_group(proc, signal.SIGKILL)
                    proc.wait()

        print("[ChainRunner] All handlers stopped cleanly.")
        if owns_work_dir:
            try:
                shutil.rmtree(work_dir)
            except OSError as ex:
                print(f"[ChainRunner] WARNING: Could not remove runtime config directory {work_dir}: {ex}", file=sys.stderr)

    return exit_code


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure and run chains of Proxy handlers across any backend."
    )
    parser.add_argument(
        "config",
        nargs="?",
        type=Path,
        default=None,
        help="Path to chain configuration file (.toml or .json)",
    )
    parser.add_argument(
        "--view-trace",
        type=Path,
        default=None,
        help="Open dashboard in standalone trace inspection mode for the given trace artifact",
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
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Run simulation to completion headlessly without starting dashboard UI",
    )
    parser.add_argument(
        "--export-trace",
        type=str,
        default=None,
        help="Path to export normalized execution trace JSON artifact",
    )
    parser.add_argument(
        "--assert-trace",
        type=str,
        default=None,
        help="Path to expected reference execution trace JSON to assert against",
    )
    parser.add_argument(
        "--telemetry-url",
        type=str,
        default=None,
        help="Telemetry collector URL for publishing sanitized chain topology (overrides telemetry.collector_url)",
    )

    args = parser.parse_args()

    # Standalone trace viewer mode (no chain config or backend required)
    if args.view_trace:
        from proxy_runner.trace_viewer import run_trace_viewer
        return run_trace_viewer(args.view_trace, port=args.port)

    if args.config and args.config.name.endswith(".trace.json"):
        from proxy_runner.trace_viewer import run_trace_viewer
        return run_trace_viewer(args.config, port=args.port)

    if not args.config:
        parser.error("the following arguments are required: config (or use --view-trace)")

    # Default to 'sim' backend if running headless, with --pcap, or with trace operations
    backend_override = args.backend
    if (args.headless or args.pcap or args.export_trace or args.assert_trace) and not backend_override:
        backend_override = "sim"

    return run_chain(
        args.config,
        bin_dir_override=args.bin_dir,
        work_dir_override=args.work_dir,
        backend_override=backend_override,
        pcap_path=args.pcap,
        dashboard_port=args.port if not args.headless else None,
        headless=args.headless or bool(args.export_trace or args.assert_trace),
        export_trace_path=args.export_trace,
        assert_trace_path=args.assert_trace,
        telemetry_url=args.telemetry_url,
    )


if __name__ == "__main__":
    sys.exit(main())
