"""
schema_validator.py - Dynamic discovery, schema validation, and topology verification for Proxy handler pipelines.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any, Optional


def discover_handler_schemas(
    search_paths: Optional[list[Path | str]] = None,
) -> dict[str, dict[str, Any]]:
    """
    Dynamically scans directories for per-handler schemas (*.schema.json or handler.schema.json)
    without relying on any static monorepo registry.
    """
    discovered: dict[str, dict[str, Any]] = {}

    default_paths: list[Path] = []
    root_dir = Path(__file__).resolve().parent.parent.parent

    # Look into handler source tree first so authoritative source schemas take precedence over stale build artifacts
    handler_root = root_dir / "handler"
    if handler_root.is_dir():
        default_paths.append(handler_root)

    # Look into known build bin directories
    for sub in [
        "build/bin",
        "build/gcc16-release/bin",
        "build/gcc16-debug/bin",
        "build/release/bin",
        "build/debug/bin",
        "bin",
    ]:
        b_dir = root_dir / sub
        if b_dir.is_dir():
            default_paths.append(b_dir)

    paths_to_scan = [Path(p) for p in (search_paths or [])] + default_paths

    for base_path in paths_to_scan:
        if not base_path.exists():
            continue

        if base_path.is_file() and base_path.name.endswith(".schema.json"):
            _try_load_schema(base_path, discovered)
            continue

        # Look for *.schema.json in this directory
        for f in base_path.glob("*.schema.json"):
            _try_load_schema(f, discovered)

        # Look for handler.schema.json in subdirectories (e.g. handler/*/handler.schema.json)
        for f in base_path.glob("*/handler.schema.json"):
            _try_load_schema(f, discovered)

    return discovered


def _try_load_schema(file_path: Path, out_dict: dict[str, dict[str, Any]]) -> None:
    try:
        content = file_path.read_text(encoding="utf-8")
        data = json.loads(content)
        name = data.get("name")
        if name and name not in out_dict:
            out_dict[name] = data
    except Exception:
        pass


def _validate_value(
    val: Any,
    param_meta: dict[str, Any],
    h_name: str,
    path_name: str,
    errors: list[str],
) -> None:
    if val is None or val == "":
        if param_meta.get("required", False):
            errors.append(
                f"Handler '{h_name}' missing required parameter: '{path_name}'"
            )
        return

    param_type = param_meta.get("type", "string")

    if param_type in ("integer", "int"):
        try:
            int_val = int(val)
            min_v = param_meta.get("min")
            max_v = param_meta.get("max")
            if min_v is not None and int_val < min_v:
                errors.append(
                    f"Handler '{h_name}' parameter '{path_name}' value {int_val} < min ({min_v})"
                )
            if max_v is not None and int_val > max_v:
                errors.append(
                    f"Handler '{h_name}' parameter '{path_name}' value {int_val} > max ({max_v})"
                )
        except (ValueError, TypeError):
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' must be an integer, got: {val}"
            )

    elif param_type in ("string", "filepath", "file"):
        str_val = str(val)
        min_len = param_meta.get("min_length") or param_meta.get("minLength")
        max_len = param_meta.get("max_length") or param_meta.get("maxLength")
        if min_len is not None and len(str_val) < min_len:
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' length {len(str_val)} < min_length ({min_len})"
            )
        if max_len is not None and len(str_val) > max_len:
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' length {len(str_val)} > max_length ({max_len})"
            )
        regex_pattern = param_meta.get("pattern")
        if regex_pattern:
            try:
                if not re.search(regex_pattern, str_val):
                    errors.append(
                        f"Handler '{h_name}' parameter '{path_name}' value '{str_val}' does not match pattern: {regex_pattern}"
                    )
            except re.error as e:
                errors.append(
                    f"Handler '{h_name}' parameter '{path_name}' has invalid regex pattern '{regex_pattern}': {e}"
                )

    elif param_type in ("number", "float"):
        try:
            num_val = float(val)
            min_v = param_meta.get("min")
            max_v = param_meta.get("max")
            if min_v is not None and num_val < min_v:
                errors.append(
                    f"Handler '{h_name}' parameter '{path_name}' value {num_val} < min ({min_v})"
                )
            if max_v is not None and num_val > max_v:
                errors.append(
                    f"Handler '{h_name}' parameter '{path_name}' value {num_val} > max ({max_v})"
                )
        except (ValueError, TypeError):
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' must be a number, got: {val}"
            )

    elif param_type in ("boolean", "bool"):
        if not isinstance(val, bool) and str(val).lower() not in (
            "true",
            "false",
            "1",
            "0",
        ):
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' must be boolean, got: {val}"
            )

    elif param_type == "array":
        if not isinstance(val, list):
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' must be a list/array, got: {type(val).__name__}"
            )
        else:
            item_schema = param_meta.get("items")
            if item_schema:
                for idx, item in enumerate(val):
                    _validate_value(
                        item, item_schema, h_name, f"{path_name}[{idx}]", errors
                    )

    elif param_type == "object":
        if not isinstance(val, dict):
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' must be an object/dict, got: {type(val).__name__}"
            )
        else:
            props = param_meta.get("properties", {})
            for p_key, p_meta in props.items():
                p_val = val.get(p_key)
                # Check ui:visible_if condition if defined
                visible_if = p_meta.get("ui:visible_if")
                if visible_if and isinstance(visible_if, dict):
                    condition_met = all(
                        str(val.get(k)) == str(v) for k, v in visible_if.items()
                    )
                    if not condition_met:
                        # Field is conditionally disabled/hidden; skip validation if not present
                        if p_val is None or p_val == "":
                            continue

                sub_path = f"{path_name}.{p_key}" if path_name else p_key
                _validate_value(p_val, p_meta, h_name, sub_path, errors)

    if "enum" in param_meta:
        allowed = param_meta["enum"]
        if str(val) not in [str(a) for a in allowed]:
            errors.append(
                f"Handler '{h_name}' parameter '{path_name}' value '{val}' not in allowed: {allowed}"
            )


def validate_handler_instance(
    handler_spec: dict[str, Any], schema: dict[str, Any]
) -> list[str]:
    """
    Validates a single handler configuration against its schema.
    Returns a list of error messages (empty if valid).
    """
    errors: list[str] = []
    h_name = handler_spec.get("name", "unnamed")

    # 1. Port checks
    ports_spec = schema.get("ports", {})
    input_ports = ports_spec.get("inputs", [])
    output_ports = ports_spec.get("outputs", [])

    has_inputs_in_schema = len(input_ports) > 0
    has_outputs_in_schema = len(output_ports) > 0

    has_input_defined = "input_channel" in handler_spec or "input_channels" in handler_spec
    has_output_defined = "output_channel" in handler_spec or "output_channels" in handler_spec

    if has_inputs_in_schema and not has_input_defined:
        port_names = [p["name"] for p in input_ports]
        errors.append(f"Handler '{h_name}' missing required input binding: {', '.join(port_names)}")

    if has_outputs_in_schema and not has_output_defined:
        port_names = [p["name"] for p in output_ports]
        errors.append(f"Handler '{h_name}' missing required output binding: {', '.join(port_names)}")

    # 2. Parameter checks
    params_schema = schema.get("parameters", {})
    instance_config = handler_spec.get("config", {})

    for param_key, param_meta in params_schema.items():
        val = instance_config.get(param_key)
        # Check if alternative embedded content key is provided (e.g. embed_param: "xml.xsd_content")
        embed_param = param_meta.get("embed_param")
        if val is None or val == "":
            if embed_param and instance_config.get(embed_param):
                # Embedded content was provided instead of the file path
                val = instance_config.get(embed_param)
        _validate_value(val, param_meta, h_name, param_key, errors)

    return errors


def validate_chain_topology(
    chain_def: dict[str, Any],
    schemas: Optional[dict[str, dict[str, Any]]] = None,
) -> dict[str, Any]:
    """
    Validates a full chain definition (global config, handlers, channel connections).
    Returns a dict with 'valid' (bool), 'errors' (list[str]), and 'warnings' (list[str]).
    """
    if schemas is None:
        schemas = discover_handler_schemas()

    errors: list[str] = []
    warnings: list[str] = []

    backend = chain_def.get("backend")
    if not backend:
        errors.append("Chain definition is missing 'backend' (e.g. 'shm', 'uds', 'socket', 'file', 'sim')")

    handlers = chain_def.get("handlers", [])
    if not handlers:
        errors.append("Chain definition contains no handlers in 'handlers'")
        return {"valid": False, "errors": errors, "warnings": warnings}

    handler_names = set()
    output_channels: dict[str, list[str]] = {}  # channel -> producing handlers
    input_channels: dict[str, list[str]] = {}   # channel -> consuming handlers

    for idx, h in enumerate(handlers):
        name = h.get("name")
        if not name:
            errors.append(f"Handler at index {idx} has no 'name'")
            name = f"handler_{idx}"
        elif name in handler_names:
            errors.append(f"Duplicate handler name '{name}' detected in chain")
        handler_names.add(name)

        h_type = h.get("type")
        if not h_type:
            errors.append(f"Handler '{name}' is missing 'type'")
            continue

        schema = schemas.get(h_type)
        if not schema:
            warnings.append(f"No schema found for handler type '{h_type}'. Skipping schema field validation.")
        else:
            inst_errors = validate_handler_instance(h, schema)
            errors.extend(inst_errors)

        # Collect produced channels
        if "output_channel" in h and h["output_channel"]:
            ch = str(h["output_channel"]).strip()
            output_channels.setdefault(ch, []).append(name)
        if "output_channels" in h and h["output_channels"]:
            chs = h["output_channels"]
            if isinstance(chs, str):
                for ch in chs.split(","):
                    c_clean = ch.strip()
                    if c_clean:
                        output_channels.setdefault(c_clean, []).append(name)
            elif isinstance(chs, list):
                for ch in chs:
                    c_clean = str(ch).strip()
                    if c_clean:
                        output_channels.setdefault(c_clean, []).append(name)

        # Collect consumed channels
        if "input_channel" in h and h["input_channel"]:
            ch = str(h["input_channel"]).strip()
            input_channels.setdefault(ch, []).append(name)
        if "input_channels" in h and h["input_channels"]:
            chs = h["input_channels"]
            if isinstance(chs, str):
                for ch in chs.split(","):
                    c_clean = ch.strip()
                    if c_clean:
                        input_channels.setdefault(c_clean, []).append(name)
            elif isinstance(chs, list):
                for ch in chs:
                    c_clean = str(ch).strip()
                    if c_clean:
                        input_channels.setdefault(c_clean, []).append(name)

    # Channel name pattern check (alphanumeric, dots, dashes, underscores)
    channel_name_regex = re.compile(r"^[a-zA-Z0-9_.-]+$")
    all_channels = set(output_channels.keys()).union(set(input_channels.keys()))
    for ch in all_channels:
        if not channel_name_regex.match(ch):
            errors.append(
                f"Channel wire identifier '{ch}' contains invalid characters. Must match pattern ^[a-zA-Z0-9_.-]+$"
            )

    # Check for duplicate output channels across handlers (each connection wire / output channel must be uniquely named)
    for ch, producers in output_channels.items():
        if len(producers) > 1:
            errors.append(
                f"Channel wire '{ch}' is produced by multiple handlers ({producers}). Connection wires must be uniquely named."
            )

    # Check for disconnected channels
    for ch, consumers in input_channels.items():
        if ch not in output_channels:
            warnings.append(
                f"Input channel '{ch}' consumed by {consumers} has no producing handler in this chain"
            )

    for ch, producers in output_channels.items():
        if ch not in input_channels:
            warnings.append(
                f"Output channel '{ch}' produced by {producers} is not consumed by any handler in this chain"
            )

    return {
        "valid": len(errors) == 0,
        "errors": errors,
        "warnings": warnings,
    }

