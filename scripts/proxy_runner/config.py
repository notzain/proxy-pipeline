"""
Chain and handler configuration management and binary discovery.
"""

import json
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:
    try:
        import tomli as tomllib
    except ModuleNotFoundError:
        import toml as tomllib


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

    search_paths = [
        root_dir / "build" / "bin",
        root_dir / "build" / "gcc16-release" / "bin",
        root_dir / "build" / "gcc16-debug" / "bin",
        root_dir / "build" / "release" / "bin",
        root_dir / "build" / "debug" / "bin",
        root_dir / "bin",
    ]
    for p in search_paths:
        if p.is_dir():
            return p

    raise FileNotFoundError(
        f"Could not find build bin directory. Looked in: {[str(p) for p in search_paths]}. "
        "Please build the project first or provide --bin-dir."
    )


def flatten_dict(d: dict[str, Any], prefix: str = "") -> list[tuple[str, Any]]:
    items: list[tuple[str, Any]] = []
    for k, v in d.items():
        full_key = f"{prefix}.{k}" if prefix else str(k)
        if isinstance(v, dict):
            items.extend(flatten_dict(v, full_key))
        else:
            items.append((full_key, v))
    return items


def generate_handler_config(
    global_config: dict[str, Any],
    backend_name: str,
    handler_spec: dict[str, Any],
) -> str:
    lines: list[str] = [
        f"# Automatically generated configuration for handler: {handler_spec.get('name', 'handler')}",
        f"# Backend: {backend_name}",
        "",
        "# Global / backend settings",
    ]

    for key, val in flatten_dict(global_config):
        if backend_name == "sim" and ("channel." in key or "shm." in key or "file." in key):
            continue  # Let SimBackend use default /tmp/proxy_sim sockets in simulation mode
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    lines.append("")
    lines.append("# Handler-specific settings")

    handler_name = handler_spec.get("name")
    if handler_name:
        global_prefix = (
            global_config.get("telemetry", {}).get("prefix", "proxy")
            if isinstance(global_config.get("telemetry"), dict)
            else global_config.get("telemetry.prefix", "proxy")
        )
        lines.append(f"telemetry.prefix={global_prefix}.{handler_name}")
        lines.append(f"sim.handler_name={handler_name}")

    if "input_channel" in handler_spec:
        in_ch = handler_spec["input_channel"]
        lines.append(f"channel.input={in_ch}")
        lines.append(f"egress.input_channel={in_ch}")
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
        lines.append(f"channel.output={out_ch}")
        lines.append(f"ingress.output_channel={out_ch}")
        lines.append(f"dummy.channel={out_ch}")
        lines.append(f"merge.output_channel={out_ch}")
        lines.append(f"xml.output_channel={out_ch}")
    if "output_channels" in handler_spec:
        out_chs = handler_spec["output_channels"]
        if isinstance(out_chs, list):
            lines.append(f"split.output_channels={','.join(out_chs)}")
        else:
            lines.append(f"split.output_channels={out_chs}")

    params = handler_spec.get("config", {})
    for key, val in flatten_dict(params):
        if backend_name == "sim" and key == "dummy.delay_ms":
            # In simulation mode, pause generator timers so they only emit on manual/step trigger
            lines.append(f"{key}=999999999")
            continue
        if isinstance(val, bool):
            lines.append(f"{key}={str(val).lower()}")
        else:
            lines.append(f"{key}={val}")

    if backend_name == "sim" and handler_spec.get("type") == "dummy_send" and "dummy.delay_ms" not in params:
        # Default pause for dummy_send in sim mode
        lines.append("dummy.delay_ms=999999999")

    lines.append("")
    return "\n".join(lines)

