"""
Protocol metadata catalog provider for REST API and LSP completion engines.
Reflects registered C++ metadata schemas and dynamically aggregates handler dslContributions.
"""

from __future__ import annotations
from typing import Any


def get_metadata_catalog_definitions() -> dict[str, Any]:
    """
    Dynamically discovers handler schemas and aggregates their declared `dslContributions`.
    This keeps the proxy architecture fully decentralized: each handler exposes what it contributes.
    """
    from .schema_validator import discover_handler_schemas

    protocols: list[dict[str, Any]] = []
    common_fields: list[dict[str, Any]] = []

    schemas = discover_handler_schemas()
    for h_type, s in schemas.items():
        contrib = s.get("dslContributions", {})
        proto = contrib.get("protocol")
        if proto and isinstance(proto, dict):
            protocols.append(proto)
            p_name = proto.get("name", h_type)
            for f in proto.get("fields", []):
                field_copy = dict(f)
                field_copy["protocol"] = p_name
                field_copy["contributor"] = h_type
                common_fields.append(field_copy)

    return {
        "protocols": protocols,
        "common_fields": common_fields,
    }
