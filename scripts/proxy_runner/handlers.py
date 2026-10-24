"""
Handler role and metadata descriptors for pipeline inspection and simulation coordination.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Optional


class HandlerRole(str, Enum):
    PROCESSOR = "processor"
    GENERATOR = "generator"
    EXTERNAL_INGRESS = "external_ingress"
    EXTERNAL_EGRESS = "external_egress"


class HandlerProtocol(str, Enum):
    NONE = "none"
    UDP = "udp"
    HTTP = "http"


@dataclass(frozen=True)
class HandlerDescriptor:
    name: str
    role: HandlerRole
    protocol: HandlerProtocol

    @property
    def is_generator(self) -> bool:
        return self.role == HandlerRole.GENERATOR

    @property
    def is_external_ingress(self) -> bool:
        return self.role == HandlerRole.EXTERNAL_INGRESS

    @property
    def is_external_egress(self) -> bool:
        return self.role == HandlerRole.EXTERNAL_EGRESS


def load_handler_descriptor(
    handler_type: str, bin_dirs: Optional[list[Path | str]] = None
) -> HandlerDescriptor:
    """
    Attempts to load a decoupled <handler_type>.json sidecar descriptor from known binary directories.
    Falls back to intelligent defaults if the sidecar is not found.
    """
    if bin_dirs:
        for b_dir in bin_dirs:
            p = Path(b_dir) / f"{handler_type}.json"
            if p.is_file():
                try:
                    with open(p, "r", encoding="utf-8") as f:
                        data = json.load(f)
                        return HandlerDescriptor(
                            name=data.get("name", handler_type),
                            role=HandlerRole(data.get("role", "processor")),
                            protocol=HandlerProtocol(data.get("protocol", "none")),
                        )
                except Exception:
                    pass

    # Built-in fallback defaults in case binary output dir is not specified or pre-build
    if handler_type == "dummy_send":
        return HandlerDescriptor(handler_type, HandlerRole.GENERATOR, HandlerProtocol.NONE)
    elif handler_type == "udp_ingress_handler":
        return HandlerDescriptor(handler_type, HandlerRole.EXTERNAL_INGRESS, HandlerProtocol.UDP)
    elif handler_type == "udp_egress_handler":
        return HandlerDescriptor(handler_type, HandlerRole.EXTERNAL_EGRESS, HandlerProtocol.UDP)

    return HandlerDescriptor(handler_type, HandlerRole.PROCESSOR, HandlerProtocol.NONE)

