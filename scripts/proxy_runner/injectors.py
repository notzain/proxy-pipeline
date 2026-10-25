"""
Pluggable packet injectors for feeding mock protocol packets (UDP, HTTP, raw IPC)
into pipeline handlers during simulation.
"""

from __future__ import annotations

import socket
from abc import ABC, abstractmethod
from typing import Any

from .pcap import PcapPacket


class BasePacketInjector(ABC):
    """
    Abstract strategy for injecting packets or envelopes into a handler during simulation.
    """

    @abstractmethod
    def inject(self, handler_spec: dict[str, Any], packets: list[PcapPacket]) -> bool:
        """
        Injects the given packets into the handler.
        Returns True if successful, False otherwise.
        """
        raise NotImplementedError


class UdpPacketInjector(BasePacketInjector):
    """
    Injects raw UDP datagram payloads directly into the network port the handler is listening on.
    """

    def inject(self, handler_spec: dict[str, Any], packets: list[PcapPacket]) -> bool:
        port = handler_spec.get("config", {}).get("ingress.port", 9000)
        host = handler_spec.get("config", {}).get("ingress.host", "127.0.0.1")
        if host == "0.0.0.0":
            host = "127.0.0.1"

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            for p in packets:
                s.sendto(p.raw_payload, (host, int(port)))
            s.close()
            return True
        except Exception as ex:
            print(f"[UdpPacketInjector] Error injecting datagram to {host}:{port}: {ex}")
            return False


class UnixSocketPacketInjector(BasePacketInjector):
    """
    Injects packed pipeline envelopes directly into an internal Unix domain socket input channel.
    """

    def inject(self, handler_spec: dict[str, Any], packets: list[PcapPacket]) -> bool:
        in_ch = handler_spec.get("input_channel")
        if not in_ch and handler_spec.get("input_channels"):
            chs = handler_spec["input_channels"]
            in_ch = chs[0] if isinstance(chs, list) and chs else str(chs).split(",")[0].strip()

        if not in_ch:
            return False

        sock_path = f"/tmp/proxy_sim_{in_ch}.sock"
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            for p in packets:
                env = p.pack_envelope()
                s.sendto(env, sock_path)
            s.close()
            return True
        except Exception as ex:
            print(f"[UnixSocketPacketInjector] Error delivering to {sock_path}: {ex}")
            return False


def get_injector_for_handler(
    handler_spec: dict[str, Any], is_external_ingress: bool, protocol: str
) -> BasePacketInjector:
    """
    Returns the appropriate packet injector strategy for the handler.
    """
    if is_external_ingress and protocol == "udp":
        return UdpPacketInjector()
    elif is_external_ingress and protocol == "http":
        # Future: return HttpPacketInjector()
        return UdpPacketInjector()
    return UnixSocketPacketInjector()

