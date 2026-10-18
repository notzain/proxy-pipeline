#!/usr/bin/env python3
"""
chain_runner.py - Orchestrator and runner for Proxy handler chains.

Loads a TOML or JSON chain configuration specifying:
  - backend: The transport backend to use for all handlers in the chain ('file', 'socket', etc.)
  - config: Global or backend-level settings (file.path, socket.host, etc.)
  - handlers: Ordered list of handlers with their individual channel mappings and parameters.

The runner:
  1. Resolves binaries based on <handler>_<backend> in the build directory.
  2. Generates dedicated runtime configuration files for each handler instance.
  3. Launches each handler process with proper I/O logging and tracking.
  4. Manages lifecycle: propagates SIGINT/Ctrl+C to cleanly terminate all processes.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, List

try:
    import tomllib
except ImportError:
    tomllib = None  # type: ignore


def load_chain_config(config_path: Path) -> dict[str, Any]:
    if not config_path.is_file():
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    content = config_path.read_text(encoding="utf-8")
    if config_path.suffix.lower() == ".json":
        return json.loads(content)
    elif config_path.suffix.lower() == ".toml":
        if tomllib is None:
            raise RuntimeError("Python 3.11+ is required for built-in TOML support.")
        return tomllib.loads(content)
    else:
        # Try TOML first, fall back to JSON
        if tomllib:
            try:
                return tomllib.loads(content)
            except Exception:
                pass
        return json.loads(content)


def find_bin_dir(root_dir: Path, requested_bin_dir: str | None = None) -> Path:
    if requested_bin_dir:
        candidate = Path(requested_bin_dir)
        if candidate.is_dir():
            return candidate

    # Standard preset build locations
    search_paths = [
        root_dir / "build" / "bin",
        root_dir / "build" / "gcc16-debug" / "bin",
        root_dir / "build" / "debug" / "bin",
        root_dir / "build" / "release" / "bin",
        root_dir / "bin",
    ]
    for p in search_paths:
        if p.is_dir():
            return p

    raise FileNotFoundError(
        f"Could not find build bin directory. Looked in: {[str(p) for p in search_paths]}. "
        "Please build the project first or provide --bin-dir."
    )


def generate_handler_config(
    global_config: dict[str, Any],
    backend_name: str,
    handler_spec: dict[str, Any],
) -> str:
    """
    Generates key-value proxy.conf content for a handler combining:
      1. Global settings (e.g. backend transport configuration)
      2. Handler-specific settings (channels, parameters)
    """
    lines: list[str] = [
        f"# Automatically generated configuration for handler: {handler_spec.get('name', 'handler')}",
        f"# Backend: {backend_name}",
        "",
        "# Global / backend settings",
    ]

    # Add global configuration
    for key, val in global_config.items():
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    # Add handler-specific overrides and settings
    lines.append("")
    lines.append("# Handler-specific settings")

    # Convenience shortcuts:
    # input_channel -> dummy.channel / print.channel / split.input_channel
    # input_channels -> merge.input_channels
    # output_channel -> dummy.channel / merge.output_channel
    # output_channels -> split.output_channels
    if "input_channel" in handler_spec:
        in_ch = handler_spec["input_channel"]
        lines.append(f"dummy.channel={in_ch}")
        lines.append(f"print.channel={in_ch}")
        lines.append(f"split.input_channel={in_ch}")
        lines.append(f"xml.input_channel={in_ch}")
    if "input_channels" in handler_spec:
        in_chs = handler_spec["input_channels"]
        if isinstance(in_chs, list):
            lines.append(f"merge.input_channels={','.join(in_chs)}")
        else:
            lines.append(f"merge.input_channels={in_chs}")
    if "output_channel" in handler_spec:
        out_ch = handler_spec["output_channel"]
        lines.append(f"dummy.channel={out_ch}")
        lines.append(f"merge.output_channel={out_ch}")
        lines.append(f"xml.output_channel={out_ch}")
    if "output_channels" in handler_spec:
        out_chs = handler_spec["output_channels"]
        if isinstance(out_chs, list):
            lines.append(f"split.output_channels={','.join(out_chs)}")
        else:
            lines.append(f"split.output_channels={out_chs}")

    # Direct config key-values inside handler spec
    params = handler_spec.get("config", {})
    for key, val in params.items():
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    lines.append("")
    return "\n".join(lines)


def run_chain(config_path: Path, bin_dir_override: str | None = None, work_dir_override: str | None = None) -> int:
    chain_def = load_chain_config(config_path)

    backend = chain_def.get("backend")
    if not backend:
        raise ValueError("Configuration must specify 'backend' (e.g. 'file' or 'socket')")

    handlers: list[dict[str, Any]] = chain_def.get("handlers", [])
    if not handlers:
        raise ValueError("Configuration must contain at least one handler in 'handlers'")

    root_dir = Path(__file__).resolve().parent.parent
    bin_dir = find_bin_dir(root_dir, bin_dir_override)
    print(f"[ChainRunner] Using binaries directory: {bin_dir}")
    print(f"[ChainRunner] Target backend: {backend}")

    # Create workspace for temporary generated configurations and logs (/tmp instead of /var/folders)
    if work_dir_override:
        work_dir = Path(work_dir_override)
        work_dir.mkdir(parents=True, exist_ok=True)
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="proxy_chain_", dir="/tmp"))

    print(f"[ChainRunner] Working directory for runtime configs: {work_dir}")

    global_config = chain_def.get("config", {})

    processes: list[tuple[str, subprocess.Popen, Path]] = []
    shutting_down = False

    def handle_signal(sig, frame):
        nonlocal shutting_down
        if shutting_down:
            return
        shutting_down = True
        print(f"\n[ChainRunner] Caught signal {sig}, terminating chain...")
        for name, proc, _ in reversed(processes):
            if proc.poll() is None:
                print(f"[ChainRunner] Stopping {name} (PID {proc.pid})...")
                proc.send_signal(signal.SIGINT)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        # Start each handler
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

            # Write generated config file
            conf_file = work_dir / f"{handler_name}.conf"
            conf_content = generate_handler_config(global_config, backend, handler_spec)
            conf_file.write_text(conf_content, encoding="utf-8")

            print(f"[ChainRunner] Launching {handler_name} [{binary_name}] with config: {conf_file}")
            proc = subprocess.Popen(
                [str(binary_path), str(conf_file)],
                cwd=str(root_dir),
            )
            processes.append((handler_name, proc, conf_file))
            time.sleep(0.1)

        print(f"\n[ChainRunner] All {len(processes)} handler(s) started successfully. Press Ctrl+C to terminate.")

        # Monitor loop
        while not shutting_down:
            for name, proc, _ in processes:
                exit_code = proc.poll()
                if exit_code is not None:
                    print(f"[ChainRunner] Handler {name} exited with code {exit_code}")
                    shutting_down = True
                    break
            time.sleep(0.5)

    except Exception as e:
        print(f"[ChainRunner] Error: {e}", file=sys.stderr)
        shutting_down = True
    finally:
        # Ensure all processes are stopped
        for name, proc, _ in reversed(processes):
            if proc.poll() is None:
                proc.send_signal(signal.SIGINT)
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    print(f"[ChainRunner] Force killing {name} (PID {proc.pid})...")
                    proc.kill()
                    proc.wait()

        print("[ChainRunner] All handlers stopped cleanly.")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure and run chains of Proxy handlers across any backend."
    )
    parser.add_argument(
        "config",
        type=Path,
        help="Path to chain configuration file (.toml or .json)",
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

    args = parser.parse_args()
    return run_chain(args.config, args.bin_dir, args.work_dir)


if __name__ == "__main__":
    sys.exit(main())

