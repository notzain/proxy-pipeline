"""Sanitized chain topology projection and publication for live telemetry."""

from __future__ import annotations

import json
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from typing import Any


MAX_TOPOLOGY_HANDLERS = 256
MAX_TOPOLOGY_EDGES = 1024
MAX_TOPOLOGY_BODY_BYTES = 64 * 1024


def _channel_names(value: Any) -> list[str]:
    if isinstance(value, str):
        values = value.split(",")
    elif isinstance(value, list):
        values = value
    else:
        return []

    return [str(item).strip() for item in values if str(item).strip()]


def _flatten_config(config: Any, prefix: str = "") -> dict[str, Any]:
    if not isinstance(config, dict):
        return {}

    flattened: dict[str, Any] = {}
    for key, value in config.items():
        full_key = f"{prefix}.{key}" if prefix else str(key)
        if isinstance(value, dict):
            flattened.update(_flatten_config(value, full_key))
        else:
            flattened[full_key] = value
    return flattened


def project_chain_topology(chain_config: dict[str, Any], chain_id: str) -> dict[str, Any]:
    if not isinstance(chain_id, str) or not chain_id.strip() or len(chain_id) > 128:
        raise ValueError("Chain identifier must be a non-empty string of at most 128 characters")
    handlers = chain_config.get("handlers")
    if not isinstance(handlers, list) or not handlers:
        raise ValueError("Chain topology requires a non-empty handlers list")
    if len(handlers) > MAX_TOPOLOGY_HANDLERS:
        raise ValueError(f"Chain topology exceeds {MAX_TOPOLOGY_HANDLERS} handlers")

    global_config = _flatten_config(chain_config.get("config", {}))
    global_prefix = str(global_config.get("telemetry.prefix", "proxy"))
    if len(global_prefix) > 128:
        raise ValueError("Telemetry metric prefix exceeds 128 characters")
    nodes: list[dict[str, str | list[str]]] = []
    node_names: set[str] = set()
    outputs_by_channel: dict[str, list[str]] = {}
    inputs_by_channel: dict[str, list[str]] = {}

    for index, handler in enumerate(handlers):
        if not isinstance(handler, dict):
            raise ValueError(f"Handler at index {index} must be an object")

        handler_type = handler.get("type")
        if not isinstance(handler_type, str) or not handler_type.strip():
            raise ValueError(f"Handler at index {index} is missing a valid type")
        if len(handler_type.strip()) > 128:
            raise ValueError(f"Handler type at index {index} exceeds 128 characters")

        name = handler.get("name") or f"{handler_type}_{index + 1}"
        if not isinstance(name, str) or not name.strip():
            raise ValueError(f"Handler at index {index} has an invalid name")
        name = name.strip()
        if len(name) > 128:
            raise ValueError(f"Handler name at index {index} exceeds 128 characters")
        if name in node_names:
            raise ValueError(f"Duplicate handler name: {name}")
        node_names.add(name)

        input_channels = _channel_names(handler.get("input_channels"))
        input_channels.extend(_channel_names(handler.get("input_channel")))
        output_channels = _channel_names(handler.get("output_channels"))
        output_channels.extend(_channel_names(handler.get("output_channel")))

        handler_config = _flatten_config(handler.get("config", {}))
        for key, value in handler_config.items():
            if key.startswith("route.rule.") and key.endswith(".target"):
                output_channels.extend(_channel_names(value))
        output_channels.extend(_channel_names(handler_config.get("route.default_output")))

        input_channels = list(dict.fromkeys(input_channels))
        output_channels = list(dict.fromkeys(output_channels))
        if any(len(channel) > 128 for channel in input_channels + output_channels):
            raise ValueError(f"Handler '{name}' has a channel name exceeding 128 characters")

        for channel in input_channels:
            inputs_by_channel.setdefault(channel, []).append(name)
        for channel in output_channels:
            outputs_by_channel.setdefault(channel, []).append(name)

        metric_prefix = f"{global_prefix}.{name}" if global_prefix else name
        if len(metric_prefix) > 128:
            raise ValueError(f"Telemetry metric prefix for handler '{name}' exceeds 128 characters")
        nodes.append({
            "name": name,
            "type": handler_type.strip(),
            "inputChannels": input_channels,
            "outputChannels": output_channels,
            "metricPrefix": metric_prefix,
        })

    edges: list[dict[str, str]] = []
    unresolved_channels: list[str] = []
    for channel, producers in outputs_by_channel.items():
        consumers = inputs_by_channel.get(channel, [])
        if not consumers:
            unresolved_channels.append(channel)
            continue
        for producer in producers:
            for consumer in consumers:
                if producer == consumer:
                    continue
                edges.append({"from": producer, "to": consumer, "channel": channel})
                if len(edges) > MAX_TOPOLOGY_EDGES:
                    raise ValueError(f"Chain topology exceeds {MAX_TOPOLOGY_EDGES} edges")

    topology = {
        "chainId": chain_id,
        "backend": str(chain_config.get("backend", "unknown")),
        "nodes": nodes,
        "edges": edges,
        "unresolvedOutputChannels": unresolved_channels,
    }
    body = json.dumps(topology, separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_TOPOLOGY_BODY_BYTES:
        raise ValueError(f"Projected topology exceeds {MAX_TOPOLOGY_BODY_BYTES} bytes")
    return topology


def publish_topology(collector_url: str, topology: dict[str, Any], timeout: float = 2.0) -> None:
    endpoint = collector_url.rstrip("/")
    if not endpoint.endswith("/api/topology"):
        endpoint += "/api/topology"

    body = json.dumps(topology, separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_TOPOLOGY_BODY_BYTES:
        raise ValueError(f"Projected topology exceeds {MAX_TOPOLOGY_BODY_BYTES} bytes")

    request = Request(
        endpoint,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            if response.status < 200 or response.status >= 300:
                raise RuntimeError(f"Collector returned HTTP {response.status}")
    except HTTPError as error:
        raise RuntimeError(f"Collector rejected topology (HTTP {error.code})") from error
    except URLError as error:
        raise RuntimeError(f"Could not publish topology to collector: {error.reason}") from error
